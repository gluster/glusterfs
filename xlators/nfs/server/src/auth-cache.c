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

enum auth_cache_lookup_results {
        ENTRY_FOUND     =  0,
        ENTRY_NOT_FOUND = -1,
        ENTRY_EXPIRED   = -2,
};

struct auth_cache_entry {
        GF_REF_DECL;                    /* refcounting */
        data_t             *data;       /* data_unref() on refcount == 0 */

        time_t              timestamp;
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

        nbytes = strlen (exportid) + strlen (host)
                 + strlen (mountid) + 3;
        hashkey = GF_MALLOC (nbytes, gf_common_mt_char);
        if (!hashkey)
                return NULL;

        snprintf (hashkey, nbytes, "%s:%s:%s", exportid,
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

        LOCK_INIT (&cache->lock);
        cache->ttl_sec = ttl_sec;
out:
        return cache;
}

/* auth_cache_entry_free -- called by refcounting subsystem on refcount == 0
 *
 * @to_free: auth_cache_entry that has refcount == 0 and needs to get free'd
 */
void
auth_cache_entry_free (void *to_free)
{
        struct auth_cache_entry *entry      = to_free;
        data_t                  *entry_data = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS, entry, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, entry->data, out);

        entry_data = entry->data;
        /* set data_t->data to NULL, otherwise data_unref() tries to free it */
        entry_data->data = NULL;
        data_unref (entry_data);

        GF_FREE (entry);
out:
        return;
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
        else
                GF_REF_INIT (entry, auth_cache_entry_free);

        return entry;
}

/**
 * auth_cache_add -- Add an auth_cache_entry to the cache->dict
 *
 * @return: 0 on success, non-zero otherwise.
 */
static int
auth_cache_add (struct auth_cache *cache, char *hashkey,
                struct auth_cache_entry *entry)
{
        int      ret        = -1;
        data_t  *entry_data = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS, cache, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, cache->cache_dict, out);

        /* FIXME: entry is passed as parameter, this can never fail? */
        entry = GF_REF_GET (entry);
        if (!entry) {
                /* entry does not have any references */
                ret = -1;
                goto out;
        }

        entry_data = bin_to_data (entry, sizeof (*entry));
        if (!entry_data) {
                ret = -1;
                GF_REF_PUT (entry);
                goto out;
        }

        /* we'll take an extra ref on the data_t, it gets unref'd when the
         * auth_cache_entry is released */
        entry->data = data_ref (entry_data);

        LOCK (&cache->lock);
        {
                ret = dict_set (cache->cache_dict, hashkey, entry_data);
        }
        UNLOCK (&cache->lock);

        if (ret) {
                /* adding to dict failed */
                GF_REF_PUT (entry);
        }
out:
        return ret;
}

/**
 * _auth_cache_expired -- Check if the auth_cache_entry has expired
 *
 * The auth_cache->lock should have been taken when this function is called.
 *
 * @return: true when the auth_cache_entry is expired, false otherwise.
 */
static int
_auth_cache_expired (struct auth_cache *cache, struct auth_cache_entry *entry)
{
        return ((time (NULL) - entry->timestamp) > cache->ttl_sec);
}

/**
 * auth_cache_get -- Get the @hashkey entry from the cache->cache_dict
 *
 * @cache: The auth_cache that should contain the @entry.
 * @haskkey: The key associated with the auth_cache_entry.
 * @entry: The found auth_cache_entry, unmodified if not found/expired.
 *
 * The using the cache->dict requires locking, this function takes care of
 * that. When the entry is found, but has expired, it will be removed from the
 * cache_dict.
 *
 * @return: 0 when found, ENTRY_NOT_FOUND or ENTRY_EXPIRED otherwise.
 */
static enum auth_cache_lookup_results
auth_cache_get (struct auth_cache *cache, char *hashkey,
                struct auth_cache_entry **entry)
{
        enum auth_cache_lookup_results  ret        = ENTRY_NOT_FOUND;
        data_t                         *entry_data = NULL;
        struct auth_cache_entry        *lookup_res = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS, cache, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, cache->cache_dict, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, hashkey, out);

        LOCK (&cache->lock);
        {
                entry_data = dict_get (cache->cache_dict, hashkey);
                if (!entry_data)
                        goto unlock;

                /* FIXME: this is dangerous use of entry_data */
                lookup_res = GF_REF_GET ((struct auth_cache_entry *) entry_data->data);
                if (lookup_res == NULL) {
                        /* entry has been free'd */
                        ret = ENTRY_EXPIRED;
                        goto unlock;
                }

                if (_auth_cache_expired (cache, lookup_res)) {
                        ret = ENTRY_EXPIRED;

                        /* free entry and remove from the cache */
                        GF_FREE (lookup_res);
                        entry_data->data = NULL;
                        dict_del (cache->cache_dict, hashkey);

                        goto unlock;
                }

                *entry = lookup_res;
                ret = ENTRY_FOUND;
        }
unlock:
        UNLOCK (&cache->lock);

out:
        return ret;
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
        char                           *hashkey    = NULL;
        struct auth_cache_entry        *lookup_res = NULL;
        enum auth_cache_lookup_results  ret        = ENTRY_NOT_FOUND;

        GF_VALIDATE_OR_GOTO (GF_NFS, cache, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, fh, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, host_addr, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, timestamp, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, can_write, out);

        hashkey = make_hashkey (fh, host_addr);
        if (!hashkey) {
                ret = -ENOMEM;
                goto out;
        }

        ret = auth_cache_get (cache, hashkey, &lookup_res);
        switch (ret) {
        case ENTRY_FOUND:
                *timestamp = lookup_res->timestamp;
                *can_write = lookup_res->item->opts->rw;
                GF_REF_PUT (lookup_res);
                break;

        case ENTRY_NOT_FOUND:
                gf_msg_debug (GF_NFS, 0, "could not find entry for %s",
                              host_addr);
                break;

        case ENTRY_EXPIRED:
                gf_msg_debug (GF_NFS, 0, "entry for host %s has expired",
                              host_addr);
                break;
        }

out:
        GF_FREE (hashkey);

        return ret;
}

/* auth_cache_entry_purge -- free up the auth_cache_entry
 *
 * This gets called through dict_foreach() by auth_cache_purge(). Each
 * auth_cache_entry has a refcount which needs to be decremented. Once the
 * auth_cache_entry reaches refcount == 0, auth_cache_entry_free() will call
 * data_unref() to free the associated data_t.
 *
 * @d: dict that gets purged by auth_cache_purge()
 * @k: hashkey of the current entry
 * @v: data_t of the current entry
 */
int
auth_cache_entry_purge (dict_t *d, char *k, data_t *v, void *_unused)
{
        struct auth_cache_entry *entry = (struct auth_cache_entry *) v->data;

        if (entry)
                GF_REF_PUT (entry);

        return 0;
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
        dict_t *old_cache_dict = NULL;

        if (!cache || !new_cache_dict)
                goto out;

        LOCK (&cache->lock);
        {
                old_cache_dict = cache->cache_dict;
                cache->cache_dict = new_cache_dict;
        }
        UNLOCK (&cache->lock);

        /* walk all entries and refcount-- with GF_REF_PUT() */
        dict_foreach (old_cache_dict, auth_cache_entry_purge, NULL);
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

        ret = auth_cache_add (cache, hashkey, entry);
        GF_REF_PUT (entry);
        if (ret)
                goto out;

        gf_msg_trace (GF_NFS, 0, "Caching file-handle (%s)", host_addr);
        ret = 0;

out:
        GF_FREE (hashkey);

        return ret;
}
