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
%#include "rpc-common-xdr.h"
%#include "glusterfs-fops.h"
%#include "glusterfs3-xdr.h"

 struct gfs3_fsetattr_req_v2 {
        opaque gfid[16];
        hyper        fd;
        struct gf_iatt stbuf;
        int        valid;
        opaque   xdata<>; /* Extra data */
}  ;

 struct gfs3_rchecksum_req_v2 {
        opaque gfid[16];
        hyper   fd;
        unsigned hyper  offset;
        unsigned int  len;
        opaque   xdata<>; /* Extra data */
}  ;
