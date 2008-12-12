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

#ifndef __READ_AHEAD_H
#define __READ_AHEAD_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "common-utils.h"

struct ra_conf;
struct ra_local;
struct ra_page;
struct ra_file;
struct ra_waitq;

struct ra_waitq {
	struct ra_waitq *next;
	void *data;
};

struct ra_fill {
	struct ra_fill *next;
	struct ra_fill *prev;
	off_t offset;
	size_t size;
	struct iovec *vector;
	int32_t count;
	dict_t *refs;
};

struct ra_local {
	mode_t mode;
	int32_t flags;
	loc_t file_loc;
	struct ra_fill fill;
	off_t offset;
	size_t size;
	int32_t op_ret;
	int32_t op_errno;
	off_t pending_offset;
	size_t pending_size;
	struct ra_file *file;
	int32_t wait_count;
	pthread_mutex_t local_lock;
};

struct ra_page {
	struct ra_page *next;
	struct ra_page *prev;
	struct ra_file *file;
	char dirty;
	char ready;
	struct iovec *vector;
	int32_t count;
	off_t offset;
	size_t size;
	struct ra_waitq *waitq;
	dict_t *ref;
};

struct ra_file {
	struct ra_file *next;
	struct ra_file *prev;
	struct ra_conf *conf;
	fd_t *fd;
	int disabled;
	size_t expected;
	struct ra_page pages;
	off_t offset;
	size_t size;
	int32_t refcount;
	pthread_mutex_t file_lock;
	struct stat stbuf;
	uint64_t page_size;
	uint32_t page_count;
};

struct ra_conf {
	uint64_t page_size;
	uint32_t page_count;
	void *cache_block;
	struct ra_file files;
	gf_boolean_t force_atime_update;
	pthread_mutex_t conf_lock;
};

typedef struct ra_conf ra_conf_t;
typedef struct ra_local ra_local_t;
typedef struct ra_page ra_page_t;
typedef struct ra_file ra_file_t;
typedef struct ra_waitq ra_waitq_t;
typedef struct ra_fill ra_fill_t;

ra_page_t *
ra_page_get (ra_file_t *file,
	     off_t offset);
ra_page_t *
ra_page_create (ra_file_t *file,
		off_t offset);
void
ra_page_fault (ra_file_t *file,
	       call_frame_t *frame,
	       off_t offset);
void
ra_wait_on_page (ra_page_t *page,
		 call_frame_t *frame);
ra_waitq_t *
ra_page_wakeup (ra_page_t *page);

void
ra_page_flush (ra_page_t *page);

void
ra_page_error (ra_page_t *page,
	       int32_t op_ret,
	       int32_t op_errno);
void
ra_page_purge (ra_page_t *page);

ra_file_t *
ra_file_ref (ra_file_t *file);
void
ra_file_unref (ra_file_t *file);

void
ra_frame_return (call_frame_t *frame);
void
ra_frame_fill (ra_page_t *page,
	       call_frame_t *frame);

static inline void
ra_file_lock (ra_file_t *file)
{
	pthread_mutex_lock (&file->file_lock);
}

static inline void
ra_file_unlock (ra_file_t *file)
{
	pthread_mutex_unlock (&file->file_lock);
}

static inline void
ra_conf_lock (ra_conf_t *conf)
{
	pthread_mutex_lock (&conf->conf_lock);
}

static inline void
ra_conf_unlock (ra_conf_t *conf)
{
	pthread_mutex_unlock (&conf->conf_lock);
}
static inline void
ra_local_lock (ra_local_t *local)
{
	pthread_mutex_lock (&local->local_lock);
}

static inline void
ra_local_unlock (ra_local_t *local)
{
	pthread_mutex_unlock (&local->local_lock);
}

#endif /* __READ_AHEAD_H */
