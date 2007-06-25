/*
  (C) 2006,2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#ifndef _XLATOR_H
#define _XLATOR_H

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include "glusterfs.h"
#include "logging.h"
#include "common-utils.h"
#include "dict.h"

#define FIRST_CHILD(xl) (xl->children->xlator)

struct _xlator;
typedef struct _xlator xlator_t;
struct _dir_entry_t;
typedef struct _dir_entry_t dir_entry_t;
struct file_context;
typedef struct file_context file_ctx_t;
struct _fd;
typedef struct _fd fd_t;
struct _loc;
typedef struct _loc loc_t;

typedef int32_t (*event_notify_fn_t) (xlator_t *this,
				      int32_t event,
				      void *data,
				      ...);

#include "list.h"

struct _fd {
  struct list_head inode_list;
  pthread_mutex_t lock;
  int32_t ref;
  struct _inode *inode;
  dict_t *ctx;
};


#include "stack.h"
#include "inode.h"

fd_t *
fd_create (inode_t *inode);

void
fd_destroy (fd_t *fd);

struct _loc {
  const char *path;
  ino_t ino;
  inode_t *inode;
};

struct _dir_entry_t {
  dir_entry_t *next;
  char *name;
  struct stat buf;
};

struct xlator_stats {
  uint64_t nr_files;   /* Number of files open via this xlator */
  uint64_t free_disk; /* Mega bytes */
  uint64_t total_disk_size; /* Mega Bytes */
  uint64_t disk_usage; /* Mega bytes */
  uint64_t disk_speed; /* MHz or Mbps */
  uint64_t nr_clients; /* Number of client nodes (filled by glusterfsd) */
  uint64_t write_usage;
  uint64_t read_usage;
  /* add more stats here */
};



typedef int32_t (*mop_stats_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
				    struct xlator_stats *stats);

typedef int32_t (*mop_fsck_cbk_t) (call_frame_t *frame,
				   void *cookie,
				   xlator_t *this,
				   int32_t op_ret,
				   int32_t op_errno);

typedef int32_t (*mop_lock_cbk_t) (call_frame_t *frame,
				   void *cookie,
				   xlator_t *this,
				   int32_t op_ret,
				   int32_t op_errno);

typedef int32_t (*mop_unlock_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno);

typedef int32_t (*mop_listlocks_cbk_t) (call_frame_t *frame,
					void *cookie,
					xlator_t *this,
					int32_t op_ret,
					int32_t op_errno,
					char *locks);

typedef int32_t (*mop_getspec_cbk_t) (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno,
				      char *spec_data);


typedef int32_t (*mop_setvolume_t) (call_frame_t *frame,
				    xlator_t *this,
				    const char *volume);

typedef int32_t (*mop_stats_t) (call_frame_t *frame,
				xlator_t *this,
				int32_t flags);

typedef int32_t (*mop_fsck_t) (call_frame_t *frame,
			       xlator_t *this,
			       int32_t flags);

typedef int32_t (*mop_lock_t) (call_frame_t *frame,
			       xlator_t *this,
			       const char *name);

typedef int32_t (*mop_unlock_t) (call_frame_t *frame,
				 xlator_t *this,
				 const char *name);

typedef int32_t (*mop_listlocks_t) (call_frame_t *frame,
				    xlator_t *this,
				    const char *pattern);

typedef int32_t (*mop_getspec_t) (call_frame_t *frame,
				  xlator_t *this,
				  int32_t flag);

struct xlator_mops {
  mop_stats_t            stats;
  mop_fsck_t             fsck;
  mop_lock_t             lock;
  mop_unlock_t           unlock;
  mop_listlocks_t        listlocks;
  mop_getspec_t          getspec;

  mop_stats_cbk_t            stats_cbk;
  mop_fsck_cbk_t             fsck_cbk;
  mop_lock_cbk_t             lock_cbk;
  mop_unlock_cbk_t           unlock_cbk;
  mop_listlocks_cbk_t        listlocks_cbk;
  mop_getspec_cbk_t          getspec_cbk;
};


typedef int32_t (*cbk_fop_lookup_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
				     inode_t *inode,
				     struct stat *buf);

typedef int32_t (*fop_lookup_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
				     inode_t *inode,
				     struct stat *buf);

typedef int32_t (*fop_forget_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno);

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

typedef int32_t (*fop_chmod_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
				    struct stat *buf);

typedef int32_t (*fop_fchmod_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
				     struct stat *buf);

typedef int32_t (*fop_chown_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
				    struct stat *buf);

typedef int32_t (*fop_fchown_cbk_t) (call_frame_t *frame,
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
				       struct stat *buf);

typedef int32_t (*fop_ftruncate_cbk_t) (call_frame_t *frame,
					void *cookie,
					xlator_t *this,
					int32_t op_ret,
					int32_t op_errno,
					struct stat *buf);

typedef int32_t (*fop_utimens_cbk_t) (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno,
				      struct stat *buf);

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
				       const char *path);

typedef int32_t (*fop_mknod_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
				    inode_t *inode,
				    struct stat *buf);

typedef int32_t (*fop_mkdir_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
				    inode_t *inode,
				    struct stat *buf);

typedef int32_t (*fop_unlink_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno);

typedef int32_t (*fop_rmdir_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno);

typedef int32_t (*fop_symlink_cbk_t) (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno,
				      inode_t *inode,
				      struct stat *buf);

typedef int32_t (*fop_rename_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
				     struct stat *buf);

typedef int32_t (*fop_link_cbk_t) (call_frame_t *frame,
				   void *cookie,
				   xlator_t *this,
				   int32_t op_ret,
				   int32_t op_errno,
				   inode_t *inode,
				   struct stat *buf);

typedef int32_t (*fop_create_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
				     fd_t *fd,
				     inode_t *inode,
				     struct stat *buf);

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
				    struct stat *stbuf);

typedef int32_t (*fop_writev_cbk_t) (call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
				     struct stat *stbuf);

typedef int32_t (*fop_flush_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno);

typedef int32_t (*fop_close_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno);

typedef int32_t (*fop_fsync_cbk_t) (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno);

typedef int32_t (*fop_opendir_cbk_t) (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno,
				      fd_t *fd);

typedef int32_t (*fop_readdir_cbk_t) (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno,
				      dir_entry_t *entries,
				      int32_t count);

typedef int32_t (*fop_closedir_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno);

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

typedef int32_t (*fop_writedir_cbk_t) (call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno);

typedef int32_t (*fop_lookup_t) (call_frame_t *frame,
				 xlator_t *this,
				 loc_t *loc);

typedef int32_t (*fop_forget_t) (call_frame_t *frame,
				 xlator_t *this,
				 inode_t *inode);

typedef int32_t (*fop_stat_t) (call_frame_t *frame,
			       xlator_t *this,
			       loc_t *loc);

typedef int32_t (*fop_fstat_t) (call_frame_t *frame,
				xlator_t *this,
				fd_t *fd);

typedef int32_t (*fop_chmod_t) (call_frame_t *frame,
				xlator_t *this,
				loc_t *loc,
				mode_t mode);

typedef int32_t (*fop_fchmod_t) (call_frame_t *frame,
				 xlator_t *this,
				 fd_t *fd,
				 mode_t mode);

typedef int32_t (*fop_chown_t) (call_frame_t *frame,
				xlator_t *this,
				loc_t *loc,
				uid_t uid,
				gid_t gid);

typedef int32_t (*fop_fchown_t) (call_frame_t *frame,
				 xlator_t *this,
				 fd_t *fd,
				 uid_t uid,
				 gid_t gid);

typedef int32_t (*fop_truncate_t) (call_frame_t *frame,
				   xlator_t *this,
				   loc_t *loc,
				   off_t offset);

typedef int32_t (*fop_ftruncate_t) (call_frame_t *frame,
				    xlator_t *this,
				    fd_t *fd,
				    off_t offset);
				
typedef int32_t (*fop_utimens_t) (call_frame_t *frame,
				  xlator_t *this,
				  loc_t *loc,
				  struct timespec tv[2]);

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
				const char *path,
				mode_t mode,
				dev_t rdev);

typedef int32_t (*fop_mkdir_t) (call_frame_t *frame,
				xlator_t *this,
				const char *path,
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
				  const char *newpath);

typedef int32_t (*fop_rename_t) (call_frame_t *frame,
				 xlator_t *this,
				 loc_t *oldloc,
				 loc_t *newloc);

typedef int32_t (*fop_link_t) (call_frame_t *frame,
			       xlator_t *this,
			       loc_t *oldloc,
			       const char *newpath);

typedef int32_t (*fop_create_t) (call_frame_t *frame,
				 xlator_t *this,
				 const char *path,
				 int32_t flags,
				 mode_t mode);

typedef int32_t (*fop_open_t) (call_frame_t *frame,
			       xlator_t *this,
			       loc_t *loc,
			       int32_t flags);

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
				 off_t offset);
				
typedef int32_t (*fop_flush_t) (call_frame_t *frame,
				xlator_t *this,
				fd_t *fd);

typedef int32_t (*fop_close_t) (call_frame_t *frame,
				xlator_t *this,
				fd_t *fd);

typedef int32_t (*fop_fsync_t) (call_frame_t *frame,
				xlator_t *this,
				fd_t *fd,
				int32_t datasync);

typedef int32_t (*fop_opendir_t) (call_frame_t *frame,
				  xlator_t *this,
				  loc_t *loc);

typedef int32_t (*fop_readdir_t) (call_frame_t *frame,
				  xlator_t *this,
				  size_t size,
				  off_t offset,
				  fd_t *fd);

typedef int32_t (*fop_closedir_t) (call_frame_t *frame,
				   xlator_t *this,
				   fd_t *fd);

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
				   loc_t *loc);

typedef int32_t (*fop_removexattr_t) (call_frame_t *frame,
				      xlator_t *this,
				      loc_t *loc,
				      const char *name);

typedef int32_t (*fop_lk_t) (call_frame_t *frame,
			     xlator_t *this,
			     fd_t *fd,
			     int32_t cmd,
			     struct flock *flock);

typedef int32_t (*fop_writedir_t) (call_frame_t *frame,
				   xlator_t *this,
				   fd_t *fd,
				   int32_t flags,
				   dir_entry_t *entries,
				   int32_t count);

struct xlator_fops {
  fop_lookup_t         lookup;
  fop_forget_t         forget;
  fop_stat_t           stat;
  fop_fstat_t          fstat;
  fop_chmod_t          chmod;
  fop_fchmod_t         fchmod;
  fop_chown_t          chown;
  fop_fchown_t         fchown;
  fop_truncate_t       truncate;
  fop_ftruncate_t      ftruncate;
  fop_utimens_t        utimens;
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
  fop_close_t          close;
  fop_fsync_t          fsync;
  fop_opendir_t        opendir;
  fop_readdir_t        readdir;
  fop_closedir_t       closedir;
  fop_fsyncdir_t       fsyncdir;
  fop_statfs_t         statfs;
  fop_setxattr_t       setxattr;
  fop_getxattr_t       getxattr;
  fop_removexattr_t    removexattr;
  fop_lk_t             lk;
  fop_writedir_t       writedir;

  /* these entries are used for a typechecking hack in STACK_WIND _only_ */
  fop_lookup_cbk_t         lookup_cbk;
  fop_forget_cbk_t         forget_cbk;
  fop_stat_cbk_t           stat_cbk;
  fop_fstat_cbk_t          fstat_cbk;
  fop_chmod_cbk_t          chmod_cbk;
  fop_fchmod_cbk_t         fchmod_cbk;
  fop_chown_cbk_t          chown_cbk;
  fop_fchown_cbk_t         fchown_cbk;
  fop_truncate_cbk_t       truncate_cbk;
  fop_ftruncate_cbk_t      ftruncate_cbk;
  fop_utimens_cbk_t        utimens_cbk;
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
  fop_close_cbk_t          close_cbk;
  fop_fsync_cbk_t          fsync_cbk;
  fop_opendir_cbk_t        opendir_cbk;
  fop_readdir_cbk_t        readdir_cbk;
  fop_closedir_cbk_t       closedir_cbk;
  fop_fsyncdir_cbk_t       fsyncdir_cbk;
  fop_statfs_cbk_t         statfs_cbk;
  fop_setxattr_cbk_t       setxattr_cbk;
  fop_getxattr_cbk_t       getxattr_cbk;
  fop_removexattr_cbk_t    removexattr_cbk;
  fop_lk_cbk_t             lk_cbk;
  fop_writedir_cbk_t       writedir_cbk;
};


typedef struct xlator_list {
  xlator_t *xlator;
  struct xlator_list *next;
} xlator_list_t;

struct _xlator {
  char *name;
  char *type;
//  struct list_head all;
  xlator_t *next, *prev;
  xlator_t *parent;
  xlator_list_t *children;

  struct xlator_fops *fops;
  struct xlator_mops *mops; 

  void (*fini) (xlator_t *this);
  int32_t (*init) (xlator_t *this);
  event_notify_fn_t notify;

  dict_t *options;
  glusterfs_ctx_t *ctx;
  inode_table_t *itable;
  char ready;
  void *private;
};

void xlator_set_type (xlator_t *xl, const char *type);

xlator_t *file_to_xlator_tree (glusterfs_ctx_t *ctx,
			       FILE *fp);


int32_t xlator_tree_init (xlator_t *xl);

void xlator_foreach (xlator_t *this,
		     void (*fn) (xlator_t *each,
				 void *data),
		     void *data);

#define GF_STAT_PRINT_FMT_STR "%"PRIx64",%"PRIx64",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx64",%"PRIx64",%"PRIx32",%"PRIx64",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32"\n"

#define GF_STAT_SCAN_FMT_STR "%"SCNx64",%"SCNx64",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx64",%"SCNx64",%"SCNx32",%"SCNx64",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32"\n"

#define GF_STATFS_PRINT_FMT_STR "%"PRIx32",%"PRIx32",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx32",%"PRIx32",%"PRIx32"\n"

#define GF_STATFS_SCAN_FMT_STR "%"SCNx32",%"SCNx32",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx32",%"SCNx32",%"SCNx32"\n"

#endif


