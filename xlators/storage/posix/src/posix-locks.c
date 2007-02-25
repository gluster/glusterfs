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

#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <assert.h>
#include <asm/types.h>   /* for BITS_PER_LONG */

#include "posix-locks.h"
#include "logging.h"

/* 
   "Multiplicative" hashing. GOLDEN_RATIO_PRIME value stolen from linux source.
   See include/linux/hash.h in the source tree.
*/

#if BITS_PER_LONG == 32
/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define GOLDEN_RATIO_PRIME 0x9e370001UL
#elif BITS_PER_LONG == 64
/*  2^63 + 2^61 - 2^57 + 2^54 - 2^51 - 2^18 + 1 */
#define GOLDEN_RATIO_PRIME 0x9e37fffffffc0001UL
#else
#define GOLDEN_RATIO_PRIME 0x9e370001UL
#endif

static uint32_t integer_hash (uint32_t key, int size)
{
  uint32_t hash = key;
  hash *= GOLDEN_RATIO_PRIME;
  return hash % size;
}

#define HASH_TABLE_SIZE		1

/* Table of inodes */
static posix_inode *inodes[HASH_TABLE_SIZE];

/* File descriptor table */
static posix_fd *fds[HASH_TABLE_SIZE];

/* Lookup an inode */
static posix_inode *
lookup_inode (ino_t inode)
{
  uint32_t hash = integer_hash (inode, HASH_TABLE_SIZE);
  posix_inode *ino = inodes[hash];
  while (ino) {
    if (ino->inode == inode)
      return ino;
    ino = ino->hash_next;
  }

  return NULL;
}

/* Lookup an fd */
static posix_fd *
lookup_fd (int fd)
{
  uint32_t hash = integer_hash (fd, HASH_TABLE_SIZE);
  posix_fd *f = fds[hash];
  while (f) {
    if (f->fd == fd)
      return f;
    f = f->hash_next;
  }

  return NULL;
}

/* Copy the second arg's fields into the first one */
static void
copy_flock (struct flock *new, struct flock *old)
{
  new->l_start  = old->l_start;
  new->l_len    = old->l_len;
  new->l_whence = old->l_whence;
  new->l_type   = old->l_type;
  new->l_pid    = old->l_pid;
}

/* Return true if the locks overlap, false otherwise */
static int
lock_overlap (struct flock *x, struct flock *y)
{
  /* 
     Notes:
     (1) if l_len == 0, it means the lock extends to the end of file,
         even if the file grows 
     (2) FUSE always gives us absolute offsets, so no need to worry 
         about SEEK_CUR or SEEK_END
  */

  unsigned long x_begin = x->l_start;
  unsigned long x_end = x->l_len == 0 ? ULONG_MAX : x->l_start + x->l_len;
  unsigned long y_begin = y->l_start;
  unsigned long y_end = y->l_len == 0 ? ULONG_MAX : y->l_start + y->l_len;

  if (((x_begin >= y_begin) && (x_begin <= y_end)) ||
      ((x_end   >= y_begin) && (x_end   <= y_end))) {
    return 1;
  }
  return 0;
}

int
posix_register_new_fd (int fd, ino_t ino)
{
  GF_ERROR_IF (fd < 0);
  GF_ERROR_IF (ino < 0);

  posix_fd *pfd = lookup_fd (fd);
  if (pfd)
    return 0;

  pfd = calloc (sizeof (posix_fd), 1);
  posix_inode *inode = lookup_inode (ino);
  uint32_t hash;

  if (!inode) {
    inode = calloc (sizeof (posix_inode), 1);
    inode->inode = ino;
    hash = integer_hash (ino, HASH_TABLE_SIZE);
    if (inodes[hash])
      inode->hash_next = inodes[hash]->hash_next;

    inodes[hash] = inode;
  }

  pfd->fd = fd;
  pfd->inode = inode;

  hash = integer_hash (fd, HASH_TABLE_SIZE);
  if (fds[hash])
    pfd->hash_next = fds[hash];

  fds[hash] = pfd;

  return 0;
}

int 
posix_release_fd (int fd)
{
  gf_log ("posix/locks", GF_LOG_DEBUG, "releasing fd %d", fd);

  int hash = integer_hash (fd, HASH_TABLE_SIZE);
  posix_fd *f = fds[hash];
  posix_fd *prev = f;

  while (f) {
    if (f->fd == fd) {
      if (f == prev) 
	fds[hash] = f->hash_next;
      else
	prev->hash_next = f->hash_next;
      free (f); 
      break;
    }

    prev = f;
    f = f->hash_next;
  }

  /* We don't need to manually release any locks associated with this
     fd. FUSE will send us appropriate F_UNLCK requests */

  return -1; 
}

int
posix_release_inode (ino_t ino)
{
  gf_log ("posix/locks", GF_LOG_DEBUG, "releasing inode %d", ino);
  posix_inode *inode = lookup_inode (ino);
  if (!inode)
    return -1;

  posix_lock *l = inode->locks;
  while (l) {
    posix_lock *next = l->next;
    free (l);
    l = next;
  }

  free (inode); /* XXX: delete from hash table */
  return 0;
}

static int
get_lock (posix_inode *inode, struct flock *newlock)
{
  posix_lock *lock = inode->locks;
  while (lock) {
    if (lock->blocked) {
      lock = lock->next;
      continue;
    }

    if (lock_overlap (newlock, &lock->flock)) {

      if (lock->flock.l_type == F_WRLCK) {
	/* this region has been exclusively locked, so no-can-do */
	errno = -EAGAIN;
	goto cant_lock;
      }

      if (newlock->l_type == F_WRLCK) {
	/* we want an exclusive lock, but there's already some other lock */
	errno = -EAGAIN;
	goto cant_lock;
      }
    }

    lock = lock->next;
    continue;

  cant_lock:
    copy_flock (newlock, &lock->flock);
    return -1;
  }

  newlock->l_type = F_UNLCK;
  return 0;
}

/*
  Release a lock 
*/
static int
release_lock (posix_fd *pfd, struct flock *lock)
{
  posix_inode *ino = pfd->inode;
  posix_lock *l = ino->locks;
  posix_lock *prev = ino->locks;

  while (l) {
    if (lock_overlap (&l->flock, lock)) {
      if (l == prev) 
	ino->locks = l->next;
      else
	prev->next = l->next;

      grant_blocked_lock (ino, lock);
      free (l);
      return 0;
    }

    prev = l;
    l = l->next;
  }

  return -1;
}

static int
alloc_lock_and_block (posix_fd *pfd, int cmd, struct flock *lock,
		      call_frame_t *frame, pid_t client_pid)
{
  posix_lock *plock = calloc (sizeof (posix_lock), 1);

  copy_flock (&plock->flock, lock);

  plock->blocked = 1;

  plock->client_pid = client_pid;
  plock->transport = frame->root->state;
  plock->frame = frame;

  plock->next = pfd->inode->locks;
  pfd->inode->locks = plock;

  return 0;
}

/*
  Check if any blocked lock can be granted as a result of 
  this lock being released, and grant it.
*/
static int
grant_blocked_lock (posix_inode *ino, struct flock *lock)
{
  posix_lock *l = ino->locks;
  while (l) {
    if (!l->blocked) {
      l = l->next;
      continue;
    }

    struct flock *newlock = &(l->flock);
    if (lock_overlap (newlock, lock)) {
      l->blocked = 0;
      STACK_UNWIND (l->frame, 0, errno, newlock);
      return 0;
    }
    
    l = l->next;
  }
}

int 
convert_lock (posix_lock *newlock, struct flock *lock)
{
  /* XXX: convert */
  return 0;
}

/*
  Test if there's no other lock conflicting with this lock
  Return: true if lock can be acquired.
*/

/*
  XXX: Another bitch
  "record locks are preserved across execve, but not preserved across fork".
  Implement this check using the transport_t and pid fields
 */

int
test_for_lock (posix_inode *ino, posix_fd *pfd, struct flock *newlock,
	       transport_t *transport, pid_t client_pid)
{
  posix_lock *lock = ino->locks;
  while (lock) {
    if (lock->blocked) {
      lock = lock->next;
      continue;
    }

    /* fuse always gives us the absolute offset, so no need to 
       worry about SEEK_CUR or SEEK_END */
    
    if (lock_overlap (&lock->flock, newlock)) {
      if (lock->transport == transport && lock->client_pid == client_pid) {
	/* The conflicting lock is held by us. Try to convert it */
	/* XXX: re-organize this: don't call convert from here */
	convert_lock (lock, newlock);
	return 1;
      }

      if (lock->flock.l_type == F_WRLCK) {
	/* this region has been exclusively locked, so no-can-do */
	errno = -EAGAIN;
	return 0;
      }

      if (newlock->l_type == F_WRLCK) {
	/* we want an exclusive lock, but there's already some other lock */
	errno = -EAGAIN;
	return 0;
      }
    }

    lock = lock->next;
  }

  return 1;
}

int
acquire_lock (int fd, posix_inode *inode, struct flock *lock,
	      call_frame_t *frame, transport_t *transport, pid_t client_pid)
{
  posix_lock *newlock = calloc (1, sizeof (posix_lock));
  copy_flock (&newlock->flock, lock);

  newlock->blocked = 0;
  newlock->fd = fd;
  newlock->client_pid = client_pid;
  newlock->transport = transport;
  newlock->frame = frame;

  newlock->next = inode->locks;
  inode->locks = newlock;

  return 0;
}

int 
posix_fcntl (int fd, int cmd, struct flock *lock, 
	     call_frame_t *frame, transport_t *transport,
	     pid_t client_pid)
{
  posix_fd *pfd = lookup_fd (fd);
  if (!pfd) {
    errno = -EBADF;
    return -1;
  }

  switch (cmd) {
  case F_GETLK: 
    return get_lock (pfd->inode, lock);

  case F_SETLK:
  case F_SETLKW:
    if (lock->l_type == F_UNLCK) {
      release_lock (pfd, lock);
      return 0;
    }
    
    int possible = test_for_lock (pfd->inode, cmd, lock, transport, client_pid);
    if (possible) {
      /* XXX: handle lock conversions */
      gf_log ("posix-lock", GF_LOG_DEBUG, "no conflicts, trying to acquire_lock");
      acquire_lock (fd, pfd->inode, lock, frame, transport, client_pid);
      return 0;
    }
    else {
      if (cmd == F_SETLKW) {
	/* 
	   if command set lock
	   alloc a lock and block, yo
	   return zero. peace.

	   HaikuCoding (tm)
	*/

	alloc_lock_and_block (pfd, cmd, lock, frame, client_pid);
	return 0;
      }

      return -1;
    }
    break;
  }

  return -1;
}

#ifdef POSIX_LOCK_TEST
                     
int main (int argc, char **argv)
{
  call_frame_t *frame = calloc (sizeof (call_frame_t), 1);
  transport_t *transport = calloc (sizeof (transport_t), 1);

  struct flock lock;
  lock.l_start = 0;
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  lock.l_len = 2;
  lock.l_pid = 0;

  posix_register_new_fd (1, 100);
  posix_register_new_fd (2, 100);

  assert (posix_fcntl (1, F_SETLK, &lock, frame, transport, 1) == 0);
  assert (posix_fcntl (2, F_SETLK, &lock, frame, transport, 1) == 0);
  lock.l_len = 1;
  assert (posix_fcntl (2, F_SETLK, &lock, frame, transport, 3) == -1);
  
  posix_release_fd (2);
  assert (posix_fcntl (2, F_SETLK, &lock, frame, transport, 1) == -1);
  assert (errno == -EBADF);
  lock.l_start = 4;
  lock.l_len = 5;
  assert (posix_fcntl (1, F_SETLK, &lock, frame, transport, 1) == 0);
  assert (posix_fcntl (1, F_SETLK, &lock, frame, transport, 2) == -1);

  return 0;
}

#endif
