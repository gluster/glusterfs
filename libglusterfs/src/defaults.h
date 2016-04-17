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

#ifndef _DEFAULTS_H
#define _DEFAULTS_H

#include "xlator.h"

typedef struct {
        int op_ret;
        int op_errno;
        inode_t *inode;
        struct iatt stat;
        struct iatt prestat;
        struct iatt poststat;
        struct iatt preparent;   /* @preoldparent in rename_cbk */
        struct iatt postparent;  /* @postoldparent in rename_cbk */
        struct iatt preparent2;  /* @prenewparent in rename_cbk */
        struct iatt postparent2; /* @postnewparent in rename_cbk */
        const char *buf;
        struct iovec *vector;
        int count;
        struct iobref *iobref;
        fd_t *fd;
        struct statvfs statvfs;
        dict_t *xattr;
        struct gf_flock lock;
        uint32_t weak_checksum;
        uint8_t *strong_checksum;
        dict_t *xdata;
        gf_dirent_t entries;
        off_t offset;            /* seek hole/data */
        int valid; /* If the response is valid or not. For call-stub it is
                      always valid irrespective of this */
        struct gf_lease lease;
        lock_migration_info_t locklist;
} default_args_cbk_t;

typedef struct {
        loc_t loc; /* @old in rename(), link() */
        loc_t loc2; /* @new in rename(), link() */
        fd_t *fd;
        off_t offset;
        int mask;
        size_t size;
        mode_t mode;
        dev_t rdev;
        mode_t umask;
        int xflag;
        int flags;
        const char *linkname;
        struct iovec *vector;
        int count;
        struct iobref *iobref;
        int datasync;
        dict_t *xattr;
        const char *name;
        int cmd;
        struct gf_flock lock;
        const char *volume;
        entrylk_cmd entrylkcmd;
        entrylk_type entrylktype;
        gf_xattrop_flags_t optype;
        int valid;
        struct iatt stat;
        gf_seek_what_t what;
        dict_t *xdata;
        struct gf_lease lease;
        lock_migration_info_t locklist;
} default_args_t;

typedef struct {
        int             fop_enum;
        unsigned int    fop_length;
        int             *enum_list;
        default_args_t  *req_list;
        dict_t          *xdata;
} compound_args_t;

typedef struct {
        int                fop_enum;
        unsigned int       fop_length;
        int                *enum_list;
        default_args_cbk_t *rsp_list;
        dict_t             *xdata;
} compound_args_cbk_t;

int32_t default_notify (xlator_t *this,
                        int32_t event,
                        void *data,
                        ...);

int32_t default_forget (xlator_t *this, inode_t *inode);

int32_t default_release (xlator_t *this, fd_t *fd);

int32_t default_releasedir (xlator_t *this, fd_t *fd);


extern struct xlator_fops *default_fops;

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

int32_t default_ipc (call_frame_t *frame, xlator_t *this, int32_t op,
                     dict_t *xdata);

int32_t default_seek (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      off_t offset, gf_seek_what_t what, dict_t *xdata);

int32_t default_lease (call_frame_t *frame, xlator_t *this, loc_t *loc,
                       struct gf_lease *lease, dict_t *xdata);

int32_t
default_getactivelk (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      dict_t *xdata);

int32_t
default_setactivelk (call_frame_t *frame, xlator_t *this, loc_t *loc,
                       lock_migration_info_t *locklist, dict_t *xdata);

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

int32_t default_ipc_resume (call_frame_t *frame, xlator_t *this,
                            int32_t op, dict_t *xdata);

int32_t default_seek_resume (call_frame_t *frame, xlator_t *this, fd_t *fd,
                             off_t offset, gf_seek_what_t what, dict_t *xdata);

int32_t default_lease_resume (call_frame_t *frame, xlator_t *this, loc_t *loc,
                              struct gf_lease *lease, dict_t *xdata);

/* _cbk_resume */

int32_t
default_lookup_cbk_resume (call_frame_t * frame, void *cookie,
                           xlator_t * this, int32_t op_ret, int32_t op_errno,
                           inode_t * inode, struct iatt *buf, dict_t * xdata,
                           struct iatt *postparent);

int32_t
default_stat_cbk_resume (call_frame_t * frame, void *cookie, xlator_t * this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf,
                         dict_t * xdata);


int32_t
default_truncate_cbk_resume (call_frame_t * frame, void *cookie,
                             xlator_t * this, int32_t op_ret,
                             int32_t op_errno, struct iatt *prebuf,
                             struct iatt *postbuf, dict_t * xdata);

int32_t
default_ftruncate_cbk_resume (call_frame_t * frame, void *cookie,
                              xlator_t * this, int32_t op_ret,
                              int32_t op_errno, struct iatt *prebuf,
                              struct iatt *postbuf, dict_t * xdata);

int32_t
default_access_cbk_resume (call_frame_t * frame, void *cookie,
                           xlator_t * this, int32_t op_ret, int32_t op_errno,
                           dict_t * xdata);

int32_t
default_readlink_cbk_resume (call_frame_t * frame, void *cookie,
                             xlator_t * this, int32_t op_ret,
                             int32_t op_errno, const char *path,
                             struct iatt *buf, dict_t * xdata);


int32_t
default_mknod_cbk_resume (call_frame_t * frame, void *cookie, xlator_t * this,
                          int32_t op_ret, int32_t op_errno, inode_t * inode,
                          struct iatt *buf, struct iatt *preparent,
                          struct iatt *postparent, dict_t * xdata);

int32_t
default_mkdir_cbk_resume (call_frame_t * frame, void *cookie, xlator_t * this,
                          int32_t op_ret, int32_t op_errno, inode_t * inode,
                          struct iatt *buf, struct iatt *preparent,
                          struct iatt *postparent, dict_t * xdata);

int32_t
default_unlink_cbk_resume (call_frame_t * frame, void *cookie,
                           xlator_t * this, int32_t op_ret, int32_t op_errno,
                           struct iatt *preparent, struct iatt *postparent,
                           dict_t * xdata);

int32_t
default_rmdir_cbk_resume (call_frame_t * frame, void *cookie, xlator_t * this,
                          int32_t op_ret, int32_t op_errno,
                          struct iatt *preparent, struct iatt *postparent,
                          dict_t * xdata);


int32_t
default_symlink_cbk_resume (call_frame_t * frame, void *cookie,
                            xlator_t * this, int32_t op_ret, int32_t op_errno,
                            inode_t * inode, struct iatt *buf,
                            struct iatt *preparent, struct iatt *postparent,
                            dict_t * xdata);


int32_t
default_rename_cbk_resume (call_frame_t * frame, void *cookie,
                           xlator_t * this, int32_t op_ret, int32_t op_errno,
                           struct iatt *buf, struct iatt *preoldparent,
                           struct iatt *postoldparent,
                           struct iatt *prenewparent,
                           struct iatt *postnewparent, dict_t * xdata);


int32_t
default_link_cbk_resume (call_frame_t * frame, void *cookie, xlator_t * this,
                         int32_t op_ret, int32_t op_errno, inode_t * inode,
                         struct iatt *buf, struct iatt *preparent,
                         struct iatt *postparent, dict_t * xdata);


int32_t
default_create_cbk_resume (call_frame_t * frame, void *cookie,
                           xlator_t * this, int32_t op_ret, int32_t op_errno,
                           fd_t * fd, inode_t * inode, struct iatt *buf,
                           struct iatt *preparent, struct iatt *postparent,
                           dict_t * xdata);

int32_t
default_open_cbk_resume (call_frame_t * frame, void *cookie, xlator_t * this,
                         int32_t op_ret, int32_t op_errno, fd_t * fd,
                         dict_t * xdata);

int32_t
default_readv_cbk_resume (call_frame_t * frame, void *cookie, xlator_t * this,
                          int32_t op_ret, int32_t op_errno,
                          struct iovec *vector, int32_t count,
                          struct iatt *stbuf, struct iobref *iobref,
                          dict_t * xdata);


int32_t
default_writev_cbk_resume (call_frame_t * frame, void *cookie,
                           xlator_t * this, int32_t op_ret, int32_t op_errno,
                           struct iatt *prebuf, struct iatt *postbuf,
                           dict_t * xdata);


int32_t
default_flush_cbk_resume (call_frame_t * frame, void *cookie, xlator_t * this,
                          int32_t op_ret, int32_t op_errno, dict_t * xdata);



int32_t
default_fsync_cbk_resume (call_frame_t * frame, void *cookie, xlator_t * this,
                          int32_t op_ret, int32_t op_errno,
                          struct iatt *prebuf, struct iatt *postbuf,
                          dict_t * xdata);

int32_t
default_fstat_cbk_resume (call_frame_t * frame, void *cookie, xlator_t * this,
                          int32_t op_ret, int32_t op_errno, struct iatt *buf,
                          dict_t * xdata);

int32_t
default_opendir_cbk_resume (call_frame_t * frame, void *cookie,
                            xlator_t * this, int32_t op_ret, int32_t op_errno,
                            fd_t * fd, dict_t * xdata);

int32_t
default_fsyncdir_cbk_resume (call_frame_t * frame, void *cookie,
                             xlator_t * this, int32_t op_ret,
                             int32_t op_errno, dict_t * xdata);

int32_t
default_statfs_cbk_resume (call_frame_t * frame, void *cookie,
                           xlator_t * this, int32_t op_ret, int32_t op_errno,
                           struct statvfs *buf, dict_t * xdata);


int32_t
default_setxattr_cbk_resume (call_frame_t * frame, void *cookie,
                             xlator_t * this, int32_t op_ret,
                             int32_t op_errno, dict_t * xdata);


int32_t
default_fsetxattr_cbk_resume (call_frame_t * frame, void *cookie,
                              xlator_t * this, int32_t op_ret,
                              int32_t op_errno, dict_t * xdata);



int32_t
default_fgetxattr_cbk_resume (call_frame_t * frame, void *cookie,
                              xlator_t * this, int32_t op_ret,
                              int32_t op_errno, dict_t * dict,
                              dict_t * xdata);


int32_t
default_getxattr_cbk_resume (call_frame_t * frame, void *cookie,
                             xlator_t * this, int32_t op_ret,
                             int32_t op_errno, dict_t * dict, dict_t * xdata);

int32_t
default_xattrop_cbk_resume (call_frame_t * frame, void *cookie,
                            xlator_t * this, int32_t op_ret, int32_t op_errno,
                            dict_t * dict, dict_t * xdata);

int32_t
default_fxattrop_cbk_resume (call_frame_t * frame, void *cookie,
                             xlator_t * this, int32_t op_ret,
                             int32_t op_errno, dict_t * dict, dict_t * xdata);


int32_t
default_removexattr_cbk_resume (call_frame_t * frame, void *cookie,
                                xlator_t * this, int32_t op_ret,
                                int32_t op_errno, dict_t * xdata);

int32_t
default_fremovexattr_cbk_resume (call_frame_t * frame, void *cookie,
                                 xlator_t * this, int32_t op_ret,
                                 int32_t op_errno, dict_t * xdata);

int32_t
default_lk_cbk_resume (call_frame_t * frame, void *cookie, xlator_t * this,
                       int32_t op_ret, int32_t op_errno,
                       struct gf_flock *lock, dict_t * xdata);

int32_t
default_inodelk_cbk_resume (call_frame_t * frame, void *cookie,
                            xlator_t * this, int32_t op_ret, int32_t op_errno,
                            dict_t * xdata);


int32_t
default_finodelk_cbk_resume (call_frame_t * frame, void *cookie,
                             xlator_t * this, int32_t op_ret,
                             int32_t op_errno, dict_t * xdata);

int32_t
default_entrylk_cbk_resume (call_frame_t * frame, void *cookie,
                            xlator_t * this, int32_t op_ret, int32_t op_errno,
                            dict_t * xdata);

int32_t
default_fentrylk_cbk_resume (call_frame_t * frame, void *cookie,
                             xlator_t * this, int32_t op_ret,
                             int32_t op_errno, dict_t * xdata);


int32_t
default_rchecksum_cbk_resume (call_frame_t * frame, void *cookie,
                              xlator_t * this, int32_t op_ret,
                              int32_t op_errno, uint32_t weak_checksum,
                              uint8_t * strong_checksum, dict_t * xdata);


int32_t
default_readdir_cbk_resume (call_frame_t * frame, void *cookie,
                            xlator_t * this, int32_t op_ret, int32_t op_errno,
                            gf_dirent_t * entries, dict_t * xdata);


int32_t
default_readdirp_cbk_resume (call_frame_t * frame, void *cookie,
                             xlator_t * this, int32_t op_ret,
                             int32_t op_errno, gf_dirent_t * entries,
                             dict_t * xdata);

int32_t
default_setattr_cbk_resume (call_frame_t * frame, void *cookie,
                            xlator_t * this, int32_t op_ret, int32_t op_errno,
                            struct iatt *statpre, struct iatt *statpost,
                            dict_t * xdata);

int32_t
default_fsetattr_cbk_resume (call_frame_t * frame, void *cookie,
                             xlator_t * this, int32_t op_ret,
                             int32_t op_errno, struct iatt *statpre,
                             struct iatt *statpost, dict_t * xdata);

int32_t default_fallocate_cbk_resume (call_frame_t * frame, void *cookie,
                                      xlator_t * this, int32_t op_ret,
                                      int32_t op_errno, struct iatt *pre,
                                      struct iatt *post, dict_t * xdata);

int32_t default_discard_cbk_resume (call_frame_t * frame, void *cookie,
                                    xlator_t * this, int32_t op_ret,
                                    int32_t op_errno, struct iatt *pre,
                                    struct iatt *post, dict_t * xdata);

int32_t default_zerofill_cbk_resume (call_frame_t * frame, void *cookie,
                                     xlator_t * this, int32_t op_ret,
                                     int32_t op_errno, struct iatt *pre,
                                     struct iatt *post, dict_t * xdata);

int32_t
default_getspec_cbk_resume (call_frame_t * frame, void *cookie,
                            xlator_t * this, int32_t op_ret, int32_t op_errno,
                            char *spec_data);

int32_t
default_lease_cbk_resume (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno,
                          struct gf_lease *lease, dict_t *xdata);

int32_t
default_getactivelk_resume (call_frame_t *frame, xlator_t *this, loc_t *loc,
                             dict_t *xdata);

int32_t
default_setactivelk_resume (call_frame_t *frame, xlator_t *this, loc_t *loc,
                              lock_migration_info_t *locklist, dict_t *xdata);


/* _CBK */
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

int32_t default_ipc_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata);

int32_t default_seek_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, off_t offset,
                          dict_t *xdata);

int32_t
default_getspec_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, char *spec_data);

int32_t
default_lease_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   struct gf_lease *lease, dict_t *xdata);

int32_t
default_lookup_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_stat_failure_cbk (call_frame_t *frame, int32_t op_errno);


int32_t
default_truncate_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_ftruncate_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_access_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_readlink_failure_cbk (call_frame_t *frame, int32_t op_errno);


int32_t
default_mknod_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_mkdir_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_unlink_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_rmdir_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_symlink_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_rename_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_link_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_create_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_open_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_readv_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_writev_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_flush_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_fsync_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_fstat_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_opendir_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_fsyncdir_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_statfs_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_setxattr_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_fsetxattr_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_fgetxattr_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_getxattr_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_xattrop_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_fxattrop_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_removexattr_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_fremovexattr_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_lk_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_inodelk_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_finodelk_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_entrylk_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_fentrylk_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_rchecksum_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_readdir_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_readdirp_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_setattr_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_fsetattr_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_fallocate_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_discard_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_zerofill_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_getspec_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_seek_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_lease_failure_cbk (call_frame_t *frame, int32_t op_errno);

int32_t
default_getactivelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno,
                          lock_migration_info_t *locklist,
                          dict_t *xdata);

int32_t
default_setactivelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata);

int32_t
default_mem_acct_init (xlator_t *this);
#endif /* _DEFAULTS_H */
