/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _XLATOR_H
#define _XLATOR_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>


#include "glusterfs.h"
#include "logging.h"
#include "common-utils.h"
#include "dict.h"
#include "compat.h"
#include "list.h"

#define FIRST_CHILD(xl) (xl->children->xlator)

#define GF_SET_ATTR_MODE  0x1
#define GF_SET_ATTR_UID   0x2
#define GF_SET_ATTR_GID   0x4
#define GF_SET_ATTR_SIZE  0x8
#define GF_SET_ATTR_ATIME 0x10
#define GF_SET_ATTR_MTIME 0x20

struct _xlator;
typedef struct _xlator xlator_t;
struct _dir_entry_t;
typedef struct _dir_entry_t dir_entry_t;
struct _gf_dirent_t;
typedef struct _gf_dirent_t gf_dirent_t;
struct _loc;
typedef struct _loc loc_t;


typedef int32_t (*event_notify_fn_t) (xlator_t *this,
				      int32_t event,
				      void *data,
				      ...);

#include "list.h"
#include "gf-dirent.h"
#include "stack.h"
#include "iobuf.h"
#include "inode.h"
#include "fd.h"
#include "globals.h"

struct _loc {
	const char *path;
	const char *name;
	ino_t       ino;
	inode_t    *inode;
	inode_t    *parent;
};


struct xlator_stats {
	uint64_t nr_files;         /* Number of files open via this xlator */
	uint64_t free_disk;        /* Mega bytes */
	uint64_t total_disk_size;  /* Mega Bytes */
	uint64_t disk_usage;       /* Mega bytes */
	uint64_t disk_speed;       /* MHz or Mbps */
	uint64_t nr_clients;       /* Number of client nodes */
	uint64_t write_usage;
	uint64_t read_usage;       /* add more stats here */
};



typedef int32_t (*mop_stats_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
				    struct xlator_stats *stats);

typedef int32_t (*mop_getspec_cbk_t) (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno,
				      char *spec_data);

typedef int32_t (*mop_log_cbk_t) (call_frame_t *frame,
                                  void *cookie, xlator_t *this,
                                  int32_t op_ret, int32_t op_errno);

typedef int32_t (*fop_checksum_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno,
				       uint8_t *file_checksum,
				       uint8_t *dir_checksum);

typedef int32_t (*fop_rchecksum_cbk_t) (call_frame_t *frame,
                                        void *cookie,
                                        xlator_t *this,
                                        int32_t op_ret,
                                        int32_t op_errno,
                                        uint32_t weak_checksum,
                                        uint8_t *strong_checksum);

typedef int32_t (*mop_setvolume_t) (call_frame_t *frame,
				    xlator_t *this,
				    const char *volume);

typedef int32_t (*mop_stats_t) (call_frame_t *frame,
				xlator_t *this,
				int32_t flags);

typedef int32_t (*mop_getspec_t) (call_frame_t *frame,
				  xlator_t *this,
				  const char *key,
				  int32_t flag);

typedef int32_t (*mop_log_t) (call_frame_t *frame,
                              xlator_t *this,
                              const char *msg);

typedef int32_t (*fop_checksum_t) (call_frame_t *frame,
				   xlator_t *this,
				   loc_t *loc,
				   int32_t flag);

typedef int32_t (*fop_rchecksum_t) (call_frame_t *frame,
                                    xlator_t *this,
                                    fd_t *fd, off_t offset,
                                    int32_t len);

struct xlator_mops {
	mop_stats_t            stats;
	mop_getspec_t          getspec;
        mop_log_t              log;

        mop_log_cbk_t          log_cbk;
	mop_stats_cbk_t        stats_cbk;
	mop_getspec_cbk_t      getspec_cbk;
};


typedef int32_t (*fop_lookup_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
				     inode_t *inode,
				     struct stat *buf,
                                     dict_t *xattr,
                                     struct stat *postparent);

typedef int32_t (*fop_stat_cbk_t) (call_frame_t *frame,
				   void *cookie,
				   xlator_t *this,
				   int32_t op_ret,
				   int32_t op_errno,
				   struct stat *buf);

typedef int32_t (*fop_fstat_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
				    struct stat *buf);

typedef int32_t (*fop_truncate_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno,
				       struct stat *prebuf,
                                       struct stat *postbuf);

typedef int32_t (*fop_ftruncate_cbk_t) (call_frame_t *frame,
					void *cookie,
					xlator_t *this,
					int32_t op_ret,
					int32_t op_errno,
					struct stat *prebuf,
                                        struct stat *postbuf);

typedef int32_t (*fop_access_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno);

typedef int32_t (*fop_readlink_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno,
				       const char *path,
                                       struct stat *buf);

typedef int32_t (*fop_mknod_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
				    inode_t *inode,
                                    struct stat *buf,
                                    struct stat *preparent,
                                    struct stat *postparent);

typedef int32_t (*fop_mkdir_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
				    inode_t *inode,
                                    struct stat *buf,
                                    struct stat *preparent,
                                    struct stat *postparent);

typedef int32_t (*fop_unlink_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
                                     struct stat *preparent,
                                     struct stat *postparent);

typedef int32_t (*fop_rmdir_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
                                    struct stat *preparent,
                                    struct stat *postparent);

typedef int32_t (*fop_symlink_cbk_t) (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno,
				      inode_t *inode,
                                      struct stat *buf,
                                      struct stat *preparent,
                                      struct stat *postparent);

typedef int32_t (*fop_rename_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
				     struct stat *buf,
                                     struct stat *preoldparent,
                                     struct stat *postoldparent,
                                     struct stat *prenewparent,
                                     struct stat *postnewparent);

typedef int32_t (*fop_link_cbk_t) (call_frame_t *frame,
				   void *cookie,
				   xlator_t *this,
				   int32_t op_ret,
				   int32_t op_errno,
				   inode_t *inode,
                                   struct stat *buf,
                                   struct stat *preparent,
                                   struct stat *postparent);

typedef int32_t (*fop_create_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
				     fd_t *fd,
				     inode_t *inode,
				     struct stat *buf,
                                     struct stat *preparent,
                                     struct stat *postparent);

typedef int32_t (*fop_open_cbk_t) (call_frame_t *frame,
				   void *cookie,
				   xlator_t *this,
				   int32_t op_ret,
				   int32_t op_errno,
				   fd_t *fd);

typedef int32_t (*fop_readv_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
				    struct iovec *vector,
				    int32_t count,
				    struct stat *stbuf,
                                    struct iobref *iobref);

typedef int32_t (*fop_writev_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
				     struct stat *prebuf,
                                     struct stat *postbuf);

typedef int32_t (*fop_flush_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno);

typedef int32_t (*fop_fsync_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
                                    struct stat *prebuf,
                                    struct stat *postbuf);

typedef int32_t (*fop_opendir_cbk_t) (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno,
				      fd_t *fd);

typedef int32_t (*fop_getdents_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno,
				       dir_entry_t *entries,
				       int32_t count);

typedef int32_t (*fop_fsyncdir_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno);

typedef int32_t (*fop_statfs_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
				     struct statvfs *buf);

typedef int32_t (*fop_setxattr_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno);

typedef int32_t (*fop_getxattr_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno,
				       dict_t *dict);

typedef int32_t (*fop_fsetxattr_cbk_t) (call_frame_t *frame,
                                        void *cookie,
                                        xlator_t *this,
                                        int32_t op_ret,
                                        int32_t op_errno);

typedef int32_t (*fop_fgetxattr_cbk_t) (call_frame_t *frame,
                                        void *cookie,
                                        xlator_t *this,
                                        int32_t op_ret,
                                        int32_t op_errno,
                                        dict_t *dict);

typedef int32_t (*fop_removexattr_cbk_t) (call_frame_t *frame,
					  void *cookie,
					  xlator_t *this,
					  int32_t op_ret,
					  int32_t op_errno);

typedef int32_t (*fop_lk_cbk_t) (call_frame_t *frame,
				 void *cookie,
				 xlator_t *this,
				 int32_t op_ret,
				 int32_t op_errno,
				 struct flock *flock);

typedef int32_t (*fop_inodelk_cbk_t) (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno);

typedef int32_t (*fop_finodelk_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno);

typedef int32_t (*fop_entrylk_cbk_t) (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno);

typedef int32_t (*fop_fentrylk_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno);

typedef int32_t (*fop_setdents_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno);

typedef int32_t (*fop_readdir_cbk_t) (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno,
				      gf_dirent_t *entries);

typedef int32_t (*fop_readdirp_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno,
				       gf_dirent_t *entries);

typedef int32_t (*fop_xattrop_cbk_t) (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno,
				      dict_t *xattr);

typedef int32_t (*fop_fxattrop_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno,
				       dict_t *xattr);

typedef int32_t (*fop_lock_notify_cbk_t) (call_frame_t *frame,
                                          void *cookie,
                                          xlator_t *this,
                                          int32_t op_ret,
                                          int32_t op_errno);

typedef int32_t (*fop_lock_fnotify_cbk_t) (call_frame_t *frame,
                                           void *cookie,
                                           xlator_t *this,
                                           int32_t op_ret,
                                           int32_t op_errno);

typedef int32_t (*fop_setattr_cbk_t) (call_frame_t *frame,
                                      void *cookie,
                                      xlator_t *this,
                                      int32_t op_ret,
                                      int32_t op_errno,
                                      struct stat *preop_stbuf,
                                      struct stat *postop_stbuf);

typedef int32_t (*fop_fsetattr_cbk_t) (call_frame_t *frame,
                                       void *cookie,
                                       xlator_t *this,
                                       int32_t op_ret,
                                       int32_t op_errno,
                                       struct stat *preop_stbuf,
                                       struct stat *postop_stbuf);

typedef int32_t (*fop_lookup_t) (call_frame_t *frame,
				 xlator_t *this,
				 loc_t *loc,
				 dict_t *xattr_req);

typedef int32_t (*fop_stat_t) (call_frame_t *frame,
			       xlator_t *this,
			       loc_t *loc);

typedef int32_t (*fop_fstat_t) (call_frame_t *frame,
				xlator_t *this,
				fd_t *fd);

typedef int32_t (*fop_truncate_t) (call_frame_t *frame,
				   xlator_t *this,
				   loc_t *loc,
				   off_t offset);

typedef int32_t (*fop_ftruncate_t) (call_frame_t *frame,
				    xlator_t *this,
				    fd_t *fd,
				    off_t offset);

typedef int32_t (*fop_access_t) (call_frame_t *frame,
				 xlator_t *this,
				 loc_t *loc,
				 int32_t mask);

typedef int32_t (*fop_readlink_t) (call_frame_t *frame,
				   xlator_t *this,
				   loc_t *loc,
				   size_t size);

typedef int32_t (*fop_mknod_t) (call_frame_t *frame,
				xlator_t *this,
				loc_t *loc,
				mode_t mode,
				dev_t rdev);

typedef int32_t (*fop_mkdir_t) (call_frame_t *frame,
				xlator_t *this,
				loc_t *loc,
				mode_t mode);

typedef int32_t (*fop_unlink_t) (call_frame_t *frame,
				 xlator_t *this,
				 loc_t *loc);

typedef int32_t (*fop_rmdir_t) (call_frame_t *frame,
				xlator_t *this,
				loc_t *loc);

typedef int32_t (*fop_symlink_t) (call_frame_t *frame,
				  xlator_t *this,
				  const char *linkname,
				  loc_t *loc);

typedef int32_t (*fop_rename_t) (call_frame_t *frame,
				 xlator_t *this,
				 loc_t *oldloc,
				 loc_t *newloc);

typedef int32_t (*fop_link_t) (call_frame_t *frame,
			       xlator_t *this,
			       loc_t *oldloc,
			       loc_t *newloc);

typedef int32_t (*fop_create_t) (call_frame_t *frame,
				 xlator_t *this,
				 loc_t *loc,
				 int32_t flags,
				 mode_t mode,
				 fd_t *fd);

/* Tell subsequent writes on the fd_t to fsync after every writev fop without
 * requiring a fsync fop.
 */
#define GF_OPEN_FSYNC   0x01

/* Tell write-behind to disable writing behind despite O_SYNC not being set.
 */
#define GF_OPEN_NOWB    0x02

typedef int32_t (*fop_open_t) (call_frame_t *frame,
			       xlator_t *this,
			       loc_t *loc,
			       int32_t flags,
			       fd_t *fd,
                               int32_t wbflags);

typedef int32_t (*fop_readv_t) (call_frame_t *frame,
				xlator_t *this,
				fd_t *fd,
				size_t size,
				off_t offset);

typedef int32_t (*fop_writev_t) (call_frame_t *frame,
				 xlator_t *this,
				 fd_t *fd,
				 struct iovec *vector,
				 int32_t count,
				 off_t offset,
                                 struct iobref *iobref);

typedef int32_t (*fop_flush_t) (call_frame_t *frame,
				xlator_t *this,
				fd_t *fd);

typedef int32_t (*fop_fsync_t) (call_frame_t *frame,
				xlator_t *this,
				fd_t *fd,
				int32_t datasync);

typedef int32_t (*fop_opendir_t) (call_frame_t *frame,
				  xlator_t *this,
				  loc_t *loc,
				  fd_t *fd);

typedef int32_t (*fop_getdents_t) (call_frame_t *frame,
				   xlator_t *this,
				   fd_t *fd,
				   size_t size,
				   off_t offset,
				   int32_t flag);

typedef int32_t (*fop_fsyncdir_t) (call_frame_t *frame,
				   xlator_t *this,
				   fd_t *fd,
				   int32_t datasync);

typedef int32_t (*fop_statfs_t) (call_frame_t *frame,
				 xlator_t *this,
				 loc_t *loc);

typedef int32_t (*fop_setxattr_t) (call_frame_t *frame,
				   xlator_t *this,
				   loc_t *loc,
				   dict_t *dict,
				   int32_t flags);

typedef int32_t (*fop_getxattr_t) (call_frame_t *frame,
				   xlator_t *this,
				   loc_t *loc,
				   const char *name);

typedef int32_t (*fop_fsetxattr_t) (call_frame_t *frame,
                                    xlator_t *this,
                                    fd_t *fd,
                                    dict_t *dict,
                                    int32_t flags);

typedef int32_t (*fop_fgetxattr_t) (call_frame_t *frame,
                                    xlator_t *this,
                                    fd_t *fd,
                                    const char *name);

typedef int32_t (*fop_removexattr_t) (call_frame_t *frame,
				      xlator_t *this,
				      loc_t *loc,
				      const char *name);

typedef int32_t (*fop_lk_t) (call_frame_t *frame,
			     xlator_t *this,
			     fd_t *fd,
			     int32_t cmd,
			     struct flock *flock);

typedef int32_t (*fop_inodelk_t) (call_frame_t *frame,
				  xlator_t *this,
                                  const char *volume,
				  loc_t *loc,
				  int32_t cmd,
				  struct flock *flock);

typedef int32_t (*fop_finodelk_t) (call_frame_t *frame,
				   xlator_t *this,
                                   const char *volume,
				   fd_t *fd,
				   int32_t cmd,
				   struct flock *flock);

typedef int32_t (*fop_entrylk_t) (call_frame_t *frame,
				  xlator_t *this, 
                                  const char *volume, loc_t *loc,
				  const char *basename, entrylk_cmd cmd,
				  entrylk_type type);

typedef int32_t (*fop_fentrylk_t) (call_frame_t *frame,
				   xlator_t *this, 
                                   const char *volume, fd_t *fd,
				   const char *basename, entrylk_cmd cmd,
				   entrylk_type type);

typedef int32_t (*fop_setdents_t) (call_frame_t *frame,
				   xlator_t *this,
				   fd_t *fd,
				   int32_t flags,
				   dir_entry_t *entries,
				   int32_t count);

typedef int32_t (*fop_readdir_t) (call_frame_t *frame,
				  xlator_t *this,
				  fd_t *fd,
				  size_t size,
				  off_t offset);

typedef int32_t (*fop_readdirp_t) (call_frame_t *frame,
			           xlator_t *this,
				   fd_t *fd,
				   size_t size,
				   off_t offset);

typedef int32_t (*fop_xattrop_t) (call_frame_t *frame,
				  xlator_t *this,
				  loc_t *loc,
				  gf_xattrop_flags_t optype,
				  dict_t *xattr);

typedef int32_t (*fop_fxattrop_t) (call_frame_t *frame,
				   xlator_t *this,
				   fd_t *fd,
				   gf_xattrop_flags_t optype,
				   dict_t *xattr);

typedef int32_t (*fop_lock_notify_t) (call_frame_t *frame,
                                      xlator_t *this, loc_t *loc,
                                      int32_t timeout);

typedef int32_t (*fop_lock_fnotify_t) (call_frame_t *frame,
                                       xlator_t *this, fd_t *fd,
                                       int32_t timeout);

typedef int32_t (*fop_setattr_t) (call_frame_t *frame,
                                  xlator_t *this,
                                  loc_t *loc,
                                  struct stat *stbuf,
                                  int32_t valid);

typedef int32_t (*fop_fsetattr_t) (call_frame_t *frame,
                                   xlator_t *this,
                                   fd_t *fd,
                                   struct stat *stbuf,
                                   int32_t valid);


struct xlator_fops {
	fop_lookup_t         lookup;
	fop_stat_t           stat;
	fop_fstat_t          fstat;
	fop_truncate_t       truncate;
	fop_ftruncate_t      ftruncate;
	fop_access_t         access;
	fop_readlink_t       readlink;
	fop_mknod_t          mknod;
	fop_mkdir_t          mkdir;
	fop_unlink_t         unlink;
	fop_rmdir_t          rmdir;
	fop_symlink_t        symlink;
	fop_rename_t         rename;
	fop_link_t           link;
	fop_create_t         create;
	fop_open_t           open;
	fop_readv_t          readv;
	fop_writev_t         writev;
	fop_flush_t          flush;
	fop_fsync_t          fsync;
	fop_opendir_t        opendir;
	fop_readdir_t        readdir;
	fop_readdirp_t       readdirp;
	fop_fsyncdir_t       fsyncdir;
	fop_statfs_t         statfs;
	fop_setxattr_t       setxattr;
	fop_getxattr_t       getxattr;
	fop_fsetxattr_t      fsetxattr;
	fop_fgetxattr_t      fgetxattr;
	fop_removexattr_t    removexattr;
	fop_lk_t             lk;
	fop_inodelk_t        inodelk;
	fop_finodelk_t       finodelk;
	fop_entrylk_t        entrylk;
	fop_fentrylk_t       fentrylk;
	fop_setdents_t       setdents;
	fop_getdents_t       getdents;
	fop_checksum_t       checksum;
	fop_rchecksum_t      rchecksum;
	fop_xattrop_t        xattrop;
	fop_fxattrop_t       fxattrop;
	fop_lock_notify_t    lock_notify;
	fop_lock_fnotify_t   lock_fnotify;
        fop_setattr_t        setattr;
        fop_fsetattr_t       fsetattr;

	/* these entries are used for a typechecking hack in STACK_WIND _only_ */
	fop_lookup_cbk_t         lookup_cbk;
	fop_stat_cbk_t           stat_cbk;
	fop_fstat_cbk_t          fstat_cbk;
	fop_truncate_cbk_t       truncate_cbk;
	fop_ftruncate_cbk_t      ftruncate_cbk;
	fop_access_cbk_t         access_cbk;
	fop_readlink_cbk_t       readlink_cbk;
	fop_mknod_cbk_t          mknod_cbk;
	fop_mkdir_cbk_t          mkdir_cbk;
	fop_unlink_cbk_t         unlink_cbk;
	fop_rmdir_cbk_t          rmdir_cbk;
	fop_symlink_cbk_t        symlink_cbk;
	fop_rename_cbk_t         rename_cbk;
	fop_link_cbk_t           link_cbk;
	fop_create_cbk_t         create_cbk;
	fop_open_cbk_t           open_cbk;
	fop_readv_cbk_t          readv_cbk;
	fop_writev_cbk_t         writev_cbk;
	fop_flush_cbk_t          flush_cbk;
	fop_fsync_cbk_t          fsync_cbk;
	fop_opendir_cbk_t        opendir_cbk;
	fop_readdir_cbk_t        readdir_cbk;
	fop_readdirp_cbk_t       readdirp_cbk;
	fop_fsyncdir_cbk_t       fsyncdir_cbk;
	fop_statfs_cbk_t         statfs_cbk;
	fop_setxattr_cbk_t       setxattr_cbk;
	fop_getxattr_cbk_t       getxattr_cbk;
	fop_fsetxattr_cbk_t      fsetxattr_cbk;
	fop_fgetxattr_cbk_t      fgetxattr_cbk;
	fop_removexattr_cbk_t    removexattr_cbk;
	fop_lk_cbk_t             lk_cbk;
	fop_inodelk_cbk_t        inodelk_cbk;
	fop_finodelk_cbk_t       finodelk_cbk;
	fop_entrylk_cbk_t        entrylk_cbk;
	fop_fentrylk_cbk_t       fentrylk_cbk;
	fop_setdents_cbk_t       setdents_cbk;
	fop_getdents_cbk_t       getdents_cbk;
	fop_checksum_cbk_t       checksum_cbk;
	fop_rchecksum_cbk_t      rchecksum_cbk;
	fop_xattrop_cbk_t        xattrop_cbk;
	fop_fxattrop_cbk_t       fxattrop_cbk;
	fop_lock_notify_cbk_t    lock_notify_cbk;
	fop_lock_fnotify_cbk_t   lock_fnotify_cbk;
        fop_setattr_cbk_t        setattr_cbk;
        fop_fsetattr_cbk_t       fsetattr_cbk;
};

typedef int32_t (*cbk_forget_t) (xlator_t *this,
				 inode_t *inode);

typedef int32_t (*cbk_release_t) (xlator_t *this,
				  fd_t *fd);

struct xlator_cbks {
	cbk_forget_t    forget;
	cbk_release_t   release;
	cbk_release_t   releasedir;
};

typedef int32_t (*dumpop_priv_t) (xlator_t *this);

typedef int32_t (*dumpop_inode_t) (xlator_t *this);

typedef int32_t (*dumpop_fd_t)  (xlator_t   *this);

typedef int32_t (*dumpop_inodectx_t) (xlator_t *this, inode_t *ino);

                              
struct xlator_dumpops {
	dumpop_priv_t            priv;
	dumpop_inode_t           inode;
        dumpop_fd_t              fd;
	dumpop_inodectx_t        inodectx;
};

typedef struct xlator_list {
	xlator_t           *xlator;
	struct xlator_list *next;
} xlator_list_t;

/* Add possible new type of option you may need */
typedef enum {
  	GF_OPTION_TYPE_ANY = 0,
  	GF_OPTION_TYPE_STR,
  	GF_OPTION_TYPE_INT,
  	GF_OPTION_TYPE_SIZET,
  	GF_OPTION_TYPE_PERCENT,
        GF_OPTION_TYPE_PERCENT_OR_SIZET,
  	GF_OPTION_TYPE_BOOL,
  	GF_OPTION_TYPE_XLATOR,
  	GF_OPTION_TYPE_PATH,
  	GF_OPTION_TYPE_TIME,
	GF_OPTION_TYPE_DOUBLE,
        GF_OPTION_TYPE_INTERNET_ADDRESS,
} volume_option_type_t;

#define ZR_VOLUME_MAX_NUM_KEY    4
#define ZR_OPTION_MAX_ARRAY_SIZE 64

/* Each translator should define this structure */
typedef struct volume_options {
  	char                *key[ZR_VOLUME_MAX_NUM_KEY]; 
	                           /* different key, same meaning */
  	volume_option_type_t type;       
  	int64_t              min;  /* -1 means no range */
  	int64_t              max;  /* -1 means no range */
  	char                *value[ZR_OPTION_MAX_ARRAY_SIZE];  
                                   /* If specified, will check for one of 
				      the value from this array */
	char                *description; /* about the key */
} volume_option_t;

typedef struct vol_opt_list {
	struct list_head  list;
	volume_option_t  *given_opt;
} volume_opt_list_t;

struct _xlator {
	/* Built during parsing */
	char          *name;
	char          *type;
	xlator_t      *next;
	xlator_t      *prev;
	xlator_list_t *parents;
	xlator_list_t *children;
	dict_t        *options;
	
	/* Set after doing dlopen() */
	struct xlator_fops    *fops;
	struct xlator_mops    *mops;
	struct xlator_cbks    *cbks;
	struct xlator_dumpops *dumpops;
	struct list_head      volume_options;  /* list of volume_option_t */

	void              (*fini) (xlator_t *this);
	int32_t           (*init) (xlator_t *this);
	event_notify_fn_t notify;

	/* Misc */
	glusterfs_ctx_t  *ctx;
	inode_table_t    *itable;
	char              ready;
	char              init_succeeded;
	void             *private;
};

#define xlator_has_parent(xl) (xl->parents != NULL)

int validate_xlator_volume_options (xlator_t *xl, volume_option_t *opt);

int32_t xlator_set_type (xlator_t *xl, const char *type);

xlator_t *file_to_xlator_tree (glusterfs_ctx_t *ctx,
			       FILE *fp);

int xlator_notify (xlator_t *this, int32_t event, void *data, ...);
int xlator_init (xlator_t *this);

int32_t xlator_tree_init (xlator_t *xl);
int32_t xlator_tree_free (xlator_t *xl);

void xlator_tree_fini (xlator_t *xl);

void xlator_foreach (xlator_t *this,
		     void (*fn) (xlator_t *each,
				 void *data),
		     void *data);

xlator_t *xlator_search_by_name (xlator_t *any, const char *name);

void inode_destroy_notify (inode_t *inode, const char *xlname);

int loc_copy (loc_t *dst, loc_t *src);
#define loc_dup(src, dst) loc_copy(dst, src)
void loc_wipe (loc_t *loc);

#define GF_STAT_PRINT_FMT_STR "%"PRIx64",%"PRIx64",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx64",%"PRIx64",%"PRIx32",%"PRIx64",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32"\n"

#define GF_STAT_SCAN_FMT_STR "%"SCNx64",%"SCNx64",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx64",%"SCNx64",%"SCNx32",%"SCNx64",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32"\n"

#define GF_STATFS_PRINT_FMT_STR "%"PRIx32",%"PRIx32",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx32",%"PRIx32",%"PRIx32"\n"

#define GF_STATFS_SCAN_FMT_STR "%"SCNx32",%"SCNx32",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx32",%"SCNx32",%"SCNx32"\n"

#endif /* _XLATOR_H */

