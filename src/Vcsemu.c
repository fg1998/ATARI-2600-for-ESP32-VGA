#include "options.h"
#include "Vcsemu.h"
#include "types.h"
#include "vmachine.h"
#include "display.h"

#include <string.h>
#include "emuapi.h"

/****************************************************************************
* Local macros / typedefs
****************************************************************************/

/****************************************************************************
* Global data
****************************************************************************/

/****************************************************************************
* Imported procedures
****************************************************************************/
extern void mainloop(void);

/****************************************************************************
* Local procedures
****************************************************************************/

/****************************************************************************
* Exported procedures
****************************************************************************/
void vcs_Init(void)
{
  init_machine();	
  init_hardware();
  tv_on();
}


void vcs_Start(char * filename)
{
  int size = emu_LoadFile(filename, (char *)theCart, 16384); 
  

  if (size > 16384)
    size = 16384;
	
  rom_size = size;
  if (size == 2048)
  {
    memcpy (&theCart[2048], &theCart[0], 2048);
    rom_size = 4096;
  }
  else if (size < 2048)
  {
    theCart[0x0ffc] = 0x00;
    theCart[0x0ffd] = 0xf0;
    rom_size = 4096;
  }

  if (filename)
    {
      strncpy (base_opts.filename, filename, sizeof base_opts.filename - 1);
      base_opts.filename[sizeof base_opts.filename - 1] = 0;
    }

  init_hardware();  /* já chama init_banking() com rom_size certo */
  /* Decathlon e a maioria dos carts so' saem do vetor de reset depois
   * de um pulso no switch RESET (ativo em baixo). O host ja fazia isto
   * nos 8 primeiros quadros; na VGA32 o F11 era manual. */
  g_console_reset_frames = 8;
}


void vcs_Step(void)
{
  mainloop();
  if (g_console_reset_frames > 0)
    g_console_reset_frames--;
}






