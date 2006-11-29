
#ifndef _FUSE_INTERNALS_H
#define _FUSE_INTERNALS_H

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif

#include <fuse/fuse.h>
struct fuse_session;
struct fuse_chan;

#include <fuse/fuse_lowlevel.h>
#include <fuse/fuse_opt.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/uio.h>

#define FUSE_MAX_PATH 4096

#define FUSE_UNKNOWN_INO 0xffffffff

struct fuse_config {
    unsigned int uid;
    unsigned int gid;
    unsigned int  umask;
    double entry_timeout;
    double negative_timeout;
    double attr_timeout;
#if 1
  double ac_attr_timeout;
  int ac_attr_timeout_set;
#endif
    int debug;
    int hard_remove;
    int use_ino;
    int readdir_ino;
    int set_mode;
    int set_uid;
    int set_gid;
    int direct_io;
    int kernel_cache;
#if 1 /* fuse version >= 2.6.0 */
  int auto_cache;
  int intr;
  int intr_signal;
#endif
};

struct fuse {
    struct fuse_session *se;
    struct fuse_operations op;
    int compat;
    struct node **name_table;
    size_t name_table_size;
    struct node **id_table;
    size_t id_table_size;
    fuse_ino_t ctr;
    unsigned int generation;
    unsigned int hidectr;
    pthread_mutex_t lock;
    pthread_rwlock_t tree_lock;
    void *user_data;
    struct fuse_config conf;
#if 1 /* if fuse_version >= 2.6 */
  int intr_installed;
#endif
};

struct lock {
  int type;
  off_t start;
  off_t end;
  pid_t pid;
  uint64_t owner;
  struct lock *next;
};

struct node {
    struct node *name_next;
    struct node *id_next;
    fuse_ino_t nodeid;
    unsigned int generation;
    int refctr;
    fuse_ino_t parent;
    char *name;
    uint64_t nlookup;
    int open_count;
    int is_hidden;
#if 1 /* if fuse version >= 2.6 */
  struct timespec stat_updated;
  struct timespec mtime;
  off_t size;
  int cache_valid;
  struct lock *locks;
#endif
};

struct fuse_dirhandle {
  pthread_mutex_t lock;
  struct fuse *fuse;
  fuse_req_t req;
  char *contents;
  int allocated;
  unsigned len;
  unsigned size;
  unsigned needlen;
  int filled;
  uint64_t fh;
  int error;
  fuse_ino_t nodeid;
};

struct fuse_context_i {
  struct fuse_context ctx;
  fuse_req_t req;
};

struct fuse *
glusterfs_fuse_new_common (struct fuse_chan *ch,
			   struct fuse_args *args);
#endif /* _FUSE_INTERNALS_H */
