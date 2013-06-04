/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CHANGELOG_RT_H
#define _CHANGELOG_RT_H

#include "locking.h"
#include "timer.h"
#include "pthread.h"

#include "changelog-helpers.h"

/* unused as of now - may be you would need it later */
typedef struct changelog_rt {
        gf_lock_t lock;
} changelog_rt_t;

int
changelog_rt_init (xlator_t *this, changelog_dispatcher_t *cd);
int
changelog_rt_fini (xlator_t *this, changelog_dispatcher_t *cd);
int
changelog_rt_enqueue (xlator_t *this, changelog_priv_t *priv, void *cbatch,
                      changelog_log_data_t *cld_0, changelog_log_data_t *cld_1);

#endif /* _CHANGELOG_RT_H */
