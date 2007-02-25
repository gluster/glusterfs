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

struct __posix_lock {
  struct flock flock;
  short blocked;           /* waiting to acquire */
  struct __posix_lock *next;

  int fd;                  /* fd from which the lock was acquired */
  /* These two together serve to uniquely identify each process
     across nodes */
  call_frame_t *frame;     
  transport_t *transport;  /* to identify client node */
  pid_t client_pid;        /* pid of client process */
};
typedef struct __posix_lock posix_lock;

typedef struct {
  call_frame_t *frame;
  enum {OP_READ, OP_WRITE} op;
  size_t size;
  off_t offset;
} posix_rw_req;

/* The "simulated" inode. This contains a list of all the locks associated 
   with this file */

struct __posix_inode {
  ino_t inode;
  posix_lock *locks;      /* list of locks on this inode */
  posix_rw_req *rw_reqs;  /* list of waiting rw requests */
  short mandatory;        /* true if any of the clients (that has a lock on this file)
                             has 'mandatory' option set */
  struct __posix_inode *hash_next; 
};
typedef struct __posix_inode posix_inode;

struct __posix_fd {
  int fd;
  posix_inode *inode;
  struct __posix_fd *hash_next;
};
typedef struct __posix_fd posix_fd;

int posix_register_new_fd (int fd, ino_t ino);
int posix_fcntl (int fd, int cmd, struct flock *lock, call_frame_t *frame, 
		 transport_t *transport, pid_t client_pid);

#endif /* __POSIX_LOCKS_H__ */
