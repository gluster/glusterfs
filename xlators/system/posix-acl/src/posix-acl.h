/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _POSIX_ACL_H
#define _POSIX_ACL_H

#include <glusterfs/xlator.h>
#include <glusterfs/glusterfs-acl.h>

struct posix_acl *
posix_acl_new(int entry_count);
struct posix_acl *
posix_acl_ref(xlator_t *this, struct posix_acl *acl);
void
posix_acl_unref(xlator_t *this, struct posix_acl *acl);

#endif /* !_POSIX_ACL_H */
