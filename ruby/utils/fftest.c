/*
 * Tests the force feedback driver
 * Copyright 2001 Johann Deneux <deneux@ifrance.com>
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
 * You can contact the author by email at this address:
 * Johann Deneux <deneux@ifrance.com>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdio.h>

#define BITS_PER_LONG (sizeof(long) * 8)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

#define N_EFFECTS 4

int main(int argc, char** argv)
{
	struct ff_effect effects[N_EFFECTS];
	struct input_event play, stop;
	int fd;
	char device_file_name[64];
	unsigned long features[4];
	int n_effects;	/* Number of effects the device can play at the same time */
	int i;

	strncpy(device_file_name, "/dev/input/event0", 64);

	for (i=1; i<argc; ++i) {
		if (strncmp(argv[i], "--help", 64) == 0) {
			printf("Usage: %s /dev/input/eventXX\n", argv[0]);
			printf("Tests the force feedback driver\n");
			exit(1);
		}
		else {
			strncpy(device_file_name, argv[i], 64);
		}
	}

	/* Open device */
	fd = open(device_file_name, O_RDWR);
	if (fd == -1) {
		perror("Open device file");
		exit(1);
	}
	printf("Device %s opened\n", device_file_name);

	/* Query device */
	if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(unsigned long) * 4), features) == -1) {
		perror("Ioctl query");
		exit(1);
	}

	printf("Axes query: ");

	if (test_bit(FF_ABS(ABS_X), features)) printf("Axis X ");
	if (test_bit(FF_ABS(ABS_Y), features)) printf("Axis Y ");

	printf("\nEffects: ");

	if (test_bit(FF_CONSTANT, features)) printf("Constant ");
	if (test_bit(FF_PERIODIC, features)) printf("Periodic ");
	if (test_bit(FF_SPRING, features)) printf("Spring ");
	if (test_bit(FF_FRICTION, features)) printf("Friction ");
	if (test_bit(FF_RUMBLE, features)) printf("Rumble ");

	printf("\nNumber of simultaneous effects: ");

	if (ioctl(fd, EVIOCGEFFECTS, &n_effects) == -1) {
		perror("Ioctl number of effects");
		exit(1);
	}

	printf("%d\n", n_effects);

	/* download a constant effect */
	effects[1].type = FF_CONSTANT;
	effects[1].id = -1;
	effects[1].u.constant.level = 0x2000;	/* Strength : 25 % */
	effects[1].u.constant.direction = 0x6000;	/* 135 degrees */
	effects[1].u.constant.shape.attack_length = 0x100;
	effects[1].u.constant.shape.attack_level = 0;
	effects[1].u.constant.shape.fade_length = 0x100;
	effects[1].u.constant.shape.fade_level = 0;
	effects[1].trigger.button = 0;
	effects[1].trigger.interval = 0;
	effects[1].replay.length = 0x1400;  /* 20 seconds */
	effects[1].replay.delay = 0;

	if (ioctl(fd, EVIOCSFF, &effects[1]) == -1) {
		perror("Upload effects[1]");
		exit(1);
	}

	/* download a periodic sinusoidal effect */
	effects[0].type = FF_PERIODIC;
	effects[0].id = -1;
	effects[0].u.periodic.waveform = FF_SINE;
	effects[0].u.periodic.period = 0.1*0x100;	/* 0.1 second */
	effects[0].u.periodic.magnitude = 0x4000;	/* 0.5 * Maximum magnitude */
	effects[0].u.periodic.offset = 0;
	effects[0].u.periodic.phase = 0;
	effects[0].u.periodic.direction = 0x4000;	/* Along X axis */
	effects[0].u.periodic.shape.attack_length = 0x100;
	effects[0].u.periodic.shape.attack_level = 0;
	effects[0].u.periodic.shape.fade_length = 0x100;
	effects[0].u.periodic.shape.fade_level = 0;
	effects[0].trigger.button = 0;
	effects[0].trigger.interval = 0;
	effects[0].replay.length = 0x1400;  /* 20 seconds */
	effects[0].replay.delay = 0;

	if (ioctl(fd, EVIOCSFF, &effects[0]) == -1) {
		perror("Upload effects[0]");
		exit(1);
	}

	/* download an interactive spring effect */
	effects[2].type = FF_SPRING;
	effects[2].id = -1;
	effects[2].u.interactive.axis = ABS_X;
	effects[2].u.interactive.right_saturation = 0x7fff;
	effects[2].u.interactive.left_saturation = 0x7fff;
	effects[2].u.interactive.right_coeff = 0x2000;
	effects[2].u.interactive.left_coeff = 0x2000;
	effects[2].u.interactive.deadband = 0x0;
	effects[2].u.interactive.center = 0x0;
	effects[2].trigger.button = 0;
	effects[2].trigger.interval = 0;
	effects[2].replay.length = 0x1400;  /* 20 seconds */
	effects[2].replay.delay = 0;

	if (ioctl(fd, EVIOCSFF, &effects[2]) == -1) {
		perror("Upload effects[2]");
		exit(1);
	}

	/* download an interactive damper effect */
	effects[3].type = FF_FRICTION;
	effects[3].id = -1;
	effects[3].u.interactive.axis = ABS_X;
	effects[3].u.interactive.right_saturation = 0x7fff;
	effects[3].u.interactive.left_saturation = 0x7fff;
	effects[3].u.interactive.right_coeff = 0x2000;
	effects[3].u.interactive.left_coeff = 0x2000;
	effects[3].u.interactive.deadband = 0x0;
	effects[3].u.interactive.center = 0x0;
	effects[3].trigger.button = 0;
	effects[3].trigger.interval = 0;
	effects[3].replay.length = 0x1400;  /* 20 seconds */
	effects[3].replay.delay = 0;

	if (ioctl(fd, EVIOCSFF, &effects[3]) == -1) {
		perror("Upload effects[3]");
		exit(1);
	}

	/* Ask user what effects to play */
	do {
		printf("Enter effect number, -1 to exit\n");
		scanf("%d", &i);
		if (i >= 0 && i < N_EFFECTS) {
			play.type = EV_FF;
			play.code = effects[i].id;
			play.value = 1;

			if (write(fd, (const void*) &play, sizeof(play)) == -1) {
				perror("Play effect");
				exit(1);
			}
		}
		else {
			printf("No such effect\n");
		}
	} while (i>=0);

	/* Stop the effects */
	for (i=0; i<N_EFFECTS; ++i) {
		stop.type = EV_FF;
		stop.code =  effects[i].id;
		stop.value = 0;
        
		if (write(fd, (const void*) &stop, sizeof(stop)) == -1) {
			perror("Stop effect");
			exit(1);
		}
	}
	

	exit(0);
}
