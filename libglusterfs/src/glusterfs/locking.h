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

typedef pthread_mutex_t gf_lock_t;

#define LOCK_INIT(x) pthread_mutex_init(x, 0)
#define LOCK(x) pthread_mutex_lock(x)
#define TRY_LOCK(x) pthread_mutex_trylock(x)
#define UNLOCK(x) pthread_mutex_unlock(x)
#define LOCK_DESTROY(x) pthread_mutex_destroy(x)

#endif /* _LOCKING_H */
