/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

/**
 *
 * Basic token bucket implementation for rate limiting. As of now interfaces
 * to throttle disk read request, directory entry scan and hash calculation
 * are available. To throttle a particular request (operation), the call needs
 * to be wrapped in-between throttling APIs, for e.g.
 *
 *  TBF_THROTTLE_BEGIN (...);  <-- induces "delays" if required
 *  {
 *      call (...);
 *  }
 *  TBF_THROTTLE_END (...);  <-- not used atm, maybe needed later
 *
 */

#include "mem-pool.h"
#include "bit-rot-tbf.h"
#include "bit-rot-stub-mem-types.h"

typedef struct br_tbf_throttle {
        char done;

        pthread_mutex_t mutex;
        pthread_cond_t  cond;

        unsigned long tokens;

        struct list_head list;
} br_tbf_throttle_t;

/**
 * OK. Most implementations of TBF I've come across generate tokens
 * every second (UML, etc..) and some chose sub-second granularity
 * (blk-iothrottle cgroups). TBF algorithm itself does not enforce
 * any logic for choosing generation interval and it seems pretty
 * logical as one could jack up token count per interval w.r.t.
 * generation rate.
 *
 * Value used here is chosen based on a series of test(s) performed
 * to balance object signing time and not maxing out on all available
 * CPU cores. It's obvious to have seconds granularity and jack up
 * token count per interval, thereby achieving close to similar
 * results. Let's stick to this as it seems to be working fine for
 * the set of ops that are throttled.
 */
#define BR_TBF_TOKENGEN_INTERVAL_USEC  600000

static br_tbf_throttle_t *
br_tbf_init_throttle (unsigned long tokens_required)
{
        br_tbf_throttle_t *throttle = NULL;

        throttle = GF_CALLOC (1, sizeof (*throttle),
                              gf_br_mt_br_tbf_throttle_t);
        if (!throttle)
                return NULL;

        throttle->done = 0;
        throttle->tokens = tokens_required;
        INIT_LIST_HEAD (&throttle->list);

        (void) pthread_mutex_init (&throttle->mutex, NULL);
        (void) pthread_cond_init (&throttle->cond, NULL);

        return throttle;
}

void
_br_tbf_dispatch_queued (br_tbf_bucket_t *bucket)
{
        gf_boolean_t xcont = _gf_false;
        br_tbf_throttle_t *tmp = NULL;
        br_tbf_throttle_t *throttle = NULL;

        list_for_each_entry_safe (throttle, tmp, &bucket->queued, list) {

                pthread_mutex_lock (&throttle->mutex);
                {
                        if (bucket->tokens < throttle->tokens) {
                                xcont = _gf_true;
                                goto unblock;
                        }

                        /* this request can now be serviced */
                        throttle->done = 1;
                        list_del_init (&throttle->list);

                        bucket->tokens -= throttle->tokens;
                        pthread_cond_signal (&throttle->cond);
                }
        unblock:
                pthread_mutex_unlock (&throttle->mutex);
                if (xcont)
                        break;
        }
}

void *br_tbf_tokengenerator (void *arg)
{
        unsigned long tokenrate = 0;
        unsigned long maxtokens = 0;
        br_tbf_bucket_t *bucket = arg;

        tokenrate = bucket->tokenrate;
        maxtokens = bucket->maxtokens;

        while (1) {
                usleep (BR_TBF_TOKENGEN_INTERVAL_USEC);

                LOCK (&bucket->lock);
                {
                        bucket->tokens += tokenrate;
                        if (bucket->tokens > maxtokens)
                                bucket->tokens = maxtokens;

                        if (!list_empty (&bucket->queued))
                                _br_tbf_dispatch_queued (bucket);
                }
                UNLOCK (&bucket->lock);
        }

        return NULL;
}

/**
 * There is lazy synchronization between this routine (when invoked
 * under br_tbf_mod() context) and br_tbf_throttle(). *bucket is
 * updated _after_ all the required variables are initialized.
 */
static int32_t
br_tbf_init_bucket (br_tbf_t *tbf, br_tbf_opspec_t *spec)
{
        int ret = 0;
        br_tbf_bucket_t *curr = NULL;
        br_tbf_bucket_t **bucket = NULL;

        GF_ASSERT (spec->op >= BR_TBF_OP_MIN);
        GF_ASSERT (spec->op <= BR_TBF_OP_MAX);

        /* no rate? no throttling. */
        if (!spec->rate)
                return 0;

        bucket = tbf->bucket + spec->op;

        curr = GF_CALLOC (1, sizeof (*curr), gf_br_mt_br_tbf_bucket_t);
        if (!curr)
                goto error_return;

        LOCK_INIT (&curr->lock);
        INIT_LIST_HEAD (&curr->queued);

        curr->tokens = 0;
        curr->tokenrate = spec->rate;
        curr->maxtokens = spec->maxlimit;

        ret = gf_thread_create (&curr->tokener,
                                NULL, br_tbf_tokengenerator, curr);
        if (ret != 0)
                goto freemem;

        *bucket = curr;
        return 0;

 freemem:
        LOCK_DESTROY (&curr->lock);
        GF_FREE (curr);
 error_return:
        return -1;
}

#define BR_TBF_ALLOC_SIZE                                               \
        (sizeof (br_tbf_t) + (BR_TBF_OP_MAX * sizeof (br_tbf_bucket_t)))

br_tbf_t *
br_tbf_init (br_tbf_opspec_t *tbfspec, unsigned int count)
{
        int32_t i = 0;
        int32_t ret = 0;
        br_tbf_t *tbf = NULL;
        br_tbf_opspec_t *opspec = NULL;

        tbf = GF_CALLOC (1, BR_TBF_ALLOC_SIZE, gf_br_mt_br_tbf_t);
        if (!tbf)
                goto error_return;

        tbf->bucket = (br_tbf_bucket_t **) ((char *)tbf + sizeof (*tbf));
        for (i = 0; i < BR_TBF_OP_MAX; i++) {
                *(tbf->bucket + i) = NULL;
        }

        for (i = 0; i < count; i++) {
                opspec = tbfspec + i;

                ret = br_tbf_init_bucket (tbf, opspec);
                if (ret)
                        break;
        }

        if (ret)
                goto error_return;

        return tbf;

 error_return:
        return NULL;
}

static void
br_tbf_mod_bucket (br_tbf_bucket_t *bucket, br_tbf_opspec_t *spec)
{
        LOCK (&bucket->lock);
        {
                bucket->tokens = 0;
                bucket->tokenrate = spec->rate;
                bucket->maxtokens = spec->maxlimit;
        }
        UNLOCK (&bucket->lock);

        /* next token tick would unqueue pending operations */
}

int
br_tbf_mod (br_tbf_t *tbf, br_tbf_opspec_t *tbfspec)
{
        int              ret    = 0;
        br_tbf_bucket_t *bucket = NULL;
        br_tbf_ops_t     op     = BR_TBF_OP_MIN;

        if (!tbf || !tbfspec)
                return -1;

        op = tbfspec->op;

        GF_ASSERT (op >= BR_TBF_OP_MIN);
        GF_ASSERT (op <= BR_TBF_OP_MAX);

        bucket = *(tbf->bucket + op);
        if (bucket) {
                br_tbf_mod_bucket (bucket, tbfspec);
        } else {
                ret = br_tbf_init_bucket (tbf, tbfspec);
        }

        return ret;
}

void
br_tbf_throttle (br_tbf_t *tbf, br_tbf_ops_t op, unsigned long tokens_requested)
{
        char waitq = 0;
        br_tbf_bucket_t *bucket = NULL;
        br_tbf_throttle_t *throttle = NULL;

        GF_ASSERT (op >= BR_TBF_OP_MIN);
        GF_ASSERT (op <= BR_TBF_OP_MAX);

        bucket = *(tbf->bucket + op);
        if (!bucket)
                return;

        LOCK (&bucket->lock);
        {
                /**
                 * if there are enough tokens in the bucket there is no need
                 * to throttle the request: therefore, consume the required
                 * number of tokens and continue.
                 */
                if (tokens_requested <= bucket->tokens) {
                        bucket->tokens -= tokens_requested;
                } else {
                        throttle = br_tbf_init_throttle (tokens_requested);
                        if (!throttle) /* let it slip through for now.. */
                                goto unblock;

                        waitq = 1;
                        pthread_mutex_lock (&throttle->mutex);
                        list_add_tail (&throttle->list, &bucket->queued);
                }
        }
 unblock:
        UNLOCK (&bucket->lock);

        if (waitq) {
                while (!throttle->done) {
                        pthread_cond_wait (&throttle->cond, &throttle->mutex);
                }

                pthread_mutex_unlock (&throttle->mutex);

                pthread_mutex_destroy (&throttle->mutex);
                pthread_cond_destroy (&throttle->cond);

                GF_FREE (throttle);
        }
}
