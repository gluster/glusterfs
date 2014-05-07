/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __SNAP_VIEW_H__
#define __SNAP_VIEW_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "dict.h"
#include "defaults.h"
#include "mem-types.h"
#include "call-stub.h"
#include "inode.h"
#include "byte-order.h"
#include "iatt.h"
#include <ctype.h>
#include <sys/uio.h>
#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "glfs.h"
#include "common-utils.h"
#include "glfs-handles.h"
#include "glfs-internal.h"
#include "glusterfs3-xdr.h"
#include "glusterfs-acl.h"
#include "syncop.h"
#include "list.h"

/*
 * The max number of snap entries we consider currently
 */
#define SNAP_VIEW_MAX_NUM_SNAPS 128

#define DEFAULT_SVD_LOG_FILE_DIRECTORY DATADIR "/log/glusterfs"

typedef enum {
        SNAP_VIEW_ENTRY_POINT_INODE = 0,
        SNAP_VIEW_VIRTUAL_INODE
} inode_type_t;

struct svs_inode {
        glfs_t *fs;
        glfs_object_t *object;
        inode_type_t type;

        /* used only for entry point directory where gfid of the directory
           from where the entry point was entered is saved.
        */
        uuid_t pargfid;
        struct iatt buf;
};
typedef struct svs_inode svs_inode_t;

struct svs_fd {
        glfs_fd_t *fd;
};
typedef struct svs_fd svs_fd_t;

struct snap_dirent {
	char name[NAME_MAX];
	char uuid[UUID_CANONICAL_FORM_LEN + 1];
        glfs_t *fs;
};
typedef struct snap_dirent snap_dirent_t;

struct svs_private {
	snap_dirent_t *dirents;
	int num_snaps;
};
typedef struct svs_private svs_private_t;

glfs_t *
svs_intialise_snapshot_volume (xlator_t *this, const char *name);

snap_dirent_t *
svs_get_snap_dirent (xlator_t *this, const char *name);

#endif /* __SNAP_VIEW_H__ */
