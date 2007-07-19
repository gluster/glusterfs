/*
  (C) 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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

#ifndef __READ_AHEAD_H
#define __READ_AHEAD_H


#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "common-utils.h"
#include "call-stub.h"

#define IOC_PAGE_SIZE    (1024 * 128)   /* 128KB */
#define IOC_CACHE_SIZE   (32 * 1024 * 1024)

struct ioc_table;
struct ioc_local;
struct ioc_page;
struct ioc_inode;
struct ioc_waitq;

/*
 * ioc_waitq - this structure is used to represents the waiting 
 *             frames on a page
 *
 * @next: pointer to next object in waitq
 * @data: pointer to the frame which is waiting
 */
struct ioc_waitq {
  struct ioc_waitq *next;
  void *data;
  off_t pending_offset;
  size_t pending_size;
};

/*
 * ioc_fill - 
 *
 */
struct ioc_fill {
  struct list_head list;  /* list of ioc_fill structures of a frame */
  off_t offset;          
  size_t size;           
  struct iovec *vector;  
  int32_t count;
  dict_t *refs;
};

struct ioc_local {
  mode_t mode;
  int32_t flags;
  loc_t file_loc;
  off_t offset;
  size_t size;
  int32_t op_ret;
  int32_t op_errno;
  struct list_head fill_list;      /* list of ioc_fill structures */
  off_t pending_offset;            /* offset from this frame should continue */
  size_t pending_size;             /* size of data this frame is waiting on */
  struct ioc_inode *inode;
  int32_t wait_count;
  pthread_mutex_t local_lock;
  struct ioc_waitq *waitq;
  void *stub;
  fd_t *fd;
  
};

/*
 * ioc_page - structure to store page of data from file 
 *
 */
struct ioc_page {
  struct list_head pages;
  struct list_head page_lru;
  struct ioc_inode *inode;   /* inode this page belongs to */
  char dirty;
  char ready;
  struct iovec *vector;
  int32_t count;
  off_t offset;
  size_t size;
  struct ioc_waitq *waitq;
  dict_t *ref;
  pthread_mutex_t page_lock;
};

struct ioc_inode {
  struct ioc_table *table;
  struct list_head pages;      /* list of pages of this inode */
  struct list_head inode_list; /* list of inodes, maintained by io-cache translator */
  struct list_head inode_lru;
  struct list_head page_lru;
  struct ioc_waitq *waitq;
   int32_t op_ret;
  int32_t op_errno;
  size_t size;
  int32_t refcount;
  pthread_mutex_t inode_lock;
  uint64_t weight;             /* weight of the inode, increases on each read */
  struct stat stbuf;
  uint32_t validating;
  struct timeval tv;           /* time-stamp at last re-validate */
};

struct ioc_table {
  size_t page_size;
  uint64_t cache_size;
  uint64_t cache_used;
  struct list_head inodes; /* list of inodes cached */
  struct list_head active; 
  struct list_head inode_lru;
  int32_t readv_count;
  pthread_mutex_t table_lock;
  xlator_t *xl;
};

typedef struct ioc_table ioc_table_t;
typedef struct ioc_local ioc_local_t;
typedef struct ioc_page ioc_page_t;
typedef struct ioc_inode ioc_inode_t;
typedef struct ioc_waitq ioc_waitq_t;
typedef struct ioc_fill ioc_fill_t;

void *
str_to_ptr (char *string);

char *
ptr_to_str (void *ptr);

int32_t 
ioc_readv_disabled_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct iovec *vector,
			int32_t count,
			struct stat *stbuf);

ioc_page_t *
ioc_page_get (ioc_inode_t *ioc_inode,
	      off_t offset);

ioc_page_t *
ioc_page_create (ioc_inode_t *ioc_inode,
		 off_t offset);

void
ioc_page_fault (ioc_inode_t *ioc_inode,
		call_frame_t *frame,
		fd_t *fd,
		off_t offset);
void
ioc_wait_on_page (ioc_page_t *page,
		  call_frame_t *frame,
		  off_t offset,
		  size_t size);

void
ioc_page_wakeup (ioc_page_t *page);

void
ioc_page_flush (ioc_page_t *page);

void
ioc_page_error (ioc_page_t *page,
		int32_t op_ret,
		int32_t op_errno);
void
ioc_page_purge (ioc_page_t *page);

ioc_inode_t *
ioc_inode_ref (ioc_inode_t *ioc_inode);

void
ioc_inode_unref (ioc_inode_t *ioc_inode);

void
ioc_inode_unref_locked (ioc_inode_t *ioc_inode);

void
ioc_frame_return (call_frame_t *frame);

void
ioc_frame_fill (ioc_page_t *page,
		call_frame_t *frame,
		off_t offset,
		size_t size);

static inline void
ioc_inode_lock (ioc_inode_t *ioc_inode)
{
  pthread_mutex_lock (&ioc_inode->inode_lock);
}

static inline void
ioc_inode_unlock (ioc_inode_t *ioc_inode)
{
  pthread_mutex_unlock (&ioc_inode->inode_lock);
}

static inline void
ioc_table_lock (ioc_table_t *table)
{
  pthread_mutex_lock (&table->table_lock);
}

static inline void
ioc_table_unlock (ioc_table_t *table)
{
  pthread_mutex_unlock (&table->table_lock);
}

static inline void
ioc_local_lock (ioc_local_t *local)
{
  pthread_mutex_lock (&local->local_lock);
}

static inline void
ioc_local_unlock (ioc_local_t *local)
{
  pthread_mutex_unlock (&local->local_lock);
}

static inline void
ioc_page_lock (ioc_page_t *page)
{
  pthread_mutex_lock (&page->page_lock);
}

static inline void
ioc_page_unlock (ioc_page_t *page)
{
  pthread_mutex_unlock (&page->page_lock);
}

static inline uint64_t
time_elapsed (struct timeval *now,
	      struct timeval *then)
{
  uint64_t sec = now->tv_sec - then->tv_sec;

  if (sec)
    return sec;
  
  return 0;
}

ioc_inode_t *
ioc_inode_search (ioc_table_t *table,
		  inode_t *inode);

void 
ioc_inode_destroy (ioc_inode_t *ioc_inode);

ioc_inode_t *
ioc_inode_update (ioc_table_t *table,
		  inode_t *inode);

int64_t 
ioc_page_destroy (ioc_page_t *page);

int32_t
__ioc_inode_flush (ioc_inode_t *ioc_inode);

void
ioc_inode_flush (ioc_inode_t *ioc_inode);

void
ioc_inode_wakeup (call_frame_t *frame, 
		  ioc_inode_t *ioc_inode, 
		  struct stat *stbuf);

int8_t
ioc_cache_still_valid (ioc_inode_t *ioc_inode,
		       struct stat *stbuf);

int32_t
ioc_prune (ioc_table_t *table);

#endif /* __READ_AHEAD_H */
