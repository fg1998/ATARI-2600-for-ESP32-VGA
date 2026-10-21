/*****************************************************************************

   This file is part of x2600, the Atari 2600 Emulator
   ===================================================
   
   Copyright 1996 Alex Hornby. For contributions see the file CREDITS.

   This software is distributed under the terms of the GNU General Public
   License. This is free software with ABSOLUTELY NO WARRANTY.
   
   See the file COPYING for details.
   
   $Id: vmachine.c,v 2.22 1997/11/22 14:27:47 ahornby Exp $
******************************************************************************/
//This file was modified from its original version for use in PocketVCS
// by Stuart Russell

/* 
   The virtual machine. Contains the RIOT timer code, and hardware 
   initialisation.
 */

#include <stdio.h>
#include "types.h"
#include "address.h"
#include "options.h"
#include "display.h"
#include "raster.h"
#include "cpu.h"
#include "collision.h"
#include "sound.h"
#include "vmachine.h"
#include "tiasound.h"

#include "emuapi.h"
extern int pausing;   // At2600.c; setado em tv_display para sinalizar fim de quadro

#define snd 1

extern void BlitScreen(void);
extern int nOptions_SoundBufSize;
extern int nOptions_SoundOn;
int Touchpadx=0;
int Touchpady=0;
//extern int Touchpadx;
//extern int Touchpady;
extern int nOptions_Landscape;
extern int nOptions_SkipFrames;



/* The Rom define might need altering for paged carts */
/* Enough for 16k carts */
int rom_size;

/* Used as a simple file buffer to store the data */
// JMH
//BYTE theCart[16384];
//BYTE theCart[4096];
BYTE * theCart=0;

/* Scratch area for those banking types that require memcpy() */
//BYTE cartScratch[4096];
BYTE * cartScratch=0;
/* Area for those carts containing RAM */
//BYTE cartRam[1024];
BYTE * cartRam=0;

/* Pointer to start of ROM data */
BYTE *theRom;

BYTE theRam[128];
BYTE tiaRead[0x0e];
BYTE tiaWrite[0x2d];
BYTE keypad[2][4];

/* These don't strictly need so much space */
BYTE riotRead[0x298];
BYTE riotWrite[0x298];

/* 
   Hardware addresses not programmer accessible 
 */

/* Set if whole emulator is reset */
int reset_flag = 0;

/* The timer resolution, can be 1,8,64,1024 */
int timer_res = 32;
int timer_count = 0;
int timer_clks = 0;
int g_intim = 0;
int g_console_reset_frames = 0;
static CLOCK s_timer_start = 0;
static unsigned s_timer_interval = 1;
static unsigned s_timer_init = 0;
extern CLOCK clk;
extern int beamadj;

/* Electron beam position */
int ebeamx, ebeamy, sbeamx;

/* The state of the electron beam */
#define VSYNCSTATE 1
#define VBLANKSTATE 2
#define HSYNCSTATE 4
#define DRAWSTATE 8
#define OVERSTATE 16
int vbeam_state;		/* 1 2 8 or 16 */
int hbeam_state;		/* 4 8 or 16 */

// Flag para resetar a auto-deteccao PAL/NTSC a cada jogo (setado em init_hardware).
int g_resetPalDetect = 0;
int g_maxEbeamy = 0;   // maximo ebeamy medido no frame atual (para auto-deteccao PAL)

/* The tv size, varies with PAL/NTSC */
int tv_width, tv_height, tv_vsync, tv_vblank, tv_overscan, tv_frame, tv_hertz,
  tv_hsync;


PlayField pf[2];  

Paddle paddle[4];

Player pl[2];

Missile ml[3];



#define MAXLIST 80
/* The various display lists */
struct RasterChange pl_change[2][MAXLIST], pf_change[1][MAXLIST], unified[MAXLIST];

/* The display list counters */
int pl_change_count[2], pf_change_count[1], unified_count;


/***************************************************************************
                           Let the functions begin!
****************************************************************************/
void
init_machine (void)
{
  if (theCart == 0) theCart = (BYTE *)emu_Malloc(16384);
  if (cartScratch == 0) cartScratch = (BYTE *)emu_Malloc(4096);
  if (cartRam == 0) cartRam = (BYTE *)emu_Malloc(1024);
}

/* Device independent screen initialisations */
void
init_screen (void)
{
  
  /* Set the electron beam to the top left */
  ebeamx = -tv_hsync;
  ebeamy = 0;
  sbeamx = 0;
  vbeam_state = VSYNCSTATE;
  hbeam_state = OVERSTATE;

  tv_vsync = 3;
  tv_hsync = 68;
  switch (base_opts.tvtype) 
    {
    case NTSC:      
      tv_width = 160;
      tv_height = 192;
      tv_vblank = 40;
      // Era 30. Alguns jogos Activision (Enduro em especial) desenham o placar
      // ate' por volta da linha 232-234, o que passava dos 222 (192+30) que
      // tinhamos e cortava as ultimas linhas do placar. 40 cobre com folga sem
      // colar no limite pratico do blit (240 no VGA). Max seguro seria 48.
      tv_overscan = 40;
      tv_frame = 262;
      tv_hertz = 60;
    break;
    case PAL:
    case SECAM:
      tv_width = 160;
      tv_height = 228;
      tv_vblank = 48;
      tv_overscan = 36;
      tv_frame = 312;
      tv_hertz = 50;
      break;
    }

}

int g_emu_frames = 0;
int g_present_y = 0;
static int s_frame_presented = 0;

void tv_frame_arm (void)
{
  s_frame_presented = 0;
}

/* Completa o VBuf ate' o overscan com o estado atual do TIA. No hardware
 * o feixe continua saindo COLUBK mesmo sem WSYNC; sem isto a metade de
 * baixo fica no preto do startGame. Nao re-rasteriza se o feixe ja
 * deu a volta (ebeamy foi a zero no wrap). */
static void tv_finish_scanlines (void)
{
  int maxy = tv_height + tv_overscan;
  int y;
  if (ebeamy <= 0 || ebeamy >= maxy)
    return;
  for (y = ebeamy; y < maxy; y++)
    tv_raster (y);
}

/* vsync_end: present no STA VSYNC,#2 (fim real do quadro Activision).
 * Caso contrario (VBLANK bit1) so' blita se o visivel ja foi desenhado
 * — senao o kernel continua no mesmo/proximo step. */
static void tv_present (int vsync_end)
{
  int drawn = ebeamy;
  if (drawn <= 0)
    drawn = g_maxEbeamy;

  if (s_frame_presented) {
    pausing = 1;
    return;
  }
  if (vsync_end) {
    if (drawn < 8)
      return;
  } else if (drawn < tv_height) {
    return;
  }

  tv_finish_scanlines ();
  g_present_y = (ebeamy > 0) ? ebeamy : g_maxEbeamy;
  s_frame_presented = 1;
  g_emu_frames++;

  // Auto-deteccao de PAL/NTSC: mede o ebeamy maximo nos primeiros frames.
  // Se consistentemente > 240 scanlines/quadro -> PAL; senao -> NTSC.
  // Re-inicializa os parametros de video se o tipo mudar.
  {
    static int   s_frameCount  = 0;
    static int   s_maxY        = 0;
    static int   s_detected    = 0;

    if (g_resetPalDetect) {
      s_frameCount = 0;
      s_maxY       = 0;
      s_detected   = 0;
      g_resetPalDetect = 0;
      g_maxEbeamy  = 0;
      if (base_opts.tvtype != NTSC) {
        base_opts.tvtype = NTSC;
        init_screen();
        extern int tv_on(void); tv_on();
      }
    }

    if (!s_detected) {
      if (g_maxEbeamy > s_maxY) s_maxY = g_maxEbeamy;
      s_frameCount++;
      if (s_frameCount >= 8) {
        int newtype = (s_maxY > 240) ? PAL : NTSC;
        if (newtype != base_opts.tvtype) {
          printf("[autopal] maxY=%d -> %s\n", s_maxY, newtype==PAL?"PAL":"NTSC");
          base_opts.tvtype = newtype;
          init_screen();
          extern int tv_on(void); tv_on();
        } else {
          printf("[autopal] maxY=%d -> %s confirmado\n", s_maxY, newtype==PAL?"PAL":"NTSC");
        }
        s_detected = 1;
        fflush(stdout);
      }
    }
  }
  g_maxEbeamy = 0;

  // Visivel 192 + um pedaco do overscan (placar). Blitar 232 (192+40) na
  // VGA 240 deixava o quadro um pouco achatado — o Decathlon "deformado".
  {
    int h = tv_height + 24;
    if (h > tv_height + tv_overscan)
      h = tv_height + tv_overscan;
    emu_DrawScreen(VBuf, tv_width, h, tv_width);
  }
  emu_DrawVsync();
  pausing = 1;
}

/* Displays the tv screen (STA VBLANK,#2). */
void tv_display (void)
{
  tv_present (0);
}

void tv_display_vsync (void)
{
  tv_present (1);
}

/* Initialise the RIOT (also known as PIA) */
void
init_riot (void)
{
  int i;

  /* Reset the arrays */
  for(i=0; i< 0x298;i++)
    {
      riotRead[i]=0;
      riotWrite[i]=0;
    }

  /* Wipe the RAM */
  for (i = 0; i < 0x80; i++)
    theRam[i] = 0;

  /* Set the timer to zero */
  riotRead[INTIM] = 0;

  /* Set the joysticks and switches to input */
  riotWrite[SWACNT] = 0;
  riotWrite[SWBCNT] = 0;

  /* Centre the joysticks */
  riotRead[SWCHA] = 0xff;
  riotRead[SWCHB] = 0x0b;

  /* Set the counter resolution */
  timer_res = 32;
  timer_count = 0;
  timer_clks = 0;
  s_timer_start = 0;
  s_timer_interval = 1;
  s_timer_init = 0;
  g_intim = 0;
}

/* Initialise the television interface adaptor (TIA) */
void
init_tia (void)
{
  int i;
  for(i=0; i< 0x2d;i++)
    {
      tiaWrite[i]=0;
    }
  for(i=0; i< 0x0e;i++)
    {
      tiaRead[i]=0;
    }

  tiaWrite[CTRLPF] = 0x00;
  for (i = 0; i < 2; i++)
    {
      pl[i].hmm = 0x0;
      pl[i].x = 0x0;
      pl[i].nusize = 0;
      pl[i].grp = 0;
      pl[i].vdel = 0;
      pl[i].vdel_flag = 0;
      pl_change_count[i] = 0;
    }

  pl[0].mask = PL0_MASK;
  pl[1].mask = PL1_MASK;
  ml[0].mask = ML0_MASK;
  ml[1].mask = ML1_MASK;
  reset_collisions ();

  pf_change_count[0] = 0;
  unified_count = 0;
  for (i = 0; i < 3; i++)
    {
      ml[i].x = 0;
      ml[i].hmm = 0;
      ml[i].enabled = 0;
      ml[i].locked = 0;
      ml[i].width = 0;
      ml[i].vdel = 0;
      ml[i].vdel_flag = 0;
    }
  
  tiaWrite[VBLANK] = 0;
  tiaRead[INPT4] = 0x80;
  tiaRead[INPT5] = 0x80;

  /* Set up the colour table */
  colour_table[P0M0_COLOUR]= 0;
  colour_table[P1M1_COLOUR]= 0;
  colour_table[PFBL_COLOUR] = 0;
  colour_table[BK_COLOUR] = 0;
}

void
init_memory(void)
{
  int i;
  for(i=0;i<1024; i++)
    cartRam[i]=0;
  for(i=0; i<128;i++)
    theRam[i]=0;
}

extern void fe_reset(void);

static int cart_has_bytes (const BYTE *needle, int nlen)
{
  int i, j;
  if (!theCart || rom_size < nlen) return 0;
  for (i = 0; i <= rom_size - nlen; i++)
    {
      for (j = 0; j < nlen; j++)
	if (theCart[i + j] != needle[j])
	  break;
      if (j == nlen)
	return 1;
    }
  return 0;
}

static int contains_ci (const char *hay, const char *needle)
{
  int i, j;
  if (!hay || !needle || !needle[0]) return 0;
  for (i = 0; hay[i]; i++)
    {
      for (j = 0; needle[j]; j++)
	{
	  char a = hay[i + j], b = needle[j];
	  if (a >= 'A' && a <= 'Z') a += 32;
	  if (b >= 'A' && b <= 'Z') b += 32;
	  if (!a || a != b) break;
	}
      if (!needle[j]) return 1;
    }
  return 0;
}

/* Stella CartDetector: FE if one of these sequences is present AND the cart
 * is not a regular F8 (STA $1FF9 / STA $FFF9). Decathlon is the first sig. */
static int is_fe_cart (void)
{
  static const BYTE fe_sigs[][5] = {
    { 0x20, 0x00, 0xD0, 0xC6, 0xC5 }, /* JSR $D000; DEC $C5  Decathlon */
    { 0x20, 0xC3, 0xF8, 0xA5, 0x82 }, /* JSR $F8C3; LDA $82  Robot Tank */
    { 0xD0, 0xFB, 0x20, 0x73, 0xFE }, /* BNE *+0; JSR $FE73  Space Shuttle */
    { 0xD0, 0xFB, 0x20, 0x68, 0xFE },
    { 0x20, 0x00, 0xF0, 0x84, 0xD6 }  /* JSR $F000; STY $D6  Thwocker */
  };
  static const BYTE f8_sigs[][3] = {
    { 0x8D, 0xF9, 0x1F }, /* STA $1FF9 */
    { 0x8D, 0xF9, 0xFF }  /* STA $FFF9 */
  };
  int i, fe = 0, f8 = 0;
  if (rom_size != 8192) return 0;
  if (contains_ci (base_opts.filename, "decathlon") ||
      contains_ci (base_opts.filename, "robot tank") ||
      contains_ci (base_opts.filename, "robottank") ||
      contains_ci (base_opts.filename, "thwocker"))
    return 1;
  for (i = 0; i < (int)(sizeof fe_sigs / sizeof fe_sigs[0]); i++)
    if (cart_has_bytes (fe_sigs[i], 5)) { fe = 1; break; }
  for (i = 0; i < (int)(sizeof f8_sigs / sizeof f8_sigs[0]); i++)
    if (cart_has_bytes (f8_sigs[i], 3)) { f8 = 1; break; }
  return fe && !f8;
}

static const char *mapper_name (int bank)
{
  switch (bank)
    {
    case 0: return "4K";
    case 1: return "F8";
    case 2: return "F6";
    case 3: return "E0";
    case 4: return "FA";
    case 5: return "F6SC";
    case 6: return "FE/Activision";
    default: return "?";
    }
}

void
init_banking (void)
{
  // vcs_Init chama init_hardware antes do cartucho existir (rom_size=0).
  // theRom = &theCart[rom_size-4096] nesse caso aponta 4K ANTES do buffer.
  fe_reset ();
  if (rom_size <= 0) {
    theRom = theCart;
    base_opts.bank = 0;
    return;
  }

  // Auto-detecta o esquema de banco pelo tamanho da ROM, com FE (Decathlon)
  // antes do F8 generico para 8K.
  if (rom_size <= 4096)       base_opts.bank = 0;
  else if (rom_size == 8192)  base_opts.bank = is_fe_cart () ? 6 : 1;
  else if (rom_size == 12288) base_opts.bank = 4;
  else if (rom_size == 16384) base_opts.bank = 2;
  else                        base_opts.bank = 0;

  printf("[bank] rom_size=%d -> bank=%d (%s)\n",
         rom_size, base_opts.bank, mapper_name (base_opts.bank));
  fflush(stdout);

  /* F8/F6: last 4K holds the reset vector. FE: prefer the 4K whose
   * $FFFC aponta para o espaco do cartucho (A12=1). O banco 1 do
   * Decathlon tem vetor lixo (Stella); se os dois forem validos, banco 0. */
  if (base_opts.bank == 6) {
    unsigned v0 = theCart[0x0ffc] | ((unsigned)theCart[0x0ffd] << 8);
    unsigned v1 = theCart[0x1ffc] | ((unsigned)theCart[0x1ffd] << 8);
    int b0_ok = (v0 & 0x1000) != 0;
    int b1_ok = (v1 & 0x1000) != 0;
    int start = (b1_ok && !b0_ok) ? 1 : 0;
    theRom = &theCart[start * 4096];
    printf("[bank] FE reset vectors bank0=$%04X bank1=$%04X -> start %d\n",
           v0, v1, start);
    fflush(stdout);
  }
  else if (rom_size == 2048)
    theRom = &theCart[rom_size - 2048];
  else
    theRom = &theCart[rom_size - 4096];
  //JMH
  //switch(base_opts.bank)
  //  {
  //  case 3:
  //    /* Parker Brothers 8k E0 */
  //    memcpy(&cartScratch[0xc00],&theCart[0x1c00],1024);
  //    memcpy(&cartScratch[0],&theCart[0],3072);
  //    theRom=cartScratch;
  //    break;
  //  default:
  //    break;
  //  }
}

extern void init_cpu( ADDRESS addr);

/* Main hardware startup */
void
init_hardware (void)
{
//  dbg_message(DBG_NORMAL,"Setting Up hardware\n");
  g_resetPalDetect = 1;   // forca reset da auto-deteccao PAL/NTSC no proximo tv_display
  init_screen ();
  init_riot ();
  init_tia ();
  init_raster ();
  init_memory();
  init_banking();
  init_cpu (0xfffc);
}

/* Do a raster change */
__inline void
do_raster_change (int i, int type, int val, struct RasterChange *rc)
{
  rc->x = ebeamx + beamadj;
  rc->type = type;
  rc->val = val;
}

/* Do a raster change on the unified list */
/* type: type of change */
/* val: value of change */
__inline void
do_unified_change (int type, int val)
{
  if (unified_count < MAXLIST)
    {
      unified[unified_count].x = ebeamx + beamadj;
      unified[unified_count].type = type;
      unified[unified_count].val = val;
      unified_count++;
    }
}

/* Do a player raster change */
/* i: player to change. 0 or 1 */
/* type: type of change */
/* val: value of change */
__inline void
do_plraster_change (int i, int type, int val)
{
  int plc = pl_change_count[i];
  /*printf("Raster change i=%d, x=%d, type=%d, val=%d\n", i, x, type, val); */
  if (plc < MAXLIST)
    {
      do_raster_change (i, type, val, &pl_change[i][plc]);
      if (type == 1)
	pl_change[i][plc].x -= 3;
      pl_change_count[i]++;
    }
}

/* Do a playfield raster change */
/* i: playfield to change. Depreciated, as 0 is now only one used */
/* type: type of change */
/* val: value of change */
__inline void
do_pfraster_change (int i, int type, int val)
{
  int pfc = pf_change_count[i];
  /*  
     if(ebeamy>=100) {
     printf("Raster change i=%d, x=%d, type=%d, val=%d\n", 
     i, ebeamx+beamadj, type, val);
     //show();
     }
   */
  if (pfc < MAXLIST)
    {
      do_raster_change (i, type, val, &pf_change[i][pfc]);
      pf_change_count[i]++;
    }
}


/* Use a unified change */
/* rc: unified change structure to use */
__inline void
use_unified_change (struct RasterChange *rc)
{
  switch (rc->type)
    {
    case 0:
      /* P0MO colour */
      colour_table[P0M0_COLOUR] = rc->val;
      break;
    case 1:
      /* POM0 colour */
      colour_table[P1M1_COLOUR] = rc->val;
      break;
    case 2:
      /* PFBL colour */
      colour_table[PFBL_COLOUR] = rc->val;
      break;
    case 3:
      /* BK colour */
      colour_table[BK_COLOUR] = rc->val;
      break;
    case 4:
      /* Priority change Normal */
      if(rc->val)
	norm_val=1;
      else
	norm_val=0;
      colour_lookup=colour_ptrs[norm_val][scores_val];
      break;
    case 5:
      /* Priority change Scores */
      if(rc->val)
	{
	  if(rc->x < 80)
	    scores_val=1;
	  else
	    scores_val=2;
	}
      else
	scores_val=0;
      colour_lookup=colour_ptrs[norm_val][scores_val];
      break;
    }
}

/* Use a playfield change */
/* pl: playfield to change */
/* rc: change to make */
__inline void
use_pfraster_change (PlayField *pl, struct RasterChange *rc)
{
  switch (rc->type)
    {
    case 0:
      /* PF0 */
      pl->pf0 = rc->val;
      break;
    case 1:
      /* PF1 */
      pl->pf1 = rc->val;
      break;
    case 2:
      /* PF2 */
      pl->pf2 = rc->val;
      break;
    case 3:
      /* Reflection */
      pl->ref = rc->val;
      break;
    }
}

/* Use a player change */
/* pl: player to change */
/* rc: change to make */
__inline void
use_plraster_change ( Player *pl, struct RasterChange *rc)
{
  switch (rc->type)
    {
    case 0:
      /* GRP */
      pl->grp = rc->val;
      break;
      /* Vertical delay */
    case 1:
      pl->vdel = pl->grp;
      break;
    }
}


int
do_paddle (int padnum)
{
  int res = 0x00;
  int x=0;
  if ((tiaWrite[VBLANK] & 0x80) == 0)
    {
	if (!nOptions_Landscape){
		x=240-Touchpadx;
		x=x*66;
		if (paddle[padnum].val > x) 
		res=0x80;
		
		}else{
		x=320-Touchpady;
		x=x*50;
		if (paddle[padnum].val > x) 
		res=0x80;
		}
		
    if (x > clk)
	res = 0x00;
    }
  return res;
}

/* Calculate the keypad rows */
/* i.e. when reading from INPTx we don't know the row */
BYTE
do_keypad (int pad, int col)
{
  BYTE res= 0x80;

//  read_keypad(pad);
  
  /* Bottom row */
  if(pad==0) {
  if( (riotWrite[SWCHA] & 0x80) && keypad[pad][col]==3)
    res=0x00;
  /* Third row */
  if( (riotWrite[SWCHA] & 0x40) && keypad[pad][col]==2)
    res=0x00;
  if( (riotWrite[SWCHA] & 0x20) && keypad[pad][col]==1)
    res=0x00;
  if( (riotWrite[SWCHA] & 0x10) && keypad[pad][col]==0)
    res=0x00;
  } 
  else {
  /* Bottom row */
  if( (riotWrite[SWCHA] & 0x80) && keypad[pad][col]==3)
    res=0x00;
  /* Third row */
  if( (riotWrite[SWCHA] & 0x40) && keypad[pad][col]==2)
    res=0x00;
  if( (riotWrite[SWCHA] & 0x20) && keypad[pad][col]==1)
    res=0x00;
  if( (riotWrite[SWCHA] & 0x10) && keypad[pad][col]==0)
    res=0x00;
  }
  return res;
}


/* 
   Called when the timer is set .
   Note that res is the bit shift, not absolute value.
   Assumes that any timer interval set will last longer than the instruction
   setting it.
 */
/* res: timer interval resolution as a bit shift value */
/* count: the number of intervals to set */
/* clkadj: the number of CPU cycles into the current instruction  */
void
set_timer (int res, int count, int clkadj)
{
  /* RIOT: INTIM comeca no valor escrito e desce 1 a cada 1/8/64/1024
   * ciclos. O x2600 original devolvia o *decorrido* (0 no primeiro
   * LDA INTIM) — o Decathlon abortava o vblank/overscan e fechava o
   * quadro com ~117 scanlines (metade de cima da VGA). */
  timer_res = res;
  timer_count = (count & 0xff) << res;
  s_timer_interval = 1u << res;
  s_timer_init = (unsigned)(count & 0xff);
  s_timer_start = clk + (CLOCK)clkadj;
  timer_clks = (int)s_timer_start;
  g_intim = count & 0xff;
}

/* INTIM: valor restante, como o 6532 real. clkadj = ciclos desta insn. */
BYTE
do_timer (int clkadj)
{
  CLOCK now = clk + (CLOCK)clkadj;
  CLOCK elapsed, duration, after;
  unsigned done;

  if (s_timer_interval == 0)
    s_timer_interval = 1;
  if (now < s_timer_start)
    {
      g_intim = (int)s_timer_init;
      return (BYTE)s_timer_init;
    }
  elapsed = now - s_timer_start;
  duration = (CLOCK)s_timer_init * s_timer_interval;
  if (elapsed < duration)
    {
      done = (unsigned)(elapsed / s_timer_interval);
      g_intim = (int)(s_timer_init - done);
      return (BYTE)g_intim;
    }
  /* Atingiu 0: o RIOT passa a descer de $FF a cada ciclo. */
  after = elapsed - duration;
  g_intim = (int)((0x100u - (unsigned)(after % 256u)) & 0xff);
  return (BYTE)g_intim;
}

#ifdef snd
//extern unsigned char *sounddata;
//#define SoundBufSize 256
//unsigned char sounddata[SoundBufSize];
#endif

/* Do the screen related part of a write to VBLANK */
/* b: the byte written */
void
do_vblank (BYTE b)
{
	
  if (b & 0x02)
    {
      /* Start vertical blank */
      // NAO muda para VBLANKSTATE aqui: o beam continua em DRAWSTATE para que
      // o tv_raster continue sendo chamado nas linhas do overscan. Jogos como
      // o Enduro desenham o placar durante o VBLANK (linhas 192-221 no NTSC).
      // O estado VBLANKSTATE so' e' usado para o VSYNC (inicio do proximo quadro).
      // vbeam_state = VBLANKSTATE;  // <-- desabilitado
#ifdef snd
//	  Tia_process(sounddata, SoundBufSize);
//      CESound_play_sample(sounddata, nOptions_SoundBufSize);
#endif
      /* Also means we can update screen */
      tv_display ();
    }
  else
    {
      /* End vblank, and start first hsync drawing */
      int i;

      vbeam_state = DRAWSTATE;
      hbeam_state = HSYNCSTATE;
      /* Set up the screen */
      for (i = 0; i < unified_count; i++)
	use_unified_change (&unified[i]);
      /* Hope for a WSYNC, but just in case */
      ebeamx = -tv_hsync;
      ebeamy = 0;
    }
}


//int count = 1;

/* do a horizontal sync */
void
do_hsync (void)
{
  /* Only perform heavy stuff if electron beam is in correct position */
  if (vbeam_state == DRAWSTATE && (ebeamx > -tv_hsync))
    {
//      if (!(count--))
//      {
//        count = 2;
      tv_raster (ebeamy);
        
//      }
      /* Fix the clock value */
      clk += (ebeamx - tv_width) / 3;
      ebeamy++;
      if (ebeamy > g_maxEbeamy) g_maxEbeamy = ebeamy;
      if (ebeamy >= tv_height + tv_overscan)
	{
	  vbeam_state = OVERSTATE;
	  ebeamy = 0;
	}
    }
  hbeam_state = HSYNCSTATE;
  ebeamx = -tv_hsync;
  sbeamx = 0;
}

/* Main screen logic */
/* clks: CPU clock length of last instruction */
__inline void
do_screen (int clks)
{
  switch (vbeam_state)
    {
    case VSYNCSTATE:
      switch (hbeam_state)
	{
	case HSYNCSTATE:
	  ebeamx += clks * 3;
	  if (ebeamx >= 0)
	    {
	      hbeam_state = DRAWSTATE;
	    }
	  break;
	case DRAWSTATE:
	  ebeamx += clks * 3;
	  if (ebeamx >= tv_width)
	    {
	      ebeamx -= (tv_hsync + tv_width);
	      /* Insert hsync stuff here */
	      sbeamx = ebeamx;
	      hbeam_state = HSYNCSTATE;
	    }
	  break;
	case OVERSTATE:
	  break;
	}
      break;
    case VBLANKSTATE:
      // Durante o VBLANK, o jogo pode ainda estar desenhando nas linhas do
      // overscan (ex: o placar do Enduro nas linhas 192-221). Continuamos
      // chamando tv_raster para essas linhas em vez de so' avancar o beam.
      switch (hbeam_state)
	{
	case HSYNCSTATE:
	  ebeamx += clks * 3;
	  if (ebeamx >= 0)
	    {
	      hbeam_state = DRAWSTATE;
	    }
	  break;
	case DRAWSTATE:
	  ebeamx += clks * 3;
	  if (ebeamx >= tv_width)
	    {
	      ebeamx -= (tv_hsync + tv_width);
	      sbeamx = ebeamx;
	      tv_raster (ebeamy);    // renderiza overscan (placar etc)
	      ebeamy++;
	      if (ebeamy > g_maxEbeamy) g_maxEbeamy = ebeamy;
	      hbeam_state = HSYNCSTATE;
	    }
	  if (ebeamy >= tv_height + tv_overscan)
	    {
	      vbeam_state = OVERSTATE;
	      ebeamy = 0;
	    }
	  break;
	case OVERSTATE:
	  break;
	}
      break;
    case DRAWSTATE:
      switch (hbeam_state)
	{
	case HSYNCSTATE:
	  ebeamx += clks * 3;
	  if (ebeamx >= 0)
	    {
	      hbeam_state = DRAWSTATE;
	    }
	  break;
	case DRAWSTATE:
	  ebeamx += clks * 3;
	  /* Uma insn pode atravessar varias scanlines (espera do TIM64T
	   * sem WSYNC). O if unico do x2600 perdia essas linhas. */
	  while (ebeamx >= tv_width)
	    {
	      sbeamx = ebeamx;
	      ebeamx -= (tv_hsync + tv_width);
	      tv_raster (ebeamy);
	      ebeamy++;
	      if (ebeamy > g_maxEbeamy) g_maxEbeamy = ebeamy;
	      hbeam_state = HSYNCSTATE;
	      if (ebeamy >= tv_height + tv_overscan)
		{
		  vbeam_state = OVERSTATE;
		  ebeamy = 0;
		  break;
		}
	      if (ebeamx >= 0)
		hbeam_state = DRAWSTATE;
	    }
	  break;
	case OVERSTATE:
	  break;
	}
      break;
    case OVERSTATE:
      break;
    }
}