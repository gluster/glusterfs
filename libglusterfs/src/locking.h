/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _LOCKING_H
#define _LOCKING_H

#include <pthread.h>

#if defined (GF_DARWIN_HOST_OS)
#include <libkern/OSAtomic.h>
#define pthread_spinlock_t OSSpinLock
#define pthread_spin_lock(l) OSSpinLockLock(l)
#define pthread_spin_unlock(l) OSSpinLockUnlock(l)
#define pthread_spin_destroy(l) 0
#define pthread_spin_init(l, v) (*l = v)
#endif

#if defined (HAVE_SPINLOCK)

typedef union {
        pthread_spinlock_t      spinlock;
        pthread_mutex_t         mutex;
} gf_lock_t;

#if !defined(LOCKING_IMPL)
extern int use_spinlocks;

/*
 * Using a dispatch table would be unpleasant because we're dealing with two
 * different types.  If the dispatch contains direct pointers to pthread_xx
 * or mutex_xxx then we have to hope that every possible union alternative
 * starts at the same address as the union itself.  I'm old enough to remember
 * compilers where this was not the case (for alignment reasons) so I'm a bit
 * paranoid about that.  Also, I don't like casting arguments through "void *"
 * which we'd also have to do to avoid type errors.  The other alternative would
 * be to define actual functions which pick out the right union member, and put
 * those in the dispatch tables.  Now we have a pointer dereference through the
 * dispatch table plus a function call, which is likely to be worse than the
 * branching here from the ?: construct.  If it were a clear win it might be
 * worth the extra complexity, but for now this way seems preferable.
 */

#define LOCK_INIT(x)    (use_spinlocks \
                                ? pthread_spin_init  (&((x)->spinlock), 0) \
                                : pthread_mutex_init (&((x)->mutex), 0))

#define LOCK(x)         (use_spinlocks \
                                ? pthread_spin_lock  (&((x)->spinlock)) \
                                : pthread_mutex_lock (&((x)->mutex)))

#define TRY_LOCK(x)     (use_spinlocks \
                                ? pthread_spin_trylock  (&((x)->spinlock)) \
                                : pthread_mutex_trylock (&((x)->mutex)))

#define UNLOCK(x)       (use_spinlocks \
                                ? pthread_spin_unlock  (&((x)->spinlock)) \
                                : pthread_mutex_unlock (&((x)->mutex)))

#define LOCK_DESTROY(x) (use_spinlocks \
                                ? pthread_spin_destroy  (&((x)->spinlock)) \
                                : pthread_mutex_destroy (&((x)->mutex)))

#endif

#else

typedef pthread_mutex_t gf_lock_t;

#define LOCK_INIT(x)    pthread_mutex_init (x, 0)
#define LOCK(x)         pthread_mutex_lock (x)
#define TRY_LOCK(x)     pthread_mutex_trylock (x)
#define UNLOCK(x)       pthread_mutex_unlock (x)
#define LOCK_DESTROY(x) pthread_mutex_destroy (x)

#endif /* HAVE_SPINLOCK */


#endif /* _LOCKING_H */
