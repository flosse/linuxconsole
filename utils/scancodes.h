/*
 * scancodes.h  Version 0.1
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */


struct scancode_list { 
	unsigned int xt;
	unsigned int at2;
	unsigned int at3;
	unsigned int sun;
	unsigned int usb;
	unsigned int adb;
	unsigned int amiga;
	unsigned int hp300;
	unsigned int atari;
	unsigned int unused;
	unsigned int code;
 };

/*
 * The base set common to all keyboards - the XT keyboard.
 */

static struct scancode_list scancodes[] = { 

	{ 0x001, 0x076, 0x08, 0x1d, 0x29, 0x35, 0x45, 0x1f, 0x01,    0,   1 },	/* Esc */
	{ 0x002, 0x016, 0x16, 0x1e, 0x1e, 0x12, 0x01, 0x3e, 0x02,    0,   2 },	/* 1 ! */
	{ 0x003, 0x01e, 0x1e, 0x1f, 0x1f, 0x13, 0x02, 0x3d, 0x03,    0,   3 },	/* 2 @ */
	{ 0x004, 0x026, 0x26, 0x20, 0x20, 0x14, 0x03, 0x3c, 0x04,    0,   4 },	/* 3 # */
	{ 0x005, 0x025, 0x25, 0x21, 0x21, 0x15, 0x04, 0x3b, 0x05,    0,   5 },	/* 4 $ */
	{ 0x006, 0x02e, 0x2e, 0x22, 0x22, 0x17, 0x05, 0x3a, 0x06,    0,   6 },	/* 5 % */
	{ 0x007, 0x036, 0x36, 0x23, 0x23, 0x16, 0x06, 0x39, 0x07,    0,   7 },	/* 6 ^ */
	{ 0x008, 0x03d, 0x3d, 0x24, 0x24, 0x1a, 0x07, 0x38, 0x08,    0,   8 },	/* 7 & */
	{ 0x009, 0x03e, 0x3e, 0x25, 0x25, 0x1c, 0x08, 0x58, 0x09,    0,   9 },	/* 8 * */
	{ 0x00a, 0x046, 0x46, 0x26, 0x26, 0x19, 0x09, 0x59, 0x0a,    0,  10 },	/* 9 ( */
	{ 0x00b, 0x045, 0x45, 0x27, 0x27, 0x1d, 0x0a, 0x5a, 0x0b,    0,  11 },	/* 0 ) */
	{ 0x00c, 0x04e, 0x4e, 0x28, 0x2d, 0x1b, 0x0b, 0x5b, 0x0c,    0,  12 },	/* - _ */
	{ 0x00d, 0x055, 0x55, 0x29, 0x2e, 0x18, 0x0c, 0x5c, 0x0d,    0,  13 },	/* = + */
	{ 0x00e, 0x066, 0x66, 0x2b, 0x2a, 0x33, 0x41, 0x5d, 0x0e,    0,  14 },	/* Backspace */
	{ 0x00f, 0x00d, 0x0d, 0x35, 0x2b, 0x30, 0x42, 0x37, 0x0f,    0,  15 },	/* Tab */
	{ 0x010, 0x015, 0x15, 0x36, 0x14, 0x0c, 0x10, 0x36, 0x10,    0,  16 },	/* Q */
	{ 0x011, 0x01d, 0x1d, 0x37, 0x1a, 0x0d, 0x11, 0x35, 0x11,    0,  17 },	/* W */
	{ 0x012, 0x024, 0x24, 0x38, 0x08, 0x0e, 0x12, 0x34, 0x12,    0,  18 },	/* E */
	{ 0x013, 0x02d, 0x2d, 0x39, 0x15, 0x0f, 0x13, 0x33, 0x13,    0,  19 },	/* R */
	{ 0x014, 0x02c, 0x2c, 0x3a, 0x17, 0x11, 0x14, 0x32, 0x14,    0,  20 },	/* T */
	{ 0x015, 0x035, 0x35, 0x3b, 0x1c, 0x10, 0x15, 0x31, 0x15,    0,  21 },	/* Y */
	{ 0x016, 0x03c, 0x3c, 0x3c, 0x18, 0x20, 0x16, 0x30, 0x16,    0,  22 },	/* U */
	{ 0x017, 0x043, 0x43, 0x3d, 0x0c, 0x22, 0x17, 0x60, 0x17,    0,  23 },	/* I */
	{ 0x018, 0x044, 0x44, 0x3e, 0x12, 0x1f, 0x18, 0x61, 0x18,    0,  24 },	/* O */
	{ 0x019, 0x04d, 0x4d, 0x3f, 0x13, 0x23, 0x19, 0x62, 0x19,    0,  25 },	/* P */
	{ 0x01a, 0x054, 0x54, 0x40, 0x2f, 0x21, 0x1a, 0x63, 0x1a,    0,  26 },	/* [ {  */
	{ 0x01b, 0x05b, 0x5b, 0x41, 0x30, 0x1e, 0x1b, 0x64, 0x1b,    0,  27 },	/* ]  } */
	{ 0x01c, 0x05a, 0x5a, 0x59, 0x28, 0x24, 0x44, 0x6d, 0x1c,    0,  28 },	/* Enter */
	{ 0x01d, 0x014, 0x11, 0x4c, 0xe0, 0x36, 0x63, 0x06, 0x1d,    0,  29 },	/* Left Control */
	{ 0x01e, 0x01c, 0x1c, 0x4d, 0x04, 0x80, 0x20, 0x2d, 0x1e,    0,  30 },	/* A */
	{ 0x01f, 0x01b, 0x1b, 0x4e, 0x16, 0x01, 0x21, 0x2c, 0x1f,    0,  31 },	/* S */
	{ 0x020, 0x023, 0x23, 0x4f, 0x07, 0x02, 0x22, 0x2b, 0x20,    0,  32 },	/* D */
	{ 0x021, 0x02b, 0x2b, 0x50, 0x09, 0x03, 0x23, 0x2a, 0x21,    0,  33 },	/* F */
	{ 0x022, 0x034, 0x34, 0x51, 0x0a, 0x05, 0x24, 0x29, 0x22,    0,  34 },	/* G */
	{ 0x023, 0x033, 0x33, 0x52, 0x0b, 0x04, 0x25, 0x28, 0x23,    0,  35 },	/* H */
	{ 0x024, 0x03b, 0x3b, 0x53, 0x0d, 0x26, 0x26, 0x68, 0x24,    0,  36 },	/* J */
	{ 0x025, 0x042, 0x42, 0x54, 0x0e, 0x28, 0x27, 0x69, 0x25,    0,  37 },	/* K */
	{ 0x026, 0x04b, 0x4b, 0x55, 0x0f, 0x25, 0x28, 0x6a, 0x26,    0,  38 },	/* L */
	{ 0x027, 0x04c, 0x4c, 0x56, 0x33, 0x29, 0x29, 0x6b, 0x27,    0,  39 },	/* ; : */
	{ 0x028, 0x052, 0x52, 0x57, 0x34, 0x27, 0x2a, 0x6c, 0x28,    0,  40 },	/* ' " */
	{ 0x029, 0x00e, 0x0e, 0x2a, 0x35, 0x32,    0, 0x3f, 0x29,    0,  41 },	/* ` ~ */
	{ 0x02a, 0x012, 0x12, 0x63, 0xe1, 0x38, 0x60, 0x04, 0x2a,    0,  42 },	/* Left Shift */
	{ 0x02b, 0x05d, 0x5c, 0x58, 0x31, 0x2a, 0x0d, 0x65, 0x2b,    0,  43 },	/* \ | */
	{ 0x02c, 0x01a, 0x1a, 0x64, 0x1d, 0x06, 0x31, 0x22, 0x2c,    0,  44 },	/* Z */
	{ 0x02d, 0x022, 0x22, 0x65, 0x1b, 0x07, 0x32, 0x21, 0x2d,    0,  45 },	/* X */
	{ 0x02e, 0x021, 0x21, 0x66, 0x06, 0x08, 0x33, 0x20, 0x2e,    0,  46 },	/* C */
	{ 0x02f, 0x02a, 0x2a, 0x67, 0x19, 0x09, 0x34, 0x19, 0x2f,    0,  47 },	/* V */
	{ 0x030, 0x032, 0x32, 0x68, 0x05, 0x0b, 0x35, 0x18, 0x30,    0,  48 },	/* B */
	{ 0x031, 0x031, 0x31, 0x69, 0x11, 0x2d, 0x36, 0x78, 0x31,    0,  49 },	/* N */
	{ 0x032, 0x03a, 0x3a, 0x6a, 0x10, 0x2e, 0x37, 0x70, 0x32,    0,  50 },	/* M */
	{ 0x033, 0x041, 0x41, 0x6b, 0x36, 0x2b, 0x38, 0x71, 0x33,    0,  51 },	/* , < */
	{ 0x034, 0x049, 0x49, 0x6c, 0x37, 0x2f, 0x39, 0x72, 0x34,    0,  52 },	/* . > */
	{ 0x035, 0x04a, 0x4a, 0x6d, 0x38, 0x2c, 0x3a, 0x73, 0x35,    0,  53 },	/* / ? */
	{ 0x036, 0x059, 0x59, 0x6e, 0xe5, 0x7b, 0x61, 0x05, 0x36,    0,  54 },	/* Right Shift */
	{ 0x037, 0x07c, 0x7e, 0x2f, 0x55, 0x43, 0x5d,    0, 0x66,    0,  55 },	/* KP * */
	{ 0x038, 0x011, 0x19, 0x13, 0xe2, 0x3a, 0x64, 0x01, 0x38,    0,  56 },	/* Left Alt */
	{ 0x039, 0x029, 0x29, 0x79, 0x2c, 0x31, 0x40, 0x79, 0x39,    0,  57 },	/* Space */
	{ 0x03a, 0x058, 0x14, 0x77, 0x39, 0x39, 0x62, 0x2f, 0x3a,    0,  58 },	/* Caps Lock */
	{ 0x03b, 0x005, 0x07, 0x05, 0x3a, 0x7a, 0x50, 0x4c, 0x3b,    0,  59 },	/* F1 */
	{ 0x03c, 0x006, 0x0f, 0x06, 0x3b, 0x78, 0x51, 0x4b, 0x3c,    0,  60 },	/* F2 */
	{ 0x03d, 0x004, 0x17, 0x08, 0x3c, 0x63, 0x52, 0x4a, 0x3d,    0,  61 },	/* F3 */
	{ 0x03e, 0x00c, 0x1f, 0x0a, 0x3d, 0x76, 0x53, 0x49, 0x3e,    0,  62 },	/* F4 */
	{ 0x03f, 0x003, 0x27, 0x0c, 0x3e, 0x60, 0x54, 0x51, 0x3f,    0,  63 },	/* F5 */
	{ 0x040, 0x00b, 0x2f, 0x0e, 0x3f, 0x61, 0x55, 0x52, 0x40,    0,  64 },	/* F6 */
	{ 0x041, 0x083, 0x37, 0x10, 0x40, 0x62, 0x56, 0x53, 0x41,    0,  65 },	/* F7 */
	{ 0x042, 0x00a, 0x3f, 0x11, 0x41, 0x64, 0x57, 0x54, 0x42,    0,  66 },	/* F8 */
	{ 0x043, 0x001, 0x47, 0x12, 0x42, 0x65, 0x58,    0, 0x43,    0,  67 },	/* F9 */
	{ 0x044, 0x009, 0x4f, 0x07, 0x43, 0x6d, 0x59,    0, 0x44,    0,  68 },	/* F10 */
	{ 0x045, 0x077, 0x76, 0x62, 0x53, 0x47, 0x5a,    0,    0,    0,  69 },	/* Num Lock */
	{ 0x046, 0x07e, 0x5f, 0x17, 0x47, 0x6b, 0x5b,    0,    0,    0,  70 },	/* Scroll Lock Break */
	{ 0x047, 0x06c, 0x6c, 0x44, 0x5f, 0x59, 0x3d,    0, 0x67,    0,  71 },	/* KP Home 7 */
	{ 0x048, 0x075, 0x75, 0x45, 0x60, 0x5b, 0x3e,    0, 0x68,    0,  72 },	/* KP Up 8 */
	{ 0x049, 0x07d, 0x7d, 0x46, 0x61, 0x5c, 0x3f,    0, 0x69,    0,  73 },	/* KP PgUp 9 */
	{ 0x04a, 0x07b, 0x84, 0x47, 0x56, 0x4e, 0x4a,    0, 0x4a,    0,  74 },	/* KP - */
	{ 0x04b, 0x06b, 0x6b, 0x5b, 0x5c, 0x56, 0x2d,    0, 0x6a,    0,  75 },	/* KP Left 4 */
	{ 0x04c, 0x073, 0x73, 0x5c, 0x5d, 0x57, 0x2e,    0, 0x6b,    0,  76 },	/* KP 5 */
	{ 0x04d, 0x074, 0x74, 0x5d, 0x5e, 0x58, 0x2f,    0, 0x6c,    0,  77 },	/* KP Right 6 */
	{ 0x04e, 0x079, 0x7c, 0x7d, 0x57, 0x45, 0x5e,    0, 0x4e,    0,  78 },	/* KP + */
	{ 0x04f, 0x069, 0x69, 0x70, 0x59, 0x53, 0x1d,    0, 0x6d,    0,  79 },	/* KP End 1 */
	{ 0x050, 0x072, 0x72, 0x71, 0x5a, 0x54, 0x1e,    0, 0x6e,    0,  80 },	/* KP Down 2 */
	{ 0x051, 0x07a, 0x7a, 0x72, 0x5b, 0x55, 0x1f,    0, 0x6f,    0,  81 },	/* KP PgDn 3 */
	{ 0x052, 0x070, 0x70, 0x5e, 0x62, 0x52, 0x0f,    0, 0x70,    0,  82 },	/* KP Ins 0 */
	{ 0x053, 0x071, 0x71, 0x32, 0x63, 0x41, 0x3c,    0, 0x71,    0,  83 },	/* KP Del . */
                                                                      
/*                                                                    
 * The most common extensions to the base.                           
 */                                                                   
                                                                      
	{ 0x057, 0x078, 0x56, 0x09, 0x44, 0x67, 0x5f,    0,    0,    0,  87 },	/* F11 */
	{ 0x058, 0x007, 0x5e, 0x0b, 0x45, 0x6f,    0,    0,    0,    0,  88 },	/* F12 */
	{ 0x11c, 0x15a, 0x79, 0x5a, 0x58, 0x4c, 0x43,    0, 0x72,    0,  96 },	/* KP Enter */
	{     0,     0,    0,    0,    0, 0x34,    0,    0,    0,    0,  96 },	/* KP Enter on Apple PowerBooks */
	{ 0x135, 0x14a, 0x77, 0x2e, 0x54, 0x4b, 0x5c,    0, 0x64,    0,  98 },	/* KP / */
	{ 0x138, 0x111, 0x39, 0x0d, 0xe6, 0x7c, 0x65, 0x02,    0,    0, 100 },	/* Right Alt (Graph) */
	{     0,     0, 0x58,    0,    0,    0,    0,    0,    0,    0, 100 },	/* Alt Graph - Sun PS/2 Type 5 */
	{ 0x153, 0x171, 0x64, 0x42, 0x4c, 0x75, 0x46,    0, 0x53,    0, 111 },	/* Delete */
	{ 0x200, 0x17e, 0x62, 0x15, 0x48, 0x71,    0,    0,    0,    0, 119 },	/* Pause Break */
	{     0, 0x177,    0,    0,    0,    0,    0,    0,    0,    0, 119 },	/* Alternate Pause Break */
                                                                      
/*                                                                    
 * The separate navigation keys                                       
 */                                                                   
                                                                      
	{ 0x147, 0x16c, 0x6e, 0x34, 0x4a, 0x73,    0,    0, 0x47,    0, 102 },	/* Home */
	{ 0x148, 0x175, 0x63, 0x14, 0x52, 0x3e, 0x4c, 0x7e, 0x48,    0, 103 },	/* Up */
	{ 0x149, 0x17d, 0x6f, 0x60, 0x4b, 0x74,    0,    0,    0,    0, 104 },	/* Page Up */
	{ 0x14b, 0x16b, 0x61, 0x18, 0x50, 0x3b, 0x4f, 0x7c, 0x4b,    0, 105 },	/* Left */
	{ 0x14d, 0x174, 0x6a, 0x1c, 0x4f, 0x3c, 0x4e, 0x7f, 0x4d,    0, 106 },	/* Right */
	{ 0x14f, 0x169, 0x65, 0x4a, 0x4d, 0x77,    0,    0,    0,    0, 107 },	/* End */
	{ 0x150, 0x172, 0x60, 0x1b, 0x51, 0x3d, 0x4d, 0x7d, 0x50,    0, 108 },	/* Down */
	{ 0x151, 0x17a, 0x6d, 0x7b, 0x4e, 0x79,    0,    0,    0,    0, 109 },	/* Page Down */
	{ 0x152, 0x170, 0x67, 0x2c, 0x49, 0x72,    0,    0, 0x52,    0, 110 },	/* Insert */
                                                                      
/*                                                                    
 * The OS keys                                                        
 */                                                                   
                                                                      
	{ 0x15b, 0x11f, 0x8b, 0x78, 0xe3, 0x37,    0,    0,    0,    0, 125 },	/* Left Meta (Win, Apple, Amiga) */
	{ 0x15c, 0x127, 0x8c, 0x7a, 0xe7, 0x37,    0,    0,    0,    0, 126 },	/* Right Meta (Win, Apple, Amiga) */
	{ 0x15d, 0x12f, 0x8d, 0x43, 0x65,    0,    0,    0,    0,    0, 127 },	/* Compose (Application) */
                                                                      
/*                                                                    
 * OS keys on a NCD PS/2 Sun keyboard                                 
 */                                                                   
                                                                      
	{     0,     0, 0x40,    0,    0,    0,    0,    0,    0,    0, 125 },	/* Left Meta (Win, Apple, Amiga) */ 
	{     0,     0, 0x48,    0,    0,    0,    0,    0,    0,    0, 126 },	/* Right Meta (Win, Apple, Amiga) */
	{     0,     0, 0x39,    0,    0,    0,    0,    0,    0,    0, 127 },	/* Compose (Application) */         
                                                                      
/*                                                                    
 * PrintScreen                                                        
 */                                                                   
                                                                      
	{ 0x12a, 0x17c, 0x57, 0x16, 0x46, 0x69,    0,    0,    0,    0,  99 },	/* PrintScreen SysRq */
	{     0, 0x084,    0,    0,    0,    0,    0,    0,    0,    0,  99 },	/* SysRq (PrintScreen+Alt) */
                                                                      
/*                                                                    
 * Right Ctrl                                                         
 */                                                                   
                                                                      
	{ 0x11d, 0x114, 0x58,    0, 0xe4, 0x7d,    0,    0,    0,    0,  97 },	/* Right Ctrl */
                                                                      
/*                                                                    
 * The European 102nd key                                             
 */                                                                   
                                                                      
	{ 0x056, 0x061, 0x13,    0, 0x64, 0x0a,    0,    0,    0,    0,  86 },	/* the 102nd key */
                                                                      
/*                                                                    
 * The European 103rd key                                             
 */                                                                   
                                                                      
	{ 0x02b,     0, 0x53, 0x58, 0x32, 0x2a,    0,    0,    0,    0,  84 },	/* the 103rd key */
	{     0,     0, 0x5d,    0,    0,    0,    0,    0,    0,    0,  84 },	/* the 103rd key */
                                                                      
/*                                                                    
 * Keys appearing with i8042 untranslation                           
 */                                                                   
                                                                      
	{     0, 0x002,    0,    0,    0,    0,    0,    0,    0,    0,  65 },	/* F7 */
	{     0, 0x07f,    0,    0,    0,    0,    0,    0,    0,    0,  99 },	/* SysRq (PrintScreen+Alt) */
                                                                      
/*                                                                    
 * Sun function keys                                                  
 */                                                                   
                                                                      
	{     0,     0, 0x0a, 0x01, 0x78,    0,    0,    0,    0,    0, 128 },	/* Stop */
	{ 0x05d,     0, 0x0b, 0x03, 0x79,    0,    0,    0,    0,    0, 129 },	/* Again */
	{ 0x05e,     0, 0x0c, 0x19, 0x76,    0,    0,    0,    0,    0, 130 },	/* Props */
	{ 0x05f,     0, 0x10, 0x1a, 0x7a,    0,    0,    0, 0x61,    0, 131 },	/* Undo */
	{ 0x062,     0, 0x13, 0x31, 0x77,    0,    0,    0,    0,    0, 132 },	/* Front */
	{     0,     0, 0x18, 0x33, 0x7c,    0,    0,    0,    0,    0, 133 },	/* Copy */
	{ 0x064,     0, 0x20, 0x48, 0x74,    0,    0,    0,    0,    0, 134 },	/* Open */
	{ 0x065,     0, 0x28, 0x49, 0x7d,    0,    0,    0,    0,    0, 135 },	/* Paste */
	{     0,     0, 0x30, 0x5f, 0x7e,    0,    0,    0,    0,    0, 136 },	/* Find */
	{     0,     0, 0x38, 0x61, 0x7b,    0,    0,    0,    0,    0, 137 },	/* Cut */
	{     0,     0, 0x09, 0x76, 0x75,    0,    0,    0, 0x62,    0, 138 },	/* Help */
                                                                      
/*                                                                    
 * Line Feed found on Sun Type 4 keyboard                            
 * Fn+KP Enter on Apple PowerBooks
 */                                                                   
                                                                      
	{ 0x05b,     0,    0, 0x6f,    0, 0x6e,    0,    0,    0,    0, 101 },	/* Line Feed */

#if 0
/*                                                                    
 * PP-06 keyboard extension keys                                      
 */                                                                   
                                                                      
	{ 0x065,     0,    0,    0,    0,    0,    0,    0,    0,    0,  87 },	/* F11 */
	{ 0x066,     0,    0,    0,    0,    0,    0,    0,    0,    0,  88 },	/* F12 */
	{ 0x06b,     0,    0,    0,    0,    0,    0,    0,    0,    0, 110 },	/* Insert */
	{ 0x06c,     0,    0,    0,    0,    0,    0,    0,    0,    0, 111 },	/* Delete */
	{ 0x06d,     0,    0,    0,    0,    0,    0,    0,    0,    0, 103 },	/* Up */
	{ 0x06e,     0,    0,    0,    0,    0,    0,    0,    0,    0, 108 },	/* Down */
	{ 0x06f,     0,    0,    0,    0,    0,    0,    0,    0,    0, 105 },	/* Left */
	{ 0x070,     0,    0,    0,    0,    0,    0,    0,    0,    0, 106 },	/* Right */
#endif
                                                                      
/*                                                                    
 * F13-F24, for USB and Focus 9000                                    
 */                                                                   
                                                                      
	{ 0x055, 0x060,    0,    0, 0x68,    0,    0,    0,    0,    0,  85 },	/* F13 (PF1)  */
	{ 0x104, 0x051,    0,    0, 0x69,    0,    0,    0,    0,    0,  89 },	/* F14 (PF2)  */
	{ 0x077, 0x062,    0,    0, 0x6a,    0,    0,    0,    0,    0,  90 },	/* F15 (PF3)  */
	{ 0x078, 0x063,    0,    0, 0x6b,    0,    0,    0,    0,    0,  91 },	/* F16 (PF4)  */
	{ 0x106, 0x064,    0,    0, 0x6c,    0,    0,    0,    0,    0,  92 },	/* F17 (PF5)  */
	{ 0x177, 0x065,    0,    0, 0x6d,    0,    0,    0,    0,    0,  93 },	/* F18 (PF6)  */
	{ 0x107, 0x067,    0,    0, 0x6e,    0,    0,    0,    0,    0,  94 },	/* F19 (PF7)  */
	{ 0x05a, 0x068,    0,    0, 0x6f,    0,    0,    0,    0,    0,  95 },	/* F20 (PF8)  */
	{ 0x074, 0x053,    0,    0, 0x70,    0,    0,    0,    0,    0, 120 },	/* F21 (PF9)  */
	{ 0x179, 0x06d,    0,    0, 0x71,    0,    0,    0,    0,    0, 121 },	/* F22 (PF10) */
	{ 0x06d, 0x050,    0,    0, 0x72,    0,    0,    0,    0,    0, 122 },	/* F23 (PF11) */
	{ 0x06f, 0x06f,    0,    0, 0x73,    0,    0,    0,    0,    0, 123 },	/* F24 (PF12) */
                                                                      
/*                                                                    
 * F13-F17 for DEC LK450                                              
 */                                                                   
                                                                      
	{     0, 0x104,    0,    0,    0,    0,    0,    0,    0,    0,  85 },	/* F13 */
	{     0, 0x10c,    0,    0,    0,    0,    0,    0,    0,    0,  89 },	/* F14 */
	{     0, 0x103,    0,    0,    0,    0,    0,    0,    0,    0,  90 },	/* F15 */
	{     0, 0x10b,    0,    0,    0,    0,    0,    0,    0,    0,  91 },	/* F16 */
	{     0, 0x102,    0,    0,    0,    0,    0,    0,    0,    0,  92 },	/* F17 */
                                                                      
/*                                                                    
 * A1-A4 for RC930                                                    
 */                                                                   
                                                                      
	{     0, 0x00f,    0,    0,    0,    0,    0,    0,    0,    0,  85 },	/* A1 */
	{     0, 0x017,    0,    0,    0,    0,    0,    0,    0,    0,  89 },	/* A2 */
	{     0, 0x01f,    0,    0,    0,    0,    0,    0,    0,    0,  90 },	/* A3 */
	{     0, 0x027,    0,    0,    0,    0,    0,    0,    0,    0,  91 },	/* A4 */
                                                                      
/*                                                                    
 * Unlabeled keys on the NCD Sun PS/2 keyboard                       
 */                                                                   
                                                                      
	{     0,     0, 0x7f,    0,    0,    0,    0,    0,    0,    0,  85 },	/* U1 */
	{     0,     0, 0x80,    0,    0,    0,    0,    0,    0,    0,  89 },	/* U2 */
	{     0,     0, 0x81,    0,    0,    0,    0,    0,    0,    0,  90 },	/* U3 */
	{     0,     0, 0x82,    0,    0,    0,    0,    0,    0,    0,  91 },	/* U4 */
	{     0,     0, 0x83,    0,    0,    0,    0,    0,    0,    0,  92 },	/* U5 */
                                                                      
/*                                                                    
 * Macro key on BTC and other keyboards                               
 */                                                                   
                                                                      
	{ 0x16f, 0x16f,    0,    0,    0,    0,    0,    0,    0,    0, 112 },	/* BTC Macro key */
                                                                      
/*                                                                    
 * Logitech key on Logitech NewTouch                                  
 */                                                                   
                                                                      
	{     0, 0x128, 0x8e,    0,    0,    0,    0,    0,    0,    0, 112 },	/* Logitech key */
                                                                      
/*                                                                    
 * Omni key on NorthGate and Lueck keyboards                         
 */                                                                   
                                                                      
	{     0, 0x173,    0,    0,    0,    0,    0,    0,    0,    0, 112 },	/* NorthGate / Lueck Omni */
                                                                      
/*                                                                    
 * Sound (NCD Sun PS/2 + Sun Type 5 keyboard)                        
 */                                                                   
                                                                      
	{     0,     0, 0x50, 0x2d, 0x7f,    0,    0,    0,    0,    0, 113 },	/* Mute */
	{     0,     0, 0x51, 0x02, 0x81,    0,    0,    0,    0,    0, 114 },	/* Volume Down */
	{     0,     0, 0x53, 0x04, 0x80,    0,    0,    0,    0,    0, 115 },	/* Volume Up */
                                                                      
/*                                                                    
 * Sound (BTC Ez multimedia keyboard + Qtronic Scorpcion pcion pcion USB keyboard)
 */                                                                   
                                                                      
	{ 0x126, 0x14b, 0x9c,    0, 0xef,    0,    0,    0,    0,    0, 113 },	/* Mute */
	{ 0x125, 0x142, 0x9d,    0, 0xee,    0,    0,    0,    0,    0, 114 },	/* Volume Down */
	{ 0x11e, 0x11c, 0x95,    0, 0xed,    0,    0,    0,    0,    0, 115 },	/* Volume Up */
                                                                      
/*                                                                    
 * MS Internet Keyboard keys + Qtronic Scorpion USB keybo keybo keyboard
 */                                                                   
                                                                      
	{ 0x16a, 0x138, 0x38,    0, 0xf1,    0,    0,    0,    0,    0, 158 },	/* Back */
	{ 0x169, 0x130, 0x30,    0, 0xf2,    0,    0,    0,    0,    0, 159 },	/* Forward */
	{ 0x168, 0x128, 0x28,    0, 0xf3,    0,    0,    0,    0,    0, 128 },	/* Stop */
	{ 0x16c, 0x148, 0x48,    0,    0,    0,    0,    0,    0,    0, 155 },	/* Mail */
	{ 0x165, 0x110, 0x10,    0, 0xf4,    0,    0,    0,    0,    0, 136 },	/* Search */
	{ 0x166, 0x118, 0x18,    0,    0,    0,    0,    0,    0,    0, 156 },	/* Favorites */
	{ 0x132, 0x13a, 0x97,    0, 0xf0,    0,    0,    0,    0,    0, 150 },	/* Web/Home */
	{ 0x16b, 0x140, 0x40,    0,    0,    0,    0,    0,    0,    0, 157 },	/* My Computer */
	{ 0x121, 0x12b, 0x99,    0,    0,    0,    0,    0,    0,    0, 140 },	/* Calculator */
                                                                      
/*                                                                    
 * The power management keys (AP PFT keyboard, Qtronic Scnic Scnic Scorpion)
 */                                                                   
                                                                      
	{ 0x15e, 0x137,    0, 0x30, 0x66, 0x7f,    0,    0,    0,    0, 116 },	/* Power */
	{     0,     0,    0,    0,    0, 0x7e,    0,    0,    0,    0, 116 },	/* Alternate coding on some Apple kbds */
	{ 0x15f, 0x13f, 0x7f,    0, 0xf8,    0,    0,    0,    0,    0, 142 },	/* Sleep */
	{ 0x163, 0x15e,    0,    0,    0,    0,    0,    0,    0,    0, 143 },	/* WakeUp */
                                                                      
/*                                                                    
 * BTC Ez Function keys + Qtronic Scorpion                           
 */                                                                   
                                                                      
	{ 0x124, 0x13b, 0x94,    0, 0xea,    0,    0,    0,    0,    0, 165 },	/* Previous song */
	{ 0x11f, 0x12b, 0x99,    0, 0xe8,    0,    0,    0,    0,    0, 164 },	/* Play/Pause */
	{ 0x117, 0x143, 0x98,    0, 0xe9,    0,    0,    0,    0,    0, 166 },	/* Stop */
	{ 0x122, 0x134, 0x93,    0, 0xeb,    0,    0,    0,    0,    0, 163 },	/* Next song */
	{ 0x06c, 0x13a, 0x97,    0, 0xec,    0,    0,    0,    0,    0, 161 },	/* Eject */
	{ 0x123, 0x133, 0x9a,    0,    0,    0,    0,    0,    0,    0, 160 },	/* Close */
	{ 0x131, 0x131, 0x9e,    0,    0,    0,    0,    0,    0,    0, 167 },	/* Record */
	{ 0x118, 0x144, 0x9f,    0,    0,    0,    0,    0,    0,    0, 168 },	/* Rewind */
	{ 0x130, 0x132, 0x91,    0,    0,    0,    0,    0,    0,    0, 139 },	/* Menu */
	{     0, 0x11b, 0xa3,    0, 0xfb,    0,    0,    0,    0,    0, 140 },	/* Calc */
	{ 0x113, 0x12d, 0xa2,    0,    0,    0,    0,    0,    0,    0, 147 },	/* X-fer */
	{     0, 0x121, 0x92,    0,    0,    0,    0,    0,    0,    0, 150 },	/* WWW */
	{ 0x120, 0x123, 0x9b,    0,    0,    0,    0,    0,    0,    0, 154 },	/* Cycle Windows */
	{ 0x112, 0x124, 0x96,    0, 0xf9,    0,    0,    0,    0,    0, 152 },	/* Coffee */
	{ 0x119, 0x14d, 0xa0,    0,    0,    0,    0,    0,    0,    0, 148 },	/* Prog 1 */
	{ 0x110, 0x115, 0xa1,    0,    0,    0,    0,    0,    0,    0, 149 },	/* Prog 2 */
                                                                      
/*                                                                    
 * NCD Sun PS/2 Setup key                                             
 */                                                                   
                                                                      
	{ 0x066,     0, 0x5d,    0,    0,    0,    0,    0,    0,    0, 141 },	/* Setup */
                                                                      
/*                                                                    
 * Turbo-Xwing function keys.                                         
 */                                                                   
                                                                      
	{     0, 0x162,    0,    0,    0,    0,    0,    0,    0,    0, 116 },	/* WWW / Power */
	{     0, 0x133,    0,    0,    0,    0,    0,    0,    0,    0, 142 },	/* Sleep */
	{     0, 0x164,    0,    0,    0,    0,    0,    0,    0,    0, 143 },	/* Joystick / Wake */
	{     0, 0x157,    0,    0,    0,    0,    0,    0,    0,    0, 140 },	/* Calc */
	{     0, 0x10c,    0,    0,    0,    0,    0,    0,    0,    0, 101 },	/* Line Feed */
	{ 0x13c, 0x106,    0,    0,    0,    0,    0,    0,    0,    0, 137 },	/* Cut */
	{ 0x178, 0x167,    0,    0,    0,    0,    0,    0,    0,    0, 133 },	/* Copy */
	{     0, 0x134,    0,    0,    0,    0,    0,    0,    0,    0, 135 },	/* Paste */
	{ 0x162, 0x14f,    0,    0,    0,    0,    0,    0,    0,    0, 138 },	/* Help */
	{ 0x067, 0x10d,    0,    0,    0,    0,    0,    0,    0,    0, 144 },	/* File */
	{ 0x068, 0x12b,    0,    0,    0,    0,    0,    0,    0,    0, 145 },	/* Send File */
	{ 0x069, 0x140,    0,    0,    0,    0,    0,    0,    0,    0, 146 },	/* Delete File */
	{ 0x06a, 0x126,    0,    0,    0,    0,    0,    0,    0,    0, 151 },	/* MS DOS */
	{ 0x06b, 0x156,    0,    0,    0,    0,    0,    0,    0,    0, 153 },	/* Direction */
	{     0, 0x129,    0,    0,    0,    0,    0,    0,    0,    0, 166 },	/* Stop */
	{     0, 0x115,    0,    0,    0,    0,    0,    0,    0,    0, 165 },	/* Prev Song */
	{     0, 0x13b,    0,    0,    0,    0,    0,    0,    0,    0, 113 },	/* Mute */
	{     0, 0x125,    0,    0,    0,    0,    0,    0,    0,    0, 163 },	/* Next Song */
	{     0, 0x116,    0,    0,    0,    0,    0,    0,    0,    0, 164 },	/* Play / Pause */
	{ 0x17d, 0x102,    0,    0,    0,    0,    0,    0,    0,    0, 162 },	/* Eject / Close */
	{     0, 0x104,    0,    0,    0,    0,    0,    0,    0,    0, 115 },	/* Volume Up */
	{     0, 0x103,    0,    0,    0,    0,    0,    0,    0,    0, 114 },	/* Volume Down */

/*
 * IBM RapidAccess function keys
 */

	{     0, 0x142,    0,    0,    0,    0,    0,    0,    0,    0, 205 },	/* Suspend */
	{     0, 0x14b,    0,    0,    0,    0,    0,    0,    0,    0, 138 },	/* Help */
	{     0, 0x13a,    0,    0,    0,    0,    0,    0,    0,    0, 148 },	/* Prog 1 */
	{     0, 0x143,    0,    0,    0,    0,    0,    0,    0,    0, 149 },	/* Prog 2 */
       	{     0, 0x132,    0,    0,    0,    0,    0,    0,    0,    0, 202 },	/* Prog 3 */
	{     0, 0x121,    0,    0,    0,    0,    0,    0,    0,    0, 203 },	/* Prog 4 */
	{     0, 0x14d,    0,    0,    0,    0,    0,    0,    0,    0, 200 },	/* Play */
	{     0, 0x13b,    0,    0,    0,    0,    0,    0,    0,    0, 166 },	/* Stop */
	{     0, 0x134,    0,    0,    0,    0,    0,    0,    0,    0, 201 },	/* Pause */
	{     0, 0x11c,    0,    0,    0,    0,    0,    0,    0,    0, 114 },	/* Volume Down */
       	{     0, 0x10e,    0,    0,    0,    0,    0,    0,    0,    0, 115 },	/* Volume Up */
	{     0, 0x133,    0,    0,    0,    0,    0,    0,    0,    0, 165 },	/* Prev Song */
	{     0, 0x12b,    0,    0,    0,    0,    0,    0,    0,    0, 163 },	/* Next Song */
	{     0, 0x124,    0,    0,    0,    0,    0,    0,    0,    0, 113 },	/* Mute */

/*
 * Chicony KBP-8993 function keys
 */

	{     0, 0x142,    0,    0,    0,    0,    0,    0,    0,    0, 205 },	/* Moon */
	{     0, 0x13a,    0,    0,    0,    0,    0,    0,    0,    0, 150 },	/* WWW */
       	{     0, 0x132,    0,    0,    0,    0,    0,    0,    0,    0, 151 },	/* MSDOS */
	{     0, 0x143,    0,    0,    0,    0,    0,    0,    0,    0, 144 },	/* MyDoc */
	{     0, 0x14b,    0,    0,    0,    0,    0,    0,    0,    0, 139 },	/* Menu */
	{     0, 0x11c,    0,    0,    0,    0,    0,    0,    0,    0, 142 },	/* Zzz */
	{     0, 0x121,    0,    0,    0,    0,    0,    0,    0,    0, 162 },	/* Close */
	{     0, 0x13b,    0,    0,    0,    0,    0,    0,    0,    0, 166 },	/* Stop */
	{     0, 0x133,    0,    0,    0,    0,    0,    0,    0,    0, 158 },	/* Back */
	{     0, 0x134,    0,    0,    0,    0,    0,    0,    0,    0, 164 },	/* Play */
	{     0, 0x12b,    0,    0,    0,    0,    0,    0,    0,    0, 159 },	/* Forward */
	{     0, 0x123,    0,    0,    0,    0,    0,    0,    0,    0, 113 },	/* Mute */
	{     0, 0x124,    0,    0,    0,    0,    0,    0,    0,    0, 114 },	/* Volume Down */
	{     0, 0x14d,    0,    0,    0,    0,    0,    0,    0,    0, 115 },	/* Volume Up */
                                                              
/*                                                                    
 * Tandberg TDV5020 function keys (there are 20)                     
 */                                                                   
                                                                      
	{     0, 0x129,    0,    0,    0,    0,    0,    0,    0,    0, 138 },	/* Help */
	{ 0x10e, 0x166,    0,    0,    0,    0,    0,    0,    0,    0, 174 },	/* Exit */
	{     0, 0x144,    0,    0,    0,    0,    0,    0,    0,    0, 137 },	/* Cut */
	{     0, 0x160,    0,    0,    0,    0,    0,    0,    0,    0, 133 },	/* Copy */
	{ 0x10c, 0x167,    0,    0,    0,    0,    0,    0,    0,    0, 175 },	/* Move */
                                                                      
/*                                                                    
 * KeyPad equal, found on Sun Type 4 and Mac keyboards
 */                                                                   
                                                                      
	{ 0x05c,     0,    0, 0x2d, 0x67, 0x51,    0,    0,    0,    0, 117 },	/* KP = */
                                                                      
/*                                                                    
 * KeyPad plusminus, found on DEC LK450                               
 */                                                                   
                                                                      
	{ 0x14e, 0x179,    0,    0,    0,    0,    0,    0,    0,    0, 118 },	/* DEC LK450 KP +- */

/*
 * Atari iKBD extra keys
 */

	{ 0x176,     0,    0,    0,    0,    0,    0,    0, 0x63,    0, 179 },	/* KP ( */
	{ 0x17b,     0,    0,    0,    0,    0,    0,    0, 0x65,    0, 180 },	/* KP ) */
	{ 0x105,     0,    0,    0,    0,    0,    0,    0, 0x60,    0, 170 },	/* ISO KEY (?) */
                                                                      
/*                                                                    
 * USB Keypad Comma, for keyboards with both . and , on the keypad
 */                                                                   

	{     0,     0,    0,    0, 0x85, 0x5f,    0,    0,    0,    0, 124 },  /* KP , */

/*     
 * USB International keys (see USB HID Usage Tables)
 */

	{ 0x073, 0x051,0x051,    0, 0x87, 0x5e,    0,    0,    0,    0, 181 },	/* Intl1 / Jpn key (\ and _), Br ABNT2 key (/?) */
	{ 0x070, 0x013,0x087,    0, 0x88,    0,    0,    0,    0,    0, 182 },  /* Intl2 / Jpn key (Hiragana) */
	{ 0x07d, 0x06a,0x05d,    0, 0x89, 0x5d,    0,    0,    0,    0, 183 },  /* Intl3 / Jpn key (\ and |) */
	{ 0x079, 0x064,0x086,    0, 0x8a,    0,    0,    0,    0,    0, 184 },  /* Intl4 / Jpn key (Henkan) */
	{ 0x07b, 0x067,0x085,    0, 0x8b,    0,    0,    0,    0,    0, 185 },  /* Intl5 / Jpn key (Muhenkan) */
	{     0,     0,    0,    0, 0x8c,    0,    0,    0,    0,    0, 186 },  /* International6 */
	{     0,     0,    0,    0, 0x8d,    0,    0,    0,    0,    0, 187 },  /* International7 */
	{     0,     0,    0,    0, 0x8e,    0,    0,    0,    0,    0, 188 },  /* International8 */
	{     0,     0,    0,    0, 0x8f,    0,    0,    0,    0,    0, 189 },  /* International9 */

/*     
 * USB language switching keys (see USB HID Usage Tables)
 */

	{ 0x071,     0,    0,    0, 0x90, 0x68,    0,    0,    0,    0, 190 },  /* Language1 Korean Hangul/English */
	{ 0x072,     0,    0,    0, 0x91, 0x66,    0,    0,    0,    0, 191 },  /* Language2 Korean Hanja */
	{     0,     0,    0,    0, 0x92,    0,    0,    0,    0,    0, 192 },  /* Language3 */
	{     0,     0,    0,    0, 0x93,    0,    0,    0,    0,    0, 193 },  /* Language4 */
	{     0,     0,    0,    0, 0x94,    0,    0,    0,    0,    0, 194 },  /* Language5 */
	{     0,     0,    0,    0, 0x95,    0,    0,    0,    0,    0, 195 },  /* Language6 */
	{     0,     0,    0,    0, 0x96,    0,    0,    0,    0,    0, 196 },  /* Language7 */
	{     0,     0,    0,    0, 0x97,    0,    0,    0,    0,    0, 197 },  /* Language8 */
	{     0,     0,    0,    0, 0x98,    0,    0,    0,    0,    0, 198 },  /* Language9 */
                                                                      
/*                                                                    
 * Qtronic Scorpion USB                                               
 */                                                                   
                                                                      
	{ 0x076,     0,    0,    0, 0xf7,    0,    0,    0,    0,    0, 176 },	/* Word Processor */
	{ 0x10d,     0,    0,    0, 0xfa,    0,    0,    0,    0,    0, 173 },	/* Referesh */
	{ 0x075,     0,    0,    0, 0xf5,    0,    0,    0,    0,    0, 177 },	/* Scroll Up */
	{ 0x137,     0,    0,    0, 0xf6,    0,    0,    0,    0,    0, 178 },	/* Scroll Down */
                                                                      
/*                                                                    
 * AT keyboard prefixes - atkbd.c internal.                          
 */                                                                   
                                                                      
	{     0, 0x0aa, 0xaa,    0,    0,    0,    0,    0,    0,    0, 251 },	/* The BAT code */
	{     0, 0x0f0, 0xf0,    0,    0,    0,    0,    0,    0,    0, 254 },	/* Release prefix */
	{     0, 0x080, 0x80,    0,    0,    0,    0,    0,    0,    0, 252 },	/* Unusual key prefix */
	{     0, 0x0e0, 0xe0,    0,    0,    0,    0,    0,    0,    0, 252 },	/* Normal key prefix 0 */
	{     0, 0x0e1, 0xe1,    0,    0,    0,    0,    0,    0,    0, 253 },	/* Normal key prefix 1 */
	{     0, 0x000, 0x00,    0,    0,    0,    0,    0,    0,    0, 255 },	/* Ignore clash */
	{     0, 0x0ff, 0xff,    0,    0,    0,    0,    0,    0,    0, 255 },	/* Ignore generic error */
	{     0, 0x112, 0xff,    0,    0,    0,    0,    0,    0,    0, 255 },	/* Ignore magic alt */

/*
 * x86 RAW mode backtranslation.
 */
	{ 0x063,     0,    0,    0,    0,    0,    0,    0,    0,    0, 169 },
	{ 0x101,     0,    0,    0,    0,    0,    0,    0,    0,    0, 171 },
	{ 0x102,     0,    0,    0,    0,    0,    0,    0,    0,    0, 172 },
	{ 0x103,     0,    0,    0,    0,    0,    0,    0,    0,    0, 124 },
	{ 0x108,     0,    0,    0,    0,    0,    0,    0,    0,    0, 186 },
	{ 0x109,     0,    0,    0,    0,    0,    0,    0,    0,    0, 187 },
	{ 0x10a,     0,    0,    0,    0,    0,    0,    0,    0,    0, 188 },
	{ 0x10b,     0,    0,    0,    0,    0,    0,    0,    0,    0, 189 },
	{ 0x10f,     0,    0,    0,    0,    0,    0,    0,    0,    0, 192 },
	{ 0x111,     0,    0,    0,    0,    0,    0,    0,    0,    0, 193 },
	{ 0x114,     0,    0,    0,    0,    0,    0,    0,    0,    0, 194 },
	{ 0x115,     0,    0,    0,    0,    0,    0,    0,    0,    0, 195 },
	{ 0x116,     0,    0,    0,    0,    0,    0,    0,    0,    0, 196 },
	{ 0x11a,     0,    0,    0,    0,    0,    0,    0,    0,    0, 197 },
	{ 0x11b,     0,    0,    0,    0,    0,    0,    0,    0,    0, 198 },
	{ 0x127,     0,    0,    0,    0,    0,    0,    0,    0,    0, 199 },
	{ 0x128,     0,    0,    0,    0,    0,    0,    0,    0,    0, 200 },
	{ 0x129,     0,    0,    0,    0,    0,    0,    0,    0,    0, 201 },
	{ 0x12b,     0,    0,    0,    0,    0,    0,    0,    0,    0, 202 },
	{ 0x12c,     0,    0,    0,    0,    0,    0,    0,    0,    0, 203 },
	{ 0x12d,     0,    0,    0,    0,    0,    0,    0,    0,    0, 204 },
	{ 0x12e,     0,    0,    0,    0,    0,    0,    0,    0,    0, 205 },
	{ 0x12f,     0,    0,    0,    0,    0,    0,    0,    0,    0, 206 },
	{ 0x133,     0,    0,    0,    0,    0,    0,    0,    0,    0, 207 },
	{ 0x134,     0,    0,    0,    0,    0,    0,    0,    0,    0, 208 },
	{ 0x136,     0,    0,    0,    0,    0,    0,    0,    0,    0, 209 },
	{ 0x139,     0,    0,    0,    0,    0,    0,    0,    0,    0, 210 },
	{ 0x13a,     0,    0,    0,    0,    0,    0,    0,    0,    0, 211 },
	{ 0x13b,     0,    0,    0,    0,    0,    0,    0,    0,    0, 212 },
	{ 0x13d,     0,    0,    0,    0,    0,    0,    0,    0,    0, 213 },
	{ 0x13e,     0,    0,    0,    0,    0,    0,    0,    0,    0, 214 },
	{ 0x13f,     0,    0,    0,    0,    0,    0,    0,    0,    0, 215 },
	{ 0x140,     0,    0,    0,    0,    0,    0,    0,    0,    0, 216 },
	{ 0x141,     0,    0,    0,    0,    0,    0,    0,    0,    0, 217 },
	{ 0x142,     0,    0,    0,    0,    0,    0,    0,    0,    0, 218 },
	{ 0x143,     0,    0,    0,    0,    0,    0,    0,    0,    0, 219 },
	{ 0x144,     0,    0,    0,    0,    0,    0,    0,    0,    0, 220 },
	{ 0x145,     0,    0,    0,    0,    0,    0,    0,    0,    0, 221 },
	{ 0x146,     0,    0,    0,    0,    0,    0,    0,    0,    0, 222 },
	{ 0x14a,     0,    0,    0,    0,    0,    0,    0,    0,    0, 223 },
	{ 0x14c,     0,    0,    0,    0,    0,    0,    0,    0,    0, 224 },
	{ 0x154,     0,    0,    0,    0,    0,    0,    0,    0,    0, 225 },
	{ 0x155,     0,    0,    0,    0,    0,    0,    0,    0,    0, 226 },
	{ 0x156,     0,    0,    0,    0,    0,    0,    0,    0,    0, 227 },
	{ 0x157,     0,    0,    0,    0,    0,    0,    0,    0,    0, 228 },
	{ 0x158,     0,    0,    0,    0,    0,    0,    0,    0,    0, 229 },
	{ 0x159,     0,    0,    0,    0,    0,    0,    0,    0,    0, 230 },
	{ 0x15a,     0,    0,    0,    0,    0,    0,    0,    0,    0, 231 },
	{ 0x164,     0,    0,    0,    0,    0,    0,    0,    0,    0, 232 },
	{ 0x167,     0,    0,    0,    0,    0,    0,    0,    0,    0, 233 },
	{ 0x16d,     0,    0,    0,    0,    0,    0,    0,    0,    0, 234 },
	{ 0x170,     0,    0,    0,    0,    0,    0,    0,    0,    0, 235 },
	{ 0x171,     0,    0,    0,    0,    0,    0,    0,    0,    0, 236 },
	{ 0x172,     0,    0,    0,    0,    0,    0,    0,    0,    0, 237 },
	{ 0x173,     0,    0,    0,    0,    0,    0,    0,    0,    0, 238 },
	{ 0x174,     0,    0,    0,    0,    0,    0,    0,    0,    0, 239 },

/*
 * End.                                                               
 */                                                                   
                                                                      
	{     0,     0,    0,    0,    0,    0,    0,    0,    0,    0,   0 }};
