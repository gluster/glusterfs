/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _ATOMIC_H
#define _ATOMIC_H

#include <inttypes.h>

#if defined(HAVE_ATOMIC_BUILTINS) || defined(HAVE_SYNC_BUILTINS)
/* optimized implementation, macros only */

typedef struct gf_atomic_t {
        int64_t    cnt;
} gf_atomic_t;

#if defined(HAVE_ATOMIC_BUILTINS)

/* all macros have a 'gf_atomic_t' as 1st argument */
#define GF_ATOMIC_INIT(op, n)     __atomic_store (&(op.cnt), __ATOMIC_RELEASE)
#define GF_ATOMIC_GET(op)         __atomic_load (&(op.cnt), __ATOMIC_ACQUIRE)
#define GF_ATOMIC_INC(op)         __atomic_add_and_fetch (&(op.cnt), 1, \
                                                          __ATOMIC_ACQ_REL)
#define GF_ATOMIC_DEC(op)         __atomic_sub_and_fetch (&(op.cnt), 1, \
                                                          __ATOMIC_ACQ_REL)
#define GF_ATOMIC_ADD(op, n)      __atomic_add_and_fetch (&(op.cnt), n, \
                                                          __ATOMIC_ACQ_REL)
#define GF_ATOMIC_SUB(op, n)      __atomic_sub_and_fetch (&(op.cnt), n, \
                                                          __ATOMIC_ACQ_REL)

#else /* !HAVE_ATOMIC_BUILTINS, but HAVE_SYNC_BUILTINS */

/* all macros have a 'gf_atomic_t' as 1st argument */
#define GF_ATOMIC_INIT(op, n)     ({ op.cnt = n; __sync_synchronize (); })
#define GF_ATOMIC_GET(op)         __sync_add_and_fetch (&(op.cnt), 0)
#define GF_ATOMIC_INC(op)         __sync_add_and_fetch (&(op.cnt), 1)
#define GF_ATOMIC_DEC(op)         __sync_sub_and_fetch (&(op.cnt), 1)
#define GF_ATOMIC_ADD(op, n)      __sync_add_and_fetch (&(op.cnt), n)
#define GF_ATOMIC_SUB(op, n)      __sync_sub_and_fetch (&(op.cnt), n)

#endif /* HAVE_ATOMIC_BUILTINS || HAVE_SYNC_BUILTINS */

#else /* no HAVE_(ATOMIC|SYNC)_BUILTINS */
/* fallback implementation, using small inline functions to improve type
 * checking while compiling */

#include "locking.h"

typedef struct gf_atomic_t {
        int64_t    cnt;
        gf_lock_t  lk;
} gf_atomic_t;


static inline void
gf_atomic_init (gf_atomic_t *op, int64_t cnt)
{
        LOCK_INIT (&op->lk);
        op->cnt = cnt;
}


static inline uint64_t
gf_atomic_get (gf_atomic_t *op)
{
        uint64_t ret;

        LOCK (&op->lk);
        {
               ret = op->cnt;
        }
        UNLOCK (&op->lk);

        return ret;
}


static inline int64_t
gf_atomic_add (gf_atomic_t *op, int64_t n)
{
        uint64_t ret;

        LOCK (&op->lk);
        {
                op->cnt += n;
                ret = op->cnt;
        }
        UNLOCK (&op->lk);

        return ret;
}


#define GF_ATOMIC_INIT(op, cnt)   gf_atomic_init (&op, cnt)
#define GF_ATOMIC_GET(op)         gf_atomic_get (&op)
#define GF_ATOMIC_INC(op)         gf_atomic_add (&op, 1)
#define GF_ATOMIC_DEC(op)         gf_atomic_add (&op, -1)
#define GF_ATOMIC_ADD(op, n)      gf_atomic_add (&op, n)
#define GF_ATOMIC_SUB(op, n)      gf_atomic_add (&op, -n)

#endif /* HAVE_ATOMIC_SYNC_OPS */

#endif /* _ATOMIC_H */
