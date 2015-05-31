/*
   Copyright 2014-present Facebook. All Rights Reserved
   This file is part of GlusterFS.

   Author :
   Shreyas Siravara <shreyas.siravara@gmail.com>

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "auth-cache.h"
#include "nfs3.h"
#include "exports.h"
#include "nfs-messages.h"

enum auth_cache_lookup_results {
        ENTRY_FOUND     =  0,
        ENTRY_NOT_FOUND = -1,
        ENTRY_EXPIRED   = -2,
};

struct auth_cache_entry {
        time_t timestamp;
        struct export_item *item;
};

/* Given a filehandle and an ip, creates a colon delimited hashkey.
 */
static char*
make_hashkey(struct nfs3_fh *fh, const char *host)
{
        char   *hashkey          = NULL;
        char    exportid[256]    = {0, };
        char    gfid[256]        = {0, };
        char    mountid[256]     = {0, };
        size_t  nbytes           = 0;

        gf_uuid_unparse (fh->exportid, exportid);
        gf_uuid_unparse (fh->gfid, gfid);
        gf_uuid_unparse (fh->mountid, mountid);

        nbytes = strlen (exportid) + strlen (gfid) + strlen (host)
                 + strlen (mountid) + 5;
        hashkey = GF_MALLOC (nbytes, gf_common_mt_char);
        if (!hashkey)
                return NULL;

        snprintf (hashkey, nbytes, "%s:%s:%s:%s", exportid, gfid,
                  mountid, host);

        return hashkey;
}

/**
 * auth_cache_init -- Initialize an auth cache and set the ttl_sec
 *
 * @ttl_sec : The TTL to set in seconds
 *
 * @return : allocated auth cache struct, NULL if allocation failed.
 */
struct auth_cache *
auth_cache_init (time_t ttl_sec)
{
        struct auth_cache *cache = GF_CALLOC (1, sizeof (*cache),
                                              gf_nfs_mt_auth_cache);

        GF_VALIDATE_OR_GOTO ("auth-cache", cache, out);

        cache->cache_dict = dict_new ();
        if (!cache->cache_dict) {
                GF_FREE (cache);
                cache = NULL;
                goto out;
        }

        cache->ttl_sec = ttl_sec;
out:
        return cache;
}

/**
 * auth_cache_entry_init -- Initialize an auth cache entry
 *
 * @return: Pointer to an allocated auth cache entry, NULL if allocation
 *          failed.
 */
struct auth_cache_entry *
auth_cache_entry_init ()
{
        struct auth_cache_entry *entry = NULL;

        entry = GF_CALLOC (1, sizeof (*entry), gf_nfs_mt_auth_cache_entry);
        if (!entry)
                gf_msg (GF_NFS, GF_LOG_WARNING, ENOMEM, NFS_MSG_NO_MEMORY,
                        "failed to allocate entry");

        return entry;
}

/**
 * auth_cache_lookup -- Lookup an item from the cache
 *
 * @cache: cache to lookup from
 * @fh   : FH to use in lookup
 * @host_addr: Address to use in lookup
 * @timestamp: The timestamp to set when lookup succeeds
 * @can_write: Is the host authorized to write to the filehandle?
 *
 * If the current time - entry time of the cache entry > ttl_sec,
 * we remove the element from the dict and return ENTRY_EXPIRED.
 *
 * @return: ENTRY_EXPIRED if entry expired
 *          ENTRY_NOT_FOUND if entry not found in dict
 *          0 if found
 */
enum auth_cache_lookup_results
auth_cache_lookup (struct auth_cache *cache, struct nfs3_fh *fh,
                   const char *host_addr, time_t *timestamp,
                   gf_boolean_t *can_write)
{
        char                    *hashkey    = NULL;
        data_t                  *entry_data = NULL;
        struct auth_cache_entry *lookup_res = NULL;
        int                      ret        = ENTRY_NOT_FOUND;

        GF_VALIDATE_OR_GOTO (GF_NFS, cache, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, cache->cache_dict, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, fh, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, host_addr, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, timestamp, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, can_write, out);

        hashkey = make_hashkey (fh, host_addr);
        if (!hashkey) {
                ret = -ENOMEM;
                goto out;
        }

        entry_data = dict_get (cache->cache_dict, hashkey);
        if (!entry_data) {
                gf_msg_debug (GF_NFS, 0, "could not find entry for %s",
                              host_addr);
                goto out;
        }

        lookup_res = (struct auth_cache_entry *)(entry_data->data);

        if ((time (NULL) - lookup_res->timestamp) > cache->ttl_sec) {
                gf_msg_debug (GF_NFS, 0, "entry for host %s has expired",
                              host_addr);
                GF_FREE (lookup_res);
                entry_data->data = NULL;
                /* Remove from the cache */
                dict_del (cache->cache_dict, hashkey);

                ret = ENTRY_EXPIRED;
                goto out;
        }

        *timestamp = lookup_res->timestamp;
        *can_write = lookup_res->item->opts->rw;

        ret = ENTRY_FOUND;
out:
        GF_FREE (hashkey);

        return ret;
}

/**
 * auth_cache_purge -- Purge the dict in the cache and create a new empty one.
 *
 * @cache: Cache to purge
 *
 */
void
auth_cache_purge (struct auth_cache *cache)
{
        dict_t *new_cache_dict = dict_new ();
        dict_t *old_cache_dict = cache->cache_dict;

        if (!cache)
                goto out;

        (void)__sync_lock_test_and_set (&cache->cache_dict, new_cache_dict);

        dict_unref (old_cache_dict);
out:
        return;
}

/**
 * is_nfs_fh_cached_and_writeable -- Checks if an NFS FH is cached for the given
 *                                   host
 * @cache: The fh cache
 * @host_addr: Address to use in lookup
 * @fh: The fh to use in lookup
 *
 *
 * @return: TRUE if cached, FALSE otherwise
 *
 */
gf_boolean_t
is_nfs_fh_cached (struct auth_cache *cache, struct nfs3_fh *fh,
                  const char *host_addr)
{
        int          ret       = 0;
        time_t       timestamp = 0;
        gf_boolean_t cached    = _gf_false;
        gf_boolean_t can_write = _gf_false;

        if (!fh)
                goto out;

        ret = auth_cache_lookup (cache, fh, host_addr, &timestamp, &can_write);
        cached = (ret == ENTRY_FOUND);

out:
        return cached;
}


/**
 * is_nfs_fh_cached_and_writeable -- Checks if an NFS FH is cached for the given
 *                                   host and writable
 * @cache: The fh cache
 * @host_addr: Address to use in lookup
 * @fh: The fh to use in lookup
 *
 *
 * @return: TRUE if cached & writable, FALSE otherwise
 *
 */
gf_boolean_t
is_nfs_fh_cached_and_writeable (struct auth_cache *cache, struct nfs3_fh *fh,
                                const char *host_addr)
{
        int          ret       = 0;
        time_t       timestamp = 0;
        gf_boolean_t cached    = _gf_false;
        gf_boolean_t writable  = _gf_false;

        if (!fh)
                goto out;

        ret = auth_cache_lookup (cache, fh, host_addr, &timestamp, &writable);
        cached = ((ret == ENTRY_FOUND) && writable);

out:
        return cached;
}

/**
 * cache_nfs_fh -- Places the nfs file handle in the underlying dict as we are
 *                 using as our cache. The key is "exportid:gfid:host_addr", the
 *                 value is an entry struct containing the export item that
 *                 was authorized for the operation and the file handle that was
 *                 authorized.
 *
 * @cache: The cache to place fh's in
 * @fh   : The fh to cache
 * @host_addr: The address of the host
 * @export_item: The export item that was authorized
 *
 */
int
cache_nfs_fh (struct auth_cache *cache, struct nfs3_fh *fh,
              const char *host_addr, struct export_item *export_item)
{
        int                      ret        = -EINVAL;
        char                    *hashkey    = NULL;
        data_t                  *entry_data = NULL;
        time_t                   timestamp  = 0;
        gf_boolean_t             can_write  = _gf_false;
        struct auth_cache_entry *entry      = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS, host_addr, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, cache, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, fh, out);

        /* If we could already find it in the cache, just return */
        ret = auth_cache_lookup (cache, fh, host_addr, &timestamp, &can_write);
        if (ret == 0) {
                gf_msg_trace (GF_NFS, 0, "found cached auth/fh for host "
                              "%s", host_addr);
                goto out;
        }

        hashkey = make_hashkey (fh, host_addr);
        if (!hashkey) {
                ret = -ENOMEM;
                goto out;
        }

        entry = auth_cache_entry_init ();
        if (!entry) {
                ret = -ENOMEM;
                goto out;
        }

        entry->timestamp = time (NULL);
        entry->item = export_item;

        /* The cache entry will simply be the time that the entry
         * was cached.
         */
        entry_data = bin_to_data (entry, sizeof (*entry));
        if (!entry_data) {
                GF_FREE (entry);
                goto out;
        }

        ret = dict_set (cache->cache_dict, hashkey, entry_data);
        if (ret == -1) {
                GF_FREE (entry);
                goto out;
        }
        gf_msg_trace (GF_NFS, 0, "Caching file-handle (%s)", host_addr);
        ret = 0;
out:
        GF_FREE (hashkey);

        return ret;
}
