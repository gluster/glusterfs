/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_FOPS_H__
#define __EC_FOPS_H__

#include "xlator.h"

#include "ec-types.h"
#include "ec-common.h"

void ec_access(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_access_cbk_t func, void *data, loc_t * loc,
               int32_t mask, dict_t * xdata);

void ec_create(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_create_cbk_t func, void *data, loc_t * loc,
               int32_t flags, mode_t mode, mode_t umask, fd_t * fd,
               dict_t * xdata);

void ec_entrylk(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_entrylk_cbk_t func, void *data,
                const char * volume, loc_t * loc, const char * basename,
                entrylk_cmd cmd, entrylk_type type, dict_t * xdata);

void ec_fentrylk(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_fentrylk_cbk_t func, void *data,
                 const char * volume, fd_t * fd, const char * basename,
                 entrylk_cmd cmd, entrylk_type type, dict_t * xdata);

void ec_flush(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_flush_cbk_t func, void *data, fd_t * fd,
              dict_t * xdata);

void ec_fsync(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_fsync_cbk_t func, void *data, fd_t * fd,
              int32_t datasync, dict_t * xdata);

void ec_fsyncdir(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_fsyncdir_cbk_t func, void *data,
                 fd_t * fd, int32_t datasync, dict_t * xdata);

void ec_getxattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_getxattr_cbk_t func, void *data,
                 loc_t * loc, const char * name, dict_t * xdata);

void ec_fgetxattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                  int32_t minimum, fop_fgetxattr_cbk_t func, void *data,
                  fd_t * fd, const char * name, dict_t * xdata);

void ec_heal(call_frame_t * frame, xlator_t * this, uintptr_t target,
             int32_t minimum, fop_heal_cbk_t func, void *data, loc_t * loc,
             int32_t partial, dict_t *xdata);

void ec_fheal(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_fheal_cbk_t func, void *data, fd_t * fd,
              int32_t partial, dict_t *xdata);

void ec_inodelk (call_frame_t *frame, xlator_t *this, gf_lkowner_t *owner,
                 uintptr_t target, int32_t minimum, fop_inodelk_cbk_t func,
                 void *data, const char *volume, loc_t *loc, int32_t cmd,
                 struct gf_flock * flock, dict_t * xdata);

void ec_finodelk(call_frame_t *frame, xlator_t *this, gf_lkowner_t *owner,
                 uintptr_t target, int32_t minimum, fop_finodelk_cbk_t func,
                 void *data, const char *volume, fd_t *fd, int32_t cmd,
                 struct gf_flock *flock, dict_t *xdata);

void ec_link(call_frame_t * frame, xlator_t * this, uintptr_t target,
             int32_t minimum, fop_link_cbk_t func, void *data, loc_t * oldloc,
             loc_t * newloc, dict_t * xdata);

void ec_lk(call_frame_t * frame, xlator_t * this, uintptr_t target,
           int32_t minimum, fop_lk_cbk_t func, void *data, fd_t * fd,
           int32_t cmd, struct gf_flock * flock, dict_t * xdata);

void ec_lookup(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_lookup_cbk_t func, void *data, loc_t * loc,
               dict_t * xdata);

void ec_mkdir(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_mkdir_cbk_t func, void *data, loc_t * loc,
              mode_t mode, mode_t umask, dict_t * xdata);

void ec_mknod(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_mknod_cbk_t func, void *data, loc_t * loc,
              mode_t mode, dev_t rdev, mode_t umask, dict_t * xdata);

void ec_open(call_frame_t * frame, xlator_t * this, uintptr_t target,
             int32_t minimum, fop_open_cbk_t func, void *data, loc_t * loc,
             int32_t flags, fd_t * fd, dict_t * xdata);

void ec_opendir(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_opendir_cbk_t func, void *data,
                loc_t * loc, fd_t * fd, dict_t * xdata);

void ec_readdir(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_readdir_cbk_t func, void *data, fd_t * fd,
                size_t size, off_t offset, dict_t * xdata);

void ec_readdirp(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_readdirp_cbk_t func, void *data,
                 fd_t * fd, size_t size, off_t offset, dict_t * xdata);

void ec_readlink(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_readlink_cbk_t func, void *data,
                 loc_t * loc, size_t size, dict_t * xdata);

void ec_readv(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_readv_cbk_t func, void *data, fd_t * fd,
              size_t size, off_t offset, uint32_t flags, dict_t * xdata);

void ec_removexattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                    int32_t minimum, fop_removexattr_cbk_t func, void *data,
                    loc_t * loc, const char * name, dict_t * xdata);

void ec_fremovexattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                     int32_t minimum, fop_fremovexattr_cbk_t func, void *data,
                     fd_t * fd, const char * name, dict_t * xdata);

void ec_rename(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_rename_cbk_t func, void *data,
               loc_t * oldloc, loc_t * newloc, dict_t * xdata);

void ec_rmdir(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_rmdir_cbk_t func, void *data, loc_t * loc,
              int xflags, dict_t * xdata);

void ec_setattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_setattr_cbk_t func, void *data,
                loc_t * loc, struct iatt * stbuf, int32_t valid,
                dict_t * xdata);

void ec_fsetattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_fsetattr_cbk_t func, void *data,
                 fd_t * fd, struct iatt * stbuf, int32_t valid,
                 dict_t * xdata);

void ec_setxattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_setxattr_cbk_t func, void *data,
                 loc_t * loc, dict_t * dict, int32_t flags, dict_t * xdata);

void ec_fsetxattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                  int32_t minimum, fop_fsetxattr_cbk_t func, void *data,
                  fd_t * fd, dict_t * dict, int32_t flags, dict_t * xdata);

void ec_stat(call_frame_t * frame, xlator_t * this, uintptr_t target,
             int32_t minimum, fop_stat_cbk_t func, void *data, loc_t * loc,
             dict_t * xdata);

void ec_fstat(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_fstat_cbk_t func, void *data, fd_t * fd,
              dict_t * xdata);

void ec_statfs(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_statfs_cbk_t func, void *data, loc_t * loc,
               dict_t * xdata);

void ec_symlink(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_symlink_cbk_t func, void *data,
                const char * linkname, loc_t * loc, mode_t umask,
                dict_t * xdata);

void ec_truncate(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_truncate_cbk_t func, void *data,
                 loc_t * loc, off_t offset, dict_t * xdata);

void ec_ftruncate(call_frame_t * frame, xlator_t * this, uintptr_t target,
                  int32_t minimum, fop_ftruncate_cbk_t func, void *data,
                  fd_t * fd, off_t offset, dict_t * xdata);

void ec_unlink(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_unlink_cbk_t func, void *data, loc_t * loc,
               int xflags, dict_t * xdata);

void ec_writev(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_writev_cbk_t func, void *data, fd_t * fd,
               struct iovec * vector, int32_t count, off_t offset,
               uint32_t flags, struct iobref * iobref, dict_t * xdata);

void ec_xattrop(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_xattrop_cbk_t func, void *data,
                loc_t * loc, gf_xattrop_flags_t optype, dict_t * xattr,
                dict_t * xdata);

void ec_fxattrop(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_fxattrop_cbk_t func, void *data,
                 fd_t * fd, gf_xattrop_flags_t optype, dict_t * xattr,
                 dict_t * xdata);

void ec_seek(call_frame_t *frame, xlator_t *this, uintptr_t target,
             int32_t minimum, fop_seek_cbk_t func, void *data, fd_t *fd,
             off_t offset, gf_seek_what_t what, dict_t *xdata);

void ec_ipc(call_frame_t *frame, xlator_t *this, uintptr_t target,
            int32_t minimum, fop_ipc_cbk_t func, void *data, int32_t op,
            dict_t *xdata);

#endif /* __EC_FOPS_H__ */
