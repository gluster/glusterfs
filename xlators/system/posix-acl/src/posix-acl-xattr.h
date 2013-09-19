/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _POSIX_ACL_XATTR_H
#define _POSIX_ACL_XATTR_H

#include "common-utils.h"
#include "posix-acl.h"
#include "glusterfs.h"
#include "glusterfs-acl.h"

struct posix_acl *posix_acl_from_xattr (xlator_t *this, const char *buf, int size);

int posix_acl_to_xattr (xlator_t *this, struct posix_acl *acl, char *buf, int size);

int posix_acl_matches_xattr (xlator_t *this, struct posix_acl *acl, const char *buf, int size);


#endif /* !_POSIX_ACL_XATTR_H */
