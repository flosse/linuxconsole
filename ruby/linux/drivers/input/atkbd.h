#ifndef _ATKBD_H
#define _ATKBD_H

/*
 * atkbd.h  Version 0.1
 *
 * Copyright (C) 1999 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

/*
 * Keyboard command definitions. Only commands used in the driver are
 * listed.
 */

#define ATKBD_CMD_SETLEDS	0x10ed
#define ATKBD_CMD_GSCANSET	0x11f0
#define ATKBD_CMD_SSCANSET	0x10f0
#define ATKBD_CMD_GETID		0x02f2
#define ATKBD_CMD_SETRATE	0x10f3
#define ATKBD_CMD_ENABLE	0x00f4
#define ATKBD_CMD_RESET_DIS	0x00f5
#define ATKBD_CMD_SETALL_MRB	0x00fa
#define ATKBD_CMD_SETALL_MB	0x00f8
#define ATKBD_CMD_SETKEY_M	0x10fd
#define ATKBD_CMD_NAK		0x00fe
#define ATKBD_CMD_RESET_BAT	0x01ff

/*
 * Keyboard return code definitions. Only return codes specifically handled
 * by the driver are listed.
 */

#define ATKBD_RET_ACK		0xfa
#define ATKBD_RET_NAK		0xfe

/*
 * These 'keycodes' are special, and handled by the driver. They are a part
 * of the scancode table, to allow changing their scancodes for oddball keyboards.
 */

#define ATKBD_KEY_UNKNOWN	  0
#define ATKBD_KEY_BAT		251
#define ATKBD_KEY_EMUL0		252
#define ATKBD_KEY_EMUL1		253
#define ATKBD_KEY_RELEASE	254
#define ATKBD_KEY_NULL		255

#endif _ATKBD_H
