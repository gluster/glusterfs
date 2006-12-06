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

struct io_cache_conf;
struct io_cache_local;
struct io_cache_page;
struct io_cache_file;
struct io_cache_waitq;

struct io_cache_waitq {
  struct io_cache_waitq *next;
  void *data;
};

struct io_cache_local {
  mode_t mode;
  int32_t flags;
  char *filename;
  char *ptr;
  off_t offset;
  size_t size;
  int32_t op_ret;
  int32_t op_errno;
  off_t pending_offset;
  size_t pending_size;
  struct io_cache_file *file;
  int32_t wait_count;
};

struct io_cache_page {
  struct io_cache_page *next;
  struct io_cache_page *prev;
  struct io_cache_file *file;
  char dirty;
  char ready;
  char *ptr;
  off_t offset;
  size_t size;
  struct io_cache_waitq *waitq;
};

struct io_cache_file {
  struct io_cache_file *next;
  struct io_cache_file *prev;
  struct io_cache_conf *conf;
  dict_t *file_ctx;
  char *filename;
  struct io_cache_page pages;
  off_t offset;
  size_t size;
  int32_t refcount;
};

struct io_cache_conf {
  size_t page_size;
  int32_t page_count;
  void *cache_block;
  struct io_cache_file files;
};

typedef struct io_cache_conf io_cache_conf_t;
typedef struct io_cache_local io_cache_local_t;
typedef struct io_cache_page io_cache_page_t;
typedef struct io_cache_file io_cache_file_t;
typedef struct io_cache_waitq io_cache_waitq_t;

io_cache_page_t *
io_cache_get_page (io_cache_file_t *file,
		   off_t offset);
io_cache_page_t *
io_cache_create_page (io_cache_file_t *file,
		      off_t offset);
void
io_cache_wait_on_page (io_cache_page_t *page,
		       call_frame_t *frame);
void
io_cache_fill_frame (io_cache_page_t *page,
		     call_frame_t *frame);
void
io_cache_wakeup_page (io_cache_page_t *page);
void
io_cache_flush_page (io_cache_page_t *page);
void
io_cache_error_page (io_cache_page_t *page,
		     int32_t op_ret,
		     int32_t op_errno);
void
io_cache_purge_page (io_cache_page_t *page);
io_cache_file_t *
io_cache_file_ref (io_cache_file_t *file);
void
io_cache_file_unref (io_cache_file_t *file);

#endif /* __IO_CACHE_H */
