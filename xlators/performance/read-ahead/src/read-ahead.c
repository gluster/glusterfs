/*
  Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

/* 
   TODO:
   - handle O_DIRECT
   - maintain offset, flush on lseek
   - ensure efficient memory managment in case of random seek
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "read-ahead.h"
#include <assert.h>
#include <sys/time.h>


static void
read_ahead (call_frame_t *frame,
            ra_file_t *file);


int
ra_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, fd_t *fd)
{
	ra_conf_t  *conf = NULL;
	ra_file_t  *file = NULL;
	int         ret = 0;

	conf  = this->private;

	if (op_ret == -1) {
		goto unwind;
	}

	file = calloc (1, sizeof (*file));
	if (!file) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto unwind;
	}

	ret = dict_set_static_ptr (fd->ctx, this->name, file);

	/* If mandatory locking has been enabled on this file,
	   we disable caching on it */

	if ((fd->inode->st_mode & S_ISGID) && !(fd->inode->st_mode & S_IXGRP))
		file->disabled = 1;

	/* If O_DIRECT open, we disable caching on it */

	if ((fd->flags & O_DIRECT) || (fd->flags & O_WRONLY))
		file->disabled = 1;

	file->offset = (unsigned long long) 0;
	file->conf = conf;
	file->pages.next = &file->pages;
	file->pages.prev = &file->pages;
	file->pages.offset = (unsigned long long) 0;
	file->pages.file = file;

	ra_conf_lock (conf);
	{
		file->next = conf->files.next;
		conf->files.next = file;
		file->next->prev = file;
		file->prev = &conf->files;
	}
	ra_conf_unlock (conf);

	file->fd = fd;
	file->page_count = conf->page_count;
	file->page_size = conf->page_size;
	pthread_mutex_init (&file->file_lock, NULL);

	if (!file->disabled) {
		file->page_count = 1;
	}

unwind:
	STACK_UNWIND (frame, op_ret, op_errno, fd);

	return 0;
}


int
ra_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
	       fd_t *fd, inode_t *inode, struct stat *buf)
{
	ra_conf_t  *conf = NULL;
	ra_file_t  *file = NULL;
	int         ret = 0;

	conf  = this->private;

	if (op_ret != -1) {
		goto unwind;
	}

	file = calloc (1, sizeof (*file));
	if (!file) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto unwind;
	}

	ret = dict_set_static_ptr (fd->ctx, this->name, file);

	/* If mandatory locking has been enabled on this file,
	   we disable caching on it */

	if ((fd->inode->st_mode & S_ISGID) && !(fd->inode->st_mode & S_IXGRP))
		file->disabled = 1;

	/* If O_DIRECT open, we disable caching on it */

	if ((fd->flags & O_DIRECT) || (fd->flags & O_WRONLY))
			file->disabled = 1;

	file->offset = (unsigned long long) 0;
	//file->size = fd->inode->buf.st_size;
	file->conf = conf;
	file->pages.next = &file->pages;
	file->pages.prev = &file->pages;
	file->pages.offset = (unsigned long long) 0;
	file->pages.file = file;

	ra_conf_lock (conf);
	{
		file->next = conf->files.next;
		conf->files.next = file;
		file->next->prev = file;
		file->prev = &conf->files;
	}
	ra_conf_unlock (conf);

	file->fd = fd;
	file->page_count = conf->page_count;
	file->page_size = conf->page_size;
	pthread_mutex_init (&file->file_lock, NULL);

unwind:
	STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);

	return 0;
}


int
ra_open (call_frame_t *frame, xlator_t *this,
         loc_t *loc, int32_t flags, fd_t *fd)
{
	STACK_WIND (frame, ra_open_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->open,
		    loc, flags, fd);

	return 0;
}

int
ra_create (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, int32_t flags, mode_t mode, fd_t *fd)
{
	STACK_WIND (frame,
		    ra_create_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, fd);

	return 0;
}

/* free cache pages between offset and offset+size,
   does not touch pages with frames waiting on it
*/

static void
flush_region (call_frame_t *frame,
              ra_file_t *file,
              off_t offset,
              off_t size)
{
	ra_page_t *trav = NULL;
	ra_page_t *next = NULL;


	ra_file_lock (file);
	{
		trav = file->pages.next;
		while (trav != &file->pages
		       && trav->offset < (offset + size)) {

			next = trav->next;
			if (trav->offset >= offset && !trav->waitq) {
				ra_page_purge (trav);
			}
			trav = next;
		}
	}
	ra_file_unlock (file);
}



int
ra_release (xlator_t *this,
	    fd_t *fd)
{
	ra_file_t *file = NULL;
	int        ret = 0;

	ret = dict_get_ptr (fd->ctx, this->name, (void **) ((void *)&file));

	if (file) {
		ra_file_destroy (file);
	}

	return 0;
}


void
read_ahead (call_frame_t *frame, ra_file_t *file)
{
	off_t      ra_offset = 0;
	size_t     ra_size = 0;
	off_t      trav_offset = 0;
	ra_page_t *trav = NULL;
	off_t      cap = 0;
	char       fault = 0;

	if (!file->page_count)
		return;

	ra_size   = file->page_size * file->page_count;
	ra_offset = floor (file->offset, file->page_size);
	cap       = file->size ? file->size : file->offset + ra_size;

	while (ra_offset < min (file->offset + ra_size, cap)) {

		ra_file_lock (file);
		{
			trav = ra_page_get (file, ra_offset);
		}
		ra_file_unlock (file);

		if (!trav)
			break;

		ra_offset += file->page_size;
	}

	if (trav)
		/* comfortable enough */
		return;

	trav_offset = ra_offset;

	trav = file->pages.next;
	cap  = file->size ? file->size : ra_offset + ra_size;

	while (trav_offset < min(ra_offset + ra_size, cap)) {
		fault = 0;
		ra_file_lock (file);
		{
			trav = ra_page_get (file, trav_offset);
			if (!trav) {
				fault = 1;
				trav = ra_page_create (file, trav_offset);
				if (trav) 
					trav->dirty = 1;
			}
		}
		ra_file_unlock (file);

		if (!trav) {
			/* OUT OF MEMORY */
			break;
		}

		if (fault) {
			gf_log (frame->this->name, GF_LOG_DEBUG,
				"RA at offset=%"PRId64, trav_offset);
			ra_page_fault (file, frame, trav_offset);
		}
		trav_offset += file->page_size;
	}

	return;
}


int
ra_need_atime_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iovec *vector,
                   int32_t count, struct stat *stbuf)
{
	STACK_DESTROY (frame->root);
	return 0;
}


static void
dispatch_requests (call_frame_t *frame,
                   ra_file_t *file)
{
	ra_local_t   *local = NULL;
	ra_conf_t    *conf = NULL;
	off_t         rounded_offset = 0;
	off_t         rounded_end = 0;
	off_t         trav_offset = 0;
	ra_page_t    *trav = NULL;
	call_frame_t *ra_frame = NULL;
	char          need_atime_update = 1;
	char          fault = 0;


	local = frame->local;
	conf  = file->conf;

	rounded_offset = floor (local->offset, file->page_size);
	rounded_end    = roof (local->offset + local->size, file->page_size);

	trav_offset = rounded_offset;
	trav        = file->pages.next;

	while (trav_offset < rounded_end) {
		fault = 0;

		ra_file_lock (file);
		{
			trav = ra_page_get (file, trav_offset);
			if (!trav) {
				trav = ra_page_create (file, trav_offset);
				fault = 1;
				need_atime_update = 0;
			}

			if (!trav)
				goto unlock;

			if (trav->ready) {
				gf_log (frame->this->name, GF_LOG_DEBUG,
					"HIT at offset=%"PRId64".",
					trav_offset);
				ra_frame_fill (trav, frame);
			} else {
				gf_log (frame->this->name, GF_LOG_DEBUG,
					"IN-TRANSIT at offset=%"PRId64".",
					trav_offset);
				ra_wait_on_page (trav, frame);
				need_atime_update = 0;
			}
		}
	unlock:
		ra_file_unlock (file);

		if (fault) {
			gf_log (frame->this->name, GF_LOG_DEBUG,
				"MISS at offset=%"PRId64".",
				trav_offset);
			ra_page_fault (file, frame, trav_offset);
		}

		trav_offset += file->page_size;
	}

	if (need_atime_update && conf->force_atime_update) {
		/* TODO: use untimens() since readv() can confuse underlying
		   io-cache and others */
		ra_frame = copy_frame (frame);
		STACK_WIND (ra_frame, ra_need_atime_cbk,
			    FIRST_CHILD (frame->this), 
			    FIRST_CHILD (frame->this)->fops->readv,
			    file->fd, 1, 1);
	}

	return ;
}


int
ra_readv_disabled_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
		       struct iovec *vector, int32_t count, struct stat *stbuf)
{
	STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf);

	return 0;
}


int
ra_readv (call_frame_t *frame, xlator_t *this,
	  fd_t *fd, size_t size, off_t offset)
{
	ra_file_t    *file = NULL;
	ra_local_t   *local = NULL;
	ra_conf_t    *conf = NULL;
	int           op_errno = 0;
	int           ret = 0;

	conf = this->private;

	gf_log (this->name, GF_LOG_DEBUG,
		"NEW REQ at offset=%"PRId64" for size=%"GF_PRI_SIZET"",
		offset, size);

	ret = dict_get_ptr (fd->ctx, this->name, (void **) ((void *)&file));

	if (file->offset != offset) {
		gf_log (this->name, GF_LOG_DEBUG,
			"unexpected offset (%"PRId64" != %"PRId64") resetting",
			file->offset, offset);

		file->expected = file->page_count = 0;
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"expected offset (%"PRId64") when page_count=%d",
			offset, file->page_count);

		if (file->expected < (conf->page_size * conf->page_count)) {
			file->expected += size;
			file->page_count = min ((file->expected / file->page_size),
						conf->page_count);
		}
	}

	if (file->disabled) {
		STACK_WIND (frame, ra_readv_disabled_cbk,
			    FIRST_CHILD (frame->this), 
			    FIRST_CHILD (frame->this)->fops->readv,
			    file->fd, size, offset);
		return 0;
	}

	local = (void *) calloc (1, sizeof (*local));
	if (!local) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		op_errno = ENOMEM;
		goto unwind;
	}

	local->fd         = fd;
	local->offset     = offset;
	local->size       = size;
	local->wait_count = 1;

	local->fill.next  = &local->fill;
	local->fill.prev  = &local->fill;

	pthread_mutex_init (&local->local_lock, NULL);

	frame->local = local;

	dispatch_requests (frame, file);

	flush_region (frame, file, 0, floor (offset, file->page_size));

	read_ahead (frame, file);

	ra_frame_return (frame);

	file->offset = offset + size;

	return 0;

unwind:
	STACK_UNWIND (frame, -1, op_errno, NULL, 0, NULL);

	return 0;
}


int
ra_flush_cbk (call_frame_t *frame,
              void *cookie,
              xlator_t *this,
              int32_t op_ret,
              int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int
ra_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	ra_file_t *file = NULL;
	int        ret = 0;

	ret = dict_get_ptr (fd->ctx, this->name, (void **) ((void *) &file));

	if (file) {
		flush_region (frame, file, 0, file->pages.prev->offset+1);
	}

	STACK_WIND (frame, ra_flush_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->flush,
		    fd);
	return 0;
}


int
ra_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd,
          int32_t datasync)
{
	ra_file_t *file = NULL;
	int        ret = 0;

	ret = dict_get_ptr (fd->ctx, this->name, (void **) ((void *) &file));

	if (file) {
		flush_region (frame, file, 0, file->pages.prev->offset+1);
	}

	STACK_WIND (frame, ra_flush_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fsync,
		    fd, datasync);
	return 0;
}


int
ra_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct stat *stbuf)
{
	fd_t      *fd = NULL;
	ra_file_t *file = NULL;
	int        ret = 0;

	fd = frame->local;
	frame->local = fd;

	ret = dict_get_ptr (fd->ctx, this->name, (void **) ((void *) &file));

	if (file) {
		flush_region (frame, file, 0, file->pages.prev->offset+1);
	}

	frame->local = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, stbuf);
	return 0;
}


int
ra_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
           struct iovec *vector, int32_t count, off_t offset)
{
	ra_file_t *file = NULL;
	int        ret = 0;

	ret = dict_get_ptr (fd->ctx, this->name, (void **) ((void *) &file));

	if (file) {
		flush_region (frame, file, 0, file->pages.prev->offset+1);

		/* reset the read-ahead counters too */
		file->expected = file->page_count = 0;
	}

	frame->local = fd;

	STACK_WIND (frame, ra_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    fd, vector, count, offset);

	return 0;
}


int
ra_attr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	     int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}


int
ra_truncate (call_frame_t *frame, xlator_t *this,
             loc_t *loc, off_t offset)
{
	ra_file_t *file = NULL;
	fd_t      *iter_fd = NULL;
	inode_t   *inode = NULL;
	int        ret = 0;

	inode = loc->inode;

	LOCK (&inode->lock);
	{
		list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
			ret = dict_get_ptr (iter_fd->ctx, this->name,
					    (void **) ((void *)&file));
			if (!file)
				continue;
			flush_region (frame, file, 0,
				      file->pages.prev->offset + 1);
		}
	}
	UNLOCK (&inode->lock);

	STACK_WIND (frame, ra_attr_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->truncate,
		    loc, offset);
	return 0;
}


int
ra_fstat (call_frame_t *frame, xlator_t *this,
	  fd_t *fd)
{
	ra_file_t *file = NULL;
	fd_t      *iter_fd = NULL;
	inode_t   *inode = NULL;
	int        ret = 0;

	inode = fd->inode;

	LOCK (&inode->lock);
	{
		list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
			ret = dict_get_ptr (iter_fd->ctx, this->name,
					    (void **) ((void *)&file));
			if (!file)
				continue;
			flush_region (frame, file, 0,
				      file->pages.prev->offset + 1);
		}
	}
	UNLOCK (&inode->lock);

	STACK_WIND (frame, ra_attr_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fstat,
		    fd);
	return 0;
}


int
ra_fchown (call_frame_t *frame, xlator_t *this,
	   fd_t *fd, uid_t uid, gid_t gid)
{
	ra_file_t *file = NULL;
	fd_t      *iter_fd = NULL;
	inode_t   *inode = NULL;
	int        ret = 0;

	inode = fd->inode;

	LOCK (&inode->lock);
	{
		list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
			ret = dict_get_ptr (iter_fd->ctx, this->name,
					    (void **) ((void *)&file));
			if (!file)
				continue;
			flush_region (frame, file, 0,
				      file->pages.prev->offset + 1);
		}
	}
	UNLOCK (&inode->lock);

	STACK_WIND (frame, ra_attr_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fchown,
		    fd, uid, gid);
	return 0;
}


int
ra_ftruncate (call_frame_t *frame, xlator_t *this,
              fd_t *fd, off_t offset)
{
	ra_file_t *file = NULL;
	fd_t      *iter_fd = NULL;
	inode_t   *inode = NULL;
	int        ret = 0;

	inode = fd->inode;

	LOCK (&inode->lock);
	{
		list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
			ret = dict_get_ptr (iter_fd->ctx, this->name,
					    (void **) ((void *)&file));
			if (!file)
				continue;
			flush_region (frame, file, 0,
				      file->pages.prev->offset + 1);
		}
	}
	UNLOCK (&inode->lock);

	STACK_WIND (frame, ra_attr_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->ftruncate,
		    fd, offset);
	return 0;
}


int
init (xlator_t *this)
{
	ra_conf_t *conf;
	dict_t *options = this->options;
	char *page_size_string = NULL;
	char *page_count_string = NULL;

	if (!this->children || this->children->next) {
		gf_log (this->name,  GF_LOG_ERROR,
			"FATAL: read-ahead not configured with exactly one child");
		return -1;
	}

	conf = (void *) calloc (1, sizeof (*conf));
	ERR_ABORT (conf);
	conf->page_size = 256 * 1024;
	conf->page_count = 2;

	if (dict_get (options, "page-size"))
		page_size_string = data_to_str (dict_get (options,
							  "page-size"));
	if (page_size_string)
	{
		if (gf_string2bytesize (page_size_string, &conf->page_size) != 0)
		{
			gf_log ("read-ahead", 
				GF_LOG_ERROR, 
				"invalid number format \"%s\" of \"option page-size\"", 
				page_size_string);
			return -1;
		}
      
		gf_log (this->name, GF_LOG_DEBUG, "Using conf->page_size = %"PRIu64"",
			conf->page_size);
	}
  
	if (dict_get (options, "page-count"))
		page_count_string = data_to_str (dict_get (options, 
							   "page-count"));
	if (page_count_string)
	{
		if (gf_string2uint_base10 (page_count_string, &conf->page_count) != 0)
		{
			gf_log ("read-ahead", 
				GF_LOG_ERROR, 
				"invalid number format \"%s\" of \"option page-count\"", 
				page_count_string);
			return -1;
		}
		gf_log (this->name, GF_LOG_DEBUG, "Using conf->page_count = %u",
			conf->page_count);
	}
  
	if (dict_get (options, "force-atime-update")) {
		char *force_atime_update_str = data_to_str (dict_get (options,
								      "force-atime-update"));
		if (gf_string2boolean (force_atime_update_str, &conf->force_atime_update) == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"'force-atime-update' takes only boolean options");
			return -1;
		}
		if (conf->force_atime_update)
			gf_log (this->name, GF_LOG_DEBUG, "Forcing atime updates on cache hit");
	}

	conf->files.next = &conf->files;
	conf->files.prev = &conf->files;

	pthread_mutex_init (&conf->conf_lock, NULL);
	this->private = conf;
	return 0;
}

void
fini (xlator_t *this)
{
	ra_conf_t *conf = this->private;

	pthread_mutex_destroy (&conf->conf_lock);
	FREE (conf);

	this->private = NULL;
	return;
}

struct xlator_fops fops = {
	.open        = ra_open,
	.create      = ra_create,
	.readv       = ra_readv,
	.writev      = ra_writev,
	.flush       = ra_flush,
	.fsync       = ra_fsync,
	.truncate    = ra_truncate,
	.ftruncate   = ra_ftruncate,
	.fstat       = ra_fstat,
	.fchown      = ra_fchown,
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
	.release       = ra_release,
};

struct xlator_options options[] = {
	{ "force-atime-update", GF_OPTION_TYPE_BOOL, 0, },
	{ "page-size", GF_OPTION_TYPE_SIZET, 0, 16 * GF_UNIT_KB, 2 * GF_UNIT_MB },
	{ "page-count", GF_OPTION_TYPE_INT, 0, 1, 16 },
	{ NULL, 0, },
};
