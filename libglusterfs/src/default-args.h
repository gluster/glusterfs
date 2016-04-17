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

int
args_seek_cbk_store (default_args_cbk_t *args, int32_t op_ret,
                     int32_t op_errno, off_t offset, dict_t *xdata);

void
args_lease_cbk_store (default_args_cbk_t *args,
                     int32_t op_ret, int32_t op_errno,
                     struct gf_lease *lease, dict_t *xdata);

void
args_cbk_wipe (default_args_cbk_t *args_cbk);

void
args_wipe (default_args_t *args);

int
args_lookup_store (default_args_t *args, loc_t *loc,
                   dict_t *xdata);

int
args_stat_store (default_args_t *args, loc_t *loc, dict_t *xdata);

int
args_fstat_store (default_args_t *args, fd_t *fd, dict_t *xdata);

int
args_truncate_store (default_args_t *args, loc_t *loc, off_t off,
                     dict_t *xdata);
int
args_ftruncate_store (default_args_t *args, fd_t *fd, off_t off,
                      dict_t *xdata);

int
args_access_store (default_args_t *args, loc_t *loc, int32_t mask,
                   dict_t *xdata);

int
args_readlink_store (default_args_t *args, loc_t *loc, size_t size,
                     dict_t *xdata);

int
args_mknod_store (default_args_t *args, loc_t *loc, mode_t mode,
                  dev_t rdev, mode_t umask, dict_t *xdata);

int
args_mkdir_store (default_args_t *args, loc_t *loc, mode_t mode,
                  mode_t umask, dict_t *xdata);

int
args_unlink_store (default_args_t *args, loc_t *loc, int xflag, dict_t *xdata);

int
args_rmdir_store (default_args_t *args, loc_t *loc, int flags, dict_t *xdata);

int
args_symlink_store (default_args_t *args, const char *linkname, loc_t *loc,
                   mode_t umask, dict_t *xdata);

int
args_rename_store (default_args_t *args, loc_t *oldloc, loc_t *newloc,
                   dict_t *xdata);

int
args_link_store (default_args_t *args, loc_t *oldloc, loc_t *newloc,
                 dict_t *xdata);

int
args_create_store (default_args_t *args,
                  loc_t *loc, int32_t flags, mode_t mode,
                  mode_t umask, fd_t *fd, dict_t *xdata);

int
args_open_store (default_args_t *args, loc_t *loc, int32_t flags,
                 fd_t *fd, dict_t *xdata);

int
args_readv_store (default_args_t *args, fd_t *fd, size_t size, off_t off,
                  uint32_t flags, dict_t *xdata);

int
args_writev_store (default_args_t *args, fd_t *fd, struct iovec *vector,
                   int32_t count, off_t off, uint32_t flags,
                   struct iobref *iobref, dict_t *xdata);

int
args_flush_store (default_args_t *args, fd_t *fd, dict_t *xdata);

int
args_fsync_store (default_args_t *args, fd_t *fd, int32_t datasync,
                  dict_t *xdata);

int
args_opendir_store (default_args_t *args, loc_t *loc, fd_t *fd, dict_t *xdata);

int
args_fsyncdir_store (default_args_t *args, fd_t *fd, int32_t datasync,
                     dict_t *xdata);

int
args_statfs_store (default_args_t *args, loc_t *loc, dict_t *xdata);

int
args_setxattr_store (default_args_t *args,
                     loc_t *loc, dict_t *dict,
                     int32_t flags, dict_t *xdata);

int
args_getxattr_store (default_args_t *args,
                     loc_t *loc, const char *name, dict_t *xdata);

int
args_fsetxattr_store (default_args_t *args,
                      fd_t *fd, dict_t *dict, int32_t flags, dict_t *xdata);

int
args_fgetxattr_store (default_args_t *args,
                      fd_t *fd, const char *name, dict_t *xdata);

int
args_removexattr_store (default_args_t *args,
                        loc_t *loc, const char *name, dict_t *xdata);

int
args_fremovexattr_store (default_args_t *args,
                         fd_t *fd, const char *name, dict_t *xdata);

int
args_lk_store (default_args_t *args,
               fd_t *fd, int32_t cmd,
                struct gf_flock *lock, dict_t *xdata);

int
args_inodelk_store (default_args_t *args,
                    const char *volume, loc_t *loc, int32_t cmd,
                    struct gf_flock *lock, dict_t *xdata);

int
args_finodelk_store (default_args_t *args,
                     const char *volume, fd_t *fd, int32_t cmd,
                     struct gf_flock *lock, dict_t *xdata);

int
args_entrylk_store (default_args_t *args,
                    const char *volume, loc_t *loc, const char *name,
                    entrylk_cmd cmd, entrylk_type type, dict_t *xdata);

int
args_fentrylk_store (default_args_t *args,
                     const char *volume, fd_t *fd, const char *name,
                     entrylk_cmd cmd, entrylk_type type, dict_t *xdata);
int
args_readdirp_store (default_args_t *args,
                     fd_t *fd, size_t size, off_t off, dict_t *xdata);

int
args_readdir_store (default_args_t *args,
                    fd_t *fd, size_t size,
                    off_t off, dict_t *xdata);

int
args_rchecksum_store (default_args_t *args,
                      fd_t *fd, off_t offset, int32_t len, dict_t *xdata);

int
args_xattrop_store (default_args_t *args,
                    loc_t *loc, gf_xattrop_flags_t optype,
                    dict_t *xattr, dict_t *xdata);

int
args_fxattrop_store (default_args_t *args,
                     fd_t *fd, gf_xattrop_flags_t optype,
                     dict_t *xattr, dict_t *xdata);

int
args_setattr_store (default_args_t *args,
                    loc_t *loc, struct iatt *stbuf,
                    int32_t valid, dict_t *xdata);

int
args_fsetattr_store (default_args_t *args,
                     fd_t *fd, struct iatt *stbuf,
                     int32_t valid, dict_t *xdata);

int
args_fallocate_store (default_args_t *args, fd_t *fd,
                      int32_t mode, off_t offset, size_t len, dict_t *xdata);

int
args_discard_store (default_args_t *args, fd_t *fd,
		    off_t offset, size_t len, dict_t *xdata);

int
args_zerofill_store (default_args_t *args, fd_t *fd,
                     off_t offset, off_t len, dict_t *xdata);

int
args_ipc_store (default_args_t *args,
                int32_t op, dict_t *xdata);

int
args_seek_store (default_args_t *args, fd_t *fd,
                 off_t offset, gf_seek_what_t what, dict_t *xdata);

void
args_lease_store (default_args_t *args, loc_t *loc, struct gf_lease *lease,
                  dict_t *xdata);

int
args_getactivelk_cbk_store (default_args_cbk_t *args,
                             int32_t op_ret, int32_t op_errno,
                             lock_migration_info_t *locklist, dict_t *xdata);

int
args_setactivelk_store (default_args_t *args, loc_t *loc,
                          lock_migration_info_t *locklist, dict_t *xdata);
#endif /* _DEFAULT_ARGS_H */
