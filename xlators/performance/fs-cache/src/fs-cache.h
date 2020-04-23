/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _FS_CACHE_H
#define _FS_CACHE_H

#include <glusterfs/compat-errno.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/logging.h>
#include <glusterfs/dict.h>
#include <glusterfs/xlator.h>
#include <glusterfs/common-utils.h>
#include <glusterfs/list.h>
#include <stdlib.h>
#include <glusterfs/locking.h>
#include "fsc-mem-types.h"
#include "fs-cache-messages.h"
#include <semaphore.h>
#include <glusterfs/statedump.h>

struct fsc_conf;
struct fsc_inode;
struct fsc_block;

#define FSC_CACHE_PATTERN_LEN 128
struct fsc_filter {
    char pattern[3][128];
};

struct fsc_inode {
  struct list_head inode_list; /*
                              * list of inodes, maintained by
                              * io-cache translator
                              */
  /*meta data of file on server */
  off_t ia_size;     /*file size on server*/
  time_t s_mtime;      /*
                        * seconds component of file mtime
                        */
  time_t s_mtime_nsec; /*
                        * nanosecond component of file mtime
                        */

  /*meta data of file on local */
  off_t fsc_size;   /*now write cache*/
  int fsc_fd;
  struct fsc_block* write_block;
  int32_t write_block_len;
  struct timeval last_op_time;
  char * local_path;
  pthread_mutex_t inode_lock;
  inode_t *inode;
  struct fsc_conf *conf;
};

struct fsc_block {
  off_t start;
  off_t end;
};


struct fsc_local {
  mode_t mode;
  int32_t flags;
  loc_t file_loc;
  off_t offset;
  size_t size;
  int32_t op_ret;
  int32_t op_errno;
  off_t pending_offset;       /*
                                 * offset from this frame should
                                 * continue
                                 */
  size_t pending_size;        /*
                                 * size of data this frame is waiting
                                 * on
                                 */
  struct fsc_inode *inode;
  fd_t *fd;
  struct iovec *vector;
  struct iobref *iobref;
  int32_t need_xattr;
  dict_t *xattr_req;
};



struct fsc_conf {
  char * cache_dir;
  struct list_head inodes; /* list of inodes cached */
  uint32_t inodes_count;
  pthread_mutex_t inodes_lock;

  struct fsc_filter filters;

  gf_boolean_t is_enable;

  uint64_t min_file_size;

  uint32_t disk_reserve;
  uint32_t disk_space_full;

  uint32_t resycle_idle_inode;
  uint32_t time_idle_inode;

  uint32_t direct_io_read;
  uint32_t direct_io_write;

  pthread_t aux_thread;
  gf_boolean_t aux_thread_active;
  pthread_mutex_t aux_lock;

  xlator_t *this;
};

typedef struct fsc_conf fsc_conf_t;
typedef struct fsc_inode fsc_inode_t;
typedef struct fsc_local fsc_local_t;
typedef struct fsc_block fsc_block_t;

#define fsc_inodes_list_lock(priv)                                                  \
    do {                                                                       \
        gf_msg_trace(priv->this->name, 0, "locked inodes_list(%p)", priv);           \
        pthread_mutex_lock(&priv->inodes_lock);                                \
    } while (0)

#define fsc_inodes_list_unlock(priv)                                                \
    do {                                                                       \
        gf_msg_trace(priv->this->name, 0, "unlocked inodes_list(%p)", priv);         \
        pthread_mutex_unlock(&priv->inodes_lock);                              \
    } while (0)

#define fsc_inode_lock(fsc_inode)                                              \
    do {                                                                       \
        gf_msg_trace(fsc_inode->conf->this->name, 0, "locked fsc_inode(%p)",        \
                     fsc_inode);                                               \
        pthread_mutex_lock(&fsc_inode->inode_lock);                            \
    } while (0)

#define fsc_inode_unlock(fsc_inode)                                            \
    do {                                                                       \
        gf_msg_trace(fsc_inode->conf->this->name, 0, "unlocked fsc_inode(%p)",      \
                     fsc_inode);                                               \
        pthread_mutex_unlock(&fsc_inode->inode_lock);                          \
    } while (0)

fsc_inode_t *
fsc_inode_create(xlator_t *this, inode_t *inode, char* path);

void fsc_inode_destroy(fsc_inode_t *fsc_inode, int32_t tag);

void fsc_inode_fini(fsc_conf_t *conf);

gf_boolean_t
fsc_inode_is_idle(fsc_inode_t *fsc_inode);

int32_t
fsc_inode_update(xlator_t *this, inode_t *inode, char *path, struct iatt *iabuf);

int32_t
fsc_inode_open_for_read(xlator_t *this, fsc_inode_t *fsc_inode);

int32_t
fsc_inode_open_for_write(xlator_t *this, fsc_inode_t *fsc_inode);

int32_t
fsc_inode_read(fsc_inode_t *fsc_inode, call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t offset, uint32_t flags, dict_t *xdata);

char *
fsc_page_aligned_alloc(size_t size, char **aligned_buf);

int32_t
fsc_block_init(xlator_t *this, fsc_inode_t *inode);

int32_t
fsc_block_add(xlator_t *this, fsc_inode_t *inode, off_t offset, size_t size);

int32_t
fsc_block_remove(xlator_t *this, fsc_inode_t *inode, off_t offset, size_t size);

int32_t
fsc_block_is_cache(xlator_t *this, fsc_inode_t *inode, off_t offset, size_t size);

int32_t
fsc_block_flush(xlator_t *this, fsc_inode_t *inode);

gf_boolean_t
fsc_check_filter(fsc_conf_t *conf, const char *path);

int
fsc_spawn_aux_thread(xlator_t *xl);

#endif /* __fsc_H */
