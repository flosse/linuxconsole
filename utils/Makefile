#
# $Id$
#
# Makefile for Linux input utilities
#
# © 1998-2000 Vojtech Pavlik (sponsored by SuSE)
# © 2008-2012 Stephen Kitt <steve@sk2.org>
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301 USA.
#
#
# Edit the options below to suit your needs
#

CFLAGS		?= -g -O2 -Wall

PROGRAMS	= inputattach jstest jscal fftest ffmvforce ffset \
		  ffcfstress jscal-restore jscal-store

PREFIX          ?= /usr/local

compile: $(PROGRAMS)

distclean: clean
clean:
	$(RM) *.o *.swp $(PROGRAMS) *.orig *.rej map *~

ffcfstress: ffcfstress.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -funsigned-char $^ $(LDFLAGS) -lm -o $@

ffmvforce.o: ffmvforce.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $^ -o $@ `sdl-config --cflags`

ffmvforce: ffmvforce.o
	$(CC) $^ -o $@ $(LDFLAGS) -g -lm `sdl-config --libs`

axbtnmap.o: axbtnmap.c axbtnmap.h

jscal.o: jscal.c axbtnmap.h

jscal: jscal.o axbtnmap.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ -lm -o $@

jstest.o: jstest.c axbtnmap.h

jstest: jstest.o axbtnmap.o

gencodes: gencodes.c scancodes.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) gencodes.c -o $@

jscal-restore: jscal-restore.in
	sed "s^@@PREFIX@@^$(PREFIX)^g" < $^ > $@

jscal-store: jscal-store.in
	sed "s^@@PREFIX@@^$(PREFIX)^g" < $^ > $@

install: compile
	install -d $(DESTDIR)$(PREFIX)/bin
	install $(PROGRAMS) $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(PREFIX)/share/joystick
	install extract filter ident $(DESTDIR)$(PREFIX)/share/joystick
	install -d $(DESTDIR)/lib/udev
	install js-set-enum-leds $(DESTDIR)/lib/udev

.PHONY: compile clean distclean install
