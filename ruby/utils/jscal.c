/*
 * jscal.c  Version 1.2
 *
 * Copyright (c) 1997-199 Vojtech Pavlik
 *
 * Sponsored by SuSE
 */

/*
 * This is a joystick calibration program for Linux joystick driver.
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
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>

#include <asm/param.h>
#include <linux/joystick.h>

#define PIT_HZ 1193180L

#define NUM_POS 3
#define MAX_AXES 16
#define MAX_CORR 1

const char *pos_name[] = {"minimum", "center", "maximum"};
const char *corr_name[] = {"none (raw)", "broken line"};
const char corr_coef_num[] = {0,4};

struct correction_data {
	int cmin[NUM_POS];
	int cmax[NUM_POS];
};

int fd;
struct js_corr corr[MAX_AXES];
char axes, buttons, fuzz;
int version;
struct correction_data corda[MAX_AXES];

struct js_info {
	int buttons;
	int axis[MAX_AXES];
	} js;

void print_position(int i, int a)
{
	printf("Axis %d: %8d\r", i, a);
	fflush(stdout);

}

int get_time(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void wait_for_event(int d, struct js_info *s)
{
	struct js_event ev;
	struct timeval tv;
	char buf;
	fd_set set;

	tv.tv_sec = 0;
	tv.tv_usec = 100000;

	FD_ZERO(&set);
	FD_SET(0, &set);
	FD_SET(d, &set);

	if (select(d+1, &set, NULL, NULL, &tv)) {

		if (FD_ISSET(d, &set)) {
			if (read(d, &ev, sizeof(struct js_event)) == sizeof(struct js_event))
			switch (ev.type & ~JS_EVENT_INIT) {
				case JS_EVENT_AXIS:
					s->axis[ev.number] = ev.value; break;
				case JS_EVENT_BUTTON:
					s->buttons = (s->buttons & ~(1 << ev.number)) | (ev.value << ev.number);
			}
		}

		if (FD_ISSET(0, &set)) {
			read(0, &buf, 1);
			s->buttons |= (1 << 31);
		}

	} else {
		s->buttons &= ~(1 << 31);
	}
}

void putcs(char *s)
{
	int i;
	putchar('\r');
	for (i = 0; i < 78; i++) putchar(' ');
	putchar('\r');
	fputs(s, stdout);
	fflush(stdout);
}

int solve_broken(int *results, struct correction_data inputs)
{
	double a, b, c, d;

	a = inputs.cmin[1];
	b = inputs.cmax[1];
	c = 32767.0 / (inputs.cmin[1] - inputs.cmax[0]);
	d = 32767.0 / (inputs.cmin[2] - inputs.cmax[1]);

	results[0] = rint(a);
	results[1] = rint(b);
	results[2] = rint(c*16384.0);
	results[3] = rint(d*16384.0);

	return 1;
}

void help(void)
{
	putchar('\n');
	puts("Usage: jscal <device>");
	putchar('\n');
	puts("  -c             --calibrate         Calibrate the joystick");
	puts("  -h             --help              Display this help");
	puts("  -s <x,y,z...>  --set-correction    Sets correction to specified values");
	puts("  -t             --test-center       Tests if joystick is corectly calibrated");
	puts("                                       returns 0 on success, see the jscal");
	puts("                                       manpage for error values");
	puts("  -V             --version           Prints the version numbers");
	puts("  -p             --print-correction  Prints the current settings as a jscal");
	puts("                                       command line");
	putchar('\n');
}

void print_info()
{
	int i,j;

	if (ioctl(fd, JSIOCGAXES, &axes)) {
		perror("jscal: error getting axes");
		exit(1);
	}
	if (ioctl(fd, JSIOCGBUTTONS, &buttons)) {
		perror("jscal: error getting buttons");
		exit(1);
	}
	if (ioctl(fd, JSIOCGCORR, &corr)) {
		perror("jscal: error getting correction");
		exit(1);
	}

	printf("Joystick has %d axes and %d buttons.\n", axes, buttons);
	for (i = 0; i < axes; i++) {
		printf("Correction for axis %d is %s, precision is %d.\n",
			i, corr_name[(int)corr[i].type], corr[i].prec);
		if (corr_coef_num[(int)corr[i].type]) {
			printf("Coeficients are:");
			for(j = 0; j < corr_coef_num[(int)corr[i].type]; j++) {
				printf(" %d", corr[i].coef[j]);
				if (j < corr_coef_num[(int)corr[i].type] - 1) putchar(',');
			}
		putchar('\n');
		}
	}
	putchar('\n');
}

void calibrate()
{
	int i, j, t, b;

	for (i=0; i<MAX_AXES; i++) {
		corr[i].type = JS_CORR_NONE;
		corr[i].prec = 0;
	}

	if (ioctl(fd, JSIOCSCORR, &corr)) {
		perror("jscal: error setting correction");
		exit(1);
	}

	{

		int i;
		int amax[MAX_AXES], amin[MAX_AXES];

		puts("Calibrating precision: wait and don't touch the joystick.");

		wait_for_event(fd, &js);
		t = get_time();
		while (get_time() < t+50) wait_for_event(fd, &js);

		wait_for_event(fd, &js);
		t = get_time();
		for(i=0; i < axes; i++)
			amin[i] = amax[i] = js.axis[i];

		do {
			wait_for_event(fd, &js);
			for(i=0; i < axes; i++) {
				if (amin[i] > js.axis[i]) amin[i] = js.axis[i];
				if (amax[i] < js.axis[i]) amax[i] = js.axis[i];
				printf("Axis %d:%5d,%5d ", i, amin[i], amax[i]);
			}
			printf("\r");
			fflush(stdout);
		} while (get_time() < t+2000);

		printf("Done. Precision is:                                             \n");

		for (i=0; i < axes; i++) {
			corr[i].prec = amax[i] - amin[i];
			printf("Axis: %d: %5d\n", i, corr[i].prec);
		}

		puts("");

	}


	b = js.buttons;

	for (j = 0; j < axes; j++)
	for (i = 0; i < NUM_POS; i++) {
		while(b ^ js.buttons) wait_for_event(fd, &js);
		printf("Move axis %d to %s position and push any button.\n", j,  pos_name[i]);

		while (!(b ^ js.buttons)) {
			print_position(j, js.axis[j]);
			wait_for_event(fd, &js);
		}

		putcs("Hold ... ");

		corda[j].cmin[i] = js.axis[j];
		corda[j].cmax[i] = js.axis[j];

		t = get_time();

		while (get_time() < t + 2000 && (b ^ js.buttons)) {
			if (js.axis[j] < corda[j].cmin[i]) corda[j].cmin[i] = js.axis[j];
			if (js.axis[j] > corda[j].cmax[i]) corda[j].cmax[i] = js.axis[j];
			wait_for_event(fd, &js);
		}
		puts("OK.");
	}

	puts("");

	for (j = 0; j < axes; j++) {
		solve_broken(corr[j].coef, corda[j]);
		corr[j].type = JS_CORR_BROKEN;
	}

	puts("Setting correction to:");
	for (i = 0; i < axes; i++) {
		printf("Correction for axis %d: %s, precision: %d.\n",
			i, corr_name[(int)corr[i].type], corr[i].prec);
		if (corr_coef_num[(int)corr[i].type]) {
			printf("Coeficients:");
			for(j = 0; j < corr_coef_num[(int)corr[i].type]; j++) {
				printf(" %d", corr[i].coef[j]);
				if (j < corr_coef_num[(int)corr[i].type] - 1) putchar(',');
			}
		putchar('\n');
		}
	}

	putchar('\n');

	if (ioctl(fd, JSIOCSCORR, &corr)) {
		perror("jscal: error setting correction");
		exit(1);
	}
}

void print_version()
{
	printf("JsCal was compiled for driver version: %d.%d.%d\n", JS_VERSION >> 16,
		(JS_VERSION >> 8) & 0xff, JS_VERSION & 0xff);
	printf("Current running driver version: %d.%d.%d\n", version >> 16,
		(version >> 8) & 0xff, version & 0xff);
}

void print_settings(char *devicename)
{
	int i,j;

	if (ioctl(fd, JSIOCGAXES, &axes)) {
		perror("jscal: error getting axes");
		exit(1);
	}
	if (ioctl(fd, JSIOCGBUTTONS, &buttons)) {
		perror("jscal: error getting buttons");
		exit(1);
	}
	if (ioctl(fd, JSIOCGCORR, &corr)) {
		perror("jscal: error getting correction");
		exit(1);
	}

	printf("jscal -s %d", axes);
	for (i = 0; i < axes; i++) {
		printf( ",%d,%d", corr[i].type, corr[i].prec);
		for (j = 0; j < corr_coef_num[(int)corr[i].type]; j++)
			printf(",%d", corr[i].coef[j]);
	}
	printf(" %s\n",devicename);
}

void set_correction(char *p)
{
	int i,j;
	int t = 0;

	if (ioctl(fd, JSIOCGAXES, &axes)) {
		perror("jscal: error getting axes");
		exit(1);
	}

	if (axes > MAX_AXES) axes = MAX_AXES;

	if (!p) {
		fprintf(stderr, "jscal: missing number of axes\n");
		exit(1);
	}
	sscanf(p, "%d", &t);
	p = strstr(p, ",");

	if (t != axes) {
		fprintf(stderr, "jscal: joystick has different number of axes (%d) than specified in command line (%d)\n", 
			axes, t);
		exit(1);
	}


	for (i = 0; i < axes; i++) {

		if (!p) {
			fprintf(stderr, "jscal: missing correction type for axis %d\n", i);
			exit(1);
		}
		sscanf(++p, "%d", &t);
		p = strstr(p, ",");


		if (t > MAX_CORR) {
			fprintf(stderr, "jscal: unknown correction type for axis %d\n", i);
			exit(1);
		}
		corr[i].type = t;

		if (!p) {
			fprintf(stderr, "jscal: missing precision for axis %d\n", i);
			exit(1);
		}
		sscanf(++p, "%d", &t);
		p = strstr(p, ",");

		corr[i].prec = t;

		for(j = 0; j < corr_coef_num[corr[i].type]; j++) {
			if (!p) {
				fprintf(stderr, "jscal: missing coefficient %d for axis %d\n", j, i);
				exit(1);
			}
			sscanf(++p, "%d", (int*) &corr[i].coef[j]);
			p = strstr(p, ",");
		}
	}

	if (p) {
		fprintf(stderr, "jscal: too many values\n");
		exit(1);
	}
	
	if (ioctl(fd, JSIOCSCORR, &corr)) {
		perror("jscal: error setting correction");
		exit(1);
	}
}

void test_center()
{
	int i;
	struct js_event ev;

	if (ioctl(fd, JSIOCGAXES, &axes)) {
		perror("jscal: error getting axes");
		exit(1);
	}

	if (ioctl(fd, JSIOCGBUTTONS, &buttons)) {
		perror("jscal: error getting buttons");
		exit(1);
	}

	if (fcntl(fd, F_SETFL, O_NONBLOCK)) {
		perror("jscal: cannot set nonblocking mode");
		exit(1);
	}

	while (read(fd, &ev, sizeof(struct js_event)) == sizeof(struct js_event)) {
		switch (ev.type & ~JS_EVENT_INIT) {
			case JS_EVENT_AXIS:
				js.axis[ev.number] = ev.value; break;
			case JS_EVENT_BUTTON:
				js.buttons = (js.buttons & ~(1 << ev.number)) | (ev.value << ev.number);
		}
	}

	for (i = 0; i < axes; i++) if (js.axis[i]) {
		fprintf(stderr, "jscal: axes not calibrated\n");
		exit(2);
	}
	if (js.buttons) {
		fprintf(stderr, "jscal: buttons pressed\n");
		exit(3);
	}
}

int action = 0;

int main(int argc, char **argv)
{
	int option_index = 0;
	char *parameter = NULL;
	int t;

	static struct option long_options[] =
	{
		{"calibrate", no_argument, NULL, 'c'},
		{"help", no_argument, NULL, 'h'},
		{"set-correction", required_argument, NULL, 's'},
		{"test-center", no_argument, NULL, 't'},
		{"version", no_argument, NULL, 'V'},
		{"print-correction", no_argument, NULL, 'p'}
	};

	if (argc == 1) {
		help();
		exit(1);
	}

	do {
		t = getopt_long(argc, argv, "chps:vVt", long_options, &option_index);
		switch (t) {
			case 'p':
			case 's':
			case 'c':
			case 't':
			case 'V':
				if (action) {
					fprintf(stderr, "jscal: more than one action specified\n");
					exit(1);
				} else {
					action = t;
					if ((parameter=optarg)) strcpy(parameter,optarg);
				}
				break;
			case 'h':
				help();
				exit(0);
			case 0:
			case EOF:
				break;
			case ':':
				fprintf(stderr, "jscal: missing parameter\n");
				exit(1);
			case '?':
				fprintf(stderr, "jscal: unknown option\n");
				exit(1);
			default:
				fprintf(stderr, "jscal: option parsing error\n");
		}
	} while (t != EOF);

	if (argc != optind + 1) {
		fprintf(stderr, "jscal: missing devicename\n");
		exit(1);
	}

	if ((fd = open(argv[argc - 1], O_RDONLY)) < 0) {
		perror("jscal: can't open joystick device");
		exit(1);
	}

	if (ioctl(fd, JSIOCGVERSION, &version)) {
		perror("jscal: error getting version");
		exit(1);
	}
	if (version != JS_VERSION) {
		fprintf(stderr, "jscal: wrong version\n");
		print_version();
		exit(1);
	}

	switch (action) {
		case 0:
			print_info();
			break;
		case 'c':
			print_info();
			calibrate();
			break;
		case 'p':
			print_settings(argv[argc -1]);
			break;
		case 's':
			set_correction(parameter);
			break;
		case 't':
			test_center();
			break;
		case 'V':
			print_version();
			break;
		default:
			fprintf(stderr, "jscal: this cannot happen\n");
			exit(1);
	}

	close(fd);
	return 0;
}
