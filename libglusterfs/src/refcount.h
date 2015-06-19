/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _REFCOUNT_H
#define _REFCOUNT_H

/* check for compiler support for __sync_*_and_fetch()
 *
 * a more comprehensive feature test is shown at
 * http://lists.iptel.org/pipermail/semsdev/2010-October/005075.html
 * this is sufficient for RHEL5 i386 builds
 */
#if (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)) && !defined(__i386__)
#undef REFCOUNT_NEEDS_LOCK
#else
#define REFCOUNT_NEEDS_LOCK
#include "locking.h"
#endif /* compiler support for __sync_*_and_fetch() */

typedef void (*gf_ref_release_t)(void *data);

struct _gf_ref_t {
#ifdef REFCOUNT_NEEDS_LOCK
        gf_lock_t          lk;      /* lock for atomically adjust cnt */
#endif
        unsigned int       cnt;     /* number of users, free on 0 */

        gf_ref_release_t   release; /* cleanup when cnt == 0 */
        void              *data;    /* parameter passed to release() */
};
typedef struct _gf_ref_t gf_ref_t;


/* _gf_ref_get -- increase the refcount
 *
 * @return: greater then 0 when a reference was taken, 0 when not
 */
unsigned int
_gf_ref_get (gf_ref_t *ref);

/* _gf_ref_put -- decrease the refcount
 *
 * @return: greater then 0 when there are still references, 0 when cleanup
 *          should be done, gf_ref_release_t is called on cleanup
 */
unsigned int
_gf_ref_put (gf_ref_t *ref);

/* _gf_ref_init -- initalize an embedded refcount object
 *
 * @release: function to call when the refcount == 0
 * @data: parameter to be passed to @release
 */
void
_gf_ref_init (gf_ref_t *ref, gf_ref_release_t release, void *data);


/*
 * Strong suggestion to use the simplified GF_REF_* API.
 */

/* GF_REF_DECL -- declaration to put inside your structure
 *
 * Example:
 *   struct my_struct {
 *       GF_REF_DECL;
 *
 *       ... // additional members
 *   };
 */
#define GF_REF_DECL         gf_ref_t _ref

/* GF_REF_INIT -- initialize a GF_REF_DECL structure
 *
 * @p: allocated structure with GF_REF_DECL
 * @d: destructor to call once refcounting reaches 0
 *
 * Sets the refcount to 1.
 */
#define GF_REF_INIT(p, d)   _gf_ref_init (&p->_ref, d, p)

/* GF_REF_GET -- increase the refcount of a GF_REF_DECL structure
 *
 * @return: greater then 0 when a reference was taken, 0 when not
 */
#define GF_REF_GET(p)       _gf_ref_get (&p->_ref)

/* GF_REF_PUT -- decrease the refcount of a GF_REF_DECL structure
 *
 * @return: greater then 0 when there are still references, 0 when cleanup
 *          should be done, gf_ref_release_t is called on cleanup
 */
#define GF_REF_PUT(p)       _gf_ref_put (&p->_ref)


#endif /* _REFCOUNT_H */
