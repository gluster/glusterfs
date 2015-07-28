/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "rbthash.h"
#include "rb.h"
#include "locking.h"
#include "mem-pool.h"
#include "logging.h"
#include "libglusterfs-messages.h"

#include <pthread.h>
#include <string.h>


int
rbthash_comparator (void *entry1, void *entry2, void *param)
{
        int             ret = 0;
        rbthash_entry_t *e1 = NULL;
        rbthash_entry_t *e2 = NULL;

        if ((!entry1) || (!entry2) || (!param))
                return -1;

        e1 = (rbthash_entry_t *)entry1;
        e2 = (rbthash_entry_t *)entry2;

        if (e1->keylen != e2->keylen) {
                if (e1->keylen < e2->keylen)
                        ret = -1;
                else if (e1->keylen > e2->keylen)
                        ret = 1;
        } else
                ret = memcmp (e1->key, e2->key, e1->keylen);

        return ret;
}


int
__rbthash_init_buckets (rbthash_table_t *tbl, int buckets)
{
        int     i = 0;
        int     ret = -1;

        if (!tbl)
                return -1;

        for (; i < buckets; i++) {
                LOCK_INIT (&tbl->buckets[i].bucketlock);
                tbl->buckets[i].bucket = rb_create ((rb_comparison_func *)rbthash_comparator, tbl, NULL);
                if (!tbl->buckets[i].bucket) {
                        gf_msg (GF_RBTHASH, GF_LOG_ERROR, 0,
                                LG_MSG_RB_TABLE_CREATE_FAILED, "Failed to "
                                "create rb table bucket");
                        ret = -1;
                        goto err;
                }
        }

        ret = 0;
err:
        return ret;
}


/*
 * rbthash_table_init - Initialize a RBT based hash table
 * @buckets - Number of buckets in the hash table
 * @hfunc   - hashing function
 * @dfunc   - destroyer for data in the RBT
 * @expected_entries - Number of entries expected in RBT. Mutually exclusive
 * with entrypool.
 * @entrypool -  Memory pool in lieu of expected_entries.
 */

rbthash_table_t *
rbthash_table_init (int buckets, rbt_hasher_t hfunc,
                    rbt_data_destroyer_t dfunc,
                    unsigned long expected_entries,
                    struct mem_pool *entrypool)
{
        rbthash_table_t         *newtab = NULL;
        int                     ret = -1;

        if (!hfunc) {
                gf_msg (GF_RBTHASH, GF_LOG_ERROR, 0, LG_MSG_HASH_FUNC_ERROR,
                        "Hash function not given");
                return NULL;
        }

        if (!entrypool && !expected_entries) {
                gf_msg (GF_RBTHASH, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY,
                        "Both mem-pool and expected entries not provided");
                return NULL;
        }

        if (entrypool && expected_entries) {
                gf_msg (GF_RBTHASH, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY,
                        "Both mem-pool and expected entries are provided");
                return NULL;
        }


        newtab = GF_CALLOC (1, sizeof (*newtab),
                            gf_common_mt_rbthash_table_t);
        if (!newtab)
                return NULL;

        newtab->buckets = GF_CALLOC (buckets, sizeof (struct rbthash_bucket),
                                     gf_common_mt_rbthash_bucket);
        if (!newtab->buckets) {
                goto free_newtab;
        }

        if (expected_entries) {
                newtab->entrypool =
                        mem_pool_new (rbthash_entry_t, expected_entries);
                if (!newtab->entrypool) {
                        goto free_buckets;
                }
                newtab->pool_alloced = _gf_true;
        } else {
                newtab->entrypool = entrypool;
        }

        LOCK_INIT (&newtab->tablelock);
        INIT_LIST_HEAD (&newtab->list);
        newtab->numbuckets = buckets;
        ret = __rbthash_init_buckets (newtab, buckets);

        if (ret == -1) {
                gf_msg (GF_RBTHASH, GF_LOG_ERROR, 0,
                        LG_MSG_RBTHASH_INIT_BUCKET_FAILED,
                        "Failed to init buckets");
                if (newtab->pool_alloced)
                        mem_pool_destroy (newtab->entrypool);
        } else {
                gf_msg_trace (GF_RBTHASH, 0, "Inited hash table: buckets:"
                        " %d", buckets);
        }

        newtab->hashfunc = hfunc;
        newtab->dfunc = dfunc;

free_buckets:
        if (ret == -1)
                GF_FREE (newtab->buckets);

free_newtab:
        if (ret == -1) {
                GF_FREE (newtab);
                newtab = NULL;
        }

        return newtab;
}

rbthash_entry_t *
rbthash_init_entry (rbthash_table_t *tbl, void *data, void *key, int keylen)
{
        int             ret = -1;
        rbthash_entry_t *entry = NULL;

        if ((!tbl) || (!data) || (!key))
                return NULL;

        entry = mem_get (tbl->entrypool);
        if (!entry) {
                gf_msg (GF_RBTHASH, GF_LOG_ERROR, 0,
                        LG_MSG_RBTHASH_GET_ENTRY_FAILED,
                        "Failed to get entry from mem-pool");
                goto ret;
        }

        entry->data = data;
        entry->key = GF_CALLOC (keylen, sizeof (char), gf_common_mt_char);
        if (!entry->key) {
                goto free_entry;
        }

        INIT_LIST_HEAD (&entry->list);
        memcpy (entry->key, key, keylen);
        entry->keylen = keylen;
        entry->keyhash = tbl->hashfunc (entry->key, entry->keylen);
        gf_msg_trace (GF_RBTHASH, 0, "HASH: %u", entry->keyhash);

        ret = 0;
free_entry:
        if (ret == -1) {
                mem_put (entry);
                entry = NULL;
        }

ret:
        return entry;
}


void
rbthash_deinit_entry (rbthash_table_t *tbl, rbthash_entry_t *entry)
{

        if (!entry)
                return;

        GF_FREE (entry->key);

        if (tbl) {
                if ((entry->data) && (tbl->dfunc))
                        tbl->dfunc (entry->data);

                LOCK (&tbl->tablelock);
                {
                        list_del_init (&entry->list);
                }
                UNLOCK (&tbl->tablelock);

                mem_put (entry);
        }

        return;
}


static struct rbthash_bucket *
rbthash_entry_bucket (rbthash_table_t *tbl, rbthash_entry_t * entry)
{
        int     nbucket = 0;

        nbucket = (entry->keyhash % tbl->numbuckets);
        gf_msg_trace (GF_RBTHASH, 0, "BUCKET: %d", nbucket);
        return &tbl->buckets[nbucket];
}


int
rbthash_insert_entry (rbthash_table_t *tbl, rbthash_entry_t *entry)
{
        struct rbthash_bucket   *bucket = NULL;
        int                     ret = -1;

        if ((!tbl) || (!entry))
                return -1;

        bucket = rbthash_entry_bucket (tbl, entry);
        if (!bucket) {
                gf_msg (GF_RBTHASH, GF_LOG_ERROR, 0,
                        LG_MSG_RBTHASH_GET_BUCKET_FAILED,
                        "Failed to get bucket");
                goto err;
        }

        ret = 0;
        LOCK (&bucket->bucketlock);
        {
                if (!rb_probe (bucket->bucket, (void *)entry)) {
                        gf_msg (GF_RBTHASH, GF_LOG_ERROR, 0,
                                LG_MSG_RBTHASH_INSERT_FAILED, "Failed to insert"
                                " entry");
                        ret = -1;
                }
        }
        UNLOCK (&bucket->bucketlock);

err:
        return ret;
}


int
rbthash_insert (rbthash_table_t *tbl, void *data, void *key, int keylen)
{
        rbthash_entry_t         *entry = NULL;
        int                     ret = -1;

        if ((!tbl) || (!data) || (!key))
                return -1;

        entry = rbthash_init_entry (tbl, data, key, keylen);
        if (!entry) {
                gf_msg (GF_RBTHASH, GF_LOG_ERROR, 0,
                        LG_MSG_RBTHASH_INIT_ENTRY_FAILED,
                        "Failed to init entry");
                goto err;
        }

        ret = rbthash_insert_entry (tbl, entry);

        if (ret == -1) {
                gf_msg (GF_RBTHASH, GF_LOG_ERROR, 0,
                        LG_MSG_RBTHASH_INSERT_FAILED,
                        "Failed to insert entry");
                rbthash_deinit_entry (tbl, entry);
        }

        LOCK (&tbl->tablelock);
        {
                list_add_tail (&entry->list, &tbl->list);
        }
        UNLOCK (&tbl->tablelock);

err:
        return ret;
}

static struct rbthash_bucket *
rbthash_key_bucket (rbthash_table_t *tbl, void *key, int keylen)
{
        uint32_t        keyhash = 0;
        int             nbucket = 0;

        if ((!tbl) || (!key))
                return NULL;

        keyhash =  tbl->hashfunc (key, keylen);
        gf_msg_trace (GF_RBTHASH, 0, "HASH: %u", keyhash);
        nbucket = (keyhash % tbl->numbuckets);
        gf_msg_trace (GF_RBTHASH, 0, "BUCKET: %u", nbucket);

        return &tbl->buckets[nbucket];
}


void *
rbthash_get (rbthash_table_t *tbl, void *key, int keylen)
{
        struct rbthash_bucket           *bucket = NULL;
        rbthash_entry_t                 *entry = NULL;
        rbthash_entry_t                 searchentry = {0, };

        if ((!tbl) || (!key))
                return NULL;

        bucket = rbthash_key_bucket (tbl, key, keylen);
        if (!bucket) {
                gf_msg (GF_RBTHASH, GF_LOG_ERROR, 0, LG_MSG_GET_BUCKET_FAILED,
                        "Failed to get bucket");
                return NULL;
        }

        searchentry.key = key;
        searchentry.keylen = keylen;
        LOCK (&bucket->bucketlock);
        {
                entry = rb_find (bucket->bucket, &searchentry);
        }
        UNLOCK (&bucket->bucketlock);

        if (!entry)
                return NULL;

        return entry->data;
}


void *
rbthash_remove (rbthash_table_t *tbl, void *key, int keylen)
{
        struct rbthash_bucket   *bucket = NULL;
        rbthash_entry_t         *entry = NULL;
        rbthash_entry_t         searchentry = {0, };
        void                    *dataref = NULL;

        if ((!tbl) || (!key))
                return NULL;

        bucket = rbthash_key_bucket (tbl, key, keylen);
        if (!bucket) {
                gf_msg (GF_RBTHASH, GF_LOG_ERROR, 0,
                        LG_MSG_RBTHASH_GET_BUCKET_FAILED,
                        "Failed to get bucket");
                return NULL;
        }

        searchentry.key = key;
        searchentry.keylen = keylen;

        LOCK (&bucket->bucketlock);
        {
                entry = rb_delete (bucket->bucket, &searchentry);
        }
        UNLOCK (&bucket->bucketlock);

        if (!entry)
                return NULL;

        GF_FREE (entry->key);
        dataref = entry->data;

        LOCK (&tbl->tablelock);
        {
                list_del_init (&entry->list);
        }
        UNLOCK (&tbl->tablelock);

        mem_put (entry);

        return dataref;
}


void
rbthash_entry_deiniter (void *entry, void *rbparam)
{
        if (!entry)
                return;

        rbthash_deinit_entry (rbparam, entry);
}


void
rbthash_table_destroy_buckets (rbthash_table_t *tbl)
{
        int x = 0;
        if (!tbl)
                return;

        for (;x < tbl->numbuckets; x++) {
                LOCK_DESTROY (&tbl->buckets[x].bucketlock);
                rb_destroy (tbl->buckets[x].bucket, rbthash_entry_deiniter);
        }

        return;
}


void
rbthash_table_destroy (rbthash_table_t *tbl)
{
        if (!tbl)
                return;

        rbthash_table_destroy_buckets (tbl);
        if (tbl->pool_alloced)
                mem_pool_destroy (tbl->entrypool);

        GF_FREE (tbl->buckets);
        GF_FREE (tbl);
}


void
rbthash_table_traverse (rbthash_table_t *tbl, rbt_traverse_t traverse,
                        void *mydata)
{
        rbthash_entry_t *entry = NULL;

        if ((tbl == NULL) || (traverse == NULL)) {
                goto out;
        }

        LOCK (&tbl->tablelock);
        {
                list_for_each_entry (entry, &tbl->list, list) {
                        traverse (entry->data, mydata);
                }
        }
        UNLOCK (&tbl->tablelock);

out:
        return;
}
