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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <pthread.h>

#if HAVE_SPINLOCK
#define LOCK_INIT(x)    pthread_spin_init (x, 0)
#define LOCK(x)         pthread_spin_lock (x)
#define TRY_LOCK(x)     pthread_spin_trylock (x)
#define UNLOCK(x)       pthread_spin_unlock (x)
#define LOCK_DESTROY(x) pthread_spin_destroy (x)

typedef pthread_spinlock_t gf_lock_t;
#else
#define LOCK_INIT(x)    pthread_mutex_init (x, 0)
#define LOCK(x)         pthread_mutex_lock (x)
#define TRY_LOCK(x)     pthread_mutex_trylock (x)
#define UNLOCK(x)       pthread_mutex_unlock (x)
#define LOCK_DESTROY(x) pthread_mutex_destroy (x)

typedef pthread_mutex_t gf_lock_t;
#endif /* HAVE_SPINLOCK */


#endif /* _LOCKING_H */
