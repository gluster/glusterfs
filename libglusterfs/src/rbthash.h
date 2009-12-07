/*
  Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __RBTHASH_TABLE_H_
#define __RBTHASH_TABLE_H_
#include "rb.h"
#include "locking.h"
#include "mem-pool.h"
#include "logging.h"
#include "common-utils.h"

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
        int             keylen;
        uint32_t        keyhash;
} rbthash_entry_t;

typedef uint32_t (*rbt_hasher_t) (void *data, int len);
typedef void (*rbt_data_destroyer_t) (void *data);

typedef struct rbthash_table {
        int                     size;
        int                     numbuckets;
        struct mem_pool         *entrypool;
        gf_lock_t               tablelock;
        struct rbthash_bucket   *buckets;
        rbt_hasher_t            hashfunc;
        rbt_data_destroyer_t    dfunc;
        gf_boolean_t            pool_alloced;
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
#endif
