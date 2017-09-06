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

// Internal lookup
enum _internal_cache_lookup_results {
        ENTRY_NOT_FOUND = -1,
        ENTRY_EXPIRED = -2,
};

struct auth_cache_entry {
        time_t timestamp;
        struct export_item *item;
        gf_boolean_t access_allowed;
};

typedef struct {
        /*
         * Create and destroy are for *permanent* implementation-specific
         * structures, when the process starts or when we switch between
         * one implementation and another.  Warm_init creates transient
         * structures (e.g. cache_dict) in other cases.  Reset might just
         * operate on permanent structures (new code) or free those from
         * warm_init (old code).
         */
        int     (*create)       (struct auth_cache *);
        int     (*warm_init)    (struct auth_cache *);
        int     (*lookup)       (struct auth_cache *cache, char *key,
                                 struct auth_cache_entry **entry);
        int32_t (*insert)       (struct auth_cache *cache, char *hashkey,
                                 struct auth_cache_entry *entry);
        void    (*reset)        (struct auth_cache *cache);
        void    (*destroy)      (struct auth_cache *cache);
} ac_vtbl_t;

static int
old_create (struct auth_cache *cache)
{
        pthread_mutex_init (&cache->lock, NULL);
        return 0;
}

static int
old_warm_init (struct auth_cache *cache)
{
        if (!cache->cache_dict) {
                cache->cache_dict = dict_new ();
                if (!cache->cache_dict) {
                        return -ENOMEM;
                }
        }

        return 0;
}

static int _cache_lookup (struct auth_cache *cache, char *key,
                          struct auth_cache_entry **entry);

static int32_t
old_cache_insert (struct auth_cache *cache, char *hashkey,
                  struct auth_cache_entry *entry)
{
        /* We must use dict_set_static_bin() to ensure that
         * the ownership of "entry" remains with us, and not
         * the dict library.
         */
        return dict_set_static_bin (cache->cache_dict, hashkey,
                                    entry, sizeof (*entry));
}

static void
old_cache_reset (struct auth_cache *cache)
{
        dict_t *old_cache_dict;

        if (!cache) {
                return;
        }

        old_cache_dict = __sync_lock_test_and_set (&cache->cache_dict,
                                                   NULL);

        if (old_cache_dict) {
                dict_unref (old_cache_dict);
        }
}

static ac_vtbl_t old_methods = {
        old_create,
        old_warm_init,
        _cache_lookup,
        old_cache_insert,
        /* Reset and destroy are the same thing for old code. */
        old_cache_reset,
        old_cache_reset,
};

static ac_vtbl_t *methods = &old_methods;

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
        if (methods->create (cache) != 0) {
                GF_FREE (cache);
                return NULL;
        }
        cache->ttl_sec = ttl_sec;
        return cache;
}

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
        if (!cache) {
                return;
        }

        pthread_mutex_lock (&cache->lock);
        {
                (methods->reset) (cache);
        }
        pthread_mutex_unlock (&cache->lock);
}


/**
 * Lookup filehandle or path from the cache.
 */
static int
_cache_lookup (struct auth_cache *cache, char *key,
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
        if (!lookup_res) {
                goto out;
        }

        if (time (NULL) - lookup_res->timestamp > cache->ttl_sec) {
                // Destroy the auth cache entry
                exp_item_unref (lookup_res->item);
                lookup_res->item = NULL;
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
static int
__cache_lookup_fh (struct auth_cache *cache, struct nfs3_fh *fh,
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
static int
__cache_lookup_path (struct auth_cache *cache, const char *path,
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
static int
__cache_item (struct auth_cache *cache, const char *path, struct nfs3_fh *fh,
              const char *host_addr, struct export_item *export_item,
              auth_cache_status_t status)
{
        int ret = -EINVAL;
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
        ret = (methods->warm_init) (cache);
        if (ret != 0) {
                goto out;
        }


        // Find an entry with the filehandle or path, depending
        // on which one is defined. Validation for these parameters
        // is above.
        if (fh) {
                ret = __cache_lookup_fh (cache, fh, host_addr, &entry);
                make_fh_hashkey (hashkey, fh, host_addr)
        }

        if (path) {
                ret = __cache_lookup_path (cache, path, host_addr, &entry);
                make_path_hashkey (hashkey, path, host_addr)
        }

        // If no entry was found, we need to create one.
        if (!entry) {
                entry = auth_cache_entry_init ();
                GF_CHECK_ALLOC (entry, ret, out);
        }

        // Update the timestamp
        entry->timestamp = time (NULL);

        // Update entry->item if it is NULL or
        // pointing to a different export_item
        if (entry->item != export_item) {
                exp_item_unref (entry->item);
                entry->item = exp_item_ref (export_item);
        }

        // Access is only allowed if  the status is set to
        // AUTH_CACHE_HOST_AUTH_OK
        entry->access_allowed = (status == AUTH_CACHE_HOST_AUTH_OK);

        // Put the entry into the cache
        if ((methods->insert) (cache, hashkey, entry) == 0) {
                gf_log (GF_NFS, GF_LOG_TRACE, "Caching %s for host(%s) as %s",
                        path ? path : "fh", host_addr,
                        entry->access_allowed ?  "ALLOWED" : "NOT ALLOWED");
        } else {
                gf_log (GF_NFS, GF_LOG_TRACE,
                        "Caching %s for host(%s) FAILED",
                        path ? path : "fh", host_addr);
        }

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
        int     ret = -EINVAL;

        GF_VALIDATE_OR_GOTO ("nfs", cache, out);
        GF_VALIDATE_OR_GOTO ("nfs", path, out);
        GF_VALIDATE_OR_GOTO ("nfs", host_addr, out);
        GF_VALIDATE_OR_GOTO ("nfs", export_item, out);

        pthread_mutex_lock (&cache->lock);
        {
                ret = __cache_item (cache, path, /* fh = */ NULL,
                                    host_addr, export_item, status);
        }
        pthread_mutex_unlock (&cache->lock);

out:
        return ret;
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
        int     ret = -EINVAL;

        GF_VALIDATE_OR_GOTO ("nfs", cache, out);
        GF_VALIDATE_OR_GOTO ("nfs", fh, out);
        GF_VALIDATE_OR_GOTO ("nfs", host_addr, out);
        GF_VALIDATE_OR_GOTO ("nfs", export_item, out);

        pthread_mutex_lock (&cache->lock);
        {
                ret = __cache_item (cache, /* path = */ NULL, fh,
                                    host_addr, export_item, status);
        }
        pthread_mutex_unlock (&cache->lock);

out:
        return ret;
}

static auth_cache_status_t
__auth_cache_allows (struct auth_cache *cache, struct nfs3_fh *fh,
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
                ret = __cache_lookup_fh (cache, fh, host_addr, &ace);
        }

        if (path) {
                ret = __cache_lookup_path (cache, path, host_addr, &ace);
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
        auth_cache_status_t     ret = AUTH_CACHE_HOST_ENOENT;

        GF_VALIDATE_OR_GOTO ("nfs", cache, out);
        GF_VALIDATE_OR_GOTO ("nfs", fh, out);
        GF_VALIDATE_OR_GOTO ("nfs", host_addr, out);

        pthread_mutex_lock (&cache->lock);
        {
                ret = __auth_cache_allows (cache, fh, /* path = */ NULL,
                                           host_addr,
                                           /* check_rw_access = */ FALSE);
        }
        pthread_mutex_unlock (&cache->lock);

out:
        return ret;
}

auth_cache_status_t
auth_cache_allows_write_to_fh (struct auth_cache *cache, struct nfs3_fh *fh,
                               const char *host_addr)
{
        auth_cache_status_t     ret = AUTH_CACHE_HOST_ENOENT;

        GF_VALIDATE_OR_GOTO ("nfs", cache, out);
        GF_VALIDATE_OR_GOTO ("nfs", fh, out);
        GF_VALIDATE_OR_GOTO ("nfs", host_addr, out);

        pthread_mutex_lock (&cache->lock);
        {
                ret = __auth_cache_allows (cache, fh, /* path = */ NULL,
                                           host_addr,
                                           /* check_rw_access = */ TRUE);
        }
        pthread_mutex_unlock (&cache->lock);

out:
        return ret;
}

auth_cache_status_t
auth_cache_allows_path (struct auth_cache *cache, const char *path,
                        const char *host_addr)
{
        auth_cache_status_t     ret = AUTH_CACHE_HOST_ENOENT;

        GF_VALIDATE_OR_GOTO ("nfs", cache, out);
        GF_VALIDATE_OR_GOTO ("nfs", path, out);
        GF_VALIDATE_OR_GOTO ("nfs", host_addr, out);

        pthread_mutex_lock (&cache->lock);
        {
                ret = __auth_cache_allows (cache, /* fh = */ NULL,
                                           path, host_addr,
                                           /* check_rw_access = */ FALSE);
        }
        pthread_mutex_unlock (&cache->lock);

out:
        return ret;
}
