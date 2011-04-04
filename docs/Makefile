# $Id: Makefile 2413 2011-04-03 17:24:32Z skitt $
#
# Makefile for Linux input utilities
#
# © 2011 Stephen Kitt <steve@sk2.org>
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

MANPAGES	= evtest.1 inputattach.1 jstest.1 jscal.1 fftest.1 \
		  ffmvforce.1 ffset.1 ffcfstress.1 jscal-store.1 \
		  jscal-restore.1

PREFIX          ?= /usr/local

install:
	install -d $(DESTDIR)$(PREFIX)/share/man/man1
	install $(MANPAGES) $(DESTDIR)$(PREFIX)/share/man/man1

.PHONY: install
