/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "list.h"
#include "xlator.h"
#include "locking.h"

#ifndef __BIT_ROT_TBF_H__
#define __BIT_ROT_TBF_H__

typedef enum br_tbf_ops {
        BR_TBF_OP_MIN     = -1,
        BR_TBF_OP_HASH    = 0,    /* checksum calculation  */
        BR_TBF_OP_READ    = 1,    /* inode read(s)         */
        BR_TBF_OP_READDIR = 2,    /* dentry read(s)        */
        BR_TBF_OP_MAX     = 3,
} br_tbf_ops_t;

/**
 * Operation rate specification
 */
typedef struct br_tbf_opspec {
        br_tbf_ops_t op;

        unsigned long rate;

        unsigned long maxlimit;
} br_tbf_opspec_t;

/**
 * Token bucket for each operation type
 */
typedef struct br_tbf_bucket {
        gf_lock_t lock;

        pthread_t tokener;         /* token generator thread          */

        unsigned long tokenrate;   /* token generation rate           */

        unsigned long tokens;      /* number of current tokens        */

        unsigned long maxtokens;   /* maximum token in the bucket     */

        struct list_head queued;   /* list of non-conformant requests */
} br_tbf_bucket_t;

typedef struct br_tbf {
        br_tbf_bucket_t **bucket;
} br_tbf_t;

br_tbf_t *
br_tbf_init (br_tbf_opspec_t *, unsigned int);

int
br_tbf_mod (br_tbf_t *, br_tbf_opspec_t *);

void
br_tbf_throttle (br_tbf_t *, br_tbf_ops_t, unsigned long);

#define TBF_THROTTLE_BEGIN(tbf, op, tokens) (br_tbf_throttle (tbf, op, tokens))
#define TBF_THROTTLE_END(tbf, op, tokens) (void)

#endif /** __BIT_ROT_TBF_H__ */
