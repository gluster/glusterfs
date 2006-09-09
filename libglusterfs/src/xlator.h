#ifndef _XLATOR_H
#define _XLATOR_H
#include <stdio.h>
#include "glusterfs.h"
#include "layout.h"
#include "common-utils.h"

struct xlator;
struct _layout_t;

struct file_context {
  struct file_context *next;
  struct xlator *volume;
  char path[PATH_MAX];
  void *context;
};

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
  char *pathname;
  struct bulk_stat *next;
};

struct xlator_stats {
  unsigned long nr_files;   /* Number of files open via this xlator */
  unsigned long long free_disk; /* Mega bytes */
  unsigned long long disk_usage; /* Mega bytes */
  unsigned long disk_speed; /* MHz or Mbps */
  unsigned long nr_clients; /* Number of client nodes (filled by glusterfsd) */
  unsigned long long write_usage;
  unsigned long long read_usage;
  /* add more stats here */
};

struct xlator_mgmt_ops {
  int (*stats) (struct xlator *this, struct xlator_stats *stats);
  int (*fsck) (struct xlator *this);
  int (*lock) (struct xlator *this, const char *name);
  int (*unlock) (struct xlator *this, const char *name);
  int (*nslookup) (struct xlator *this, const char *name,
		   dict_t *ns);
  int (*nsupdate) (struct xlator *this, const char *name,
		   dict_t *ns);
};

struct xlator_fops {
  int (*open) (struct xlator *this, const char *path, int flags,
	       mode_t mode, struct file_context *ctx);
  int (*getattr) (struct xlator *this, const char *path, 
		  struct stat *stbuf);
  int (*readlink) (struct xlator *this, const char *path, 
		   char *dest, size_t size);
  int (*mknod) (struct xlator *this, const char *path, 
		mode_t mode, dev_t dev, uid_t uid, gid_t gid);
  int (*mkdir) (struct xlator *this, const char *path,
		mode_t mode, uid_t uid, gid_t gid);
  int (*unlink) (struct xlator *this, const char *path);
  int (*rmdir) (struct xlator *this, const char *path);
  int (*symlink) (struct xlator *this, const char *oldpath, 
		  const char *newpath, uid_t uid, gid_t gid);
  int (*rename) (struct xlator *this, const char *oldpath,
		 const char *newpath, uid_t uid, gid_t gid);
  int (*link) (struct xlator *this, const char *oldpath,
	       const char *newpath, uid_t uid, gid_t gid);
  int (*chmod) (struct xlator *this, const char *path, mode_t mode);
  int (*chown) (struct xlator *this, const char *path, uid_t uid, gid_t gid);
  int (*truncate) (struct xlator *this, const char *path, off_t offset);
  int (*utime) (struct xlator *this, const char *path, struct utimbuf *buf);
  int (*read) (struct xlator *this, const char *path, char *buf, size_t size,
	       off_t offset, struct file_context *ctx);
  int (*write) (struct xlator *this, const char *path, const char *buf, size_t size,
	       off_t offset, struct file_context *ctx);
  int (*statfs) (struct xlator *this, const char *path, struct statvfs *buf);
  int (*flush) (struct xlator *this, const char *path, 
		struct file_context *ctx);
  int (*release) (struct xlator *this, const char *path, 
		  struct file_context *ctx);
  int (*fsync) (struct xlator *this, const char *path, int flags,
		struct file_context *ctx);
  int (*setxattr) (struct xlator *this, const char *path, const char *name,
		   const char *value, size_t size, int flags);
  int (*getxattr) (struct xlator *this, const char *path, const char *name,
		   char *value, size_t size);
  int (*listxattr) (struct xlator *this, const char *path, char *list, size_t size);
  int (*removexattr) (struct xlator *this, const char *path, const char *name);
  int (*opendir) (struct xlator *this, const char *path, 
		  struct file_context *ctx);
  char *(*readdir) (struct xlator *this, const char *path, off_t offset);
  int (*releasedir) (struct xlator *this, const char *path,
		     struct file_context *ctx);
  int (*fsyncdir) (struct xlator *this, const char *path, int flags, 
		   struct file_context *ctx);
  int (*access) (struct xlator *this, const char *path, mode_t mode);
  int (*ftruncate) (struct xlator *this, const char *path, off_t offset,
		    struct  file_context *ctx);
  int (*fgetattr) (struct xlator *this, const char *path, struct stat *buf,
		 struct file_context *ctx);
  int (*bulk_getattr) (struct xlator *this, const char *path, struct bulk_stat *bstbuf);
};

struct xlator {
  char *name;
  struct xlator *next; /* for maintainence */
  struct xlator *parent;
  struct xlator *first_child;
  struct xlator *next_sibling;

  struct xlator_fops *fops;
  struct xlator_mgmt_ops *mgmt_ops;

  void (*fini) (struct xlator *this);
  int (*init) (struct xlator *this);
  struct _layout_t * (*getlayout) (struct xlator *this, 
				   struct _layout_t *layout);
  struct _layout_t * (*setlayout) (struct xlator *this, 
				   struct _layout_t *layout);

  dict_t *options;
  void *private;
};


void xlator_set_type (struct xlator *xl, const char *type);
in_addr_t resolve_ip (const char *hostname);

struct xlator * file_to_xlator_tree (FILE *fp);

void xlator_foreach (struct xlator *this,
		     void (*fn) (struct xlator *each));
#endif
