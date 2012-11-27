/*
 * $Id$
 *
 *  Copyright (c) 1999-2000 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 *
 *  Twiddler support Copyright (c) 2001 Arndt Schoenewald
 *  Sponsored by Quelltext AG (http://www.quelltext-ag.de), Dortmund, Germany
 *
 *  Sahara Touchit-213 mode added by Claudio Nieder 2008-05-01.
 */

/*
 * Input line discipline attach program
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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/serio.h>
#include "serio-ids.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static int readchar(int fd, unsigned char *c, int timeout)
{
	struct timeval tv;
	fd_set set;

	tv.tv_sec = 0;
	tv.tv_usec = timeout * 1000;

	FD_ZERO(&set);
	FD_SET(fd, &set);

	if (!select(fd + 1, &set, NULL, NULL, &tv))
		return -1;

	if (read(fd, c, 1) != 1)
		return -1;

	return 0;
}

static void setline(int fd, int flags, int speed)
{
	struct termios t;

	tcgetattr(fd, &t);

	t.c_cflag = flags | CREAD | HUPCL | CLOCAL;
	t.c_iflag = IGNBRK | IGNPAR;
	t.c_oflag = 0;
	t.c_lflag = 0;
	t.c_cc[VMIN ] = 1;
	t.c_cc[VTIME] = 0;

	cfsetispeed(&t, speed);
	cfsetospeed(&t, speed);

	tcsetattr(fd, TCSANOW, &t);
}

static int logitech_command(int fd, char *c)
{
	int i;
	unsigned char d;

	for (i = 0; c[i]; i++) {
		if (write(fd, c + i, 1) != 1)
			return -1;
		if (readchar(fd, &d, 1000))
			return -1;
		if (c[i] != d)
			return -1;
	}
	return 0;
}

static int magellan_init(int fd, unsigned long *id, unsigned long *extra)
{
	if (write(fd, "m3\rpBB\rz\r", 9) != 9)
		return -1;
	return 0;
}

static int warrior_init(int fd, unsigned long *id, unsigned long *extra)
{
	if (logitech_command(fd, "*S"))
		return -1;

	setline(fd, CS8, B4800);
	return 0;
}

static int spaceball_waitchar(int fd, unsigned char c, char *d,
				int timeout)
{
	unsigned char b = 0;

	while (!readchar(fd, &b, timeout)) {
		if (b == 0x0a)
			continue;
		*d++ = b;
		if (b == c)
			break;
	}

	*d = 0;

	return -(b != c);
}

static int spaceball_waitcmd(int fd, char c, char *d)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (spaceball_waitchar(fd, 0x0d, d, 1000))
			return -1;
		if (d[0] == c)
			return 0;
	}

	return -1;
}

static int spaceball_cmd(int fd, char *c, char *d)
{
	int i;

	for (i = 0; c[i]; i++)
		if (write(fd, c + i, 1) != 1)
			return -1;
	if (write(fd, "\r", 1) != 1)
		return -1;

	i = spaceball_waitcmd(fd, toupper(c[0]), d);

	return i;
}

#define SPACEBALL_1003		1
#define SPACEBALL_2003B		3
#define SPACEBALL_2003C		4
#define SPACEBALL_3003C		7
#define SPACEBALL_4000FLX	8
#define SPACEBALL_4000FLX_L	9

static int spaceball_init(int fd, unsigned long *id, unsigned long *extra)
{
	char r[64];

	if (spaceball_waitchar(fd, 0x11, r, 4000) ||
	    spaceball_waitchar(fd, 0x0d, r, 1000))
		return -1;

	if (spaceball_waitcmd(fd, '@', r))
		return -1;

	if (strncmp("@1 Spaceball alive", r, 18))
		return -1;

	if (spaceball_waitcmd(fd, '@', r))
		return -1;

	if (spaceball_cmd(fd, "hm", r))
		return -1;

	if (!strncmp("Hm2003B", r, 7))
		*id = SPACEBALL_2003B;
	if (!strncmp("Hm2003C", r, 7))
		*id = SPACEBALL_2003C;
	if (!strncmp("Hm3003C", r, 7))
		*id = SPACEBALL_3003C;

	if (!strncmp("HvFirmware", r, 10)) {

		if (spaceball_cmd(fd, "\"", r))
			return -1;

		if (strncmp("\"1 Spaceball 4000 FLX", r, 21))
			return -1;

		if (spaceball_waitcmd(fd, '"', r))
			return -1;

		if (strstr(r, " L "))
			*id = SPACEBALL_4000FLX_L;
		else
			*id = SPACEBALL_4000FLX;

		if (spaceball_waitcmd(fd, '"', r))
			return -1;

		if (spaceball_cmd(fd, "YS", r))
			return -1;

		if (spaceball_cmd(fd, "M", r))
			return -1;

		return 0;
	}

	if (spaceball_cmd(fd, "P@A@A", r) ||
	    spaceball_cmd(fd, "FT@", r)   ||
	    spaceball_cmd(fd, "MSS", r))
		return -1;

	return 0;
}

static int stinger_init(int fd, unsigned long *id, unsigned long *extra)
{
	int i;
	unsigned char c;
	unsigned char *response = (unsigned char *)"\r\n0600520058C272";

	if (write(fd, " E5E5", 5) != 5)		/* Enable command */
		return -1;

	for (i = 0; i < 16; i++)		/* Check for Stinger */
		if (readchar(fd, &c, 200) || c != response[i])
			return -1;

	return 0;
}

static int mzp_init(int fd, unsigned long *id, unsigned long *extra)
{
	if (logitech_command(fd, "*X*q"))
		return -1;

	setline(fd, CS8, B9600);
	return 0;
}

static int newton_init(int fd, unsigned long *id, unsigned long *extra)
{
	int i;
	unsigned char c;
	unsigned char response[35] = {
		0x16, 0x10, 0x02, 0x64, 0x5f, 0x69, 0x64, 0x00,
		0x00, 0x00, 0x0c, 0x6b, 0x79, 0x62, 0x64, 0x61,
		0x70, 0x70, 0x6c, 0x00, 0x00, 0x00, 0x01, 0x6e,
		0x6f, 0x66, 0x6d, 0x00, 0x00, 0x00, 0x00, 0x10,
		0x03, 0xdd, 0xe7
	};

	for (i = 0; i < sizeof(response); i++)
		if (readchar(fd, &c, 400) || c != response[i])
			return -1;

	return 0;
}

static int twiddler_init(int fd, unsigned long *id, unsigned long *extra)
{
	unsigned char c[10];
	int count, line;

	/* Turn DTR off, otherwise the Twiddler won't send any data. */
	if (ioctl(fd, TIOCMGET, &line) < 0)
		return -1;
	line &= ~TIOCM_DTR;
	if (ioctl(fd, TIOCMSET, &line) < 0)
		return -1;

	/*
	 * Check whether the device on the serial line is the Twiddler.
	 *
	 * The Twiddler sends data packets of 5 bytes which have the following
	 * properties: the MSB is 0 on the first and 1 on all other bytes, and
	 * the high order nibble of the last byte is always 0x8.
	 *
	 * We read and check two of those 5 byte packets to be sure that we
	 * are indeed talking to a Twiddler.
	 */

	/* Read at most 5 bytes until we find one with the MSB set to 0 */
	for (count = 0; count < 5; count++) {
		if (readchar(fd, c, 500))
			return -1;
		if ((c[0] & 0x80) == 0)
			break;
	}

	if (count == 5) {
		/* Could not find header byte in data stream */
		return -1;
	}

	/* Read remaining 4 bytes plus the full next data packet */
	for (count = 1; count < 10; count++)
		if (readchar(fd, c + count, 500))
			return -1;

	/* Check whether the bytes of both data packets obey the rules */
	for (count = 1; count < 10; count++) {
		if ((count % 5 == 0 && (c[count] & 0x80) != 0x00) ||
		    (count % 5 == 4 && (c[count] & 0xF0) != 0x80) ||
		    (count % 5 != 0 && (c[count] & 0x80) != 0x80)) {
			/* Invalid byte in data packet */
			return -1;
		}
	}

	return 0;
}

static int pm6k_init(int fd, unsigned long *id, unsigned long *extra)
{
	int i = 0;
	unsigned char cmd[6] = {0xF1, 0x00, 0x00, 0x00, 0x00, 0x0E};
	unsigned char data[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	/* Enable the touchscreen */
	if (write(fd, cmd, sizeof(cmd)) != sizeof(cmd))
		return -1;

	/* Read ACK */
	for(i=0;i<sizeof(data);i++)
		if (readchar(fd, &data[i], 100)<0)
			break ;

	return 0;
}

static int fujitsu_init(int fd, unsigned long *id, unsigned long *extra)
{
	unsigned char cmd, data;

	/* Wake up the touchscreen */
	cmd = 0xff; /* Dummy data */;
	if (write(fd, &cmd, 1) != 1)
		return -1;

	/* Wait to settle down */
	usleep(100 * 1000); /* 100 ms */

	/* Reset the touchscreen */
	cmd = 0x81; /* Cold reset */
	if (write(fd, &cmd, 1) != 1)
		return -1;

	/* Read ACK */
	if (readchar(fd, &data, 100) || (data & 0xbf) != 0x90)
		return -1;

	/* Read status */
	if (readchar(fd, &data, 100) || data != 0x00)
		return -1;

	return 0;
}

static int tsc40_init(int fd, unsigned long *id, unsigned long *extra)
{
	unsigned char cmd[2], data;
	unsigned int eeprom;

	/* Datasheet can be found here:
	 * http://www.distec.de/PDF/Drivers/DMC/TSC40_Protocol_Description.pdf
	 */

#define TSC40_CMD_DATA1	0x01
#define TSC40_CMD_RATE	0x05
#define TSC40_CMD_ID	0x15
#define TSC40_CMD_RESET	0x55

#define TSC40_RATE_150	0x45
#define TSC40_NACK	0x15

	/* trigger a software reset to get into a well known state */
	cmd[0] = TSC40_CMD_RESET;
	if (write(fd, cmd, 1) != 1)
		return -1;
	
	/* wait to settle down */
	usleep(15 * 1000); /* 15 ms */	
	
	/* read panel ID to check if an EEPROM is used */
	cmd[0] = TSC40_CMD_ID;
	if (write(fd, cmd, 1) != 1)
		return -1;
	
	if (readchar(fd, &data, 100))
		return -1;

	/* if bit7 is not set --> EEPROM is used */
	eeprom = !((data & 0x80) >> 7);
	
	/* ignore 2nd byte of ID cmd */
	if (readchar(fd, &data, 100))
		return -1;
	
	/* set coordinate oupt rate setting */
	cmd[0] = TSC40_CMD_RATE;
	cmd[1] = TSC40_RATE_150;
	if (write(fd, cmd, 2) != 2)
		return -1;
	
	/* read response */
	if (readchar(fd, &data, 100))
		return -1;
	
	if ((data == TSC40_NACK) && (eeprom == 1)) {
		/* get detailed failure information */
		if (readchar(fd, &data, 100))
			return -1;

		switch (data) {
		case 0x02:	/* EEPROM data abnormal */
		case 0x04:	/* EEPROM write error */
		case 0x08:	/* Touch screen not connected */
			return -1;
			break;
			
		default:
			/* 0x01: EEPROM data empty */
			break;
		}
	}

	/* start sending coordinate informations */
	cmd[0] = TSC40_CMD_DATA1;
	if (write(fd, cmd, 1) != 1)
		return -1;

	return 0;
}

static int t213_init(int fd, unsigned long *id, unsigned long *extra)
{
	char cmd[]={0x0a,1,'A'};
	int count=10;
	int state=0;
	unsigned char data;

	/*
	 * In case the controller is in "ELO-mode" send a few times
	 * the check active packet to force it into the documented
	 * touchkit mode.
	 */
	while (count>0) {
		if (write(fd, &cmd, 3) != 3)
			return -1;
		while (!readchar(fd, &data, 100)) {
			switch (state) {
			case 0:
				if (data==0x0a) {
					state=1;
				}
				break;
			case 1:
				if (data==1) {
					state=2;
				} else if (data!=0x0a) {
					state=0;
				}
				break;
			case 2:
				if (data=='A') {
					return 0;
				} else if (data==0x0a) {
					state=1;
				} else {
					state=0;
				}
				break;
			}
					
		}
		count--;
	}
	return -1;
}

static int zhenhua_init(int fd, unsigned long *id, unsigned long *extra)
{
	/* Zhen Hua 5 byte protocol: first (synchronization) byte allways
	 * contain 0xF7, next four bytes are axis of controller with values
	 * between 50-200.
	 * Incoming data (each byte) have reversed bits (lowest bit is
	 * highest bit) - something like little-endian but on bit level.
	 * Synchronization byte without reversing bits have (raw) value:
	 * 0xEF
	 *
	 * Initialization is almost same as twiddler_init */

	unsigned char c[10];
	int count;

	for (count=0 ; count < 5 ; count++) {
		if(readchar(fd, c+0, 500)) return -1;
		if(c[0] == 0xef) break;
	}

	if (count == 5) {
		/* Could not find header byte in data stream */
		return -1;
	}

	/* Read remaining 4 bytes plus the full next data packet */
	for (count = 1; count < 10; count++) {
		if (readchar(fd, c+count, 500)) return -1;
	}

	/* check if next sync byte exists */
	if (c[5] != 0xef)
		return -1;

	return 0;
		
}

#define EP_PROMPT_MODE  "B"     /* Prompt mode */
#define EP_ABSOLUTE     "F"     /* Absolute Mode */
#define EP_UPPER_ORIGIN "b"     /* Origin upper left */
#define EP_STREAM_MODE  "@"     /* Stream mode */

static int easypen_init(int fd, unsigned long *id, unsigned long *extra)
{
	char buf[256];

	/* reset */
	write(fd, 0, 1);
	usleep(400000);

	/* set prompt mode */
	if (write(fd, EP_PROMPT_MODE, 1) == -1)
		return -1;

	/* clear buffer */
	while (read(fd, buf, sizeof(buf)) == sizeof(buf));

	/* set options */
	if (write(fd, EP_ABSOLUTE EP_STREAM_MODE EP_UPPER_ORIGIN, 3) == -1)
		return -1;

	return 0;
}

static int dump_init(int fd, unsigned long *id, unsigned long *extra)
{
	unsigned char c, o = 0;

	c = 0x80;

	if (write(fd, &c, 1) != 1)         /* Enable command */
                return -1;

	while (1)
		if (!readchar(fd, &c, 1)) {
			printf("%02x (%c) ", c, ((c > 32) && (c < 127)) ? c : 'x');
			o = 1;
		} else {
			if (o) {
				printf("\n");
				o = 0;
			}
		}
}

struct input_types {
	const char *name;
	const char *name2;
	const char *desc;
	int speed;
	int flags;
	unsigned long type;
	unsigned long id;
	unsigned long extra;
	int flush;
	int (*init)(int fd, unsigned long *id, unsigned long *extra);
};

static struct input_types input_types[] = {
{ "--sunkbd",		"-skb",		"Sun Type 4 and Type 5 keyboards",
	B1200, CS8,
	SERIO_SUNKBD,		0x00,	0x00,	1,	NULL },
{ "--lkkbd",		"-lk",		"DEC LK201 / LK401 keyboards",
	B4800, CS8|CSTOPB,
	SERIO_LKKBD,		0x00,	0x00,	1,	NULL },
{ "--vsxxx-aa",		"-vs",
			"DEC VSXXX-AA / VSXXX-GA mouse and VSXXX-A tablet",
	B4800, CS8|CSTOPB|PARENB|PARODD,
	SERIO_VSXXXAA,		0x00,	0x00,	1,	NULL },
{ "--spaceorb",		"-orb",		"SpaceOrb 360 / SpaceBall Avenger",
	B9600, CS8,
	SERIO_SPACEORB,		0x00,	0x00,	1,	NULL },
{ "--spaceball",	"-sbl",		"SpaceBall 2003 / 3003 / 4000 FLX",
	B9600, CS8,
	SERIO_SPACEBALL,	0x00,	0x00,	0,	spaceball_init },
{ "--magellan",		"-mag",		"Magellan / SpaceMouse",
	B9600, CS8 | CSTOPB | CRTSCTS,
	SERIO_MAGELLAN,		0x00,	0x00,	1,	magellan_init },
{ "--warrior",		"-war",		"WingMan Warrior",
	B1200, CS7 | CSTOPB,
	SERIO_WARRIOR,		0x00,	0x00,	1,	warrior_init },
{ "--stinger",		"-sting",	"Gravis Stinger",
	B1200, CS8,
	SERIO_STINGER,		0x00,	0x00,	1,	stinger_init },
{ "--mousesystems",	"-msc",		"3-button Mouse Systems mouse",
	B1200, CS8,
	SERIO_MSC,		0x00,	0x01,	1,	NULL },
{ "--sunmouse",		"-sun",		"3-button Sun mouse",
	B1200, CS8,
	SERIO_SUN,		0x00,	0x01,	1,	NULL },
{ "--microsoft",	"-bare",	"2-button Microsoft mouse",
	B1200, CS7,
	SERIO_MS,		0x00,	0x00,	1,	NULL },
{ "--mshack",		"-ms",		"3-button mouse in Microsoft mode",
	B1200, CS7,
	SERIO_MS,		0x00,	0x01,	1,	NULL },
{ "--mouseman",		"-mman",	"3-button Logitech / Genius mouse",
	B1200, CS7,
	SERIO_MP,		0x00,	0x01,	1,	NULL },
{ "--intellimouse",	"-ms3",		"Microsoft IntelliMouse",
	B1200, CS7,
	SERIO_MZ,		0x00,	0x11,	1,	NULL },
{ "--mmwheel",		"-mmw",
			"Logitech mouse with 4-5 buttons or a wheel",
	B1200, CS7 | CSTOPB,
	SERIO_MZP,		0x00,	0x13,	1,	mzp_init },
{ "--iforce",		"-ifor",	"I-Force joystick or wheel",
	B38400, CS8,
	SERIO_IFORCE,		0x00,	0x00,	0,	NULL },
{ "--newtonkbd",	"-newt",	"Newton keyboard",
	B9600, CS8,
	SERIO_NEWTON,		0x00,	0x00,	1,	newton_init },
{ "--h3600ts",		"-ipaq",	"Ipaq h3600 touchscreen",
	B115200, CS8,
	SERIO_H3600,		0x00,	0x00,	0,	NULL },
{ "--stowawaykbd",	"-ipaqkbd",	"Stowaway keyboard",
	B115200, CS8,
	SERIO_STOWAWAY,		0x00,	0x00,	1,	NULL },
{ "--ps2serkbd",	"-ps2ser",	"PS/2 via serial keyboard",
	B1200, CS8,
	SERIO_PS2SER,		0x00,	0x00,	1,	NULL },
{ "--twiddler",		"-twid",	"Handykey Twiddler chording keyboard",
	B2400, CS8,
	SERIO_TWIDKBD,		0x00,	0x00,	0,	twiddler_init },
{ "--twiddler-joy",	"-twidjoy",	"Handykey Twiddler used as a joystick",
	B2400, CS8,
	SERIO_TWIDJOY,		0x00,	0x00,	0,	twiddler_init },
{ "--elotouch",		"-elo",		"ELO touchscreen, 10-byte mode",
	B9600, CS8 | CRTSCTS,
	SERIO_ELO,		0x00,	0x00,	0,	NULL },
{ "--elo4002",		"-elo6b",	"ELO touchscreen, 6-byte mode",
	B9600, CS8 | CRTSCTS,
	SERIO_ELO,		0x01,	0x00,	0,	NULL },
{ "--elo271-140",	"-elo4b",	"ELO touchscreen, 4-byte mode",
	B9600, CS8 | CRTSCTS,
	SERIO_ELO,		0x02,	0x00,	0,	NULL },
{ "--elo261-280",	"-elo3b",	"ELO Touchscreen, 3-byte mode",
	B9600, CS8 | CRTSCTS,
	SERIO_ELO,		0x03,	0x00,	0,	NULL },
{ "--mtouch",		"-mtouch",	"MicroTouch (3M) touchscreen",
	B9600, CS8 | CRTSCTS,
	SERIO_MICROTOUCH,	0x00,	0x00,	0,	NULL },
#ifdef SERIO_TSC40
{ "--tsc",		"-tsc",		"TSC-10/25/40 serial touchscreen",
	B9600, CS8,
	SERIO_TSC40,		0x00,	0x00,	0,	tsc40_init },
#endif
{ "--touchit213",	"-t213",	"Sahara Touch-iT213 Tablet PC",
	B9600, CS8,
	SERIO_TOUCHIT213,	0x00,	0x00,	0,	t213_init },
{ "--touchright",	"-tr",	"Touchright serial touchscreen",
	B9600, CS8 | CRTSCTS,
	SERIO_TOUCHRIGHT,	0x00,	0x00,	0,	NULL },
{ "--touchwin",		"-tw",	"Touchwindow serial touchscreen",
	B4800, CS8 | CRTSCTS,
	SERIO_TOUCHWIN,		0x00,	0x00,	0,	NULL },
{ "--penmount9000",		"-pm9k",	"PenMount 9000 touchscreen",
	B19200, CS8,
	SERIO_PENMOUNT,		0x00,	0x00,	0,	NULL },
{ "--penmount6000",		"-pm6k",	"PenMount 6000 touchscreen",
	B19200, CS8,
	SERIO_PENMOUNT,		0x01,	0x00,	0,	pm6k_init },
{ "--penmount3000",		"-pm3k",	"PenMount 3000 touchscreen",
	B38400, CS8,
	SERIO_PENMOUNT,		0x02,	0x00,	0,	NULL },
{ "--penmount6250",		"-pmm1",	"PenMount 6250 touchscreen",
	B19200, CS8,
	SERIO_PENMOUNT,		0x03,	0x00,	0,	NULL },
{ "--fujitsu",		"-fjt",	"Fujitsu serial touchscreen",
	B9600, CS8,
	SERIO_FUJITSU,		0x00,	0x00,	1,	fujitsu_init },
{ "--ps2mult",	"-ps2m",	"PS/2 serial multiplexer",
	B57600, CS8,
	SERIO_PS2MULT,		0x00,	0x00,	1,	NULL },
{ "--zhen-hua",		"-zhen",	"Zhen Hua 5-byte protocol",
	B19200, CS8,
	SERIO_ZHENHUA,		0x00,	0x00,	0,	zhenhua_init },
{ "--easypen",		"-ep",		"Genius EasyPen 3x4 tablet",
	B9600, CS8|CREAD|CLOCAL|HUPCL|PARENB|PARODD,
	SERIO_EASYPEN,		0x00,	0x00,	0,	easypen_init },
#ifdef SERIO_TAOSEVM
{ "--taos-evm",		"-taos",	"TAOS evaluation module",
	B1200, CS8,
	SERIO_TAOSEVM,		0,	0,	0,	NULL },
#endif
{ "--dump",		"-dump",	"Just enable device",
	B2400, CS8,
	0,			0x00,	0x00,	0,	dump_init },
{ "--w8001",		"-w8001",	"Wacom W8001",
	B38400, CS8,
	SERIO_W8001,		0x00,	0x00,	0,	NULL },
{ NULL, NULL, NULL, 0, 0, 0, 0, 0, 0, NULL }
};

static void show_help(void)
{
	struct input_types *type;

	puts("");
	puts("Usage: inputattach [--daemon] [--baud <baud>] [--always] [--noinit] <mode> <device>");
	puts("");
	puts("Modes:");

	for (type = input_types; type->name; type++)
		printf("  %-16s %-8s  %s\n",
			type->name, type->name2, type->desc);

	puts("");
}

/* palmed wisdom from http://stackoverflow.com/questions/1674162/ */
#define RETRY_ERROR(x) (x == EAGAIN || x == EWOULDBLOCK || x == EINTR)

int main(int argc, char **argv)
{
	unsigned long devt;
	int ldisc;
	struct input_types *type = NULL;
	const char *device = NULL;
	int daemon_mode = 0;
	int need_device = 0;
	unsigned long id, extra;
	int fd;
	int i;
	unsigned char c;
	int retval;
	int baud = -1;
	int ignore_init_res = 0;
	int no_init = 0;
	int one_read = 0;

	for (i = 1; i < argc; i++) {
		if (!strcasecmp(argv[i], "--help")) {
			show_help();
			return EXIT_SUCCESS;
		} else if (!strcasecmp(argv[i], "--daemon")) {
			daemon_mode = 1;
		} else if (!strcasecmp(argv[i], "--always")) {
			ignore_init_res = 1;
		} else if (!strcasecmp(argv[i], "--noinit")) {
			no_init = 1;
		} else if (need_device) {
			device = argv[i];
			need_device = 0;
		} else if (!strcasecmp(argv[i], "--baud")) {
			if (argc <= i + 1) {
				show_help();
				fprintf(stderr,
					"inputattach: require baud rate\n");
				return EXIT_FAILURE;
			}

			baud = atoi(argv[++i]);
		} else {
			if (type && type->name) {
				fprintf(stderr,
					"inputattach: '%s' - "
					"only one mode allowed\n", argv[i]);
				return EXIT_FAILURE;
			}
			for (type = input_types; type->name; type++) {
				if (!strcasecmp(argv[i], type->name) ||
				    !strcasecmp(argv[i], type->name2)) {
					break;
				}
			}
			if (!type->name) {
				fprintf(stderr,
					"inputattach: invalid mode '%s'\n",
					argv[i]);
				return EXIT_FAILURE;
			}
			need_device = 1;
		}
	}

	if (!type || !type->name) {
		fprintf(stderr, "inputattach: must specify mode\n");
		return EXIT_FAILURE;
        }

	if (need_device) {
		fprintf(stderr, "inputattach: must specify device\n");
		return EXIT_FAILURE;
	}

	fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "inputattach: '%s' - %s\n",
			device, strerror(errno));
		return 1;
	}

	switch(baud) {
	case -1: break;
	case 2400: type->speed = B2400; break;
	case 4800: type->speed = B4800; break;
	case 9600: type->speed = B9600; break;
	case 19200: type->speed = B19200; break;
	case 38400: type->speed = B38400; break;
	default:
		fprintf(stderr, "inputattach: invalid baud rate '%d'\n",
				baud);
		return EXIT_FAILURE;
	}

	setline(fd, type->flags, type->speed);

	if (type->flush)
		while (!readchar(fd, &c, 100))
			/* empty */;

	id = type->id;
	extra = type->extra;

	if (type->init && !no_init) {
		if (type->init(fd, &id, &extra)) {
			if (ignore_init_res) {
				fprintf(stderr, "inputattach: ignored device initialization failure\n");
			} else {
				fprintf(stderr, "inputattach: device initialization failed\n");
				return EXIT_FAILURE;
			}
		}
	}

	ldisc = N_MOUSE;
	if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
		fprintf(stderr, "inputattach: can't set line discipline\n");
		return EXIT_FAILURE;
	}

	devt = type->type | (id << 8) | (extra << 16);

	if (ioctl(fd, SPIOCSTYPE, &devt) < 0) {
		fprintf(stderr, "inputattach: can't set device type\n");
		return EXIT_FAILURE;
	}

	retval = EXIT_SUCCESS;
	if (daemon_mode && daemon(0, 0) < 0) {
		perror("inputattach");
		retval = EXIT_FAILURE;
	}

	do {
		i = read(fd, NULL, 0);
		if (i == -1) {
			if (RETRY_ERROR(errno))
				continue;
		} else {
			one_read = 1;
		}
	} while (!i);

	ldisc = 0;
	if (one_read) {
		// If we've never managed to read, avoid resetting the line
		// discipline - another inputattach is probably running
		ioctl(fd, TIOCSETD, &ldisc);
	}
	close(fd);

	return retval;
}
