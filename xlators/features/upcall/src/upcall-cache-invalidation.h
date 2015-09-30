/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __UPCALL_CACHE_INVALIDATION_H__
#define __UPCALL_CACHE_INVALIDATION_H__

/* The time period for which a client will be notified of cache_invalidation
 * events post its last access */
#define CACHE_INVALIDATION_TIMEOUT "60"

/* Flags sent for cache_invalidation */
#define UP_NLINK   0x00000001   /* update nlink */
#define UP_MODE    0x00000002   /* update mode and ctime */
#define UP_OWN     0x00000004   /* update mode,uid,gid and ctime */
#define UP_SIZE    0x00000008   /* update fsize */
#define UP_TIMES   0x00000010   /* update all times */
#define UP_ATIME   0x00000020   /* update atime only */
#define UP_PERM    0x00000040   /* update fields needed for
                                   permission checking */
#define UP_RENAME  0x00000080   /* this is a rename op -
                                   delete the cache entry */
#define UP_FORGET  0x00000100   /* inode_forget on server side -
                                   invalidate the cache entry */
#define UP_PARENT_TIMES   0x00000200   /* update parent dir times */

/* for fops - open, read, lk, */
#define UP_UPDATE_CLIENT        (UP_ATIME)

/* for fop - write, truncate */
#define UP_WRITE_FLAGS          (UP_SIZE | UP_TIMES)

/* for fop - setattr */
#define UP_ATTR_FLAGS           (UP_SIZE | UP_TIMES | UP_OWN |        \
                                 UP_MODE | UP_PERM)
/* for fop - rename */
#define UP_RENAME_FLAGS         (UP_RENAME)

/* to invalidate parent directory entries for fops -rename, unlink,
 * rmdir, mkdir, create */
#define UP_PARENT_DENTRY_FLAGS  (UP_PARENT_TIMES)

/* for fop - unlink, link, rmdir, mkdir */
#define UP_NLINK_FLAGS          (UP_NLINK | UP_TIMES)

/* xlator options */
gf_boolean_t is_cache_invalidation_enabled(xlator_t *this);
int32_t get_cache_invalidation_timeout(xlator_t *this);

#endif /* __UPCALL_CACHE_INVALIDATION_H__ */
