/*
  Copyright (c) 2012 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

/* This is used by rpcgen to auto generate the rpc stubs.
 * mount3udp_svc.c is heavily modified though
 */

const MNTUDPPATHLEN = 1024;

typedef string mntudpdirpath<MNTPATHLEN>;

program MOUNTUDP_PROGRAM {
        version MOUNTUDP_V3 {
                void MOUNTUDPPROC3_NULL(void) = 0;
                mountres3 MOUNTUDPPROC3_MNT (mntudpdirpath) = 1;
                mountstat3 MOUNTUDPPROC3_UMNT (mntudpdirpath) = 3;
        } = 3;
} = 100005;
