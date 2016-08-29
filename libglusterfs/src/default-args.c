/*
  Copyright (c) 2008-2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "defaults.h"

int
args_lookup_store (default_args_t *args, loc_t *loc,
                   dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_lookup_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     inode_t *inode, struct iatt *buf,
                     dict_t *xdata, struct iatt *postparent)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (inode)
                args->inode = inode_ref (inode);
        if (buf)
                args->stat = *buf;
        if (postparent)
                args->postparent = *postparent;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}


int
args_stat_store (default_args_t *args, loc_t *loc, dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_stat_cbk_store (default_args_cbk_t *args,
                   int32_t op_ret, int32_t op_errno,
                   struct iatt *buf, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (op_ret == 0)
                args->stat = *buf;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_fstat_store (default_args_t *args, fd_t *fd, dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_fstat_cbk_store (default_args_cbk_t *args,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *buf, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (buf)
                args->stat = *buf;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_truncate_store (default_args_t *args, loc_t *loc, off_t off,
                     dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        args->offset = off;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_truncate_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (prebuf)
                args->prestat = *prebuf;
        if (postbuf)
                args->poststat = *postbuf;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_ftruncate_store (default_args_t *args, fd_t *fd, off_t off,
                      dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);

        args->offset = off;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_ftruncate_cbk_store (default_args_cbk_t *args,
                        int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                        struct iatt *postbuf, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (prebuf)
                args->prestat = *prebuf;
        if (postbuf)
                args->poststat = *postbuf;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}


int
args_access_store (default_args_t *args, loc_t *loc, int32_t mask,
                   dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        args->mask = mask;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_access_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}


int
args_readlink_store (default_args_t *args, loc_t *loc, size_t size,
                     dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        args->size = size;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_readlink_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno,
                       const char *path, struct iatt *stbuf, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (path)
                args->buf = gf_strdup (path);
        if (stbuf)
                args->stat = *stbuf;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_mknod_store (default_args_t *args, loc_t *loc, mode_t mode,
                  dev_t rdev, mode_t umask, dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        args->mode = mode;
        args->rdev = rdev;
        args->umask = umask;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_mknod_cbk_store (default_args_cbk_t *args, int op_ret,
                    int32_t op_errno, inode_t *inode, struct iatt *buf,
                    struct iatt *preparent, struct iatt *postparent,
                    dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (inode)
                args->inode = inode_ref (inode);
        if (buf)
                args->stat = *buf;
        if (preparent)
                args->preparent = *preparent;
        if (postparent)
                args->postparent = *postparent;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_mkdir_store (default_args_t *args, loc_t *loc, mode_t mode,
                  mode_t umask, dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        args->mode  = mode;
        args->umask = umask;

        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_mkdir_cbk_store (default_args_cbk_t *args,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (inode)
                args->inode = inode_ref (inode);
        if (buf)
                args->stat = *buf;
        if (preparent)
                args->preparent = *preparent;
        if (postparent)
                args->postparent = *postparent;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_unlink_store (default_args_t *args, loc_t *loc, int xflag, dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        args->xflag = xflag;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_unlink_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *preparent, struct iatt *postparent,
                     dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (preparent)
                args->preparent = *preparent;
        if (postparent)
                args->postparent = *postparent;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_rmdir_store (default_args_t *args, loc_t *loc, int flags, dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        args->flags = flags;
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_rmdir_cbk_store (default_args_cbk_t *args,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *preparent, struct iatt *postparent,
                    dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (preparent)
                args->preparent = *preparent;
        if (postparent)
                args->postparent = *postparent;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_symlink_store (default_args_t *args, const char *linkname, loc_t *loc,
                   mode_t umask, dict_t *xdata)
{
        args->linkname = gf_strdup (linkname);
        args->umask = umask;
        loc_copy (&args->loc, loc);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_symlink_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno,
                      inode_t *inode, struct iatt *buf,
                      struct iatt *preparent, struct iatt *postparent,
                      dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (inode)
                args->inode = inode_ref (inode);
        if (buf)
                args->stat = *buf;
        if (preparent)
                args->preparent = *preparent;
        if (postparent)
                args->postparent = *postparent;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}


int
args_rename_store (default_args_t *args, loc_t *oldloc, loc_t *newloc,
                   dict_t *xdata)
{
        loc_copy (&args->loc, oldloc);
        loc_copy (&args->loc2, newloc);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_rename_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf,
                     struct iatt *preoldparent, struct iatt *postoldparent,
                     struct iatt *prenewparent, struct iatt *postnewparent,
                     dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (buf)
                args->stat = *buf;
        if (preoldparent)
                args->preparent = *preoldparent;
        if (postoldparent)
                args->postparent = *postoldparent;
        if (prenewparent)
                args->preparent2 = *prenewparent;
        if (postnewparent)
                args->postparent2 = *postnewparent;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_link_store (default_args_t *args, loc_t *oldloc, loc_t *newloc,
                 dict_t *xdata)
{
        loc_copy (&args->loc, oldloc);
        loc_copy (&args->loc2, newloc);

        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_link_cbk_store (default_args_cbk_t *args,
                   int32_t op_ret, int32_t op_errno,
                   inode_t *inode, struct iatt *buf,
                   struct iatt *preparent, struct iatt *postparent,
                   dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (inode)
                args->inode = inode_ref (inode);
        if (buf)
                args->stat = *buf;
        if (preparent)
                args->preparent = *preparent;
        if (postparent)
                args->postparent = *postparent;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_create_store (default_args_t *args,
                  loc_t *loc, int32_t flags, mode_t mode,
                  mode_t umask, fd_t *fd, dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        args->flags = flags;
        args->mode = mode;
        args->umask = umask;
        if (fd)
                args->fd = fd_ref (fd);
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_create_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     fd_t *fd, inode_t *inode, struct iatt *buf,
                     struct iatt *preparent, struct iatt *postparent,
                     dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (fd)
                args->fd = fd_ref (fd);
        if (inode)
                args->inode = inode_ref (inode);
        if (buf)
                args->stat = *buf;
        if (preparent)
                args->preparent = *preparent;
        if (postparent)
                args->postparent = *postparent;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_open_store (default_args_t *args, loc_t *loc, int32_t flags,
                 fd_t *fd, dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        args->flags = flags;
        if (fd)
                args->fd = fd_ref (fd);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_open_cbk_store (default_args_cbk_t *args,
                   int32_t op_ret, int32_t op_errno,
                   fd_t *fd, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (fd)
                args->fd = fd_ref (fd);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_readv_store (default_args_t *args, fd_t *fd, size_t size, off_t off,
                  uint32_t flags, dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);
        args->size  = size;
        args->offset  = off;
        args->flags = flags;

        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_readv_cbk_store (default_args_cbk_t *args,
                    int32_t op_ret, int32_t op_errno, struct iovec *vector,
                    int32_t count, struct iatt *stbuf,
                    struct iobref *iobref, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (op_ret >= 0) {
                args->vector = iov_dup (vector, count);
                args->count = count;
                args->stat = *stbuf;
                args->iobref = iobref_ref (iobref);
        }
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_writev_store (default_args_t *args, fd_t *fd, struct iovec *vector,
                   int32_t count, off_t off, uint32_t flags,
                   struct iobref *iobref, dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);
        args->vector = iov_dup (vector, count);
        args->count  = count;
        args->offset = off;
        args->flags  = flags;
        args->iobref = iobref_ref (iobref);
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_writev_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (op_ret >= 0)
                args->poststat = *postbuf;
        if (prebuf)
                args->prestat = *prebuf;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_flush_store (default_args_t *args, fd_t *fd, dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_flush_cbk_store (default_args_cbk_t *args,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_fsync_store (default_args_t *args, fd_t *fd, int32_t datasync,
                  dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);
        args->datasync = datasync;
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_fsync_cbk_store (default_args_cbk_t *args,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (prebuf)
                args->prestat = *prebuf;
        if (postbuf)
                args->poststat = *postbuf;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_opendir_store (default_args_t *args, loc_t *loc, fd_t *fd, dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        if (fd)
                args->fd = fd_ref (fd);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_opendir_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno,
                      fd_t *fd, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (fd)
                args->fd = fd_ref (fd);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_fsyncdir_store (default_args_t *args, fd_t *fd, int32_t datasync,
                     dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);
        args->datasync = datasync;
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}
int
args_fsyncdir_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_statfs_store (default_args_t *args, loc_t *loc, dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_statfs_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     struct statvfs *buf, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (op_ret == 0)
                args->statvfs = *buf;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_setxattr_store (default_args_t *args,
                     loc_t *loc, dict_t *dict,
                     int32_t flags, dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        /* TODO */
        if (dict)
                args->xattr = dict_ref (dict);
        args->flags = flags;
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_setxattr_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret,
                       int32_t op_errno, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_getxattr_store (default_args_t *args,
                     loc_t *loc, const char *name, dict_t *xdata)
{
        loc_copy (&args->loc, loc);

        if (name)
                args->name = gf_strdup (name);
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_getxattr_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno,
                       dict_t *dict, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (dict)
                args->xattr = dict_ref (dict);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_fsetxattr_store (default_args_t *args,
                      fd_t *fd, dict_t *dict, int32_t flags, dict_t *xdata)
{
        args->fd = fd_ref (fd);

        if (dict)
                args->xattr = dict_ref (dict);
        args->flags = flags;
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_fsetxattr_cbk_store (default_args_cbk_t *args,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_fgetxattr_store (default_args_t *args,
                      fd_t *fd, const char *name, dict_t *xdata)
{
        args->fd = fd_ref (fd);

        if (name)
                args->name = gf_strdup (name);
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_fgetxattr_cbk_store (default_args_cbk_t *args,
                        int32_t op_ret, int32_t op_errno,
                        dict_t *dict, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (dict)
                args->xattr = dict_ref (dict);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_removexattr_store (default_args_t *args,
                        loc_t *loc, const char *name, dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        args->name = gf_strdup (name);
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_removexattr_cbk_store (default_args_cbk_t *args,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_fremovexattr_store (default_args_t *args,
                         fd_t *fd, const char *name, dict_t *xdata)
{
        args->fd = fd_ref (fd);
        args->name = gf_strdup (name);
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_fremovexattr_cbk_store (default_args_cbk_t *args,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_lk_store (default_args_t *args,
               fd_t *fd, int32_t cmd,
                struct gf_flock *lock, dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);
        args->cmd = cmd;
        args->lock = *lock;
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_lk_cbk_store (default_args_cbk_t *args,
                 int32_t op_ret, int32_t op_errno,
                 struct gf_flock *lock, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (op_ret == 0)
                args->lock = *lock;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}


int
args_inodelk_store (default_args_t *args,
                    const char *volume, loc_t *loc, int32_t cmd,
                    struct gf_flock *lock, dict_t *xdata)
{
        if (volume)
                args->volume = gf_strdup (volume);

        loc_copy (&args->loc, loc);
        args->cmd  = cmd;
        args->lock = *lock;
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_inodelk_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_finodelk_store (default_args_t *args,
                     const char *volume, fd_t *fd, int32_t cmd,
                     struct gf_flock *lock, dict_t *xdata)
{
        if (fd)
                args->fd   = fd_ref (fd);

        if (volume)
                args->volume = gf_strdup (volume);

        args->cmd  = cmd;
        args->lock = *lock;

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_finodelk_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_entrylk_store (default_args_t *args,
                    const char *volume, loc_t *loc, const char *name,
                    entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        if (volume)
                args->volume = gf_strdup (volume);

        loc_copy (&args->loc, loc);

        args->entrylkcmd = cmd;
        args->entrylktype = type;

        if (name)
                args->name = gf_strdup (name);

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_entrylk_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_fentrylk_store (default_args_t *args,
                     const char *volume, fd_t *fd, const char *name,
                     entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        if (volume)
                args->volume = gf_strdup (volume);

        if (fd)
                args->fd = fd_ref (fd);
        args->entrylkcmd = cmd;
        args->entrylktype = type;
        if (name)
                args->name = gf_strdup (name);

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_fentrylk_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}


int
args_readdirp_store (default_args_t *args,
                     fd_t *fd, size_t size, off_t off, dict_t *xdata)
{
        args->fd = fd_ref (fd);
        args->size = size;
        args->offset = off;
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_readdirp_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno,
                       gf_dirent_t *entries, dict_t *xdata)
{
        gf_dirent_t *stub_entry = NULL, *entry = NULL;

        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (op_ret > 0) {
                list_for_each_entry (entry, &entries->list, list) {
                        stub_entry = gf_dirent_for_name (entry->d_name);
                        if (!stub_entry)
                                goto out;
                        stub_entry->d_off = entry->d_off;
                        stub_entry->d_ino = entry->d_ino;
                        stub_entry->d_stat = entry->d_stat;
                        stub_entry->d_type = entry->d_type;
                        if (entry->inode)
                                stub_entry->inode = inode_ref (entry->inode);
                        if (entry->dict)
                                stub_entry->dict = dict_ref (entry->dict);
                        list_add_tail (&stub_entry->list,
                                       &args->entries.list);
                }
        }
        if (xdata)
                args->xdata = dict_ref (xdata);
out:
        return 0;
}


int
args_readdir_store (default_args_t *args,
                    fd_t *fd, size_t size,
                    off_t off, dict_t *xdata)
{
        args->fd = fd_ref (fd);
        args->size = size;
        args->offset = off;

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_readdir_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno,
                      gf_dirent_t *entries, dict_t *xdata)
{
        gf_dirent_t *stub_entry = NULL, *entry = NULL;

        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (op_ret > 0) {
                list_for_each_entry (entry, &entries->list, list) {
                        stub_entry = gf_dirent_for_name (entry->d_name);
                        if (!stub_entry)
                                goto out;
                        stub_entry->d_off = entry->d_off;
                        stub_entry->d_ino = entry->d_ino;
                        stub_entry->d_type = entry->d_type;
                        list_add_tail (&stub_entry->list,
                                       &args->entries.list);
                }
        }
        if (xdata)
                args->xdata = dict_ref (xdata);
out:
        return 0;
}


int
args_rchecksum_store (default_args_t *args,
                      fd_t *fd, off_t offset, int32_t len, dict_t *xdata)
{
        args->fd = fd_ref (fd);
        args->offset = offset;
        args->size    = len;
        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_rchecksum_cbk_store (default_args_cbk_t *args,
                        int32_t op_ret, int32_t op_errno,
                        uint32_t weak_checksum, uint8_t *strong_checksum,
                        dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (op_ret >= 0) {
                args->weak_checksum =
                        weak_checksum;
                args->strong_checksum =
                        memdup (strong_checksum, MD5_DIGEST_LENGTH);
        }
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_xattrop_store (default_args_t *args,
                    loc_t *loc, gf_xattrop_flags_t optype,
                    dict_t *xattr, dict_t *xdata)
{
        loc_copy (&args->loc, loc);

        args->optype = optype;
        args->xattr = dict_ref (xattr);

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}


int
args_xattrop_cbk_store (default_args_cbk_t *args, int32_t op_ret,
                        int32_t op_errno, dict_t *xattr, dict_t *xdata)
{
        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (xattr)
                args->xattr = dict_ref (xattr);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}


int
args_fxattrop_store (default_args_t *args,
                     fd_t *fd, gf_xattrop_flags_t optype,
                     dict_t *xattr, dict_t *xdata)
{
        args->fd = fd_ref (fd);

        args->optype = optype;
        args->xattr = dict_ref (xattr);

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_fxattrop_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno,
                       dict_t *xattr, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (xattr)
                args->xattr = dict_ref (xattr);
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_setattr_store (default_args_t *args,
                    loc_t *loc, struct iatt *stbuf,
                    int32_t valid, dict_t *xdata)
{
        loc_copy (&args->loc, loc);

        if (stbuf)
                args->stat = *stbuf;

        args->valid = valid;

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_setattr_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno,
                      struct iatt *statpre, struct iatt *statpost,
                      dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (statpre)
                args->prestat = *statpre;
        if (statpost)
                args->poststat = *statpost;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}


int
args_fsetattr_store (default_args_t *args,
                     fd_t *fd, struct iatt *stbuf,
                     int32_t valid, dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);

        if (stbuf)
                args->stat = *stbuf;

        args->valid = valid;

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}
int
args_fsetattr_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *statpre, struct iatt *statpost,
                       dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (statpre)
                args->prestat = *statpre;
        if (statpost)
                args->poststat = *statpost;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_fallocate_store (default_args_t *args, fd_t *fd,
                      int32_t mode, off_t offset, size_t len, dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);

	args->flags = mode;
	args->offset = offset;
	args->size = len;

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_fallocate_cbk_store(default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *statpre, struct iatt *statpost,
                       dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (statpre)
                args->prestat = *statpre;
        if (statpost)
                args->poststat = *statpost;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_discard_store (default_args_t *args, fd_t *fd,
		    off_t offset, size_t len, dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);

	args->offset = offset;
	args->size = len;

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_discard_cbk_store(default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *statpre, struct iatt *statpost,
                     dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (statpre)
                args->prestat = *statpre;
        if (statpost)
                args->poststat = *statpost;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_zerofill_store (default_args_t *args, fd_t *fd,
                     off_t offset, off_t len, dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);

        args->offset = offset;
        args->size = len;

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_zerofill_cbk_store(default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *statpre, struct iatt *statpost,
                     dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (statpre)
                args->prestat = *statpre;
        if (statpost)
                args->poststat = *statpost;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_ipc_store (default_args_t *args,
                int32_t op, dict_t *xdata)
{
        args->cmd = op;

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_ipc_cbk_store (default_args_cbk_t *args,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_seek_store (default_args_t *args, fd_t *fd,
                 off_t offset, gf_seek_what_t what, dict_t *xdata)
{
        if (fd)
                args->fd = fd_ref (fd);

        args->offset = offset;
        args->what = what;

        if (xdata)
                args->xdata = dict_ref (xdata);
        return 0;
}

int
args_seek_cbk_store (default_args_cbk_t *args, int32_t op_ret,
                     int32_t op_errno, off_t offset, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        args->offset = offset;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
}

int
args_getactivelk_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno,
                      lock_migration_info_t *locklist, dict_t *xdata)
{
        lock_migration_info_t  *stub_entry = NULL, *entry = NULL;
        int     ret = 0;

        args->op_ret = op_ret;
        args->op_errno = op_errno;
        /*op_ret needs to carry the number of locks present in the list*/
        if (op_ret > 0) {
                list_for_each_entry (entry, &locklist->list, list) {
                        stub_entry = GF_CALLOC (1, sizeof (*stub_entry),
                                                gf_common_mt_char);
                        if (!stub_entry) {
                               ret = -1;
                               goto out;
                        }

                        INIT_LIST_HEAD (&stub_entry->list);
                        stub_entry->flock = entry->flock;

                        stub_entry->lk_flags = entry->lk_flags;

                        stub_entry->client_uid = gf_strdup (entry->client_uid);
                        if (!stub_entry->client_uid) {
                                GF_FREE (stub_entry);
                                ret = -1;
                                goto out;
                        }

                        list_add_tail (&stub_entry->list,
                                       &args->locklist.list);
                }
        }

        if (xdata)
                args->xdata = dict_ref (xdata);
out:
        return ret;
}

int
args_setactivelk_store (default_args_t *args, loc_t *loc,
                          lock_migration_info_t *locklist, dict_t *xdata)
{
        lock_migration_info_t  *stub_entry = NULL, *entry = NULL;
        int     ret = 0;

        list_for_each_entry (entry, &locklist->list, list) {
                stub_entry = GF_CALLOC (1, sizeof (*stub_entry),
                                        gf_common_mt_lock_mig);
                if (!stub_entry) {
                       ret = -1;
                       goto out;
                }

                INIT_LIST_HEAD (&stub_entry->list);
                stub_entry->flock = entry->flock;

                stub_entry->lk_flags = entry->lk_flags;

                stub_entry->client_uid = gf_strdup (entry->client_uid);
                if (!stub_entry->client_uid) {
                        GF_FREE (stub_entry);
                        ret = -1;
                        goto out;
                }

                list_add_tail (&stub_entry->list,
                               &args->locklist.list);
        }

        loc_copy (&args->loc, loc);

        if (xdata)
                args->xdata = dict_ref (xdata);

out:
        return ret;
}

void
args_lease_store (default_args_t *args, loc_t *loc, struct gf_lease *lease,
                  dict_t *xdata)
{
        loc_copy (&args->loc, loc);
        args->lease = *lease;

        if (xdata)
                args->xdata = dict_ref (xdata);

        return;
}

void
args_lease_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     struct gf_lease *lease, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (op_ret == 0)
                args->lease = *lease;
        if (xdata)
                args->xdata = dict_ref (xdata);
}

void
args_cbk_wipe (default_args_cbk_t *args_cbk)
{
        if (!args_cbk)
                return;
        if (args_cbk->inode)
                inode_unref (args_cbk->inode);

        GF_FREE ((char *)args_cbk->buf);

        GF_FREE (args_cbk->vector);

        if (args_cbk->iobref)
                iobref_unref (args_cbk->iobref);

        if (args_cbk->fd)
                fd_unref (args_cbk->fd);

        if (args_cbk->xattr)
                dict_unref (args_cbk->xattr);

        GF_FREE (args_cbk->strong_checksum);

        if (args_cbk->xdata)
                dict_unref (args_cbk->xdata);

        if (!list_empty (&args_cbk->entries.list))
                gf_dirent_free (&args_cbk->entries);
}

void
args_wipe (default_args_t *args)
{
        if (!args)
                return;

        loc_wipe (&args->loc);

        loc_wipe (&args->loc2);

        if (args->fd)
                fd_unref (args->fd);

        GF_FREE ((char *)args->linkname);

	GF_FREE (args->vector);

        if (args->iobref)
                iobref_unref (args->iobref);

        if (args->xattr)
                dict_unref (args->xattr);

        if (args->xdata)
                dict_unref (args->xdata);

	GF_FREE ((char *)args->name);

	GF_FREE ((char *)args->volume);

}

void
args_cbk_init (default_args_cbk_t *args_cbk)
{
        INIT_LIST_HEAD (&args_cbk->entries);
}
