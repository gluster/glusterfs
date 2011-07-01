/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _POSIX_ACL_H
#define _POSIX_ACL_H

#include <stdint.h>

#include "xlator.h"
#include "common-utils.h"
#include "byte-order.h"


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

#define POSIX_ACL_UNDEFINED_ID        ((id_t)-1)


struct posix_ace {
        uint16_t     tag;
        uint16_t     perm;
        uint32_t     id;
};


struct posix_acl {
        int               refcnt;
        int               count;
        struct posix_ace  entries[0];
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
        struct posix_acl *minimal_acl;
};


struct posix_acl *posix_acl_new (xlator_t *this, int entry_count);
struct posix_acl *posix_acl_ref (xlator_t *this, struct posix_acl *acl);
void posix_acl_unref (xlator_t *this, struct posix_acl *acl);
void posix_acl_destroy (xlator_t *this, struct posix_acl *acl);
struct posix_acl_ctx *posix_acl_ctx_get (inode_t *inode, xlator_t *this);
int posix_acl_get (inode_t *inode, xlator_t *this,
                   struct posix_acl **acl_access_p,
                   struct posix_acl **acl_default_p);
int posix_acl_set (inode_t *inode, xlator_t *this, struct posix_acl *acl_access,
                   struct posix_acl *acl_default);

#endif /* !_POSIX_ACL_H */
