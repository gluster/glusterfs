/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __RBTHASH_TABLE_H_
#define __RBTHASH_TABLE_H_
#include "rb.h"
#include "locking.h"
#include "mem-pool.h"
#include "logging.h"
#include "common-utils.h"
#include "list.h"

#include <pthread.h>

#define GF_RBTHASH_MEMPOOL      16384 //1048576
#define GF_RBTHASH              "rbthash"

struct rbthash_bucket {
        struct rb_table *bucket;
        gf_lock_t       bucketlock;
};

typedef struct rbthash_entry {
        void            *data;
        void            *key;
        int              keylen;
        uint32_t         keyhash;
        struct list_head list;
} rbthash_entry_t;

typedef uint32_t (*rbt_hasher_t) (void *data, int len);
typedef void (*rbt_data_destroyer_t) (void *data);
typedef void (*rbt_traverse_t) (void *data, void *mydata);

typedef struct rbthash_table {
        int                     size;
        int                     numbuckets;
        struct mem_pool         *entrypool;
        gf_lock_t               tablelock;
        struct rbthash_bucket   *buckets;
        rbt_hasher_t            hashfunc;
        rbt_data_destroyer_t    dfunc;
        gf_boolean_t            pool_alloced;
        struct list_head        list;
} rbthash_table_t;

extern rbthash_table_t *
rbthash_table_init (int buckets, rbt_hasher_t hfunc,
                    rbt_data_destroyer_t dfunc, unsigned long expected_entries,
                    struct mem_pool *entrypool);

extern int
rbthash_insert (rbthash_table_t *tbl, void *data, void *key, int keylen);

extern void *
rbthash_get (rbthash_table_t *tbl, void *key, int keylen);

extern void *
rbthash_remove (rbthash_table_t *tbl, void *key, int keylen);

extern void *
rbthash_replace (rbthash_table_t *tbl, void *key, int keylen, void *newdata);

extern void
rbthash_table_destroy (rbthash_table_t *tbl);

extern void
rbthash_table_traverse (rbthash_table_t *tbl, rbt_traverse_t traverse,
                        void *mydata);
#endif
