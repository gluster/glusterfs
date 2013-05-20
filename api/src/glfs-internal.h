/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef _GLFS_INTERNAL_H
#define _GLFS_INTERNAL_H

#include "xlator.h"

struct glfs;

typedef int (*glfs_init_cbk) (struct glfs *fs, int ret);

struct glfs {
	char               *volname;

	glusterfs_ctx_t    *ctx;

	pthread_t           poller;

	glfs_init_cbk       init_cbk;
	pthread_mutex_t     mutex;
	pthread_cond_t      cond;
	int                 init;
	int                 ret;
	int                 err;

	xlator_t           *active_subvol;

	char               *oldvolfile;
	ssize_t             oldvollen;

	inode_t            *cwd;

	uint32_t            dev_id; /* Used to fill st_dev in struct stat */
};

struct glfs_fd {
	struct glfs       *fs;
	off_t              offset;
	fd_t              *fd;
	struct list_head   entries;
	gf_dirent_t       *next;
};

#define DEFAULT_EVENT_POOL_SIZE           16384
#define GF_MEMPOOL_COUNT_OF_DICT_T        4096
#define GF_MEMPOOL_COUNT_OF_DATA_T        (GF_MEMPOOL_COUNT_OF_DICT_T * 4)
#define GF_MEMPOOL_COUNT_OF_DATA_PAIR_T   (GF_MEMPOOL_COUNT_OF_DICT_T * 4)

int glfs_mgmt_init (struct glfs *fs);
void glfs_init_done (struct glfs *fs, int ret);
int glfs_process_volfp (struct glfs *fs, FILE *fp);
int glfs_resolve (struct glfs *fs, xlator_t *subvol, const char *path, loc_t *loc,
		  struct iatt *iatt, int reval);
int glfs_lresolve (struct glfs *fs, xlator_t *subvol, const char *path, loc_t *loc,
		   struct iatt *iatt, int reval);
void glfs_first_lookup (xlator_t *subvol);

static inline void
__glfs_entry_fs (struct glfs *fs)
{
	THIS = fs->ctx->master;
}


static inline void
__glfs_entry_fd (struct glfs_fd *fd)
{
	THIS = fd->fd->inode->table->xl->ctx->master;
}


void glfs_fd_destroy (struct glfs_fd *glfd);

struct glfs_fd *glfs_fd_new (struct glfs *fs);

xlator_t * glfs_fd_subvol (struct glfs_fd *glfd);

xlator_t * glfs_active_subvol (struct glfs *fs);

#endif /* !_GLFS_INTERNAL_H */
