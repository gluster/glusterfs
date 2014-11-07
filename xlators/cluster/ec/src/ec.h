/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_H__
#define __EC_H__

#include "xlator.h"
#include "timer.h"

#define EC_XATTR_CONFIG  "trusted.ec.config"
#define EC_XATTR_SIZE    "trusted.ec.size"
#define EC_XATTR_VERSION "trusted.ec.version"
#define EC_XATTR_HEAL    "trusted.ec.heal"

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
