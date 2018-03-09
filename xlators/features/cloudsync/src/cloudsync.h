/*
 *   Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */


#ifndef __CLOUDSYNC_H__
#define __CLOUDSYNC_H__

#include "glusterfs.h"
#include "xlator.h"
#include "defaults.h"
#include "syncop.h"
#include "call-stub.h"
#include "cloudsync-common.h"
#include "cloudsync-autogen-fops.h"


#define CS_LOCK_DOMAIN "cs.protect.file.stat"
typedef struct cs_dlstore {
        off_t           off;
        struct iovec   *vector;
        int32_t         count;
        struct iobref  *iobref;
        uint32_t        flags;
} cs_dlstore;

typedef struct cs_inode_ctx {
        gf_cs_obj_state state;
} cs_inode_ctx_t;

cs_local_t *
cs_local_init (xlator_t *this, call_frame_t *frame, loc_t *loc, fd_t *fd,
               glusterfs_fop_t fop);

int
locate_and_execute (call_frame_t *frame);


int32_t
cs_resume_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    dict_t *dict, int32_t flags, dict_t *xdata);

int32_t
cs_inodelk_unlock_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
                       int32_t op_errno, dict_t *xdata);

size_t
cs_write_callback (void *lcurlbuf, size_t size, size_t nitems, void *frame);

void
cs_common_cbk (call_frame_t *frame);

gf_boolean_t
cs_is_file_remote (struct iatt *stbuf, dict_t *xattr);

int32_t
cs_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata);
int
cs_build_loc (loc_t *loc, call_frame_t *frame);

int
cs_blocking_inodelk_cbk (call_frame_t *lock_frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata);

int cs_read_authinfo(xlator_t *this);

int
__cs_inode_ctx_update (xlator_t *this, inode_t *inode, uint64_t val);

int
cs_inode_ctx_reset (xlator_t *this, inode_t *inode);

void
__cs_inode_ctx_get (xlator_t *this, inode_t *inode, cs_inode_ctx_t **ctx);

gf_cs_obj_state
__cs_get_file_state (xlator_t *this, inode_t *inode, cs_inode_ctx_t *ctx);

int
cs_inodelk_unlock (call_frame_t *main_frame);

int
cs_resume_postprocess (xlator_t *this, call_frame_t *frame, inode_t *inode);

int32_t
cs_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata);
int32_t
cs_resume_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    off_t offset, dict_t *xattr_req);
#endif /* __CLOUDSYNC_H__ */

