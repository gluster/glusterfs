#ifndef _XLATOR_H
#define _XLATOR_H
#include <stdio.h>
#include "dict.h"
//#include "schedule.h"

struct xlator;

struct xlator_fops {
  int (*open) (struct xlator *this, const char *path, int flags, uint64_t fh);
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
	       off_t offset, uint64_t fh);
  int (*write) (struct xlator *this, const char *path, char *buf, size_t size,
	       off_t offset, uint64_t fh);
  int (*statfs) (struct xlator *this, const char *path, char *buf);
  int (*flush) (struct xlator *this, const char *path, uint64_t fh);
  int (*release) (struct xlator *this, const char *path, uint64_t fh);
  int (*fsync) (struct xlator *this, const char *path, int flags, uint64_t fh);
  int (*setxattr) (struct xlator *this, const char *path, const char *name,
		   const char *value, size_t size, int flags);
  int (*getxattr) (struct xlator *this, const char *path, const char *name,
		   char *value, size_t size);
  int (*listxattr) (struct xlator *this, const char *path, char *list, size_t size);
  int (*removexattr) (struct xlator *this, const char *path, const char *name);
  int (*opendir) (struct xlator *this, const char *path, uint64_t fh);
  int (*readdir) (struct xlator *this, const char *path, off_t offset); // FIXME
  int (*releasedir) (struct xlator *this, const char *path, uint64_t fh);
  int (*fsyncdir) (struct xlator *this, const char *path, int flags, uint64_t fh);
  int (*access) (struct xlator *this, const char *path, int mode); //FIXME
  int (*create) (struct xlator *this, const char *path, int mode); //FIXME
  int (*ftruncate) (struct xlator *this, const char *path, off_t offset, uint64_t fh);
  int (*fgetattr) (struct xlator *this, const char *path, struct stat *buf); //FIXME

};

struct xlator {
  char *name;
  struct xlator *next; /* for maintainence */
  struct xlator *parent;
  struct xlator *first_child;
  struct xlator *next_sibling;

  struct xlator_fops *fops;

  void (*fini) (struct xlator *this);
  void (*init) (struct xlator *this, void *data);

  dict_t *options;
  void *private;
};

struct xlator_fops *
type_to_fops (const char *type);
#endif
