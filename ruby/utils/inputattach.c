/*
 *  inputattach.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Input line discipline attach program
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

#include <linux/serio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>

int readchar(int fd, unsigned char *c, int timeout)
{
	struct timeval tv;
	fd_set set;

	tv.tv_sec = 0;
	tv.tv_usec = timeout * 1000;

	FD_ZERO(&set);
	FD_SET(fd, &set);

	if (!select(fd+1, &set, NULL, NULL, &tv)) return -1;

	read(fd, c, 1);

	return 0;
}

void setline(int fd, int flags, int speed)
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

int logitech_command(int fd, char *c)
{
	int i;
	unsigned char d;
	for (i = 0; c[i]; i++) {
		write(fd, c + i, 1);
		if (readchar(fd, &d, 1000))
			return -1;
		if (c[i] != d)
			return -1;
	}
	return 0;
}

int magellan_init(int fd)
{
	write(fd, "m3\rpBB\rz\r", 9);
	return 0;
}

int warrior_init(int fd)
{
	if (logitech_command(fd, "*S")) return -1;
	setline(fd, CS8, B4800);
	return 0;
}

int mzp_init(int fd)
{
	if (logitech_command(fd, "*X*q")) return -1;
	setline(fd, CS8, B9600);
	return 0;
}

int dump_init(int fd)
{
	unsigned char c;
	while (1)
		if (!readchar(fd, &c, 1)) 
			printf("%02x ", c);
		else printf("\n");
}

struct input_types {
	char name[16];
	char name2[16];
	int speed;
	int flags;
	unsigned long type;
	unsigned long extra;
	int flush;
	int (*init)(int fd);
};

struct input_types input_types[] = {

{ "--sunkbd",		"-skb",		B1200, CS8,			SERIO_SUNKBD,	0x00,	1,	NULL },
{ "--spaceorb",		"-orb",		B9600, CS8,			SERIO_SPACEORB,	0x00,	1,	NULL },
{ "--magellan",		"-mag",		B9600, CS8 | CSTOPB | CRTSCTS,	SERIO_MAGELLAN,	0x00,	1,	magellan_init },
{ "--warrior",		"-war",		B1200, CS7 | CSTOPB,		SERIO_WARRIOR,	0x00,	1,	warrior_init },
{ "--mousesystems",	"-msc",		B1200, CS8,			SERIO_MSC,	0x01,	1,	NULL },
{ "--sunmouse",		"-sun",		B1200, CS8,			SERIO_SUN,	0x01,	1,	NULL },
{ "--microsoft",	"-bare",	B1200, CS7,			SERIO_MS,	0x00,	1,	NULL },
{ "--mshack",		"-ms",		B1200, CS7,			SERIO_MS,	0x01,	1,	NULL },
{ "--mouseman",		"-mman",	B1200, CS7,			SERIO_MP,	0x01,	1,	NULL },
{ "--intellimouse",	"-ms3",		B1200, CS7,			SERIO_MZ,	0x11,	1,	NULL },
{ "--mmwheel",		"-mmw",		B1200, CS7 | CSTOPB,		SERIO_MZP,	0x13,	1,	mzp_init },
{ "--wmforce",		"-wmf",		B38400, CS8 | CRTSCTS,		SERIO_WMFORCE,	0x00,	0,	NULL },
{ "--iforce",		"-ifor",	B38400, CS8 | CRTSCTS,		SERIO_IFORCE,	0x00,	0,	NULL },
{ "--dump",		"-dump",	B38400, CS8 | CRTSCTS,		0,		0x00,	0,	dump_init },
{ "", "", 0, 0 }

};

int main(int argc, char **argv)
{
	unsigned long devt;
	int ldisc;
        int type;
        int fd;
	char c;

        if (argc < 2 || argc > 3 || !strcmp("--help", argv[1])) {
                puts("");
                puts("Usage: inputttach <mode> <device>");
                puts("");
                puts("Modes:");
                puts("  --sunkbd        -skb   Sun Type 4 and Type 5 keyboards");
                puts("  --spaceorb      -orb   SpaceOrb 360 / SpaceBall Avenger");
                puts("  --magellan      -mag   Magellan / SpaceMouse");
                puts("  --warrior       -war   WingMan Warrior");
		puts("  --mousesystems  -msc   3-button Mouse Systems mice");
		puts("  --sunmouse      -sun   3-button Sun mice");
		puts("  --microsoft     -bare  2-button Microsoft mice");
		puts("  --mshack        -ms    3-button mice in Microsoft mode");
		puts("  --mouseman      -mman  3-button Logitech and Genius mice");
		puts("  --intellimouse  -ms3   Microsoft IntelliMouse");
		puts("  --mmwheel       -mmw   Logitech mice with 4-5 buttons or wheel");
		puts("  --wmforce       -wmf   Logitech WingMan Force");
                puts("");
                return 1;
        }

        for (type = 0; input_types[type].speed; type++) {
                if (!strncasecmp(argv[1], input_types[type].name, 16) ||
			!strncasecmp(argv[1], input_types[type].name2, 16))
                        break;
        }

	if (!input_types[type].speed) {
		fprintf(stderr, "inputattach: invalid mode\n");
		return 1;
	}

	if ((fd = open(argv[2], O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0) {
		perror("inputattach");
		return 1;
	}

	setline(fd, input_types[type].flags, input_types[type].speed);

	if (input_types[type].flush)
		while (!readchar(fd, &c, 100));

	if (input_types[type].init && input_types[type].init(fd)) {
		fprintf(stderr, "inputattach: device initialization failed\n");
		return 1;
	}

	ldisc = N_MOUSE;
	if(ioctl(fd, TIOCSETD, &ldisc)) {
		fprintf(stderr, "inputattach: can't set line discipline\n"); 
		return 1;
	}

	devt = SERIO_RS232 | input_types[type].type | (input_types[type].extra << 16);

	if(ioctl(fd, SPIOCSTYPE, &devt)) {
		fprintf(stderr, "inputattach: can't set device type\n");
		return 1;
	}

	read(fd, NULL, 0);

	ldisc = 0;
	ioctl(fd, TIOCSETD, &ldisc);
	close(fd);

	return 0;
}
