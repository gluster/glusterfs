/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _FD_H
#define _FD_H

#include "glusterfs/list.h"
#include <sys/types.h>
#include <unistd.h>
#include "glusterfs/glusterfs.h"
#include "glusterfs/fd-lk.h"

#define GF_ANON_FD_NO -2
#define GF_ANON_FD_FLAGS (O_RDWR | O_LARGEFILE)

struct _inode;
struct _dict;
struct fd_lk_ctx;

struct _fd_ctx {
    union {
        uint64_t key;
        void *xl_key;
    };
    union {
        uint64_t value1;
        void *ptr1;
    };
};

struct _fd {
    uint64_t pid;
    int32_t flags;
    gf_atomic_uint32_t refcount;
    struct list_head inode_list;
    struct _inode *inode;
    gf_lock_t lock; /* used ONLY for manipulating
                       'struct _fd_ctx' array (_ctx).*/
    struct _fd_ctx *_ctx;
    struct fd_lk_ctx *lk_ctx;
    int xl_count;           /* Number of xl referred in this fd */
    gf_boolean_t anonymous; /* fd which does not have counterpart open
                               fd on backend (server for client, posix
                               for server). */
};
typedef struct _fd fd_t;

struct fd_table_entry {
    fd_t *fd;
    int next_free;
};
typedef struct fd_table_entry fdentry_t;

struct _fdtable {
    int refcount;
    uint32_t max_fds;
    pthread_rwlock_t lock;
    fdentry_t *fdentries;
    int first_free;
};
typedef struct _fdtable fdtable_t;

/* Signifies no more entries in the fd table. */
#define GF_FDTABLE_END -1

/* This is used to invalidated
 * the next_free value in an fdentry that has been allocated
 */
#define GF_FDENTRY_ALLOCATED -2

#include "glusterfs/logging.h"
#include "glusterfs/xlator.h"

void
gf_fd_put(fdtable_t *fdtable, int32_t fd);

fd_t *
gf_fd_fdptr_get(fdtable_t *fdtable, int64_t fd);

fdtable_t *
gf_fd_fdtable_alloc(void);

int
gf_fd_unused_get(fdtable_t *fdtable, fd_t *fdptr);

fdentry_t *
gf_fd_fdtable_get_all_fds(fdtable_t *fdtable, uint32_t *count);

void
gf_fd_fdtable_destroy(fdtable_t *fdtable);

fd_t *
__fd_ref(fd_t *fd);

fd_t *
fd_ref(fd_t *fd);

void
fd_unref(fd_t *fd);

void
fd_close(fd_t *fd);

fd_t *
fd_create(struct _inode *inode, pid_t pid);

fd_t *
fd_create_uint64(struct _inode *inode, uint64_t pid);

fd_t *
fd_lookup(struct _inode *inode, pid_t pid);

fd_t *
fd_lookup_uint64(struct _inode *inode, uint64_t pid);

fd_t *
fd_lookup_anonymous(inode_t *inode, int32_t flags);

fd_t *
fd_anonymous(inode_t *inode);

fd_t *
fd_anonymous_with_flags(inode_t *inode, int32_t flags);

gf_boolean_t
fd_is_anonymous(fd_t *fd);

uint8_t
fd_list_empty(struct _inode *inode);

fd_t *
fd_bind(fd_t *fd);

int
fd_ctx_set(fd_t *fd, xlator_t *xlator, uint64_t value);

uint64_t
fd_ctx_get(fd_t *fd, xlator_t *xlator);
#define fd_ctx_get_ptr(_fd, _xl) (void *)(uintptr_t)fd_ctx_get(_fd, _xl)

uint64_t
fd_ctx_del(fd_t *fd, xlator_t *xlator);
#define fd_ctx_del_ptr(_fd, _xl) (void *)(uintptr_t)fd_ctx_del(_fd, _xl)

int
__fd_ctx_set(fd_t *fd, xlator_t *xlator, uint64_t value);

uint64_t
__fd_ctx_get(fd_t *fd, xlator_t *xlator);
#define __fd_ctx_get_ptr(_fd, _xl) (void *)(uintptr_t)__fd_ctx_get(_fd, _xl)

void
fd_ctx_dump(fd_t *fd, char *prefix);

fdentry_t *
gf_fd_fdtable_copy_all_fds(fdtable_t *fdtable, uint32_t *count);

void
gf_fdptr_put(fdtable_t *fdtable, fd_t *fd);

#endif /* _FD_H */
