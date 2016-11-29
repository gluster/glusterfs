/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "common-utils.h"
#include "refcount.h"

#ifndef REFCOUNT_NEEDS_LOCK

void *
_gf_ref_get (gf_ref_t *ref)
{
        unsigned int cnt = __sync_fetch_and_add (&ref->cnt, 1);

        /* if cnt == 0, we're in a fatal position, the object will be free'd
         *
         * There is a race when two threads do a _gf_ref_get(). Only one of
         * them may get a 0 returned. That is acceptible, because one
         * _gf_ref_get() returning 0 should be handled as a fatal problem and
         * when correct usage/locking is used, it should never happen.
         */
        GF_ASSERT (cnt != 0);

        return cnt ? ref->data : NULL;
}

void
_gf_ref_put (gf_ref_t *ref)
{
        unsigned int cnt = __sync_fetch_and_sub (&ref->cnt, 1);

        /* if cnt == 1, the last user just did a _gf_ref_put()
         *
         * When cnt == 0, one _gf_ref_put() was done too much and there has
         * been a thread using the refcounted structure when it was not
         * supposed to.
         */
        GF_ASSERT (cnt != 0);

        if (cnt == 1 && ref->release)
                ref->release (ref->data);
}

#else

void *
_gf_ref_get (gf_ref_t *ref)
{
        unsigned int cnt = 0;

        LOCK (&ref->lk);
        {
                /* never can be 0, should have been free'd */
                if (ref->cnt > 0)
                        cnt = ++ref->cnt;
                else
                        GF_ASSERT (ref->cnt > 0);
        }
        UNLOCK (&ref->lk);

        return cnt ? ref->data : NULL;
}

void
_gf_ref_put (gf_ref_t *ref)
{
        unsigned int cnt = 0;
        int release = 0;

        LOCK (&ref->lk);
        {
                if (ref->cnt != 0) {
                        cnt = --ref->cnt;
                        /* call release() only when cnt == 0 */
                        release = (cnt == 0);
                } else
                        GF_ASSERT (ref->cnt != 0);
        }
        UNLOCK (&ref->lk);

        if (release && ref->release)
                ref->release (ref->data);
}

#endif /* REFCOUNT_NEEDS_LOCK */


void
_gf_ref_init (gf_ref_t *ref, gf_ref_release_t release, void *data)
{
        GF_ASSERT (ref);

#ifdef REFCOUNT_NEEDS_LOCK
        LOCK_INIT (&ref->lk);
#endif
        ref->cnt = 1;
        ref->release = release;
        ref->data = data;
}
