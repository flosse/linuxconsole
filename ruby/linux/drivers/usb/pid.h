/*
 *  PID Force feedback support for hid devices.
 *
 *  Copyright (c) 2002 Rodrigo Damazio.
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
 * Should you need to contact me, the author, you can do so by
 * e-mail - mail your message to <rdamazio@lsi.usp.br>
 */

#define FF_EFFECTS_MAX 64

#define FF_PID_FLAGS_USED	1
#define FF_PID_FLAGS_UPDATING	2

#define FF_PID_FALSE	0
#define FF_PID_TRUE	1

struct hid_pid_effect {
    unsigned int flags;
    pid_t owner;
    struct ff_effect effect;
};

struct hid_ff_pid {
    struct hid_device *hid;

    struct urb *urbffout;
    struct usb_ctrlrequest ffcr;

    char ctrl_buffer[8];

    unsigned long int gain;
    unsigned int max_effects;

    struct hid_pid_effect effects[FF_EFFECTS_MAX];
};
