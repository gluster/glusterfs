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

#include "refcount.h"
#include "auth-cache.h"
#include "nfs3.h"
#include "exports.h"
#include "nfs-messages.h"

/* Given a filehandle and an ip, creates a colon delimited hashkey.
 */
#define make_fh_hashkey(hashkey, fh, host)                                     \
        do {                                                                   \
                char exportid[256] = {0, };                                    \
                char mountid[256] = {0, };                                     \
                size_t nbytes = 0;                                             \
                gf_uuid_unparse (fh->exportid, exportid);                      \
                gf_uuid_unparse (fh->mountid, mountid);                        \
                nbytes = strlen (exportid) + strlen (host)                     \
                         + strlen (mountid) + 5;                               \
                hashkey = alloca (nbytes);                                     \
                snprintf (hashkey, nbytes, "%s:%s:%s", exportid,               \
                          mountid, host);                                      \
        } while (0);                                                           \

#define make_path_hashkey(hashkey, path, host)                          \
        do {                                                            \
                size_t nbytes = strlen (path) + strlen (host) + 2;      \
                hashkey = alloca (nbytes);                              \
                snprintf (hashkey, nbytes, "%s:%s", path, host);        \
        } while (0);
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

        LOCK_INIT (&cache->lock);
        cache->ttl_sec = ttl_sec;
out:
        return cache;
}

struct auth_cache_entry {
        time_t timestamp;
        struct export_item *item;
        gf_boolean_t access_allowed;
};

/**
 * auth_cache_entry_init -- Initialize an auth cache entry
 *
 * @return: Pointer to an allocated auth cache entry, NULL if allocation
 *          failed.
 */
static struct auth_cache_entry *
auth_cache_entry_init ()
{
        struct auth_cache_entry *entry = NULL;

        entry = GF_CALLOC (1, sizeof (*entry), gf_nfs_mt_auth_cache_entry);
        if (!entry)
                gf_msg (GF_NFS, GF_LOG_WARNING, ENOMEM, NFS_MSG_NO_MEMORY,
                        "failed to allocate entry");

        return entry;
}

// Internal lookup
enum _internal_cache_lookup_results {
        ENTRY_NOT_FOUND = -1,
        ENTRY_EXPIRED = -2,
};

/**
 * auth_cache_purge -- Purge the dict in the cache and set
 *                     the dict pointer to NULL. It will be allocated
 *                     on the first insert into the dict.
 *
 * @cache: Cache to purge
 *
 */
void
auth_cache_purge (struct auth_cache *cache)
{
        dict_t *new_cache_dict = NULL;
        dict_t *old_cache_dict = cache->cache_dict;

        if (!cache || !cache->cache_dict)
                goto out;

        (void)__sync_lock_test_and_set (&cache->cache_dict, new_cache_dict);

        dict_destroy (old_cache_dict);
out:
        return;
}


/**
 * Lookup filehandle or path from the cache.
 */
int _cache_lookup (struct auth_cache *cache, char *key,
                   struct auth_cache_entry **entry)
{
        int ret = ENTRY_NOT_FOUND;
        struct auth_cache_entry *lookup_res;
        data_t *entry_data;

        if (!cache->cache_dict) {
                goto out;
        }

        if (!entry) {
                goto out;
        }

        *entry = NULL;

        entry_data = dict_get (cache->cache_dict, key);
        if (!entry_data) {
                goto out;
        }

        lookup_res = (struct auth_cache_entry *)(entry_data->data);
        if (time (NULL) - lookup_res->timestamp > cache->ttl_sec) {
                GF_FREE (lookup_res);
                entry_data->data = NULL;
                dict_del (cache->cache_dict, key);  // Remove from the cache
                ret = ENTRY_EXPIRED;
                goto out;
        }

        *entry = lookup_res;

        return 0;

out:
        return -1;
}

/**
 * Lookup filehandle from the cache.
 */
int
_cache_lookup_fh (struct auth_cache *cache, struct nfs3_fh *fh,
                  const char *host_addr, struct auth_cache_entry **ec)
{
        char *hashkey;
        int ret = ENTRY_NOT_FOUND;
        if (fh && host_addr) {
                make_fh_hashkey (hashkey, fh, host_addr);
                ret =_cache_lookup (cache, hashkey, ec);
        }
        return ret;
}

/**
 * Lookup path from the cache.
 */
int
_cache_lookup_path (struct auth_cache *cache, const char *path,
                    const char *host_addr, struct auth_cache_entry **ec)
{
        char *hashkey;
        int ret = ENTRY_NOT_FOUND;
        if (path && host_addr) {
                make_path_hashkey (hashkey, path, host_addr);
                ret = _cache_lookup (cache, hashkey, ec);
        }
        return ret;
}

/**
 * cache_item -- Caches either a filehandle or path.
 *               See descriptions of functions that invoke this one.
 */
int
cache_item (struct auth_cache *cache, const char *path, struct nfs3_fh *fh,
            const char *host_addr, struct export_item *export_item,
            auth_cache_status_t status)
{
        int ret = -EINVAL;
        data_t *entry_data = NULL;
        struct auth_cache_entry *entry = NULL;
        char *hashkey = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS, host_addr, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, cache, out);

        // We can cache either a file-handle or a path, not both,
        // and at least one of them must be defined!
        if ((fh && path) || (!fh && !path)) {
                goto out;
        }

        // If a dict has not been allocated already, allocate it.
        if (!cache->cache_dict) {
                cache->cache_dict = dict_new ();
                if (!cache->cache_dict) {
                        ret = -ENOMEM;
                        goto out;
                }
        }


        // Find an entry with the filehandle or path, depending
        // on which one is defined. Validation for these parameters
        // is above.
        if (fh) {
                ret = _cache_lookup_fh (cache, fh, host_addr, &entry);
                make_fh_hashkey (hashkey, fh, host_addr)
        }

        if (path) {
                ret = _cache_lookup_path (cache, path, host_addr, &entry);
                make_path_hashkey (hashkey, path, host_addr)
        }

        // If no entry was found, we need to create one.
        if (!entry) {
                entry = auth_cache_entry_init ();
                GF_CHECK_ALLOC (entry, ret, out);
        }

        // Populate the entry
        entry->timestamp = time (NULL);
        entry->item = export_item;
        // Access is only allowed if  the status is set to
        // AUTH_CACHE_HOST_AUTH_OK
        entry->access_allowed = (status == AUTH_CACHE_HOST_AUTH_OK);

        // Put the entry into the cache
        entry_data = bin_to_data (entry, sizeof (*entry));
        dict_set (cache->cache_dict, hashkey, entry_data);
        gf_log (GF_NFS, GF_LOG_TRACE, "Caching %s for host(%s) as %s",
                path ? path : "fh", host_addr, entry->access_allowed ?
                "ALLOWED" : "NOT ALLOWED");
out:
        return ret;
}

/**
 * cache_nfs_path -- Places the path in the underlying dict as we are
 *                   using as our cache. The value is an entry struct
 *                   containing the export item that was authorized or
 *                   deauthorized for the operation and the path authorized
 *                   or deauthorized.
 *
 * @cache: The cache to place fh's in
 * @path :  The path to cache
 * @host_addr: The address of the host
 * @export_item: The export item that was authorized/deauthorized
 *
 */
int
cache_nfs_path (struct auth_cache *cache, const char *path,
                const char *host_addr, struct export_item *export_item,
                auth_cache_status_t status)
{
        return cache_item (cache, path, NULL, host_addr, export_item, status);
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
              const char *host_addr, struct export_item *export_item,
              auth_cache_status_t status)
{
        return cache_item (cache, NULL, fh, host_addr, export_item, status);
}

auth_cache_status_t
auth_cache_allows (struct auth_cache *cache, struct nfs3_fh *fh,
                   const char *path, const char *host_addr,
                   gf_boolean_t check_rw_access)
{
        int ret = 0;
        int status = AUTH_CACHE_HOST_EACCES;
        gf_boolean_t cache_allows = FALSE;
        struct auth_cache_entry *ace = NULL;

        if ((fh && path) || (!fh && !path)) {
                status = AUTH_CACHE_HOST_ENOENT;
                goto out;
        }

        if (fh) {
                ret = _cache_lookup_fh (cache, fh, host_addr, &ace);
        }

        if (path) {
                ret = _cache_lookup_path (cache, path, host_addr, &ace);
        }

        cache_allows = (ret == 0) && ace->access_allowed;
        if (check_rw_access) {
                cache_allows = cache_allows && ace->item->opts->rw;
        }

        if (!ace) {
                status = AUTH_CACHE_HOST_ENOENT;
        }

        if (cache_allows) {
                status = AUTH_CACHE_HOST_AUTH_OK;
        }
out:
        return status;
}

auth_cache_status_t
auth_cache_allows_fh (struct auth_cache *cache, struct nfs3_fh *fh,
                      const char *host_addr)
{
        return auth_cache_allows (cache, fh, NULL, host_addr, FALSE);
}

auth_cache_status_t
auth_cache_allows_write_to_fh (struct auth_cache *cache, struct nfs3_fh *fh,
                               const char *host_addr)
{
        return auth_cache_allows (cache, fh, NULL, host_addr, TRUE);
}

auth_cache_status_t
auth_cache_allows_path (struct auth_cache *cache, const char *path,
                        const char *host_addr)
{
        return auth_cache_allows (cache, NULL, path, host_addr, FALSE);
}
