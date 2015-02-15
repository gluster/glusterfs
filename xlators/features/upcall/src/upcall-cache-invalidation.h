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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

/* TODO: Below macros have to be replaced with
 * xlator options - Bug1200271 */
#define ON_CACHE_INVALIDATION 0 /* disable by default */

/* The time period for which a client will be notified of cache_invalidation
 * events post its last access */
#define CACHE_INVALIDATION_PERIOD 60

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

/* for fops - open, read, lk, */
#define UP_IDEMPOTENT_FLAGS     (UP_ATIME)

/* for fop - write, truncate */
#define UP_WRITE_FLAGS          (UP_SIZE | UP_TIMES)

/* for fop - setattr */
#define UP_ATTR_FLAGS           (UP_SIZE | UP_TIMES | UP_OWN |        \
                                 UP_MODE | UP_PERM)
/* for fop - rename */
#define UP_RENAME_FLAGS         (UP_RENAME)

/* to invalidate parent directory entries for fops -rename, unlink,
 * rmdir, mkdir, create */
#define UP_PARENT_DENTRY_FLAGS  (UP_TIMES)

/* for fop - unlink, link, rmdir, mkdir */
#define UP_NLINK_FLAGS          (UP_NLINK | UP_TIMES)

#define CACHE_INVALIDATE(frame, this, client, inode, p_flags) do {      \
 if (ON_CACHE_INVALIDATION) {                                             \
        (void)upcall_cache_invalidate (frame, this, client, inode, p_flags);  \
 }                                                                      \
} while (0)

#define CACHE_INVALIDATE_DIR(frame, this, client, inode_p, p_flags) do {      \
 if (ON_CACHE_INVALIDATION) {                                             \
        dentry_t *dentry;                                               \
        dentry_t *dentry_tmp;                                           \
        list_for_each_entry_safe (dentry, dentry_tmp,                   \
                                  &inode_p->dentry_list,                \
                                  inode_list) {                         \
                (void)upcall_cache_invalidate (frame, this, client,     \
                                               dentry->inode, p_flags); \
        }                                                               \
 }                                                                      \
} while (0)

#endif /* __UPCALL_CACHE_INVALIDATION_H__ */
