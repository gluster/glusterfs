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

CLEANFILES = 

install-data-am:
	[ "${GF_HOST_OS}" == "GF_DARWIN_HOST_OS" ] && cp mount_glusterfs /sbin/ || cp mount.glusterfs /sbin/
	[ "${GF_HOST_OS}" == "GF_DARWIN_HOST_OS" ] && chmod +x /sbin/mount_glusterfs || chmod +x /sbin/mount.glusterfs

