/*
  Copyright (c) 2007-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CALL_STUB_H_
#define _CALL_STUB_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "stack.h"
#include "list.h"

typedef struct {
	struct list_head list;
	char wind;
	call_frame_t *frame;
	glusterfs_fop_t fop;

	union {
		/* lookup */
		struct {
			fop_lookup_t fn;
			loc_t loc;
			dict_t *xattr_req;
		} lookup;
		struct {
			fop_lookup_cbk_t fn;
			int32_t op_ret, op_errno;
			inode_t *inode;
			struct stat buf;
			dict_t *dict;
                        struct stat postparent;
		} lookup_cbk;

		/* stat */
		struct {
			fop_stat_t fn;
			loc_t loc;
		} stat;
		struct {
			fop_stat_cbk_t fn;
			int32_t op_ret, op_errno;
			struct stat buf;
		} stat_cbk;

		/* fstat */
		struct {
			fop_fstat_t fn;
			fd_t *fd;
		} fstat;
		struct {
			fop_fstat_cbk_t fn;
			int32_t op_ret, op_errno;
			struct stat buf;
		} fstat_cbk;

		/* chmod */
		struct {
			fop_chmod_t fn;
			loc_t loc;
			mode_t mode;
		} chmod;
		struct {
			fop_chmod_cbk_t fn;
			int32_t op_ret, op_errno;
			struct stat buf;
		} chmod_cbk;

		/* fchmod */
		struct {
			fop_fchmod_t fn;
			fd_t *fd;
			mode_t mode;
		} fchmod;
		struct {
			fop_fchmod_cbk_t fn;
			int32_t op_ret, op_errno;
			struct stat buf;
		} fchmod_cbk;

		/* chown */
		struct {
			fop_chown_t fn;
			loc_t loc;
			uid_t uid;
			gid_t gid;
		} chown;
		struct {
			fop_chown_cbk_t fn;
			int32_t op_ret, op_errno;
			struct stat buf;
		} chown_cbk;

		/* fchown */
		struct {
			fop_fchown_t fn;
			fd_t *fd;
			uid_t uid;
			gid_t gid;
		} fchown;
		struct {
			fop_fchown_cbk_t fn;
			int32_t op_ret, op_errno;
			struct stat buf;
		} fchown_cbk;

		/* truncate */
		struct {
			fop_truncate_t fn;
			loc_t loc;
			off_t off;
		} truncate;
		struct {
			fop_truncate_cbk_t fn;
			int32_t op_ret, op_errno;
			struct stat prebuf;
                        struct stat postbuf;
		} truncate_cbk;

		/* ftruncate */
		struct {
			fop_ftruncate_t fn;
			fd_t *fd;
			off_t off;
		} ftruncate;
		struct {
			fop_ftruncate_cbk_t fn;
			int32_t op_ret, op_errno;
			struct stat prebuf;
                        struct stat postbuf;
		} ftruncate_cbk;

		/* utimens */
		struct {
			fop_utimens_t fn;
			loc_t loc;
			struct timespec tv[2];
		} utimens;
		struct {
			fop_utimens_cbk_t fn;
			int32_t op_ret, op_errno;
			struct stat buf;
		} utimens_cbk;

		/* access */
		struct {
			fop_access_t fn;
			loc_t loc;
			int32_t mask;
		} access;
		struct {
			fop_access_cbk_t fn;
			int32_t op_ret, op_errno;
		} access_cbk;

		/* readlink */
		struct {
			fop_readlink_t fn;
			loc_t loc;
			size_t size;
		} readlink;
		struct {
			fop_readlink_cbk_t fn;
			int32_t op_ret, op_errno;
			const char *buf;
                        struct stat sbuf;
		} readlink_cbk;

		/* mknod */
		struct {
			fop_mknod_t fn;
			loc_t loc;
			mode_t mode;
			dev_t rdev;
		} mknod;
		struct {
			fop_mknod_cbk_t fn;
			int32_t op_ret, op_errno;
			inode_t *inode;
			struct stat buf;
                        struct stat preparent;
                        struct stat postparent;
		} mknod_cbk;

		/* mkdir */
		struct {
			fop_mkdir_t fn;
			loc_t loc;
			mode_t mode;
		} mkdir;
		struct {
			fop_mkdir_cbk_t fn;
			int32_t op_ret, op_errno;
			inode_t *inode;
			struct stat buf;
                        struct stat preparent;
                        struct stat postparent;
		} mkdir_cbk;

		/* unlink */
		struct {
			fop_unlink_t fn;
			loc_t loc;
		} unlink;
		struct {
			fop_unlink_cbk_t fn;
			int32_t op_ret, op_errno;
                        struct stat preparent;
                        struct stat postparent;
		} unlink_cbk;

		/* rmdir */
		struct {
			fop_rmdir_t fn;
			loc_t loc;
		} rmdir;
		struct {
			fop_rmdir_cbk_t fn;
			int32_t op_ret, op_errno;
                        struct stat preparent;
                        struct stat postparent;
		} rmdir_cbk;

		/* symlink */
		struct {
			fop_symlink_t fn;
			const char *linkname;
			loc_t loc;
		} symlink;
		struct {
			fop_symlink_cbk_t fn;
			int32_t op_ret, op_errno;
			inode_t *inode;
			struct stat buf;
                        struct stat preparent;
                        struct stat postparent;
		} symlink_cbk;

		/* rename */
		struct {
			fop_rename_t fn;
			loc_t old;
			loc_t new;
		} rename;
		struct {
			fop_rename_cbk_t fn;
			int32_t op_ret, op_errno;
			struct stat buf;
                        struct stat preoldparent;
                        struct stat postoldparent;
                        struct stat prenewparent;
                        struct stat postnewparent;
		} rename_cbk;

		/* link */
		struct {
			fop_link_t fn;
			loc_t oldloc;
			loc_t newloc;
		} link;
		struct {
			fop_link_cbk_t fn;
			int32_t op_ret, op_errno;
			inode_t *inode;
			struct stat buf;
                        struct stat preparent;
                        struct stat postparent;
		} link_cbk;

		/* create */
		struct {
			fop_create_t fn;
			loc_t loc;
			int32_t flags;
			mode_t mode;
			fd_t *fd;
		} create;
		struct {
			fop_create_cbk_t fn;
			int32_t op_ret, op_errno;
			fd_t *fd;
			inode_t *inode;
			struct stat buf;
                        struct stat preparent;
                        struct stat postparent;
		} create_cbk;

		/* open */
		struct {
			fop_open_t fn;
			loc_t loc;
			int32_t flags;
			fd_t *fd;
                        int32_t wbflags;
		} open;
		struct {
			fop_open_cbk_t fn;
			int32_t op_ret, op_errno;
			fd_t *fd;
		} open_cbk;

		/* readv */
		struct {
			fop_readv_t fn;
			fd_t *fd;
			size_t size;
			off_t off;
		} readv;
		struct {
			fop_readv_cbk_t fn;
			int32_t op_ret;
			int32_t op_errno;
			struct iovec *vector;
			int32_t count;
			struct stat stbuf;
			struct iobref *iobref;
		} readv_cbk;

		/* writev */
		struct {
			fop_writev_t fn;
			fd_t *fd;
			struct iovec *vector;
			int32_t count;
			off_t off;
			struct iobref *iobref;
		} writev;
		struct {
			fop_writev_cbk_t fn;
			int32_t op_ret, op_errno;
                        struct stat prebuf;
			struct stat postbuf;
		} writev_cbk;

		/* flush */
		struct {
			fop_flush_t fn;
			fd_t *fd;
		} flush;
		struct {
			fop_flush_cbk_t fn;
			int32_t op_ret, op_errno;
		} flush_cbk;

		/* fsync */
		struct {
			fop_fsync_t fn;
			fd_t *fd;
			int32_t datasync;
		} fsync;
		struct {
			fop_fsync_cbk_t fn;
			int32_t op_ret, op_errno;
                        struct stat prebuf;
                        struct stat postbuf;
		} fsync_cbk;

		/* opendir */
		struct {
			fop_opendir_t fn;
			loc_t loc;
			fd_t *fd;
		} opendir;
		struct {
			fop_opendir_cbk_t fn;
			int32_t op_ret, op_errno;
			fd_t *fd;
		} opendir_cbk;

		/* getdents */
		struct {
			fop_getdents_t fn;
			fd_t *fd;
			size_t size;
			off_t off;
			int32_t flag;
		} getdents;
		struct {
			fop_getdents_cbk_t fn;
			int32_t op_ret;
			int32_t op_errno;
			dir_entry_t entries;
			int32_t count;
		} getdents_cbk;

		/* setdents */
		struct {
			fop_setdents_t fn;
			fd_t *fd;
			int32_t flags;
			dir_entry_t entries;
			int32_t count;
		} setdents;
		struct {
			fop_setdents_cbk_t fn;
			int32_t op_ret;
			int32_t op_errno;
		} setdents_cbk;

		/* fsyncdir */
		struct {
			fop_fsyncdir_t fn;
			fd_t *fd;
			int32_t datasync;
		} fsyncdir;
		struct {
			fop_fsyncdir_cbk_t fn;
			int32_t op_ret, op_errno;
		} fsyncdir_cbk;

		/* statfs */
		struct {
			fop_statfs_t fn;
			loc_t loc;
		} statfs;
		struct {
			fop_statfs_cbk_t fn;
			int32_t op_ret, op_errno;
			struct statvfs buf;
		} statfs_cbk;

		/* setxattr */
		struct {
			fop_setxattr_t fn;
			loc_t loc;
			dict_t *dict;
			int32_t flags;
		} setxattr;
		struct {
			fop_setxattr_cbk_t fn;
			int32_t op_ret, op_errno;
		} setxattr_cbk;

		/* getxattr */
		struct {
			fop_getxattr_t fn;
			loc_t loc;
			const char *name;
		} getxattr;
		struct {
			fop_getxattr_cbk_t fn;
			int32_t op_ret, op_errno;
			dict_t *dict;
		} getxattr_cbk;

		/* fsetxattr */
		struct {
			fop_fsetxattr_t fn;
			fd_t *fd;
			dict_t *dict;
			int32_t flags;
		} fsetxattr;
		struct {
			fop_fsetxattr_cbk_t fn;
			int32_t op_ret, op_errno;
		} fsetxattr_cbk;

		/* fgetxattr */
		struct {
			fop_fgetxattr_t fn;
			fd_t *fd;
			const char *name;
		} fgetxattr;
		struct {
			fop_fgetxattr_cbk_t fn;
			int32_t op_ret, op_errno;
			dict_t *dict;
		} fgetxattr_cbk;

		/* removexattr */
		struct {
			fop_removexattr_t fn;
			loc_t loc;
			const char *name;
		} removexattr;
		struct {
			fop_removexattr_cbk_t fn;
			int32_t op_ret, op_errno;
		} removexattr_cbk;

		/* lk */
		struct {
			fop_lk_t fn;
			fd_t *fd;
			int32_t cmd;
			struct flock lock;
		} lk;
		struct {
			fop_lk_cbk_t fn;
			int32_t op_ret, op_errno;
			struct flock lock;
		} lk_cbk;

		/* inodelk */
		struct {
			fop_inodelk_t fn;
                        const char *volume;
			loc_t loc;
			int32_t cmd;
			struct flock lock;
		} inodelk;

		struct {
			fop_inodelk_cbk_t fn;
			int32_t op_ret, op_errno;
		} inodelk_cbk;

		/* finodelk */
		struct {
			fop_finodelk_t fn;
                        const char *volume;
			fd_t *fd;
			int32_t cmd;
			struct flock lock;
		} finodelk;

		struct {
			fop_finodelk_cbk_t fn;
			int32_t op_ret, op_errno;
		} finodelk_cbk;

		/* entrylk */
		struct {
			fop_entrylk_t fn;
			loc_t loc;
                        const char *volume;
			const char *name;
			entrylk_cmd cmd;
			entrylk_type type;
		} entrylk;

		struct {
			fop_entrylk_cbk_t fn;
			int32_t op_ret, op_errno;
		} entrylk_cbk;

		/* fentrylk */
		struct {
			fop_fentrylk_t fn;
			fd_t *fd;
                        const char *volume;
			const char *name;
			entrylk_cmd cmd;
			entrylk_type type;
		} fentrylk;

		struct {
			fop_fentrylk_cbk_t fn;
			int32_t op_ret, op_errno;
		} fentrylk_cbk;

		/* readdir */
		struct {
			fop_readdir_t fn;
			fd_t *fd;
			size_t size;
			off_t off;
		} readdir;
		struct {
			fop_readdir_cbk_t fn;
			int32_t op_ret, op_errno;
			gf_dirent_t entries;
		} readdir_cbk;

                /* readdirp */
		struct {
			fop_readdirp_t fn;
			fd_t *fd;
			size_t size;
			off_t off;
		} readdirp;
		struct {
			fop_readdirp_cbk_t fn;
			int32_t op_ret, op_errno;
			gf_dirent_t entries;
		} readdirp_cbk;

		/* checksum */
		struct {
			fop_checksum_t fn;
			loc_t loc;
			int32_t flags;
		} checksum;
		struct {
			fop_checksum_cbk_t fn;
			int32_t op_ret, op_errno;
			uint8_t *file_checksum;
			uint8_t *dir_checksum;
		} checksum_cbk;

		/* rchecksum */
		struct {
			fop_rchecksum_t fn;
			fd_t *fd;
                        off_t offset;
			int32_t len;
		} rchecksum;
		struct {
			fop_rchecksum_cbk_t fn;
			int32_t op_ret, op_errno;
			uint32_t weak_checksum;
			uint8_t *strong_checksum;
		} rchecksum_cbk;

		/* xattrop */
		struct {
			fop_xattrop_t fn;
			loc_t loc;
			gf_xattrop_flags_t optype;
			dict_t *xattr;
		} xattrop;
		struct {
			fop_xattrop_cbk_t fn;
			int32_t op_ret;
			int32_t op_errno;
			dict_t *xattr;
		} xattrop_cbk;

		/* fxattrop */
		struct {
			fop_fxattrop_t fn;
			fd_t *fd;
			gf_xattrop_flags_t optype;
			dict_t *xattr;
		} fxattrop;
		struct {
			fop_fxattrop_cbk_t fn;
			int32_t op_ret;
			int32_t op_errno;
			dict_t *xattr;
		} fxattrop_cbk;

		struct {
			fop_lock_notify_t fn;
			loc_t loc;
			int32_t timeout;
		} lock_notify;
		struct {
			fop_lock_notify_cbk_t fn;
			int32_t op_ret;
			int32_t op_errno;
		} lock_notify_cbk;

		struct {
			fop_lock_fnotify_t fn;
			fd_t *fd;
			int32_t timeout;
		} lock_fnotify;
		struct {
			fop_lock_fnotify_cbk_t fn;
			int32_t op_ret;
			int32_t op_errno;
		} lock_fnotify_cbk;
                
                /* setattr */
                struct {
                        fop_setattr_t fn;
                        loc_t loc;
                        struct stat stbuf;
                        int32_t valid;
                } setattr;
                struct {
                        fop_setattr_cbk_t fn;
                        int32_t op_ret;
                        int32_t op_errno;
                        struct stat statpre;
                        struct stat statpost;
                } setattr_cbk;

                /* fsetattr */
                struct {
                        fop_fsetattr_t fn;
                        fd_t *fd;
                        struct stat stbuf;
                        int32_t valid;
                } fsetattr;
                struct {
                        fop_fsetattr_cbk_t fn;
                        int32_t op_ret;
                        int32_t op_errno;
                        struct stat statpre;
                        struct stat statpost;
                } fsetattr_cbk;

	} args;
} call_stub_t;

call_stub_t *
fop_lookup_stub (call_frame_t *frame,
		 fop_lookup_t fn,
		 loc_t *loc,
		 dict_t *xattr_req);

call_stub_t *
fop_lookup_cbk_stub (call_frame_t *frame,
		     fop_lookup_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
		     struct stat *buf,
                     dict_t *dict,
                     struct stat *postparent);
call_stub_t *
fop_stat_stub (call_frame_t *frame,
	       fop_stat_t fn,
	       loc_t *loc);
call_stub_t *
fop_stat_cbk_stub (call_frame_t *frame,
		   fop_stat_cbk_t fn,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf);
call_stub_t *
fop_fstat_stub (call_frame_t *frame,
		fop_fstat_t fn,
		fd_t *fd);
call_stub_t *
fop_fstat_cbk_stub (call_frame_t *frame,
		    fop_fstat_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf);
call_stub_t *
fop_chmod_stub (call_frame_t *frame,
		fop_chmod_t fn,
		loc_t *loc,
		mode_t mode);
call_stub_t *
fop_chmod_cbk_stub (call_frame_t *frame,
		    fop_chmod_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf);
call_stub_t *
fop_fchmod_stub (call_frame_t *frame,
		 fop_fchmod_t fn,
		 fd_t *fd,
		 mode_t mode);
call_stub_t *
fop_fchmod_cbk_stub (call_frame_t *frame,
		     fop_fchmod_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf);
call_stub_t *
fop_chown_stub (call_frame_t *frame,
		fop_chown_t fn,
		loc_t *loc,
		uid_t uid,
		gid_t gid);

call_stub_t *
fop_chown_cbk_stub (call_frame_t *frame,
		    fop_chown_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf);

call_stub_t *
fop_fchown_stub (call_frame_t *frame,
		 fop_fchown_t fn,
		 fd_t *fd,
		 uid_t uid,
		 gid_t gid);

call_stub_t *
fop_fchown_cbk_stub (call_frame_t *frame,
		     fop_fchown_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf);

call_stub_t *
fop_truncate_stub (call_frame_t *frame,
		   fop_truncate_t fn,
		   loc_t *loc,
		   off_t off);

call_stub_t *
fop_truncate_cbk_stub (call_frame_t *frame,
		       fop_truncate_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct stat *prebuf,
                       struct stat *postbuf);

call_stub_t *
fop_ftruncate_stub (call_frame_t *frame,
		    fop_ftruncate_t fn,
		    fd_t *fd,
		    off_t off);

call_stub_t *
fop_ftruncate_cbk_stub (call_frame_t *frame,
			fop_ftruncate_cbk_t fn,
			int32_t op_ret,
			int32_t op_errno,
			struct stat *prebuf,
                        struct stat *postbuf);

call_stub_t *
fop_utimens_stub (call_frame_t *frame,
		  fop_utimens_t fn,
		  loc_t *loc,
		  struct timespec tv[2]);

call_stub_t *
fop_utimens_cbk_stub (call_frame_t *frame,
		      fop_utimens_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf);

call_stub_t *
fop_access_stub (call_frame_t *frame,
		 fop_access_t fn,
		 loc_t *loc,
		 int32_t mask);

call_stub_t *
fop_access_cbk_stub (call_frame_t *frame,
		     fop_access_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno);

call_stub_t *
fop_readlink_stub (call_frame_t *frame,
		   fop_readlink_t fn,
		   loc_t *loc,
		   size_t size);

call_stub_t *
fop_readlink_cbk_stub (call_frame_t *frame,
		       fop_readlink_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       const char *path,
                       struct stat *buf);

call_stub_t *
fop_mknod_stub (call_frame_t *frame,
		fop_mknod_t fn,
		loc_t *loc,
		mode_t mode,
		dev_t rdev);

call_stub_t *
fop_mknod_cbk_stub (call_frame_t *frame,
		    fop_mknod_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
                    struct stat *buf,
                    struct stat *preparent,
                    struct stat *postparent);

call_stub_t *
fop_mkdir_stub (call_frame_t *frame,
		fop_mkdir_t fn,
		loc_t *loc,
		mode_t mode);

call_stub_t *
fop_mkdir_cbk_stub (call_frame_t *frame,
		    fop_mkdir_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
                    struct stat *buf,
                    struct stat *preparent,
                    struct stat *postparent);

call_stub_t *
fop_unlink_stub (call_frame_t *frame,
		 fop_unlink_t fn,
		 loc_t *loc);

call_stub_t *
fop_unlink_cbk_stub (call_frame_t *frame,
		     fop_unlink_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
                     struct stat *preparent,
                     struct stat *postparent);

call_stub_t *
fop_rmdir_stub (call_frame_t *frame,
		fop_rmdir_t fn,
		loc_t *loc);

call_stub_t *
fop_rmdir_cbk_stub (call_frame_t *frame,
		    fop_rmdir_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
                    struct stat *preparent,
                    struct stat *postparent);

call_stub_t *
fop_symlink_stub (call_frame_t *frame,
		  fop_symlink_t fn,
		  const char *linkname,
		  loc_t *loc);

call_stub_t *
fop_symlink_cbk_stub (call_frame_t *frame,
		      fop_symlink_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      inode_t *inode,
                      struct stat *buf,
                      struct stat *preparent,
                      struct stat *postparent);

call_stub_t *
fop_rename_stub (call_frame_t *frame,
		 fop_rename_t fn,
		 loc_t *oldloc,
		 loc_t *newloc);

call_stub_t *
fop_rename_cbk_stub (call_frame_t *frame,
		     fop_rename_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf,
                     struct stat *preoldparent,
                     struct stat *postoldparent,
                     struct stat *prenewparent,
                     struct stat *postnewparent);

call_stub_t *
fop_link_stub (call_frame_t *frame,
	       fop_link_t fn,
	       loc_t *oldloc,
	       loc_t *newloc);

call_stub_t *
fop_link_cbk_stub (call_frame_t *frame,
		   fop_link_cbk_t fn,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
                   struct stat *buf,
                   struct stat *preparent,
                   struct stat *postparent);

call_stub_t *
fop_create_stub (call_frame_t *frame,
		 fop_create_t fn,
		 loc_t *loc,
		 int32_t flags,
		 mode_t mode, fd_t *fd);

call_stub_t *
fop_create_cbk_stub (call_frame_t *frame,
		     fop_create_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     fd_t *fd,
		     inode_t *inode,
		     struct stat *buf,
                     struct stat *preparent,
                     struct stat *postparent);

call_stub_t *
fop_open_stub (call_frame_t *frame,
	       fop_open_t fn,
	       loc_t *loc,
	       int32_t flags,
	       fd_t *fd,
               int32_t wbflags);

call_stub_t *
fop_open_cbk_stub (call_frame_t *frame,
		   fop_open_cbk_t fn,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd);

call_stub_t *
fop_readv_stub (call_frame_t *frame,
		fop_readv_t fn,
		fd_t *fd,
		size_t size,
		off_t off);

call_stub_t *
fop_readv_cbk_stub (call_frame_t *frame,
		    fop_readv_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct iovec *vector,
		    int32_t count,
		    struct stat *stbuf,
                    struct iobref *iobref);

call_stub_t *
fop_writev_stub (call_frame_t *frame,
		 fop_writev_t fn,
		 fd_t *fd,
		 struct iovec *vector,
		 int32_t count,
		 off_t off,
                 struct iobref *iobref);

call_stub_t *
fop_writev_cbk_stub (call_frame_t *frame,
		     fop_writev_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
                     struct stat *prebuf,
                     struct stat *postbuf);

call_stub_t *
fop_flush_stub (call_frame_t *frame,
		fop_flush_t fn,
		fd_t *fd);

call_stub_t *
fop_flush_cbk_stub (call_frame_t *frame,
		    fop_flush_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno);

call_stub_t *
fop_fsync_stub (call_frame_t *frame,
		fop_fsync_t fn,
		fd_t *fd,
		int32_t datasync);

call_stub_t *
fop_fsync_cbk_stub (call_frame_t *frame,
		    fop_fsync_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
                    struct stat *prebuf,
                    struct stat *postbuf);

call_stub_t *
fop_opendir_stub (call_frame_t *frame,
		  fop_opendir_t fn,
		  loc_t *loc, fd_t *fd);

call_stub_t *
fop_opendir_cbk_stub (call_frame_t *frame,
		      fop_opendir_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      fd_t *fd);

call_stub_t *
fop_getdents_stub (call_frame_t *frame,
		   fop_getdents_t fn,
		   fd_t *fd,
		   size_t size,
		   off_t off,
		   int32_t flag);

call_stub_t *
fop_getdents_cbk_stub (call_frame_t *frame,
		       fop_getdents_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       dir_entry_t *entries,
		       int32_t count);

call_stub_t *
fop_setdents_stub (call_frame_t *frame,
		   fop_setdents_t fn,
		   fd_t *fd,
		   int32_t flags,
		   dir_entry_t *entries,
		   int32_t count);

call_stub_t *
fop_setdents_cbk_stub (call_frame_t *frame,
		       fop_setdents_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno);

call_stub_t *
fop_fsyncdir_stub (call_frame_t *frame,
		   fop_fsyncdir_t fn,
		   fd_t *fd,
		   int32_t datasync);

call_stub_t *
fop_fsyncdir_cbk_stub (call_frame_t *frame,
		       fop_fsyncdir_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno);

call_stub_t *
fop_statfs_stub (call_frame_t *frame,
		 fop_statfs_t fn,
		 loc_t *loc);

call_stub_t *
fop_statfs_cbk_stub (call_frame_t *frame,
		     fop_statfs_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct statvfs *buf);

call_stub_t *
fop_setxattr_stub (call_frame_t *frame,
		   fop_setxattr_t fn,
		   loc_t *loc,
		   dict_t *dict,
		   int32_t flags);

call_stub_t *
fop_setxattr_cbk_stub (call_frame_t *frame,
		       fop_setxattr_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno);

call_stub_t *
fop_getxattr_stub (call_frame_t *frame,
		   fop_getxattr_t fn,
		   loc_t *loc,
		   const char *name);

call_stub_t *
fop_getxattr_cbk_stub (call_frame_t *frame,
		       fop_getxattr_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       dict_t *value);

call_stub_t *
fop_fsetxattr_stub (call_frame_t *frame,
                    fop_fsetxattr_t fn,
                    fd_t *fd,
                    dict_t *dict,
                    int32_t flags);

call_stub_t *
fop_fsetxattr_cbk_stub (call_frame_t *frame,
                        fop_fsetxattr_cbk_t fn,
                        int32_t op_ret,
                        int32_t op_errno);

call_stub_t *
fop_fgetxattr_stub (call_frame_t *frame,
                    fop_fgetxattr_t fn,
                    fd_t *fd,
                    const char *name);

call_stub_t *
fop_fgetxattr_cbk_stub (call_frame_t *frame,
                        fop_fgetxattr_cbk_t fn,
                        int32_t op_ret,
                        int32_t op_errno,
                        dict_t *value);

call_stub_t *
fop_removexattr_stub (call_frame_t *frame,
		      fop_removexattr_t fn,
		      loc_t *loc,
		      const char *name);

call_stub_t *
fop_removexattr_cbk_stub (call_frame_t *frame,
			  fop_removexattr_cbk_t fn,
			  int32_t op_ret,
			  int32_t op_errno);
call_stub_t *
fop_lk_stub (call_frame_t *frame,
	     fop_lk_t fn,
	     fd_t *fd,
	     int32_t cmd,
	     struct flock *lock);

call_stub_t *
fop_lk_cbk_stub (call_frame_t *frame,
		 fop_lk_cbk_t fn,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct flock *lock);

call_stub_t *
fop_inodelk_stub (call_frame_t *frame, fop_inodelk_t fn,
		  const char *volume, loc_t *loc, int32_t cmd, 
                  struct flock *lock);

call_stub_t *
fop_finodelk_stub (call_frame_t *frame, fop_finodelk_t fn,
		   const char *volume, fd_t *fd, int32_t cmd, 
                   struct flock *lock);

call_stub_t *
fop_entrylk_stub (call_frame_t *frame, fop_entrylk_t fn,
		  const char *volume, loc_t *loc, const char *basename,
		  entrylk_cmd cmd, entrylk_type type);

call_stub_t *
fop_fentrylk_stub (call_frame_t *frame, fop_fentrylk_t fn,
		   const char *volume, fd_t *fd, const char *basename,
		   entrylk_cmd cmd, entrylk_type type);

call_stub_t *
fop_inodelk_cbk_stub (call_frame_t *frame, fop_inodelk_cbk_t fn,
		      int32_t op_ret, int32_t op_errno);

call_stub_t *
fop_finodelk_cbk_stub (call_frame_t *frame, fop_inodelk_cbk_t fn,
		       int32_t op_ret, int32_t op_errno);

call_stub_t *
fop_entrylk_cbk_stub (call_frame_t *frame, fop_entrylk_cbk_t fn,
		      int32_t op_ret, int32_t op_errno);

call_stub_t *
fop_fentrylk_cbk_stub (call_frame_t *frame, fop_entrylk_cbk_t fn,
		       int32_t op_ret, int32_t op_errno);

call_stub_t *
fop_readdir_stub (call_frame_t *frame,
		  fop_readdir_t fn,
		  fd_t *fd,
		  size_t size,
		  off_t off);

call_stub_t *
fop_readdirp_stub (call_frame_t *frame,
		   fop_readdir_t fn,
		   fd_t *fd,
		   size_t size,
		   off_t off);

call_stub_t *
fop_readdirp_cbk_stub (call_frame_t *frame,
		       fop_readdir_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       gf_dirent_t *entries);

call_stub_t *
fop_readdir_cbk_stub (call_frame_t *frame,
		      fop_readdir_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      gf_dirent_t *entries);

call_stub_t *
fop_checksum_stub (call_frame_t *frame,
		   fop_checksum_t fn,
		   loc_t *loc,
		   int32_t flags);

call_stub_t *
fop_checksum_cbk_stub (call_frame_t *frame,
		       fop_checksum_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       uint8_t *file_checksum,
		       uint8_t *dir_checksum);

call_stub_t *
fop_rchecksum_stub (call_frame_t *frame,
                    fop_rchecksum_t fn,
                    fd_t *fd, off_t offset,
                    int32_t len);

call_stub_t *
fop_rchecksum_cbk_stub (call_frame_t *frame,
                        fop_rchecksum_cbk_t fn,
                        int32_t op_ret,
                        int32_t op_errno,
                        uint32_t weak_checksum,
                        uint8_t *strong_checksum);

call_stub_t *
fop_xattrop_stub (call_frame_t *frame,
		  fop_xattrop_t fn,
		  loc_t *loc,
		  gf_xattrop_flags_t optype,
		  dict_t *xattr);

call_stub_t *
fop_xattrop_stub_cbk_stub (call_frame_t *frame,
			   fop_xattrop_cbk_t fn,
			   int32_t op_ret,
			   int32_t op_errno);

call_stub_t *
fop_fxattrop_stub (call_frame_t *frame,
		   fop_fxattrop_t fn,
		   fd_t *fd,
		   gf_xattrop_flags_t optype,
		   dict_t *xattr);

call_stub_t *
fop_fxattrop_stub_cbk_stub (call_frame_t *frame,
			    fop_xattrop_cbk_t fn,
			    int32_t op_ret,
			    int32_t op_errno);

call_stub_t *
fop_lock_notify_stub_cbk_stub (call_frame_t *frame,
			       fop_lock_notify_cbk_t fn,
			       int32_t op_ret,
			       int32_t op_errno);

call_stub_t *
fop_lock_notify_stub (call_frame_t *frame,
		      fop_lock_notify_t fn,
		      loc_t *loc,
		      int32_t timeout);

call_stub_t *
fop_lock_fnotify_stub_cbk_stub (call_frame_t *frame,
				fop_lock_fnotify_cbk_t fn,
				int32_t op_ret,
				int32_t op_errno);

call_stub_t *
fop_lock_fnotify_stub (call_frame_t *frame,
		       fop_lock_fnotify_t fn,
		       fd_t *fd,
		       int32_t timeout);

call_stub_t *
fop_setattr_stub (call_frame_t *frame,
                  fop_setattr_t fn,
                  loc_t *loc,
                  struct stat *stbuf,
                  int32_t valid);

call_stub_t *
fop_setattr_cbk_stub (call_frame_t *frame,
                      fop_setattr_cbk_t fn,
                      int32_t op_ret,
                      int32_t op_errno,
                      struct stat *statpre,
                      struct stat *statpost);

call_stub_t *
fop_fsetattr_stub (call_frame_t *frame,
                   fop_fsetattr_t fn,
                   fd_t *fd,
                   struct stat *stbuf,
                   int32_t valid);

call_stub_t *
fop_fsetattr_cbk_stub (call_frame_t *frame,
                       fop_setattr_cbk_t fn,
                       int32_t op_ret,
                       int32_t op_errno,
                       struct stat *statpre,
                       struct stat *statpost);

void call_resume (call_stub_t *stub);
void call_stub_destroy (call_stub_t *stub);
#endif
