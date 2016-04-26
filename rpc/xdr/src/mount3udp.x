/*
  Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifdef RPC_XDR
%#include "rpc-pragmas.h"
#endif
%#include "compat.h"

/* This is used by rpcgen to auto generate the rpc stubs.
 * mount3udp_svc.c is heavily modified though
 */

%#include "xdr-nfs3.h"

const MNTUDPPATHLEN = 1024;

typedef string mntudpdirpath<MNTPATHLEN>;

program MOUNTUDP_PROGRAM {
        version MOUNTUDP_V3 {
                void MOUNTUDPPROC3_NULL(void) = 0;
                mountres3 MOUNTUDPPROC3_MNT (mntudpdirpath) = 1;
                mountstat3 MOUNTUDPPROC3_UMNT (mntudpdirpath) = 3;
        } = 3;
} = 100005;
