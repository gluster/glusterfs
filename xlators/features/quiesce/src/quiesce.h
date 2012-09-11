/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __QUIESCE_H__
#define __QUIESCE_H__

#include "quiesce-mem-types.h"
#include "xlator.h"
#include "timer.h"

#define GF_FOPS_EXPECTED_IN_PARALLEL 512

typedef struct {
        gf_timer_t       *timer;
        gf_boolean_t      pass_through;
        gf_lock_t         lock;
        struct list_head  req;
        int               queue_size;
        pthread_t         thr;
        struct mem_pool  *local_pool;
} quiesce_priv_t;

typedef struct {
        fd_t               *fd;
        char               *name;
        char               *volname;
        loc_t               loc;
        off_t               size;
        off_t               offset;
        mode_t              mode;
        int32_t             flag;
        struct iatt         stbuf;
        struct iovec       *vector;
        struct iobref      *iobref;
        dict_t             *dict;
        struct gf_flock     flock;
        entrylk_cmd         cmd;
        entrylk_type        type;
        gf_xattrop_flags_t  xattrop_flags;
        int32_t             wbflags;
        uint32_t            io_flag;
} quiesce_local_t;

#endif
