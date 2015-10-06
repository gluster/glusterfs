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
args_ipc_cbk_store (default_args_cbk_t *args,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        args->op_ret = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata = dict_ref (xdata);

        return 0;
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
