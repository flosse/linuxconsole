#ifndef _SUNKBD_H
#define _SUNKBD_H

/*
 * sunkbd.h  Version 0.1
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

#define SUNKBD_DEBUG_IO

/*
 * Keyboard command definitions. Only commands used in the driver are
 * listed.
 */

#define SUNKBD_CMD_RESET	0x1
#define SUNKBD_CMD_BELLON	0x2
#define SUNKBD_CMD_BELLOFF	0x3
#define SUNKBD_CMD_CLICK	0xa
#define SUNKBD_CMD_NOCLICK	0xb
#define SUNKBD_CMD_SETLED	0xe
#define SUNKBD_CMD_LAYOUT	0xf

/*
 * Keyboard return code definitions. Only return codes specifically handled
 * by the driver are listed.
 */

#define SUNKBD_RET_RESET	0xff
#define SUNKBD_RET_ALLUP	0x7f
#define SUNKBD_RET_LAYOUT	0xfe

/*
 * Keyboard bitmask definitions.
 */

#define SUNKBD_LAYOUT_5_MASK	0x20
#define SUNKBD_RELEASE		0x80
#define SUNKBD_KEY		0x7f


#endif _SUNKBD_H
