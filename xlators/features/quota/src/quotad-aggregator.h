/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _QUOTAD_AGGREGATOR_H
#define _QUOTAD_AGGREGATOR_H

#include "quota.h"
#include "stack.h"
#include "glusterfs3-xdr.h"
#include "inode.h"

typedef struct {
        void          *pool;
        xlator_t      *this;
	xlator_t      *active_subvol;
        inode_table_t *itable;
        loc_t          loc;
        dict_t        *xdata;
} quotad_aggregator_state_t;

typedef int (*quotad_aggregator_lookup_cbk_t) (xlator_t *this,
                                               call_frame_t *frame,
                                               void *rsp);
int
qd_nameless_lookup (xlator_t *this, call_frame_t *frame, gfs3_lookup_req *req,
                    dict_t *xdata, quotad_aggregator_lookup_cbk_t lookup_cbk);
int
quotad_aggregator_init (xlator_t *this);

#endif
