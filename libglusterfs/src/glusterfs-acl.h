/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _COMMON_ACL_H
#define _COMMON_ACL_H

#include <stdint.h>
#include <sys/types.h> /* For uid_t */

#include "locking.h" /* For gf_lock_t in struct posix_acl_conf */

#define ACL_PROGRAM    100227
#define ACLV3_VERSION  3

#define POSIX_ACL_MINIMAL_ACE_COUNT   3

#define POSIX_ACL_READ                (0x04)
#define POSIX_ACL_WRITE               (0x02)
#define POSIX_ACL_EXECUTE             (0x01)

#define POSIX_ACL_UNDEFINED_TAG       (0x00)
#define POSIX_ACL_USER_OBJ            (0x01)
#define POSIX_ACL_USER                (0x02)
#define POSIX_ACL_GROUP_OBJ           (0x04)
#define POSIX_ACL_GROUP               (0x08)
#define POSIX_ACL_MASK                (0x10)
#define POSIX_ACL_OTHER               (0x20)

#define POSIX_ACL_UNDEFINED_ID        (-1)

#define POSIX_ACL_VERSION             (0x02)

#define POSIX_ACL_ACCESS_XATTR        "system.posix_acl_access"
#define POSIX_ACL_DEFAULT_XATTR       "system.posix_acl_default"

struct posix_acl_xattr_entry {
        uint16_t            tag;
        uint16_t            perm;
        uint32_t            id;
};

struct posix_acl_xattr_header {
        uint32_t                        version;
        struct posix_acl_xattr_entry    entries[];
};

struct posix_ace {
        uint16_t     tag;
        uint16_t     perm;
        uint32_t     id;
};


struct posix_acl {
        int               refcnt;
        int               count;
        struct posix_ace  entries[];
};

struct posix_acl_ctx {
        uid_t             uid;
        gid_t             gid;
        mode_t            perm;
        struct posix_acl *acl_access;
        struct posix_acl *acl_default;
};

struct posix_acl_conf {
        gf_lock_t         acl_lock;
        uid_t             super_uid;
        struct posix_acl *minimal_acl;
};

#endif /* _COMMON_ACL_H */
