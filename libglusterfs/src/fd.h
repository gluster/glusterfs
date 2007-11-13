#ifndef _FD_H
#define _FD_H

#include "glusterfs.h"
#include "logging.h"
#include "list.h"
#include "dict.h"

struct _fd {
  struct list_head inode_list;
  pthread_mutex_t lock;
  int32_t ref;
  struct _inode *inode;
  dict_t *ctx;
};
typedef struct _fd fd_t;

struct _fdtable {
  uint32_t max_fds;
  fd_t **fds;
  pthread_mutex_t lock;
};
typedef struct _fdtable fdtable_t;

inline void 
gf_fd_put (fdtable_t *fdtable, int32_t fd);

fd_t *
gf_fd_fdptr_get (fdtable_t *fdtable, int32_t fd);

fdtable_t *
gf_fd_fdtable_alloc (void);

int32_t 
gf_fd_unused_get (fdtable_t *fdtable, fd_t *fdptr);

void 
gf_fd_fdtable_destroy (fdtable_t *fdtable);

#endif
