/*****************************************************************************

   This file is part of Virtual VCS, the Atari 2600 Emulator
   ===================================================

   Copyright 1996 Daniel Boris. For contributions see the file CREDITS.

   This software is distributed under the terms of the GNU General Public
   License. This is free software with ABSOLUTELY NO WARRANTY.

   See the file COPYING for Details.

******************************************************************************/
/*
  This module has been completely re-written since X2600
  This has been hacked for the E-125 by Stuart Russell
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "keyboard.h"
#include "types.h"
#include "address.h"
#include "vmachine.h"
#include "extern.h"
#include "Memory.h"
#include "display.h"
#include "resource.h"

#include "emuapi.h"

// this returns a byte from the key state buffer. 
// example: KEY(0) will be expanded to: keys[SCANCODE_0]
//#define KEY(__a) keys[SCANCODE_##__a]

extern char *KeyboardGetstate(void);
extern int pausing;
extern int nOptions_Color;
extern int nOptions_P1Diff;
extern int nOptions_P2Diff;


void keyjoy(void) {
	int key = emu_ReadKeys();
	BYTE dir = 0x0f;

	/* SWCHA por jogador: bit0=UP bit1=DOWN bit2=LEFT bit3=RIGHT (ativo em baixo).
	 * O path antigo punha o stick no nibble ALTO (porta direita / P1) e ainda
	 * invertia LEFT/RIGHT. Decathlon e a maioria dos carts leem a porta
	 * ESQUERDA (P0, nibble baixo) — select/reset iam, o joystick nao. */
	if (key & MASK_JOY2_UP)    dir &= (BYTE)~0x01;
	if (key & MASK_JOY2_DOWN)  dir &= (BYTE)~0x02;
	if (key & MASK_JOY2_LEFT)  dir &= (BYTE)~0x04;
	if (key & MASK_JOY2_RIGHT) dir &= (BYTE)~0x08;

	if (emu_SwapJoysticks(1))
	  riotRead[SWCHA] = (BYTE)(0xf0 | dir);           /* J1 = P0 esquerda */
	else
	  riotRead[SWCHA] = (BYTE)((dir << 4) | 0x0f);    /* J2 = P1 direita */
}


void keycons(void) {
//---------------------------------------------------------
// This function reads the state of the joysticks (buttons)
//---------------------------------------------------------
	int key = emu_ReadKeys();
  int sw = emu_GetPad() & 0xff;

	riotRead[SWCHB] |= 0x03;

	// COLOR/B&W is a console switch (level), not an edge toggle.
	// F2 on the VGA32 latches MASK_KEY_USER3; if we flipped nOptions_Color
	// every frame while that bit stayed set, the picture would strobe.
	if ((key & MASK_KEY_USER3) || (sw == 2))
	  nOptions_Color = 0;
	else
	  nOptions_Color = 1;

	if (!nOptions_Color)
	  riotRead[SWCHB] &= 0xF7;		/* BW */
	else
		riotRead[SWCHB] |= 0x08;	/* Color */

	if (g_console_reset_frames > 0 || (key & MASK_KEY_USER1) || (sw == 4))
		riotRead[SWCHB] &= 0xFE;	/* Reset */
	if ((key & MASK_KEY_USER2) || (sw == 3))
		riotRead[SWCHB] &= 0xFD;	/* Select */

	if (nOptions_P1Diff) riotRead[SWCHB] &= 0xBF; 	/* P0 amateur */
	else riotRead[SWCHB] |= 0x40;   /* P0 pro */

	if (nOptions_P2Diff) riotRead[SWCHB] &= 0x7f;    /* P1 amateur */
	else riotRead[SWCHB] |= 0x80;	/* P1 pro */
}

void keytrig(void) {
	int key;
	
	// read the keyboard state. The return value (in keys) is a pointer to a
	// 256 byte buffer which holds the state of all the keyboard keys. If a 
	// byte's upper bit is set to 1, the key is pressed.
	
	key = emu_ReadKeys();

	if (!(tiaWrite[VBLANK] & 0x40)) {
		BYTE fire = (key & MASK_JOY2_BTN) ? 0x00 : 0x80;
		if (emu_SwapJoysticks(1)) {
		  tiaRead[INPT4] = fire;   /* P0 */
		  tiaRead[INPT5] = 0x80;
		} else {
		  tiaRead[INPT4] = 0x80;
		  tiaRead[INPT5] = fire;   /* P1 */
		}
	}
}