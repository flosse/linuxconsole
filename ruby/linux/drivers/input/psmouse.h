#ifndef _PSMOUSE_H
#define _PSMOUSE_H

/*
 * psmouse.h  Version 0.1
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
 * Mouse command definitions. Only commands used in the driver are
 * listed.
 */

#define PSMOUSE_CMD_READPACKET	0x0300
#define PSMOUSE_CMD_SETSCALE11	0x00e6
#define PSMOUSE_CMD_SETSCALE21	0x00e7
#define PSMOUSE_CMD_SETRES	0x10e8
#define PSMOUSE_CMD_GETINFO	0x03e9
#define PSMOUSE_CMD_SETSTREAM	0x00ea
#define PSMOUSE_CMD_POLL	0x03eb	
#define PSMOUSE_CMD_POLLWHEEL	0x04eb	
#define PSMOUSE_CMD_GETID	0x01f2
#define PSMOUSE_CMD_SETRATE	0x10f3
#define PSMOUSE_CMD_ENABLE	0x00f4
#define PSMOUSE_CMD_RESET_DIS	0x00f6
#define PSMOUSE_CMD_NAK		0x00fe
#define PSMOUSE_CMD_BAT		0x02ff

/*
 * Mouse return code definitions. Only return codes specifically handled
 * by the driver are listed.
 */

#define PSMOUSE_RET_BAT		0xaa
#define PSMOUSE_RET_ACK		0xfa
#define PSMOUSE_RET_NAK		0xfe

/*
 * Mouse flags definitions. These describe the type of the mouse.
 */

#define PSMOUSE_FLAG_WHEEL	0x01
#define PSMOUSE_FLAG_3BUTTON	0x02

#endif _PSMOUSE_H
