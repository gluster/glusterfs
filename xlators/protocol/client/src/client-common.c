/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "dict.h"
#include "xlator.h"
#include "rpc-common-xdr.h"
#include "glusterfs3-xdr.h"
#include "glusterfs3.h"
#include "client.h"

/* processing to be done before fops are woudn down */
int
client_pre_stat (xlator_t *this, gfs3_stat_req *req, loc_t *loc,
                 dict_t *xdata)
{
        int            op_errno = ESTALE;

        if (!(loc && loc->inode))
                goto out;

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (req->gfid,  loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_readlink (xlator_t *this, gfs3_readlink_req *req, loc_t *loc,
                     size_t size, dict_t *xdata)
{
        int                op_errno          = ESTALE;

        if (!(loc && loc->inode))
                goto out;

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (req->gfid,  loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);
        req->size = size;
        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);
        return 0;
out:
        return -op_errno;
}

int
client_pre_mknod (xlator_t *this, gfs3_mknod_req *req, loc_t *loc,
                   mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata)
{
        int             op_errno = ESTALE;

        if (!(loc && loc->parent))
                goto out;

        if (!gf_uuid_is_null (loc->parent->gfid))
                memcpy (req->pargfid,  loc->parent->gfid, 16);
        else
                memcpy (req->pargfid, loc->pargfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                   !gf_uuid_is_null (*((uuid_t *)req->pargfid)),
                                   out, op_errno, EINVAL);
        req->bname  = (char *)loc->name;
        req->mode   = mode;
        req->dev    = rdev;
        req->umask = umask;


        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_mkdir (xlator_t *this, gfs3_mkdir_req *req, loc_t *loc,
                   mode_t mode, mode_t umask, dict_t *xdata)
{
        int             op_errno = ESTALE;

        if (!(loc && loc->parent))
                goto out;

        if (!gf_uuid_is_null (loc->parent->gfid))
                memcpy (req->pargfid,  loc->parent->gfid, 16);
        else
                memcpy (req->pargfid, loc->pargfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                   !gf_uuid_is_null (*((uuid_t *)req->pargfid)),
                                   out, op_errno, EINVAL);

        req->bname = (char *)loc->name;
        req->mode  = mode;
        req->umask = umask;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_unlink (xlator_t *this, gfs3_unlink_req *req, loc_t *loc,
                    int32_t flags, dict_t *xdata)
{
        int              op_errno = 0;

        if (!(loc && loc->parent))
                goto out;

        if (!gf_uuid_is_null (loc->parent->gfid))
                memcpy (req->pargfid,  loc->parent->gfid, 16);
        else
                memcpy (req->pargfid, loc->pargfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                   !gf_uuid_is_null (*((uuid_t *)req->pargfid)),
                                   out, op_errno, EINVAL);
        req->bname = (char *)loc->name;
        req->xflags = flags;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_rmdir (xlator_t *this, gfs3_rmdir_req *req, loc_t *loc,
                   int32_t flags, dict_t *xdata)
{
        int             op_errno = ESTALE;

        if (!(loc && loc->parent))
                goto out;

        if (!gf_uuid_is_null (loc->parent->gfid))
                memcpy (req->pargfid,  loc->parent->gfid, 16);
        else
                memcpy (req->pargfid, loc->pargfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                   !gf_uuid_is_null (*((uuid_t *)req->pargfid)),
                                   out, op_errno, EINVAL);
        req->bname = (char *)loc->name;
        req->xflags = flags;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_symlink (xlator_t *this, gfs3_symlink_req *req, loc_t *loc,
                     const char *linkname, mode_t umask, dict_t *xdata)
{
        int               op_errno = ESTALE;

        if (!(loc && loc->parent))
                goto out;

        if (!gf_uuid_is_null (loc->parent->gfid))
                memcpy (req->pargfid,  loc->parent->gfid, 16);
        else
                memcpy (req->pargfid, loc->pargfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                   !gf_uuid_is_null (*((uuid_t *)req->pargfid)),
                                   out, op_errno, EINVAL);
        req->linkname = (char *)linkname;
        req->bname    = (char *)loc->name;
        req->umask = umask;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);
        return 0;
out:
        return -op_errno;
}

int
client_pre_rename (xlator_t *this, gfs3_rename_req *req, loc_t *oldloc,
                    loc_t *newloc, dict_t *xdata)
{
        int              op_errno = ESTALE;

        if (!(oldloc && newloc && oldloc->parent &&
              newloc->parent))
                goto out;

        if (!gf_uuid_is_null (oldloc->parent->gfid))
                memcpy (req->oldgfid,  oldloc->parent->gfid, 16);
        else
                memcpy (req->oldgfid, oldloc->pargfid, 16);

        if (!gf_uuid_is_null (newloc->parent->gfid))
                memcpy (req->newgfid, newloc->parent->gfid, 16);
        else
                memcpy (req->newgfid, newloc->pargfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                   !gf_uuid_is_null (*((uuid_t *)req->oldgfid)),
                                   out, op_errno, EINVAL);
        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                   !gf_uuid_is_null (*((uuid_t *)req->newgfid)),
                                   out, op_errno, EINVAL);
        req->oldbname =  (char *)oldloc->name;
        req->newbname = (char *)newloc->name;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_link (xlator_t *this,
                  gfs3_link_req *req, loc_t *oldloc, loc_t *newloc,
                  dict_t *xdata)
{
        int            op_errno = ESTALE;

        if (!(oldloc && oldloc->inode && newloc &&
              newloc->parent))
                goto out;

        if (!gf_uuid_is_null (oldloc->inode->gfid))
                memcpy (req->oldgfid,  oldloc->inode->gfid, 16);
        else
                memcpy (req->oldgfid, oldloc->gfid, 16);

        if (!gf_uuid_is_null (newloc->parent->gfid))
                memcpy (req->newgfid, newloc->parent->gfid, 16);
        else
                memcpy (req->newgfid, newloc->pargfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                   !gf_uuid_is_null (*((uuid_t *)req->oldgfid)),
                                   out, op_errno, EINVAL);
        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                   !gf_uuid_is_null (*((uuid_t *)req->newgfid)),
                                   out, op_errno, EINVAL);
        req->newbname = (char *)newloc->name;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_truncate (xlator_t *this, gfs3_truncate_req *req,
                     loc_t *loc, off_t offset, dict_t *xdata)
{
        int             op_errno = ESTALE;

        if (!(loc && loc->inode))
                goto out;

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (req->gfid,  loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);
        req->offset = offset;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);
        return 0;
out:
        return -op_errno;
}

int
client_pre_open (xlator_t *this, gfs3_open_req *req, loc_t *loc, fd_t *fd,
                  int32_t flags, dict_t *xdata)
{
        int            op_errno = ESTALE;

        if (!(loc && loc->inode))
                goto out;

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (req->gfid,  loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);
        req->flags = gf_flags_from_flags (flags);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_readv (xlator_t *this, gfs3_read_req *req, fd_t *fd, size_t size,
                  off_t offset, int32_t flags, dict_t *xdata)
{
        int64_t         remote_fd  = -1;
        int             op_errno   = ESTALE;

        CLIENT_GET_REMOTE_FD (this, fd, FALLBACK_TO_ANON_FD,
                              remote_fd, op_errno, out);

        req->size   = size;
        req->offset = offset;
        req->fd     = remote_fd;
        req->flag   = flags;

        memcpy (req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_writev (xlator_t *this, gfs3_write_req *req,
                   fd_t *fd, size_t size, off_t offset, int32_t flags,
                   dict_t **xdata)
{
        int64_t         remote_fd = -1;
        int             op_errno = ESTALE;

        CLIENT_GET_REMOTE_FD (this, fd, FALLBACK_TO_ANON_FD,
                              remote_fd, op_errno, out);

        req->size   = size;
        req->offset = offset;
        req->fd     = remote_fd;
        req->flag   = flags;

        memcpy (req->gfid, fd->inode->gfid, 16);

#ifdef GF_TESTING_IO_XDATA
        if (!*xdata)
                *xdata = dict_new ();

        ret = dict_set_str (*xdata, "testing-the-xdata-key",
                            "testing-the-xdata-value");
#endif

        GF_PROTOCOL_DICT_SERIALIZE (this, *xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_statfs (xlator_t *this, gfs3_statfs_req *req, loc_t *loc,
                   dict_t *xdata)
{
        int            op_errno = ESTALE;

        if (!loc)
                goto out;

        if (loc->inode) {
                if (!gf_uuid_is_null (loc->inode->gfid))
                        memcpy (req->gfid,  loc->inode->gfid, 16);
                else
                        memcpy (req->gfid, loc->gfid, 16);
        } else {
                req->gfid[15] = 1;
        }

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_flush (xlator_t *this, gfs3_flush_req *req, fd_t *fd, dict_t *xdata)
{
        int64_t         remote_fd = -1;
        int             op_errno = ESTALE;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->fd = remote_fd;
        memcpy (req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_fsync (xlator_t *this, gfs3_fsync_req *req, fd_t *fd,
                   int32_t flags, dict_t *xdata)
{
        int64_t         remote_fd = -1;
        int             op_errno  = 0;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->fd   = remote_fd;
        req->data = flags;
        memcpy (req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_setxattr (xlator_t *this, gfs3_setxattr_req *req, loc_t *loc,
                      dict_t *xattr, int32_t flags, dict_t *xdata)
{
        int                op_errno = ESTALE;

        if (!(loc && loc->inode))
                goto out;

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (req->gfid,  loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);
        if (xattr) {
                GF_PROTOCOL_DICT_SERIALIZE (this, xattr,
                                            (&req->dict.dict_val),
                                            req->dict.dict_len,
                                            op_errno, out);
        }

        req->flags = flags;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_getxattr (xlator_t *this, gfs3_getxattr_req *req, loc_t *loc,
                     const char *name, dict_t *xdata)
{
        int                op_errno   = ESTALE;

        if (!loc) {
                op_errno = EINVAL;
                goto out;
        }

        if (loc->inode && !gf_uuid_is_null (loc->inode->gfid))
                memcpy (req->gfid,  loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);
        req->namelen = 1; /* Use it as a flag */

        req->name = (char *)name;
        if (!req->name) {
                req->name = "";
                req->namelen = 0;
        }

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_removexattr (xlator_t *this, gfs3_removexattr_req *req,
                         loc_t *loc, const char *name, dict_t *xdata)
{
        int                   op_errno = ESTALE;

        if (!(loc && loc->inode))
                goto out;

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (req->gfid,  loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);
        req->name = (char *)name;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_opendir (xlator_t *this,
                    gfs3_opendir_req *req, loc_t *loc,
                    fd_t *fd, dict_t *xdata)
{
        int               op_errno = ESTALE;

        if (!(loc && loc->inode))
                goto out;

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (req->gfid,  loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_fsyncdir (xlator_t *this, gfs3_fsyncdir_req *req, fd_t *fd,
                      int32_t flags, dict_t *xdata)
{
        int32_t            op_errno  = ESTALE;
        int64_t            remote_fd = -1;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->fd   = remote_fd;
        req->data = flags;
        memcpy (req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_access (xlator_t *this, gfs3_access_req *req, loc_t *loc,
                   int32_t mask, dict_t *xdata)
{
        int              op_errno = ESTALE;

        if (!(loc && loc->inode))
                goto out;

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (req->gfid,  loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);
        req->mask = mask;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);
        return 0;
out:
        return -op_errno;
}

int
client_pre_create (xlator_t *this, gfs3_create_req *req,
                    loc_t *loc, fd_t *fd, mode_t mode,
                    int32_t flags, mode_t umask, dict_t *xdata)
{
        int              op_errno = ESTALE;

        if (!(loc && loc->parent))
                goto out;

        if (!gf_uuid_is_null (loc->parent->gfid))
                memcpy (req->pargfid,  loc->parent->gfid, 16);
        else
                memcpy (req->pargfid, loc->pargfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                   !gf_uuid_is_null (*((uuid_t *)req->pargfid)),
                                   out, op_errno, EINVAL);
        req->bname = (char *)loc->name;
        req->mode  = mode;
        req->flags = gf_flags_from_flags (flags);
        req->umask = umask;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_ftruncate (xlator_t *this, gfs3_ftruncate_req *req, fd_t *fd,
                       off_t offset, dict_t *xdata)
{
        int64_t             remote_fd = -1;
        int                 op_errno = EINVAL;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->offset = offset;
        req->fd     = remote_fd;
        memcpy (req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);
        return 0;
out:
        return -op_errno;
}

int
client_pre_fstat (xlator_t *this, gfs3_fstat_req *req, fd_t *fd,
                   dict_t *xdata)
{
        int64_t         remote_fd = -1;
        int             op_errno = ESTALE;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->fd = remote_fd;
        memcpy (req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_lk (xlator_t *this, gfs3_lk_req *req,
               int32_t cmd, struct gf_flock *flock, fd_t *fd, dict_t *xdata)
{
        int64_t          remote_fd  = -1;
        int              op_errno   = ESTALE;
        int32_t          gf_cmd     = 0;
        int32_t          gf_type    = 0;
        int              ret        = 0;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        ret = client_cmd_to_gf_cmd (cmd, &gf_cmd);
        if (ret) {
                op_errno = EINVAL;
                gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                        PC_MSG_INVALID_ENTRY, "Unknown cmd (%d)!", gf_cmd);
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

        req->fd    = remote_fd;
        req->cmd   = gf_cmd;
        req->type  = gf_type;
        gf_proto_flock_from_flock (&req->flock, flock);

        memcpy (req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_lookup (xlator_t *this, gfs3_lookup_req *req, loc_t *loc,
                   dict_t *xdata)
{
        int              op_errno          = ESTALE;

        if (!(loc && loc->inode))
                goto out;

        if ((loc->parent) && (!gf_uuid_is_null (loc->parent->gfid)))
                        memcpy (req->pargfid, loc->parent->gfid, 16);
                else
                        memcpy (req->pargfid, loc->pargfid, 16);

        if ((loc->inode) && (!gf_uuid_is_null (loc->inode->gfid)))
                memcpy (req->gfid, loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);


        if (loc->name)
                req->bname = (char *)loc->name;
        else
                req->bname = "";

        if (xdata) {
                GF_PROTOCOL_DICT_SERIALIZE (this, xdata,
                                            (&req->xdata.xdata_val),
                                            req->xdata.xdata_len,
                                            op_errno, out);
        }
        return 0;
out:
        return -op_errno;
}

int
client_pre_readdir (xlator_t *this, gfs3_readdir_req *req, fd_t *fd,
                    size_t size, off_t offset, dict_t *xdata)
{
        int64_t           remote_fd  = -1;
        int               op_errno   = ESTALE;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->size = size;
        req->offset = offset;
        req->fd = remote_fd;

        memcpy (req->gfid, fd->inode->gfid, 16);
        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_inodelk (xlator_t *this, gfs3_inodelk_req *req, loc_t *loc,
                     int cmd, struct gf_flock *flock, const char *volume,
                     dict_t *xdata)
{
        int               op_errno = ESTALE;
        int32_t           gf_cmd  = 0;
        int32_t           gf_type = 0;

        if (!(loc && loc->inode))
                goto out;

        if (!gf_uuid_is_null (loc->gfid))
                memcpy (req->gfid,  loc->gfid, 16);
        else
                memcpy (req->gfid, loc->inode->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);
        if (cmd == F_GETLK || cmd == F_GETLK64)
                gf_cmd = GF_LK_GETLK;
        else if (cmd == F_SETLK || cmd == F_SETLK64)
                gf_cmd = GF_LK_SETLK;
        else if (cmd == F_SETLKW || cmd == F_SETLKW64)
                gf_cmd = GF_LK_SETLKW;
        else {
                gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                        PC_MSG_INVALID_ENTRY, "Unknown cmd (%d)!", gf_cmd);
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
        req->cmd    = gf_cmd;
        req->type   = gf_type;
        gf_proto_flock_from_flock (&req->flock, flock);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_finodelk (xlator_t *this, gfs3_finodelk_req *req, fd_t *fd,
                     int cmd, struct gf_flock *flock, const char *volume,
                     dict_t *xdata)
{
        int                op_errno = ESTALE;
        int64_t            remote_fd = -1;
        int32_t            gf_type  = 0;
        int32_t            gf_cmd   = 0;

        CLIENT_GET_REMOTE_FD (this, fd, FALLBACK_TO_ANON_FD,
                              remote_fd, op_errno, out);

        if (cmd == F_GETLK || cmd == F_GETLK64)
                gf_cmd = GF_LK_GETLK;
        else if (cmd == F_SETLK || cmd == F_SETLK64)
                gf_cmd = GF_LK_SETLK;
        else if (cmd == F_SETLKW || cmd == F_SETLKW64)
                gf_cmd = GF_LK_SETLKW;
        else {
                gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                        PC_MSG_INVALID_ENTRY, "Unknown cmd (%d)!", gf_cmd);
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
        req->fd    = remote_fd;
        req->cmd   = gf_cmd;
        req->type  = gf_type;
        gf_proto_flock_from_flock (&req->flock, flock);
        memcpy (req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);
        return 0;
out:
        return -op_errno;
}

int
client_pre_entrylk (xlator_t *this, gfs3_entrylk_req *req, loc_t *loc,
                     entrylk_cmd cmd_entrylk, entrylk_type type,
                     const char *volume, const char *basename, dict_t *xdata)
{
        int               op_errno = ESTALE;

        if (!(loc && loc->inode))
                goto out;

        if (!gf_uuid_is_null (loc->gfid))
                memcpy (req->gfid,  loc->gfid, 16);
        else
                memcpy (req->gfid, loc->inode->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);
        req->cmd = cmd_entrylk;
        req->type = type;
        req->volume = (char *)volume;
        req->name = "";
        if (basename) {
                req->name = (char *)basename;
                req->namelen = 1;
        }

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_fentrylk (xlator_t *this, gfs3_fentrylk_req *req, fd_t *fd,
                      entrylk_cmd cmd_entrylk, entrylk_type type,
                      const char *volume, const char *basename, dict_t *xdata)
{
        int64_t            remote_fd = -1;
        int                op_errno = ESTALE;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->fd  = remote_fd;
        req->cmd = cmd_entrylk;
        req->type = type;
        req->volume = (char *)volume;
        req->name = "";
        if (basename) {
                req->name = (char *)basename;
                req->namelen = 1;
        }
        memcpy (req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_xattrop (xlator_t *this, gfs3_xattrop_req *req, loc_t *loc,
                    dict_t *xattr, int32_t flags, dict_t *xdata)
{
        int               op_errno   = ESTALE;

        if (!(loc && loc->inode))
                goto out;

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (req->gfid,  loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);
        if (xattr) {
                GF_PROTOCOL_DICT_SERIALIZE (this, xattr,
                                            (&req->dict.dict_val),
                                            req->dict.dict_len,
                                            op_errno, out);
        }

        req->flags = flags;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_fxattrop (xlator_t *this, gfs3_fxattrop_req *req, fd_t *fd,
                    dict_t *xattr, int32_t flags, dict_t *xdata)
{
        int               op_errno   = ESTALE;
        int64_t           remote_fd  = -1;

        CLIENT_GET_REMOTE_FD (this, fd, FALLBACK_TO_ANON_FD,
                              remote_fd, op_errno, out);

        req->fd     = remote_fd;
        req->flags  = flags;
        memcpy (req->gfid, fd->inode->gfid, 16);

        if (xattr) {
                GF_PROTOCOL_DICT_SERIALIZE (this, xattr,
                                            (&req->dict.dict_val),
                                            req->dict.dict_len,
                                            op_errno, out);
        }

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_fgetxattr (xlator_t *this, gfs3_fgetxattr_req *req, fd_t *fd,
                      const char *name, dict_t *xdata)
{
        int64_t             remote_fd  = -1;
        int                 op_errno   = ESTALE;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->namelen = 1; /* Use it as a flag */
        req->fd   = remote_fd;
        req->name = (char *)name;
        if (!req->name) {
                req->name = "";
                req->namelen = 0;
        }
        memcpy (req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_fsetxattr (xlator_t *this, gfs3_fsetxattr_req *req, fd_t *fd,
                       int32_t flags, dict_t *xattr, dict_t *xdata)
{
        int64_t             remote_fd = -1;
        int                 op_errno = ESTALE;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->fd    = remote_fd;
        req->flags = flags;
        memcpy (req->gfid, fd->inode->gfid, 16);

        if (xattr) {
                GF_PROTOCOL_DICT_SERIALIZE (this, xattr,
                                            (&req->dict.dict_val),
                                            req->dict.dict_len,
                                            op_errno, out);
        }

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_rchecksum (xlator_t *this, gfs3_rchecksum_req *req, fd_t *fd,
                       int32_t len, off_t offset, dict_t *xdata)
{
        int64_t             remote_fd = -1;
        int                 op_errno = ESTALE;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->len    = len;
        req->offset = offset;
        req->fd     = remote_fd;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_setattr (xlator_t *this, gfs3_setattr_req *req, loc_t *loc,
                     int32_t valid, struct iatt *stbuf, dict_t *xdata)
{
        int               op_errno = ESTALE;

        if (!(loc && loc->inode))
                return -op_errno;

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (req->gfid, loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);

        req->valid = valid;
        gf_stat_from_iatt (&req->stbuf, stbuf);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_fsetattr (xlator_t *this, gfs3_fsetattr_req *req, fd_t *fd,
                     int32_t valid, struct iatt *stbuf, dict_t *xdata)
{
        int                op_errno    = ESTALE;
        int64_t            remote_fd   = -1;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->fd = remote_fd;
        req->valid = valid;
        gf_stat_from_iatt (&req->stbuf, stbuf);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);
        return 0;
out:
        return -op_errno;
}

int
client_pre_readdirp (xlator_t *this, gfs3_readdirp_req *req, fd_t *fd,
                     size_t size, off_t offset, dict_t *xdata)
{
        int               op_errno          = ESTALE;
        int64_t           remote_fd         = -1;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->size = size;
        req->offset = offset;
        req->fd = remote_fd;
        memcpy (req->gfid, fd->inode->gfid, 16);

        /* dict itself is 'xdata' here */
        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->dict.dict_val),
                                    req->dict.dict_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_fremovexattr (xlator_t *this, gfs3_fremovexattr_req *req, fd_t *fd,
                          const char *name, dict_t *xdata)
{
        int64_t                remote_fd = -1;
        int                    op_errno = ESTALE;

        if (!(fd && fd->inode))
                goto out;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        memcpy (req->gfid,  fd->inode->gfid, 16);
        req->name = (char *)name;
        req->fd = remote_fd;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_fallocate (xlator_t *this, gfs3_fallocate_req *req, fd_t *fd,
                      int32_t flags, off_t offset, size_t size, dict_t *xdata)
{
        int                op_errno    = ESTALE;
        int64_t            remote_fd   = -1;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->fd = remote_fd;
	req->flags = flags;
	req->offset = offset;
	req->size = size;
	memcpy(req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);
        return 0;
out:
        return -op_errno;
}

int
client_pre_discard (xlator_t *this, gfs3_discard_req *req, fd_t *fd,
                    off_t offset, size_t size, dict_t *xdata)
{
        int                op_errno    = ESTALE;
        int64_t            remote_fd   = -1;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->fd = remote_fd;
	req->offset = offset;
	req->size = size;
	memcpy(req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);
        return 0;
out:
        return -op_errno;
}

int
client_pre_zerofill (xlator_t *this, gfs3_zerofill_req *req, fd_t *fd,
                     off_t offset, size_t size, dict_t *xdata)
{
        int                op_errno    = ESTALE;
        int64_t            remote_fd   = -1;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        req->fd = remote_fd;
        req->offset = offset;
        req->size = size;
        memcpy(req->gfid, fd->inode->gfid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);
        return 0;
out:
        return -op_errno;
}

int
client_pre_ipc (xlator_t *this, gfs3_ipc_req *req, int32_t cmd,
                 dict_t *xdata)
{
        int                op_errno    = ESTALE;

        req->op = cmd;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);
        return 0;
out:
        return -op_errno;
}

int
client_pre_seek (xlator_t *this, gfs3_seek_req *req, fd_t *fd,
                 off_t offset, gf_seek_what_t what, dict_t *xdata)
{
        int64_t                 remote_fd   = -1;
        int                     op_errno    = ESTALE;

        CLIENT_GET_REMOTE_FD (this, fd, DEFAULT_REMOTE_FD,
                              remote_fd, op_errno, out);

        memcpy (req->gfid, fd->inode->gfid, 16);
        req->fd = remote_fd;
        req->offset = offset;
        req->what = what;

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);

        return 0;
out:
        return -op_errno;
}

int
client_pre_lease (xlator_t *this, gfs3_lease_req *req, loc_t *loc,
                  struct gf_lease *lease, dict_t *xdata)
{
        int op_errno = 0;

        if (!(loc && loc->inode))
                goto out;

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (req->gfid, loc->inode->gfid, 16);
        else
                memcpy (req->gfid, loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                      !gf_uuid_is_null (*((uuid_t *)req->gfid)),
                                      out, op_errno, EINVAL);

        gf_proto_lease_from_lease (&req->lease, lease);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&req->xdata.xdata_val),
                                    req->xdata.xdata_len, op_errno, out);
out:
        return -op_errno;
}

/* processing done after fop responses are obtained */
int
client_post_stat (xlator_t *this, gfs3_stat_rsp *rsp, struct iatt *iatt,
                  dict_t **xdata)
{
        int     ret = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->stat, iatt);
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);

out:
        return ret;
}

int
client_post_readlink (xlator_t *this, gfs3_readlink_rsp *rsp,
                      struct iatt *iatt, dict_t **xdata)
{
        int             ret = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->buf, iatt);
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);

out:

        return ret;
}

int
client_post_mknod (xlator_t *this, gfs3_mknod_rsp *rsp, struct iatt *stbuf,
                   struct iatt *preparent, struct iatt *postparent,
                   dict_t **xdata)
{
        int             ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->stat, stbuf);
                gf_stat_to_iatt (&rsp->preparent, preparent);
                gf_stat_to_iatt (&rsp->postparent, postparent);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                       rsp->op_errno, out);

out:
        return ret;
}

int
client_post_mkdir (xlator_t *this, gfs3_mkdir_rsp *rsp, struct iatt *stbuf,
                   struct iatt *preparent, struct iatt *postparent,
                   dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->stat, stbuf);
                gf_stat_to_iatt (&rsp->preparent, preparent);
                gf_stat_to_iatt (&rsp->postparent, postparent);
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);

out:
        return ret;
}

int
client_post_unlink (xlator_t *this, gfs3_unlink_rsp *rsp,
                   struct iatt *preparent, struct iatt *postparent,
                   dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->preparent, preparent);
                gf_stat_to_iatt (&rsp->postparent, postparent);
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_rmdir (xlator_t *this, gfs3_rmdir_rsp *rsp,
                   struct iatt *preparent, struct iatt *postparent,
                   dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->preparent, preparent);
                gf_stat_to_iatt (&rsp->postparent, postparent);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);

out:
        return ret;
}

int
client_post_symlink (xlator_t *this, gfs3_symlink_rsp *rsp, struct iatt *stbuf,
                     struct iatt *preparent, struct iatt *postparent,
                     dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->stat, stbuf);
                gf_stat_to_iatt (&rsp->preparent, preparent);
                gf_stat_to_iatt (&rsp->postparent, postparent);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_rename (xlator_t *this, gfs3_rename_rsp *rsp, struct iatt *stbuf,
                   struct iatt *preoldparent, struct iatt *postoldparent,
                   struct iatt *prenewparent, struct iatt *postnewparent,
                   dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->stat, stbuf);

                gf_stat_to_iatt (&rsp->preoldparent, preoldparent);
                gf_stat_to_iatt (&rsp->postoldparent, postoldparent);

                gf_stat_to_iatt (&rsp->prenewparent, prenewparent);
                gf_stat_to_iatt (&rsp->postnewparent, postnewparent);
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_link (xlator_t *this, gfs3_link_rsp *rsp, struct iatt *stbuf,
                  struct iatt *preparent, struct iatt *postparent,
                  dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->stat, stbuf);
                gf_stat_to_iatt (&rsp->preparent, preparent);
                gf_stat_to_iatt (&rsp->postparent, postparent);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_truncate (xlator_t *this, gfs3_truncate_rsp *rsp,
                      struct iatt *prestat, struct iatt *poststat,
                      dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->prestat, prestat);
                gf_stat_to_iatt (&rsp->poststat, poststat);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_open (xlator_t *this, gfs3_open_rsp *rsp, dict_t **xdata)
{
        int     ret     = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_readv (xlator_t *this, gfs3_read_rsp *rsp, struct iobref **iobref,
                   struct iobref *rsp_iobref, struct iatt *stat,
                   struct iovec *vector, struct iovec *rsp_vector,
                   int *rspcount, dict_t **xdata)
{
        int     ret     = 0;

        if (rsp->op_ret != -1) {
                *iobref = rsp_iobref;
                gf_stat_to_iatt (&rsp->stat, stat);

                vector[0].iov_len = rsp->op_ret;
                if (rsp->op_ret > 0)
                        vector[0].iov_base = rsp_vector->iov_base;
                *rspcount = 1;
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);

#ifdef GF_TESTING_IO_XDATA
        dict_dump_to_log (xdata);
#endif
out:
        return ret;
}

int
client_post_writev (xlator_t *this, gfs3_write_rsp *rsp, struct iatt *prestat,
                    struct iatt *poststat, dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->prestat, prestat);
                gf_stat_to_iatt (&rsp->poststat, poststat);
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);

out:
        return ret;
}

int
client_post_statfs (xlator_t *this, gfs3_statfs_rsp *rsp,
                    struct statvfs *statfs, dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_statfs_to_statfs (&rsp->statfs, statfs);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_flush (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata)
{
        int     ret = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);

out:
        return ret;
}

int
client_post_fsync (xlator_t *this, gfs3_fsync_rsp *rsp,
                   struct iatt *prestat, struct iatt *poststat,
                   dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->prestat, prestat);
                gf_stat_to_iatt (&rsp->poststat, poststat);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_setxattr (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata)
{
        int     ret     = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_getxattr (xlator_t *this, gfs3_getxattr_rsp *rsp, dict_t **dict,
                      dict_t **xdata)
{
        int     op_errno = 0;
        int     ret      = 0;

        if (-1 != rsp->op_ret) {
                GF_PROTOCOL_DICT_UNSERIALIZE (this, *dict,
                                              (rsp->dict.dict_val),
                                              (rsp->dict.dict_len), rsp->op_ret,
                                               op_errno, out);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      op_errno, out);

out:
        return -op_errno;
}

int
client_post_removexattr (xlator_t *this, gf_common_rsp *rsp,
                         dict_t **xdata)
{
        int     ret     = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_opendir (xlator_t *this, gfs3_opendir_rsp *rsp, dict_t **xdata)
{
        int     ret     = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_fsyncdir (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata)
{
        int     ret     = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_access (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata)
{
        int     ret     = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_create (xlator_t *this, gfs3_create_rsp *rsp,
                    struct iatt *stbuf, struct iatt *preparent,
                    struct iatt *postparent,
                    clnt_local_t *local, dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->stat, stbuf);

                gf_stat_to_iatt (&rsp->preparent, preparent);
                gf_stat_to_iatt (&rsp->postparent, postparent);
                gf_uuid_copy (local->loc.gfid, stbuf->ia_gfid);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_ftruncate (xlator_t *this, gfs3_ftruncate_rsp *rsp,
                       struct iatt *prestat, struct iatt *poststat,
                       dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->prestat, prestat);
                gf_stat_to_iatt (&rsp->poststat, poststat);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_fstat (xlator_t *this, gfs3_fstat_rsp *rsp, struct iatt *stat,
                   dict_t **xdata)
{
        int     ret = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->stat, stat);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return -ret;
}

int
client_post_lk (xlator_t *this, gfs3_lk_rsp *rsp, struct gf_flock *lock,
                dict_t **xdata)
{
        int     ret     = 0;

        if (rsp->op_ret >= 0) {
                gf_proto_flock_to_flock (&rsp->flock, lock);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_lookup (xlator_t *this, gfs3_lookup_rsp *rsp, struct iatt *stbuf,
                    struct iatt *postparent, dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->postparent, postparent);
                gf_stat_to_iatt (&rsp->stat, stbuf);
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_readdir (xlator_t *this, gfs3_readdir_rsp *rsp,
                     gf_dirent_t *entries, dict_t **xdata)
{
        int     ret     = 0;

        if (rsp->op_ret > 0) {
                unserialize_rsp_dirent (this, rsp, entries);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);

out:
        return ret;
}

int
client_post_inodelk (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata)
{
        int     ret = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);

out:
        return ret;
}

int
client_post_finodelk (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata)
{
        int     ret = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);

out:
        return ret;
}

int
client_post_entrylk (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata)
{
        int     ret = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);

out:
        return ret;
}

int
client_post_fentrylk (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata)
{
        int     ret = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);

out:
        return ret;
}

int
client_post_xattrop (xlator_t *this, gfs3_xattrop_rsp *rsp, dict_t **dict,
                      dict_t **xdata)
{
        int     op_errno = 0;
        int     ret      = 0;

        if (-1 != rsp->op_ret) {
                GF_PROTOCOL_DICT_UNSERIALIZE (this, *dict,
                                              (rsp->dict.dict_val),
                                              (rsp->dict.dict_len), rsp->op_ret,
                                               op_errno, out);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      op_errno, out);

out:
        return -op_errno;
}

int
client_post_fxattrop (xlator_t *this, gfs3_fxattrop_rsp *rsp, dict_t **dict,
                      dict_t **xdata)
{
        int     op_errno = 0;
        int     ret      = 0;

        if (-1 != rsp->op_ret) {
                GF_PROTOCOL_DICT_UNSERIALIZE (this, *dict,
                                              (rsp->dict.dict_val),
                                              (rsp->dict.dict_len), rsp->op_ret,
                                               op_errno, out);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      op_errno, out);

out:
        return -op_errno;
}

int
client_post_fgetxattr (xlator_t *this, gfs3_fgetxattr_rsp *rsp, dict_t **dict,
                      dict_t **xdata)
{
        int     op_errno = 0;
        int     ret      = 0;

        if (-1 != rsp->op_ret) {
                GF_PROTOCOL_DICT_UNSERIALIZE (this, *dict,
                                              (rsp->dict.dict_val),
                                              (rsp->dict.dict_len), rsp->op_ret,
                                               op_errno, out);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      op_errno, out);

out:
        return -op_errno;
}

int
client_post_fsetxattr (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata)
{
        int     ret     = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_rchecksum (xlator_t *this, gfs3_rchecksum_rsp *rsp, dict_t **xdata)
{
        int     ret     = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_setattr (xlator_t *this, gfs3_setattr_rsp *rsp,
                     struct iatt *prestat, struct iatt *poststat,
                     dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->statpre, prestat);
                gf_stat_to_iatt (&rsp->statpost, poststat);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_fsetattr (xlator_t *this, gfs3_fsetattr_rsp *rsp,
                      struct iatt *prestat, struct iatt *poststat,
                      dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->statpre, prestat);
                gf_stat_to_iatt (&rsp->statpost, poststat);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_readdirp (xlator_t *this, gfs3_readdirp_rsp *rsp,
                      fd_t *fd, gf_dirent_t *entries,
                      dict_t **xdata)
{
        int     ret     = 0;

        if (rsp->op_ret > 0) {
                unserialize_rsp_direntp (this, fd, rsp, entries);
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_fremovexattr (xlator_t *this, gf_common_rsp *rsp,
                          dict_t **xdata)
{
        int     ret     = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_fallocate (xlator_t *this, gfs3_fallocate_rsp *rsp,
                   struct iatt *prestat, struct iatt *poststat,
                   dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->statpre, prestat);
                gf_stat_to_iatt (&rsp->statpost, poststat);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_discard (xlator_t *this, gfs3_discard_rsp *rsp,
                     struct iatt *prestat,
                     struct iatt *poststat, dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->statpre, prestat);
                gf_stat_to_iatt (&rsp->statpost, poststat);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_zerofill (xlator_t *this, gfs3_zerofill_rsp *rsp,
                     struct iatt *prestat, struct iatt *poststat,
                     dict_t **xdata)
{
        int     ret     = 0;

        if (-1 != rsp->op_ret) {
                gf_stat_to_iatt (&rsp->statpre, prestat);
                gf_stat_to_iatt (&rsp->statpost, poststat);
        }
        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_ipc (xlator_t *this, gfs3_ipc_rsp *rsp, dict_t **xdata)
{
        int     ret     = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_seek (xlator_t *this, gfs3_seek_rsp *rsp, dict_t **xdata)
{
        int     ret     = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}

int
client_post_lease (xlator_t *this, gfs3_lease_rsp *rsp, struct gf_lease *lease,
                   dict_t **xdata)
{
        int ret = 0;

        if (rsp->op_ret >= 0) {
                gf_proto_lease_to_lease (&rsp->lease, lease);
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, *xdata, (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), ret,
                                      rsp->op_errno, out);
out:
        return ret;
}
