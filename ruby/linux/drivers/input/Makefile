#
# Makefile for Linux input driver
#
# (c) 1998-1999 Vojtech Pavlik
#
# Sponsored by SuSE
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
#
# Should you need to contact me, the author, you can do so either by
# e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
# Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
#
#
# Edit the options below to suit your needs
#

CC		= gcc

CPPFLAGS        = -I.#                                          # for *.h

CFLAGS		= -O2 -Wall -pipe -m486 -fomit-frame-pointer#	# i386
# CFLAGS	= -O2 -Wall -pipe -m68020 -ffixed-a2#		# m68k
# CFLAGS	= -O2 -Wall -pipe -fomit-frame-pointer -ffixed-8# Alpha
# CFLAGS	= -O2 -Wall -pipe#				# Generic

MODFLAGS	= -D__KERNEL__ -DMODULE#			# Normal modules
# MODFLAGS	= -D__KERNEL__ -DMODULE -DMODVERSIONS\
#		  -include /usr/src/linux/include/linux/modversions.h # Versioned modules

DRIVERS		= atkbd.o ct82c710.o evbug.o evdev.o \
		  i8042.o inport.o input.o iobug.o joydev.o \
		  logibm.o magellan.o mousedev.o parkbd.o \
		  psmouse.o serio.o sermouse.o serport.o sunkbd.o\
		  warrior.o xtkbd.o gameport.o adi.o sidewinder.o ns558.o

PROGRAMS	= evtest inputattach jstest jscal

#
# Nothing should need to be changed below this line
#

compile: $(DRIVERS) $(PROGRAMS)

atkbd.o: atkbd.c atkbd.h input.h serio.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c atkbd.c -o atkbd.o

ct82c710.o: ct82c710.c serio.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c ct82c710.c -o ct82c710.o

evbug.o: evbug.c input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c evbug.c -o evbug.o

evdev.o: evdev.c input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c evdev.c -o evdev.o

i8042.o: i8042.c i8042.h serio.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c i8042.c -o i8042.o

inport.o: inport.c input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c inport.c -o inport.o

input.o: input.c input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c input.c -o input.o

iobug.o: iobug.c serio.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c iobug.c -o iobug.o

joydev.o: joydev.c input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c joydev.c -o joydev.o

logibm.o: logibm.c input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c logibm.c -o logibm.o

magellan.o: magellan.c input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c magellan.c -o magellan.o

mousedev.o: mousedev.c input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c mousedev.c -o mousedev.o

parkbd.o: parkbd.c serio.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c parkbd.c -o parkbd.o

psmouse.o: psmouse.c serio.h input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c psmouse.c -o psmouse.o

serio.o: serio.c serio.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c serio.c -o serio.o

sermouse.o: sermouse.c serio.h input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c sermouse.c -o sermouse.o

serport.o: serport.c input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c serport.c -o serport.o

sunkbd.o: sunkbd.c sunkbd.h serio.h input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c sunkbd.c -o sunkbd.o

warrior.o: warrior.c serio.h input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c warrior.c -o warrior.o

xtkbd.o: xtkbd.c serio.h input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c xtkbd.c -o xtkbd.o

gameport.o: gameport.c gameport.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c gameport.c -o gameport.o

adi.o: adi.c gameport.h input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c adi.c -o adi.o

sidewinder.o: sidewinder.c gameport.h input.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c sidewinder.c -o sidewinder.o

ns558.o: ns558.c gameport.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MODFLAGS) -c ns558.c -o ns558.o

clean:
	-$(RM) *.o *.swp $(PROGRAMS) *.orig *.rej map *~

jscal: jscal.o
	$(CC) $(CFLAGS) $(CPPFLAGS) -lm $^ -o $@

jstest.o: jstest.c joystick.h
jscal.o: jscal.c joystick.h
jsattach.o: jsattach.c
