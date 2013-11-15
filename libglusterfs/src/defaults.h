/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* libglusterfs/src/defaults.h:
       This file contains definition of default fops and mops functions.
*/

#ifndef _DEFAULTS_H
#define _DEFAULTS_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"

int32_t default_notify (xlator_t *this,
                        int32_t event,
                        void *data,
                        ...);

int32_t default_forget (xlator_t *this, inode_t *inode);

int32_t default_release (xlator_t *this, fd_t *fd);

int32_t default_releasedir (xlator_t *this, fd_t *fd);


/* Management Operations */

int32_t default_getspec (call_frame_t *frame,
                         xlator_t *this,
                         const char *key,
                         int32_t flag);

int32_t default_rchecksum (call_frame_t *frame,
                           xlator_t *this,
                           fd_t *fd, off_t offset,
                           int32_t len, dict_t *xdata);

/* FileSystem operations */
int32_t default_lookup (call_frame_t *frame,
                        xlator_t *this,
                        loc_t *loc,
                        dict_t *xdata);

int32_t default_stat (call_frame_t *frame,
                      xlator_t *this,
                      loc_t *loc, dict_t *xdata);

int32_t default_fstat (call_frame_t *frame,
                       xlator_t *this,
                       fd_t *fd, dict_t *xdata);

int32_t default_truncate (call_frame_t *frame,
                          xlator_t *this,
                          loc_t *loc,
                          off_t offset, dict_t *xdata);

int32_t default_ftruncate (call_frame_t *frame,
                           xlator_t *this,
                           fd_t *fd,
                           off_t offset, dict_t *xdata);

int32_t default_access (call_frame_t *frame,
                        xlator_t *this,
                        loc_t *loc,
                        int32_t mask, dict_t *xdata);

int32_t default_readlink (call_frame_t *frame,
                          xlator_t *this,
                          loc_t *loc,
                          size_t size, dict_t *xdata);

int32_t default_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
                       mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata);

int32_t default_mkdir (call_frame_t *frame, xlator_t *this,
                       loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata);

int32_t default_unlink (call_frame_t *frame,
                        xlator_t *this,
                        loc_t *loc, int xflag, dict_t *xdata);

int32_t default_rmdir (call_frame_t *frame, xlator_t *this,
                       loc_t *loc, int xflag, dict_t *xdata);

int32_t default_symlink (call_frame_t *frame, xlator_t *this,
                         const char *linkpath, loc_t *loc, mode_t umask,
                         dict_t *xdata);

int32_t default_rename (call_frame_t *frame,
                        xlator_t *this,
                        loc_t *oldloc,
                        loc_t *newloc, dict_t *xdata);

int32_t default_link (call_frame_t *frame,
                      xlator_t *this,
                      loc_t *oldloc,
                      loc_t *newloc, dict_t *xdata);

int32_t default_create (call_frame_t *frame, xlator_t *this,
                        loc_t *loc, int32_t flags, mode_t mode,
                        mode_t umask, fd_t *fd, dict_t *xdata);

int32_t default_open (call_frame_t *frame,
                      xlator_t *this,
                      loc_t *loc,
                      int32_t flags, fd_t *fd,
                      dict_t *xdata);

int32_t default_readv (call_frame_t *frame,
                       xlator_t *this,
                       fd_t *fd,
                       size_t size,
                       off_t offset,
                       uint32_t flags, dict_t *xdata);

int32_t default_writev (call_frame_t *frame,
                        xlator_t *this,
                        fd_t *fd,
                        struct iovec *vector,
                        int32_t count,
                        off_t offset,
                        uint32_t flags,
                        struct iobref *iobref, dict_t *xdata);

int32_t default_flush (call_frame_t *frame,
                       xlator_t *this,
                       fd_t *fd, dict_t *xdata);

int32_t default_fsync (call_frame_t *frame,
                       xlator_t *this,
                       fd_t *fd,
                       int32_t datasync, dict_t *xdata);

int32_t default_opendir (call_frame_t *frame,
                         xlator_t *this,
                         loc_t *loc, fd_t *fd, dict_t *xdata);

int32_t default_fsyncdir (call_frame_t *frame,
                          xlator_t *this,
                          fd_t *fd,
                          int32_t datasync, dict_t *xdata);

int32_t default_statfs (call_frame_t *frame,
                        xlator_t *this,
                        loc_t *loc, dict_t *xdata);

int32_t default_setxattr (call_frame_t *frame,
                          xlator_t *this,
                          loc_t *loc,
                          dict_t *dict,
                          int32_t flags, dict_t *xdata);

int32_t default_getxattr (call_frame_t *frame,
                          xlator_t *this,
                          loc_t *loc,
                          const char *name, dict_t *xdata);

int32_t default_fsetxattr (call_frame_t *frame,
                           xlator_t *this,
                           fd_t *fd,
                           dict_t *dict,
                           int32_t flags, dict_t *xdata);

int32_t default_fgetxattr (call_frame_t *frame,
                           xlator_t *this,
                           fd_t *fd,
                           const char *name, dict_t *xdata);

int32_t default_removexattr (call_frame_t *frame,
                             xlator_t *this,
                             loc_t *loc,
                             const char *name, dict_t *xdata);

int32_t default_fremovexattr (call_frame_t *frame,
                              xlator_t *this,
                              fd_t *fd,
                              const char *name, dict_t *xdata);

int32_t default_lk (call_frame_t *frame,
                    xlator_t *this,
                    fd_t *fd,
                    int32_t cmd,
                    struct gf_flock *flock, dict_t *xdata);

int32_t default_inodelk (call_frame_t *frame, xlator_t *this,
                         const char *volume, loc_t *loc, int32_t cmd,
                         struct gf_flock *flock, dict_t *xdata);

int32_t default_finodelk (call_frame_t *frame, xlator_t *this,
                          const char *volume, fd_t *fd, int32_t cmd,
                          struct gf_flock *flock, dict_t *xdata);

int32_t default_entrylk (call_frame_t *frame, xlator_t *this,
                         const char *volume, loc_t *loc, const char *basename,
                         entrylk_cmd cmd, entrylk_type type, dict_t *xdata);

int32_t default_fentrylk (call_frame_t *frame, xlator_t *this,
                          const char *volume, fd_t *fd, const char *basename,
                          entrylk_cmd cmd, entrylk_type type, dict_t *xdata);

int32_t default_readdir (call_frame_t *frame,
                          xlator_t *this,
                          fd_t *fd,
                          size_t size, off_t off, dict_t *xdata);

int32_t default_readdirp (call_frame_t *frame,
                          xlator_t *this,
                          fd_t *fd,
                          size_t size, off_t off, dict_t *xdata);

int32_t default_xattrop (call_frame_t *frame,
                         xlator_t *this,
                         loc_t *loc,
                         gf_xattrop_flags_t flags,
                         dict_t *dict, dict_t *xdata);

int32_t default_fxattrop (call_frame_t *frame,
                          xlator_t *this,
                          fd_t *fd,
                          gf_xattrop_flags_t flags,
                          dict_t *dict, dict_t *xdata);

int32_t default_setattr (call_frame_t *frame,
                         xlator_t *this,
                         loc_t *loc,
                         struct iatt *stbuf,
                         int32_t valid, dict_t *xdata);

int32_t default_fsetattr (call_frame_t *frame,
                          xlator_t *this,
                          fd_t *fd,
                          struct iatt *stbuf,
                          int32_t valid, dict_t *xdata);

int32_t default_fallocate(call_frame_t *frame,
			  xlator_t *this,
			  fd_t *fd,
			  int32_t keep_size, off_t offset,
			  size_t len, dict_t *xdata);

int32_t default_discard(call_frame_t *frame,
			xlator_t *this,
			fd_t *fd,
			off_t offset,
			size_t len, dict_t *xdata);

int32_t default_zerofill(call_frame_t *frame,
                        xlator_t *this,
                        fd_t *fd,
                        off_t offset,
                        off_t len, dict_t *xdata);


/* Resume */
int32_t default_getspec_resume (call_frame_t *frame,
                                xlator_t *this,
                                const char *key,
                                int32_t flag);

int32_t default_rchecksum_resume (call_frame_t *frame,
                           xlator_t *this,
                           fd_t *fd, off_t offset,
                           int32_t len, dict_t *xdata);

/* FileSystem operations */
int32_t default_lookup_resume (call_frame_t *frame,
                               xlator_t *this,
                               loc_t *loc,
                               dict_t *xdata);

int32_t default_stat_resume (call_frame_t *frame,
                      xlator_t *this,
                      loc_t *loc, dict_t *xdata);

int32_t default_fstat_resume (call_frame_t *frame,
                       xlator_t *this,
                       fd_t *fd, dict_t *xdata);

int32_t default_truncate_resume (call_frame_t *frame,
                          xlator_t *this,
                          loc_t *loc,
                          off_t offset, dict_t *xdata);

int32_t default_ftruncate_resume (call_frame_t *frame,
                           xlator_t *this,
                           fd_t *fd,
                           off_t offset, dict_t *xdata);

int32_t default_access_resume (call_frame_t *frame,
                        xlator_t *this,
                        loc_t *loc,
                        int32_t mask, dict_t *xdata);

int32_t default_readlink_resume (call_frame_t *frame,
                          xlator_t *this,
                          loc_t *loc,
                          size_t size, dict_t *xdata);

int32_t default_mknod_resume (call_frame_t *frame, xlator_t *this, loc_t *loc,
                              mode_t mode, dev_t rdev, mode_t umask,
                              dict_t *xdata);

int32_t default_mkdir_resume (call_frame_t *frame, xlator_t *this, loc_t *loc,
                              mode_t mode, mode_t umask, dict_t *xdata);

int32_t default_unlink_resume (call_frame_t *frame,
                               xlator_t *this,
                               loc_t *loc, int xflag, dict_t *xdata);

int32_t default_rmdir_resume (call_frame_t *frame, xlator_t *this,
                              loc_t *loc, int xflag, dict_t *xdata);

int32_t default_symlink_resume (call_frame_t *frame, xlator_t *this,
                                const char *linkpath, loc_t *loc, mode_t umask,
                                dict_t *xdata);

int32_t default_rename_resume (call_frame_t *frame,
                        xlator_t *this,
                        loc_t *oldloc,
                        loc_t *newloc, dict_t *xdata);

int32_t default_link_resume (call_frame_t *frame,
                      xlator_t *this,
                      loc_t *oldloc,
                      loc_t *newloc, dict_t *xdata);

int32_t default_create_resume (call_frame_t *frame, xlator_t *this,
                               loc_t *loc, int32_t flags, mode_t mode,
                               mode_t umask, fd_t *fd, dict_t *xdata);

int32_t default_open_resume (call_frame_t *frame,
                             xlator_t *this,
                             loc_t *loc,
                             int32_t flags, fd_t *fd, dict_t *xdata);

int32_t default_readv_resume (call_frame_t *frame,
                              xlator_t *this,
                              fd_t *fd,
                              size_t size,
                              off_t offset, uint32_t flags, dict_t *xdata);

int32_t default_writev_resume (call_frame_t *frame,
                               xlator_t *this,
                               fd_t *fd,
                               struct iovec *vector,
                               int32_t count,
                               off_t offset, uint32_t flags,
                               struct iobref *iobref, dict_t *xdata);

int32_t default_flush_resume (call_frame_t *frame,
                       xlator_t *this,
                       fd_t *fd, dict_t *xdata);

int32_t default_fsync_resume (call_frame_t *frame,
                       xlator_t *this,
                       fd_t *fd,
                       int32_t datasync, dict_t *xdata);

int32_t default_opendir_resume (call_frame_t *frame,
                         xlator_t *this,
                         loc_t *loc, fd_t *fd, dict_t *xdata);

int32_t default_fsyncdir_resume (call_frame_t *frame,
                          xlator_t *this,
                          fd_t *fd,
                          int32_t datasync, dict_t *xdata);

int32_t default_statfs_resume (call_frame_t *frame,
                        xlator_t *this,
                        loc_t *loc, dict_t *xdata);

int32_t default_setxattr_resume (call_frame_t *frame,
                          xlator_t *this,
                          loc_t *loc,
                          dict_t *dict,
                          int32_t flags, dict_t *xdata);

int32_t default_getxattr_resume (call_frame_t *frame,
                          xlator_t *this,
                          loc_t *loc,
                          const char *name, dict_t *xdata);

int32_t default_fsetxattr_resume (call_frame_t *frame,
                           xlator_t *this,
                           fd_t *fd,
                           dict_t *dict,
                           int32_t flags, dict_t *xdata);

int32_t default_fgetxattr_resume (call_frame_t *frame,
                           xlator_t *this,
                           fd_t *fd,
                           const char *name, dict_t *xdata);

int32_t default_removexattr_resume (call_frame_t *frame,
                             xlator_t *this,
                             loc_t *loc,
                             const char *name, dict_t *xdata);

int32_t default_fremovexattr_resume (call_frame_t *frame,
                                     xlator_t *this,
                                     fd_t *fd,
                                     const char *name, dict_t *xdata);

int32_t default_lk_resume (call_frame_t *frame,
                    xlator_t *this,
                    fd_t *fd,
                    int32_t cmd,
                    struct gf_flock *flock, dict_t *xdata);

int32_t default_inodelk_resume (call_frame_t *frame, xlator_t *this,
                         const char *volume, loc_t *loc, int32_t cmd,
                         struct gf_flock *flock, dict_t *xdata);

int32_t default_finodelk_resume (call_frame_t *frame, xlator_t *this,
                          const char *volume, fd_t *fd, int32_t cmd,
                          struct gf_flock *flock, dict_t *xdata);

int32_t default_entrylk_resume (call_frame_t *frame, xlator_t *this,
                         const char *volume, loc_t *loc, const char *basename,
                         entrylk_cmd cmd, entrylk_type type, dict_t *xdata);

int32_t default_fentrylk_resume (call_frame_t *frame, xlator_t *this,
                          const char *volume, fd_t *fd, const char *basename,
                          entrylk_cmd cmd, entrylk_type type, dict_t *xdata);

int32_t default_readdir_resume (call_frame_t *frame,
                          xlator_t *this,
                          fd_t *fd,
                          size_t size, off_t off, dict_t *xdata);

int32_t default_readdirp_resume (call_frame_t *frame,
                                 xlator_t *this,
                                 fd_t *fd,
                                 size_t size, off_t off, dict_t *xdata);

int32_t default_xattrop_resume (call_frame_t *frame,
                         xlator_t *this,
                         loc_t *loc,
                         gf_xattrop_flags_t flags,
                         dict_t *dict, dict_t *xdata);

int32_t default_fxattrop_resume (call_frame_t *frame,
                          xlator_t *this,
                          fd_t *fd,
                          gf_xattrop_flags_t flags,
                          dict_t *dict, dict_t *xdata);
int32_t default_rchecksum_resume (call_frame_t *frame,
                                  xlator_t *this,
                                  fd_t *fd, off_t offset,
                                  int32_t len, dict_t *xdata);

int32_t default_setattr_resume (call_frame_t *frame,
                         xlator_t *this,
                         loc_t *loc,
                         struct iatt *stbuf,
                         int32_t valid, dict_t *xdata);

int32_t default_fsetattr_resume (call_frame_t *frame,
                          xlator_t *this,
                          fd_t *fd,
                          struct iatt *stbuf,
                          int32_t valid, dict_t *xdata);

int32_t default_fallocate_resume(call_frame_t *frame,
				 xlator_t *this,
				 fd_t *fd,
				 int32_t keep_size, off_t offset,
				 size_t len, dict_t *xdata);

int32_t default_discard_resume(call_frame_t *frame,
			       xlator_t *this,
			       fd_t *fd,
			       off_t offset,
			       size_t len, dict_t *xdata);

int32_t default_zerofill_resume(call_frame_t *frame,
                               xlator_t *this,
                               fd_t *fd,
                               off_t offset,
                               off_t len, dict_t *xdata);


/* _cbk */

int32_t
default_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, dict_t *xdata, struct iatt *postparent);

int32_t
default_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata);


int32_t
default_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata);

int32_t
default_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata);

int32_t
default_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata);

int32_t
default_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, const char *path,
                      struct iatt *buf, dict_t *xdata);


int32_t
default_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata);

int32_t
default_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata);

int32_t
default_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata);

int32_t
default_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata);


int32_t
default_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata);


int32_t
default_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf,
                    struct iatt *preoldparent, struct iatt *postoldparent,
                    struct iatt *prenewparent, struct iatt *postnewparent, dict_t *xdata);


int32_t
default_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata);


int32_t
default_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata);

int32_t
default_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata);

int32_t
default_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iovec *vector,
                   int32_t count, struct iatt *stbuf, struct iobref *iobref, dict_t *xdata);


int32_t
default_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf, dict_t *xdata);


int32_t
default_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xdata);



int32_t
default_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata);

int32_t
default_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata);

int32_t
default_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata);

int32_t
default_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata);

int32_t
default_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct statvfs *buf, dict_t *xdata);


int32_t
default_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata);


int32_t
default_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata);



int32_t
default_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata);


int32_t
default_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata);

int32_t
default_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata);

int32_t
default_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata);


int32_t
default_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata);

int32_t
default_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata);

int32_t
default_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct gf_flock *lock, dict_t *xdata);

int32_t
default_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata);


int32_t
default_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata);

int32_t
default_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata);

int32_t
default_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata);


int32_t
default_rchecksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, uint32_t weak_checksum,
                       uint8_t *strong_checksum, dict_t *xdata);


int32_t
default_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, gf_dirent_t *entries, dict_t *xdata);


int32_t
default_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, gf_dirent_t *entries, dict_t *xdata);

int32_t
default_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                     struct iatt *statpost, dict_t *xdata);

int32_t
default_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                      struct iatt *statpost, dict_t *xdata);

int32_t default_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
			      int32_t op_ret, int32_t op_errno, struct iatt *pre,
			      struct iatt *post, dict_t *xdata);

int32_t default_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
			    int32_t op_ret, int32_t op_errno, struct iatt *pre,
			    struct iatt *post, dict_t *xdata);

int32_t default_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, struct iatt *pre,
                            struct iatt *post, dict_t *xdata);

int32_t
default_getspec_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, char *spec_data);

int32_t
default_mem_acct_init (xlator_t *this);

#endif /* _DEFAULTS_H */
