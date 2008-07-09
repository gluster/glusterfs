# Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
# This file is part of GlusterFS.
#
# GlusterFS is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# GlusterFS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


xlator_PROGRAMS = bdb.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/storage

bdb_so_SOURCES = bctx.c bdb-ll.c bdb.c

noinst_HEADERS = bdb.h 

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64 -D_GNU_SOURCE -D$(GF_HOST_OS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles 

AM_LDFLAGS = -ldb

CLEANFILES = 
DISTCLEANFILES = .deps/*.P Makefile.in

