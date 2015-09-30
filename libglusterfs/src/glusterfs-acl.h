/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERFS_ACL_H
#define _GLUSTERFS_ACL_H


/* WARNING: Much if this code is restricted to Linux usage.
 *
 * It would be much cleaner to replace the code with something that is based on
 * libacl (or its libc implementation on *BSD).
 *
 * Initial work for replacing this Linux specific implementation has been
 * started as part of the "Improve POSIX ACLs" feature. Functionality for this
 * feature has been added to the end of this file.
 */

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

#define POSIX_ACL_XATTR_VERSION       (0x02)

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

typedef struct posix_acl_xattr_entry  posix_acl_xattr_entry;
typedef struct posix_acl_xattr_header posix_acl_xattr_header;

static inline size_t
posix_acl_xattr_size (unsigned int count)
{
        return (sizeof(posix_acl_xattr_header) +
                       (count * sizeof(posix_acl_xattr_entry)));
}

static inline ssize_t
posix_acl_xattr_count (size_t size)
{
        if (size < sizeof(posix_acl_xattr_header))
                return (-1);
        size -= sizeof(posix_acl_xattr_header);
        if (size % sizeof(posix_acl_xattr_entry))
                return (-1);
        return (size / sizeof(posix_acl_xattr_entry));
}

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


/* Above this comment, the legacy POSIX ACL support is kept until it is not
 * used anymore. Below you will find the more portable version to support POSIX
 * ACls based on the implementation of libacl (see sys/acl.h). */

/* virtual xattrs passed over RPC, not stored on disk */
#define GF_POSIX_ACL_ACCESS       "glusterfs.posix.acl"
#define GF_POSIX_ACL_DEFAULT      "glusterfs.posix.default_acl"
#define GF_POSIX_ACL_REQUEST(key) \
        (!strncmp(key, GF_POSIX_ACL_ACCESS, strlen(GF_POSIX_ACL_ACCESS)) || \
         !strncmp(key, GF_POSIX_ACL_DEFAULT, strlen(GF_POSIX_ACL_DEFAULT)))

#ifdef HAVE_SYS_ACL_H /* only NetBSD does not support POSIX ACLs */

#include <sys/acl.h>

static inline const char*
gf_posix_acl_get_key (const acl_type_t type)
{
        char *acl_key = NULL;

        switch (type) {
        case ACL_TYPE_ACCESS:
                acl_key = GF_POSIX_ACL_ACCESS;
                break;
        case ACL_TYPE_DEFAULT:
                acl_key = GF_POSIX_ACL_DEFAULT;
                break;
        default:
                errno = EINVAL;
        }

        return acl_key;
}

static inline const acl_type_t
gf_posix_acl_get_type (const char *key)
{
        acl_type_t type = 0;

        if (!strncmp (key, GF_POSIX_ACL_ACCESS, strlen (GF_POSIX_ACL_ACCESS)))
                type = ACL_TYPE_ACCESS;
        else if (!strncmp (key, GF_POSIX_ACL_DEFAULT,
                           strlen (GF_POSIX_ACL_DEFAULT)))
                type = ACL_TYPE_DEFAULT;
        else
                errno = EINVAL;

        return type;
}

#endif /* HAVE_SYS_ACL_H */
#endif /* _GLUSTERFS_ACL_H */
