/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#ifndef __IO_CACHE_H
#define __IO_CACHE_H


#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define roof(a,b) ((((a)+(b)-1)/(b))*(b))
#define floor(a,b) (((a)/(b))*(b))

struct ra_conf;
struct ra_local;
struct ra_page;
struct ra_file;
struct ra_waitq;

struct ra_waitq {
  struct ra_waitq *next;
  void *data;
};

struct ra_local {
  mode_t mode;
  int32_t flags;
  char *filename;
  char is_static;
  char *ptr;
  off_t offset;
  size_t size;
  int32_t op_ret;
  int32_t op_errno;
  off_t pending_offset;
  size_t pending_size;
  struct ra_file *file;
  int32_t wait_count;
};

struct ra_page {
  struct ra_page *next;
  struct ra_page *prev;
  struct ra_file *file;
  char dirty;
  char ready;
  char *ptr;
  off_t offset;
  size_t size;
  struct ra_waitq *waitq;
  dict_t *ref;
};

struct ra_file {
  struct ra_file *next;
  struct ra_file *prev;
  struct ra_conf *conf;
  dict_t *file_ctx;
  char *filename;
  struct ra_page pages;
  off_t offset;
  size_t size;
  int32_t refcount;
};

struct ra_conf {
  size_t page_size;
  int32_t page_count;
  void *cache_block;
  struct ra_file files;
};

typedef struct ra_conf ra_conf_t;
typedef struct ra_local ra_local_t;
typedef struct ra_page ra_page_t;
typedef struct ra_file ra_file_t;
typedef struct ra_waitq ra_waitq_t;

ra_page_t *
ra_get_page (ra_file_t *file,
	     off_t offset);
ra_page_t *
ra_create_page (ra_file_t *file,
		off_t offset);
void
ra_wait_on_page (ra_page_t *page,
		 call_frame_t *frame);
void
ra_fill_frame (ra_page_t *page,
	       call_frame_t *frame);
void
ra_wakeup_page (ra_page_t *page);
void
ra_flush_page (ra_page_t *page);
void
ra_error_page (ra_page_t *page,
	       int32_t op_ret,
	       int32_t op_errno);
void
ra_purge_page (ra_page_t *page);
ra_file_t *
ra_file_ref (ra_file_t *file);
void
ra_file_unref (ra_file_t *file);

#endif /* __IO_CACHE_H */
