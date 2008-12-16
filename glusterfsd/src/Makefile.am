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

sbin_PROGRAMS = glusterfsd

glusterfsd_SOURCES = glusterfsd.c fetch-spec.c
glusterfsd_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la $(GF_LDADD)
glusterfsd_LDFLAGS = $(GF_LDFLAGS) $(GF_GLUSTERFS_LDFLAGS)
noinst_HEADERS = glusterfsd.h

AM_CFLAGS = -fPIC -Wall -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\" $(GF_GLUSTERFS_CFLAGS)

CLEANFILES = 

$(top_builddir)/libglusterfs/src/libglusterfs.la:
	make -C $(top_builddir)/libglusterfs/src/ all

uninstall-local:
	rm -f $(DESTDIR)$(sbindir)/glusterfs

install-data-local:
	$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/run
	$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/log/glusterfs
	rm -f $(DESTDIR)$(sbindir)/glusterfs
	ln -s glusterfsd $(DESTDIR)$(sbindir)/glusterfs
