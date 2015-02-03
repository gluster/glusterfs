/*
  Copyright (c) 2008-2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __ROT_BUFFS_H
#define __ROT_BUFFS_H

#include "list.h"
#include "locking.h"
#include "common-utils.h"

typedef struct rbuf_iovec {
        struct iovec iov;

        struct list_head list;
} rbuf_iovec_t;

#define RBUF_IOVEC_SIZE  (sizeof (rbuf_iovec_t))

typedef struct rbuf_list {
        gf_lock_t c_lock;

        pthread_mutex_t b_lock;          /* protects this structure */
        pthread_cond_t b_cond;           /* signal for writer completion */

        gf_boolean_t awaiting;

        unsigned long long pending;      /* pending writers */
        unsigned long long completed;    /* completed writers */

        rbuf_iovec_t *rvec;              /* currently used IO vector */

        struct list_head veclist;        /* list of attached rbuf_iov */

        unsigned long long used;         /* consumable entries
                                             attached in ->veclist */
        unsigned long long total;        /* total entries in ->veclist (used
                                            during deallocation) */

        unsigned long seq[2];            /* if interested, this whould store
                                            the start sequence number and the
                                            range */

        struct list_head list;           /* attachment to rbuf_t */
} rbuf_list_t;

struct rlist_iter {
        struct list_head veclist;

        unsigned long long iter;
};

#define RLIST_ENTRY_COUNT(rlist)  rlist->used

#define rlist_iter_init(riter, rlist)              \
        do {                                       \
                (riter)->iter = rlist->used;       \
                (riter)->veclist = rlist->veclist; \
        } while (0)

#define rvec_for_each_entry(pos, riter)                                 \
        for (pos = list_entry                                           \
                     ((riter)->veclist.next, typeof(*pos), list);       \
             (riter)->iter > 0;                                         \
             pos = list_entry                                           \
                     (pos->list.next, typeof(*pos), list),              \
                     --((riter)->iter))

/**
 * Sequence number assigment routine is called during buffer
 * switch under rbuff ->lock.
 */
typedef void (sequence_fn) (rbuf_list_t *, void *);

#define RLIST_STORE_SEQ(rlist, start, range)    \
        do {                                    \
                rlist->seq[0] = start;          \
                rlist->seq[1] = range;          \
        } while (0)

#define RLIST_GET_SEQ(rlist, start, range)      \
        do {                                    \
                start = rlist->seq[0];          \
                range = rlist->seq[1];          \
        } while (0)

typedef struct rbuf {
        gf_lock_t lock;        /* protects "current" rlist */

        rbuf_list_t *current;  /* cached pointer to first free rlist */

        struct list_head freelist;
} rbuf_t;

typedef enum {
        RBUF_CONSUMABLE = 1,
        RBUF_BUSY,
        RBUF_EMPTY,
        RBUF_WOULD_STARVE,
} rlist_retval_t;

/* Initialization/Destruction */
rbuf_t *rbuf_init (int);
void rbuf_dtor (rbuf_t *);

/* Producer API */
char *rbuf_reserve_write_area (rbuf_t *, size_t, void **);
int rbuf_write_complete (void *);

/* Consumer API */
int rbuf_get_buffer (rbuf_t *, void **, sequence_fn *, void *);
int rbuf_wait_for_completion (rbuf_t *, void *,
                              void (*)(rbuf_list_t *, void *), void *);

#endif
