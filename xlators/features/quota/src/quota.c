/*
  Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
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

struct quota_local {
	struct stat    stbuf;
	inode_t       *inode;
	char          *path;
	fd_t          *fd;
	off_t          offset;
	int32_t        count;
	struct iovec  *vector;
	struct iobref *iobref;
	loc_t          loc;
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


static void
build_root_loc (xlator_t *this, loc_t *loc)
{
        memset (loc, 0, sizeof (*loc));
	loc->path = "/";
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
	loc_t          loc;

	pool  = this->ctx->pool;
	frame = create_frame (this, pool);
  
	build_root_loc (this, &loc);

	STACK_WIND (frame, quota_statvfs_cbk,
		    this->children->xlator,
		    this->children->xlator->fops->statfs, &loc);

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
		    int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	struct quota_priv *priv = this->private;
	struct quota_local *local = NULL;

	local = frame->local;

	if ((op_ret >= 0) && priv->disk_usage_limit) {
		gf_quota_usage_subtract (this, (local->stbuf.st_blocks -
						buf->st_blocks) * 512);
		loc_wipe (&local->loc);
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}


int
quota_truncate_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			 int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	struct quota_local *local = NULL;
	struct quota_priv  *priv = NULL;

	priv = this->private;
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
		local = CALLOC (1, sizeof (struct quota_local));
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
		     int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	struct quota_priv  *priv = NULL;
	struct quota_local *local = NULL;

	local = frame->local;
	priv = this->private;

	if ((op_ret >= 0) && priv->disk_usage_limit) {
		gf_quota_usage_subtract (this, (local->stbuf.st_blocks -
						buf->st_blocks) * 512);
		fd_unref (local->fd);
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}


int
quota_ftruncate_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	struct quota_local *local = NULL;
	struct quota_priv  *priv = NULL;

	priv = this->private;
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
		local = CALLOC (1, sizeof (struct quota_local));
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
		 inode_t *inode, struct stat *buf)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if ((op_ret >= 0) && priv->disk_usage_limit) {
		gf_quota_usage_add (this, buf->st_blocks * 512);
	}

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}


int
quota_mknod (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, mode_t mode, dev_t rdev)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if (gf_quota_check_free_disk (this) == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"min-free-disk limit (%u) crossed, current available is %u",
			priv->min_free_disk_limit, priv->current_free_disk);
		STACK_UNWIND (frame, -1, ENOSPC, NULL, NULL);
		return 0;
	}

        if (priv->current_disk_usage > priv->disk_usage_limit) {
		gf_log (this->name, GF_LOG_ERROR, 
			"Disk usage limit (%"PRIu64") crossed, current usage is %"PRIu64"",
			priv->disk_usage_limit, priv->current_disk_usage);
		STACK_UNWIND (frame, -1, ENOSPC, NULL, NULL);
		return 0;
        }

	STACK_WIND (frame, quota_mknod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mknod,
		    loc, mode, rdev);
	return 0;
}


int
quota_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int32_t op_ret, int32_t op_errno, inode_t *inode,
		 struct stat *buf)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if ((op_ret >= 0) && priv->disk_usage_limit) {
		gf_quota_usage_subtract (this, buf->st_blocks * 512);
	}

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}


int
quota_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if (gf_quota_check_free_disk (this) == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"min-free-disk limit (%u) crossed, current available is %u",
			priv->min_free_disk_limit, priv->current_free_disk);
		STACK_UNWIND (frame, -1, ENOSPC, NULL, NULL);
		return 0;
		
	}

        if (priv->current_disk_usage > priv->disk_usage_limit) {
		gf_log (this->name, GF_LOG_ERROR, 
			"Disk usage limit (%"PRIu64") crossed, current usage is %"PRIu64"",
			priv->disk_usage_limit, priv->current_disk_usage);
		STACK_UNWIND (frame, -1, ENOSPC, NULL, NULL);
		return 0;
        }

	STACK_WIND (frame, quota_mkdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mkdir,
		    loc, mode);

	return 0;
}


int
quota_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno)
{
	struct quota_local *local = NULL;

	local = frame->local;

	if (local) {
		if (op_ret >= 0) {
			gf_quota_usage_subtract (this,
						 local->stbuf.st_blocks * 512);
		}
		loc_wipe (&local->loc);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int
quota_unlink_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	struct quota_local *local = NULL;

	local = frame->local;

	if (op_ret >= 0) {
		if (buf->st_nlink == 1) {
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
		local = CALLOC (1, sizeof (struct quota_local));
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
		 int32_t op_ret, int32_t op_errno)
{
	struct quota_local *local = NULL;

	local = frame->local;

	if (local) {
		if (op_ret >= 0) {
			gf_quota_usage_subtract (this, local->stbuf.st_blocks * 512);
		}
		loc_wipe (&local->loc);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int
quota_rmdir_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	struct quota_local *local = NULL;

	local = frame->local;

	if (op_ret >= 0) {
		local->stbuf = *buf;
	}

	STACK_WIND (frame, quota_rmdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    &local->loc);

	return 0;
}


int
quota_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	struct quota_local *local = NULL;
	struct quota_priv  *priv = NULL;

	priv = this->private;

	if (priv->disk_usage_limit) {
		local = CALLOC (1, sizeof (struct quota_local));
		frame->local = local;

		loc_copy (&local->loc, loc);

		STACK_WIND (frame, quota_rmdir_stat_cbk,
			    FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->stat, loc);
		return 0;
	}

	STACK_WIND (frame, quota_rmdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    loc);
	return 0;
}


int
quota_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		   int32_t op_ret, int32_t op_errno, inode_t *inode,
		   struct stat *buf)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if ((op_ret >= 0) && priv->disk_usage_limit) {
		gf_quota_usage_add (this, buf->st_blocks * 512);
	}

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}


int
quota_symlink (call_frame_t *frame, xlator_t *this,
	       const char *linkpath, loc_t *loc)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if (gf_quota_check_free_disk (this) == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"min-free-disk limit (%u) crossed, current available is %u",
			priv->min_free_disk_limit, priv->current_free_disk);
		STACK_UNWIND (frame, -1, ENOSPC, NULL, NULL);
		return 0;
		
	}
        if (priv->current_disk_usage > priv->disk_usage_limit) {
		gf_log (this->name, GF_LOG_ERROR, 
			"Disk usage limit (%"PRIu64") crossed, current usage is %"PRIu64"",
			priv->disk_usage_limit, priv->current_disk_usage);
		STACK_UNWIND (frame, -1, ENOSPC, NULL, NULL);
		return 0;
        }

	STACK_WIND (frame, quota_symlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->symlink,
		    linkpath, loc);
	return 0;
}


int
quota_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno,
		  fd_t *fd, inode_t *inode, struct stat *buf)
{
	struct quota_priv *priv = this->private;
	int                ret = 0;

	if ((op_ret >= 0) && priv->disk_usage_limit) {
		gf_quota_usage_add (this, buf->st_blocks * 512);

		ret = fd_ctx_set (fd, this, 1);
	}

	STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);
	return 0;
}


int
quota_create (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, int32_t flags, mode_t mode, fd_t *fd)
{
	struct quota_priv *priv = NULL;

	priv = this->private;

	if (gf_quota_check_free_disk (this) == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"min-free-disk limit (%u) crossed, current available is %u",
			priv->min_free_disk_limit, priv->current_free_disk);
		STACK_UNWIND (frame, -1, ENOSPC, NULL, NULL, NULL);
		return 0;
		
	}
        if (priv->current_disk_usage > priv->disk_usage_limit) {
		gf_log (this->name, GF_LOG_ERROR, 
			"Disk usage limit (%"PRIu64") crossed, current usage is %"PRIu64"",
			priv->disk_usage_limit, priv->current_disk_usage);
		STACK_UNWIND (frame, -1, ENOSPC, NULL, NULL, NULL);
		return 0;
        }

	STACK_WIND (frame, quota_create_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, fd);
	return 0;
}


int
quota_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret, int32_t op_errno, fd_t *fd)
{
	int                ret = 0;

	if (op_ret >= 0)
		ret = fd_ctx_set (fd, this, 1);

	STACK_UNWIND (frame, op_ret, op_errno, fd);
	return 0;
}


int
quota_open (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, int32_t flags, fd_t *fd)
{
	STACK_WIND (frame, quota_open_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->open,
		    loc, flags, fd);
	return 0;
}


int
quota_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, struct stat *stbuf)
{
	struct quota_priv *priv = NULL;
	struct quota_local *local = NULL;


	priv = this->private;
	local = frame->local;

	if (priv->disk_usage_limit) {
		if (op_ret >= 0) { 
			gf_quota_usage_add (this, (stbuf->st_blocks -
						   local->stbuf.st_blocks) * 512);
		}
		fd_unref (local->fd);
		iobref_unref (local->iobref);
	}

	STACK_UNWIND (frame, op_ret, op_errno, stbuf);
	return 0;
}


int
quota_writev_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret,	int32_t op_errno, struct stat *buf)
{
	struct quota_local *local = NULL;
	struct quota_priv  *priv = NULL;
	int                 iovlen = 0;


	local = frame->local;
	priv = this->private;

	if (op_ret >= 0) {
		if (priv->current_disk_usage > priv->disk_usage_limit) {
			iovlen = iov_length (local->vector, local->count);

			if (iovlen > (buf->st_blksize - (buf->st_size % buf->st_blksize))) {
				fd_unref (local->fd);
				iobref_unref (local->iobref);
				STACK_UNWIND (frame, -1, ENOSPC, NULL);
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
	struct quota_priv  *priv = NULL;

	priv = this->private;

	if (gf_quota_check_free_disk (this) == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"min-free-disk limit (%u) crossed, current available is %u",
			priv->min_free_disk_limit, priv->current_free_disk);
		STACK_UNWIND (frame, -1, ENOSPC, NULL);
		return 0;
	}

	if (priv->disk_usage_limit) {
		local = CALLOC (1, sizeof (struct quota_local));
		local->fd     = fd_ref (fd);
		local->iobref = iobref_ref (iobref);
		local->vector = vector;
		local->count  = count;
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
	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_CRITICAL, 
			"failed to set the disk-usage value: %s",
			strerror (op_errno));
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
	STACK_UNWIND (frame, op_ret, op_errno, statvfs);
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
	loc_t         loc;

	pool = this->ctx->pool;
	frame = create_frame (this, pool);
	build_root_loc (this, &loc);

	STACK_WIND (frame, quota_getxattr_cbk,
		    this->children->xlator,
		    this->children->xlator->fops->getxattr,
		    &loc,
		    "trusted.glusterfs-quota-du");
	return ;
}


void
gf_quota_cache_sync (xlator_t *this)
{
	struct quota_priv *priv = NULL;
	call_frame_t      *frame = NULL;
	dict_t            *dict = get_new_dict ();
	loc_t              loc;


	priv = this->private;
	build_root_loc (this, &loc);

	frame = create_frame (this, this->ctx->pool);
	dict_set (dict, "trusted.glusterfs-quota-du", 
		  data_from_uint64 (priv->current_disk_usage));

	STACK_WIND (frame, quota_setxattr_cbk,
		    this->children->xlator,
		    this->children->xlator->fops->setxattr,
		    &loc, dict, 0);
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
	struct quota_priv *priv = this->private;
	
	switch (event)
	{
	case GF_EVENT_CHILD_UP:
		if (priv->only_first_time) {
			priv->only_first_time = 0;
			if (priv->disk_usage_limit) {
				gf_quota_get_disk_usage (this);
			}
		}
	default:
		default_notify (this, event, data);
		break;
	}

	return 0;
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

	_private = CALLOC (1, sizeof (struct quota_priv));
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

struct xlator_mops mops = {
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
