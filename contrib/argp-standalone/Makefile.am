# From glibc

# Copyright (C) 1997, 2003, 2004 Free Software Foundation, Inc.
# This file is part of the GNU C Library.

# The GNU C Library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public License as
# published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.

# The GNU C Library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.

# You should have received a copy of the GNU Library General Public
# License along with the GNU C Library; see the file COPYING.LIB.  If
# not, write to the Free Software Foundation, Inc.,
# 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
AUTOMAKE_OPTIONS = foreign
SUBDIRS = .

LIBOBJS = @LIBOBJS@

noinst_LIBRARIES = libargp.a

noinst_HEADERS = argp.h argp-fmtstream.h argp-namefrob.h

EXTRA_DIST = mempcpy.c strchrnul.c strndup.c strcasecmp.c vsnprintf.c autogen.sh

# Leaves out argp-fs-xinl.c and argp-xinl.c 
libargp_a_SOURCES = argp-ba.c argp-eexst.c argp-fmtstream.c \
		    argp-help.c argp-parse.c argp-pv.c \
		    argp-pvh.c

libargp_a_LIBADD = $(LIBOBJS)


