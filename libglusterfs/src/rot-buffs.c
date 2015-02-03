/*
  Copyright (c) 2008-2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

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
 * TODO: use bitmaps during lockless writer completion.
 */

inline void *
rbuf_alloc_rvec ()
{
        return GF_CALLOC (1, RLIST_IOV_MELDED_ALLOC_SIZE, gf_common_mt_rvec_t);
}

inline void
rbuf_reset_rvec (rbuf_iovec_t *rvec)
{
        /* iov_base is _never_ modified */
        rvec->iov.iov_len = 0;
}

/* TODO: alloc multiple rbuf_iovec_t */
inline int
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
        rlist->used++;
        return 0;
}

inline void
rlist_free_rvec (rbuf_iovec_t *rvec)
{
        if (!rvec)
                return;
        list_del (&rvec->list);
        GF_FREE (rvec);
}

inline void
rlist_purge_all_rvec (rbuf_list_t *rlist)
{
        rbuf_iovec_t *rvec = NULL;

        if (!rlist)
                return;
        while (list_empty (&rlist->veclist)) {
                rvec = list_first_entry (&rlist->veclist, rbuf_iovec_t, list);
                rlist_free_rvec (rvec);
        }
}

inline void
rbuf_purge_rlist (rbuf_t *rbuf)
{
        rbuf_list_t *rlist = NULL;

        while (list_empty (&rbuf->freelist)) {
                rlist = list_first_entry (&rbuf->freelist, rbuf_list_t, list);
                list_del (&rlist->list);

                rlist_purge_all_rvec (rlist);

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

        ret = pthread_spin_init (&rbuf->lock, 0);
        if (ret != 0)
                goto dealloc_rbuf;
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

                MARK_RLIST_ACTIVE (rlist);
                list_add_tail (&rlist->list, &rbuf->freelist);
        }

        if (ret != 0)
                goto dealloc_rlist;

        /* cache currently used buffer: first in the list */
        rbuf->current = list_first_entry (&rbuf->freelist, rbuf_list_t, list);
        return rbuf;

 dealloc_rlist:
        rbuf_purge_rlist (rbuf);
 dealloc_rbuf:
        (void) pthread_spin_destroy (&rbuf->lock);
        GF_FREE (rbuf);
 error_return:
        return NULL;
}

void
rbuf_dtor (rbuf_t *rbuf)
{
        rbuf->current = NULL;
        rbuf_purge_rlist (rbuf);
        (void) pthread_spin_destroy (&rbuf->lock);

        GF_FREE (rbuf);
}

inline char *
rbuf_adjust_write_area (struct iovec *iov, size_t bytes)
{
        char *wbuf = NULL;

        wbuf = iov->iov_base + iov->iov_len;
        iov->iov_len += bytes;
        return wbuf;
}

inline char *
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

        if (!rbuf || (bytes <= 0) || !opaque)
                return NULL;

        pthread_spin_lock (&rbuf->lock);
        {
                rlist = RBUF_CURRENT_BUFFER (rbuf);
                wbuf = rbuf_alloc_write_area (rlist, bytes);
                if (!wbuf)
                        goto unlock;
                rlist->pending++;
        }
 unlock:
        pthread_spin_unlock (&rbuf->lock);

        if (wbuf)
                *opaque = rlist;
        return wbuf;
}

int
rbuf_write_complete (void *opaque)
{
        rbuf_list_t *rlist = NULL;

        if (!opaque)
                return -1;

        rlist = opaque;

        pthread_mutex_lock (&rlist->b_lock);
        {
                rlist->completed++;
                if (rlist->awaiting)
                        pthread_cond_signal (&rlist->b_cond);
        }
        pthread_mutex_unlock (&rlist->b_lock);

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

        pthread_spin_lock (&rbuf->lock);
        {
                rlist = RBUF_CURRENT_BUFFER (rbuf);
                if (!rlist->pending) {
                        retval = RBUF_EMPTY;
                        goto unlock;
                }

                list_del_init (&rlist->list);
                if (list_empty (&rbuf->freelist)) {

                        /* removal would lead to writer starvation, disallow */
                        list_add (&rlist->list, &rbuf->freelist);
                        retval = RBUF_WOULD_STARVE;
                        goto unlock;
                }

                if (seqfn)
                        seqfn (rlist, mydata);
                rbuf->current =
                        list_first_entry (&rbuf->freelist, rbuf_list_t, list);
        }
 unlock:
        pthread_spin_unlock (&rbuf->lock);

        if (retval == RBUF_CONSUMABLE) {
                *opaque = rlist; /* caller _owns_ the buffer */
                MARK_RLIST_WAITING (rlist);
        }
        return retval;
}

/* TODO: shrink ->veclist using hueristics */
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
                while (rlist->completed != rlist->pending) {
                        rlist->awaiting = _gf_true;
                        pthread_cond_wait (&rlist->b_cond, &rlist->b_lock);
                }
        }
        pthread_mutex_unlock (&rlist->b_lock);

        fn (rlist, arg); /* invoke handler */

        rlist->awaiting = _gf_false;
        MARK_RLIST_COMPLETE (rlist);

        rlist->rvec = list_first_entry (&rlist->veclist, rbuf_iovec_t, list);
        rbuf_reset_rvec (rlist->rvec);

        rlist->used = 1;
        rlist->pending = rlist->completed = 0;

        pthread_spin_lock (&rbuf->lock);
        {
                list_add_tail (&rlist->list, &rbuf->freelist);
        }
        pthread_spin_unlock (&rbuf->lock);

        return 0;
}
