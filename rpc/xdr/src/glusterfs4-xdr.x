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
%#include "glusterfs-fops.h"
%#include "glusterfs3-xdr.h"


union gfx_value switch (gf_dict_data_type_t type) {
        case GF_DATA_TYPE_INT:
                hyper value_int;
        case GF_DATA_TYPE_UINT:
                unsigned hyper value_uint;
        case GF_DATA_TYPE_DOUBLE:
                double value_dbl;
        case GF_DATA_TYPE_STR:
                opaque val_string<>;
        case GF_DATA_TYPE_IATT:
                gf_iatt iatt;
        case GF_DATA_TYPE_GFUUID:
                opaque uuid[20];
        case GF_DATA_TYPE_PTR:
                opaque other<>;
};

struct gfx_dict_pair {
       opaque key<>;
       gfx_value value;
};

struct gfx_dict {
       unsigned int count;
       gfx_dict_pair pairs<>;
};

/* fops */
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

struct gfs4_icreate_rsp {
       int op_ret;
       int op_errno;
       gf_iatt stat;
       opaque xdata<>;
};

struct gfs4_icreate_req {
       opaque gfid[16];
       unsigned int mode;
       opaque xdata<>;
};

struct gfs4_namelink_rsp {
       int op_ret;
       int op_errno;
       gf_iatt preparent;
       gf_iatt postparent;
       opaque xdata<>;
};

struct gfs4_namelink_req {
       opaque pargfid[16];
       string bname<>;
       opaque xdata<>;
};

struct gfs4_inodelk_contention_req {
        opaque                gfid[16];
        struct gf_proto_flock flock;
        unsigned int          pid;
        string                domain<>;
        opaque                xdata<>;
};

struct gfs4_entrylk_contention_req {
        opaque                gfid[16];
        unsigned int          type;
        unsigned int          pid;
        string                name<>;
        string                domain<>;
        opaque                xdata<>;
};
