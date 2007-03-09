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
static posix_inode *posix_inodes[HASH_TABLE_SIZE];

/* File descriptor table */
static posix_fd *posix_fds[HASH_TABLE_SIZE];

/* Lookup an inode */
static posix_inode *
lookup_inode (ino_t inode)
{
  uint32_t hash = integer_hash (inode, HASH_TABLE_SIZE);
  posix_inode *ino = posix_inodes[hash];
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
  posix_fd *f = posix_fds[hash];
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
    inode->hash_next = posix_inodes[hash];
    posix_inodes[hash] = inode;
  }

  pfd->fd = fd;
  pfd->inode = inode;

  hash = integer_hash (fd, HASH_TABLE_SIZE);
  pfd->hash_next = posix_fds[hash];
  posix_fds[hash] = pfd;

  return 0;
}

int 
posix_release_fd (int fd)
{
  int hash = integer_hash (fd, HASH_TABLE_SIZE);
  posix_fd *f = posix_fds[hash];
  posix_fd *prev = f;

  while (f) {
    if (f->fd == fd) {
      if (f == prev) 
	posix_fds[hash] = f->hash_next;
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
  posix_inode *inode = lookup_inode (ino);
  if (!inode)
    return -1;

  posix_lock *l = inode->locks;
  while (l) {
    posix_lock *next = l->next;
    free (l);
    l = next;
  }

  int hash = integer_hash (ino, HASH_TABLE_SIZE);
  posix_inode *i = posix_inodes[hash];
  posix_inode *prev = i;
  while (i) {
    if (i == inode) {
      if (i == prev)
	posix_inodes[hash] = i->hash_next;
      else
	prev->hash_next = i->hash_next;
      break;
    }

    i = i->hash_next;
  }

  free (inode); 
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
    if ((l->fd == pfd->fd) && lock_overlap (&l->flock, lock)) {
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

/* XXX: Find a cleaner way to write this mess */

static void
split_lock (posix_lock *oldlock, struct flock *newlock)
{
  posix_inode *inode = lookup_fd (oldlock->fd)->inode;

  posix_lock *leftover = calloc (sizeof (posix_lock), 1);
  posix_lock *leftover2 = NULL;

  leftover->fd = oldlock->fd;
  leftover->frame = oldlock->frame;
  leftover->transport = oldlock->transport;
  leftover->client_pid = oldlock->client_pid;

  unsigned long L1 = oldlock->flock.l_start;
  unsigned long R1 = L1 + (oldlock->flock.l_len == 0 ? ULONG_MAX : oldlock->flock.l_len);

  unsigned long L2 = newlock->l_start;
  unsigned long R2 = L2 + (newlock->l_len == 0 ? ULONG_MAX : newlock->l_len);
  
  if (R1 == L2) { /* case 1 */
    copy_flock (&leftover->flock, &oldlock->flock);
    copy_flock (&oldlock->flock, newlock);
    
    leftover->flock.l_len = (R1 - leftover->flock.l_start) - 1;
  }
  else if (L1 < L2 && L2 < R1) { /* case 2 */
    copy_flock (&leftover->flock, &oldlock->flock);
    copy_flock (&oldlock->flock, newlock);

    leftover->flock.l_len = (L2 - leftover->flock.l_start - 1);
  }
  else if (L2 < L1 && R2 <= R1) { /* case 5 */
    copy_flock (&leftover->flock, &oldlock->flock);
    copy_flock (&oldlock->flock, newlock);

    if (R2 == ULONG_MAX) {
      free (leftover);
      return;
    }
    else
      leftover->flock.l_start = R2 + 1;
  }
  else if (L1 == R2) { /* case 6 */
    copy_flock (&leftover->flock, &oldlock->flock);
    copy_flock (&oldlock->flock, newlock);
    
    leftover->flock.l_start = L1 + 1;
  }
  else if (L1 < L2 && R2 < R1) { /* case 7 */
    leftover2 = calloc (sizeof (posix_lock), 1);
    leftover2->fd = oldlock->fd;
    leftover2->frame = oldlock->frame;
    leftover2->transport = oldlock->transport;
    leftover2->client_pid = oldlock->client_pid;

    copy_flock (&leftover->flock, &oldlock->flock);
    copy_flock (&leftover2->flock, &oldlock->flock);
    copy_flock (&oldlock->flock, newlock);
    
    leftover->flock.l_len = L2 - L1 - 1;

    leftover2->flock.l_start = R2 + 1;
    if (R1 == ULONG_MAX)
      leftover2->flock.l_len = 0;
    else
      leftover2->flock.l_len = R1 - leftover2->flock.l_start;
  }
  else {
    gf_log ("posix-lock", GF_LOG_DEBUG, "Unexpected case in split_lock");
  }

  leftover->next = inode->locks;
  inode->locks = leftover;

  if (leftover2) {
    leftover2->next = inode->locks;
    inode->locks = leftover2;
  }
}

static void
merge_lock (posix_lock *oldlock, struct flock *newlock)
{
  int min (int x, int y)
  {
    return x < y ? x : y;
  }

  int max_len (int x, int y)
  {
    /* 0 is infinity when it comes to l_len */
    if (x == 0) return x;
    if (y == 0) return y;
    else return x > y ? x : y;
  }

  oldlock->flock.l_start = min (oldlock->flock.l_start, newlock->l_start);
  oldlock->flock.l_len   = max_len (oldlock->flock.l_len, newlock->l_len);
}

/*
  Convert 
  Let's designate the regions locked by oldlock and newlock by
  oldlock: L1      R1
             ------
  newlock: L2      R2
             ------
  
  The possibilities are:

  (1) L1    R1
            L2    R2                split into 2 locks, merge if types equal

  (2) L1    R1 
         L2       R2                split into 2 locks

  (3) L1    R1                      convert
      L2    R2 

  (4)    L1  R1
      L2          R2                older lock subsumed by new lock

  (5)    L1           R1            split into 2 locks
      L2          R2

  (6)             L1       R1       split into 2 locks, merge is types equal
      L2          R2

  (7) L1                 R1         split into 3 locks, do nothing if types equal
          L2    R2
*/

static int 
convert_lock (posix_lock *oldlock, struct flock *newlock)
{
  unsigned long L1 = oldlock->flock.l_start;
  unsigned long R1 = L1 + (oldlock->flock.l_len == 0 ? ULONG_MAX : oldlock->flock.l_len);

  unsigned long L2 = newlock->l_start;
  unsigned long R2 = L2 + (newlock->l_len == 0 ? ULONG_MAX : newlock->l_len);

  if (R1 == L2) {  /* case 1 */
    if (oldlock->flock.l_type == newlock->l_type) 
      merge_lock (oldlock, newlock);
    else
      split_lock (oldlock, newlock);
  }
  else if (L1 < L2 && L2 < R1) { /* case 2 */
    split_lock (oldlock, newlock);
  }
  else if (L1 == L2 && R1 == R2) { /* case 3 */
    oldlock->flock.l_type = newlock->l_type;
  }
  else if (L2 < L1 && R1 <= R2) { /* case 4 */
    oldlock->flock.l_type = newlock->l_type;
    merge_lock (oldlock, newlock);
  }
  else if (L2 < L1 && R2 <= R1) { /* case 5 */
    split_lock (oldlock, newlock);
  }
  else if (L1 == R2) { /* case 6 */
    if (oldlock->flock.l_type == newlock->l_type)
      merge_lock (oldlock, newlock);
    else
      split_lock (oldlock, newlock);
  }
  else if (L1 < L2 && R2 <= R1) { /* case 7 */
    if (oldlock->flock.l_type == newlock->l_type)
      /* Do nothing; this region is already locked */ ;
    else
      split_lock (oldlock, newlock);
  }
  else {
    gf_log ("posix-lock", GF_LOG_DEBUG, "Unexpected case in convert_lock");
  }

  return 0;
}

/*
  Test if there's no other lock conflicting with this lock
  Return: NULL if lock can be acquired, the offending lock if there's
          a conflict
  convert is a result param that will be set to true if there
  is a lock on the region that can be converted
*/

static posix_lock *
test_for_lock (posix_inode *ino, posix_fd *pfd, struct flock *newlock,
	       transport_t *transport, pid_t client_pid, int *convert)
{
  *convert = 0;
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
	/* The conflicting lock is held by us. Will be converted */
	*convert = 1;
	return lock;
      }

      if (lock->flock.l_type == F_WRLCK) {
	/* this region has been exclusively locked, so no-can-do */
	errno = -EAGAIN;
	return lock;
      }

      if (newlock->l_type == F_WRLCK) {
	/* we want an exclusive lock, but there's already some other lock */
	errno = -EAGAIN;
	return lock;
      }
    }

    lock = lock->next;
  }

  return NULL;
}

static int
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
    STACK_UNWIND (frame, -1, errno, lock);
    return -1;
  }

  switch (cmd) {
  case F_GETLK: {
    int ret = get_lock (pfd->inode, lock);
    STACK_UNWIND (frame, ret, errno, lock);
    return 0;
    break;
  }

  case F_SETLK:
  case F_SETLKW:
    if (lock->l_type == F_UNLCK) {
      gf_log ("posix-lock", GF_LOG_DEBUG, "releasing lock");
      release_lock (pfd, lock);
      STACK_UNWIND (frame, 0, errno, lock);
      return 0;
    }

    int convert = 0;
    posix_lock *conflict = test_for_lock (pfd->inode, pfd, lock, transport, client_pid, &convert);
    if (conflict == NULL) {
      gf_log ("posix-lock", GF_LOG_DEBUG, "no conflicts, trying to acquire lock");
      acquire_lock (fd, pfd->inode, lock, frame, transport, client_pid);
      STACK_UNWIND (frame, 0, errno, lock);
      return 0;
    }
    else if (conflict && convert) {
      gf_log ("posix-lock", GF_LOG_DEBUG, "trying to convert lock");
      convert_lock (conflict, lock);
      STACK_UNWIND (frame, 0, errno, lock);
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
	
	gf_log ("posix-lock", GF_LOG_DEBUG, "blocking while trying to acquire lock");
	alloc_lock_and_block (pfd, cmd, lock, frame, client_pid);
	return 0;
      }
      
      STACK_UNWIND (frame, -1, errno, lock);
      return -1;
    }
    break;
  }

  STACK_UNWIND (frame, -1, errno, lock);
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
