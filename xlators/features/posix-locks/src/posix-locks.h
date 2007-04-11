/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>

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

#ifndef __POSIX_LOCKS_H__
#define __POSIX_LOCKS_H__

#include "transport.h"
#include "stack.h"

struct __posix_fd;

struct __posix_lock {
  struct flock flock;
  short blocked;                /* waiting to acquire */
  struct __posix_lock *next;
  struct __posix_lock *prev;

  struct __posix_fd *pfd;       /* fd from which the lock was acquired */
  call_frame_t *frame;     

  /* These two together serve to uniquely identify each process
     across nodes */
  transport_t *transport;     /* to identify client node */
  pid_t client_pid;           /* pid of client process */
};
typedef struct __posix_lock posix_lock_t;

typedef enum {OP_READ, OP_WRITE} rw_op_t;
struct __posix_rw_req_t {
  call_frame_t *frame;
  dict_t *ctx;
  rw_op_t op;
  struct iovec *vector; /* only for writev */
  int size;             /* for a readv, this is the size of the data we wish to read
                           for a writev, it is the count of struct iovec's */
  off_t offset;
  struct flock region;  
  struct __posix_rw_req_t *next;
};
typedef struct __posix_rw_req_t posix_rw_req_t;

/* The "simulated" inode. This contains a list of all the locks associated 
   with this file */

struct __posix_inode {
  ino_t ino;
  posix_lock_t *locks;      /* list of locks on this inode */
  posix_rw_req_t *rw_reqs;  /* list of waiting r/w requests */
  struct __posix_inode *hash_next;
  int refcount;
};
typedef struct __posix_inode posix_inode_t;

struct __posix_fd {
  posix_inode_t *inode;
};
typedef struct __posix_fd posix_fd_t;

#define HASH_TABLE_SIZE		2047

typedef struct {
  posix_inode_t *inodes[HASH_TABLE_SIZE];
  pthread_mutex_t locks_mutex;
  int mandatory;         /* true if mandatory locking is enabled */
} posix_locks_private_t;

#endif /* __POSIX_LOCKS_H__ */
