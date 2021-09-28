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

#if defined(GF_DARWIN_HOST_OS)
#include <libkern/OSAtomic.h>
#define pthread_spinlock_t OSSpinLock
#define pthread_spin_lock(l) OSSpinLockLock(l)
#define pthread_spin_unlock(l) OSSpinLockUnlock(l)
#define pthread_spin_destroy(l) 0
#define pthread_spin_init(l, v) (*l = v)
#endif

#if defined(GF_LOCK_DEBUG)

/* Use assert() just to avoid chicken-egg problem
   with defining GF_ASSERT() through the headers. */

#include <assert.h>

#define DEBUG_LOCK_INIT 0xfadebead
#define DEBUG_LOCK_DEAD 0xdeadbeef

typedef struct gf_debug_lock {
    pthread_mutex_t mutex;
    int tag;
} gf_lock_t;

static inline int
LOCK_INIT(gf_lock_t *lock)
{
    assert(lock->tag != DEBUG_LOCK_INIT);
    lock->tag = DEBUG_LOCK_INIT;
    return pthread_mutex_init(&lock->mutex, NULL);
}

static inline int
LOCK(gf_lock_t *lock)
{
    assert(lock->tag == DEBUG_LOCK_INIT);
    return pthread_mutex_lock(&lock->mutex);
}

static inline int
TRY_LOCK(gf_lock_t *lock)
{
    assert(lock->tag == DEBUG_LOCK_INIT);
    return pthread_mutex_trylock(&lock->mutex);
}

static inline int
UNLOCK(gf_lock_t *lock)
{
    assert(lock->tag == DEBUG_LOCK_INIT);
    return pthread_mutex_unlock(&lock->mutex);
}

static inline int
LOCK_DESTROY(gf_lock_t *lock)
{
    assert(lock->tag == DEBUG_LOCK_INIT);
    lock->tag = DEBUG_LOCK_DEAD;
    return pthread_mutex_destroy(&lock->mutex);
}

#else /* not GF_LOCK_DEBUG */

typedef pthread_mutex_t gf_lock_t;

#define LOCK_INIT(x) pthread_mutex_init(x, NULL)
#define LOCK(x) pthread_mutex_lock(x)
#define TRY_LOCK(x) pthread_mutex_trylock(x)
#define UNLOCK(x) pthread_mutex_unlock(x)
#define LOCK_DESTROY(x) pthread_mutex_destroy(x)

#endif /* GF_LOCK_DEBUG */

#endif /* _LOCKING_H */
