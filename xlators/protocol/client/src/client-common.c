/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <glusterfs/dict.h>
#include "client.h"

int32_t
client_cmd_to_gf_cmd(int32_t cmd, int32_t *gf_cmd)
{
    int ret = 0;

    if (cmd == F_GETLK || cmd == F_GETLK64)
        *gf_cmd = GF_LK_GETLK;
    else if (cmd == F_SETLK || cmd == F_SETLK64)
        *gf_cmd = GF_LK_SETLK;
    else if (cmd == F_SETLKW || cmd == F_SETLKW64)
        *gf_cmd = GF_LK_SETLKW;
    else if (cmd == F_RESLK_LCK)
        *gf_cmd = GF_LK_RESLK_LCK;
    else if (cmd == F_RESLK_LCKW)
        *gf_cmd = GF_LK_RESLK_LCKW;
    else if (cmd == F_RESLK_UNLCK)
        *gf_cmd = GF_LK_RESLK_UNLCK;
    else if (cmd == F_GETLK_FD)
        *gf_cmd = GF_LK_GETLK_FD;
    else
        ret = -1;
    return ret;
}

/* New PRE and POST functions */

int
client_post_common_iatt(gfx_common_iatt_rsp *rsp, struct iatt *iatt,
                        dict_t **xdata)
{
    if (-1 != rsp->op_ret) {
        gfx_stat_to_iattx(&rsp->stat, iatt);
    }

    return xdr_to_dict(&rsp->xdata, xdata);
}

int
client_post_common_2iatt(gfx_common_2iatt_rsp *rsp, struct iatt *iatt,
                         struct iatt *iatt2, dict_t **xdata)
{
    if (-1 != rsp->op_ret) {
        gfx_stat_to_iattx(&rsp->prestat, iatt);
        gfx_stat_to_iattx(&rsp->poststat, iatt2);
    }

    return xdr_to_dict(&rsp->xdata, xdata);
}

int
client_post_common_3iatt(gfx_common_3iatt_rsp *rsp, struct iatt *iatt,
                         struct iatt *iatt2, struct iatt *iatt3, dict_t **xdata)
{
    if (-1 != rsp->op_ret) {
        gfx_stat_to_iattx(&rsp->stat, iatt);
        gfx_stat_to_iattx(&rsp->preparent, iatt2);
        gfx_stat_to_iattx(&rsp->postparent, iatt3);
    }

    return xdr_to_dict(&rsp->xdata, xdata);
}

int
client_post_common_dict(gfx_common_dict_rsp *rsp, dict_t **dict, dict_t **xdata)
{
    int ret = 0;
    ret = xdr_to_dict(&rsp->dict, dict);
    if (ret)
        gf_msg_debug(THIS->name, EINVAL,
                     "while decoding found empty dictionary");
    xdr_to_dict(&rsp->xdata, xdata);

    return ret;
}

int
client_post_readv_v2(gfx_read_rsp *rsp, struct iobref **iobref,
                     struct iobref *rsp_iobref, struct iatt *stat,
                     struct iovec *vector, struct iovec *rsp_vector,
                     int *rspcount, dict_t **xdata)
{
    int ret = -1;

    if (rsp->op_ret != -1) {
        *iobref = rsp_iobref;
        gfx_stat_to_iattx(&rsp->stat, stat);

        vector[0].iov_len = rsp->op_ret;
        if (rsp->op_ret > 0)
            vector[0].iov_base = rsp_vector->iov_base;
        *rspcount = 1;
    }

    ret = xdr_to_dict(&rsp->xdata, xdata);

#ifdef GF_TESTING_IO_XDATA
    dict_dump_to_log(xdata);
#endif
    return ret;
}

int
client_pre_stat_v2(gfx_stat_req *req, loc_t *loc, dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->inode))
        goto out;

    if (!gf_uuid_is_null(loc->inode->gfid))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_readlink_v2(gfx_readlink_req *req, loc_t *loc, size_t size,
                       dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->inode))
        goto out;

    if (!gf_uuid_is_null(loc->inode->gfid))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);
    req->size = size;
    dict_to_xdr(xdata, &req->xdata);
    return 0;
out:
    return -op_errno;
}

int
client_pre_mknod_v2(gfx_mknod_req *req, loc_t *loc, mode_t mode, dev_t rdev,
                    mode_t umask, dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->parent))
        goto out;

    if (!gf_uuid_is_null(loc->parent->gfid))
        memcpy(req->pargfid, loc->parent->gfid, 16);
    else
        memcpy(req->pargfid, loc->pargfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->pargfid)),
                                  out, op_errno, EINVAL);
    req->bname = (char *)loc->name;
    req->mode = mode;
    req->dev = rdev;
    req->umask = umask;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_mkdir_v2(gfx_mkdir_req *req, loc_t *loc, mode_t mode, mode_t umask,
                    dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->parent))
        goto out;

    if (!gf_uuid_is_null(loc->parent->gfid))
        memcpy(req->pargfid, loc->parent->gfid, 16);
    else
        memcpy(req->pargfid, loc->pargfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->pargfid)),
                                  out, op_errno, EINVAL);

    req->bname = (char *)loc->name;
    req->mode = mode;
    req->umask = umask;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_unlink_v2(gfx_unlink_req *req, loc_t *loc, int32_t flags,
                     dict_t *xdata)
{
    int op_errno = 0;

    if (!(loc && loc->parent))
        goto out;

    if (!gf_uuid_is_null(loc->parent->gfid))
        memcpy(req->pargfid, loc->parent->gfid, 16);
    else
        memcpy(req->pargfid, loc->pargfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->pargfid)),
                                  out, op_errno, EINVAL);
    req->bname = (char *)loc->name;
    req->xflags = flags;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_rmdir_v2(gfx_rmdir_req *req, loc_t *loc, int32_t flags,
                    dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->parent))
        goto out;

    if (!gf_uuid_is_null(loc->parent->gfid))
        memcpy(req->pargfid, loc->parent->gfid, 16);
    else
        memcpy(req->pargfid, loc->pargfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->pargfid)),
                                  out, op_errno, EINVAL);
    req->bname = (char *)loc->name;
    req->xflags = flags;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_symlink_v2(gfx_symlink_req *req, loc_t *loc, const char *linkname,
                      mode_t umask, dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->parent))
        goto out;

    if (!gf_uuid_is_null(loc->parent->gfid))
        memcpy(req->pargfid, loc->parent->gfid, 16);
    else
        memcpy(req->pargfid, loc->pargfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->pargfid)),
                                  out, op_errno, EINVAL);
    req->linkname = (char *)linkname;
    req->bname = (char *)loc->name;
    req->umask = umask;

    dict_to_xdr(xdata, &req->xdata);
    return 0;
out:
    return -op_errno;
}

int
client_pre_rename_v2(gfx_rename_req *req, loc_t *oldloc, loc_t *newloc,
                     dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(oldloc && newloc && oldloc->parent && newloc->parent))
        goto out;

    if (!gf_uuid_is_null(oldloc->parent->gfid))
        memcpy(req->oldgfid, oldloc->parent->gfid, 16);
    else
        memcpy(req->oldgfid, oldloc->pargfid, 16);

    if (!gf_uuid_is_null(newloc->parent->gfid))
        memcpy(req->newgfid, newloc->parent->gfid, 16);
    else
        memcpy(req->newgfid, newloc->pargfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->oldgfid)),
                                  out, op_errno, EINVAL);
    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->newgfid)),
                                  out, op_errno, EINVAL);
    req->oldbname = (char *)oldloc->name;
    req->newbname = (char *)newloc->name;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_link_v2(gfx_link_req *req, loc_t *oldloc, loc_t *newloc,
                   dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(oldloc && oldloc->inode && newloc && newloc->parent))
        goto out;

    if (!gf_uuid_is_null(oldloc->inode->gfid))
        memcpy(req->oldgfid, oldloc->inode->gfid, 16);
    else
        memcpy(req->oldgfid, oldloc->gfid, 16);

    if (!gf_uuid_is_null(newloc->parent->gfid))
        memcpy(req->newgfid, newloc->parent->gfid, 16);
    else
        memcpy(req->newgfid, newloc->pargfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->oldgfid)),
                                  out, op_errno, EINVAL);
    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->newgfid)),
                                  out, op_errno, EINVAL);
    req->newbname = (char *)newloc->name;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_truncate_v2(gfx_truncate_req *req, loc_t *loc, off_t offset,
                       dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->inode))
        goto out;

    if (!gf_uuid_is_null(loc->inode->gfid))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);
    req->offset = offset;

    dict_to_xdr(xdata, &req->xdata);
    return 0;
out:
    return -op_errno;
}

int
client_pre_open_v2(gfx_open_req *req, loc_t *loc, fd_t *fd, int32_t flags,
                   dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->inode))
        goto out;

    if (!gf_uuid_is_null(loc->inode->gfid))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);
    req->flags = gf_flags_from_flags(flags);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_readv_v2(xlator_t *this, gfx_read_req *req, fd_t *fd, size_t size,
                    off_t offset, int32_t flags, dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = ESTALE;

    CLIENT_GET_REMOTE_FD(this, fd, FALLBACK_TO_ANON_FD, remote_fd, op_errno,
                         GFS3_OP_READ, out);

    req->size = size;
    req->offset = offset;
    req->fd = remote_fd;
    req->flag = flags;

    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_writev_v2(xlator_t *this, gfx_write_req *req, fd_t *fd, size_t size,
                     off_t offset, int32_t flags, dict_t **xdata)
{
    int64_t remote_fd = -1;
    int op_errno = ESTALE;

    CLIENT_GET_REMOTE_FD(this, fd, FALLBACK_TO_ANON_FD, remote_fd, op_errno,
                         GFS3_OP_WRITE, out);

    req->size = size;
    req->offset = offset;
    req->fd = remote_fd;
    req->flag = flags;

    memcpy(req->gfid, fd->inode->gfid, 16);

#ifdef GF_TESTING_IO_XDATA
    if (!*xdata)
        *xdata = dict_new();

    ret = dict_set_str(*xdata, "testing-the-xdata-key",
                       "testing-the-xdata-value");
#endif

    dict_to_xdr(*xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_copy_file_range_v2(xlator_t *this, gfx_copy_file_range_req *req,
                              fd_t *fd_in, off64_t off_in, fd_t *fd_out,
                              off64_t off_out, size_t size, int32_t flags,
                              dict_t **xdata)
{
    int64_t remote_fd_in = -1;
    int64_t remote_fd_out = -1;
    int op_errno = ESTALE;

    CLIENT_GET_REMOTE_FD(this, fd_in, FALLBACK_TO_ANON_FD, remote_fd_in,
                         op_errno, GFS3_OP_COPY_FILE_RANGE, out);

    CLIENT_GET_REMOTE_FD(this, fd_out, FALLBACK_TO_ANON_FD, remote_fd_out,
                         op_errno, GFS3_OP_COPY_FILE_RANGE, out);
    req->size = size;
    req->off_in = off_in;
    req->off_out = off_out;
    req->fd_in = remote_fd_in;
    req->fd_out = remote_fd_out;
    req->flag = flags;

    memcpy(req->gfid1, fd_in->inode->gfid, 16);
    memcpy(req->gfid2, fd_out->inode->gfid, 16);

    dict_to_xdr(*xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_statfs_v2(gfx_statfs_req *req, loc_t *loc, dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!loc)
        goto out;

    if (loc->inode) {
        if (!gf_uuid_is_null(loc->inode->gfid))
            memcpy(req->gfid, loc->inode->gfid, 16);
        else
            memcpy(req->gfid, loc->gfid, 16);
    } else {
        req->gfid[15] = 1;
    }

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_flush_v2(xlator_t *this, gfx_flush_req *req, fd_t *fd, dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = ESTALE;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_FLUSH, out);

    req->fd = remote_fd;
    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_fsync_v2(xlator_t *this, gfx_fsync_req *req, fd_t *fd, int32_t flags,
                    dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = 0;

    CLIENT_GET_REMOTE_FD(this, fd, FALLBACK_TO_ANON_FD, remote_fd, op_errno,
                         GFS3_OP_FSYNC, out);

    req->fd = remote_fd;
    req->data = flags;
    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_setxattr_v2(gfx_setxattr_req *req, loc_t *loc, dict_t *xattr,
                       int32_t flags, dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->inode))
        goto out;

    if (!gf_uuid_is_null(loc->inode->gfid))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);
    if (xattr) {
        dict_to_xdr(xattr, &req->dict);
    }

    req->flags = flags;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_getxattr_v2(gfx_getxattr_req *req, loc_t *loc, const char *name,
                       dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!loc) {
        op_errno = EINVAL;
        goto out;
    }

    if (loc->inode && !gf_uuid_is_null(loc->inode->gfid))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);
    req->namelen = 1; /* Use it as a flag */

    req->name = (char *)name;
    if (!req->name) {
        req->name = "";
        req->namelen = 0;
    }

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_removexattr_v2(gfx_removexattr_req *req, loc_t *loc,
                          const char *name, dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->inode))
        goto out;

    if (!gf_uuid_is_null(loc->inode->gfid))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);
    req->name = (char *)name;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_opendir_v2(gfx_opendir_req *req, loc_t *loc, fd_t *fd, dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->inode))
        goto out;

    if (!gf_uuid_is_null(loc->inode->gfid))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_fsyncdir_v2(xlator_t *this, gfx_fsyncdir_req *req, fd_t *fd,
                       int32_t flags, dict_t *xdata)
{
    int32_t op_errno = ESTALE;
    int64_t remote_fd = -1;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_FSYNCDIR, out);

    req->fd = remote_fd;
    req->data = flags;
    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_access_v2(gfx_access_req *req, loc_t *loc, int32_t mask,
                     dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->inode))
        goto out;

    if (!gf_uuid_is_null(loc->inode->gfid))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);
    req->mask = mask;

    dict_to_xdr(xdata, &req->xdata);
    return 0;
out:
    return -op_errno;
}

int
client_pre_create_v2(gfx_create_req *req, loc_t *loc, fd_t *fd, mode_t mode,
                     int32_t flags, mode_t umask, dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->parent))
        goto out;

    if (!gf_uuid_is_null(loc->parent->gfid))
        memcpy(req->pargfid, loc->parent->gfid, 16);
    else
        memcpy(req->pargfid, loc->pargfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->pargfid)),
                                  out, op_errno, EINVAL);
    req->bname = (char *)loc->name;
    req->mode = mode;
    req->flags = gf_flags_from_flags(flags);
    req->umask = umask;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_ftruncate_v2(xlator_t *this, gfx_ftruncate_req *req, fd_t *fd,
                        off_t offset, dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = EINVAL;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_FTRUNCATE, out);

    req->offset = offset;
    req->fd = remote_fd;
    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);
    return 0;
out:
    return -op_errno;
}

int
client_pre_fstat_v2(xlator_t *this, gfx_fstat_req *req, fd_t *fd, dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = ESTALE;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_FSTAT, out);

    req->fd = remote_fd;
    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_lk_v2(xlator_t *this, gfx_lk_req *req, int32_t cmd,
                 struct gf_flock *flock, fd_t *fd, dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = ESTALE;
    int32_t gf_cmd = 0;
    int32_t gf_type = 0;
    int ret = 0;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_LK, out);

    ret = client_cmd_to_gf_cmd(cmd, &gf_cmd);
    if (ret) {
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_WARNING, EINVAL, PC_MSG_UNKNOWN_CMD,
                "gf_cmd=%d", gf_cmd, NULL);
        goto out;
    }

    switch (flock->l_type) {
        case F_RDLCK:
            gf_type = GF_LK_F_RDLCK;
            break;
        case F_WRLCK:
            gf_type = GF_LK_F_WRLCK;
            break;
        case F_UNLCK:
            gf_type = GF_LK_F_UNLCK;
            break;
    }

    req->fd = remote_fd;
    req->cmd = gf_cmd;
    req->type = gf_type;
    gf_proto_flock_from_flock(&req->flock, flock);

    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_lookup_v2(xlator_t *this, gfx_lookup_req *req, loc_t *loc,
                     dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->inode))
        goto out;

    if ((loc->parent) && (!gf_uuid_is_null(loc->parent->gfid)))
        memcpy(req->pargfid, loc->parent->gfid, 16);
    else
        memcpy(req->pargfid, loc->pargfid, 16);

    if ((loc->inode) && (!gf_uuid_is_null(loc->inode->gfid)))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    if (loc->name)
        req->bname = (char *)loc->name;
    else
        req->bname = "";

    if (xdata) {
        dict_to_xdr(xdata, &req->xdata);
    }
    return 0;
out:
    return -op_errno;
}

int
client_pre_readdir_v2(xlator_t *this, gfx_readdir_req *req, fd_t *fd,
                      size_t size, off_t offset, dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = ESTALE;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_READDIR, out);

    req->size = size;
    req->offset = offset;
    req->fd = remote_fd;

    memcpy(req->gfid, fd->inode->gfid, 16);
    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_inodelk_v2(gfx_inodelk_req *req, loc_t *loc, int cmd,
                      struct gf_flock *flock, const char *volume, dict_t *xdata)
{
    int op_errno = ESTALE;
    int32_t gf_cmd = 0;
    int32_t gf_type = 0;

    if (!(loc && loc->inode))
        goto out;

    if (!gf_uuid_is_null(loc->gfid))
        memcpy(req->gfid, loc->gfid, 16);
    else
        memcpy(req->gfid, loc->inode->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);
    if (cmd == F_GETLK || cmd == F_GETLK64)
        gf_cmd = GF_LK_GETLK;
    else if (cmd == F_SETLK || cmd == F_SETLK64)
        gf_cmd = GF_LK_SETLK;
    else if (cmd == F_SETLKW || cmd == F_SETLKW64)
        gf_cmd = GF_LK_SETLKW;
    else {
        gf_smsg(THIS->name, GF_LOG_WARNING, EINVAL, PC_MSG_UNKNOWN_CMD,
                "gf_cmd=%d", gf_cmd, NULL);
        op_errno = EINVAL;
        goto out;
    }

    switch (flock->l_type) {
        case F_RDLCK:
            gf_type = GF_LK_F_RDLCK;
            break;
        case F_WRLCK:
            gf_type = GF_LK_F_WRLCK;
            break;
        case F_UNLCK:
            gf_type = GF_LK_F_UNLCK;
            break;
    }

    req->volume = (char *)volume;
    req->cmd = gf_cmd;
    req->type = gf_type;
    gf_proto_flock_from_flock(&req->flock, flock);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_finodelk_v2(xlator_t *this, gfx_finodelk_req *req, fd_t *fd, int cmd,
                       struct gf_flock *flock, const char *volume,
                       dict_t *xdata)
{
    int op_errno = ESTALE;
    int64_t remote_fd = -1;
    int32_t gf_type = 0;
    int32_t gf_cmd = 0;

    CLIENT_GET_REMOTE_FD(this, fd, FALLBACK_TO_ANON_FD, remote_fd, op_errno,
                         GFS3_OP_FINODELK, out);

    if (cmd == F_GETLK || cmd == F_GETLK64)
        gf_cmd = GF_LK_GETLK;
    else if (cmd == F_SETLK || cmd == F_SETLK64)
        gf_cmd = GF_LK_SETLK;
    else if (cmd == F_SETLKW || cmd == F_SETLKW64)
        gf_cmd = GF_LK_SETLKW;
    else {
        gf_smsg(this->name, GF_LOG_WARNING, EINVAL, PC_MSG_UNKNOWN_CMD,
                "gf_cmd=%d", gf_cmd, NULL);
        goto out;
    }

    switch (flock->l_type) {
        case F_RDLCK:
            gf_type = GF_LK_F_RDLCK;
            break;
        case F_WRLCK:
            gf_type = GF_LK_F_WRLCK;
            break;
        case F_UNLCK:
            gf_type = GF_LK_F_UNLCK;
            break;
    }

    req->volume = (char *)volume;
    req->fd = remote_fd;
    req->cmd = gf_cmd;
    req->type = gf_type;
    gf_proto_flock_from_flock(&req->flock, flock);
    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);
    return 0;
out:
    return -op_errno;
}

int
client_pre_entrylk_v2(gfx_entrylk_req *req, loc_t *loc, entrylk_cmd cmd_entrylk,
                      entrylk_type type, const char *volume,
                      const char *basename, dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->inode))
        goto out;

    if (!gf_uuid_is_null(loc->gfid))
        memcpy(req->gfid, loc->gfid, 16);
    else
        memcpy(req->gfid, loc->inode->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);
    req->cmd = cmd_entrylk;
    req->type = type;
    req->volume = (char *)volume;
    req->name = "";
    if (basename) {
        req->name = (char *)basename;
        req->namelen = 1;
    }

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_fentrylk_v2(xlator_t *this, gfx_fentrylk_req *req, fd_t *fd,
                       entrylk_cmd cmd_entrylk, entrylk_type type,
                       const char *volume, const char *basename, dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = ESTALE;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_FENTRYLK, out);

    req->fd = remote_fd;
    req->cmd = cmd_entrylk;
    req->type = type;
    req->volume = (char *)volume;
    req->name = "";
    if (basename) {
        req->name = (char *)basename;
        req->namelen = 1;
    }
    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_xattrop_v2(gfx_xattrop_req *req, loc_t *loc, dict_t *xattr,
                      int32_t flags, dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->inode))
        goto out;

    if (!gf_uuid_is_null(loc->inode->gfid))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);
    dict_to_xdr(xattr, &req->dict);

    req->flags = flags;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_fxattrop_v2(xlator_t *this, gfx_fxattrop_req *req, fd_t *fd,
                       dict_t *xattr, int32_t flags, dict_t *xdata)
{
    int op_errno = ESTALE;
    int64_t remote_fd = -1;

    CLIENT_GET_REMOTE_FD(this, fd, FALLBACK_TO_ANON_FD, remote_fd, op_errno,
                         GFS3_OP_FXATTROP, out);

    req->fd = remote_fd;
    req->flags = flags;
    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xattr, &req->dict);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_fgetxattr_v2(xlator_t *this, gfx_fgetxattr_req *req, fd_t *fd,
                        const char *name, dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = ESTALE;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_FGETXATTR, out);

    req->namelen = 1; /* Use it as a flag */
    req->fd = remote_fd;
    req->name = (char *)name;
    if (!req->name) {
        req->name = "";
        req->namelen = 0;
    }
    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_fsetxattr_v2(xlator_t *this, gfx_fsetxattr_req *req, fd_t *fd,
                        int32_t flags, dict_t *xattr, dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = ESTALE;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_FSETXATTR, out);

    req->fd = remote_fd;
    req->flags = flags;
    memcpy(req->gfid, fd->inode->gfid, 16);

    if (xattr) {
        dict_to_xdr(xattr, &req->dict);
    }

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_rchecksum_v2(xlator_t *this, gfx_rchecksum_req *req, fd_t *fd,
                        int32_t len, off_t offset, dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = ESTALE;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_RCHECKSUM, out);

    req->len = len;
    req->offset = offset;
    req->fd = remote_fd;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_setattr_v2(gfx_setattr_req *req, loc_t *loc, int32_t valid,
                      struct iatt *stbuf, dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->inode))
        return -op_errno;

    if (!gf_uuid_is_null(loc->inode->gfid))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);

    req->valid = valid;
    gfx_stat_from_iattx(&req->stbuf, stbuf);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_fsetattr_v2(xlator_t *this, gfx_fsetattr_req *req, fd_t *fd,
                       int32_t valid, struct iatt *stbuf, dict_t *xdata)
{
    int op_errno = ESTALE;
    int64_t remote_fd = -1;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_FSETATTR, out);

    memcpy(req->gfid, fd->inode->gfid, 16);
    req->fd = remote_fd;
    req->valid = valid;
    gfx_stat_from_iattx(&req->stbuf, stbuf);

    dict_to_xdr(xdata, &req->xdata);
    return 0;
out:
    return -op_errno;
}

int
client_pre_readdirp_v2(xlator_t *this, gfx_readdirp_req *req, fd_t *fd,
                       size_t size, off_t offset, dict_t *xdata)
{
    int op_errno = ESTALE;
    int64_t remote_fd = -1;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_READDIRP, out);

    req->size = size;
    req->offset = offset;
    req->fd = remote_fd;
    memcpy(req->gfid, fd->inode->gfid, 16);

    /* dict itself is 'xdata' here */
    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_fremovexattr_v2(xlator_t *this, gfx_fremovexattr_req *req, fd_t *fd,
                           const char *name, dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = ESTALE;

    if (!(fd && fd->inode))
        goto out;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_FREMOVEXATTR, out);

    memcpy(req->gfid, fd->inode->gfid, 16);
    req->name = (char *)name;
    req->fd = remote_fd;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_fallocate_v2(xlator_t *this, gfx_fallocate_req *req, fd_t *fd,
                        int32_t flags, off_t offset, size_t size, dict_t *xdata)
{
    int op_errno = ESTALE;
    int64_t remote_fd = -1;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_FALLOCATE, out);

    req->fd = remote_fd;
    req->flags = flags;
    req->offset = offset;
    req->size = size;
    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);
    return 0;
out:
    return -op_errno;
}

int
client_pre_discard_v2(xlator_t *this, gfx_discard_req *req, fd_t *fd,
                      off_t offset, size_t size, dict_t *xdata)
{
    int op_errno = ESTALE;
    int64_t remote_fd = -1;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_DISCARD, out);

    req->fd = remote_fd;
    req->offset = offset;
    req->size = size;
    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);
    return 0;
out:
    return -op_errno;
}

int
client_pre_zerofill_v2(xlator_t *this, gfx_zerofill_req *req, fd_t *fd,
                       off_t offset, size_t size, dict_t *xdata)
{
    int op_errno = ESTALE;
    int64_t remote_fd = -1;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_ZEROFILL, out);

    req->fd = remote_fd;
    req->offset = offset;
    req->size = size;
    memcpy(req->gfid, fd->inode->gfid, 16);

    dict_to_xdr(xdata, &req->xdata);
    return 0;
out:
    return -op_errno;
}

int
client_pre_ipc_v2(gfx_ipc_req *req, int32_t cmd, dict_t *xdata)
{
    req->op = cmd;

    dict_to_xdr(xdata, &req->xdata);
    return 0;
}

int
client_pre_seek_v2(xlator_t *this, gfx_seek_req *req, fd_t *fd, off_t offset,
                   gf_seek_what_t what, dict_t *xdata)
{
    int64_t remote_fd = -1;
    int op_errno = ESTALE;

    CLIENT_GET_REMOTE_FD(this, fd, DEFAULT_REMOTE_FD, remote_fd, op_errno,
                         GFS3_OP_SEEK, out);

    memcpy(req->gfid, fd->inode->gfid, 16);
    req->fd = remote_fd;
    req->offset = offset;
    req->what = what;

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_pre_lease_v2(gfx_lease_req *req, loc_t *loc, struct gf_lease *lease,
                    dict_t *xdata)
{
    int op_errno = 0;

    if (!(loc && loc->inode))
        goto out;

    if (!gf_uuid_is_null(loc->inode->gfid))
        memcpy(req->gfid, loc->inode->gfid, 16);
    else
        memcpy(req->gfid, loc->gfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->gfid)), out,
                                  op_errno, EINVAL);

    gf_proto_lease_from_lease(&req->lease, lease);

    dict_to_xdr(xdata, &req->xdata);
out:
    return -op_errno;
}

int
client_pre_put_v2(gfx_put_req *req, loc_t *loc, mode_t mode, mode_t umask,
                  int32_t flags, size_t size, off_t offset, dict_t *xattr,
                  dict_t *xdata)
{
    int op_errno = ESTALE;

    if (!(loc && loc->parent))
        goto out;

    if (!gf_uuid_is_null(loc->parent->gfid))
        memcpy(req->pargfid, loc->parent->gfid, 16);
    else
        memcpy(req->pargfid, loc->pargfid, 16);

    GF_ASSERT_AND_GOTO_WITH_ERROR(!gf_uuid_is_null(*((uuid_t *)req->pargfid)),
                                  out, op_errno, EINVAL);
    req->bname = (char *)loc->name;
    req->mode = mode;
    req->umask = umask;
    req->flag = gf_flags_from_flags(flags);
    req->size = size;
    req->offset = offset;

    if (xattr)
        dict_to_xdr(xattr, &req->xattr);

    dict_to_xdr(xdata, &req->xdata);

    return 0;
out:
    return -op_errno;
}

int
client_post_create_v2(gfx_create_rsp *rsp, struct iatt *stbuf,
                      struct iatt *preparent, struct iatt *postparent,
                      clnt_local_t *local, dict_t **xdata)
{
    if (-1 != rsp->op_ret) {
        gfx_stat_to_iattx(&rsp->stat, stbuf);

        gfx_stat_to_iattx(&rsp->preparent, preparent);
        gfx_stat_to_iattx(&rsp->postparent, postparent);
        gf_uuid_copy(local->loc.gfid, stbuf->ia_gfid);
    }
    return xdr_to_dict(&rsp->xdata, xdata);
}

int
client_post_lease_v2(gfx_lease_rsp *rsp, struct gf_lease *lease, dict_t **xdata)
{
    if (rsp->op_ret >= 0) {
        gf_proto_lease_to_lease(&rsp->lease, lease);
    }

    return xdr_to_dict(&rsp->xdata, xdata);
}

int
client_post_lk_v2(gfx_lk_rsp *rsp, struct gf_flock *lock, dict_t **xdata)
{
    if (rsp->op_ret >= 0) {
        gf_proto_flock_to_flock(&rsp->flock, lock);
    }
    return xdr_to_dict(&rsp->xdata, xdata);
}

int
client_post_readdir_v2(xlator_t *this, gfx_readdir_rsp *rsp,
                       gf_dirent_t *entries, dict_t **xdata)
{
    if (rsp->op_ret > 0) {
        unserialize_rsp_dirent_v2(this, rsp, entries);
    }
    return xdr_to_dict(&rsp->xdata, xdata);
}

int
client_post_readdirp_v2(xlator_t *this, gfx_readdirp_rsp *rsp, fd_t *fd,
                        gf_dirent_t *entries, dict_t **xdata)
{
    if (rsp->op_ret > 0) {
        unserialize_rsp_direntp_v2(this, fd, rsp, entries);
    }
    return xdr_to_dict(&rsp->xdata, xdata);
}

int
client_post_rename_v2(gfx_rename_rsp *rsp, struct iatt *stbuf,
                      struct iatt *preoldparent, struct iatt *postoldparent,
                      struct iatt *prenewparent, struct iatt *postnewparent,
                      dict_t **xdata)
{
    if (-1 != rsp->op_ret) {
        gfx_stat_to_iattx(&rsp->stat, stbuf);

        gfx_stat_to_iattx(&rsp->preoldparent, preoldparent);
        gfx_stat_to_iattx(&rsp->postoldparent, postoldparent);

        gfx_stat_to_iattx(&rsp->prenewparent, prenewparent);
        gfx_stat_to_iattx(&rsp->postnewparent, postnewparent);
    }

    return xdr_to_dict(&rsp->xdata, xdata);
}

void
set_fd_reopen_status(xlator_t *this, dict_t *xdata,
                     enum gf_fd_reopen_status fd_reopen_status)
{
    clnt_conf_t *conf = NULL;

    conf = this->private;
    if (!conf) {
        gf_msg_debug(this->name, ENOMEM, "Failed to get client conf");
        return;
    }

    if (!conf->strict_locks)
        fd_reopen_status = FD_REOPEN_ALLOWED;

    if (dict_set_int32(xdata, "fd-reopen-status", fd_reopen_status))
        gf_smsg(this->name, GF_LOG_WARNING, ENOMEM, PC_MSG_NO_MEM, NULL);

    return;
}
