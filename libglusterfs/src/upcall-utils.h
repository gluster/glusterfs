/*
  Copyright (c) 2015, Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _UPCALL_UTILS_H
#define _UPCALL_UTILS_H

#include "iatt.h"
#include "compat-uuid.h"
#include "compat.h"

/* Flags sent for cache_invalidation */
#define UP_NLINK         0x00000001   /* update nlink */
#define UP_MODE          0x00000002   /* update mode and ctime */
#define UP_OWN           0x00000004   /* update mode,uid,gid and ctime */
#define UP_SIZE          0x00000008   /* update fsize */
#define UP_TIMES         0x00000010   /* update all times */
#define UP_ATIME         0x00000020   /* update atime only */
#define UP_PERM          0x00000040   /* update fields needed for permission
                                         checking */
#define UP_RENAME        0x00000080   /* this is a rename op - delete the cache
                                         entry */
#define UP_FORGET        0x00000100   /* inode_forget on server side -
                                         invalidate the cache entry */
#define UP_PARENT_TIMES  0x00000200   /* update parent dir times */

#define UP_XATTR         0x00000400   /* update the xattrs and ctime */
#define UP_XATTR_RM      0x00000800   /* Remove the xattrs and update ctime */

#define UP_EXPLICIT_LOOKUP 0x00001000 /* Request an explicit lookup */

#define UP_INVAL_ATTR      0x00002000 /* Request to invalidate iatt and xatt */

/* for fops - open, read, lk, */
#define UP_UPDATE_CLIENT        (UP_ATIME)

/* for fop - write, truncate */
#define UP_WRITE_FLAGS          (UP_SIZE | UP_TIMES)

/* for fop - setattr */
#define UP_ATTR_FLAGS           (UP_SIZE | UP_TIMES | UP_OWN | UP_MODE | \
                                 UP_PERM)
/* for fop - rename */
#define UP_RENAME_FLAGS         (UP_RENAME)

/* to invalidate parent directory entries for fops -rename, unlink, rmdir,
 * mkdir, create */
#define UP_PARENT_DENTRY_FLAGS  (UP_PARENT_TIMES)

/* for fop - unlink, link, rmdir, mkdir */
#define UP_NLINK_FLAGS          (UP_NLINK | UP_TIMES)

#define IATT_UPDATE_FLAGS       (UP_NLINK | UP_MODE | UP_OWN | UP_SIZE | \
                                 UP_TIMES | UP_ATIME | UP_PERM)

typedef enum {
        GF_UPCALL_EVENT_NULL,
        GF_UPCALL_CACHE_INVALIDATION,
        GF_UPCALL_RECALL_LEASE,
} gf_upcall_event_t;

struct gf_upcall {
        char      *client_uid;
        uuid_t    gfid;
        uint32_t  event_type;
        void      *data;
};

struct gf_upcall_cache_invalidation {
        uint32_t flags;
        uint32_t expire_time_attr;
        struct iatt stat;
        struct iatt p_stat; /* parent dir stat */
        struct iatt oldp_stat; /* oldparent dir stat */
        dict_t *dict; /* For xattrs */
};

struct gf_upcall_recall_lease {
        uint32_t  lease_type; /* Lease type to which client can downgrade to*/
        uuid_t    tid;        /* transaction id of the fop that caused
                                 the recall */
        dict_t   *dict;
};

#endif /* _UPCALL_UTILS_H */
