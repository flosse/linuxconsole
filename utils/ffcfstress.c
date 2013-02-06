/*
 * ffcfstress.c
 *
 * Force Feedback: Constant Force Stress Test
 *
 * Copyright (C) 2001 Oliver Hamann
 *
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

#include <linux/input.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>

#include "bitmaskros.h"


/* Default values for the options */
#define DEFAULT_DEVICE_NAME "/dev/input/event0"
#define DEFAULT_UPDATE_RATE      25.0
#define DEFAULT_MOTION_FREQUENCY  0.1
#define DEFAULT_MOTION_AMPLITUDE  1.0
#define DEFAULT_SPRING_STRENGTH   1.0
#define DEFAULT_AXIS_INDEX          0
#define DEFAULT_AXIS_CODE       ABS_X

static const char* axis_names[] = { "X", "Y", "Z", "RX", "RY", "RZ", "WHEEL" };
static const int axis_codes[] = { ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_WHEEL };

/* Options */
const char * device_name = DEFAULT_DEVICE_NAME;
double update_rate       = DEFAULT_UPDATE_RATE;
double motion_frequency  = DEFAULT_MOTION_FREQUENCY;
double motion_amplitude  = DEFAULT_MOTION_AMPLITUDE;
double spring_strength   = DEFAULT_SPRING_STRENGTH;
int    axis_index        = DEFAULT_AXIS_INDEX;
int    axis_code         = DEFAULT_AXIS_CODE;
int stop_and_play = 0;  /* Stop-upload-play effects instead of updating */
int autocenter_off = 0; /* switch the autocentering off */


/* Global variables about the initialized device */
int device_handle;
int axis_min, axis_max;
struct ff_effect effect;


/* Parse command line arguments */
void parse_args(int argc, char * argv[])
{
	int i;

	int help = (argc < 2);

	for (i=1; i<argc && !help; i++) {
	        if (!strcmp(argv[i],"-d")) {
		        if (i<argc-1) device_name = argv[++i]; else help = 1;
		} else if (!strcmp(argv[i],"-u")) {
		        if (i<argc-1) update_rate = atof(argv[++i]); else help = 1;
		} else if (!strcmp(argv[i],"-f")) {
		        if (i<argc-1) motion_frequency=atof(argv[++i]); else help = 1;
		} else if (!strcmp(argv[i],"-a")) {
		        if (i<argc-1) motion_amplitude=atof(argv[++i]); else help = 1;
		} else if (!strcmp(argv[i],"-s")) {
		        if (i<argc-1) spring_strength =atof(argv[++i]); else help = 1;
		} else if (!strcmp(argv[i],"-x")) {
		        if (i<argc-1) { 
			        axis_index = atoi(argv[++i]); 
				if (axis_index < 0 || axis_index >= sizeof(axis_names)/sizeof(char*))
				    help = 1;
				else
				    axis_code = axis_codes[axis_index];
			} else help = 1;
		} else if (!strcmp(argv[i],"-o")) {
			;
		} else if (!strcmp(argv[i],"-A")) {
			autocenter_off = 1;
		} else help = 1;
	}
 

	if (help) {
		printf("-------- ffcfstress - Force Feedback: Constant Force Stress Test --------\n");
		printf("Description:\n");
		printf("  This program is for stress testing constant non-enveloped forces on\n");
		printf("  a force feedback device via the event interface. It simulates a\n");
		printf("  moving spring force by a frequently updated constant force effect.\n");
		printf("  BE CAREFUL IN USAGE, YOUR DEVICE MAY GET DAMAGED BY THE STRESS TEST!\n");
		printf("Usage:\n");
		printf("  %s <option> [<option>...]\n",argv[0]);
		printf("Options:\n");
		printf("  -d <string>  device name (default: %s)\n",DEFAULT_DEVICE_NAME);
		printf("  -u <double>  update rate in Hz (default: %.2f)\n",DEFAULT_UPDATE_RATE);
		printf("  -f <double>  spring center motion frequency in Hz (default: %.2f)\n",DEFAULT_MOTION_FREQUENCY);
		printf("  -a <double>  spring center motion amplitude 0.0..1.0 (default: %.2f)\n",DEFAULT_MOTION_AMPLITUDE);
		printf("  -s <double>  spring strength factor (default: %.2f)\n",DEFAULT_SPRING_STRENGTH);
		printf("  -x <int>     absolute axis to test (default: %d=%s)\n",DEFAULT_AXIS_INDEX, axis_names[DEFAULT_AXIS_INDEX]);
		printf("               (0 = X, 1 = Y, 2 = Z, 3 = RX, 4 = RY, 5 = RZ, 6 = WHEEL)\n");
		printf("  -A           switch off auto-centering\n");
		printf("  -o           dummy option (useful because at least one option is needed)\n");
		exit(1);
	}
}



/* Initialize device, create constant force effect */
void init_device()
{
	unsigned char key_bits[1 + KEY_MAX/8/sizeof(unsigned char)];
	unsigned char abs_bits[1 + ABS_MAX/8/sizeof(unsigned char)];
	unsigned char ff_bits[1 + FF_MAX/8/sizeof(unsigned char)];

	struct input_event event;
	struct input_absinfo absinfo;

	/* Open event device with write permission */
	device_handle = open(device_name,O_RDWR|O_NONBLOCK);
	if (device_handle<0) {
		fprintf(stderr,"ERROR: can not open %s (%s) [%s:%d]\n",
		        device_name,strerror(errno),__FILE__,__LINE__);
		exit(1);
	}

	/* Which buttons has the device? */
	memset(key_bits,0,sizeof(key_bits));
	if (ioctl(device_handle,EVIOCGBIT(EV_KEY,sizeof(key_bits)),key_bits)<0) {
		fprintf(stderr,"ERROR: can not get key bits (%s) [%s:%d]\n",
		        strerror(errno),__FILE__,__LINE__);
		exit(1);
	}

	/* Which axes has the device? */
	memset(abs_bits,0,sizeof(abs_bits));
	if (ioctl(device_handle,EVIOCGBIT(EV_ABS,sizeof(abs_bits)),abs_bits)<0) {
		fprintf(stderr,"ERROR: can not get abs bits (%s) [%s:%d]\n",
		        strerror(errno),__FILE__,__LINE__);
		exit(1);
	}

	/* Now get some information about force feedback */
	memset(ff_bits,0,sizeof(ff_bits));
	if (ioctl(device_handle,EVIOCGBIT(EV_FF ,sizeof(ff_bits)),ff_bits)<0) {
		fprintf(stderr,"ERROR: can not get ff bits (%s) [%s:%d]\n",
		        strerror(errno),__FILE__,__LINE__);
		exit(1);
	}

	/* Check if selected axis is available */
	if (!testBit(axis_code, abs_bits)) {
		fprintf(stderr,"ERROR: selected axis %s not available [%s:%d] (see available ones with fftest)\n",
		        axis_names[axis_index], __FILE__,__LINE__);
		exit(1);
	}

	/* get axis value range */
	if (ioctl(device_handle,EVIOCGABS(axis_code),&absinfo)<0) {
		fprintf(stderr,"ERROR: can not get axis value range (%s) [%s:%d]\n",
		        strerror(errno),__FILE__,__LINE__);
		exit(1);
	}
	axis_min=absinfo.minimum;
	axis_max=absinfo.maximum;
	if (axis_min>=axis_max) {
		fprintf(stderr,"ERROR: bad axis value range (%d,%d) [%s:%d]\n",
		        axis_min,axis_max,__FILE__,__LINE__);
		exit(1);
	}

	/* force feedback supported? */
	if (!testBit(FF_CONSTANT,ff_bits)) {
		fprintf(stderr,"ERROR: device (or driver) has no constant force feedback support [%s:%d]\n",
		        __FILE__,__LINE__);
		exit(1);
	}

	/* Switch off auto centering */
	if (autocenter_off) {
		memset(&event,0,sizeof(event));
		event.type=EV_FF;
		event.code=FF_AUTOCENTER;
		event.value=0;
		if (write(device_handle,&event,sizeof(event))!=sizeof(event)) {
			fprintf(stderr,"ERROR: failed to disable auto centering (%s) [%s:%d]\n",
				strerror(errno),__FILE__,__LINE__);
			exit(1);
		}
	}

	/* Initialize constant force effect */
	memset(&effect,0,sizeof(effect));
	effect.type=FF_CONSTANT;
	effect.id=-1;
	effect.trigger.button=0;
	effect.trigger.interval=0;
	effect.replay.length=0xffff;
	effect.replay.delay=0;
	effect.u.constant.level=0;
	effect.direction=0xC000;
	effect.u.constant.envelope.attack_length=0;
	effect.u.constant.envelope.attack_level=0;
	effect.u.constant.envelope.fade_length=0;
	effect.u.constant.envelope.fade_level=0;

	/* Upload effect */
	if (ioctl(device_handle,EVIOCSFF,&effect)<0) {
		fprintf(stderr,"ERROR: uploading effect failed (%s) [%s:%d]\n",
		        strerror(errno),__FILE__,__LINE__);
		exit(1);
	}

	/* Start effect */
	memset(&event,0,sizeof(event));
	event.type=EV_FF;
	event.code=effect.id;
	event.value=1;
	if (write(device_handle,&event,sizeof(event))!=sizeof(event)) {
		fprintf(stderr,"ERROR: starting effect failed (%s) [%s:%d]\n",
		        strerror(errno),__FILE__,__LINE__);
		exit(1);
	}
}


/* update the device: set force and query joystick position */
void update_device(double force, double * position)
{
	struct input_event event;

	/* Delete effect */
	if (stop_and_play && effect.id!=-1) {
		if (ioctl(device_handle,EVIOCRMFF,effect.id)<0) {
			fprintf(stderr,"ERROR: removing effect failed (%s) [%s:%d]\n",
			        strerror(errno),__FILE__,__LINE__);
			exit(1);
		}
		effect.id=-1;
	}

	/* Set force */
	if (force>1.0) force=1.0;
	else if (force<-1.0) force=-1.0;
	effect.u.constant.level=(short)(force*32767.0);
	effect.direction=0xC000;
	effect.u.constant.envelope.attack_level=(short)(force*32767.0); /* this one counts! */
	effect.u.constant.envelope.fade_level=(short)(force*32767.0); /* only to be safe */

	/* Upload effect */
	if (ioctl(device_handle,EVIOCSFF,&effect)<0) {
		perror("upload effect");
		/* We do not exit here. Indeed, too frequent updates may be
		 * refused, but that is not a fatal error */
	}

	/* Start effect */
	if (stop_and_play && effect.id!=-1) {
		memset(&event,0,sizeof(event));
		event.type=EV_FF;
		event.code=effect.id;
		event.value=1;
		if (write(device_handle,&event,sizeof(event))!=sizeof(event)) {
			fprintf(stderr,"ERROR: re-starting effect failed (%s) [%s:%d]\n",
			        strerror(errno),__FILE__,__LINE__);
			exit(1);
		}
	}

	/* Get events */
	while (read(device_handle,&event,sizeof(event))==sizeof(event)) {
		if (event.type==EV_ABS && event.code==axis_code) {
			*position=((double)(((short)event.value)-axis_min))*2.0/(axis_max-axis_min)-1.0;
			if (*position>1.0) *position=1.0;
			else if (*position<-1.0) *position=-1.0;
		}
	}
}


/* little helper to print a graph bar from a value */
void fprint_bar(FILE * file, double value, int radius)
{
	int i,c;

	for (i=0; i<radius*2+1; i++) {
		if (i==radius) c='|';
		else if ((i<radius && value*radius<i-radius+0.25) ||
		         (i>radius && value*radius>i-radius-0.25)) c='*';
		else if ((i<radius && value*radius<i-radius+0.75) ||
		         (i>radius && value*radius>i-radius-0.75)) c='+';
		else if (i==0) c='<';
		else if (i==radius*2) c='>';
		else c='-';
		fputc(c,file);
	}
}


/* main: perform the spring simulation */
int main(int argc, char * argv[])
{
	double time,position,center,force;

	/* Parse command line arguments */
	parse_args(argc,argv);

	/* Initialize device, create constant force effect */
	init_device();

	/* Print header */
	printf("\n        position                   center                     force\n");

	/* For ever */
	for (position=0, time=0;; time+=1.0/update_rate) {

		/* Spring center oscillates */
		center = sin( time * 2 * M_PI * motion_frequency ) * motion_amplitude;

		/* Calculate spring force */
		force = ( center - position ) * spring_strength;
		if (force >  1.0) force =  1.0;
		if (force < -1.0) force = -1.0;

		/* Print graph bars */
		printf("\r");
		fprint_bar(stdout,position,12);
		printf(" ");
		fprint_bar(stdout,center,12);
		printf(" ");
		fprint_bar(stdout,force,12);
		fflush(stdout);

		/* Set force and ask for joystick position */
		update_device(force,&position);

		/* Next time... */
		usleep((unsigned long)(1000000.0/update_rate));

	}
}
