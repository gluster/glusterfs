/*
 Copyright (c) 2021 Pavilion Data Systems, Inc. <https://pavilion.io>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */
#ifndef _TIERFS_H
#define _TIERFS_H

#include <semaphore.h>
#include <glusterfs/xlator.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/call-stub.h>
#include <glusterfs/syncop.h>
#include <glusterfs/compat-errno.h>
#include "tier.h"
#include "tier-mem-types.h"

struct _tierfs {
    gf_lock_t lock; /* lock for controlling access   */
    xlator_t *xl;   /* xlator                        */
    void *handle;   /* handle returned from dlopen   */
    /* Ideally the configuration of cold tier should be here */
    char *mount_point; /* for now, this is the mount point   */
    pthread_t download_thr;
    struct list_head dl_list;
    uint64_t block_size;          /* block size to compare for local writes */
    int32_t nreqs;                /* num requests active */
    int32_t dl_trigger_threshold; /* default is 20 */
    int bmdirfd; /* fd for bitmap dir, need to perform fsync for consistency */
    bool dl_thread_needed; /* optional */
};
typedef struct _tierfs tierfs_t;

typedef struct tierfs_inode {
    struct list_head active_list;
    gf_lock_t bmlock;
    uint8_t *bitmap;
    uint8_t *bm_inprogress;
    fd_t *localfd;
    char *remotepath;
    gf_atomic_t ref;
    uuid_t gfid;
    uint64_t filesize;
    uint64_t bmsize;
    int32_t block_count;
    int tierfd;
    int bmfd;
    bool full_download; /* set if user asks for the download, would be done in
                           foreground */
    /* set if bitmap file exists when inode is accessed */
    bool bmfile_check_done;
} tierfs_inode_ctx_t;

#endif /* _TIERFS_H */
