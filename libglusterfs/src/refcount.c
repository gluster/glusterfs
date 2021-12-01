/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs/common-utils.h"
#include "glusterfs/refcount.h"

void *
_gf_ref_get(gf_ref_t *ref)
{
    unsigned int cnt = GF_ATOMIC_FETCH_ADD(ref->cnt, 1);

    /* if cnt == 0, we're in a fatal position, the object will be free'd
     *
     * There is a race when two threads do a _gf_ref_get(). Only one of
     * them may get a 0 returned. That is acceptable, because one
     * _gf_ref_get() returning 0 should be handled as a fatal problem and
     * when correct usage/locking is used, it should never happen.
     */
    GF_ASSERT(cnt != 0);

    return cnt ? ref->data : NULL;
}

unsigned int
_gf_ref_put(gf_ref_t *ref)
{
    unsigned int cnt = GF_ATOMIC_FETCH_SUB(ref->cnt, 1);

    /* if cnt == 1, the last user just did a _gf_ref_put()
     *
     * When cnt == 0, one _gf_ref_put() was done too much and there has
     * been a thread using the refcounted structure when it was not
     * supposed to.
     */
    GF_ASSERT(cnt != 0);

    if (cnt == 1 && ref->release)
        ref->release(ref->data);

    return (cnt != 1);
}

void
_gf_ref_init(gf_ref_t *ref, gf_ref_release_t release, void *data)
{
    GF_ASSERT(ref);

    GF_ATOMIC_INIT(ref->cnt, 1);
    ref->release = release;
    ref->data = data;
}
