/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
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


#ifndef _POSIX_ACL_XATTR_H
#define _POSIX_ACL_XATTR_H

#include <stdint.h>

#include "common-utils.h"
#include "posix-acl.h"

#define POSIX_ACL_ACCESS_XATTR "system.posix_acl_access"
#define POSIX_ACL_DEFAULT_XATTR "system.posix_acl_default"

#define POSIX_ACL_VERSION 2 

struct posix_acl_xattr_entry {
        uint16_t            tag;
        uint16_t            perm;
        uint32_t            id;
};

struct posix_acl_xattr_header {
        uint32_t                        version;
        struct posix_acl_xattr_entry    entries[0];
};

struct posix_acl *posix_acl_from_xattr (xlator_t *this, const char *buf, int size);

int posix_acl_to_xattr (xlator_t *this, struct posix_acl *acl, char *buf, int size);

int posix_acl_matches_xattr (xlator_t *this, struct posix_acl *acl, const char *buf, int size);


#endif /* !_POSIX_ACL_XATTR_H */
