# $Id: Makefile 2413 2011-04-03 17:24:32Z skitt $
#
# Makefile for Linux input utilities
#
# © 2011-2012 Stephen Kitt <steve@sk2.org>
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

VERSION := 1.4.3

PACKAGE := linuxconsoletools-$(VERSION)

all: compile

clean distclean compile:
	$(MAKE) -C utils $@

install:
	$(MAKE) -C utils $@
	$(MAKE) -C docs $@

dist: clean
	rm -rf $(PACKAGE)
	mkdir $(PACKAGE)
	cp -a docs utils COPYING Makefile NEWS README $(PACKAGE)
	(cd $(PACKAGE); find . -name .svn -o -name *~ | xargs rm -rf; rm docs/FB-Driver-HOWTO docs/console.txt)
	tar cjf $(PACKAGE).tar.bz2 $(PACKAGE)

.PHONY: all clean distclean compile install dist
