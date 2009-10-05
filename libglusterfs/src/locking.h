/*
   Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
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
