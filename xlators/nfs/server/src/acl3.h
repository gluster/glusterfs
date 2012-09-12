/*
 * Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 */

#ifndef _ACL3_H
#define _ACL3_H

#define GF_ACL3_PORT            38469
#define GF_ACL                  GF_NFS"-ACL"

#define ACL_PROGRAM 100227
#define ACL_V3 3

#define ACL_USER_OBJ 0x1
#define ACL_GROUP_OBJ 0x4
#define ACL_OTHER_OBJ 0x20

#define POSIX_ACL_XATTR_VERSION 0x0002
#define NFS_ACL_MAX_ENTRIES     1024

rpcsvc_program_t *
acl3svc_init(xlator_t *nfsx);

#endif
