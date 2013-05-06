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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
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
#include <stdlib.h>

int main(int argc, char** argv)
{
	int fd;
	const char * device_file_name = "/dev/input/event0";
	int i;
	int gain = -1;
	int autocenter = -1;

	for (i=1; i<argc; ++i) {
		if (strcmp(argv[i], "--help") == 0) {
			printf("Usage: %s /dev/input/eventXX [-g gain] [-a autocenter_strength]\n", argv[0]);
			printf("Sets the gain and the autocenter of a force-feedback device\n");
			printf("Values should belong to 0 to 100\n");
			exit(1);
		}
		else if (strcmp(argv[i], "-g") == 0) {
			if (++i >= argc) {
				fprintf(stderr, "Missing gain value\n");
				exit(1);
			}
			gain = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-a") == 0) {
			if (++i >= argc) {
				fprintf(stderr, "Missing auto-center value\n");
				exit(1);
			}
			autocenter = atoi(argv[i]);
		}
		else {
			device_file_name = argv[i];
		}
	}

	if (autocenter == -1 && gain == -1) {
		exit(0);
	}

	/* Open device */
	fd = open(device_file_name, O_RDWR);
	if (fd == -1) {
		perror("Open device file");
		exit(1);
	}
	printf("Device %s opened\n", device_file_name);

	if (autocenter >= 0 && autocenter <= 100) {
		struct input_event ie;
		ie.type = EV_FF;
		ie.code = FF_AUTOCENTER;
		ie.value = 0xFFFFUL * autocenter / 100;
		if (write(fd, &ie, sizeof(ie)) == -1)
			perror("set auto-center");
	}

	if (gain >= 0 && gain <= 100) {
		struct input_event ie;
		ie.type = EV_FF;
		ie.code = FF_GAIN;
		ie.value = 0xFFFFUL * gain / 100;
		if (write(fd, &ie, sizeof(ie)) == -1)
			perror("set gain");
	}

	exit(0);
}
