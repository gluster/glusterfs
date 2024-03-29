/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <glusterfs/defaults.h>

#include "meta-mem-types.h"
#include "meta.h"

meta_fd_t *
meta_fd_get(fd_t *fd, xlator_t *this)
{
    meta_fd_t *meta_fd = NULL;

    LOCK(&fd->lock);
    {
        meta_fd = __fd_ctx_get_ptr(fd, this);
        if (!meta_fd) {
            meta_fd = GF_CALLOC(1, sizeof(*meta_fd), gf_meta_mt_fd_t);
            if (!meta_fd)
                goto unlock;
            __fd_ctx_set(fd, this, (long)meta_fd);
        }
    }
unlock:
    UNLOCK(&fd->lock);

    return meta_fd;
}

int
meta_fd_release(fd_t *fd, xlator_t *this)
{
    meta_fd_t *meta_fd = NULL;
    int i = 0;

    meta_fd = fd_ctx_del_ptr(fd, this);

    if (meta_fd) {
        if (meta_fd->dirents) {
            for (i = 0; i < meta_fd->size; i++)
                GF_FREE((void *)meta_fd->dirents[i].name);
            GF_FREE(meta_fd->dirents);
        }
        GF_FREE(meta_fd->data);
        GF_FREE(meta_fd);
    }
    return 0;
}

struct meta_ops *
meta_ops_get(inode_t *inode, xlator_t *this)
{
    struct meta_ops *ops = NULL;
    uint64_t value = 0;

    inode_ctx_get2(inode, this, NULL, &value);

    ops = (void *)(uintptr_t)value;

    return ops;
}

struct xlator_fops *
meta_fops_get(inode_t *inode, xlator_t *this)
{
    struct meta_ops *ops = NULL;

    ops = meta_ops_get(inode, this);
    if (!ops)
        return default_fops;

    return &ops->fops;
}

int
meta_ops_set(inode_t *inode, xlator_t *this, struct meta_ops *ops)
{
    uint64_t value = 0;
    int ret = 0;

    meta_defaults_init(&ops->fops);

    value = (long)ops;

    ret = inode_ctx_set2(inode, this, NULL, &value);

    return ret;
}

void *
meta_ctx_get(inode_t *inode, xlator_t *this)
{
    void *ctx = NULL;
    uint64_t value = 0;

    inode_ctx_get2(inode, this, &value, 0);

    ctx = (void *)(uintptr_t)value;

    return ctx;
}

int
meta_ctx_set(inode_t *inode, xlator_t *this, void *ctx)
{
    uint64_t value = 0;
    int ret = 0;

    value = (long)ctx;

    ret = inode_ctx_set2(inode, this, &value, 0);

    return ret;
}

void
meta_local_cleanup(meta_local_t *local)
{
    if (!local)
        return;

    if (local->xdata)
        dict_unref(local->xdata);

    GF_FREE(local);
    return;
}

static meta_local_t *
meta_local(call_frame_t *frame)
{
    meta_local_t *local = NULL;

    local = frame->local;
    if (!local)
        local = frame->local = GF_CALLOC(1, sizeof(*local), gf_meta_mt_local_t);
    return local;
}

dict_t *
meta_direct_io_mode(dict_t *xdata, call_frame_t *frame)
{
    meta_local_t *local = NULL;

    if (!xdata) {
        local = meta_local(frame);
        if (!local)
            return NULL;
        xdata = local->xdata = dict_new();
        if (!xdata)
            return NULL;
    }

    if (dict_set_int8(xdata, "direct-io-mode", 1) != 0)
        return NULL;

    return xdata;
}

static void
meta_uuid_copy(uuid_t dst, uuid_t src)
{
    if (gf_uuid_is_null(src))
        gf_uuid_generate(dst);
    else
        gf_uuid_copy(dst, src);
}

static void
default_meta_iatt_fill(struct iatt *iatt, inode_t *inode, ia_type_t type,
                       gf_boolean_t is_tunable)
{
    struct timespec ts = {
        0,
    };

    iatt->ia_type = type;
    switch (type) {
        case IA_IFDIR:
            iatt->ia_prot = ia_prot_from_st_mode(0555);
            iatt->ia_nlink = 2;
            break;
        case IA_IFLNK:
            iatt->ia_prot = ia_prot_from_st_mode(0777);
            iatt->ia_nlink = 1;
            break;
        default:
            iatt->ia_prot = ia_prot_from_st_mode(is_tunable ? 0644 : 0444);
            iatt->ia_nlink = 1;
            break;
    }
    iatt->ia_uid = 0;
    iatt->ia_gid = 0;
    iatt->ia_size = 0;

    meta_uuid_copy(iatt->ia_gfid, inode->gfid);
    iatt->ia_ino = gfid_to_ino(iatt->ia_gfid);

    timespec_now_realtime(&ts);
    iatt->ia_mtime = iatt->ia_ctime = iatt->ia_atime = ts.tv_sec;
    iatt->ia_mtime_nsec = iatt->ia_ctime_nsec = iatt->ia_atime_nsec =
        ts.tv_nsec;
}

void
meta_iatt_fill(xlator_t *this, struct iatt *iatt, inode_t *inode,
               ia_type_t type)
{
    struct meta_ops *ops = NULL;
    xlator_t *xl = this;

    if (xl == NULL)
        xl = THIS;

    ops = meta_ops_get(inode, xl);
    if (!ops)
        return;

    if (!ops->iatt_fill)
        default_meta_iatt_fill(iatt, inode, type, !!ops->file_write);
    else
        ops->iatt_fill(xl, inode, iatt);
    return;
}

int
meta_inode_discover(call_frame_t *frame, xlator_t *this, loc_t *loc,
                    dict_t *xdata)
{
    struct iatt iatt = {};
    struct iatt postparent = {};

    meta_iatt_fill(this, &iatt, loc->inode, loc->inode->ia_type);

    META_STACK_UNWIND(lookup, frame, 0, 0, loc->inode, &iatt, xdata,
                      &postparent);
    return 0;
}

int
meta_file_fill(xlator_t *this, meta_fd_t *meta_fd, fd_t *fd)
{
    strfd_t *strfd = NULL;
    struct meta_ops *ops = NULL;
    int ret = 0;

    if (meta_fd->data)
        return meta_fd->size;

    strfd = strfd_open();
    if (!strfd)
        return -1;

    ops = meta_ops_get(fd->inode, this);
    if (!ops) {
        strfd_close(strfd);
        return -1;
    }

    if (ops->file_fill)
        ret = ops->file_fill(this, fd->inode, strfd);

    if (ret >= 0) {
        meta_fd->data = strfd->data;
        meta_fd->size = strfd->size;

        strfd->data = NULL;
    }

    strfd_close(strfd);

    return meta_fd->size;
}

int
meta_dir_fill(xlator_t *this, meta_fd_t *meta_fd, struct meta_ops *ops,
              fd_t *fd)
{
    struct meta_dirent *dp = NULL;
    int ret = 0;

    if (meta_fd->dirents)
        return meta_fd->size;

    if (ops->dir_fill)
        ret = ops->dir_fill(this, fd->inode, &dp);

    if (dp) {
        meta_fd->dirents = dp;
        meta_fd->size = ret;
    }

    return meta_fd->size;
}

int
fixed_dirents_len(struct meta_dirent *dirents)
{
    int i = 0;
    struct meta_dirent *dirent = NULL;

    if (!dirents)
        return 0;

    for (dirent = dirents; dirent->name; dirent++)
        i++;

    return i;
}
