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

#ifndef GF_DARWIN_HOST_OS
#ifndef GFAPI_PUBLIC
#define GFAPI_PUBLIC(sym, ver) /**/
#endif
#ifndef GFAPI_PRIVATE
#define GFAPI_PRIVATE(sym, ver) /**/
#endif
#define GFAPI_SYMVER_PUBLIC_DEFAULT(fn, ver) \
        asm(".symver pub_"STR(fn)", "STR(fn)"@@GFAPI_"STR(ver))

#define GFAPI_SYMVER_PRIVATE_DEFAULT(fn, ver) \
        asm(".symver priv_"STR(fn)", "STR(fn)"@@GFAPI_PRIVATE_"STR(ver))

#define GFAPI_SYMVER_PUBLIC(fn1, fn2, ver) \
        asm(".symver pub_"STR(fn1)", "STR(fn2)"@@GFAPI_"STR(ver))

#define GFAPI_SYMVER_PRIVATE(fn1, fn2, ver) \
        asm(".symver priv_"STR(fn1)", "STR(fn2)"@@GFAPI_PRIVATE_"STR(ver))
#define STR(str) #str
#else
#ifndef GFAPI_PUBLIC
#define GFAPI_PUBLIC(sym, ver) __asm("_" __STRING(sym) "$GFAPI_" __STRING(ver))
#endif
#ifndef GFAPI_PRIVATE
#define GFAPI_PRIVATE(sym, ver) __asm("_" __STRING(sym) "$GFAPI_PRIVATE_" __STRING(ver))
#endif
#define GFAPI_SYMVER_PUBLIC_DEFAULT(fn, dotver) /**/
#define GFAPI_SYMVER_PRIVATE_DEFAULT(fn, dotver) /**/
#define GFAPI_SYMVER_PUBLIC(fn1, fn2, dotver) /**/
#define GFAPI_SYMVER_PRIVATE(fn1, fn2, dotver) /**/
#endif

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
        uuid_t              vol_uuid;

	glusterfs_ctx_t    *ctx;

	pthread_t           poller;

	glfs_init_cbk       init_cbk;
	pthread_mutex_t     mutex;
	pthread_cond_t      cond;
        pthread_cond_t      child_down_cond; /* for broadcasting CHILD_DOWN */
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

#define GF_MEMPOOL_COUNT_OF_LRU_BUF_T     256

int glfs_mgmt_init (struct glfs *fs);
void glfs_init_done (struct glfs *fs, int ret)
        GFAPI_PRIVATE(glfs_init_done, 3.4.0);
int glfs_process_volfp (struct glfs *fs, FILE *fp);
int glfs_resolve (struct glfs *fs, xlator_t *subvol, const char *path,
                       loc_t *loc, struct iatt *iatt, int reval)
        GFAPI_PRIVATE(glfs_resolve, 3.7.0);
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

xlator_t *glfs_active_subvol (struct glfs *fs)
        GFAPI_PRIVATE(glfs_active_subvol, 3.4.0);
xlator_t *__glfs_active_subvol (struct glfs *fs);
void glfs_subvol_done (struct glfs *fs, xlator_t *subvol)
        GFAPI_PRIVATE(glfs_subvol_done, 3.4.0);

inode_t *glfs_refresh_inode (xlator_t *subvol, inode_t *inode);

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
                          int follow, int reval)
        GFAPI_PRIVATE(glfs_resolve_at, 3.4.0);
int glfs_loc_touchup (loc_t *loc)
	GFAPI_PRIVATE(glfs_loc_touchup, 3.4.0);
void glfs_iatt_to_stat (struct glfs *fs, struct iatt *iatt, struct stat *stat);
int glfs_loc_link (loc_t *loc, struct iatt *iatt);
int glfs_loc_unlink (loc_t *loc);
dict_t *dict_for_key_value (const char *name, const char *value, size_t size);
int glfs_getxattr_process (void *value, size_t size, dict_t *xattr,
			   const char *name);

/* Sends RPC call to glusterd to fetch required volume info */
int glfs_get_volume_info (struct glfs *fs);

/*
  SYNOPSIS

       glfs_new_from_ctx: Creates a virtual mount object by taking a
       glusterfs_ctx_t object.

  DESCRIPTION

       glfs_new_from_ctx() is not same as glfs_new(). It takes the
       glusterfs_ctx_t object instead of creating one by glusterfs_ctx_new().
       Again the usage is restricted to NFS MOUNT over UDP i.e. in
       glfs_resolve_at() which would take fs object as input but never use
       (purpose is not to change the ABI of glfs_resolve_at()).

  PARAMETERS

       @ctx: glusterfs_ctx_t object

  RETURN VALUES

       fs     : Pointer to the newly created glfs_t object.
       NULL   : Otherwise.
*/

struct glfs *glfs_new_from_ctx (glusterfs_ctx_t *ctx)
        GFAPI_PRIVATE(glfs_new_from_ctx, 3.7.0);

/*
  SYNOPSIS

       glfs_free_from_ctx: Free up the memory occupied by glfs_t object
       created by glfs_new_from_ctx().

  DESCRIPTION

       The glfs_t object allocated by glfs_new_from_ctx() must be released
       by the caller using this routine. The usage is restricted to NFS
       MOUNT over UDP i.e.
       __mnt3udp_get_export_subdir_inode ()
                                => glfs_resolve_at().

  PARAMETERS

       @fs: The glfs_t object to be deallocated.

  RETURN VALUES

       void
*/

void glfs_free_from_ctx (struct glfs *fs)
         GFAPI_PRIVATE(glfs_free_from_ctx, 3.7.0);


#endif /* !_GLFS_INTERNAL_H */
