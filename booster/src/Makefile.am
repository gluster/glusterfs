# Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

ldpreload_PROGRAMS = glusterfs-booster.so
ldpreloaddir = $(libdir)/glusterfs/
glusterfs_booster_so_SOURCES = entry.c 
glusterfs_booster_so_CFLAGS = -I$(top_srcdir)/libglusterfsclient/src/ -D_GNU_SOURCE -D$(GF_HOST_OS) -fPIC -Wall \
	-pthread $(GF_BOOSTER_CFLAGS)
glusterfs_booster_so_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE  \
	-I$(top_srcdir)/libglusterfsclient/src \
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\"
glusterfs_booster_so_LDFLAGS = -shared -nostartfiles
glusterfs_booster_so_LDADD = -L$(top_builddir)/libglusterfs/src -lglusterfs \
	-L$(top_builddir)/libglusterfsclient/src -lglusterfsclient  

CLEANFILES = 

