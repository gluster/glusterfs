/*
   Copyright (c) 2010-2011-2011-2011 Gluster, Inc. <http://www.gluster.com>
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


#ifndef __QUIESCE_H__
#define __QUIESCE_H__

#include "quiesce-mem-types.h"
#include "xlator.h"
#include "timer.h"

#define GF_FOPS_EXPECTED_IN_PARALLEL 4096

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
} quiesce_local_t;

#endif
