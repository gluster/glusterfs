/*
 * Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 */

#ifdef RPC_XDR
%#include "rpc-pragmas.h"
#endif
%#include "compat.h"

/* XDR: libgfchangelog -> changelog */

struct changelog_probe_req {
       unsigned int filter;
       char sock[UNIX_PATH_MAX];
};

struct changelog_probe_rsp {
       int op_ret;
};

/* XDR: changelog -> libgfchangelog */
struct changelog_event_req {
       /* sequence number for the buffer */
       unsigned long seq;

       /* time of dispatch */
       unsigned long tv_sec;
       unsigned long tv_usec;
};

struct changelog_event_rsp {
       int op_ret;

       /* ack'd buffers sequence number */
       unsigned long seq;
};
