/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
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
#include "layout.h"
#include "common-utils.h"


struct _layout_t;

struct _call_ctx_t;
typedef struct _call_ctx_t call_ctx_t;
struct _call_frame_t;
typedef struct _call_frame_t call_frame_t;
struct xlator;
typedef struct xlator xlator_t;
struct _dir_entry_t;
typedef struct _dir_entry_t dir_entry_t;

struct _dir_entry_t {
  char *name;
  struct stat buf;
};

struct file_context {
  struct file_context *next;
  xlator_t *volume;
  int8_t path[PATH_MAX];
  void *context;
};

typedef int32_t (*ret_fn_t) (call_frame_t *frame,
			     xlator_t *this,
			     int32_t op_ret,
			     int32_t op_errno,
			     ...);

struct _call_frame_t {
  call_ctx_t *root;      /* stack root */
  call_frame_t *parent;  /* previous BP */
  call_frame_t *next;    /* */
  call_frame_t *prev;    /* maintainence list */
  dict_t *local;         /* SP */
  xlator_t *this;        /* implicit object */
  ret_fn_t ret;          /* return address */
  int32_t ref_count;
};
	     
struct _call_ctx_t {
  uint64_t unique;
  call_frame_t frames;
};

#define STACK_WIND(frame, rfn, obj, fn, params ...) \
do {                                                \
  call_frame_t *_new = calloc (1,                   \
			       sizeof (*_new));     \
  _new->root = frame->root;                         \
  _new->next = frame->root->frames.next;            \
  _new->prev = &frame->root->frames;                \
  if (frame->root->frames.next)                     \
    frame->root->frames.next->prev = _new;          \
  frame->root->frames.next = _new;                  \
  _new->this = obj;                                 \
  _new->ret = (ret_fn_t) rfn;                       \
  _new->parent = frame;                             \
  frame->ref_count--;                               \
                                                    \
  fn (_new, obj, params);                           \
} while (0)


#define FRAME_DESTROY(frame)         \
do {                                 \
  if (frame->next)                   \
    frame->next->prev = frame->prev; \
  if (frame->prev)                   \
    frame->prev->next = frame->next; \
  if (frame->local)                  \
    dict_destroy (frame->local);     \
  if (frame->parent)                 \
    frame->parent->ref_count--;      \
  free (free);                       \
} while (0);


#define STACK_UNWIND(frame, params ...)     \
do {                                        \
  ret_fn_t fn = frame->ret;                 \
  call_frame_t *_parent = frame->parent;    \
  if (!frame->ref_count)                    \
    FRAME_DESTROY (frame);                  \
  fn (_parent, _parent->this, params);      \
} while (0)


#define FILL_MY_CTX(tmp, ctx, xl)  do {\
  tmp = ctx->next;\
  while (tmp != NULL && tmp->volume != xl) \
    tmp = tmp->next; \
} while (0)

/* just detach node from link list and free the context */
#define RM_MY_CTX(ctx, tmp) do {        \
  while (ctx && ctx->next != tmp)       \
    ctx = ctx->next;                    \
  if (ctx && ctx->next == tmp)          \
    ctx->next = tmp->next;              \
} while (0)

/* required for bulk_getattr call */
struct bulk_stat {
  struct stat *stbuf;
  int8_t *pathname;
  struct bulk_stat *next;
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

struct xlator_mgmt_rsps {

  int32_t (*stats) (call_frame_t *frame,
		    xlator_t *this,
		    int32_t ret,
		    int32_t errno,
		    struct xlator_stats *stats);

  int32_t (*fsck) (call_frame_t *frame,
		   xlator_t *this,
		   int32_t ret,
		   int32_t errno);

  int32_t (*lock) (call_frame_t *frame,
		   xlator_t *this,
		   int32_t ret,
		   int32_t errno);

  int32_t (*unlock) (call_frame_t *frame,
		     xlator_t *this,
		     int32_t ret,
		     int32_t errno);
  
  int32_t (*listlocks) (call_frame_t *frame,
			xlator_t *this,
			int32_t ret,
			int32_t errno,
			int8_t *locks);

  int32_t (*nslookup) (call_frame_t *frame,
		       xlator_t *this,
		       int32_t ret,
		       int32_t errno,
		       dict_t *ns);
  
  int32_t (*nsupdate) (call_frame_t *frame,
		       xlator_t *this,
		       int32_t ret,
		       int32_t errno);

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
		   const int8_t *name);

  int32_t (*unlock) (call_frame_t *frame,
		     xlator_t *this,
		     const int8_t *name);

  int32_t (*listlocks) (call_frame_t *frame,
			xlator_t *this,
			const int8_t *pattern);

  int32_t (*nslookup) (call_frame_t *frame,
		       xlator_t *this,
		       const int8_t *name);

  int32_t (*nsupdate) (call_frame_t *frame,
		       xlator_t *this,
		       const int8_t *name,
		       dict_t *ns);
};


struct xlator_fops {

  int32_t (*create) (call_frame_t *frame,
		     xlator_t *this,
		     const int8_t *path,
		     mode_t mode);

  int32_t (*open) (call_frame_t *frame,
		   xlator_t *this,
		   const int8_t *path,
		   int32_t flags,
		   mode_t mode);

  int32_t (*getattr) (call_frame_t *frame,
		      xlator_t *this, 
		      const int8_t *path);

  int32_t (*readlink) (call_frame_t *frame,
		       xlator_t *this, 
		       const int8_t *path, 
		       size_t size);

  int32_t (*mknod) (call_frame_t *frame,
		    xlator_t *this,
		    const int8_t *path,
		    mode_t mode,
		    dev_t dev);

  int32_t (*mkdir) (call_frame_t *frame,
		    xlator_t *this, 
		    const int8_t *path,
		    mode_t mode);

  int32_t (*unlink) (call_frame_t *frame,
		     xlator_t *this, 
		     const int8_t *path);

  int32_t (*rmdir) (call_frame_t *frame,
		    xlator_t *this, 
		    const int8_t *path);

  int32_t (*symlink) (call_frame_t *frame,
		      xlator_t *this, 
		      const int8_t *oldpath, 
		      const int8_t *newpath);

  int32_t (*rename) (call_frame_t *frame,
		     xlator_t *this, 
		     const int8_t *oldpath,
		     const int8_t *newpath);

  int32_t (*link) (call_frame_t *frame,
		   xlator_t *this,
		   const int8_t *oldpath,
		   const int8_t *newpath);

  int32_t (*chmod) (call_frame_t *frame,
		    xlator_t *this, 
		    const int8_t *path, 
		    mode_t mode);

  int32_t (*chown) (call_frame_t *frame,
		    xlator_t *this, 
		    const int8_t *path, 
		    uid_t uid, 
		    gid_t gid);

  int32_t (*truncate) (call_frame_t *frame,
		       xlator_t *this, 
		       const int8_t *path, 
		       off_t offset);

  int32_t (*utime) (call_frame_t *frame,
		    xlator_t *this, 
		    const int8_t *path,
		    struct utimbuf *buf);

  int32_t (*read) (call_frame_t *frame,
		   xlator_t *this,
		   struct file_context *ctx,
		   size_t size,
		   off_t offset);

  int32_t (*write) (call_frame_t *frame,
		    xlator_t *this, 
		    struct file_context *ctx,
		    int8_t *buf, 
		    size_t size,
		    off_t offset);

  int32_t (*statfs) (call_frame_t *frame,
		     xlator_t *this, 
		     const int8_t *path);

  int32_t (*flush) (call_frame_t *frame,
		    xlator_t *this,
		    struct file_context *ctx);

  int32_t (*release) (call_frame_t *frame,
		      xlator_t *this,
		      struct file_context *ctx);

  int32_t (*fsync) (call_frame_t *frame,
		    xlator_t *this, 
		    struct file_context *ctx,
		    int32_t flags);

  int32_t (*setxattr) (call_frame_t *frame,
		       xlator_t *this,
		       const int8_t *path,
		       const int8_t *name,
		       const int8_t *value,
		       size_t size,
		       int32_t flags);

  int32_t (*getxattr) (call_frame_t *frame,
		       xlator_t *this, 
		       const int8_t *path, 
		       const int8_t *name,
		       size_t size);

  int32_t (*listxattr) (call_frame_t *frame,
			xlator_t *this, 
			const int8_t *path,
			size_t size);

  int32_t (*removexattr) (call_frame_t *frame,
			  xlator_t *this,
			  const int8_t *path,
			  const int8_t *name);

  int32_t (*opendir) (call_frame_t *frame,
		      xlator_t *this,
		      const int8_t *path);

  int32_t (*readdir) (call_frame_t *frame,
		     xlator_t *this,
		     const int8_t *path);

  int32_t (*releasedir) (call_frame_t *frame,
			 xlator_t *this,
			 struct file_context *ctx);

  int32_t (*fsyncdir) (call_frame_t *frame,
		       xlator_t *this,
		       struct file_context *ctx,
		       int32_t flags);

  int32_t (*access) (call_frame_t *frame,
		     xlator_t *this,
		     const int8_t *path,
		     mode_t mode);

  int32_t (*ftruncate) (call_frame_t *frame,
			xlator_t *this,
			struct file_context *ctx,
			off_t offset);

  int32_t (*fgetattr) (call_frame_t *frame,
		       xlator_t *this,
		       struct file_context *ctx);
};

struct xlator_fop_rsps {

  int32_t (*create) (call_frame_t *frame,
		     xlator_t *this,
		     int32_t ret,
		     int32_t errno,
		     struct file_context *ctx,
		     struct stat *buf);

  int32_t (*open) (call_frame_t *frame,
		   xlator_t *this,
		   int32_t ret,
		   int32_t errno,
		   struct file_context *ctx,
		   struct stat *buf);

  int32_t (*getattr) (call_frame_t *frame,
		      xlator_t *this,
		      int32_t ret,
		      int32_t errno,
		      struct stat *buf);

  int32_t (*read) (call_frame_t *frame,
		   xlator_t *this,
		   int32_t ret,
		   int32_t errno,
		   int8_t *buf);

  int32_t (*write) (call_frame_t *frame,
		    xlator_t *this,
		    int32_t ret,
		    int32_t errno);
  
  int32_t (*readdir) (call_frame_t *frame,
		      xlator_t *this,
		      int32_t ret,
		      int32_t errno,
		      dir_entry_t *entries,
		      int32_t count);

  int32_t (*fsync) (call_frame_t *frame,
		    xlator_t *this,
		    int32_t ret,
		    int32_t errno);

  int32_t (*chown) (call_frame_t *frame,
		    xlator_t *this,
		    int32_t ret,
		    int32_t errno,
		    struct stat *buf);

  int32_t (*chmod) (call_frame_t *frame,
		    xlator_t *this,
		    int32_t ret,
		    int32_t errno,
		    struct stat *buf);

  int32_t (*unlink) (call_frame_t *frame,
		     xlator_t *this,
		     int32_t ret,
		     int32_t errno);

  int32_t (*rename) (call_frame_t *frame,
		     xlator_t *this,
		     int32_t ret,
		     int32_t errno);

  int32_t (*readlink) (call_frame_t *frame,
		       xlator_t *this,
		       int32_t ret,
		       int32_t errno,
		       int8_t *buf);

  int32_t (*symlink) (call_frame_t *frame,
		      xlator_t *this,
		      int32_t ret,
		      int32_t errno,
		      struct stat *buf);

  int32_t (*mknod) (call_frame_t *frame,
		    xlator_t *this,
		    int32_t ret,
		    int32_t errno,
		    struct stat *buf);
  
  int32_t (*link) (call_frame_t *frame,
		   xlator_t *this,
		   int32_t ret,
		   int32_t errno,
		   struct stat *buf);

  int32_t (*flush) (call_frame_t *frame,
		    xlator_t *this,
		    int32_t ret,
		    int32_t errno);
  
  int32_t (*release) (call_frame_t *frame,
		      xlator_t *this,
		      int32_t ret,
		      int32_t errno);

  int32_t (*opendir) (call_frame_t *frame,
		      xlator_t *this,
		      int32_t ret,
		      int32_t errno,
		      struct file_context *ctx);

  int32_t (*rmdir) (call_frame_t *frame,
		    xlator_t *this,
		    int32_t ret,
		    int32_t errno);

  int32_t (*truncate) (call_frame_t *frame,
		       xlator_t *this,
		       int32_t ret,
		       int32_t errno,
		       struct stat *buf);

  int32_t (*utime) (call_frame_t *frame,
		    xlator_t *this,
		    int32_t ret,
		    int32_t errno,
		    struct stat *buf);

  int32_t (*statfs) (call_frame_t *frame,
		     xlator_t *this,
		     int32_t ret,
		     int32_t errno,
		     struct statvfs *buf);

  int32_t (*setxattr) (call_frame_t *frame,
		       xlator_t *this,
		       int32_t ret,
		       int32_t errno);

  int32_t (*getxattr) (call_frame_t *frame,
		       xlator_t *this,
		       int32_t ret,
		       int32_t errno,
		       void *value);

  int32_t (*listxattr) (call_frame_t *frame,
			xlator_t *this,
			int32_t ret,
			int32_t errno,
			void *value);

  int32_t (*removexattr) (call_frame_t *frame,
			  xlator_t *this,
			  int32_t ret,
			  int32_t errno);
			
  int32_t (*releasedir) (call_frame_t *frame,
			 xlator_t *this,
			 int32_t ret,
			 int32_t errno);

  int32_t (*fsyncdir) (call_frame_t *frame,
		       xlator_t *this,
		       int32_t ret,
		       int32_t errno);

  int32_t (*access) (call_frame_t *frame,
		     xlator_t *this,
		     int32_t ret,
		     int32_t errno);

  int32_t (*ftruncate) (call_frame_t *frame,
			xlator_t *this,
			int32_t ret,
			int32_t errno,
			struct stat *buf);

  int32_t (*fgetattr) (call_frame_t *frame,
		       xlator_t *this,
		       int32_t ret,
		       int32_t errno,
		       struct stat *buf);
};


struct xlator {
  int8_t *name;
  xlator_t *next; /* for maintainence */
  xlator_t *parent;
  xlator_t *first_child;
  xlator_t *next_sibling;

  struct xlator_fops *fops;
  struct xlator_mops *mops; 

  void (*fini) (xlator_t *this);
  int32_t (*init) (xlator_t *this);

  dict_t *options;
  void *private;
};


void xlator_set_type (xlator_t *xl, const int8_t *type);
in_addr_t resolve_ip (const int8_t *hostname);

xlator_t * file_to_xlator_tree (FILE *fp);

void xlator_foreach (xlator_t *this,
		     void (*fn) (xlator_t *each));

#define GF_STAT_PRINT_FMT_STR "%"PRIx64",%"PRIx64",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx64",%"PRIx64",%"PRIx32",%"PRIx64",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32",%"PRIx32"\n"

#define GF_STAT_SCAN_FMT_STR "%"SCNx64",%"SCNx64",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx64",%"SCNx64",%"SCNx32",%"SCNx64",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32",%"SCNx32"\n"

#define GF_STATFS_PRINT_FMT_STR "%"PRIx32",%"PRIx32",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx32",%"PRIx32",%"PRIx32"\n"

#define GF_STATFS_SCAN_FMT_STR "%"SCNx32",%"SCNx32",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx32",%"SCNx32",%"SCNx32"\n"

#endif
