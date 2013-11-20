/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "gidcache.h"
#include "mem-pool.h"

/*
 * We treat this as a very simple set-associative LRU cache, with entries aged
 * out after a configurable interval.  Hardly rocket science, but lots of
 * details to worry about.
 */
#define BUCKET_START(p,n)       ((p) + ((n) * AUX_GID_CACHE_ASSOC))

/*
 * Initialize the cache.
 */
int gid_cache_init(gid_cache_t *cache, uint32_t timeout)
{
	if (!cache)
		return -1;

	LOCK_INIT(&cache->gc_lock);
	cache->gc_max_age = timeout;
	cache->gc_nbuckets = AUX_GID_CACHE_BUCKETS;
	memset(cache->gc_cache, 0, sizeof(gid_list_t) * AUX_GID_CACHE_SIZE);

	return 0;
}

/*
 * Reconfigure the cache timeout.
 */
int gid_cache_reconf(gid_cache_t *cache, uint32_t timeout)
{
        if (!cache)
                return -1;

        LOCK(&cache->gc_lock);
        cache->gc_max_age = timeout;
        UNLOCK(&cache->gc_lock);

        return 0;
}

/*
 * Look up an ID in the cache. If found, return the actual cache entry to avoid
 * an additional allocation and memory copy. The caller should copy the data and
 * release (unlock) the cache as soon as possible.
 */
const gid_list_t *gid_cache_lookup(gid_cache_t *cache, uint64_t id,
				   uint64_t uid, uint64_t gid)
{
	int bucket;
	int i;
	time_t now;
	const gid_list_t *agl;

	LOCK(&cache->gc_lock);
	now = time(NULL);
	bucket = id % cache->gc_nbuckets;
	agl = BUCKET_START(cache->gc_cache, bucket);
	for (i = 0; i < AUX_GID_CACHE_ASSOC; i++, agl++) {
		if (!agl->gl_list)
			continue;
		if (agl->gl_id != id)
			continue;

		/*
		  @uid and @gid reflect the latest UID/GID of the
		   process performing the syscall (taken from frame->root).

		   If the UID and GID has changed for the PID since the
		   time we cached it, we should treat the cache as having
		   stale values and query them freshly.
		*/
		if (agl->gl_uid != uid || agl->gl_gid != gid)
			break;

		/*
		 * We don't put new entries in the cache when expiration=0, but
		 * there might be entries still in there if expiration was
		 * changed very recently.  Writing the check this way ensures
		 * that they're not used.
		 */
		if (now < agl->gl_deadline) {
			return agl;
		}

		/*
		 * We're not going to find any more UID matches, and reaping
		 * is handled further down to maintain LRU order.
		 */
		break;
	}
	UNLOCK(&cache->gc_lock);
	return NULL;
}

/*
 * Release an entry found via lookup.
 */
void gid_cache_release(gid_cache_t *cache, const gid_list_t *agl)
{
	UNLOCK(&cache->gc_lock);
}

/*
 * Add a new list entry to the cache. If an entry for this ID already exists,
 * update it.
 */
int gid_cache_add(gid_cache_t *cache, gid_list_t *gl)
{
	gid_list_t *agl;
	int bucket;
	int i;
	time_t now;

	if (!gl || !gl->gl_list)
		return -1;

	if (!cache->gc_max_age)
		return 0;

	LOCK(&cache->gc_lock);
	now = time(NULL);

	/*
	 * Scan for the first free entry or one that matches this id. The id
	 * check is added to address a bug where the cache might contain an
	 * expired entry for this id. Since lookup occurs in LRU order and
	 * does not reclaim entries, it will always return failure on discovery
	 * of an expired entry. This leads to duplicate entries being added,
	 * which still do not satisfy lookups until the expired entry (and
	 * everything before it) is reclaimed.
	 *
	 * We address this through reuse of an entry already allocated to this
	 * id, whether expired or not, since we have obviously already received
	 * more recent data. The entry is repopulated with the new data and a new
	 * deadline and is pushed forward to reside as the last populated entry in
	 * the bucket.
	 */
	bucket = gl->gl_id % cache->gc_nbuckets;
	agl = BUCKET_START(cache->gc_cache, bucket);
	for (i = 0; i < AUX_GID_CACHE_ASSOC; ++i, ++agl) {
		if (agl->gl_id == gl->gl_id)
			break;
		if (!agl->gl_list)
			break;
	}

	/*
	 * The way we allocate free entries naturally places the newest
	 * ones at the highest indices, so evicting the lowest makes
	 * sense, but that also means we can't just replace it with the
	 * one that caused the eviction.  That would cause us to thrash
	 * the first entry while others remain idle.  Therefore, we
	 * need to slide the other entries down and add the new one at
	 * the end just as if the *last* slot had been free.
	 *
	 * Deadline expiration is also handled here, since the oldest
	 * expired entry will be in the first position.  This does mean
	 * the bucket can stay full of expired entries if we're idle
	 * but, if the small amount of extra memory or scan time before
	 * we decide to evict someone ever become issues, we could
	 * easily add a reaper thread.
	 */

	if (i >= AUX_GID_CACHE_ASSOC) {
		/* cache full, evict the first (LRU) entry */
		i = 0;
		agl = BUCKET_START(cache->gc_cache, bucket);
		GF_FREE(agl->gl_list);
	} else if (agl->gl_list) {
		/* evict the old entry we plan to reuse */
		GF_FREE(agl->gl_list);
	}

	/*
	 * If we have evicted an entry, slide the subsequent populated entries
	 * back and populate the last entry.
	 */
	for (; i < AUX_GID_CACHE_ASSOC - 1; i++) {
		if (!agl[1].gl_list)
			break;
		agl[0] = agl[1];
		agl++;
	}

	agl->gl_id = gl->gl_id;
	agl->gl_uid = gl->gl_uid;
	agl->gl_gid = gl->gl_gid;
	agl->gl_count = gl->gl_count;
	agl->gl_list = gl->gl_list;
	agl->gl_deadline = now + cache->gc_max_age;

	UNLOCK(&cache->gc_lock);

	return 1;
}
