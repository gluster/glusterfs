/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#if defined(HAVE_SPINLOCK)
/* None of this matters otherwise. */

#include <pthread.h>
#include <unistd.h>

#define LOCKING_IMPL
#include "locking.h"

int use_spinlocks = 0;

static void __attribute__((constructor))
gf_lock_setup (void)
{
        //use_spinlocks = (sysconf(_SC_NPROCESSORS_ONLN) > 1);
}

#endif
