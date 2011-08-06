/*
  Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <sys/time.h>

#include "xlator.h"
#include "defaults.h"
#include "common-utils.h"
#include "quota-mem-types.h"

#ifndef MAX_IOVEC
#define MAX_IOVEC 16
#endif

struct quota_local {
	struct iatt    stbuf;
	inode_t       *inode;
	char          *path;
	fd_t          *fd;
	off_t          offset;
	int32_t        count;
	struct iovec   vector[MAX_IOVEC];
	struct iobref *iobref;
	loc_t          loc;
        int            flags;
};


struct quota_priv {
	char       only_first_time;          /* Used to make sure a call is done only one time */
	gf_lock_t  lock;                     /* Used while updating variables */

	uint64_t   disk_usage_limit;         /* Used for Disk usage quota */
	uint64_t   current_disk_usage;       /* Keep the current usage value */

	uint32_t   min_free_disk_limit;        /* user specified limit, in %*/
	uint32_t   current_free_disk;          /* current free disk space available, in % */
	uint32_t   refresh_interval;           /* interval in seconds */
	uint32_t   min_disk_last_updated_time; /* used for interval calculation */	

	loc_t      root_loc;		     /* Store '/' loc_t to make xattr calls */
};


int
quota_statvfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		   int32_t op_ret, int32_t op_errno, struct statvfs *stbuf)
{
	struct quota_priv *priv = this->private;
	
	if (op_ret >= 0) {
		priv->current_free_disk =
			(stbuf->f_bavail * 100) / stbuf->f_blocks;
	}

	STACK_DESTROY (frame->root);
	return 0;
}


void
gf_quota_usage_subtract (xlator_t *this, size_t size)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	LOCK (&priv->lock);
	{
		if (priv->current_disk_usage < size)
			priv->current_disk_usage = 0;
		else
			priv->current_disk_usage -= size;
	}
	UNLOCK (&priv->lock);
}


void
gf_quota_usage_add (xlator_t *this, size_t size)
{
	struct quota_priv *priv = this->private;

	LOCK (&priv->lock);
	{
		priv->current_disk_usage += size;
	}
	UNLOCK (&priv->lock);
}


void 
gf_quota_update_current_free_disk (xlator_t *this)
{
	call_frame_t *frame = NULL;
	call_pool_t   *pool = NULL;

	struct quota_priv *priv = NULL;

	pool  = this->ctx->pool;
	frame = create_frame (this, pool);
  
	priv = this->private;

	STACK_WIND (frame, quota_statvfs_cbk,
		    this->children->xlator,
		    this->children->xlator->fops->statfs, &(priv->root_loc));

	return ;
}


int
gf_quota_check_free_disk (xlator_t *this) 
{
        struct quota_priv * priv = NULL;
	struct timeval tv = {0, 0};

	priv = this->private;
	if (priv->min_free_disk_limit) {
		gettimeofday (&tv, NULL);
		if (tv.tv_sec > (priv->refresh_interval + 
				 priv->min_disk_last_updated_time)) {
			priv->min_disk_last_updated_time = tv.tv_sec;
			gf_quota_update_current_free_disk (this);
		}
		if (priv->current_free_disk <= priv->min_free_disk_limit)
			return -1;
	}

	return 0;
}


int
quota_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf)
{
	struct quota_priv *priv = this->private;
	struct quota_local *local = NULL;

	local = frame->local;

	if ((op_ret >= 0) && priv->disk_usage_limit) {
		gf_quota_usage_subtract (this, (local->stbuf.ia_blocks -
						postbuf->ia_blocks) * 512);
		loc_wipe (&local->loc);
	}

	STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno,
                             prebuf, postbuf);
	return 0;
}


int
quota_truncate_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			 int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
	struct quota_local *local = NULL;

	local = frame->local;

	if (op_ret >= 0) {
		local->stbuf = *buf;
	}

	STACK_WIND (frame, quota_truncate_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->truncate,
		    &local->loc, local->offset);
	return 0;
}


int
quota_truncate (call_frame_t *frame, xlator_t *this,
		loc_t *loc, off_t offset)
{
	struct quota_local *local = NULL;
	struct quota_priv  *priv = NULL;

	priv = this->private;

	if (priv->disk_usage_limit) {
		local = GF_CALLOC (1, sizeof (struct quota_local),
                                   gf_quota_mt_quota_local);
		frame->local  = local;

		loc_copy (&local->loc, loc);
		local->offset = offset;

		STACK_WIND (frame, quota_truncate_stat_cbk,
			    FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->stat, loc);
		return 0;
	}

	STACK_WIND (frame, quota_truncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate,
		    loc, offset);
	return 0;
}


int
quota_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf)
{
	struct quota_priv  *priv = NULL;
	struct quota_local *local = NULL;

	local = frame->local;
	priv = this->private;

	if ((op_ret >= 0) && priv->disk_usage_limit) {
		gf_quota_usage_subtract (this, (local->stbuf.ia_blocks -
						postbuf->ia_blocks) * 512);
		fd_unref (local->fd);
	}

	STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno,
                             prebuf, postbuf);
	return 0;
}


int
quota_ftruncate_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
	struct quota_local *local = NULL;

	local = frame->local;

	if (op_ret >= 0) {
		local->stbuf = *buf;
	}

	STACK_WIND (frame, quota_ftruncate_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->ftruncate,
		    local->fd, local->offset);
	return 0;
}


int
quota_ftruncate (call_frame_t *frame, xlator_t *this,
		 fd_t *fd, off_t offset)
{
	struct quota_local *local = NULL;
	struct quota_priv  *priv = NULL;


	priv = this->private;

	if (priv->disk_usage_limit) {
		local = GF_CALLOC (1, sizeof (struct quota_local),
                                   gf_quota_mt_quota_local);
		frame->local  = local;

		local->fd = fd_ref (fd);
		local->offset = offset;

		STACK_WIND (frame, quota_ftruncate_fstat_cbk,
			    FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->fstat, fd);
		return 0;
	}

	STACK_WIND (frame, quota_ftruncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->ftruncate,
		    fd, offset);
	return 0;
}


int
quota_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int32_t op_ret, int32_t op_errno,
                 inode_t *inode, struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if ((op_ret >= 0) && priv->disk_usage_limit) {
		gf_quota_usage_add (this, buf->ia_blocks * 512);
	}

	STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
	return 0;
}


int
quota_mknod (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, mode_t mode, dev_t rdev, dict_t *params)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if (gf_quota_check_free_disk (this) == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"min-free-disk limit (%u) crossed, current available is %u",
			priv->min_free_disk_limit, priv->current_free_disk);
		STACK_UNWIND_STRICT (mknod, frame, -1, ENOSPC, NULL, NULL,
                                     NULL, NULL);
		return 0;
	}

        if (priv->current_disk_usage > priv->disk_usage_limit) {
		gf_log (this->name, GF_LOG_ERROR, 
			"Disk usage limit (%"PRIu64") crossed, current usage is %"PRIu64"",
			priv->disk_usage_limit, priv->current_disk_usage);
		STACK_UNWIND_STRICT (mknod, frame, -1, ENOSPC, NULL, NULL,
                                     NULL, NULL);
		return 0;
        }

	STACK_WIND (frame, quota_mknod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mknod,
		    loc, mode, rdev, params);
	return 0;
}


int
quota_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if ((op_ret >= 0) && priv->disk_usage_limit) {
		gf_quota_usage_subtract (this, buf->ia_blocks * 512);
	}

	STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
	return 0;
}


int
quota_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             dict_t *params)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if (gf_quota_check_free_disk (this) == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"min-free-disk limit (%u) crossed, current available is %u",
			priv->min_free_disk_limit, priv->current_free_disk);
		STACK_UNWIND_STRICT (mkdir, frame, -1, ENOSPC, NULL, NULL,
                                     NULL, NULL);
		return 0;
		
	}

        if (priv->current_disk_usage > priv->disk_usage_limit) {
		gf_log (this->name, GF_LOG_ERROR, 
			"Disk usage limit (%"PRIu64") crossed, current usage is %"PRIu64"",
			priv->disk_usage_limit, priv->current_disk_usage);
		STACK_UNWIND_STRICT (mkdir, frame, -1, ENOSPC, NULL, NULL,
                                     NULL, NULL);
		return 0;
        }

	STACK_WIND (frame, quota_mkdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mkdir,
		    loc, mode, params);

	return 0;
}


int
quota_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                  struct iatt *postparent)
{
	struct quota_local *local = NULL;

	local = frame->local;

	if (local) {
		if (op_ret >= 0) {
			gf_quota_usage_subtract (this,
						 local->stbuf.ia_blocks * 512);
		}
		loc_wipe (&local->loc);
	}

	STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno, preparent, postparent);
	return 0;
}


int
quota_unlink_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
	struct quota_local *local = NULL;

	local = frame->local;

	if (op_ret >= 0) {
		if (buf->ia_nlink == 1) {
			local->stbuf = *buf;
		}
	}

	STACK_WIND (frame, quota_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    &local->loc);

	return 0;
}


int
quota_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	struct quota_local *local = NULL;
	struct quota_priv  *priv = NULL;

	priv = this->private;

	if (priv->disk_usage_limit) {
		local = GF_CALLOC (1, sizeof (struct quota_local),
                                   gf_quota_mt_quota_local);
		frame->local = local;

		loc_copy (&local->loc, loc);

		STACK_WIND (frame,
			    quota_unlink_stat_cbk,
			    FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->stat,
			    loc);
		return 0;
	}

	STACK_WIND (frame, quota_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    loc);
	return 0;
}


int
quota_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                 struct iatt *postparent)
{
	struct quota_local *local = NULL;

	local = frame->local;

	if (local) {
		if (op_ret >= 0) {
			gf_quota_usage_subtract (this, local->stbuf.ia_blocks * 512);
		}
		loc_wipe (&local->loc);
	}

	STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno, preparent, postparent);
	return 0;
}


int
quota_rmdir_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
	struct quota_local *local = NULL;

	local = frame->local;

	if (op_ret >= 0) {
		local->stbuf = *buf;
	}

	STACK_WIND (frame, quota_rmdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    &local->loc, local->flags);

	return 0;
}


int
quota_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
	struct quota_local *local = NULL;
	struct quota_priv  *priv = NULL;

	priv = this->private;

	if (priv->disk_usage_limit) {
		local = GF_CALLOC (1, sizeof (struct quota_local),
                                   gf_quota_mt_quota_local);
		frame->local = local;

		loc_copy (&local->loc, loc);
                local->flags = flags;

		STACK_WIND (frame, quota_rmdir_stat_cbk,
			    FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->stat, loc);
		return 0;
	}

	STACK_WIND (frame, quota_rmdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    loc, flags);
	return 0;
}


int
quota_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if ((op_ret >= 0) && priv->disk_usage_limit) {
		gf_quota_usage_add (this, buf->ia_blocks * 512);
	}

	STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
	return 0;
}


int
quota_symlink (call_frame_t *frame, xlator_t *this,
	       const char *linkpath, loc_t *loc, dict_t *params)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if (gf_quota_check_free_disk (this) == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"min-free-disk limit (%u) crossed, current available is %u",
			priv->min_free_disk_limit, priv->current_free_disk);
		STACK_UNWIND_STRICT (symlink, frame, -1, ENOSPC, NULL, NULL,
                                     NULL, NULL);
		return 0;
		
	}
        if (priv->current_disk_usage > priv->disk_usage_limit) {
		gf_log (this->name, GF_LOG_ERROR, 
			"Disk usage limit (%"PRIu64") crossed, current usage is %"PRIu64"",
			priv->disk_usage_limit, priv->current_disk_usage);
		STACK_UNWIND_STRICT (symlink, frame, -1, ENOSPC, NULL, NULL,
                                     NULL, NULL);
		return 0;
        }

	STACK_WIND (frame, quota_symlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->symlink,
		    linkpath, loc, params);
	return 0;
}


int
quota_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno,
		  fd_t *fd, inode_t *inode, struct iatt *buf,
                  struct iatt *preparent, struct iatt *postparent)
{
	struct quota_priv *priv = this->private;

	if ((op_ret >= 0) && priv->disk_usage_limit) {
		gf_quota_usage_add (this, buf->ia_blocks * 512);

		fd_ctx_set (fd, this, 1);
	}

	STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent);
	return 0;
}


int
quota_create (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, int32_t flags, mode_t mode, fd_t *fd, dict_t *params)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if (gf_quota_check_free_disk (this) == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"min-free-disk limit (%u) crossed, current available is %u",
			priv->min_free_disk_limit, priv->current_free_disk);
		STACK_UNWIND_STRICT (create, frame, -1, ENOSPC, NULL, NULL, NULL,
                                     NULL, NULL);
		return 0;
		
	}
        if (priv->current_disk_usage > priv->disk_usage_limit) {
		gf_log (this->name, GF_LOG_ERROR, 
			"Disk usage limit (%"PRIu64") crossed, current usage is %"PRIu64"",
			priv->disk_usage_limit, priv->current_disk_usage);
		STACK_UNWIND_STRICT (create, frame, -1, ENOSPC, NULL, NULL, NULL,
                                     NULL, NULL);
		return 0;
        }

	STACK_WIND (frame, quota_create_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, fd, params);
	return 0;
}


int
quota_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret, int32_t op_errno, fd_t *fd)
{

	if (op_ret >= 0)
		fd_ctx_set (fd, this, 1);

	STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);
	return 0;
}


int
quota_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            fd_t *fd, int32_t wbflags)
{
	STACK_WIND (frame, quota_open_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->open,
		    loc, flags, fd, wbflags);
	return 0;
}


int
quota_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf)
{
	struct quota_priv *priv = NULL;
	struct quota_local *local = NULL;


	priv = this->private;
	local = frame->local;

	if (priv->disk_usage_limit) {
		if (op_ret >= 0) { 
			gf_quota_usage_add (this, (postbuf->ia_blocks -
						   prebuf->ia_blocks) * 512);
		}
		fd_unref (local->fd);
		iobref_unref (local->iobref);
	}

	STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}


int
quota_writev_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret,	int32_t op_errno, struct iatt *buf)
{
	struct quota_local *local = NULL;
	struct quota_priv  *priv = NULL;
	int                 iovlen = 0;


	local = frame->local;
	priv = this->private;

	if (op_ret >= 0) {
		if (priv->current_disk_usage > priv->disk_usage_limit) {
			iovlen = iov_length (local->vector, local->count);

			if (iovlen > (buf->ia_blksize - (buf->ia_size % buf->ia_blksize))) {
				fd_unref (local->fd);
				iobref_unref (local->iobref);
				STACK_UNWIND_STRICT (writev, frame, -1, ENOSPC,
                                                     NULL, NULL);
				return 0;
			}
		}
		local->stbuf = *buf;
	}
	
	STACK_WIND (frame, quota_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    local->fd, local->vector, local->count, local->offset,
                    local->iobref);

	return 0;
}


int
quota_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
	      struct iovec *vector, int32_t count, off_t off,
              struct iobref *iobref)
{
	struct quota_local *local = NULL;
	struct quota_priv  *priv  = NULL;
        int                 i     = 0;

	priv = this->private;

	if (gf_quota_check_free_disk (this) == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"min-free-disk limit (%u) crossed, current available is %u",
			priv->min_free_disk_limit, priv->current_free_disk);
		STACK_UNWIND_STRICT (writev, frame, -1, ENOSPC,
                                     NULL, NULL);
		return 0;
	}

	if (priv->disk_usage_limit) {
		local = GF_CALLOC (1, sizeof (struct quota_local),
                                   gf_quota_mt_quota_local);
		local->fd     = fd_ref (fd);
		local->iobref = iobref_ref (iobref);
                for (i = 0; i < count; i++) {
                        local->vector[i].iov_base = vector[i].iov_base;
                        local->vector[i].iov_len = vector[i].iov_len;
                }

                local->count = count;
		local->offset = off;
		frame->local  = local;

		STACK_WIND (frame, quota_writev_fstat_cbk,
			    FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->fstat, fd);
		return 0;
	}

	STACK_WIND (frame, quota_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    fd, vector, count, off, iobref);
	return 0;
}


int
quota_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno)
{
	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_CRITICAL, 
			"failed to remove the disk-usage value: %s",
			strerror (op_errno));
	} 
	
	STACK_DESTROY (frame->root);
	return 0;
}


int
quota_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno)
{
        dict_t            *dict = NULL;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_CRITICAL, 
			"failed to set the disk-usage value: %s",
			strerror (op_errno));
	} 

        if (cookie) {
                dict = (dict_t *) cookie;
                dict_unref (dict);
        }

	STACK_DESTROY (frame->root);
	return 0;
}


int
quota_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, struct statvfs *statvfs)
{
	struct quota_priv *priv = NULL;
	uint64_t           f_blocks = 0;
	int64_t            f_bfree = 0;
	uint64_t           f_bused = 0;


	priv = this->private;

	if (op_ret != 0)
		goto unwind;

	f_blocks = priv->disk_usage_limit / statvfs->f_frsize;
	f_bused = priv->current_disk_usage / statvfs->f_frsize;

	if (f_blocks && (f_blocks < statvfs->f_blocks))
		statvfs->f_blocks = f_blocks;

	f_bfree = (statvfs->f_blocks - f_bused);

	if (f_bfree >= 0)
		statvfs->f_bfree = statvfs->f_bavail = f_bfree;
	else
		statvfs->f_bfree = statvfs->f_bavail = 0;

unwind:
	STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, statvfs);
	return 0;
}


int
quota_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	STACK_WIND (frame, quota_statfs_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->statfs, loc);

	return 0;
}


int
quota_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, dict_t *value)
{
	data_t *data = NULL;
	struct quota_priv *priv = this->private;
	
	if (op_ret >= 0) {
		data = dict_get (value, "trusted.glusterfs-quota-du");
		if (data) {
			LOCK (&priv->lock);
			{
				priv->current_disk_usage = data_to_uint64 (data);
			}
			UNLOCK (&priv->lock);

			return 0;
		}
	} 

	STACK_DESTROY (frame->root);

	return 0;
}


void
gf_quota_get_disk_usage (xlator_t *this)
{
	call_frame_t *frame = NULL;
	call_pool_t  *pool = NULL;

	struct quota_priv *priv = NULL;

	pool = this->ctx->pool;
	frame = create_frame (this, pool);
	priv = this->private;

	STACK_WIND (frame, quota_getxattr_cbk,
		    this->children->xlator,
		    this->children->xlator->fops->getxattr,
		    &(priv->root_loc),
		    "trusted.glusterfs-quota-du");
	return ;
}


void
gf_quota_cache_sync (xlator_t *this)
{
	struct quota_priv *priv = NULL;
	call_frame_t      *frame = NULL;
	dict_t            *dict = get_new_dict ();



	priv = this->private;

	frame = create_frame (this, this->ctx->pool);
	dict_set (dict, "trusted.glusterfs-quota-du", 
		  data_from_uint64 (priv->current_disk_usage));

        dict_ref (dict);

	STACK_WIND_COOKIE (frame, quota_setxattr_cbk,
                           (void *) (dict_t *) dict,
                           this->children->xlator,
                           this->children->xlator->fops->setxattr,
                           &(priv->root_loc), dict, 0);
}


int
quota_release (xlator_t *this, fd_t *fd)
{
	gf_quota_cache_sync (this);

	return 0;
}


/* notify */
int32_t
notify (xlator_t *this,
	int32_t event,
	void *data,
	...)
{
	default_notify (this, event, data);
	return 0;
}

int32_t
quota_lookup_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct iatt *buf,
                    dict_t *dict,
                    struct iatt *postparent)
{
	STACK_UNWIND_STRICT (
                     lookup, 
                     frame,
		      op_ret,
		      op_errno,
		      inode,
		      buf,
                      dict,
                      postparent);
	return 0;
}

int32_t
quota_lookup (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		dict_t *xattr_req)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if (priv->only_first_time) {
		if (strcmp (loc->path, "/") == 0) {
			loc_copy(&(priv->root_loc), loc); 
			priv->only_first_time = 0;
			if (priv->disk_usage_limit)
				gf_quota_get_disk_usage (this);
		}
	}

	STACK_WIND (frame,
		    quota_lookup_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lookup,
		    loc,
		    xattr_req);
	return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_quota_mt_end + 1);
        
        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        "failed");
                return ret;
        }

        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{

	struct quota_priv *_private = NULL;
	uint64_t	   disk_usage_limit;	
	uint32_t   	   min_free_disk_limit;
	data_t 		  *data = NULL;
	int		   ret = 0;
	
	_private = this->private;

	data = dict_get (options, "disk-usage-limit");
	if (data) {
		if (gf_string2bytesize (data->data, &disk_usage_limit) != 0) {
                        gf_log (this->name, GF_LOG_ERROR, 
                                "Reconfigure: Invalid number '%s' "
				"for disk-usage limit", data->data);
			//return -1;
			ret = -1;
			goto out;
                }
		_private->disk_usage_limit = disk_usage_limit;
		gf_log (this->name, GF_LOG_TRACE,
                        "Reconfiguring disk-usage-limit %"PRIu64"",
			disk_usage_limit);

	}
	
    
        data = dict_get (options, "min-free-disk-limit");
        if (data) {
		if (gf_string2percent (data->data, &min_free_disk_limit) != 0){
                        gf_log (this->name, GF_LOG_ERROR, 
                                "Reconfigure : Invalid percent '%s'  for"
				" min-free-disk-limit", data->data);
			ret = -1;
			goto out;
		}

		_private->min_free_disk_limit = min_free_disk_limit;
		gf_log (this->name, GF_LOG_TRACE,
                        "Reconfiguring min-free-disk-limit to %d percent",
			min_free_disk_limit);
		
        }
out:	
	return ret;

}

int32_t 
init (xlator_t *this)
{
	int     ret  = 0;
        data_t *data = NULL;
	struct quota_priv *_private = NULL;

	if (!this->children || this->children->next) {
		gf_log (this->name, GF_LOG_ERROR, 
			"FATAL: quota should have exactly one child");
		return -1;
	}
	
	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

	_private = GF_CALLOC (1, sizeof (struct quota_priv),
                              gf_quota_mt_quota_priv);
        _private->disk_usage_limit = 0;
        data = dict_get (this->options, "disk-usage-limit");
        if (data) {
		if (gf_string2bytesize (data->data, &_private->disk_usage_limit) != 0) {
                        gf_log (this->name, GF_LOG_ERROR, 
                                "invalid number '%s' for disk-usage limit", data->data);
			ret = -1;
			goto out;
                }

		LOCK_INIT (&_private->lock);
		_private->current_disk_usage = 0;
	}
	
        _private->min_free_disk_limit = 0;
        data = dict_get (this->options, "min-free-disk-limit");
        if (data) {
		if (gf_string2percent (data->data, &_private->min_free_disk_limit) != 0) {
                        gf_log (this->name, GF_LOG_ERROR, 
                                "invalid percent '%s' for min-free-disk limit", data->data);
			ret = -1;
			goto out;
                }
		_private->refresh_interval = 20; /* 20seconds is default */
		data = dict_get (this->options, "refresh-interval");
		if (data) {
			if (gf_string2time (data->data, 
					    &_private->refresh_interval)!= 0) {
				gf_log (this->name, GF_LOG_ERROR, 
					"invalid time '%s' for refresh "
					"interval", data->data);
				ret = -1;
				goto out;
			}
		}
        }

	_private->only_first_time = 1;
        this->private = (void *)_private;
	ret = 0;
 out:
	return ret;
}



void 
fini (xlator_t *this)
{
	struct quota_priv *_private = this->private;

	if (_private) {
		gf_quota_cache_sync (this);
		this->private = NULL;
	}
	
	return ;
}

struct xlator_fops fops = {
	.create      = quota_create,
	.open        = quota_open,
	.lookup	     = quota_lookup,
	.truncate    = quota_truncate,
	.ftruncate   = quota_ftruncate,
	.writev      = quota_writev,
	.unlink      = quota_unlink,
	.rmdir       = quota_rmdir,
	.mknod       = quota_mknod,
	.mkdir       = quota_mkdir,
	.symlink     = quota_symlink,
	.statfs      = quota_statfs,
};

struct xlator_cbks cbks = {
	.release     = quota_release
};

struct volume_options options[] = {
	{ .key  = {"min-free-disk-limit"}, 
	  .type = GF_OPTION_TYPE_PERCENT
	},
	{ .key  = {"refresh-interval"}, 
	  .type = GF_OPTION_TYPE_TIME
	},
	{ .key  = {"disk-usage-limit"}, 
	  .type = GF_OPTION_TYPE_SIZET 
	},
	{ .key = {NULL} },
};
