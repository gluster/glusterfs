# Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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


xlator_PROGRAMS = posix.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/storage

posix_so_SOURCES = posix.c 
posix_so_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

noinst_HEADERS = posix.h 

AM_CFLAGS = -fPIC -fno-strict-aliasing -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles \
	$(GF_CFLAGS)

CLEANFILES = 

