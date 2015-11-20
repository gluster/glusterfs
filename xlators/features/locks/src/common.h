/*
   Copyright (c) 2006-2012, 2015-2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __COMMON_H__
#define __COMMON_H__

#include "lkowner.h"
/*dump locks format strings */
#define RANGE_FMT               "type=%s, whence=%hd, start=%llu, len=%llu"
#define ENTRY_FMT               "type=%s on basename=%s"
#define DUMP_GEN_FMT            "pid = %llu, owner=%s, client=%p"
#define GRNTD_AT                "granted at %s"
#define BLKD_AT                 "blocked at %s"
#define CONN_ID                 "connection-id=%s"
#define DUMP_BLKD_FMT           DUMP_GEN_FMT", "CONN_ID", "BLKD_AT
#define DUMP_GRNTD_FMT          DUMP_GEN_FMT", "CONN_ID", "GRNTD_AT
#define DUMP_BLKD_GRNTD_FMT     DUMP_GEN_FMT", "CONN_ID", "BLKD_AT", "GRNTD_AT

#define ENTRY_BLKD_FMT          ENTRY_FMT", "DUMP_BLKD_FMT
#define ENTRY_GRNTD_FMT         ENTRY_FMT", "DUMP_GRNTD_FMT
#define ENTRY_BLKD_GRNTD_FMT    ENTRY_FMT", "DUMP_BLKD_GRNTD_FMT

#define RANGE_BLKD_FMT          RANGE_FMT", "DUMP_BLKD_FMT
#define RANGE_GRNTD_FMT         RANGE_FMT", "DUMP_GRNTD_FMT
#define RANGE_BLKD_GRNTD_FMT    RANGE_FMT", "DUMP_BLKD_GRNTD_FMT

#define SET_FLOCK_PID(flock, lock) ((flock)->l_pid = lock->client_pid)


posix_lock_t *
new_posix_lock (struct gf_flock *flock, client_t *client, pid_t client_pid,
                gf_lkowner_t *owner, fd_t *fd, uint32_t lk_flags,
                int can_block);

pl_inode_t *
pl_inode_get (xlator_t *this, inode_t *inode);

posix_lock_t *
pl_getlk (pl_inode_t *inode, posix_lock_t *lock);

int
pl_setlk (xlator_t *this, pl_inode_t *inode, posix_lock_t *lock,
          int can_block);

void
grant_blocked_locks (xlator_t *this, pl_inode_t *inode);

void
posix_lock_to_flock (posix_lock_t *lock, struct gf_flock *flock);

int
locks_overlap (posix_lock_t *l1, posix_lock_t *l2);

int
same_owner (posix_lock_t *l1, posix_lock_t *l2);

void __delete_lock (posix_lock_t *);

void __destroy_lock (posix_lock_t *);

pl_dom_list_t *
get_domain (pl_inode_t *pl_inode, const char *volume);

void
grant_blocked_inode_locks (xlator_t *this, pl_inode_t *pl_inode,
                           pl_dom_list_t *dom);

void
__delete_inode_lock (pl_inode_lock_t *lock);

void
__pl_inodelk_unref (pl_inode_lock_t *lock);

void
grant_blocked_entry_locks (xlator_t *this, pl_inode_t *pl_inode,
                           pl_dom_list_t *dom);

void pl_update_refkeeper (xlator_t *this, inode_t *inode);

int32_t
__get_inodelk_count (xlator_t *this, pl_inode_t *pl_inode, char *domname);
int32_t
get_inodelk_count (xlator_t *this, inode_t *inode, char *domname);

int32_t
__get_entrylk_count (xlator_t *this, pl_inode_t *pl_inode);
int32_t
get_entrylk_count (xlator_t *this, inode_t *inode);

void pl_trace_in (xlator_t *this, call_frame_t *frame, fd_t *fd, loc_t *loc,
                  int cmd, struct gf_flock *flock, const char *domain);

void pl_trace_out (xlator_t *this, call_frame_t *frame, fd_t *fd, loc_t *loc,
                   int cmd, struct gf_flock *flock, int op_ret, int op_errno, const char *domain);

void pl_trace_block (xlator_t *this, call_frame_t *frame, fd_t *fd, loc_t *loc,
                     int cmd, struct gf_flock *flock, const char *domain);

void pl_trace_flush (xlator_t *this, call_frame_t *frame, fd_t *fd);

void entrylk_trace_in (xlator_t *this, call_frame_t *frame, const char *volume,
                       fd_t *fd, loc_t *loc, const char *basename,
                       entrylk_cmd cmd, entrylk_type type);

void entrylk_trace_out (xlator_t *this, call_frame_t *frame, const char *volume,
                        fd_t *fd, loc_t *loc, const char *basename,
                        entrylk_cmd cmd, entrylk_type type,
                        int op_ret, int op_errno);

void entrylk_trace_block (xlator_t *this, call_frame_t *frame, const char *volume,
                          fd_t *fd, loc_t *loc, const char *basename,
                          entrylk_cmd cmd, entrylk_type type);

void
pl_print_verdict (char *str, int size, int op_ret, int op_errno);

void
pl_print_lockee (char *str, int size, fd_t *fd, loc_t *loc);

void
pl_print_locker (char *str, int size, xlator_t *this, call_frame_t *frame);

void
pl_print_inodelk (char *str, int size, int cmd, struct gf_flock *flock, const char *domain);

void
pl_trace_release (xlator_t *this, fd_t *fd);

unsigned long
fd_to_fdnum (fd_t *fd);

fd_t *
fd_from_fdnum (posix_lock_t *lock);

int
pl_reserve_setlk (xlator_t *this, pl_inode_t *pl_inode, posix_lock_t *lock,
                  int can_block);
int
reservelks_equal (posix_lock_t *l1, posix_lock_t *l2);

int
pl_verify_reservelk (xlator_t *this, pl_inode_t *pl_inode,
                     posix_lock_t *lock, int can_block);
int
pl_reserve_unlock (xlator_t *this, pl_inode_t *pl_inode, posix_lock_t *reqlock);

int32_t
check_entrylk_on_basename (xlator_t *this, inode_t *parent, char *basename);

void __pl_inodelk_unref (pl_inode_lock_t *lock);
void __pl_entrylk_unref (pl_entry_lock_t *lock);

int
pl_metalock_is_active (pl_inode_t *pl_inode);

int
__pl_queue_lock (pl_inode_t *pl_inode, posix_lock_t *reqlock, int can_block);

gf_boolean_t
pl_does_monkey_want_stuck_lock();
#endif /* __COMMON_H__ */
