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

#ifndef THROTTLE_TBF_H__
#define THROTTLE_TBF_H__

typedef enum tbf_ops {
        TBF_OP_MIN     = -1,
        TBF_OP_HASH    = 0,    /* checksum calculation  */
        TBF_OP_READ    = 1,    /* inode read(s)         */
        TBF_OP_READDIR = 2,    /* dentry read(s)        */
        TBF_OP_MAX     = 3,
} tbf_ops_t;

/**
 * Operation rate specification
 */
typedef struct tbf_opspec {
        tbf_ops_t op;

        unsigned long rate;

        unsigned long maxlimit;

        unsigned long token_gen_interval;/* Token generation interval in usec */
} tbf_opspec_t;

/**
 * Token bucket for each operation type
 */
typedef struct tbf_bucket {
        gf_lock_t lock;

        pthread_t tokener;         /* token generator thread          */

        unsigned long tokenrate;   /* token generation rate           */

        unsigned long tokens;      /* number of current tokens        */

        unsigned long maxtokens;   /* maximum token in the bucket     */

        struct list_head queued;   /* list of non-conformant requests */

        unsigned long token_gen_interval;/* Token generation interval in usec */
} tbf_bucket_t;

typedef struct tbf {
        tbf_bucket_t **bucket;
} tbf_t;

tbf_t *
tbf_init (tbf_opspec_t *, unsigned int);

int
tbf_mod (tbf_t *, tbf_opspec_t *);

void
tbf_throttle (tbf_t *, tbf_ops_t, unsigned long);

#define TBF_THROTTLE_BEGIN(tbf, op, tokens) (tbf_throttle (tbf, op, tokens))
#define TBF_THROTTLE_END(tbf, op, tokens)

#endif /** THROTTLE_TBF_H__ */
