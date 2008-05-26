/*
   Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-cache.h"
#include <assert.h>
#include <sys/time.h>

static uint32_t
ioc_get_priority (ioc_table_t *table, 
		  const char *path);

static uint32_t
ioc_get_priority (ioc_table_t *table, 
		  const char *path);

static inline ioc_inode_t *
ioc_inode_reupdate (ioc_inode_t *ioc_inode)
{
  ioc_table_t *table = ioc_inode->table;

  list_add_tail (&ioc_inode->inode_lru, &table->inode_lru[ioc_inode->weight]);
  
  return ioc_inode;
}

static inline ioc_inode_t *
ioc_get_inode (dict_t *dict,
	       char *name)
{
  ioc_inode_t *ioc_inode = NULL;
  data_t *ioc_inode_data = dict_get (dict, name);
  ioc_table_t *table = NULL;

  if (ioc_inode_data) {
    ioc_inode = data_to_ptr (ioc_inode_data);
    table = ioc_inode->table;

    ioc_table_lock (table);
    if (list_empty (&ioc_inode->inode_lru)) {
      ioc_inode = ioc_inode_reupdate (ioc_inode);
    }
    ioc_table_unlock (table);
  }
  
  return ioc_inode;
}

int32_t
ioc_inode_need_revalidate (ioc_inode_t *ioc_inode)
{
  int8_t need_revalidate = 0;
  struct timeval tv = {0,};
  int32_t ret = -1;
  ioc_table_t *table = ioc_inode->table;

  ret = gettimeofday (&tv, NULL);

  if (time_elapsed (&tv, &ioc_inode->tv) >= table->force_revalidate_timeout)
    need_revalidate = 1;

  return need_revalidate;
}

/*
 * __ioc_inode_flush - flush all the cached pages of the given inode
 *
 * @ioc_inode: 
 *
 * assumes lock is held
 */
int32_t
__ioc_inode_flush (ioc_inode_t *ioc_inode)
{
  ioc_page_t *curr = NULL, *next = NULL;
  int32_t destroy_size = 0;
  int32_t ret = 0;

  list_for_each_entry_safe (curr, next, &ioc_inode->pages, pages) {
    ret = ioc_page_destroy (curr);
    
    if (ret != -1) 
      destroy_size += ret;
  }
  
  return destroy_size;
}

void
ioc_inode_flush (ioc_inode_t *ioc_inode)
{
  int32_t destroy_size = 0;    

  ioc_inode_lock (ioc_inode);
  destroy_size = __ioc_inode_flush (ioc_inode);
  ioc_inode_unlock (ioc_inode);
  
  if (destroy_size) {
    ioc_table_lock (ioc_inode->table);
    {
      ioc_inode->table->cache_used -= destroy_size;
    }
    ioc_table_unlock (ioc_inode->table);
  }

  return;
}

/* 
 * ioc_utimens_cbk -
 * 
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 */
int32_t
ioc_utimens_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

/* 
 * ioc_utimens -
 * 
 * @frame:
 * @this:
 * @loc:
 * @tv:
 *
 */
int32_t
ioc_utimens (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     struct timespec *tv)
{
  ioc_inode_t *ioc_inode = ioc_get_inode (loc->inode->ctx, this->name);

  if (ioc_inode)
    ioc_inode_flush (ioc_inode);

  STACK_WIND (frame, ioc_utimens_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->utimens,
	      loc, tv);
  return 0;
}

int32_t
ioc_lookup_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *stbuf,
		dict_t *dict)
{
  ioc_inode_t *ioc_inode = NULL;
  uint8_t cache_still_valid = 0;
  ioc_local_t *local = frame->local;
  data_t *page_data = NULL;
  ioc_table_t *table = this->private;
  
  if (op_ret == 0) {
    ioc_inode = ioc_get_inode (inode->ctx, this->name);

    if (ioc_inode) {
      cache_still_valid = ioc_cache_still_valid (ioc_inode, stbuf);
    
      if (!cache_still_valid) {
	ioc_inode_flush (ioc_inode);
      } 
      /* update the time-stamp of revalidation */
      ioc_inode_lock (ioc_inode);
      {
	gettimeofday (&ioc_inode->tv, NULL);
      }
      ioc_inode_unlock (ioc_inode);

      ioc_table_lock (ioc_inode->table);
      {
	list_move_tail (&ioc_inode->inode_lru,
			&table->inode_lru[ioc_inode->weight]);
      }
      ioc_table_unlock (ioc_inode->table);
    }

    if (local && local->need_xattr >= stbuf->st_size) {
      char *src = NULL, *dst = NULL;
      data_t *content_data = NULL;
      ioc_page_t *page = NULL;

      if (!ioc_inode) {
	uint32_t weight = ioc_get_priority (table, local->file_loc.path);
      
	ioc_inode = ioc_inode_update (table, inode, weight);
	dict_set (inode->ctx, this->name, data_from_static_ptr (ioc_inode));
      }

      ioc_inode_lock (ioc_inode);
      {
	content_data = dict_get (dict, "glusterfs.content");
	page = ioc_page_get (ioc_inode, 0);

	if (content_data) {
	  if (page) {
	    dict_unref (page->ref);
	    free (page->vector);
	    page->vector = NULL;

	    ioc_table_lock (table);
	    {
	      table->cache_used -= page->size;
	    }
	    ioc_table_unlock (table);
	  } else {
	    page = ioc_page_create (ioc_inode, 0);
	  }

	  dst = calloc (1, stbuf->st_size);
	  page->ref = dict_ref (get_new_dict ());
	  page_data = data_from_dynptr (dst, stbuf->st_size);
	  dict_set (page->ref, NULL, page_data);

	  src = data_to_ptr (content_data);
	  memcpy (dst, src, stbuf->st_size);

	  page->vector = calloc (1, sizeof (*page->vector));
	  page->vector->iov_base = dst;
	  page->vector->iov_len = stbuf->st_size;
	  page->count = 1;
      
	  page->waitq = NULL;
	  page->size = stbuf->st_size;
	  page->ready = 1;

	  ioc_table_lock (table);
	  {
	    table->cache_used += page->size;
	  }
	  ioc_table_unlock (table);

	} else {
	  if (!(page && page->ready) && dict) {
	    gf_log (this->name,
		    GF_LOG_DEBUG,
		    "glusterfs.content is NULL");

	    ioc_inode_unlock (ioc_inode);
	    STACK_WIND (frame,
			ioc_lookup_cbk,
			FIRST_CHILD (this),
			FIRST_CHILD (this)->fops->lookup,
			&local->file_loc,
			local->need_xattr);
	    return 0;
	  } else {
	    char *buf = calloc (1, stbuf->st_size);
	    char *tmp = buf;
	    int i;
	    for (i = 0; i < page->count; i++) {
	      memcpy (tmp, page->vector[i].iov_base, page->vector[i].iov_len);
	      tmp += page->vector[i].iov_len;
	    }

	    dict_set (dict, "glusterfs.content", data_from_dynptr (buf, stbuf->st_size));
	  }
	}
	ioc_inode->mtime = stbuf->st_mtime;
	gettimeofday (&ioc_inode->tv, NULL);
      }
      ioc_inode_unlock (ioc_inode);

      if (content_data && ioc_need_prune (ioc_inode->table)) {
	ioc_prune (ioc_inode->table);
      }
    }
  }

  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf, dict);
  return 0;
}

int32_t 
ioc_lookup (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t need_xattr)
{
  if (need_xattr) {
    ioc_inode_t *ioc_inode = NULL;
    ioc_page_t *page = NULL;

    ioc_local_t *local = calloc (1, sizeof (*local));
    local->need_xattr = need_xattr;
    local->file_loc.path = loc->path;
    local->file_loc.inode = loc->inode;
    frame->local = local;

    ioc_inode = ioc_get_inode (loc->inode->ctx, this->name);

    if (ioc_inode) {
      ioc_inode_lock (ioc_inode);
      {
	page = ioc_page_get (ioc_inode, 0);
	if (need_xattr <= ioc_inode->table->page_size && page && page->ready) {
	  need_xattr = -1;
	}
      }
      ioc_inode_unlock (ioc_inode);
    }
  }

  STACK_WIND (frame,
	      ioc_lookup_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->lookup,
	      loc,
	      need_xattr);
  return 0;
}

/*
 * ioc_forget - 
 *
 * @frame:
 * @this:
 * @inode:
 *
 */
int32_t
ioc_forget (call_frame_t *frame,
	    xlator_t *this,
	    inode_t *inode)
{
  ioc_inode_t *ioc_inode = NULL;

  ioc_inode = ioc_get_inode (inode->ctx, this->name);

  if (ioc_inode)
    ioc_inode_destroy (ioc_inode);
	    
  return 0;
}


/* 
 * ioc_cache_validate_cbk - 
 *
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @buf
 *
 */
int32_t
ioc_cache_validate_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct stat *stbuf)
{
  ioc_local_t *local = frame->local;
  ioc_inode_t *ioc_inode = NULL;
  size_t destroy_size = 0;

  ioc_inode = local->inode;

  if (op_ret == -1 || (op_ret >= 0 && !ioc_cache_still_valid(ioc_inode, stbuf))) {
      gf_log (ioc_inode->table->xl->name, GF_LOG_DEBUG,
	      "cache for inode(%p) is invalid. flushing all pages", ioc_inode);
      ioc_inode_lock (ioc_inode);
      destroy_size = __ioc_inode_flush (ioc_inode);
      if (op_ret >= 0)
	ioc_inode->mtime = stbuf->st_mtime;
      ioc_inode_unlock (ioc_inode);
  }

  if (destroy_size) {
    ioc_table_lock (ioc_inode->table);
    ioc_inode->table->cache_used -= destroy_size;
    ioc_table_unlock (ioc_inode->table);
  }

  if (op_ret < 0)
    stbuf = NULL;
  
  ioc_inode_lock (ioc_inode);
  gettimeofday (&ioc_inode->tv, NULL);
  ioc_inode_unlock (ioc_inode);

  ioc_inode_wakeup (frame, ioc_inode, stbuf);
  
  STACK_DESTROY (frame->root);

  return 0;
}


/*
 * ioc_cache_validate -
 *
 * @frame:
 * @ioc_inode:
 * @fd:
 *
 */
static int32_t
ioc_cache_validate (call_frame_t *frame,
		    ioc_inode_t *ioc_inode,
		    fd_t *fd,
		    ioc_page_t *page)
{
  char need_validate = 0;
  ioc_waitq_t *waiter = NULL;
  call_frame_t *validate_frame = NULL;

  waiter = calloc (1, sizeof (ioc_waitq_t));
  ERR_ABORT (waiter);

  ioc_inode_lock (ioc_inode);

  if (!ioc_inode->waitq) {
    need_validate = 1;
  }

  waiter->data = page;
  waiter->next = ioc_inode->waitq;
  ioc_inode->waitq = waiter;

  ioc_inode_unlock (ioc_inode);
  
  if (need_validate) {
    ioc_local_t *validate_local = calloc (1, sizeof (ioc_local_t));
    ERR_ABORT (validate_local);
    validate_frame = copy_frame (frame);
    validate_local->fd = fd;
    validate_local->inode = ioc_inode;
    validate_frame->local = validate_local;
    
    STACK_WIND (validate_frame,
		ioc_cache_validate_cbk,
		FIRST_CHILD (frame->this),
		FIRST_CHILD (frame->this)->fops->fstat,
		fd);
  }
  return 0;
}

static inline uint32_t
is_match (const char *path,
	  const char *pattern)
{
  char *pathname = strdup (path);
  int32_t ret = 0;

  ret = fnmatch (pattern, path, FNM_NOESCAPE);
  
  free (pathname);
  
  return (ret == 0);
}

static uint32_t
ioc_get_priority (ioc_table_t *table, 
		  const char *path)
{
  uint32_t priority = 0;
  struct ioc_priority *curr = NULL;
  
  list_for_each_entry (curr, &table->priority_list, list) {
    if (is_match (path, curr->pattern)) 
      priority = curr->priority;
  }

  return priority;
}

/* 
 * ioc_open_cbk - open callback for io cache
 *
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @fd:
 *
 */
int32_t
ioc_open_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      fd_t *fd)
{
  ioc_local_t *local = frame->local;
  ioc_table_t *table = this->private;
  ioc_inode_t *ioc_inode = NULL;
  inode_t *inode = local->file_loc.inode;
  uint32_t weight = 0;
  const char *path = local->file_loc.path;

  if (op_ret != -1) {
    /* look for ioc_inode corresponding to this fd */
    LOCK (&fd->inode->lock);
    ioc_inode = ioc_get_inode (fd->inode->ctx, this->name);
      
    if (!ioc_inode) {
      /* this is the first time someone is opening this file */
      /* assign weight */
      weight = ioc_get_priority (table, path);
 
      ioc_inode = ioc_inode_update (table, inode, weight);
      dict_set (fd->inode->ctx, this->name, data_from_static_ptr (ioc_inode));
    } else {
      ioc_table_lock (ioc_inode->table);
      list_move_tail (&ioc_inode->inode_lru,
		      &table->inode_lru[ioc_inode->weight]);
      ioc_table_unlock (ioc_inode->table);
    }
    UNLOCK (&fd->inode->lock);

    /* If mandatory locking has been enabled on this file,
       we disable caching on it */
    if (((inode->st_mode & S_ISGID) && !(inode->st_mode & S_IXGRP))) {
      dict_set (fd->ctx, this->name, data_from_uint32 (1));
    }
  
    /* If O_DIRECT open, we disable caching on it */
    if ((local->flags & O_DIRECT)){
      /* O_DIRECT is only for one fd, not the inode as a whole */
      dict_set (fd->ctx, this->name, data_from_uint32 (1));
    }
  }

  FREE (local);
  frame->local = NULL;

  STACK_UNWIND (frame, op_ret, op_errno, fd);

  return 0;
}

/*
 * ioc_create_cbk - create callback for io cache
 *
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @fd:
 * @inode:
 * @buf:
 *
 */
int32_t
ioc_create_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd,
		inode_t *inode,
		struct stat *buf)
{
  ioc_local_t *local = frame->local;
  ioc_table_t *table = this->private;
  ioc_inode_t *ioc_inode = NULL;
  uint32_t weight = 0;
  const char *path = local->file_loc.path;

  if (op_ret != -1) {
    {
      /* assign weight */
      weight = ioc_get_priority (table, path);

      ioc_inode = ioc_inode_update (table, inode, weight);
      LOCK (&fd->inode->lock);
      dict_set (fd->inode->ctx, this->name, data_from_static_ptr (ioc_inode));
      UNLOCK (&fd->inode->lock);
    }
    /* If mandatory locking has been enabled on this file,
       we disable caching on it */
    if ((inode->st_mode & S_ISGID) && 
	!(inode->st_mode & S_IXGRP)) {
      dict_set (fd->ctx, this->name, data_from_uint32 (1));
    }

    /* If O_DIRECT open, we disable caching on it */
    if (local->flags & O_DIRECT){
      /* O_DIRECT is only for one fd, not the inode as a whole */
      dict_set (fd->ctx, this->name, data_from_uint32 (1));
    }
    
  }
  
  frame->local = NULL;
  FREE (local);

  STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);

  return 0;
}

/*
 * ioc_open - open fop for io cache
 * @frame:
 * @this:
 * @loc:
 * @flags:
 *
 */
int32_t
ioc_open (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  int32_t flags,
	  fd_t *fd)
{
  
  ioc_local_t *local = calloc (1, sizeof (ioc_local_t));
  ERR_ABORT (local);

  local->flags = flags;
  local->file_loc.path = loc->path;
  local->file_loc.inode = loc->inode;
  
  frame->local = local;
  
  STACK_WIND (frame,
	      ioc_open_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->open,
	      loc,
	      flags,
	      fd);

  return 0;
}

/*
 * ioc_create - create fop for io cache
 * 
 * @frame:
 * @this:
 * @pathname:
 * @flags:
 * @mode:
 *
 */
int32_t
ioc_create (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags,
	    mode_t mode,
	    fd_t *fd)
{
  ioc_local_t *local = calloc (1, sizeof (ioc_local_t));
  ERR_ABORT (local);

  local->flags = flags;
  local->file_loc.path = loc->path;
  frame->local = local;

  STACK_WIND (frame, ioc_create_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->create,
	      loc, flags, mode, fd);
  return 0;
}


/*
 * ioc_close_cbk - close callback
 * 
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 */
int32_t
ioc_close_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


/*
 * ioc_close - close fop for io cache
 * 
 * @frame:
 * @this:
 * @fd:
 *
 */
int32_t
ioc_close (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  STACK_WIND (frame,
	      ioc_close_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->close,
	      fd);
  return 0;
}

/* 
 * ioc_readv_disabled_cbk 
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @vector:
 * @count:
 *
 */ 
int32_t
ioc_readv_disabled_cbk (call_frame_t *frame, 
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct iovec *vector,
			int32_t count,
			struct stat *stbuf)
{
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (vector);

  STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf);
  return 0;
}


int32_t
ioc_need_prune (ioc_table_t *table)
{
  int64_t cache_difference = 0;
  
  ioc_table_lock (table);
  {
    cache_difference = table->cache_used - table->cache_size;
  }
  ioc_table_unlock (table);

  if (cache_difference > 0)
    return ((cache_difference*100/table->cache_size) >= 20);
  else 
    return 0;
}

/*
 * dispatch_requests -
 * 
 * @frame:
 * @inode:
 *
 * 
 */
static void
dispatch_requests (call_frame_t *frame,
		   ioc_inode_t *ioc_inode,
		   fd_t *fd,
		   off_t offset,
		   size_t size)
{
  ioc_local_t *local = frame->local;
  ioc_table_t *table = ioc_inode->table;
  off_t rounded_offset = 0;
  off_t rounded_end = 0;
  off_t trav_offset = 0;
  ioc_page_t *trav = NULL;
  int32_t fault = 0;
  int8_t might_need_validate = 0;  /* if a page exists, do we need to validate it? */
  int8_t need_validate = 0;

  rounded_offset = floor (offset, table->page_size);
  rounded_end = roof (offset + size, table->page_size);
  trav_offset = rounded_offset;

  /* once a frame does read, it should be waiting on something */
  local->wait_count++;

  /* Requested region can fall in three different pages,
   * 1. Ready - region is already in cache, we just have to serve it.
   * 2. In-transit - page fault has been generated on this page, we need
   *    to wait till the page is ready
   * 3. Fault - page is not in cache, we have to generate a page fault
   */

  might_need_validate = ioc_inode_need_revalidate (ioc_inode);

  while (trav_offset < rounded_end) {
    size_t trav_size = 0;
    off_t local_offset = 0;

    ioc_inode_lock (ioc_inode);
    /* look for requested region in the cache */
    trav = ioc_page_get (ioc_inode, trav_offset);

    local_offset = max (trav_offset, offset);
    trav_size = min (((offset+size) - local_offset), table->page_size);

    if (!trav) {
      /* page not in cache, we need to generate page fault */
      trav = ioc_page_create (ioc_inode, trav_offset);
      fault = 1;
      if (!trav) {
	gf_log (frame->this->name, GF_LOG_CRITICAL,
		"ioc_page_create returned NULL");
      }
    } 

    ioc_wait_on_page (trav, frame, local_offset, trav_size);

    if (trav->ready) {
      /* page found in cache */
      if (!might_need_validate) {
	/* fresh enough */
	gf_log (frame->this->name, GF_LOG_DEBUG,
		"cache hit for trav_offset=%lld/local_offset=%lld",
		trav_offset, local_offset);
	ioc_page_wakeup (trav);
      } else {
	/* we need to validate the cache */
	need_validate = 1;
	trav->ready = 0;
      }
    }

    ioc_inode_unlock (ioc_inode);
    
    if (fault) {
      fault = 0;
      /* new page created, increase the table->cache_used */
      ioc_page_fault (ioc_inode, frame, fd, trav_offset);
    }

    if (need_validate) {
      need_validate = 0;
      gf_log (frame->this->name, GF_LOG_DEBUG,
	      "sending validate request for inode(%"PRId64") at offset=%lld",
	      fd->inode->ino, trav_offset);
      ioc_cache_validate (frame, ioc_inode, fd, trav);
    }

    
    trav_offset += table->page_size;
  }

  ioc_frame_return (frame);

  if (ioc_need_prune (ioc_inode->table)) {
    ioc_prune (ioc_inode->table);
  }

  return;
}


/*
 * ioc_readv -
 * 
 * @frame:
 * @this:
 * @fd:
 * @size:
 * @offset:
 *
 */
int32_t
ioc_readv (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   size_t size,
	   off_t offset)
{
  ioc_inode_t *ioc_inode = ioc_get_inode (fd->inode->ctx, this->name);
  ioc_local_t *local = NULL;
  data_t *fd_ctx_data = dict_get (fd->ctx, this->name);
  uint32_t weight = 0;

  if (!ioc_inode) {
    /* caching disabled, go ahead with normal readv */
    STACK_WIND (frame, 
		ioc_readv_disabled_cbk,
		FIRST_CHILD (frame->this), 
		FIRST_CHILD (frame->this)->fops->readv,
		fd, 
		size, 
		offset);
    return 0;
  }

  if (fd_ctx_data) {
    /* disable caching for this fd, go ahead with normal readv */
    STACK_WIND (frame, 
		ioc_readv_disabled_cbk,
		FIRST_CHILD (frame->this), 
		FIRST_CHILD (frame->this)->fops->readv,
		fd, 
		size, 
		offset);
    return 0;
  }

  local = (ioc_local_t *) calloc (1, sizeof (ioc_local_t));
  ERR_ABORT (local);
  INIT_LIST_HEAD (&local->fill_list);

  frame->local = local;  
  local->pending_offset = offset;
  local->pending_size = size;
  local->offset = offset;
  local->size = size;
  local->inode = ioc_inode;

  gf_log (this->name,
	  GF_LOG_DEBUG,
	  "NEW REQ (%p) offset = %lld && size = %d", frame, offset, size);

  weight = ioc_inode->weight;

  ioc_table_lock (ioc_inode->table);
  {
    list_move_tail (&ioc_inode->inode_lru, &ioc_inode->table->inode_lru[weight]);
  }
  ioc_table_unlock (ioc_inode->table);

  dispatch_requests (frame, ioc_inode, fd, offset, size);
  
  return 0;
}

/*
 * ioc_writev_cbk -
 * 
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 */
int32_t
ioc_writev_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *stbuf)
{
  ioc_local_t *local = frame->local;
  ioc_inode_t *ioc_inode = ioc_get_inode (local->fd->inode->ctx, this->name);
  
  if (ioc_inode)
    ioc_inode_flush (ioc_inode);

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

/*
 * ioc_writev
 * 
 * @frame:
 * @this:
 * @fd:
 * @vector:
 * @count:
 * @offset:
 *
 */
int32_t
ioc_writev (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    struct iovec *vector,
	    int32_t count,
	    off_t offset)
{
  ioc_local_t *local = NULL;
  ioc_inode_t *ioc_inode = ioc_get_inode (fd->inode->ctx, this->name);

  local = calloc (1, sizeof (ioc_local_t));
  ERR_ABORT (local);

  local->fd = fd;
  frame->local = local;

  if (ioc_inode)
    ioc_inode_flush (ioc_inode);

  STACK_WIND (frame,
	      ioc_writev_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->writev,
	      fd,
	      vector,
	      count,
	      offset);

  return 0;
}

/*
 * ioc_truncate_cbk -
 * 
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @buf:
 *
 */
int32_t 
ioc_truncate_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

/*
 * ioc_truncate -
 * 
 * @frame:
 * @this:
 * @loc:
 * @offset:
 *
 */
int32_t 
ioc_truncate (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      off_t offset)
{
  ioc_inode_t *ioc_inode =  ioc_get_inode (loc->inode->ctx, this->name);

  if (ioc_inode)
    ioc_inode_flush (ioc_inode);

  STACK_WIND (frame,
	      ioc_truncate_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->truncate,
	      loc,
	      offset);
  return 0;
}

/*
 * ioc_ftruncate -
 * 
 * @frame:
 * @this:
 * @fd:
 * @offset:
 *
 */
int32_t
ioc_ftruncate (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       off_t offset)
{
  ioc_inode_t *ioc_inode = ioc_get_inode (fd->inode->ctx, this->name);
  
  if (ioc_inode)
    ioc_inode_flush (ioc_inode);

  STACK_WIND (frame,
	      ioc_truncate_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->ftruncate,
	      fd,
	      offset);
  return 0;
}

int32_t
ioc_lk_cbk (call_frame_t *frame,
	    void *cookie,
	    xlator_t *this,
	    int32_t op_ret,
	    int32_t op_errno,
	    struct flock *lock)
{
  STACK_UNWIND (frame, op_ret, op_errno, lock);
  return 0;
}

int32_t 
ioc_lk (call_frame_t *frame,
	xlator_t *this,
	fd_t *fd,
	int32_t cmd,
	struct flock *lock)
{
  ioc_inode_t *ioc_inode = ioc_get_inode (fd->inode->ctx, this->name);

  if (!ioc_inode) {
    gf_log (this->name, GF_LOG_ERROR,
	    "inode context is NULL: returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD, NULL);
  }

  ioc_inode_lock (ioc_inode);
  gettimeofday (&ioc_inode->tv, NULL);
  ioc_inode_unlock (ioc_inode);

  STACK_WIND (frame, ioc_lk_cbk, 
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->lk, fd, cmd, lock);
  return 0;
}

int32_t
ioc_get_priority_list (const char *opt_str, struct list_head *first)
{
  int32_t max_pri = 0;
  char *tmp_str = NULL;
  char *tmp_str1 = NULL;
  char *tmp_str2 = NULL;
  char *dup_str = NULL;
  char *stripe_str = NULL;
  char *pattern = NULL;
  char *priority = NULL;
  char *string = strdup (opt_str);
  struct ioc_priority *curr = NULL;

  /* Get the pattern for cache priority. "option priority *.jpg:1,abc*:2" etc */
  /* TODO: inode_lru in table is statically hard-coded to 5, should be changed to 
   *      run-time configuration */
  stripe_str = strtok_r (string, ",", &tmp_str);
  while (stripe_str) {
    curr = calloc (1, sizeof (struct ioc_priority));
    ERR_ABORT (curr);
    list_add_tail (&curr->list, first);

    dup_str = strdup (stripe_str);
    pattern = strtok_r (dup_str, ":", &tmp_str1);
    if (!pattern)
      return -1;
    priority = strtok_r (NULL, ":", &tmp_str1);
    if (!priority)
      return -1;
    gf_log ("io-cache", 
	    GF_LOG_DEBUG, 
	    "ioc priority : pattern %s : priority %s", 
	    pattern,
	    priority);
    curr->pattern = strdup (pattern);
    curr->priority = strtol (priority, &tmp_str2, 0);
    if (tmp_str2 && (*tmp_str2))
      return -1;
    else
      max_pri = max (max_pri, curr->priority);
    stripe_str = strtok_r (NULL, ",", &tmp_str);
  }

  return max_pri;
}

/*
 * init - 
 * @this:
 *
 */
int32_t 
init (xlator_t *this)
{
  ioc_table_t *table;
  dict_t *options = this->options;
  uint32_t index = 0;

  if (!this->children || this->children->next) {
    gf_log (this->name, GF_LOG_ERROR,
	    "FATAL: io-cache not configured with exactly one child");
    return -1;
  }

  table = (void *) calloc (1, sizeof (*table));
  ERR_ABORT (table);

  table->xl = this;
  table->page_size = IOC_PAGE_SIZE;
  table->cache_size = IOC_CACHE_SIZE;

  if (dict_get (options, "page-size")) {
    table->page_size = gf_str_to_long_long (data_to_str (dict_get (options,
								   "page-size")));
    gf_log (this->name, GF_LOG_DEBUG,
	    "Using table->page_size = 0x%x",
	    table->page_size);
  }

  if (dict_get (options, "cache-size")) {
    table->cache_size = gf_str_to_long_long (data_to_str (dict_get (options,
								      "cache-size")));
    gf_log (this->name, GF_LOG_DEBUG,
	    "Using table->cache_size = 0x%x",
	    table->cache_size);
  }

  table->force_revalidate_timeout = 1;

  if (dict_get (options, "force-revalidate-timeout")) {
    table->force_revalidate_timeout = data_to_uint32 (dict_get (options,
								"force-revalidate-timeout"));
    gf_log (this->name, GF_LOG_DEBUG,
	    "Using %d seconds to force revalidate cache",
	    table->force_revalidate_timeout);
  }

  INIT_LIST_HEAD (&table->priority_list);
  if (dict_get (options, "priority")) {
    char *option_list = data_to_str (dict_get (options, "priority"));
    gf_log (this->name,
	    GF_LOG_DEBUG,
	    "option path %s", option_list);
    /* parse the list of pattern:priority */
    table->max_pri = ioc_get_priority_list (option_list, &table->priority_list);
    
    if (table->max_pri == -1)
      return -1;
  }
  table->max_pri ++;
  INIT_LIST_HEAD (&table->inodes);
  
  table->inode_lru = calloc (table->max_pri, sizeof (struct list_head));
  ERR_ABORT (table->inode_lru);
  for (index = 0; index < (table->max_pri); index++)
    INIT_LIST_HEAD (&table->inode_lru[index]);

  pthread_mutex_init (&table->table_lock, NULL);
  this->private = table;
  return 0;
}

/*
 * fini -
 * 
 * @this:
 *
 */
void
fini (xlator_t *this)
{
  ioc_table_t *table = this->private;

  pthread_mutex_destroy (&table->table_lock);
  FREE (table);

  this->private = NULL;
  return;
}

struct xlator_fops fops = {
  .open        = ioc_open,
  .create      = ioc_create,
  .readv       = ioc_readv,
  .writev      = ioc_writev,
  .close       = ioc_close,
  .truncate    = ioc_truncate,
  .ftruncate   = ioc_ftruncate,
  .forget      = ioc_forget,
  .utimens     = ioc_utimens,
  .lookup      = ioc_lookup,
  .lk          = ioc_lk
};

struct xlator_mops mops = {
};
