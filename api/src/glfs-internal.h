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

#define GLFS_SYMLINK_MAX_FOLLOW 2048

#define DEFAULT_REVAL_COUNT 1

/*
 * syncop_xxx() calls are executed in two ways, one is inside a synctask where
 * the executing function will do 'swapcontext' and the other is without
 * synctask where the executing thread is made to wait using pthread_cond_wait.
 * Executing thread may change when syncop_xxx() is executed inside a synctask.
 * This leads to errno_location change i.e. errno may give errno of
 * non-executing thread. So errno is not touched inside a synctask execution.
 * All gfapi calls are executed using the second way of executing syncop_xxx()
 * where the executing thread waits using pthread_cond_wait so it is ok to set
 * errno in these cases. The following macro makes syncop_xxx() behave just
 * like a system call, where -1 is returned and errno is set when a failure
 * occurs.
 */
#define DECODE_SYNCOP_ERR(ret) do {  \
        if (ret < 0) {          \
                errno = -ret;   \
                ret = -1;       \
        }                       \
        } while (0)

#define ESTALE_RETRY(ret,errno,reval,loc,label) do {	\
	if (ret == -1 && errno == ESTALE) {	        \
		if (reval < DEFAULT_REVAL_COUNT) {	\
			reval++;			\
			loc_wipe (loc);			\
			goto label;			\
		}					\
	}						\
	} while (0)

#define GLFS_LOC_FILL_INODE(oinode, loc, label) do {   \
	loc.inode = inode_ref (oinode);                \
	uuid_copy (loc.gfid, oinode->gfid);            \
	ret = glfs_loc_touchup (&loc);                 \
	if (ret != 0) {                                \
		errno = EINVAL;                        \
		goto label;                            \
	}                                              \
	} while (0)

#define GLFS_LOC_FILL_PINODE(pinode, loc, ret, errno, label, path) do {   \
	loc.inode = inode_new (pinode->table);                            \
	if (!loc.inode) {                                                 \
		ret = -1;                                                 \
		errno = ENOMEM;                                           \
		goto label;                                               \
	}                                                                 \
	loc.parent = inode_ref (pinode);                                  \
	loc.name = path;                                                  \
	ret = glfs_loc_touchup (&loc);                                    \
	if (ret != 0) {                                                   \
		errno = EINVAL;                                           \
		goto label;                                               \
	}                                                                 \
	} while (0)

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
	xlator_t           *next_subvol;
	xlator_t           *old_subvol;

	char               *oldvolfile;
	ssize_t             oldvollen;

	inode_t            *cwd;

	uint32_t            dev_id; /* Used to fill st_dev in struct stat */

	struct list_head    openfds;

	gf_boolean_t        migration_in_progress;
};

struct glfs_fd {
	struct list_head   openfds;
	struct glfs       *fs;
	off_t              offset;
	fd_t              *fd; /* Currently guared by @fs->mutex. TODO: per-glfd lock */
	struct list_head   entries;
	gf_dirent_t       *next;
	struct dirent     *readdirbuf;
};

/* glfs object handle introduced for the alternate gfapi implementation based
   on glfs handles/gfid/inode
*/
struct glfs_object {
        inode_t         *inode;
        uuid_t          gfid;
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
fd_t *glfs_resolve_fd (struct glfs *fs, xlator_t *subvol, struct glfs_fd *glfd);

fd_t *__glfs_migrate_fd (struct glfs *fs, xlator_t *subvol, struct glfs_fd *glfd);

int glfs_first_lookup (xlator_t *subvol);

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


/*
  By default all lock attempts from user context must
  use glfs_lock() and glfs_unlock(). This allows
  for a safe implementation of graph migration where
  we can give up the mutex during syncop calls so
  that bottom up calls (particularly CHILD_UP notify)
  can do a mutex_lock() on @glfs without deadlocking
  the filesystem
*/
static inline int
glfs_lock (struct glfs *fs)
{
	pthread_mutex_lock (&fs->mutex);

	while (!fs->init)
		pthread_cond_wait (&fs->cond, &fs->mutex);

	while (fs->migration_in_progress)
		pthread_cond_wait (&fs->cond, &fs->mutex);

	return 0;
}


static inline void
glfs_unlock (struct glfs *fs)
{
	pthread_mutex_unlock (&fs->mutex);
}


void glfs_fd_destroy (struct glfs_fd *glfd);

struct glfs_fd *glfs_fd_new (struct glfs *fs);
void glfs_fd_bind (struct glfs_fd *glfd);

xlator_t * glfs_active_subvol (struct glfs *fs);
xlator_t * __glfs_active_subvol (struct glfs *fs);
void glfs_subvol_done (struct glfs *fs, xlator_t *subvol);

inode_t * glfs_refresh_inode (xlator_t *subvol, inode_t *inode);

inode_t *glfs_cwd_get (struct glfs *fs);
int glfs_cwd_set (struct glfs *fs, inode_t *inode);
inode_t *glfs_resolve_inode (struct glfs *fs, xlator_t *subvol,
			     struct glfs_object *object);
int glfs_create_object (loc_t *loc, struct glfs_object **retobject);
int __glfs_cwd_set (struct glfs *fs, inode_t *inode);

int glfs_resolve_base (struct glfs *fs, xlator_t *subvol, inode_t *inode,
		       struct iatt *iatt);
int glfs_resolve_at (struct glfs *fs, xlator_t *subvol, inode_t *at,
                     const char *origpath, loc_t *loc, struct iatt *iatt,
                     int follow, int reval);
int glfs_loc_touchup (loc_t *loc);
void glfs_iatt_to_stat (struct glfs *fs, struct iatt *iatt, struct stat *stat);
int glfs_loc_link (loc_t *loc, struct iatt *iatt);
int glfs_loc_unlink (loc_t *loc);

#endif /* !_GLFS_INTERNAL_H */
