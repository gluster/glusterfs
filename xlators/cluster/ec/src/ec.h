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

#define EC_XATTR_PREFIX  "trusted.ec."
#define EC_XATTR_CONFIG  EC_XATTR_PREFIX"config"
#define EC_XATTR_SIZE    EC_XATTR_PREFIX"size"
#define EC_XATTR_VERSION EC_XATTR_PREFIX"version"
#define EC_XATTR_HEAL    EC_XATTR_PREFIX"heal"
#define EC_XATTR_DIRTY   EC_XATTR_PREFIX"dirty"

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
    uint32_t          xl_notify_count;
    uintptr_t         xl_notify;
    uintptr_t         node_mask;
    xlator_t **       xl_list;
    gf_lock_t         lock;
    gf_timer_t *      timer;
    struct mem_pool * fop_pool;
    struct mem_pool * cbk_pool;
    struct mem_pool * lock_pool;
    gf_boolean_t      shd;
    gf_boolean_t      iamshd;
};

#endif /* __EC_H__ */
