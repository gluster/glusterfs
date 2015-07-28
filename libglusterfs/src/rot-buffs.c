/*
  Copyright (c) 2008-2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <math.h>

#include "mem-types.h"
#include "mem-pool.h"

#include "rot-buffs.h"

/**
 * Producer-Consumer based on top of rotational buffers.
 *
 * This favours writers (producer) and keeps the critical section
 * light weight. Buffer switch happens when a consumer wants to
 * consume data. This is the slow path and waits for pending
 * writes to finish.
 *
 * TODO: do away with opaques (use arrays with indexing).
 */

#define ROT_BUFF_DEFAULT_COUNT  2
#define ROT_BUFF_ALLOC_SIZE  (1 * 1024 * 1024)  /* 1MB per iovec */

#define RLIST_IOV_MELDED_ALLOC_SIZE  (RBUF_IOVEC_SIZE + ROT_BUFF_ALLOC_SIZE)

/**
 * iovec list is not shrinked (deallocated) if usage/total count
 * falls in this range. this is the fast path and should satisfy
 * most of the workloads. for the rest shrinking iovec list is
 * generous.
 */
#define RVEC_LOW_WATERMARK_COUNT  1
#define RVEC_HIGH_WATERMARK_COUNT (1 << 4)

static inline
rbuf_list_t *rbuf_current_buffer (rbuf_t *rbuf)
{
        return rbuf->current;
}

static void
rlist_mark_waiting (rbuf_list_t *rlist)
{
        LOCK (&rlist->c_lock);
        {
                rlist->awaiting = _gf_true;
        }
        UNLOCK (&rlist->c_lock);
}

static int
__rlist_has_waiter (rbuf_list_t *rlist)
{
        return (rlist->awaiting == _gf_true);
}

static void *
rbuf_alloc_rvec ()
{
        return GF_CALLOC (1, RLIST_IOV_MELDED_ALLOC_SIZE, gf_common_mt_rvec_t);
}

static void
rlist_reset_vector_usage (rbuf_list_t *rlist)
{
        rlist->used = 1;
}

static void
rlist_increment_vector_usage (rbuf_list_t *rlist)
{
        rlist->used++;
}

static void
rlist_increment_total_usage (rbuf_list_t *rlist)
{
        rlist->total++;
}

static int
rvec_in_watermark_range (rbuf_list_t *rlist)
{
        return ((rlist->total >= RVEC_LOW_WATERMARK_COUNT)
                    && (rlist->total <= RVEC_HIGH_WATERMARK_COUNT));
}

static void
rbuf_reset_rvec (rbuf_iovec_t *rvec)
{
        /* iov_base is _never_ modified */
        rvec->iov.iov_len = 0;
}

/* TODO: alloc multiple rbuf_iovec_t */
static int
rlist_add_new_vec (rbuf_list_t *rlist)
{
        rbuf_iovec_t *rvec = NULL;

        rvec = (rbuf_iovec_t *) rbuf_alloc_rvec ();
        if (!rvec)
                return -1;
        INIT_LIST_HEAD (&rvec->list);
        rvec->iov.iov_base = ((char *)rvec) + RBUF_IOVEC_SIZE;
        rvec->iov.iov_len = 0;

        list_add_tail (&rvec->list, &rlist->veclist);

        rlist->rvec = rvec; /* cache the latest */

        rlist_increment_vector_usage (rlist);
        rlist_increment_total_usage (rlist);

        return 0;
}

static void
rlist_free_rvec (rbuf_iovec_t *rvec)
{
        if (!rvec)
                return;
        list_del (&rvec->list);
        GF_FREE (rvec);
}

static void
rlist_purge_all_rvec (rbuf_list_t *rlist)
{
        rbuf_iovec_t *rvec = NULL;

        if (!rlist)
                return;
        while (!list_empty (&rlist->veclist)) {
                rvec = list_first_entry (&rlist->veclist, rbuf_iovec_t, list);
                rlist_free_rvec (rvec);
        }
}

static void
rlist_shrink_rvec (rbuf_list_t *rlist, unsigned long long shrink)
{
        rbuf_iovec_t *rvec = NULL;

        while (!list_empty (&rlist->veclist) && (shrink-- > 0)) {
                rvec = list_first_entry (&rlist->veclist, rbuf_iovec_t, list);
                rlist_free_rvec (rvec);
        }
}

static void
rbuf_purge_rlist (rbuf_t *rbuf)
{
        rbuf_list_t *rlist = NULL;

        while (!list_empty (&rbuf->freelist)) {
                rlist = list_first_entry (&rbuf->freelist, rbuf_list_t, list);
                list_del (&rlist->list);

                rlist_purge_all_rvec (rlist);

                LOCK_DESTROY (&rlist->c_lock);

                (void) pthread_mutex_destroy (&rlist->b_lock);
                (void) pthread_cond_destroy (&rlist->b_cond);

                GF_FREE (rlist);
        }
}

rbuf_t *
rbuf_init (int bufcount)
{
        int          j     = 0;
        int          ret   = 0;
        rbuf_t      *rbuf  = NULL;
        rbuf_list_t *rlist = NULL;

        if (bufcount <= 0)
                bufcount = ROT_BUFF_DEFAULT_COUNT;

        rbuf = GF_CALLOC (1, sizeof (rbuf_t), gf_common_mt_rbuf_t);
        if (!rbuf)
                goto error_return;

        LOCK_INIT (&rbuf->lock);
        INIT_LIST_HEAD (&rbuf->freelist);

        /* it could have been one big calloc() but this is just once.. */
        for (j = 0; j < bufcount; j++) {
                rlist = GF_CALLOC (1,
                                   sizeof (rbuf_list_t), gf_common_mt_rlist_t);
                if (!rlist) {
                        ret = -1;
                        break;
                }

                INIT_LIST_HEAD (&rlist->list);
                INIT_LIST_HEAD (&rlist->veclist);

                rlist->pending = rlist->completed = 0;

                ret = rlist_add_new_vec (rlist);
                if (ret)
                        break;

                LOCK_INIT (&rlist->c_lock);

                rlist->awaiting = _gf_false;
                ret = pthread_mutex_init (&rlist->b_lock, 0);
                if (ret != 0) {
                        GF_FREE (rlist);
                        break;
                }

                ret = pthread_cond_init (&rlist->b_cond, 0);
                if (ret != 0) {
                        GF_FREE (rlist);
                        break;
                }

                list_add_tail (&rlist->list, &rbuf->freelist);
        }

        if (ret != 0)
                goto dealloc_rlist;

        /* cache currently used buffer: first in the list */
        rbuf->current = list_first_entry (&rbuf->freelist, rbuf_list_t, list);
        return rbuf;

 dealloc_rlist:
        rbuf_purge_rlist (rbuf);
        LOCK_DESTROY (&rbuf->lock);
        GF_FREE (rbuf);
 error_return:
        return NULL;
}

void
rbuf_dtor (rbuf_t *rbuf)
{
        if (!rbuf)
                return;
        rbuf->current = NULL;
        rbuf_purge_rlist (rbuf);
        LOCK_DESTROY (&rbuf->lock);

        GF_FREE (rbuf);
}

static char *
rbuf_adjust_write_area (struct iovec *iov, size_t bytes)
{
        char *wbuf = NULL;

        wbuf = iov->iov_base + iov->iov_len;
        iov->iov_len += bytes;
        return wbuf;
}

static char *
rbuf_alloc_write_area (rbuf_list_t *rlist, size_t bytes)
{
        int           ret = 0;
        struct iovec *iov = NULL;

        /* check for available space in _current_ IO buffer */
        iov = &rlist->rvec->iov;
        if (iov->iov_len + bytes <= ROT_BUFF_ALLOC_SIZE)
                return rbuf_adjust_write_area (iov, bytes); /* fast path */

        /* not enough bytes, try next available buffers */
        if (list_is_last (&rlist->rvec->list, &rlist->veclist)) {
                /* OH! consumed all vector buffers */
                GF_ASSERT (rlist->used == rlist->total);
                ret = rlist_add_new_vec (rlist);
                if (ret)
                        goto error_return;
        } else {
                /* not the end, have available rbuf_iovec's */
                rlist->rvec = list_next_entry (rlist->rvec, list);
                rlist->used++;
                rbuf_reset_rvec (rlist->rvec);
        }

        iov = &rlist->rvec->iov;
        return rbuf_adjust_write_area (iov, bytes);

 error_return:
        return NULL;
}

char *
rbuf_reserve_write_area (rbuf_t *rbuf, size_t bytes, void **opaque)
{
        char        *wbuf  = NULL;
        rbuf_list_t *rlist = NULL;

        if (!rbuf || (bytes <= 0) || (bytes > ROT_BUFF_ALLOC_SIZE) || !opaque)
                return NULL;

        LOCK (&rbuf->lock);
        {
                rlist = rbuf_current_buffer (rbuf);
                wbuf = rbuf_alloc_write_area (rlist, bytes);
                if (!wbuf)
                        goto unblock;
                rlist->pending++;
        }
 unblock:
        UNLOCK (&rbuf->lock);

        if (wbuf)
                *opaque = rlist;
        return wbuf;
}

static void
rbuf_notify_waiter (rbuf_list_t *rlist)
{
        pthread_mutex_lock (&rlist->b_lock);
        {
                pthread_cond_signal (&rlist->b_cond);
        }
        pthread_mutex_unlock (&rlist->b_lock);
}

int
rbuf_write_complete (void *opaque)
{
        rbuf_list_t *rlist = NULL;
        gf_boolean_t notify = _gf_false;

        if (!opaque)
                return -1;

        rlist = opaque;

        LOCK (&rlist->c_lock);
        {
                rlist->completed++;
                /**
                 * it's safe to test ->pending without rbuf->lock *only* if
                 * there's a waiter as there can be no new incoming writes.
                 */
                if (__rlist_has_waiter (rlist)
                                  && (rlist->completed == rlist->pending))
                        notify = _gf_true;
        }
        UNLOCK (&rlist->c_lock);

        if (notify)
                rbuf_notify_waiter (rlist);

        return 0;
}

int
rbuf_get_buffer (rbuf_t *rbuf,
                 void **opaque, sequence_fn *seqfn, void *mydata)
{
        int retval = RBUF_CONSUMABLE;
        rbuf_list_t *rlist = NULL;

        if (!rbuf || !opaque)
                return -1;

        LOCK (&rbuf->lock);
        {
                rlist = rbuf_current_buffer (rbuf);
                if (!rlist->pending) {
                        retval = RBUF_EMPTY;
                        goto unblock;
                }

                if (list_is_singular (&rbuf->freelist)) {
                        /**
                         * removal would lead to writer starvation, disallow
                         * switching.
                         */
                        retval = RBUF_WOULD_STARVE;
                        goto unblock;
                }

                list_del_init (&rlist->list);
                if (seqfn)
                        seqfn (rlist, mydata);
                rbuf->current =
                        list_first_entry (&rbuf->freelist, rbuf_list_t, list);
        }
 unblock:
        UNLOCK (&rbuf->lock);

        if (retval == RBUF_CONSUMABLE)
                *opaque = rlist; /* caller _owns_ the buffer */

        return retval;
}

/**
 * Wait for completion of pending writers and invoke dispatcher
 * routine (for buffer consumption).
 */

static void
__rbuf_wait_for_writers (rbuf_list_t *rlist)
{
        while (rlist->completed != rlist->pending)
                pthread_cond_wait (&rlist->b_cond, &rlist->b_lock);
}

#ifndef M_E
#define M_E 2.7
#endif

static void
rlist_shrink_vector (rbuf_list_t *rlist)
{
        unsigned long long shrink = 0;

        /**
         * fast path: don't bother to deallocate if vectors are hardly
         * used.
         */
        if (rvec_in_watermark_range (rlist))
                return;

        /**
         * Calculate the shrink count based on total allocated vectors.
         * Note that the calculation sticks to rlist->total irrespective
         * of the actual usage count (rlist->used). Later, ->used could
         * be used to apply slack to the calculation based on how much
         * it lags from ->total. For now, let's stick to slow decay.
         */
        shrink = rlist->total - (rlist->total * pow (M_E, -0.2));

        rlist_shrink_rvec (rlist, shrink);
        rlist->total -= shrink;
}

int
rbuf_wait_for_completion (rbuf_t *rbuf, void *opaque,
                          void (*fn)(rbuf_list_t *, void *), void *arg)
{
        rbuf_list_t *rlist = NULL;

        if (!rbuf || !opaque)
                return -1;

        rlist = opaque;

        pthread_mutex_lock (&rlist->b_lock);
        {
                rlist_mark_waiting (rlist);
                __rbuf_wait_for_writers (rlist);
        }
        pthread_mutex_unlock (&rlist->b_lock);

        /**
         * from here on, no need of locking until the rlist is put
         * back into rotation.
         */

        fn (rlist, arg); /* invoke dispatcher */

        rlist->awaiting = _gf_false;
        rlist->pending = rlist->completed = 0;

        rlist_shrink_vector (rlist);
        rlist_reset_vector_usage (rlist);

        rlist->rvec = list_first_entry (&rlist->veclist, rbuf_iovec_t, list);
        rbuf_reset_rvec (rlist->rvec);

        LOCK (&rbuf->lock);
        {
                list_add_tail (&rlist->list, &rbuf->freelist);
        }
        UNLOCK (&rbuf->lock);

        return 0;
}
