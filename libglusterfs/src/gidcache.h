/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __GIDCACHE_H__
#define __GIDCACHE_H__

#include "glusterfs.h"
#include "locking.h"

/*
 * TBD: make the cache size tunable
 *
 * The current size represents a pretty trivial amount of memory, and should
 * provide good hit rates even for quite busy systems.  If we ever want to
 * support really large cache sizes, we'll need to do dynamic allocation
 * instead of just defining an array within a private structure. It doesn't make
 * a whole lot of sense to change the associativity, because it won't improve
 * hit rates all that much and will increase the maintenance cost as we have
 * to scan more entries with every lookup/update.
 */

#define AUX_GID_CACHE_ASSOC     4
#define AUX_GID_CACHE_BUCKETS   256
#define AUX_GID_CACHE_SIZE      (AUX_GID_CACHE_ASSOC * AUX_GID_CACHE_BUCKETS)

typedef struct {
	uint64_t	gl_id;
	uint64_t        gl_uid;
	uint64_t        gl_gid;
	int		gl_count;
	gid_t		*gl_list;
	time_t		gl_deadline;
} gid_list_t;

typedef struct {
	gf_lock_t	gc_lock;
	uint32_t	gc_max_age;
	unsigned int	gc_nbuckets;
	gid_list_t	gc_cache[AUX_GID_CACHE_SIZE];
} gid_cache_t;

int gid_cache_init(gid_cache_t *, uint32_t);
int gid_cache_reconf(gid_cache_t *, uint32_t);
const gid_list_t *gid_cache_lookup(gid_cache_t *, uint64_t, uint64_t, uint64_t);
void gid_cache_release(gid_cache_t *, const gid_list_t *);
int gid_cache_add(gid_cache_t *, gid_list_t *);

#endif /* __GIDCACHE_H__ */
