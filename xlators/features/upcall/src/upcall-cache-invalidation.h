/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __UPCALL_CACHE_INVALIDATION_H__
#define __UPCALL_CACHE_INVALIDATION_H__

/* The time period for which a client will be notified of cache_invalidation
 * events post its last access */
#define CACHE_INVALIDATION_TIMEOUT "60"

/* xlator options */
gf_boolean_t is_cache_invalidation_enabled(xlator_t *this);
int32_t get_cache_invalidation_timeout(xlator_t *this);

#endif /* __UPCALL_CACHE_INVALIDATION_H__ */
