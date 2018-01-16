/*
  Copyright (c); 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later);, or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __CLIENT_COMMON_H__
#define __CLIENT_COMMON_H__

#include "dict.h"
#include "xlator.h"
#include "rpc-common-xdr.h"
#include "glusterfs3-xdr.h"
#include "glusterfs4-xdr.h"
#include "glusterfs3.h"
#include "client.h"

int
client_pre_stat (xlator_t *this, gfs3_stat_req *req, loc_t *loc,
                 dict_t *xdata);

int
client_pre_readlink (xlator_t *this, gfs3_readlink_req *req, loc_t *loc,
                     size_t size, dict_t *xdata);

int
client_pre_mknod (xlator_t *this, gfs3_mknod_req *req, loc_t *loc,
                   mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata);

int
client_pre_mkdir (xlator_t *this, gfs3_mkdir_req *req, loc_t *loc,
                   mode_t mode, mode_t umask, dict_t *xdata);

int
client_pre_unlink (xlator_t *this, gfs3_unlink_req *req, loc_t *loc,
                    int32_t flags, dict_t *xdata);

int
client_pre_rmdir (xlator_t *this, gfs3_rmdir_req *req, loc_t *loc,
                   int32_t flags, dict_t *xdata);

int
client_pre_symlink (xlator_t *this, gfs3_symlink_req *req, loc_t *loc,
                     const char *linkname, mode_t umask, dict_t *xdata);

int
client_pre_rename (xlator_t *this, gfs3_rename_req *req, loc_t *oldloc,
                   loc_t *newloc, dict_t *xdata);

int
client_pre_link (xlator_t *this,
                  gfs3_link_req *req, loc_t *oldloc, loc_t *newloc,
                  dict_t *xdata);

int
client_pre_truncate (xlator_t *this, gfs3_truncate_req *req,
                     loc_t *loc, off_t offset, dict_t *xdata);

int
client_pre_open (xlator_t *this, gfs3_open_req *req, loc_t *loc, fd_t *fd,
                  int32_t flags, dict_t *xdata);

int
client_pre_readv (xlator_t *this, gfs3_read_req *req, fd_t *fd, size_t size,
                  off_t offset, int32_t flags, dict_t *xdata);

int
client_pre_writev (xlator_t *this, gfs3_write_req *req,
                   fd_t *fd, size_t size, off_t offset, int32_t flags,
                   dict_t **xdata);

int
client_pre_statfs (xlator_t *this, gfs3_statfs_req *req, loc_t *loc,
                   dict_t *xdata);

int
client_pre_flush (xlator_t *this, gfs3_flush_req *req, fd_t *fd, dict_t *xdata);

int
client_pre_fsync (xlator_t *this, gfs3_fsync_req *req, fd_t *fd,
                   int32_t flags, dict_t *xdata);

int
client_pre_setxattr (xlator_t *this, gfs3_setxattr_req *req, loc_t *loc,
                      dict_t *xattr, int32_t flags, dict_t *xdata);

int
client_pre_getxattr (xlator_t *this, gfs3_getxattr_req *req, loc_t *loc,
                     const char *name, dict_t *xdata);

int
client_pre_removexattr (xlator_t *this, gfs3_removexattr_req *req,
                         loc_t *loc, const char *name, dict_t *xdata);

int
client_pre_opendir (xlator_t *this,
                    gfs3_opendir_req *req, loc_t *loc,
                    fd_t *fd, dict_t *xdata);

int
client_pre_fsyncdir (xlator_t *this, gfs3_fsyncdir_req *req, fd_t *fd,
                      int32_t flags, dict_t *xdata);

int
client_pre_access (xlator_t *this, gfs3_access_req *req, loc_t *loc,
                   int32_t mask, dict_t *xdata);

int
client_pre_create (xlator_t *this, gfs3_create_req *req,
                    loc_t *loc, fd_t *fd, mode_t mode,
                    int32_t flags, mode_t umask, dict_t *xdata);

int
client_pre_ftruncate (xlator_t *this, gfs3_ftruncate_req *req, fd_t *fd,
                       off_t offset, dict_t *xdata);

int
client_pre_fstat (xlator_t *this, gfs3_fstat_req *req, fd_t *fd,
                   dict_t *xdata);

int
client_pre_lk (xlator_t *this, gfs3_lk_req *req,
               int32_t cmd, struct gf_flock *flock, fd_t *fd, dict_t *xdata);

int
client_pre_lookup (xlator_t *this, gfs3_lookup_req *req, loc_t *loc,
                   dict_t *xdata);

int
client_pre_readdir (xlator_t *this, gfs3_readdir_req *req, fd_t *fd,
                    size_t size, off_t offset, dict_t *xdata);

int
client_pre_inodelk (xlator_t *this, gfs3_inodelk_req *req, loc_t *loc,
                     int cmd, struct gf_flock *flock, const char *volume,
                     dict_t *xdata);

int
client_pre_finodelk (xlator_t *this, gfs3_finodelk_req *req, fd_t *fd,
                     int cmd, struct gf_flock *flock, const char *volume,
                     dict_t *xdata);

int
client_pre_entrylk (xlator_t *this, gfs3_entrylk_req *req, loc_t *loc,
                     entrylk_cmd cmd_entrylk, entrylk_type type,
                     const char *volume, const char *basename, dict_t *xdata);

int
client_pre_fentrylk (xlator_t *this, gfs3_fentrylk_req *req, fd_t *fd,
                      entrylk_cmd cmd_entrylk, entrylk_type type,
                      const char *volume, const char *basename, dict_t *xdata);

int
client_pre_xattrop (xlator_t *this, gfs3_xattrop_req *req, loc_t *loc,
                    dict_t *xattr, int32_t flags, dict_t *xdata);

int
client_pre_fxattrop (xlator_t *this, gfs3_fxattrop_req *req, fd_t *fd,
                    dict_t *xattr, int32_t flags, dict_t *xdata);

int
client_pre_fgetxattr (xlator_t *this, gfs3_fgetxattr_req *req, fd_t *fd,
                      const char *name, dict_t *xdata);

int
client_pre_fsetxattr (xlator_t *this, gfs3_fsetxattr_req *req, fd_t *fd,
                       int32_t flags, dict_t *xattr, dict_t *xdata);
int
client_pre_seek (xlator_t *this, gfs3_seek_req *req, fd_t *fd,
                 off_t offset, gf_seek_what_t what, dict_t *xdata);

int
client_pre_rchecksum (xlator_t *this, gfs3_rchecksum_req *req, fd_t *fd,
                       int32_t len, off_t offset, dict_t *xdata);

int
client_pre_setattr (xlator_t *this, gfs3_setattr_req *req, loc_t *loc,
                     int32_t valid, struct iatt *stbuf, dict_t *xdata);
int
client_pre_fsetattr (xlator_t *this, gfs3_fsetattr_req *req, fd_t *fd,
                     int32_t valid, struct iatt *stbuf, dict_t *xdata);

int
client_pre_readdirp (xlator_t *this, gfs3_readdirp_req *req, fd_t *fd,
                     size_t size, off_t offset, dict_t *xdata);

int
client_pre_fremovexattr (xlator_t *this, gfs3_fremovexattr_req *req, fd_t *fd,
                          const char *name, dict_t *xdata);

int
client_pre_fallocate (xlator_t *this, gfs3_fallocate_req *req, fd_t *fd,
                      int32_t flags, off_t offset, size_t size, dict_t *xdata);
int
client_pre_discard (xlator_t *this, gfs3_discard_req *req, fd_t *fd,
                    off_t offset, size_t size, dict_t *xdata);
int
client_pre_zerofill (xlator_t *this, gfs3_zerofill_req *req, fd_t *fd,
                     off_t offset, size_t size, dict_t *xdata);
int
client_pre_ipc (xlator_t *this, gfs3_ipc_req *req, int32_t cmd,
                 dict_t *xdata);

int
client_pre_lease (xlator_t *this, gfs3_lease_req *req, loc_t *loc,
                  struct gf_lease *lease, dict_t *xdata);

int
client_post_stat (xlator_t *this, gfs3_stat_rsp *rsp, struct iatt *iatt,
                  dict_t **xdata);

int
client_post_readlink (xlator_t *this, gfs3_readlink_rsp *rsp,
                      struct iatt *iatt, dict_t **xdata);

int
client_post_mknod (xlator_t *this, gfs3_mknod_rsp *rsp, struct iatt *stbuf,
                   struct iatt *preparent, struct iatt *postparent,
                   dict_t **xdata);

int
client_post_mkdir (xlator_t *this, gfs3_mkdir_rsp *rsp, struct iatt *stbuf,
                   struct iatt *preparent, struct iatt *postparent,
                   dict_t **xdata);

int
client_post_unlink (xlator_t *this, gfs3_unlink_rsp *rsp,
                   struct iatt *preparent, struct iatt *postparent,
                   dict_t **xdata);

int
client_post_rmdir (xlator_t *this, gfs3_rmdir_rsp *rsp,
                   struct iatt *preparent, struct iatt *postparent,
                   dict_t **xdata);

int
client_post_symlink (xlator_t *this, gfs3_symlink_rsp *rsp, struct iatt *stbuf,
                     struct iatt *preparent, struct iatt *postparent,
                     dict_t **xdata);

int
client_post_rename (xlator_t *this, gfs3_rename_rsp *rsp, struct iatt *stbuf,
                   struct iatt *preoldparent, struct iatt *postoldparent,
                   struct iatt *prenewparent, struct iatt *postnewparent,
                   dict_t **xdata);
int
client_post_link (xlator_t *this, gfs3_link_rsp *rsp, struct iatt *stbuf,
                  struct iatt *preparent, struct iatt *postparent,
                  dict_t **xdata);

int
client_post_truncate (xlator_t *this, gfs3_truncate_rsp *rsp,
                      struct iatt *prestat, struct iatt *poststat,
                      dict_t **xdata);

int
client_post_open (xlator_t *this, gfs3_open_rsp *rsp, dict_t **xdata);

int
client_post_readv (xlator_t *this, gfs3_read_rsp *rsp, struct iobref **iobref,
                   struct iobref *rsp_iobref, struct iatt *stat,
                   struct iovec *vector, struct iovec *rsp_vector,
                   int *rspcount, dict_t **xdata);

int
client_post_writev (xlator_t *this, gfs3_write_rsp *rsp, struct iatt *prestat,
                    struct iatt *poststat, dict_t **xdata);

int
client_post_statfs (xlator_t *this, gfs3_statfs_rsp *rsp,
                    struct statvfs *statfs, dict_t **xdata);

int
client_post_flush (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata);

int
client_post_fsync (xlator_t *this, gfs3_fsync_rsp *rsp,
                   struct iatt *prestat, struct iatt *poststat,
                   dict_t **xdata);
int
client_post_setxattr (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata);

int
client_post_getxattr (xlator_t *this, gfs3_getxattr_rsp *rsp, dict_t **dict,
                      dict_t **xdata);

int
client_post_removexattr (xlator_t *this, gf_common_rsp *rsp,
                         dict_t **xdata);

int
client_post_opendir (xlator_t *this, gfs3_opendir_rsp *rsp, dict_t **xdata);

int
client_post_fsyncdir (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata);

int
client_post_access (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata);

int
client_post_create (xlator_t *this, gfs3_create_rsp *rsp,
                    struct iatt *stbuf, struct iatt *preparent,
                    struct iatt *postparent,
                    clnt_local_t *local, dict_t **xdata);

int
client_post_ftruncate (xlator_t *this, gfs3_ftruncate_rsp *rsp,
                       struct iatt *prestat, struct iatt *poststat,
                       dict_t **xdata);

int
client_post_fstat (xlator_t *this, gfs3_fstat_rsp *rsp, struct iatt *stat,
                   dict_t **xdata);

int
client_post_lk (xlator_t *this, gfs3_lk_rsp *rsp, struct gf_flock *lock,
                dict_t **xdata);

int
client_post_lookup (xlator_t *this, gfs3_lookup_rsp *rsp, struct iatt *stbuf,
                    struct iatt *postparent, dict_t **xdata);

int
client_post_readdir (xlator_t *this, gfs3_readdir_rsp *rsp,
                     gf_dirent_t *entries, dict_t **xdata);

int
client_post_inodelk (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata);

int
client_post_finodelk (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata);

int
client_post_entrylk (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata);

int
client_post_fentrylk (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata);

int
client_post_xattrop (xlator_t *this, gfs3_xattrop_rsp *rsp, dict_t **dict,
                      dict_t **xdata);

int
client_post_fxattrop (xlator_t *this, gfs3_fxattrop_rsp *rsp, dict_t **dict,
                      dict_t **xdata);

int
client_post_fgetxattr (xlator_t *this, gfs3_fgetxattr_rsp *rsp, dict_t **dict,
                      dict_t **xdata);

int
client_post_fsetxattr (xlator_t *this, gf_common_rsp *rsp, dict_t **xdata);

int
client_post_rchecksum (xlator_t *this, gfs3_rchecksum_rsp *rsp, dict_t **xdata);

int
client_post_setattr (xlator_t *this, gfs3_setattr_rsp *rsp,
                     struct iatt *prestat, struct iatt *poststat,
                     dict_t **xdata);

int
client_post_fsetattr (xlator_t *this, gfs3_fsetattr_rsp *rsp,
                      struct iatt *prestat, struct iatt *poststat,
                      dict_t **xdata);

int
client_post_readdirp (xlator_t *this, gfs3_readdirp_rsp *rsp,
                      fd_t *fd, gf_dirent_t *entries,
                      dict_t **xdata);

int
client_post_fremovexattr (xlator_t *this, gf_common_rsp *rsp,
                          dict_t **xdata);

int
client_post_fallocate (xlator_t *this, gfs3_fallocate_rsp *rsp,
                   struct iatt *prestat, struct iatt *poststat,
                   dict_t **xdata);

int
client_post_discard (xlator_t *this, gfs3_discard_rsp *rsp,
                     struct iatt *prestat,
                     struct iatt *poststat, dict_t **xdata);

int
client_post_zerofill (xlator_t *this, gfs3_zerofill_rsp *rsp,
                     struct iatt *prestat, struct iatt *poststat,
                     dict_t **xdata);

int
client_post_ipc (xlator_t *this, gfs3_ipc_rsp *rsp, dict_t **xdata);

int
client_post_seek (xlator_t *this, gfs3_seek_rsp *rsp, dict_t **xdata);

int
client_post_lease (xlator_t *this, gfs3_lease_rsp *rsp, struct gf_lease *lease,
                   dict_t **xdata);

/* New functions for version 4 */
int
client_post_common_dict (xlator_t *this, gfx_common_dict_rsp *rsp, dict_t **dict,
                         dict_t **xdata);
int
client_post_common_3iatt (xlator_t *this, gfx_common_3iatt_rsp *rsp, struct iatt *iatt,
                          struct iatt *iatt2, struct iatt *iatt3, dict_t **xdata);
int
client_post_common_2iatt (xlator_t *this, gfx_common_2iatt_rsp *rsp, struct iatt *iatt,
                          struct iatt *iatt2, dict_t **xdata);
int
client_post_common_iatt (xlator_t *this, gfx_common_iatt_rsp *rsp, struct iatt *iatt,
                         dict_t **xdata);
int
client_post_common_rsp (xlator_t *this, gfx_common_rsp *rsp, dict_t **xdata);


int
client_pre_stat_v2 (xlator_t *this, gfx_stat_req *req, loc_t *loc,
                 dict_t *xdata);

int
client_pre_readlink_v2 (xlator_t *this, gfx_readlink_req *req, loc_t *loc,
                     size_t size, dict_t *xdata);

int
client_pre_mknod_v2 (xlator_t *this, gfx_mknod_req *req, loc_t *loc,
                   mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata);

int
client_pre_mkdir_v2 (xlator_t *this, gfx_mkdir_req *req, loc_t *loc,
                   mode_t mode, mode_t umask, dict_t *xdata);

int
client_pre_unlink_v2 (xlator_t *this, gfx_unlink_req *req, loc_t *loc,
                    int32_t flags, dict_t *xdata);

int
client_pre_rmdir_v2 (xlator_t *this, gfx_rmdir_req *req, loc_t *loc,
                   int32_t flags, dict_t *xdata);

int
client_pre_symlink_v2 (xlator_t *this, gfx_symlink_req *req, loc_t *loc,
                     const char *linkname, mode_t umask, dict_t *xdata);

int
client_pre_rename_v2 (xlator_t *this, gfx_rename_req *req, loc_t *oldloc,
                   loc_t *newloc, dict_t *xdata);

int
client_pre_link_v2 (xlator_t *this,
                  gfx_link_req *req, loc_t *oldloc, loc_t *newloc,
                  dict_t *xdata);

int
client_pre_truncate_v2 (xlator_t *this, gfx_truncate_req *req,
                     loc_t *loc, off_t offset, dict_t *xdata);

int
client_pre_open_v2 (xlator_t *this, gfx_open_req *req, loc_t *loc, fd_t *fd,
                  int32_t flags, dict_t *xdata);

int
client_pre_readv_v2 (xlator_t *this, gfx_read_req *req, fd_t *fd, size_t size,
                  off_t offset, int32_t flags, dict_t *xdata);

int
client_pre_writev_v2 (xlator_t *this, gfx_write_req *req,
                   fd_t *fd, size_t size, off_t offset, int32_t flags,
                   dict_t **xdata);

int
client_pre_statfs_v2 (xlator_t *this, gfx_statfs_req *req, loc_t *loc,
                   dict_t *xdata);

int
client_pre_flush_v2 (xlator_t *this, gfx_flush_req *req, fd_t *fd, dict_t *xdata);

int
client_pre_fsync_v2 (xlator_t *this, gfx_fsync_req *req, fd_t *fd,
                   int32_t flags, dict_t *xdata);

int
client_pre_setxattr_v2 (xlator_t *this, gfx_setxattr_req *req, loc_t *loc,
                      dict_t *xattr, int32_t flags, dict_t *xdata);

int
client_pre_getxattr_v2 (xlator_t *this, gfx_getxattr_req *req, loc_t *loc,
                     const char *name, dict_t *xdata);

int
client_pre_removexattr_v2 (xlator_t *this, gfx_removexattr_req *req,
                         loc_t *loc, const char *name, dict_t *xdata);

int
client_pre_opendir_v2 (xlator_t *this,
                    gfx_opendir_req *req, loc_t *loc,
                    fd_t *fd, dict_t *xdata);

int
client_pre_fsyncdir_v2 (xlator_t *this, gfx_fsyncdir_req *req, fd_t *fd,
                      int32_t flags, dict_t *xdata);

int
client_pre_access_v2 (xlator_t *this, gfx_access_req *req, loc_t *loc,
                   int32_t mask, dict_t *xdata);

int
client_pre_create_v2 (xlator_t *this, gfx_create_req *req,
                    loc_t *loc, fd_t *fd, mode_t mode,
                    int32_t flags, mode_t umask, dict_t *xdata);

int
client_pre_ftruncate_v2 (xlator_t *this, gfx_ftruncate_req *req, fd_t *fd,
                       off_t offset, dict_t *xdata);

int
client_pre_fstat_v2 (xlator_t *this, gfx_fstat_req *req, fd_t *fd,
                   dict_t *xdata);

int
client_pre_lk_v2 (xlator_t *this, gfx_lk_req *req,
               int32_t cmd, struct gf_flock *flock, fd_t *fd, dict_t *xdata);

int
client_pre_lookup_v2 (xlator_t *this, gfx_lookup_req *req, loc_t *loc,
                   dict_t *xdata);

int
client_pre_readdir_v2 (xlator_t *this, gfx_readdir_req *req, fd_t *fd,
                    size_t size, off_t offset, dict_t *xdata);

int
client_pre_inodelk_v2 (xlator_t *this, gfx_inodelk_req *req, loc_t *loc,
                     int cmd, struct gf_flock *flock, const char *volume,
                     dict_t *xdata);

int
client_pre_finodelk_v2 (xlator_t *this, gfx_finodelk_req *req, fd_t *fd,
                     int cmd, struct gf_flock *flock, const char *volume,
                     dict_t *xdata);

int
client_pre_entrylk_v2 (xlator_t *this, gfx_entrylk_req *req, loc_t *loc,
                     entrylk_cmd cmd_entrylk, entrylk_type type,
                     const char *volume, const char *basename, dict_t *xdata);

int
client_pre_fentrylk_v2 (xlator_t *this, gfx_fentrylk_req *req, fd_t *fd,
                      entrylk_cmd cmd_entrylk, entrylk_type type,
                      const char *volume, const char *basename, dict_t *xdata);

int
client_pre_xattrop_v2 (xlator_t *this, gfx_xattrop_req *req, loc_t *loc,
                    dict_t *xattr, int32_t flags, dict_t *xdata);

int
client_pre_fxattrop_v2 (xlator_t *this, gfx_fxattrop_req *req, fd_t *fd,
                    dict_t *xattr, int32_t flags, dict_t *xdata);

int
client_pre_fgetxattr_v2 (xlator_t *this, gfx_fgetxattr_req *req, fd_t *fd,
                      const char *name, dict_t *xdata);

int
client_pre_fsetxattr_v2 (xlator_t *this, gfx_fsetxattr_req *req, fd_t *fd,
                       int32_t flags, dict_t *xattr, dict_t *xdata);
int
client_pre_seek_v2 (xlator_t *this, gfx_seek_req *req, fd_t *fd,
                 off_t offset, gf_seek_what_t what, dict_t *xdata);

int
client_pre_rchecksum_v2 (xlator_t *this, gfx_rchecksum_req *req, fd_t *fd,
                       int32_t len, off_t offset, dict_t *xdata);

int
client_pre_setattr_v2 (xlator_t *this, gfx_setattr_req *req, loc_t *loc,
                     int32_t valid, struct iatt *stbuf, dict_t *xdata);
int
client_pre_fsetattr_v2 (xlator_t *this, gfx_fsetattr_req *req, fd_t *fd,
                     int32_t valid, struct iatt *stbuf, dict_t *xdata);

int
client_pre_readdirp_v2 (xlator_t *this, gfx_readdirp_req *req, fd_t *fd,
                     size_t size, off_t offset, dict_t *xdata);

int
client_pre_fremovexattr_v2 (xlator_t *this, gfx_fremovexattr_req *req, fd_t *fd,
                          const char *name, dict_t *xdata);

int
client_pre_fallocate_v2 (xlator_t *this, gfx_fallocate_req *req, fd_t *fd,
                      int32_t flags, off_t offset, size_t size, dict_t *xdata);
int
client_pre_discard_v2 (xlator_t *this, gfx_discard_req *req, fd_t *fd,
                    off_t offset, size_t size, dict_t *xdata);
int
client_pre_zerofill_v2 (xlator_t *this, gfx_zerofill_req *req, fd_t *fd,
                     off_t offset, size_t size, dict_t *xdata);
int
client_pre_ipc_v2 (xlator_t *this, gfx_ipc_req *req, int32_t cmd,
                 dict_t *xdata);

int
client_pre_lease_v2 (xlator_t *this, gfx_lease_req *req, loc_t *loc,
                  struct gf_lease *lease, dict_t *xdata);

int
client_pre_put_v2 (xlator_t *this, gfx_put_req *req, loc_t *loc, mode_t mode,
                   mode_t umask, int32_t flags, size_t size, off_t offset,
                   dict_t *xattr, dict_t *xdata);

int
client_post_readv_v2 (xlator_t *this, gfx_read_rsp *rsp, struct iobref **iobref,
                      struct iobref *rsp_iobref, struct iatt *stat,
                      struct iovec *vector, struct iovec *rsp_vector,
                      int *rspcount, dict_t **xdata);


int
client_post_create_v2 (xlator_t *this, gfx_create_rsp *rsp,
                       struct iatt *stbuf, struct iatt *preparent,
                       struct iatt *postparent,
                       clnt_local_t *local, dict_t **xdata);
int
client_post_lease_v2 (xlator_t *this, gfx_lease_rsp *rsp, struct gf_lease *lease,
                      dict_t **xdata);
int
client_post_lk_v2 (xlator_t *this, gfx_lk_rsp *rsp, struct gf_flock *lock,
                   dict_t **xdata);
int
client_post_readdir_v2 (xlator_t *this, gfx_readdir_rsp *rsp,
                        gf_dirent_t *entries, dict_t **xdata);
int
client_post_readdirp_v2 (xlator_t *this, gfx_readdirp_rsp *rsp,
                         fd_t *fd, gf_dirent_t *entries,
                         dict_t **xdata);
int
client_post_rename_v2 (xlator_t *this, gfx_rename_rsp *rsp, struct iatt *stbuf,
                       struct iatt *preoldparent, struct iatt *postoldparent,
                       struct iatt *prenewparent, struct iatt *postnewparent,
                       dict_t **xdata);

#endif /* __CLIENT_COMMON_H__ */
