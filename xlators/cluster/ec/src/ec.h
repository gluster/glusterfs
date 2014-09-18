/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __EC_H__
#define __EC_H__

#include "xlator.h"
#include "timer.h"

#define EC_XATTR_CONFIG  "trusted.ec.config"
#define EC_XATTR_SIZE    "trusted.ec.size"
#define EC_XATTR_VERSION "trusted.ec.version"

struct _ec;
typedef struct _ec ec_t;

struct _ec
{
    xlator_t *        xl;
    int32_t           nodes;
    int32_t           bits_for_nodes;
    int32_t           fragments;
    int32_t           redundancy;
    uint32_t          fragment_size;
    uint32_t          stripe_size;
    int32_t           up;
    uint32_t          idx;
    uint32_t          xl_up_count;
    uintptr_t         xl_up;
    uintptr_t         node_mask;
    xlator_t **       xl_list;
    gf_lock_t         lock;
    gf_timer_t *      timer;
    struct mem_pool * fop_pool;
    struct mem_pool * cbk_pool;
    struct mem_pool * lock_pool;
};

#endif /* __EC_H__ */
