/*
  Copyright (c) 2008-2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* libglusterfs/src/defaults.h:
       This file contains definition of default fops and mops functions.
*/

#ifndef _DEFAULT_ARGS_H
#define _DEFAULT_ARGS_H

#include "xlator.h"

int
args_lookup_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     inode_t *inode, struct iatt *buf,
                     dict_t *xdata, struct iatt *postparent);


int
args_stat_cbk_store (default_args_cbk_t *args,
                   int32_t op_ret, int32_t op_errno,
                   struct iatt *buf, dict_t *xdata);

int
args_fstat_cbk_store (default_args_cbk_t *args,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *buf, dict_t *xdata);

int
args_truncate_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata);


int
args_ftruncate_cbk_store (default_args_cbk_t *args,
                        int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                        struct iatt *postbuf, dict_t *xdata);


int
args_access_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata);


int
args_readlink_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno,
                       const char *path, struct iatt *stbuf, dict_t *xdata);

int
args_mknod_cbk_store (default_args_cbk_t *args, int32_t op_ret,
                    int32_t op_errno, inode_t *inode, struct iatt *buf,
                    struct iatt *preparent, struct iatt *postparent,
                    dict_t *xdata);

int
args_mkdir_cbk_store (default_args_cbk_t *args,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata);

int
args_unlink_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *preparent, struct iatt *postparent,
                     dict_t *xdata);

int
args_rmdir_cbk_store (default_args_cbk_t *args,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *preparent, struct iatt *postparent,
                    dict_t *xdata);

int
args_symlink_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno,
                      inode_t *inode, struct iatt *buf,
                      struct iatt *preparent, struct iatt *postparent,
                      dict_t *xdata);


int
args_rename_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf,
                     struct iatt *preoldparent, struct iatt *postoldparent,
                     struct iatt *prenewparent, struct iatt *postnewparent,
                     dict_t *xdata);

int
args_link_cbk_store (default_args_cbk_t *args,
                   int32_t op_ret, int32_t op_errno,
                   inode_t *inode, struct iatt *buf,
                   struct iatt *preparent, struct iatt *postparent,
                   dict_t *xdata);

int
args_create_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     fd_t *fd, inode_t *inode, struct iatt *buf,
                     struct iatt *preparent, struct iatt *postparent,
                     dict_t *xdata);

int
args_open_cbk_store (default_args_cbk_t *args,
                   int32_t op_ret, int32_t op_errno,
                   fd_t *fd, dict_t *xdata);

int
args_readv_cbk_store (default_args_cbk_t *args,
                    int32_t op_ret, int32_t op_errno, struct iovec *vector,
                    int32_t count, struct iatt *stbuf,
                    struct iobref *iobref, dict_t *xdata);

int
args_writev_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata);


int
args_flush_cbk_store (default_args_cbk_t *args,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata);


int
args_fsync_cbk_store (default_args_cbk_t *args,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata);

int
args_opendir_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno,
                      fd_t *fd, dict_t *xdata);

int
args_fsyncdir_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
args_statfs_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     struct statvfs *buf, dict_t *xdata);

int
args_setxattr_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret,
                       int32_t op_errno, dict_t *xdata);

int
args_getxattr_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno,
                       dict_t *dict, dict_t *xdata);

int
args_fsetxattr_cbk_store (default_args_cbk_t *args,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
args_fgetxattr_cbk_store (default_args_cbk_t *args,
                        int32_t op_ret, int32_t op_errno,
                        dict_t *dict, dict_t *xdata);

int
args_removexattr_cbk_store (default_args_cbk_t *args,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
args_fremovexattr_cbk_store (default_args_cbk_t *args,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
args_lk_cbk_store (default_args_cbk_t *args,
                 int32_t op_ret, int32_t op_errno,
                 struct gf_flock *lock, dict_t *xdata);


int
args_inodelk_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
args_finodelk_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
args_entrylk_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
args_fentrylk_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata);


int
args_readdirp_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno,
                       gf_dirent_t *entries, dict_t *xdata);


int
args_readdir_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno,
                      gf_dirent_t *entries, dict_t *xdata);


int
args_rchecksum_cbk_store (default_args_cbk_t *args,
                        int32_t op_ret, int32_t op_errno,
                        uint32_t weak_checksum, uint8_t *strong_checksum,
                        dict_t *xdata);


int
args_xattrop_cbk_store (default_args_cbk_t *args, int32_t op_ret,
                        int32_t op_errno, dict_t *xattr, dict_t *xdata);


int
args_fxattrop_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno,
                       dict_t *xattr, dict_t *xdata);

int
args_setattr_cbk_store (default_args_cbk_t *args,
                      int32_t op_ret, int32_t op_errno,
                      struct iatt *statpre, struct iatt *statpost,
                      dict_t *xdata);


int
args_fsetattr_cbk_store (default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *statpre, struct iatt *statpost,
                       dict_t *xdata);

int
args_fallocate_cbk_store(default_args_cbk_t *args,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *statpre, struct iatt *statpost,
                       dict_t *xdata);

int
args_discard_cbk_store(default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *statpre, struct iatt *statpost,
                     dict_t *xdata);

int
args_zerofill_cbk_store(default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *statpre, struct iatt *statpost,
                     dict_t *xdata);

int
args_ipc_cbk_store (default_args_cbk_t *args,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata);

void
args_cbk_wipe (default_args_cbk_t *args_cbk);

#endif /* _DEFAULT_ARGS_H */
