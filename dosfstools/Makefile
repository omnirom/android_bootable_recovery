# Makefile
#
# Copyright (C) 2008-2014 Daniel Baumann <mail@daniel-baumann.ch>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
# The complete text of the GNU General Public License
# can be found in /usr/share/common-licenses/GPL-3 file.

SHELL := sh -e
LANGUAGES = $(shell cd manpages/po && ls)

DESTDIR =
PREFIX = /usr/local
SBINDIR = $(PREFIX)/sbin
DOCDIR = $(PREFIX)/share/doc
MANDIR = $(PREFIX)/share/man

#OPTFLAGS = -O2 -fomit-frame-pointer -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64
OPTFLAGS = -O2 -fomit-frame-pointer -D_GNU_SOURCE $(shell getconf LFS_CFLAGS)
#WARNFLAGS = -Wall -pedantic -std=c99
WARNFLAGS = -Wall -Wextra -Wno-sign-compare -Wno-missing-field-initializers -Wmissing-prototypes -Wstrict-prototypes -Wwrite-strings
DEBUGFLAGS = -g
CFLAGS += $(OPTFLAGS) $(WARNFLAGS) $(DEBUGFLAGS)

VPATH = src

all: build

build: fatlabel fsck.fat mkfs.fat

fatlabel: boot.o check.o common.o fat.o file.o io.o lfn.o fatlabel.o

fsck.fat: boot.o check.o common.o fat.o file.o io.o lfn.o fsck.fat.o

mkfs.fat: mkfs.fat.o

rebuild: distclean build

install: install-bin install-doc install-man install-symlinks

install-bin: build
	install -d -m 0755 $(DESTDIR)/$(SBINDIR)
	install -m 0755 fatlabel fsck.fat mkfs.fat $(DESTDIR)/$(SBINDIR)

install-doc:
	install -d -m 0755 $(DESTDIR)/$(DOCDIR)/dosfstools
	install -p -m 0644 ChangeLog doc/* $(DESTDIR)/$(DOCDIR)/dosfstools

install-man:
	for MANPAGE in manpages/en/*; \
	do \
		SECTION="8"; \
		mkdir -p $(DESTDIR)/$(MANDIR)/man$${SECTION}/; \
		install -m 0644 $${MANPAGE} $(DESTDIR)/$(MANDIR)/man$${SECTION}/$$(basename $${MANPAGE}); \
	done

	for LANGUAGE in $(LANGUAGES); \
	do \
		for MANPAGE in manpages/$${LANGUAGE}/*; \
		do \
			SECTION="8"; \
			mkdir -p $(DESTDIR)/$(MANDIR)/$${LANGUAGE}/man$${SECTION}/; \
			install -m 0644 $${MANPAGE} $(DESTDIR)/$(MANDIR)/$${LANGUAGE}/man$${SECTION}/$$(basename $${MANPAGE} .$${LANGUAGE}.$${SECTION}).$${SECTION}; \
		done; \
	done
install-symlinks: install-bin install-man
	if [ -e $(DESTDIR)/$(SBINDIR)/fatlabel ]; \
	then \
		ln -sf fatlabel $(DESTDIR)/$(SBINDIR)/dosfslabel; \
		if [ -e $(DESTDIR)/$(MANDIR)/man8/fatlabel.8 ]; \
		then \
			ln -sf fatlabel.8 $(DESTDIR)/$(MANDIR)/man8/dosfslabel.8; \
		fi; \
	fi

	if [ -e $(DESTDIR)/$(SBINDIR)/fsck.fat ]; \
	then \
		ln -sf fsck.fat $(DESTDIR)/$(SBINDIR)/dosfsck; \
		ln -sf fsck.fat $(DESTDIR)/$(SBINDIR)/fsck.msdos; \
		ln -sf fsck.fat $(DESTDIR)/$(SBINDIR)/fsck.vfat; \
		if [ -e $(DESTDIR)/$(MANDIR)/man8/fsck.fat.8 ]; \
		then \
			ln -sf fsck.fat.8 $(DESTDIR)/$(MANDIR)/man8/dosfsck.8; \
			ln -sf fsck.fat.8 $(DESTDIR)/$(MANDIR)/man8/fsck.msdos.8; \
			ln -sf fsck.fat.8 $(DESTDIR)/$(MANDIR)/man8/fsck.vfat.8; \
		fi; \
	fi

	if [ -e $(DESTDIR)/$(SBINDIR)/mkfs.fat ]; \
	then \
		ln -sf mkfs.fat $(DESTDIR)/$(SBINDIR)/mkdosfs; \
		ln -sf mkfs.fat $(DESTDIR)/$(SBINDIR)/mkfs.msdos; \
		ln -sf mkfs.fat $(DESTDIR)/$(SBINDIR)/mkfs.vfat; \
		if [ -e $(DESTDIR)/$(MANDIR)/man8/mkfs.fat.8 ]; \
		then \
			ln -sf mkfs.fat.8 $(DESTDIR)/$(MANDIR)/man8/mkdosfs.8; \
			ln -sf mkfs.fat.8 $(DESTDIR)/$(MANDIR)/man8/mkfs.msdos.8; \
			ln -sf mkfs.fat.8 $(DESTDIR)/$(MANDIR)/man8/mkfs.vfat.8; \
		fi; \
	fi

uninstall: uninstall-symlinks uninstall-man uninstall-doc uninstall-bin

uninstall-bin:
	rm -f $(DESTDIR)/$(SBINDIR)/fatlabel
	rm -f $(DESTDIR)/$(SBINDIR)/fsck.fat
	rm -f $(DESTDIR)/$(SBINDIR)/mkfs.fat

	rmdir --ignore-fail-on-non-empty $(DESTDIR)/$(SBINDIR)

uninstall-doc:
	rm -rf $(DESTDIR)/$(DOCDIR)/dosfstools

	rmdir --ignore-fail-on-non-empty $(DESTDIR)/$(DOCDIR)

uninstall-man:
	for MANPAGE in manpages/en/*; \
	do \
		SECTION="8"; \
		rm -f $(DESTDIR)/$(MANDIR)/man$${SECTION}/$$(basename $${MANPAGE} .en.$${SECTION}).$${SECTION}; \
	done

	for LANGUAGE in $(LANGUAGES); \
	do \
		for MANPAGE in manpages/$${LANGUAGE}/*; \
		do \
			SECTION="8"; \
			rm -f $(DESTDIR)/$(MANDIR)/$${LANGUAGE}/man$${SECTION}/$$(basename $${MANPAGE} .$${LANGUAGE}.$${SECTION}).$${SECTION}; \
		done; \
	done

uninstall-symlinks:
	rm -f $(DESTDIR)/$(SBINDIR)/dosfslabel
	rm -f $(DESTDIR)/$(MANDIR)/man8/dosfslabel.8

	rm -f $(DESTDIR)/$(SBINDIR)/dosfsck
	rm -f $(DESTDIR)/$(MANDIR)/man8/dosfsck.8
	rm -f $(DESTDIR)/$(SBINDIR)/fsck.msdos
	rm -f $(DESTDIR)/$(MANDIR)/man8/fsck.msdos.8
	rm -f $(DESTDIR)/$(SBINDIR)/fsck.vfat
	rm -f $(DESTDIR)/$(MANDIR)/man8/fsck.vfat.8

	rm -f $(DESTDIR)/$(SBINDIR)/mkdosfs
	rm -f $(DESTDIR)/$(MANDIR)/man8/mkdosfs.8
	rm -f $(DESTDIR)/$(SBINDIR)/mkfs.msdos
	rm -f $(DESTDIR)/$(MANDIR)/man8/mkfs.msdos.8
	rm -f $(DESTDIR)/$(SBINDIR)/mkfs.vfat
	rm -f $(DESTDIR)/$(MANDIR)/man8/mkfs.vfat.8

reinstall: distclean install

clean:
	rm -f *.o

distclean: clean
	rm -f fatlabel fsck.fat mkfs.fat

.PHONY: build rebuild install install-bin install-doc install-man install-symlinks uninstall uninstall-bin uninstall-doc uninstall-man uninstall-symlinks reinstall clean distclean
