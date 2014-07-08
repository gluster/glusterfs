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

#include "glusterfs-acl.h"

#define ACL3_NULL 0
#define ACL3_GETACL 1
#define ACL3_SETACL 2
#define ACL3_PROC_COUNT 3

#define GF_ACL3_PORT            38469
#define GF_ACL                  GF_NFS"-ACL"

/* Flags for the getacl/setacl mode */
#define NFS_ACL                 0x0001
#define NFS_ACLCNT              0x0002
#define NFS_DFACL               0x0004
#define NFS_DFACLCNT            0x0008

/*
 * NFSv3, identifies the default ACL by NFS_ACL_DEFAULT. Gluster
 * NFS needs to mask it OFF before sending it up to POSIX layer
 * or File system layer.
 */
#define NFS_ACL_DEFAULT             0x1000

#define NFS_ACL_MAX_ENTRIES         1024

rpcsvc_program_t *
acl3svc_init(xlator_t *nfsx);

#endif
