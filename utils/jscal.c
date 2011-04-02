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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
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
#include <stdlib.h>

#include <asm/param.h>
#include <linux/joystick.h>

#define PIT_HZ 1193180L

#define NUM_POS 3
#define MAX_CORR 1

const char *pos_name[] = {"minimum", "center", "maximum"};
const char *corr_name[] = {"none (raw)", "broken line"};
const char corr_coef_num[] = {0,4};

struct correction_data {
	int cmin[NUM_POS];
	int cmax[NUM_POS];
};

int fd;
struct js_corr corr[ABS_MAX + 1];
__u8 axmap[ABS_MAX + 1];
__u8 axmap2[ABS_MAX + 1];
__u16 buttonmap[(KEY_MAX - BTN_MISC + 1)];
char axes, buttons, fuzz;
int version;
struct correction_data corda[ABS_MAX + 1];

struct js_info {
	int buttons;
	int axis[ABS_MAX + 1];
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
	puts("  -q             --print-mappings    Print the current axis and button");
  puts("                                       mappings as a jscal command line");
	puts("  -u <n_of_axes,axmap1,axmap2,...,");
  puts("      n_of_buttons,btnmap1,btnmap2,");
  puts("      ...>       --set-mappings      Sets axis and button mappings to the");
  puts("                                        specified values");
	putchar('\n');
}

void print_info()
{
	int i,j;

	if (ioctl(fd, JSIOCGAXES, &axes) < 0) {
		perror("jscal: error getting axes");
		exit(1);
	}
	if (ioctl(fd, JSIOCGBUTTONS, &buttons) < 0) {
		perror("jscal: error getting buttons");
		exit(1);
	}
	if (ioctl(fd, JSIOCGCORR, &corr) < 0) {
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
	int axis, pos;

	for (i=0; i<ABS_MAX + 1; i++) {
		corr[i].type = JS_CORR_NONE;
		corr[i].prec = 0;
	}

	if (ioctl(fd, JSIOCSCORR, &corr) < 0) {
		perror("jscal: error setting correction");
		exit(1);
	}

	{

		int i;
		int amax[ABS_MAX + 1], amin[ABS_MAX + 1];

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
				if (amin[i] > js.axis[i]) {
					amin[i] = js.axis[i];
					t = get_time();
				}
				if (amax[i] < js.axis[i]) {
					amax[i] = js.axis[i];
					t = get_time();
				}
				printf("Axis %d:%5d,%5d ", i, amin[i], amax[i]);
			}
			printf("\r");
			fflush(stdout);
		} while (get_time() < t+4000);

		printf("Done. Precision is:                                             \n");

		for (i=0; i < axes; i++) {
			corr[i].prec = amax[i] - amin[i];
			printf("Axis: %d: %5d\n", i, corr[i].prec);
		}

		puts("");

	}


	b = js.buttons;

	for (axis = 0; axis < axes; axis++)
		for (pos = 0; pos < NUM_POS; pos++) {
			while(b ^ js.buttons) wait_for_event(fd, &js);
			printf("Move axis %d to %s position and push any button.\n", axis, pos_name[pos]);

			while (!(b ^ js.buttons)) {
				print_position(axis, js.axis[axis]);
				wait_for_event(fd, &js);
			}

			putcs("Hold ... ");

			corda[axis].cmin[pos] = js.axis[axis];
			corda[axis].cmax[pos] = js.axis[axis];

			t = get_time();

			while (get_time() < t + 2000 && (b ^ js.buttons)) {
				if (js.axis[axis] < corda[axis].cmin[pos]) {
					corda[axis].cmin[pos] = js.axis[axis];
					t = get_time();
				}
				if (js.axis[axis] > corda[axis].cmax[pos]) {
					corda[axis].cmax[pos] = js.axis[axis];
					t = get_time();
				}
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

	if (ioctl(fd, JSIOCSCORR, &corr) < 0) {
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

void print_mappings(char *devicename)
{
	int i;

	if (ioctl(fd, JSIOCGAXES, &axes) < 0) {
		perror("jscal: error getting axes");
		exit(1);
	}
	if (ioctl(fd, JSIOCGBUTTONS, &buttons) < 0) {
		perror("jscal: error getting buttons");
		exit(1);
	}
	if (ioctl(fd, JSIOCGAXMAP, &axmap) < 0) {
		perror("jscal: error getting axis map");
		exit(1);
	}
	if (ioctl(fd, JSIOCGBTNMAP, &buttonmap) < 0) {
	        buttons=0;
	}

	printf("jscal -u %d", axes);
	for (i = 0; i < axes; i++)
  {
		printf( ",%d", axmap[i]);
	}

  printf(",%d", buttons);
	for (i = 0; i < buttons; i++)
  {
		printf( ",%d", buttonmap[i]);
	}

	printf(" %s\n",devicename);
}


void get_axmap2(void)
{
        if (ioctl(fd, JSIOCGAXMAP, &axmap2) < 0) {
		perror("jscal: error getting axis map");
		exit(1);
	}
}

/*
 * Remap the calibration data to fit the (potentially) new axis map.
 * axmap2 stores the original axis map, axmap the new one.
 */
void correct_axes(void)
{
        int axmes[ABS_MAX + 1];
        struct js_corr corr_tmp[ABS_MAX + 1];
        int i;
        int ax[axes];
	//Create remapping table
        for(i=0;i<axes;++i){
	        axmes[(axmap2[i])]=i;
	}
	for(i=0;i<axes;++i){
	        ax[i]=axmes[(axmap[i])];
	}
	//Read again current callibration settings
	if (ioctl(fd, JSIOCGCORR, &corr) < 0) {
		perror("jscal: error getting correction");
		exit(1);
	}
	//Remap callibration settings
	for (i = 0; i < axes; i++) {
	        corr_tmp[i]=corr[(ax[i])];
	}
	if (ioctl(fd, JSIOCSCORR, &corr_tmp) < 0) {
		perror("jscal: error setting correction");
		exit(1);
	}

}

void print_settings(char *devicename)
{
	int i,j;

	if (ioctl(fd, JSIOCGAXES, &axes) < 0) {
		perror("jscal: error getting axes");
		exit(1);
	}
	if (ioctl(fd, JSIOCGBUTTONS, &buttons) < 0) {
		perror("jscal: error getting buttons");
		exit(1);
	}
	if (ioctl(fd, JSIOCGCORR, &corr) < 0) {
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

// n axes                      n buttons
// 10,0,1,2,5,6,16,17,40,41,42:13,288,289,290,291,292,293,294,295,296,297,298,299,300
void set_mappings(char *p)
{
	int i;
	int axes_on_cl = 0;
	int btns_on_cl = 0;
  int axis_mapping = 0;
  int btn_mapping = 0;

	if (ioctl(fd, JSIOCGAXES, &axes) < 0) {
		perror("jscal: error getting axes");
		exit(1);
	}
	if (ioctl(fd, JSIOCGBUTTONS, &buttons) < 0) {
		perror("jscal: error getting buttons");
		exit(1);
	}

	if (axes > ABS_MAX + 1) axes = ABS_MAX + 1;

	if (!p) {
		fprintf(stderr, "jscal: missing argument for --set-mappings\n");
		exit(1);
	}

   //axes
	sscanf(p, "%d", &axes_on_cl);
	p = strstr(p, ",");

	if (axes_on_cl != axes) {
		fprintf(stderr, "jscal: joystick has %d axes and not %d as specified on command line\n", 
			axes, axes_on_cl);
		exit(1);
	}


	for (i = 0; i < axes; i++)
  {
		if (!p) {
			fprintf(stderr, "jscal: missing mapping for axis %d\n", i);
			exit(1);
		}
		sscanf(++p, "%d", &axis_mapping);
		p = strstr(p, ",");


		if (axis_mapping > ABS_MAX + 1) {
			fprintf(stderr, "jscal: invalid axis mapping for axis %d (max is %d)\n", i, ABS_MAX + 1);
			exit(1);
		}
		axmap[i] = axis_mapping;
	}

  //buttons
	sscanf(++p, "%d", &btns_on_cl);
	p = strstr(p, ",");

	if ((btns_on_cl != buttons)&&(btns_on_cl!=0)) {
		fprintf(stderr, "jscal: joystick has %d buttons and not %d as specified on command line\n", 
			buttons, btns_on_cl);
		exit(1);
	}


	for (i = 0; i < btns_on_cl; i++)
	  {
		if (!p) {
			fprintf(stderr, "jscal: missing mapping for button %d\n", i);
			exit(1);
		}
		sscanf(++p, "%d", &btn_mapping);
		p = strstr(p, ",");


		if (btn_mapping > KEY_MAX) {
			fprintf(stderr, "jscal: invalid button mapping for button %d (max is %d)\n", i, KEY_MAX);
			exit(1);
		}
		if (btn_mapping < BTN_MISC) {
			fprintf(stderr, "jscal: invalid button mapping for button %d (min is %d)\n", i, BTN_MISC);
			exit(1);
		}
		buttonmap[i] = btn_mapping;
	  }

	if (p) {
		fprintf(stderr, "jscal: too many values\n");
		exit(1);
	}

	// Save the current axis map
	get_axmap2();
	
	// Apply the new axis map
	if (ioctl(fd, JSIOCSAXMAP, &axmap) < 0) {
		perror("jscal: error setting axis map");
		exit(1);
	}

	// Move the calibration data accordingly
	correct_axes();

	if (btns_on_cl!=0){
		if (ioctl(fd, JSIOCSBTNMAP, &buttonmap) < 0) {
		       perror("jscal: error setting button map");
	               exit(1);
		}
       }
}

void set_correction(char *p)
{
	int i,j;
	int t = 0;

	if (ioctl(fd, JSIOCGAXES, &axes) < 0) {
		perror("jscal: error getting axes");
		exit(1);
	}

	if (axes > ABS_MAX + 1) axes = ABS_MAX + 1;

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
	
	if (ioctl(fd, JSIOCSCORR, &corr) < 0) {
		perror("jscal: error setting correction");
		exit(1);
	}
}

void test_center()
{
	int i;
	struct js_event ev;

	if (ioctl(fd, JSIOCGAXES, &axes) < 0) {
		perror("jscal: error getting axes");
		exit(1);
	}

	if (ioctl(fd, JSIOCGBUTTONS, &buttons) < 0) {
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

  // /usr/include/getopt.h
	static struct option long_options[] =
	{
		{"calibrate", no_argument, NULL, 'c'},
		{"help", no_argument, NULL, 'h'},
		{"set-correction", required_argument, NULL, 's'},
		{"set-mappings", required_argument, NULL, 'u'},
		{"test-center", no_argument, NULL, 't'},
		{"version", no_argument, NULL, 'V'},
		{"print-correction", no_argument, NULL, 'p'},
		{"print-mappings", no_argument, NULL, 'q'},
    {NULL, no_argument, NULL, 0 }
	};

	if (argc == 1) {
		help();
		exit(1);
	}

	do {
		t = getopt_long(argc, argv, "chpqu:s:vVt", long_options, &option_index);
		switch (t) {
			case 'p':
			case 'q':
			case 's':
			case 'u':
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

	if (ioctl(fd, JSIOCGVERSION, &version) < 0) {
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
		case 'q':
			print_mappings(argv[argc -1]);
			break;
		case 's':
			set_correction(parameter);
			break;
		case 'u':
			set_mappings(parameter);
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
