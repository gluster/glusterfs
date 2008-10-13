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

xlator_LTLIBRARIES = posix-locks.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

posix_locks_la_LDFLAGS = -module -avoidversion

posix_locks_la_SOURCES = common.c posix.c internal.c
posix_locks_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

noinst_HEADERS = locks.h common.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -fno-strict-aliasing -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src $(GF_CFLAGS) -shared -nostartfiles

CLEANFILES = 

