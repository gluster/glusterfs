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
struct _inode;
typedef struct _inode inode_t;
struct _fd;
typedef struct _fd fd_t;

#include "stack.h"

struct _fd {
  pthread_mutex_t lock;
  int32_t nlookup;
  struct _inode *inode;
  fd_t *inode_next, *inode_prev;
  dict_t *ctx;
};

struct _inode {
  pthread_mutex_t lock;
  int32_t nlookup;        /* Number of references */
  ino_t vinode;           /* virtual inode number */
  ino_t inode;            /* inode number in the stroage (persistant) */
  ino_t parent;           /* parent's virtual inode number */
  char *name;             /* direntry name */
  fd_t fds;               /* list head of open fd's */
  struct stat buf;        /* attributes */
  dict_t *ctx;            /* per xlator private */
};

struct _dir_entry_t {
  dir_entry_t *next;
  char *name;
  struct stat buf;
};

struct xlator_stats {
  uint64_t nr_files;   /* Number of files open via this xlator */
  uint64_t free_disk; /* Mega bytes */
  uint64_t disk_usage; /* Mega bytes */
  uint64_t disk_speed; /* MHz or Mbps */
  uint64_t nr_clients; /* Number of client nodes (filled by glusterfsd) */
  uint64_t write_usage;
  uint64_t read_usage;
  /* add more stats here */
};

struct xlator_mops_cbk {
  int32_t (*stats_cbk) (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct xlator_stats *stats);
  
  int32_t (*fsck_cbk) (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno);

  int32_t (*lock_cbk) (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno);

  int32_t (*unlock_cbk) (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno);
  
  int32_t (*listlocks_cbk) (call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    char *locks);

  int32_t (*getspec) (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      char *spec_data);
};

struct xlator_mops {

  int32_t (*stats) (call_frame_t *frame,
		    xlator_t *this,
		    int32_t flags);

  int32_t (*fsck) (call_frame_t *frame,
		   xlator_t *this,
		   int32_t flags);

  int32_t (*lock) (call_frame_t *frame,
		   xlator_t *this,
		   const char *name);

  int32_t (*unlock) (call_frame_t *frame,
		     xlator_t *this,
		     const char *name);

  int32_t (*listlocks) (call_frame_t *frame,
			xlator_t *this,
			const char *pattern);

  int32_t (*getspec) (call_frame_t *frame,
		      xlator_t *this,
		      int32_t flag);
};


struct xlator_fops_cbk {
  int32_t (*lookup_cbk) (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 inode_t *inode);

  int32_t (*forget_cbk) (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno);

  int32_t (*getattr_cbk) (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  struct stat *buf);

  int32_t (*fgetattr_cbk) (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   struct stat *buf);

  int32_t (*chmod_cbk) (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			inode_t *inode);

  int32_t (*fchmod_cbk) (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 inode_t *inode);

  int32_t (*chown_cbk) (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			inode_t *inode);

  int32_t (*fchown_cbk) (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 inode_t *inode);

  int32_t (*truncate_cbk) (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   inode_t *inode);

  int32_t (*ftruncate_cbk) (call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    inode_t *inode);

  int32_t (*utimens_cbk) (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  inode_t *inode);

  int32_t (*futimens_cbk) (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   inode_t *inode);

  int32_t (*access_cbk) (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 inode_t *inode);

  int32_t (*readlink_cbk) (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   const char *path,
			   inode_t *inode);

  int32_t (*mknod_cbk) (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t *op_ret,
			int32_t op_errno,
			inode_t *inode);

  int32_t (*mkdir_cbk) (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t *op_ret,
			int32_t op_errno,
			inode_t *inode);

  int32_t (*unlink_cbk) (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t *op_ret,
			 int32_t op_errno);

  int32_t (*rmdir_cbk) (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno);

  int32_t (*symlink_cbk) (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  inode_t *inode);

  int32_t (*rename_cbk) (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno);

  int32_t (*link_cbk) (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       inode_t *inode);

  int32_t (*create_cbk) (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 fd_t *fd,
			 inode_t *inode);

  int32_t (*open_cbk) (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       fd_t *fd);

  int32_t (*readv_cbk) (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct iovec *vector,
			int32_t count);

  int32_t (*writev_cbk) (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno);

  int32_t (*flush_cbk) (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno);

  int32_t (*release_cbk) (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno);

  int32_t (*fsync_cbk) (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno);

  int32_t (*opendir_cbk) (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  fd_t *fd);

  int32_t (*readdir_cbk) (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  dir_entry_t *entries);

  int32_t (*releasedir_cbk) (call_frame_t *frame,
			     void *cookie,
			     xlator_t *this,
			     int32_t op_ret,
			     int32_t op_errno);

  int32_t (*fsyncdir_cbk) (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno);

  int32_t (*statfs_cbk) (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct statvfs *buf);

  int32_t (*setxattr_cbk) (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno);

  int32_t (*getxattr_cbk) (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   void *value);

  int32_t (*listxattr_cbk) (call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    void *value);

  int32_t (*removexattr_cbk) (call_frame_t *frame,
			      void *cookie,
			      xlator_t *this,
			      int32_t op_ret,
			      int32_t op_errno);

  int32_t (*lk_cbk) (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct flock *flock);
};

struct xlator_fops {
  int32_t (*lookup) (call_frame_t *frame,
		     xlator_t *this,
		     inode_t *parent,
		     const char *name);

  int32_t (*forget) (call_frame_t *frame,
		     xlator_t *this,
		     inode_t *inode,
		     unsigned long nlookup);

  int32_t (*getattr) (call_frame_t *frame,
		      xlator_t *this,
		      inode_t *inode);

  int32_t (*fgetattr) (call_frame_t *frame,
		       xlator_t *this,
		       fd_t *fd);

  int32_t (*chmod) (call_frame_t *frame,
		    xlator_t *this,
		    inode_t *inode,
		    mode_t mode);

  int32_t (*fchmod) (call_frame_t *frame,
		     xlator_t *this,
		     fd_t *fd,
		     mode_t mode);

  int32_t (*chown) (call_frame_t *frame,
		    xlator_t *this,
		    inode_t *inode,
		    uid_t uid,
		    gid_t gid);

  int32_t (*fchown) (call_frame_t *frame,
		     xlator_t *this,
		     fd_t *fd,
		     uid_t uid,
		     gid_t gid);

  int32_t (*truncate) (call_frame_t *frame,
		       xlator_t *this,
		       inode_t *inode,
		       off_t offset);

  int32_t (*ftruncate) (call_frame_t *frame,
			xlator_t *this,
			fd_t *fd,
			off_t offset);

  int32_t (*utimens) (call_frame_t *frame,
		      xlator_t *this,
		      inode_t *inode,
		      struct timespec tv[2]);

  int32_t (*futimens) (call_frame_t *frame,
		       xlator_t *this,
		       fd_t *fd,
		       struct timespec tv[2]);

  int32_t (*access) (call_frame_t *frame,
		     xlator_t *this,
		     inode_t *inode,
		     int mask);

  int32_t (*readlink) (call_frame_t *frame,
		       xlator_t *this,
		       inode_t *inode);

  int32_t (*mknod) (call_frame_t *frame,
		    xlator_t *this,
		    inode_t *parent,
		    const char *name,
		    mode_t mode,
		    dev_t rdev);

  int32_t (*mkdir) (call_frame_t *frame,
		    xlator_t *this,
		    inode_t *parent,
		    const char *name,
		    mode_t mode);

  int32_t (*unlink) (call_frame_t *frame,
		     xlator_t *this,
		     inode_t *parent,
		     const char *name);

  int32_t (*rmdir) (call_frame_t *frame,
		    xlator_t *this,
		    inode_t *parent,
		    const char *name);

  int32_t (*symlink) (call_frame_t *frame,
		      xlator_t *this,
		      const char *linkname,
		      inode_t *parent,
		      const char *name);

  int32_t (*rename) (call_frame_t *frame,
		     xlator_t *this,
		     inode_t *olddir,
		     const char *oldname,
		     inode_t *newdir,
		     const char *newname);

  int32_t (*link) (call_frame_t *frame,
		   xlator_t *this,
		   inode_t *inode,
		   inode_t *newparent,
		   const char *newname);

  int32_t (*create) (call_frame_t *frame,
		     xlator_t *this,
		     inode_t *parent,
		     const char *name,
		     int32_t flags,
		     mode_t mode);

  int32_t (*open) (call_frame_t *frame,
		   xlator_t *this,
		   inode_t *inode);

  int32_t (*readv) (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    size_t size,
		    off_t offset);

  int32_t (*writev) (call_frame_t *frame,
		     xlator_t *this,
		     fd_t *fd,
		     struct iovec *vector,
		     int32_t count,
		     off_t offset);

  int32_t (*flush) (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd);

  int32_t (*release) (call_frame_t *frame,
		      xlator_t *this,
		      fd_t *fd);

  int32_t (*fsync) (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    int32_t datasync);

  int32_t (*opendir) (call_frame_t *frame,
		      xlator_t *this,
		      inode_t *inode);

  int32_t (*readdir) (call_frame_t *frame,
		      xlator_t *this,
		      size_t size,
		      off_t offset,
		      fd_t *fd);

  int32_t (*releasedir) (call_frame_t *frame,
			 xlator_t *this,
			 fd_t *fd);

  int32_t (*fsyncdir) (call_frame_t *frame,
		       xlator_t *this,
		       fd_t *fd,
		       int32_t datasync);

  int32_t (*statfs) (call_frame_t *frame,
		     xlator_t *this,
		     inode_t *inode);

  int32_t (*setxattr) (call_frame_t *frame,
		       xlator_t *this,
		       inode_t *inode,
		       const char *name,
		       const char *value,
		       size_t size,
		       int32_t flags);

  int32_t (*getxattr) (call_frame_t *frame,
		       xlator_t *this,
		       inode_t *inode,
		       const char *name,
		       size_t size);

  int32_t (*listxattr) (call_frame_t *frame,
			xlator_t *this,
			inode_t *inode,
			size_t size);

  int32_t (*removexattr) (call_frame_t *frame,
			  xlator_t *this,
			  inode_t *inode,
			  const char *name);

  int32_t (*lk) (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t cmd,
		 struct flock *flock);
		 
};


typedef struct xlator_list {
  xlator_t *xlator;
  struct xlator_list *next;
} xlator_list_t;

struct _xlator {
  char *name;
  char *type;
  xlator_t *next; /* for maintainence */
  xlator_t *prev; /* for maintainence */
  xlator_t *parent;
  xlator_list_t *children;

  struct xlator_fops *fops;
  struct xlator_mops *mops; 

  void (*fini) (xlator_t *this);
  int32_t (*init) (xlator_t *this);
  void (*notify) (xlator_t *this, int32_t event, void *data);

  dict_t *options;
  glusterfs_ctx_t *ctx;
  void *private;
};

/*typedef struct _xlator xlator_t;
typedef struct _dir_entry_t dir_entry_t;
typedef struct file_context file_ctx_t;
typedef struct _inode inode_t;
typedef struct _fd fd_t;
*/

void xlator_set_type (xlator_t *xl, const char *type);

xlator_t *
file_to_xlator_tree (glusterfs_ctx_t *ctx,
		     FILE *fp);

void xlator_foreach (xlator_t *this,
		     void (*fn) (xlator_t *each,
				 void *data),
		     void *data);

#define GF_STAT_PRINT_FMT_STR "%"PRIx64",%"PRIx64",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx64",%"PRIx64",%"PRIx32",%"PRIx64",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32"\n"

#define GF_STAT_SCAN_FMT_STR "%"SCNx64",%"SCNx64",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx64",%"SCNx64",%"SCNx32",%"SCNx64",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32"\n"

#define GF_STATFS_PRINT_FMT_STR "%"PRIx32",%"PRIx32",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx32",%"PRIx32",%"PRIx32"\n"

#define GF_STATFS_SCAN_FMT_STR "%"SCNx32",%"SCNx32",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx32",%"SCNx32",%"SCNx32"\n"

#endif


