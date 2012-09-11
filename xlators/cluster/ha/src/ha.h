/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __HA_H_
#define __HA_H_

#include "ha-mem-types.h"

typedef struct {
	call_stub_t *stub;
	int32_t op_ret, op_errno;
	int32_t active, tries, revalidate, revalidate_error;
	int32_t call_count;
	char *state, *pattern;
	dict_t *dict;
	loc_t loc;
	struct iatt buf;
        struct iatt postparent;
        struct iatt preparent;
	fd_t *fd;
	inode_t *inode;
	int32_t flags;
	int32_t first_success;
} ha_local_t;

typedef struct {
	char *state;
	xlator_t **children;
	int child_count, pref_subvol;
} ha_private_t;

typedef struct {
	char *fdstate;
	char *path;
	gf_lock_t lock;
	int active;
} hafd_t;

#define HA_ACTIVE_CHILD(this, local) (((ha_private_t *)this->private)->children[local->active])

extern int ha_alloc_init_fd (call_frame_t *frame, fd_t *fd);

extern int ha_handle_cbk (call_frame_t *frame, void *cookie, int op_ret, int op_errno) ;

extern int ha_alloc_init_inode (call_frame_t *frame, inode_t *inode);

#endif
