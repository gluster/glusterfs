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
#include "timer.h"
#include "rpc-clnt.h"
#include "protocol-common.h"

#define DEFAULT_SVD_LOG_FILE_DIRECTORY DATADIR "/log/glusterfs"

#define SNAP_VIEW_MAX_GLFS_T            256
#define SNAP_VIEW_MAX_GLFS_FDS          1024
#define SNAP_VIEW_MAX_GLFS_OBJ_HANDLES  1024

#define SVS_STACK_DESTROY(_frame)                                   \
        do {                                                        \
                ((call_frame_t *)_frame)->local = NULL;             \
                STACK_DESTROY (((call_frame_t *)_frame)->root);     \
        } while (0)

int
svs_mgmt_submit_request (void *req, call_frame_t *frame,
                         glusterfs_ctx_t *ctx,
                         rpc_clnt_prog_t *prog, int procnum,
                         fop_cbk_fn_t cbkfn, xdrproc_t xdrproc);

int
svs_get_snapshot_list (xlator_t *this);

int
mgmt_get_snapinfo_cbk (struct rpc_req *req, struct iovec *iov,
                       int count, void *myframe);

char *clnt_handshake_procs[GF_HNDSK_MAXVALUE] = {
        [GF_HNDSK_NULL]         = "NULL",
        [GF_HNDSK_SETVOLUME]    = "SETVOLUME",
        [GF_HNDSK_GETSPEC]      = "GETSPEC",
        [GF_HNDSK_PING]         = "PING",
        [GF_HNDSK_EVENT_NOTIFY] = "EVENTNOTIFY",
};

rpc_clnt_prog_t svs_clnt_handshake_prog = {
        .progname  = "GlusterFS Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .procnames = clnt_handshake_procs,
};


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
        char snap_volname[NAME_MAX];
        glfs_t *fs;
};
typedef struct snap_dirent snap_dirent_t;

struct svs_private {
        snap_dirent_t           *dirents;
        int                     num_snaps;
        char                    *volname;
        struct list_head        snaplist;
        pthread_mutex_t         snaplist_lock;
        uint32_t                is_snaplist_done;
        gf_timer_t              *snap_timer;
        pthread_attr_t          thr_attr;
};
typedef struct svs_private svs_private_t;

glfs_t *
svs_intialise_snapshot_volume (xlator_t *this, const char *name);

snap_dirent_t *
svs_get_snap_dirent (xlator_t *this, const char *name);

#endif /* __SNAP_VIEW_H__ */
