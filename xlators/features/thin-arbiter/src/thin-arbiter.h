/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _THIN_ARBITER_H
#define _THIN_ARBITER_H

#include "locking.h"
#include "common-utils.h"
#include "glusterfs.h"
#include "xlator.h"
#include "defaults.h"
#include "list.h"

#define THIN_ARBITER_SOURCE_XATTR "trusted.ta.source"
#define THIN_ARBITER_SOURCE_SIZE 2

#define TA_FAILED_FOP(fop, frame, op_errno)           \
    do {                                              \
        default_##fop##_failure_cbk(frame, op_errno); \
    } while (0)

#define TA_STACK_UNWIND(fop, frame, op_ret, op_errno, params ...)\
    do {                                                        \
        ta_fop_t    *__local    = NULL;                         \
        int32_t     __op_ret   = 0;                             \
        int32_t     __op_errno = 0;                             \
                                                                \
        __local = frame->local;                                 \
        __op_ret = op_ret;                                      \
        __op_errno = op_errno;                                  \
        if (__local) {                                          \
                ta_release_fop (__local);                       \
                frame->local = NULL;                            \
        }                                                       \
        STACK_UNWIND_STRICT (fop, frame, __op_ret,              \
                            __op_errno, params);                \
                                                                \
    } while (0)

struct _ta_fop;
typedef struct _ta_fop ta_fop_t;

struct _ta_fop {
    gf_xattrop_flags_t  xattrop_flags;
    loc_t               loc;
    fd_t                *fd;
    dict_t              *dict;
    dict_t              *brick_xattr;
    int32_t             on_disk[2];
    int32_t             idx;
};




#endif /* _THIN_ARBITER_H */
