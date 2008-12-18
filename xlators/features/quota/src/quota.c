/*
  Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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
	struct stat stbuf;
	inode_t    *inode;
	char       *path;
	fd_t       *fd;
	off_t      offset;
	int32_t    count;
	struct iovec *vector;
	dict_t     *refs;
};

struct quota_priv {
	char only_first_time;                /* Used to make sure a call is done only one time */
	gf_lock_t  lock;                     /* Used while updating variables */

	uint64_t disk_usage_limit;         /* Used for Disk usage quota */
	uint64_t current_disk_usage;       /* Keep the current usage value */

	uint32_t min_free_disk_limit;        /* user specified limit, in % */ 
	uint32_t current_free_disk;          /* current free disk space available, in % */
	uint32_t refresh_interval;      /* interval in seconds */
	uint32_t min_disk_last_updated_time; /* used for interval calculation */	
};

int32_t
quota_statvfs_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct statvfs *stbuf)
{
	struct quota_priv *priv = this->private;
	
	if (op_ret >= 0) {
		priv->current_free_disk = (stbuf->f_bavail * 100) / stbuf->f_blocks;
	}

	STACK_DESTROY (frame->root);
	return 0;
}
void 
gf_quota_update_current_free_disk (xlator_t *this)
{
	call_frame_t *frame = NULL;
	call_pool_t *pool = this->ctx->pool;

	frame = create_frame (this, pool);
  
	{
		loc_t tmp_loc = {
			.inode = NULL,
			.path = "/",
		};
		STACK_WIND (frame,
			    quota_statvfs_cbk,
			    this->children->xlator,
			    this->children->xlator->fops->statfs,
			    &tmp_loc);
	}

	return ;
}

static int
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


static int32_t
quota_truncate_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
	struct quota_priv *priv = this->private;
	if ((op_ret >= 0) && priv->disk_usage_limit) {
		LOCK (&priv->lock);
		{
			priv->current_disk_usage -= (buf->st_blocks * 512);
		}
		UNLOCK (&priv->lock);
	}
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t
quota_truncate (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		off_t offset)
{
	STACK_WIND (frame,
		    quota_truncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate,
		    loc, offset);
	return 0;
}

static int32_t
quota_ftruncate_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
	struct quota_priv *priv = this->private;
	if ((op_ret >= 0) && priv->disk_usage_limit) {
		LOCK (&priv->lock);
		{
			priv->current_disk_usage -= (buf->st_blocks * 512);
		}
		UNLOCK (&priv->lock);
	}
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t
quota_ftruncate (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 off_t offset)
{
	STACK_WIND (frame,
		    quota_ftruncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->ftruncate,
		    fd, offset);
	return 0;
}



static int32_t
quota_mknod_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
	struct quota_priv *priv = this->private;
	if ((op_ret >= 0) && priv->disk_usage_limit) {
		LOCK (&priv->lock);
		{
			priv->current_disk_usage += (buf->st_blocks * 512);
		}
		UNLOCK (&priv->lock);
	}

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}

int32_t
quota_mknod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode,
	     dev_t rdev)
{
	struct quota_priv *priv = this->private;
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

	STACK_WIND (frame,
		    quota_mknod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mknod,
		    loc, mode, rdev);
	return 0;
}

static int32_t
quota_mkdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
	struct quota_priv *priv = this->private;
	if ((op_ret >= 0) && priv->disk_usage_limit) {
		LOCK (&priv->lock);
		{
			priv->current_disk_usage += (buf->st_blocks * 512);
		}
		UNLOCK (&priv->lock);
	}

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}

int32_t
quota_mkdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
	struct quota_priv *priv = this->private;
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

	STACK_WIND (frame,
		    quota_mkdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mkdir,
		    loc, mode);

	return 0;
}

static int32_t
quota_unlink_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
	struct quota_local *local = frame->local;
	struct quota_priv *priv = this->private;

	if (local) {
		if (op_ret >= 0) {
			LOCK (&priv->lock);
			{
				priv->current_disk_usage -= (local->stbuf.st_blocks * 512);
			}
			UNLOCK (&priv->lock);
		}
		FREE (local->path);
		inode_unref (local->inode);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t 
quota_unlink_stat_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct stat *buf)
{
	loc_t tmp_loc;
	struct quota_local *local = frame->local;
	if (op_ret >= 0) {
		if (buf->st_nlink == 1) {
			local->stbuf = *buf;
		}
	}
	
	tmp_loc.path = local->path;
	tmp_loc.inode = local->inode;

	STACK_WIND (frame,
		    quota_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    &tmp_loc);

	return 0;
}

int32_t
quota_unlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
	struct quota_local *local = NULL;
	struct quota_priv *priv = this->private;

	if (priv->disk_usage_limit) {
		local = CALLOC (1, sizeof (struct quota_local));
		local->path  = strdup (loc->path);
		local->inode = inode_ref (loc->inode);
		frame->local = local;
		STACK_WIND (frame,
			    quota_unlink_stat_cbk,
			    FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->stat,
			    loc);
		return 0;
	}

	STACK_WIND (frame,
		    quota_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    loc);
	return 0;
}

static int32_t
quota_rmdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	struct quota_local *local = frame->local;
	struct quota_priv *priv = this->private;

	if (local) {
		if (op_ret >= 0) {
			LOCK (&priv->lock);
			{
				priv->current_disk_usage -= (local->stbuf.st_blocks * 512);
			}
			UNLOCK (&priv->lock);
		}
		FREE (local->path);
		inode_unref (local->inode);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t 
quota_rmdir_stat_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf)
{
	loc_t tmp_loc;
	struct quota_local *local = frame->local;
	if (op_ret >= 0) {
		local->stbuf = *buf;
	}

	tmp_loc.path = local->path;
	tmp_loc.inode = local->inode;

	STACK_WIND (frame,
		    quota_rmdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    &tmp_loc);

	return 0;
}

int32_t
quota_rmdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
	struct quota_local *local = NULL;
	struct quota_priv *priv = this->private;

	if (priv->disk_usage_limit) {
		local = CALLOC (1, sizeof (struct quota_local));
		local->path  = strdup (loc->path);
		local->inode = inode_ref (loc->inode);
		frame->local = local;
		STACK_WIND (frame,
			    quota_rmdir_stat_cbk,
			    FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->stat,
			    loc);
		return 0;
	}

	STACK_WIND (frame,
		    quota_rmdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    loc);
	return 0;
}

static int32_t
quota_symlink_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *buf)
{
	struct quota_priv *priv = this->private;
	if ((op_ret >= 0) && priv->disk_usage_limit) {
		LOCK (&priv->lock);
		{
			priv->current_disk_usage += (buf->st_blocks * 512);
		}
		UNLOCK (&priv->lock);
	}

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}

int32_t
quota_symlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *linkpath,
	       loc_t *loc)
{
	struct quota_priv *priv = this->private;
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

	STACK_WIND (frame,
		    quota_symlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->symlink,
		    linkpath, loc);
	return 0;
}


static int32_t
quota_create_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  fd_t *fd,
		  inode_t *inode,
		  struct stat *buf)
{
	struct quota_priv *priv = this->private;
	if ((op_ret >= 0) && priv->disk_usage_limit) {
		LOCK (&priv->lock);
		{
			priv->current_disk_usage += (buf->st_blocks * 512);
		}
		UNLOCK (&priv->lock);
	}

	STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);
	return 0;
}

int32_t
quota_create (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flags,
	      mode_t mode, fd_t *fd)
{
	struct quota_priv *priv = this->private;
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


static int32_t
quota_writev_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
	struct quota_priv *priv = this->private;
	struct quota_local *local = frame->local;

	if (priv->disk_usage_limit) {
		if (op_ret >= 0) { 
			LOCK (&priv->lock);
			{
				priv->current_disk_usage += ((stbuf->st_blocks - local->stbuf.st_blocks) * 512);
			}
			UNLOCK (&priv->lock);
		}
		fd_unref (local->fd);
		dict_unref (local->refs);
	}

	STACK_UNWIND (frame, op_ret, op_errno, stbuf);
	return 0;
}

int32_t 
quota_writev_fstat_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct stat *buf)
{
	struct quota_local *local = frame->local;
	struct quota_priv *priv = this->private;
	int iovlen = 0;
	int idx = 0;

	if (op_ret >= 0) {
		if (priv->current_disk_usage > priv->disk_usage_limit) {
			for (idx = 0; idx < local->count; idx++) {
				iovlen += local->vector[idx].iov_len;
			}
			if (iovlen > (buf->st_blksize - (buf->st_size % buf->st_blksize))) {
				fd_unref (local->fd);
				dict_unref (local->refs);
				STACK_UNWIND (frame, -1, ENOSPC, NULL);
				return 0;
			}
		}

		local->stbuf = *buf;
	}
	
	STACK_WIND (frame,
		    quota_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    local->fd, local->vector, local->count, local->offset);

	return 0;
}

int32_t
quota_writev (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      struct iovec *vector,
	      int32_t count,
	      off_t off)
{
	struct quota_local *local = NULL;
	struct quota_priv *priv = this->private;

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
		local->refs   = dict_ref (frame->root->req_refs);
		local->vector = vector;
		local->count  = count;
		local->offset = off;
		frame->local  = local;
		STACK_WIND (frame,
			    quota_writev_fstat_cbk,
			    FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->fstat,
			    fd);
		return 0;
	}

	STACK_WIND (frame,
		    quota_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    fd, vector, count, off);
	return 0;
}


int32_t
quota_removexattr_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_CRITICAL, 
			"failed to remove the disk-usage value: %s",
			strerror (op_errno));
	} 
	
	STACK_DESTROY (frame->root);
	return 0;
}

int32_t
quota_setxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_CRITICAL, 
			"failed to set the disk-usage value: %s",
			strerror (op_errno));
	} 

	STACK_DESTROY (frame->root);
	return 0;
}

int32_t
quota_getxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dict_t *value)
{
	data_t *data = NULL;
	struct quota_priv *priv = this->private;
	loc_t tmp_loc = {
		.inode = NULL,
		.path = "/",
	};
	
	if (op_ret >= 0) {
		data = dict_get (value, "trusted.glusterfs-quota-du");
		if (data) {
			LOCK (&priv->lock);
			{
				priv->current_disk_usage = data_to_uint64 (data);
			}
			UNLOCK (&priv->lock);
			STACK_WIND (frame, 
				    quota_removexattr_cbk,
				    this->children->xlator,
				    this->children->xlator->fops->removexattr,
				    &tmp_loc,
				    "trusted.glusterfs-quota-du");
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
	call_pool_t *pool = this->ctx->pool;

	frame = create_frame (this, pool);
	{
		loc_t tmp_loc = {
			.inode = NULL,
			.path = "/",
		};

		STACK_WIND (frame,
			    quota_getxattr_cbk,
			    this->children->xlator,
			    this->children->xlator->fops->getxattr,
			    &tmp_loc,
			    "trusted.glusterfs-quota-du");
	}

	return ;
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
	call_frame_t *frame = NULL;
	struct quota_priv *_private = this->private;

	this->private = NULL;

	if (_private) {
		dict_t *dict = get_new_dict ();
		loc_t tmp_loc = {
			.inode = NULL,
			.path = "/",
		};
		frame = create_frame (this, this->ctx->pool);
		dict_set (dict, "trusted.glusterfs-quota-du", 
			  data_from_uint64 (_private->current_disk_usage));

		STACK_WIND (frame,
			    quota_setxattr_cbk,
			    this->children->xlator,
			    this->children->xlator->fops->setxattr,
			    &tmp_loc,
			    dict,
			    0);
		
		FREE (_private);
	}
	
	return ;
}

struct xlator_fops fops = {
	.create      = quota_create,
	.truncate    = quota_truncate,
	.ftruncate   = quota_ftruncate,
	.writev      = quota_writev,
	.unlink      = quota_unlink,
	.rmdir       = quota_rmdir,
	.mknod       = quota_mknod,
	.mkdir       = quota_mkdir,
	.symlink     = quota_symlink,
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
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
