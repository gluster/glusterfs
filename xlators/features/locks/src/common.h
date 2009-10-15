/*
  Copyright (c) 2006, 2007, 2008 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __COMMON_H__
#define __COMMON_H__

posix_lock_t *
new_posix_lock (struct flock *flock, transport_t *transport, pid_t client_pid);

pl_inode_t *
pl_inode_get (xlator_t *this, inode_t *inode);

posix_lock_t *
pl_getlk (pl_inode_t *inode, posix_lock_t *lock, gf_lk_domain_t domain);

int
pl_setlk (xlator_t *this, pl_inode_t *inode, posix_lock_t *lock,
	  int can_block, gf_lk_domain_t domain);

void
grant_blocked_locks (xlator_t *this, pl_inode_t *inode, gf_lk_domain_t domain);

void
posix_lock_to_flock (posix_lock_t *lock, struct flock *flock);

int
locks_overlap (posix_lock_t *l1, posix_lock_t *l2);

int
same_owner (posix_lock_t *l1, posix_lock_t *l2);

void __delete_lock (pl_inode_t *, posix_lock_t *);

void __destroy_lock (posix_lock_t *);

void pl_update_refkeeper (xlator_t *this, inode_t *inode);

#endif /* __COMMON_H__ */
