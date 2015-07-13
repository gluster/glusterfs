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

#ifndef _AUTH_CACHE_H_
#define _AUTH_CACHE_H_

#include "nfs-mem-types.h"
#include "mount3.h"
#include "exports.h"
#include "dict.h"
#include "nfs3.h"

struct auth_cache {
        gf_lock_t lock;          /* locking for the dict (and entries) */
        dict_t *cache_dict;      /* Dict holding fh -> authcache_entry */
        time_t ttl_sec;          /* TTL of the auth cache in seconds */
};


/* Initializes the cache */
struct auth_cache *
auth_cache_init (time_t ttl_sec);

/* Inserts FH into cache */
int
cache_nfs_fh (struct auth_cache *cache, struct nfs3_fh *fh,
              const char *host_addr, struct export_item *export_item);

/* Checks if the filehandle cached & writable */
gf_boolean_t
is_nfs_fh_cached_and_writeable (struct auth_cache *cache, struct nfs3_fh *fh,
                                const char *host_addr);

/* Checks if the filehandle is cached */
gf_boolean_t
is_nfs_fh_cached (struct auth_cache *cache, struct nfs3_fh *fh,
                  const char *host_addr);

/* Purge the cache */
void
auth_cache_purge (struct auth_cache *cache);

#endif /* _AUTH_CACHE_H_ */
