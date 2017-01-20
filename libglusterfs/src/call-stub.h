/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CALL_STUB_H_
#define _CALL_STUB_H_

#include "xlator.h"
#include "defaults.h"
#include "default-args.h"
#include "stack.h"
#include "list.h"

typedef struct _call_stub {
	struct list_head list;
	char wind;
	call_frame_t *frame;
	glusterfs_fop_t fop;
        struct mem_pool *stub_mem_pool; /* pointer to stub mempool in ctx_t */
        uint32_t jnl_meta_len;
        uint32_t jnl_data_len;
        void (*serialize) (struct _call_stub *, char *, char *);

	union {
		fop_lookup_t lookup;
		fop_stat_t stat;
		fop_fstat_t fstat;
		fop_truncate_t truncate;
		fop_ftruncate_t ftruncate;
		fop_access_t access;
		fop_readlink_t readlink;
		fop_mknod_t mknod;
		fop_mkdir_t mkdir;
		fop_unlink_t unlink;
		fop_rmdir_t rmdir;
		fop_symlink_t symlink;
		fop_rename_t rename;
		fop_link_t link;
		fop_create_t create;
		fop_open_t open;
		fop_readv_t readv;
		fop_writev_t writev;
		fop_flush_t flush;
		fop_fsync_t fsync;
		fop_opendir_t opendir;
		fop_fsyncdir_t fsyncdir;
		fop_statfs_t statfs;
		fop_setxattr_t setxattr;
		fop_getxattr_t getxattr;
		fop_fgetxattr_t fgetxattr;
		fop_fsetxattr_t fsetxattr;
		fop_removexattr_t removexattr;
		fop_fremovexattr_t fremovexattr;
		fop_lk_t lk;
		fop_inodelk_t inodelk;
		fop_finodelk_t finodelk;
		fop_entrylk_t entrylk;
		fop_fentrylk_t fentrylk;
		fop_readdir_t readdir;
		fop_readdirp_t readdirp;
		fop_rchecksum_t rchecksum;
		fop_xattrop_t xattrop;
		fop_fxattrop_t fxattrop;
		fop_setattr_t setattr;
		fop_fsetattr_t fsetattr;
		fop_fallocate_t fallocate;
		fop_discard_t discard;
                fop_zerofill_t zerofill;
                fop_ipc_t ipc;
                fop_seek_t seek;
                fop_lease_t lease;
                fop_getactivelk_t getactivelk;
	        fop_setactivelk_t setactivelk;
        } fn;

	union {
		fop_lookup_cbk_t lookup;
		fop_stat_cbk_t stat;
		fop_fstat_cbk_t fstat;
		fop_truncate_cbk_t truncate;
		fop_ftruncate_cbk_t ftruncate;
		fop_access_cbk_t access;
		fop_readlink_cbk_t readlink;
		fop_mknod_cbk_t mknod;
		fop_mkdir_cbk_t mkdir;
		fop_unlink_cbk_t unlink;
		fop_rmdir_cbk_t rmdir;
		fop_symlink_cbk_t symlink;
		fop_rename_cbk_t rename;
		fop_link_cbk_t link;
		fop_create_cbk_t create;
		fop_open_cbk_t open;
		fop_readv_cbk_t readv;
		fop_writev_cbk_t writev;
		fop_flush_cbk_t flush;
		fop_fsync_cbk_t fsync;
		fop_opendir_cbk_t opendir;
		fop_fsyncdir_cbk_t fsyncdir;
		fop_statfs_cbk_t statfs;
		fop_setxattr_cbk_t setxattr;
		fop_getxattr_cbk_t getxattr;
		fop_fgetxattr_cbk_t fgetxattr;
		fop_fsetxattr_cbk_t fsetxattr;
		fop_removexattr_cbk_t removexattr;
		fop_fremovexattr_cbk_t fremovexattr;
		fop_lk_cbk_t lk;
		fop_inodelk_cbk_t inodelk;
		fop_finodelk_cbk_t finodelk;
		fop_entrylk_cbk_t entrylk;
		fop_fentrylk_cbk_t fentrylk;
		fop_readdir_cbk_t readdir;
		fop_readdirp_cbk_t readdirp;
		fop_rchecksum_cbk_t rchecksum;
		fop_xattrop_cbk_t xattrop;
		fop_fxattrop_cbk_t fxattrop;
		fop_setattr_cbk_t setattr;
		fop_fsetattr_cbk_t fsetattr;
		fop_fallocate_cbk_t fallocate;
		fop_discard_cbk_t discard;
                fop_zerofill_cbk_t zerofill;
                fop_ipc_cbk_t ipc;
                fop_seek_cbk_t seek;
                fop_lease_cbk_t lease;
                fop_getactivelk_cbk_t getactivelk;
                fop_setactivelk_cbk_t setactivelk;
	} fn_cbk;

        default_args_t args;
        default_args_cbk_t args_cbk;
} call_stub_t;


call_stub_t *
fop_lookup_stub (call_frame_t *frame,
		 fop_lookup_t fn,
		 loc_t *loc,
		 dict_t *xdata);

call_stub_t *
fop_lookup_cbk_stub (call_frame_t *frame,
		     fop_lookup_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
		     struct iatt *buf,
                     dict_t *xdata,
                     struct iatt *postparent);
call_stub_t *
fop_stat_stub (call_frame_t *frame,
	       fop_stat_t fn,
	       loc_t *loc, dict_t *xdata);
call_stub_t *
fop_stat_cbk_stub (call_frame_t *frame,
		   fop_stat_cbk_t fn,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct iatt *buf, dict_t *xdata);
call_stub_t *
fop_fstat_stub (call_frame_t *frame,
		fop_fstat_t fn,
		fd_t *fd, dict_t *xdata);
call_stub_t *
fop_fstat_cbk_stub (call_frame_t *frame,
		    fop_fstat_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct iatt *buf, dict_t *xdata);

call_stub_t *
fop_truncate_stub (call_frame_t *frame,
		   fop_truncate_t fn,
		   loc_t *loc,
		   off_t off, dict_t *xdata);

call_stub_t *
fop_truncate_cbk_stub (call_frame_t *frame,
		       fop_truncate_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata);

call_stub_t *
fop_ftruncate_stub (call_frame_t *frame,
		    fop_ftruncate_t fn,
		    fd_t *fd,
		    off_t off, dict_t *xdata);

call_stub_t *
fop_ftruncate_cbk_stub (call_frame_t *frame,
			fop_ftruncate_cbk_t fn,
			int32_t op_ret,
			int32_t op_errno,
			struct iatt *prebuf,
                        struct iatt *postbuf, dict_t *xdata);

call_stub_t *
fop_access_stub (call_frame_t *frame,
		 fop_access_t fn,
		 loc_t *loc,
		 int32_t mask, dict_t *xdata);

call_stub_t *
fop_access_cbk_stub (call_frame_t *frame,
		     fop_access_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_readlink_stub (call_frame_t *frame,
		   fop_readlink_t fn,
		   loc_t *loc,
		   size_t size, dict_t *xdata);

call_stub_t *
fop_readlink_cbk_stub (call_frame_t *frame,
		       fop_readlink_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       const char *path,
                       struct iatt *buf, dict_t *xdata);

call_stub_t *
fop_mknod_stub (call_frame_t *frame, fop_mknod_t fn, loc_t *loc, mode_t mode,
                dev_t rdev, mode_t umask, dict_t *xdata);

call_stub_t *
fop_mknod_cbk_stub (call_frame_t *frame,
		    fop_mknod_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
                    struct iatt *buf,
                    struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata);

call_stub_t *
fop_mkdir_stub (call_frame_t *frame, fop_mkdir_t fn, loc_t *loc, mode_t mode,
                mode_t umask, dict_t *xdata);

call_stub_t *
fop_mkdir_cbk_stub (call_frame_t *frame,
		    fop_mkdir_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
                    struct iatt *buf,
                    struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata);

call_stub_t *
fop_unlink_stub (call_frame_t *frame, fop_unlink_t fn,
		 loc_t *loc, int xflag, dict_t *xdata);

call_stub_t *
fop_unlink_cbk_stub (call_frame_t *frame,
		     fop_unlink_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
                     struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata);

call_stub_t *
fop_rmdir_stub (call_frame_t *frame, fop_rmdir_t fn,
		loc_t *loc, int flags, dict_t *xdata);

call_stub_t *
fop_rmdir_cbk_stub (call_frame_t *frame,
		    fop_rmdir_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
                    struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata);

call_stub_t *
fop_symlink_stub (call_frame_t *frame, fop_symlink_t fn,
		  const char *linkname, loc_t *loc, mode_t umask, dict_t *xdata);

call_stub_t *
fop_symlink_cbk_stub (call_frame_t *frame,
		      fop_symlink_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      inode_t *inode,
                      struct iatt *buf,
                      struct iatt *preparent,
                      struct iatt *postparent, dict_t *xdata);

call_stub_t *
fop_rename_stub (call_frame_t *frame,
		 fop_rename_t fn,
		 loc_t *oldloc,
		 loc_t *newloc, dict_t *xdata);

call_stub_t *
fop_rename_cbk_stub (call_frame_t *frame,
		     fop_rename_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct iatt *buf,
                     struct iatt *preoldparent,
                     struct iatt *postoldparent,
                     struct iatt *prenewparent,
                     struct iatt *postnewparent, dict_t *xdata);

call_stub_t *
fop_link_stub (call_frame_t *frame,
	       fop_link_t fn,
	       loc_t *oldloc,
	       loc_t *newloc, dict_t *xdata);

call_stub_t *
fop_link_cbk_stub (call_frame_t *frame,
		   fop_link_cbk_t fn,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
                   struct iatt *buf,
                   struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata);

call_stub_t *
fop_create_stub (call_frame_t *frame, fop_create_t fn,
		 loc_t *loc, int32_t flags, mode_t mode,
                 mode_t umask, fd_t *fd, dict_t *xdata);

call_stub_t *
fop_create_cbk_stub (call_frame_t *frame,
		     fop_create_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     fd_t *fd,
		     inode_t *inode,
		     struct iatt *buf,
                     struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata);

call_stub_t *
fop_open_stub (call_frame_t *frame,
	       fop_open_t fn,
	       loc_t *loc,
	       int32_t flags,
	       fd_t *fd,
               dict_t *xdata);

call_stub_t *
fop_open_cbk_stub (call_frame_t *frame,
		   fop_open_cbk_t fn,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd, dict_t *xdata);

call_stub_t *
fop_readv_stub (call_frame_t *frame,
		fop_readv_t fn,
		fd_t *fd,
		size_t size,
		off_t off, uint32_t flags, dict_t *xdata);

call_stub_t *
fop_readv_cbk_stub (call_frame_t *frame,
		    fop_readv_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct iovec *vector,
		    int32_t count,
		    struct iatt *stbuf,
                    struct iobref *iobref, dict_t *xdata);

call_stub_t *
fop_writev_stub (call_frame_t *frame,
		 fop_writev_t fn,
		 fd_t *fd,
		 struct iovec *vector,
		 int32_t count,
		 off_t off, uint32_t flags,
                 struct iobref *iobref, dict_t *xdata);

call_stub_t *
fop_writev_cbk_stub (call_frame_t *frame,
		     fop_writev_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
                     struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata);

call_stub_t *
fop_flush_stub (call_frame_t *frame,
		fop_flush_t fn,
		fd_t *fd, dict_t *xdata);

call_stub_t *
fop_flush_cbk_stub (call_frame_t *frame,
		    fop_flush_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_fsync_stub (call_frame_t *frame,
		fop_fsync_t fn,
		fd_t *fd,
		int32_t datasync, dict_t *xdata);

call_stub_t *
fop_fsync_cbk_stub (call_frame_t *frame,
		    fop_fsync_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
                    struct iatt *prebuf,
                    struct iatt *postbuf, dict_t *xdata);

call_stub_t *
fop_opendir_stub (call_frame_t *frame,
		  fop_opendir_t fn,
		  loc_t *loc, fd_t *fd, dict_t *xdata);

call_stub_t *
fop_opendir_cbk_stub (call_frame_t *frame,
		      fop_opendir_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      fd_t *fd, dict_t *xdata);

call_stub_t *
fop_fsyncdir_stub (call_frame_t *frame,
		   fop_fsyncdir_t fn,
		   fd_t *fd,
		   int32_t datasync, dict_t *xdata);

call_stub_t *
fop_fsyncdir_cbk_stub (call_frame_t *frame,
		       fop_fsyncdir_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_statfs_stub (call_frame_t *frame,
		 fop_statfs_t fn,
		 loc_t *loc, dict_t *xdata);

call_stub_t *
fop_statfs_cbk_stub (call_frame_t *frame,
		     fop_statfs_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct statvfs *buf, dict_t *xdata);

call_stub_t *
fop_setxattr_stub (call_frame_t *frame,
		   fop_setxattr_t fn,
		   loc_t *loc,
		   dict_t *dict,
		   int32_t flags, dict_t *xdata);

call_stub_t *
fop_setxattr_cbk_stub (call_frame_t *frame,
		       fop_setxattr_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_getxattr_stub (call_frame_t *frame,
		   fop_getxattr_t fn,
		   loc_t *loc,
		   const char *name, dict_t *xdata);

call_stub_t *
fop_getxattr_cbk_stub (call_frame_t *frame,
		       fop_getxattr_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       dict_t *value, dict_t *xdata);

call_stub_t *
fop_fsetxattr_stub (call_frame_t *frame,
                    fop_fsetxattr_t fn,
                    fd_t *fd,
                    dict_t *dict,
                    int32_t flags, dict_t *xdata);

call_stub_t *
fop_fsetxattr_cbk_stub (call_frame_t *frame,
                        fop_fsetxattr_cbk_t fn,
                        int32_t op_ret,
                        int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_fgetxattr_stub (call_frame_t *frame,
                    fop_fgetxattr_t fn,
                    fd_t *fd,
                    const char *name, dict_t *xdata);

call_stub_t *
fop_fgetxattr_cbk_stub (call_frame_t *frame,
                        fop_fgetxattr_cbk_t fn,
                        int32_t op_ret,
                        int32_t op_errno,
                        dict_t *value, dict_t *xdata);

call_stub_t *
fop_removexattr_stub (call_frame_t *frame,
		      fop_removexattr_t fn,
		      loc_t *loc,
		      const char *name, dict_t *xdata);

call_stub_t *
fop_removexattr_cbk_stub (call_frame_t *frame,
			  fop_removexattr_cbk_t fn,
			  int32_t op_ret,
			  int32_t op_errno, dict_t *xdata);


call_stub_t *
fop_fremovexattr_stub (call_frame_t *frame,
                       fop_fremovexattr_t fn,
                       fd_t *fd,
                       const char *name, dict_t *xdata);

call_stub_t *
fop_fremovexattr_cbk_stub (call_frame_t *frame,
                           fop_fremovexattr_cbk_t fn,
                           int32_t op_ret,
                           int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_lk_stub (call_frame_t *frame,
	     fop_lk_t fn,
	     fd_t *fd,
	     int32_t cmd,
	     struct gf_flock *lock, dict_t *xdata);

call_stub_t *
fop_lk_cbk_stub (call_frame_t *frame,
		 fop_lk_cbk_t fn,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct gf_flock *lock, dict_t *xdata);

call_stub_t *
fop_inodelk_stub (call_frame_t *frame, fop_inodelk_t fn,
		  const char *volume, loc_t *loc, int32_t cmd,
                  struct gf_flock *lock, dict_t *xdata);

call_stub_t *
fop_finodelk_stub (call_frame_t *frame, fop_finodelk_t fn,
		   const char *volume, fd_t *fd, int32_t cmd,
                   struct gf_flock *lock, dict_t *xdata);

call_stub_t *
fop_entrylk_stub (call_frame_t *frame, fop_entrylk_t fn,
		  const char *volume, loc_t *loc, const char *basename,
		  entrylk_cmd cmd, entrylk_type type, dict_t *xdata);

call_stub_t *
fop_fentrylk_stub (call_frame_t *frame, fop_fentrylk_t fn,
		   const char *volume, fd_t *fd, const char *basename,
		   entrylk_cmd cmd, entrylk_type type, dict_t *xdata);

call_stub_t *
fop_inodelk_cbk_stub (call_frame_t *frame, fop_inodelk_cbk_t fn,
		      int32_t op_ret, int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_finodelk_cbk_stub (call_frame_t *frame, fop_inodelk_cbk_t fn,
		       int32_t op_ret, int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_entrylk_cbk_stub (call_frame_t *frame, fop_entrylk_cbk_t fn,
		      int32_t op_ret, int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_fentrylk_cbk_stub (call_frame_t *frame, fop_entrylk_cbk_t fn,
		       int32_t op_ret, int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_readdir_stub (call_frame_t *frame,
		  fop_readdir_t fn,
		  fd_t *fd,
		  size_t size,
		  off_t off, dict_t *xdata);

call_stub_t *
fop_readdirp_stub (call_frame_t *frame,
		   fop_readdirp_t fn,
		   fd_t *fd,
		   size_t size,
		   off_t off,
                   dict_t *xdata);

call_stub_t *
fop_readdirp_cbk_stub (call_frame_t *frame,
		       fop_readdir_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       gf_dirent_t *entries, dict_t *xdata);

call_stub_t *
fop_readdir_cbk_stub (call_frame_t *frame,
		      fop_readdir_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      gf_dirent_t *entries, dict_t *xdata);

call_stub_t *
fop_rchecksum_stub (call_frame_t *frame,
                    fop_rchecksum_t fn,
                    fd_t *fd, off_t offset,
                    int32_t len, dict_t *xdata);

call_stub_t *
fop_rchecksum_cbk_stub (call_frame_t *frame,
                        fop_rchecksum_cbk_t fn,
                        int32_t op_ret,
                        int32_t op_errno,
                        uint32_t weak_checksum,
                        uint8_t *strong_checksum, dict_t *xdata);

call_stub_t *
fop_xattrop_stub (call_frame_t *frame,
		  fop_xattrop_t fn,
		  loc_t *loc,
		  gf_xattrop_flags_t optype,
		  dict_t *xattr, dict_t *xdata);

call_stub_t *
fop_xattrop_stub_cbk_stub (call_frame_t *frame,
			   fop_xattrop_cbk_t fn,
			   int32_t op_ret,
			   int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_fxattrop_stub (call_frame_t *frame,
		   fop_fxattrop_t fn,
		   fd_t *fd,
		   gf_xattrop_flags_t optype,
		   dict_t *xattr, dict_t *xdata);

call_stub_t *
fop_fxattrop_stub_cbk_stub (call_frame_t *frame,
			    fop_xattrop_cbk_t fn,
			    int32_t op_ret,
			    int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_setattr_stub (call_frame_t *frame,
                  fop_setattr_t fn,
                  loc_t *loc,
                  struct iatt *stbuf,
                  int32_t valid, dict_t *xdata);

call_stub_t *
fop_setattr_cbk_stub (call_frame_t *frame,
                      fop_setattr_cbk_t fn,
                      int32_t op_ret,
                      int32_t op_errno,
                      struct iatt *statpre,
                      struct iatt *statpost, dict_t *xdata);

call_stub_t *
fop_fsetattr_stub (call_frame_t *frame,
                   fop_fsetattr_t fn,
                   fd_t *fd,
                   struct iatt *stbuf,
                   int32_t valid, dict_t *xdata);

call_stub_t *
fop_fsetattr_cbk_stub (call_frame_t *frame,
                       fop_setattr_cbk_t fn,
                       int32_t op_ret,
                       int32_t op_errno,
                       struct iatt *statpre,
                       struct iatt *statpost, dict_t *xdata);

call_stub_t *
fop_fallocate_stub(call_frame_t *frame,
		   fop_fallocate_t fn,
		   fd_t *fd,
		   int32_t mode, off_t offset,
		   size_t len, dict_t *xdata);

call_stub_t *
fop_fallocate_cbk_stub(call_frame_t *frame,
		       fop_fallocate_cbk_t fn,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *statpre, struct iatt *statpost,
                       dict_t *xdata);

call_stub_t *
fop_discard_stub(call_frame_t *frame,
		 fop_discard_t fn,
		 fd_t *fd,
		 off_t offset,
		 size_t len, dict_t *xdata);

call_stub_t *
fop_discard_cbk_stub(call_frame_t *frame,
		     fop_discard_cbk_t fn,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *statpre, struct iatt *statpost,
                     dict_t *xdata);

call_stub_t *
fop_zerofill_stub(call_frame_t *frame,
                 fop_zerofill_t fn,
                 fd_t *fd,
                 off_t offset,
                 off_t len, dict_t *xdata);

call_stub_t *
fop_zerofill_cbk_stub(call_frame_t *frame,
                     fop_zerofill_cbk_t fn,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *statpre, struct iatt *statpost,
                     dict_t *xdata);

call_stub_t *
fop_ipc_stub (call_frame_t *frame, fop_ipc_t fn, int32_t op, dict_t *xdata);

call_stub_t *
fop_ipc_cbk_stub (call_frame_t *frame, fop_ipc_cbk_t fn,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata);

call_stub_t *
fop_seek_stub (call_frame_t *frame, fop_seek_t fn, fd_t *fd, off_t offset,
               gf_seek_what_t what, dict_t *xdata);

call_stub_t *
fop_seek_cbk_stub (call_frame_t *frame, fop_seek_cbk_t fn,
                  int32_t op_ret, int32_t op_errno, off_t offset,
                  dict_t *xdata);

call_stub_t *
fop_lease_stub (call_frame_t *frame, fop_lease_t fn, loc_t *loc,
                struct gf_lease *lease, dict_t *xdata);

call_stub_t *
fop_lease_cbk_stub (call_frame_t *frame, fop_lease_cbk_t fn,
                    int32_t op_ret, int32_t op_errno,
                    struct gf_lease *lease, dict_t *xdata);

call_stub_t *
fop_getactivelk_stub (call_frame_t *frame, fop_getactivelk_t fn,
                       loc_t *loc, dict_t *xdata);

call_stub_t *
fop_getactivelk_cbk_stub (call_frame_t *frame, fop_getactivelk_cbk_t fn,
                          int32_t op_ret, int32_t op_errno,
                          lock_migration_info_t *lmi, dict_t *xdata);

call_stub_t *
fop_setactivelk_stub (call_frame_t *frame, fop_setactivelk_t fn,
                        loc_t *loc, lock_migration_info_t *locklist,
                        dict_t *xdata);

call_stub_t *
fop_setactivelk_cbk_stub (call_frame_t *frame, fop_setactivelk_cbk_t fn,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata);

void call_resume (call_stub_t *stub);
void call_resume_keep_stub (call_stub_t *stub);
void call_stub_destroy (call_stub_t *stub);
void call_unwind_error (call_stub_t *stub, int op_ret, int op_errno);
void call_unwind_error_keep_stub (call_stub_t *stub, int op_ret, int op_errno);

/*
 * Sometimes we might want to call just this, perhaps repeatedly, without
 * having (or being able) to destroy and recreate it.
 */
void call_resume_wind (call_stub_t *stub);

#endif
