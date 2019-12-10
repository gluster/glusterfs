/*
   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _POSIX_GFID_PATH_H
#define _POSIX_GFID_PATH_H

#include <glusterfs/compat-errno.h>

#include <stdint.h>               // for int32_t
#include "glusterfs/dict.h"       // for dict_t
#include "glusterfs/glusterfs.h"  // for gf_boolean_t
#include "glusterfs/inode.h"      // for inode_t
#include "uuid.h"                 // for uuid_t
#define MAX_GFID2PATH_LINK_SUP 500

gf_boolean_t
posix_is_gfid2path_xattr(const char *name);
int32_t
posix_get_gfid2path(xlator_t *this, inode_t *inode, const char *real_path,
                    int *op_errno, dict_t *dict);
#endif /* _POSIX_GFID_PATH_H */
