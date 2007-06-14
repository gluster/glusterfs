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

/* 
   TODO:
   - handle O_DIRECT
   - maintain offset, flush on lseek
   - ensure efficient memory managment in case of random seek
*/

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-cache.h"
#include <assert.h>
#include <sys/time.h>


static int32_t
ioc_forget_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
ioc_forget (call_frame_t *frame,
	    xlator_t *this,
	    inode_t *inode)
{
  data_t *ioc_inode_data = dict_get (inode->ctx, this->name);
  char *ioc_inode_str = NULL;
  ioc_inode_t *ioc_inode = NULL;

  if (ioc_inode_data) {
    ioc_inode_str = data_to_str (ioc_inode_data);
    ioc_inode = str_to_ptr (ioc_inode_str);
    ioc_inode_destroy (ioc_inode);
  }

  STACK_WIND (frame,
	      ioc_forget_cbk,
	      FIRST_CHILD (frame->this),
	      FIRST_CHILD (frame->this)->fops->forget,
	      inode);
  
  return 0;
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
static int32_t
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
  data_t *ioc_inode_data = NULL;
  char *ioc_inode_str = NULL;

  if (op_ret != -1) {
    /* search for inode corresponding to this fd */
    ioc_inode_data = dict_get (fd->inode->ctx, this->name);
    if (!ioc_inode_data) {
      /* this is the first time someone is opening this file */
      ioc_inode = ioc_inode_update (table, fd->inode);
      ioc_inode_str = ptr_to_str (ioc_inode);
      dict_set (fd->inode->ctx, this->name, str_to_data (ioc_inode_str));
    } else {
      ioc_inode_str = data_to_str (ioc_inode_data);
      ioc_inode = str_to_ptr (ioc_inode_str);
      ioc_inode = ioc_inode_ref (ioc_inode);
    }

    /* If O_DIRECT open, we disable caching on it */
    if ((local->flags & O_DIRECT) || (local->flags & O_WRONLY))
      ioc_inode->disabled = 1;
  }

  free (local);
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
static int32_t
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
  data_t *ioc_inode_data = NULL;
  char *ioc_inode_str = NULL;

  if (op_ret != -1) {
    ioc_inode_data = dict_get (fd->inode->ctx, this->name);
    
    if (!ioc_inode) {
      ioc_inode = ioc_inode_update (table, fd->inode);
      ioc_inode_str = ptr_to_str (ioc_inode);
      dict_set (fd->inode->ctx, this->name, str_to_data (ioc_inode_str));
    } else {
      ioc_inode_str = data_to_str (ioc_inode_data);
      ioc_inode = str_to_ptr (ioc_inode_str);
      ioc_inode = ioc_inode_ref (ioc_inode);
    }
    /* If O_DIRECT open, we disable caching on it */
    if ((local->flags & O_DIRECT) || (local->flags & O_WRONLY))
      ioc_inode->disabled = 1;
    
  }

  free (local);
  frame->local = NULL;

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
static int32_t
ioc_open (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  int32_t flags)
{
  
  ioc_local_t *local = calloc (1, sizeof (ioc_local_t));

  
  local->flags = flags;
  frame->local = local;

  STACK_WIND (frame,
	      ioc_open_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->open,
	      loc,
	      flags);

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
static int32_t
ioc_create (call_frame_t *frame,
	    xlator_t *this,
	    const char *pathname,
	    int32_t flags,
	    mode_t mode)
{
  ioc_local_t *local = calloc (1, sizeof (ioc_local_t));

  local->flags = flags;
  frame->local = local;

  STACK_WIND (frame,
	      ioc_create_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->create,
	      pathname,
	      flags,
	      mode);

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
static int32_t
ioc_close_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static void
ioc_dump_inode (ioc_inode_t *ioc_inode)
{
  int32_t fd = open ("/root/glusterfs-test.txt", O_WRONLY);
  ioc_page_t *curr = NULL;

  if (fd < 0) {
    perror ("io-cache");
    gf_log ("io-cache",
	    GF_LOG_DEBUG,
	    "failed to open glusterfs-test.txt");
  }
  list_for_each_entry (curr, &ioc_inode->pages, pages) {
    writev (fd, curr->vector, curr->count);
  }
  
  close (fd);
}

/*
 * ioc_close - close fop for io cache
 * 
 * @frame:
 * @this:
 * @fd:
 *
 */
static int32_t
ioc_close (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{

  ioc_inode_t *ioc_inode= NULL;
  char *ioc_inode_str = NULL;
    
  ioc_inode_str = data_to_str (dict_get (fd->inode->ctx, this->name));
  ioc_inode = str_to_ptr (ioc_inode_str);

  /* we need to tell io-cache that a file is being closed and
   * references to a inode through this fd are unreferenced
   */

  ioc_inode_unref (ioc_inode);

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
static int32_t
ioc_cache_validate_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct stat *buf)
{
  ioc_local_t *local = frame->local;
  call_stub_t *readv_stub = local->stub;
  ioc_inode_t *ioc_inode = local->inode;
  
  gf_log ("io-cache",
	  GF_LOG_DEBUG,
	  "resuming from cache_validate_cbk");
  /* TODO: compare the struct stat with the stat associated with
   *       cache for this inode */
  if (buf->st_mtime != ioc_inode->stbuf.st_mtime) {
    /* file has been modified since we cached it */
    local->op_ret = -1;
  } else {
    local->op_ret = 0;
  }
  
  call_resume (readv_stub);
  local->stub = NULL;
  return 0;
}

static int32_t
ioc_cache_validate (call_frame_t *frame,
		    ioc_inode_t *ioc_inode,
		    fd_t *fd)
{
  ioc_local_t *local = frame->local;
  call_stub_t *readv_stub = local->stub;
  local->inode = ioc_inode;

  /* DEBUG: short the validate 
  local->op_ret = 0;
  call_resume (readv_stub);
  local->stub = NULL;

  return 0;
  */
  gf_log ("io-cache",
	  GF_LOG_DEBUG,
	  "doing stat");
  STACK_WIND (frame,
	      ioc_cache_validate_cbk,
	      FIRST_CHILD (frame->this),
	      FIRST_CHILD (frame->this)->fops->fstat,
	      fd);
}

/*
 * dispatch_requests -
 * 
 * @frame:
 * @inode:
 *
 * TODO: thorough review of this routine required
 */
static void
dispatch_requests (call_frame_t *frame,
		   ioc_inode_t *ioc_inode,
		   fd_t *fd)
{
  ioc_local_t *local = frame->local;
  ioc_table_t *table = ioc_inode->table;
  off_t rounded_offset = 0;
  off_t rounded_end = 0;
  off_t trav_offset = 0;
  ioc_page_t *trav = NULL;
  int32_t fault = 0;

  rounded_offset = floor (local->offset, table->page_size);
  rounded_end = roof (local->offset + local->size, table->page_size);
  trav_offset = rounded_offset;

  /* once a frame does read, it should be waiting on something */
  local->wait_count++;

  /* Requested region can fall in three different pages,
   * 1. Ready - region is already in cache, we just have to serve it.
   * 2. In-transit - page fault has been generated on this page, we need
   *    to wait till the page is ready
   * 3. Fault - page is not in cache, we have to generate a page fault
   */
  while (trav_offset < rounded_end) {
    ioc_inode_lock (ioc_inode);

    /* look for requested region in the cache */
    trav = ioc_page_get (ioc_inode, trav_offset);

    if (!trav) {
      /* page not in cache, we are ready to generate page fault */
      trav = ioc_page_create (ioc_inode, trav_offset);
      if (!trav) {
	gf_log ("io-cache",
		GF_LOG_CRITICAL,
		"ioc_page_create returned NULL");
      }
      ioc_wait_on_page (trav, frame);
      gf_log ("io-cache",
	      GF_LOG_DEBUG,
	      "generating page fault for frame = %p", frame);
      fault = 1;
    } else {
      if (trav->ready) {
	ioc_local_lock (local);
	local->wait_count++;
	ioc_local_unlock (local);

	/* TODO: frame_fill should fill from local->offset to local->size, if
	 * everthing exists in trav page. otherwise it should fill as much as
	 * possible and change the local->pending_offset and
	 * local->pending_size
	 */
	ioc_frame_fill (trav, frame);
	ioc_frame_return (frame);	
      } else {
	/* page in-transit, we will have to wait till page is ready
	 * wake_up on the page causes our frame to return
	 */
	ioc_wait_on_page (trav, frame);
      }
    }
    ioc_inode_unlock (ioc_inode);

    if (fault) {
      fault = 0;
      ioc_page_fault (ioc_inode, frame, fd, trav_offset);
    }
    trav_offset += table->page_size;
  }
  
  /* frame should always unwind from here and nowhere else */
  ioc_frame_return (frame);
  return;
}

/*
 * ioc_inode_flush - flush all the cached pages of the given inode
 *
 * @ioc_inode: 
 *
 */
void
ioc_inode_flush (ioc_inode_t *ioc_inode)
{
  ioc_page_t *curr = NULL, *prev = NULL;
  
  list_for_each_entry (curr, &ioc_inode->pages, pages) {
    if (prev) {
      ioc_page_purge (prev);
    }
    prev = curr;
  }
}

int32_t
ioc_readv_continue (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    size_t size,
		    off_t offset)
{
  ioc_local_t *local = frame->local;
  data_t *ioc_inode_data = dict_get (fd->inode->ctx, this->name);
  char *ioc_inode_str = NULL;
  ioc_inode_t *ioc_inode = NULL;
   
  ioc_inode_str = data_to_str (ioc_inode_data);
  ioc_inode = str_to_ptr (ioc_inode_str);
  
  if (local->op_ret < 0) {
    /* cache invalid, flush it */
    ioc_inode_flush (ioc_inode);
    local->op_ret = 0;
  } 

  local->offset = offset;
  local->size   = size;

  local->pending_offset = offset;
  local->pending_size   = size;

  local->inode   = ioc_inode_ref (ioc_inode);
  INIT_LIST_HEAD (&local->fill_list);
  pthread_mutex_init (&local->local_lock, NULL);

  frame->local = local;

  dispatch_requests (frame, ioc_inode, fd);

  return 0;
}

/* TODO: read returns exactly the same size as required, no matter first time or later. but the data returned
 * is corrupted after the first page in cache
 */
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
  ioc_inode_t *ioc_inode = NULL;
  ioc_local_t *local = NULL;
  ioc_table_t *table = NULL;
  data_t *ioc_inode_data = dict_get (fd->inode->ctx, this->name);
  char *ioc_inode_str = NULL;

  if (!ioc_inode_data) {
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
  ioc_inode_str = data_to_str (ioc_inode_data);
  ioc_inode = str_to_ptr (ioc_inode_str);

  if (ioc_inode->disabled) {
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

  table = ioc_inode->table;
  local = (ioc_local_t *) calloc (1, sizeof (ioc_local_t));
  frame->local = local;

  {  
    /* see if we have updated copy in cache 
     * NOTE: ioc_cache_validate is a barrier, we need to pause the flow of this function till we
     *       realise whether we have valid copy or not in the cache */
    call_stub_t *dispatch_stub = fop_readv_stub (frame,
						 ioc_readv_continue,
						 fd,
						 size,
						 offset);
    local->stub = dispatch_stub;
    
    ioc_cache_validate (frame, ioc_inode, fd);
    
  }

  return 0;
}

/*
 * ioc_flush_cbk -
 * 
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 */

static int32_t
ioc_flush_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * ioc_flush -
 *
 * @frame:
 * @this:
 * @fd:
 *
 */
static int32_t
ioc_flush (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  ioc_inode_t *inode = NULL;

  inode = data_to_ptr (dict_get (fd->inode->ctx, this->name));

  /* we need to flush the corresponding to this file, if we hold it in our cache 
  */
  STACK_WIND (frame,
	      ioc_flush_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->flush,
	      fd);
  return 0;
}

/*
 * ioc_fsync - fsync fop for io-cache
 * @frame:        call frame
 * @this:         xlator_t for io-cache
 * @fd:           file descriptor
 * @datasync:     
 *
 * io-cache flushes out any data which is in cache, 
 * corresponding to the inode which this file points to
 */
static int32_t
ioc_fsync (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   int32_t datasync)
{
  ioc_inode_t *inode = NULL;


  inode = data_to_ptr (dict_get (fd->inode->ctx, this->name));

  /* TODO: we need to flush the corresponding to this file, if we hold it in our cache 
   */

  STACK_WIND (frame,
	      ioc_flush_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->fsync,
	      fd,
	      datasync);
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
static int32_t
ioc_writev_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
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
static int32_t
ioc_writev (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    struct iovec *vector,
	    int32_t count,
	    off_t offset)
{
  ioc_inode_t *ioc_inode = NULL;


  ioc_inode = data_to_ptr (dict_get (fd->inode->ctx, this->name));

  /* we need to flush the corresponding to this file, if we hold it in our cache 
   */

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
static int32_t 
ioc_truncate_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{

  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
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
static int32_t 
ioc_truncate (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      off_t offset)
{

  /* TODO: flush the cached data for this inode 
   */

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
static int32_t
ioc_ftruncate (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       off_t offset)
{
  ioc_inode_t *ioc_inode = NULL;
  
  ioc_inode = data_to_ptr (dict_get (fd->inode->ctx, this->name));

  /* TODO: flush the data cached for this fd's inode */

  STACK_WIND (frame,
	      ioc_truncate_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->ftruncate,
	      fd,
	      offset);
  return 0;
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

  if (!this->children || this->children->next) {
    gf_log ("io-cache",
	    GF_LOG_ERROR,
	    "FATAL: io-cache not configured with exactly one child");
    return -1;
  }

  table = (void *) calloc (1, sizeof (*table));

  table->page_size = IOC_PAGE_SIZE;
  table->page_count   = IOC_PAGE_COUNT;

  if (dict_get (options, "ioc-page-size")) {
    table->page_size = data_to_int32 (dict_get (options,
					       "ioc-page-size"));
    gf_log ("io-cache",
	    GF_LOG_DEBUG,
	    "Using table->page_size = 0x%x",
	    table->page_size);
  }

  if (dict_get (options, "ioc-page-count")) {
    table->page_count = data_to_int32 (dict_get (options,
						 "ioc-page-count"));
    gf_log ("io-cache",
	    GF_LOG_DEBUG,
	    "Using table->page_count = 0x%x",
	    table->page_count);
  }

  INIT_LIST_HEAD (&table->inodes);
  INIT_LIST_HEAD (&table->inode_lru);

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
  free (table);

  this->private = NULL;
  return;
}

struct xlator_fops fops = {
  .open        = ioc_open,
  .create      = ioc_create,
  .readv       = ioc_readv,
  .writev      = ioc_writev,
  .flush       = ioc_flush,
  .fsync       = ioc_fsync,
  .close       = ioc_close,
  .truncate    = ioc_truncate,
  .ftruncate   = ioc_ftruncate,
  .forget      = ioc_forget
};

struct xlator_mops mops = {
};
