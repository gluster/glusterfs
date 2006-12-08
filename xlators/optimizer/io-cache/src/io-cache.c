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

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-cache.h"
#include <assert.h>

static void
read_ahead (call_frame_t *frame,
	    io_cache_file_t *file);

static int32_t
io_cache_read_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   char *buf);

static int32_t
io_cache_open_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   dict_t *file_ctx,
		   struct stat *buf)
{
  io_cache_local_t *local = frame->local;
  io_cache_conf_t *conf = this->private;

  if (op_ret != -1) {
    io_cache_file_t *file = calloc (1, sizeof (*file));

    file->file_ctx = file_ctx;
    file->filename = strdup (local->filename);

    dict_set (file_ctx,
	      this->name,
	      int_to_data ((long) io_cache_file_ref (file)));

    file->offset = (unsigned long long) -1;
    file->size = 0;
    file->conf = conf;
    file->pages.next = &file->pages;
    file->pages.prev = &file->pages;
    file->pages.offset = (unsigned long) -1;
    file->pages.file = file;

    file->next = conf->files.next;
    conf->files.next = file;
    file->next->prev = file;
    file->prev = &conf->files;

    read_ahead (frame, file);
  }

  free (local->filename);
  free (local);
  frame->local = NULL;

  STACK_UNWIND (frame, op_ret, op_errno, file_ctx, buf);

  return 0;
}

static int32_t
io_cache_open (call_frame_t *frame,
	       xlator_t *this,
	       const char *pathname,
	       int32_t flags,
	       mode_t mode)
{
  io_cache_local_t *local = calloc (1, sizeof (*local));

  local->mode = mode;
  local->flags = flags;
  local->filename = strdup (pathname);
  frame->local = local;

  STACK_WIND (frame,
	      io_cache_open_cbk,
	      this->first_child,
	      this->first_child->fops->open,
	      pathname,
	      flags,
	      mode);

  return 0;
}

static int32_t
flush_cbk (call_frame_t *frame,
	   call_frame_t *prev_frame,
	   xlator_t *this,
	   int32_t op_ret,
	   int32_t op_errno)
{
  STACK_DESTROY (frame->root);
  return 0;
}

/* free cache pages between offset and offset+size,
   does not touch pages with frames waiting on it
*/
static void
flush_region (call_frame_t *frame,
	      io_cache_file_t *file,
	      off_t offset,
	      size_t size)
{
  io_cache_page_t *trav = file->pages.next;
  io_cache_conf_t *conf = file->conf;

  while (trav != &file->pages && trav->offset < (offset + size)) {
    io_cache_page_t *next = trav->next;
    if (trav->offset >= offset && !trav->waitq) {
      trav->prev->next = trav->next;
      trav->next->prev = trav->prev;

      if (trav->dirty && file->file_ctx) {
	call_frame_t *flush_frame = copy_frame (frame);

	flush_frame->local = NULL;

	STACK_WIND (flush_frame,
		    flush_cbk,
		    flush_frame->this->first_child,
		    flush_frame->this->first_child->fops->write,
		    file->file_ctx,
		    trav->ptr,
		    conf->page_size,
		    trav->offset);
      }
      io_cache_purge_page (trav);
    }
    trav = next;
  }
}

static int32_t
io_cache_release_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  frame->local = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
io_cache_release (call_frame_t *frame,
		  xlator_t *this,
		  dict_t *file_ctx)
{
  io_cache_file_t *file;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));

  flush_region (frame, file, 0, file->pages.next->offset);
  dict_del (file_ctx, this->name);

  io_cache_file_unref (file);
  file->file_ctx = NULL;

  STACK_WIND (frame,
	      io_cache_release_cbk,
	      this->first_child,
	      this->first_child->fops->release,
	      file_ctx);
  return 0;
}


static int32_t
io_cache_read_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   char *buf)
{
  io_cache_local_t *local = frame->local;
  off_t pending_offset = local->pending_offset;
  off_t pending_size = local->pending_size;
  io_cache_file_t *file = local->file;
  io_cache_conf_t *conf = file->conf;
  io_cache_page_t *trav;
  off_t trav_offset;
  size_t payload_size;


  trav_offset = pending_offset;  
  payload_size = op_ret;

  if (op_ret < 0) {
    while (trav_offset < (pending_offset + pending_size)) {
      trav = io_cache_get_page (file, trav_offset);
      if (trav)
	io_cache_error_page (trav, op_ret, op_errno);
      trav_offset += conf->page_size;
    }
  } else {
    /* read region */
    while (trav_offset < (pending_offset + payload_size)) {
      trav = io_cache_get_page (file, trav_offset);
      if (!trav) {
	/* page was flushed */
	/* some serious bug ? */
	//	trav = io_cache_create_page (file, trav_offset);
	gf_log ("io-cache",
		GF_LOG_DEBUG,
		"wasted copy: %lld[+%d]", trav_offset, conf->page_size);
	trav_offset += conf->page_size;
	continue;
      }
      if (trav->dirty) {
	/* this region was write()n before the read reply arrived */
	trav_offset += conf->page_size;
	continue;
      }
      if (!trav->ptr) {
	/*
	trav->ptr = malloc (conf->page_size);
	memcpy (trav->ptr,
		&buf[trav_offset-pending_offset],
		min (pending_offset+payload_size-trav_offset,
		     conf->page_size));
	*/
	trav->ptr = &buf[trav_offset-pending_offset];
	trav->ref = dict_ref (frame->root->reply);
	trav->ready = 1;
	trav->size = min (pending_offset+payload_size-trav_offset,
			  conf->page_size);
      }

      if (trav->waitq) {
	io_cache_wakeup_page (trav);
      }
      trav_offset += conf->page_size;
    }

    /* region which was not copied, (beyond end of file)
       flush to avoid -ve cache */
    while (trav_offset < (pending_offset + pending_size)) {
      trav = io_cache_get_page (file, trav_offset);
      if (trav) {
	/* some serious bug */

      //      if (trav->waitq)
	trav->size = 0;
	trav->ready = 1;
	if (trav->waitq) {
	  gf_log ("io-cache",
		  GF_LOG_DEBUG,
		  "waking up from the left");
	  io_cache_wakeup_page (trav);
	}
	//	io_cache_flush_page (trav);
      }
      trav_offset += conf->page_size;
    }
  }

  io_cache_file_unref (local->file);
  free (frame->local);
  frame->local = NULL;
  STACK_DESTROY (frame->root);
  return 0;
}

static void
read_ahead (call_frame_t *frame,
	    io_cache_file_t *file)
{
  io_cache_local_t *local = frame->local;
  io_cache_conf_t *conf = file->conf;

  off_t ra_offset;
  size_t ra_size;
  size_t chunk_size;
  off_t trav_offset;
  io_cache_page_t *trav;

  int32_t i;

  ra_offset = roof (local->offset + local->size, conf->page_size);
  ra_size = roof (local->size, conf->page_size);
  ra_size = conf->page_size * conf->page_count;

  trav_offset = ra_offset;

  trav = file->pages.next;

  while (trav_offset < (ra_offset + ra_size)) {
    /*
    while (trav != &file->pages && trav->offset < trav_offset)
      trav = trav->next;

    if (trav->offset != trav_offset) {
    */
    trav = io_cache_get_page (file, trav_offset);
    if (!trav) {
      trav = io_cache_create_page (file, trav_offset);

      call_frame_t *ra_frame = copy_frame (frame);
      io_cache_local_t *ra_local = calloc (1, sizeof (io_cache_local_t));
    
      ra_frame->local = ra_local;
      ra_local->pending_offset = trav->offset;
      ra_local->pending_size = conf->page_size;
      ra_local->file = io_cache_file_ref (file);

      /*
      gf_log ("io-cache",
	      GF_LOG_DEBUG,
	      "RA: %lld[+%d]", trav_offset, conf->page_size); 
      */
      STACK_WIND (ra_frame,
		  io_cache_read_cbk,
		  ra_frame->this->first_child,
		  ra_frame->this->first_child->fops->read,
		  file->file_ctx,
		  conf->page_size,
		  trav_offset);
    }
    trav_offset += conf->page_size;
  }
  return ;
}

static void
dispatch_requests (call_frame_t *frame,
		   io_cache_file_t *file)
{
  io_cache_local_t *local = frame->local;
  io_cache_conf_t *conf = file->conf;
  off_t rounded_offset;
  off_t rounded_end;
  off_t trav_offset;
  /*
  off_t dispatch_offset;
  size_t dispatch_size;
  char dispatch_found = 0;
  */
  io_cache_page_t *trav;

  rounded_offset = floor (local->offset, conf->page_size);
  rounded_end = roof (local->offset + local->size, conf->page_size);

  trav_offset = rounded_offset;

  /*
  dispatch_offset = 0;
  dispatch_size = 0;
  */

  trav = file->pages.next;

  while (trav_offset < rounded_end) {
    /*
    while (trav != &file->pages && trav->offset < trav_offset)
      trav = trav->next;
    if (trav->offset != trav_offset) {
      io_cache_page_t *newpage = calloc (1, sizeof (*trav));
      newpage->offset = trav_offset;
      newpage->file = file;
      newpage->next = trav;
      newpage->prev = trav->prev;
      newpage->prev->next = newpage;
      newpage->next->prev = newpage;
      trav = newpage;
    */
    trav = io_cache_get_page (file, trav_offset);
    if (!trav) {
      trav = io_cache_create_page (file, trav_offset);

      call_frame_t *worker_frame = copy_frame (frame);
      io_cache_local_t *worker_local = calloc (1, sizeof (io_cache_local_t));

      /*
      gf_log ("io-cache",
	      GF_LOG_DEBUG,
	      "MISS: region: %lld[+%d]", trav_offset, conf->page_size);
      */
      worker_frame->local = worker_local;
      worker_local->pending_offset = trav_offset;
      worker_local->pending_size = conf->page_size;
      worker_local->file = io_cache_file_ref (file);

      STACK_WIND (worker_frame,
		  io_cache_read_cbk,
		  worker_frame->this->first_child,
		  worker_frame->this->first_child->fops->read,
		  file->file_ctx,
		  conf->page_size,
		  trav_offset);

    /*
      if (!dispatch_found) {
	dispatch_found = 1;
	dispatch_offset = trav_offset;
      }
      dispatch_size = (trav->offset - dispatch_offset + conf->page_size);
    */
    }
    if (trav->ready) {
      io_cache_fill_frame (trav, frame);
    } else {
      /*
      gf_log ("io-cache",
	      GF_LOG_DEBUG,
	      "Might catch...?");
      */
      io_cache_wait_on_page (trav, frame);
    }

    trav_offset += conf->page_size;
  }

  /*
  if (dispatch_found) {
    call_frame_t *worker_frame = copy_frame (frame);
    io_cache_local_t *worker_local = calloc (1, sizeof (io_cache_local_t));
    
    gf_log ("io-cache",
	    GF_LOG_DEBUG,
	    "MISS: region: %lld[+%d]", dispatch_offset, dispatch_size);
    worker_frame->local = worker_local;
    worker_local->pending_offset = dispatch_offset;
    worker_local->pending_size = dispatch_size;
    worker_local->file = io_cache_file_ref (file);

    STACK_WIND (worker_frame,
		io_cache_read_cbk,
		worker_frame->this->first_child,
		worker_frame->this->first_child->fops->read,
		file->file_ctx,
		dispatch_size,
		dispatch_offset);
  }
  */
  return ;
}


static int32_t
io_cache_read (call_frame_t *frame,
	       xlator_t *this,
	       dict_t *file_ctx,
	       size_t size,
	       off_t offset)
{
  /* TODO: do something about atime update on server */
  io_cache_file_t *file;
  io_cache_local_t *local;
  io_cache_conf_t *conf;

  /*
  gf_log ("io-cache",
	  GF_LOG_DEBUG,
	  "read: %lld[+%d]", offset, size);
  */
  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  conf = file->conf;

  local = (void *) calloc (1, sizeof (*local));
  local->ptr = calloc (size, 1);
  local->offset = offset;
  local->size = size;
  local->file = io_cache_file_ref (file);
  local->wait_count = 1; /* for synchronous STACK_UNWIND from protocol
			    in case of error */
  frame->local = local;


  dispatch_requests (frame, file);


  flush_region (frame, file, 0, floor (offset, conf->page_size));

  if (local->wait_count != 1)
    read_ahead (frame, file);

  local->wait_count--;
  if (!local->wait_count) {
    /*     gf_log ("io-cache",
	    GF_LOG_DEBUG,
	    "HIT for %lld[+%d]", offset, size); 
    */
    /* CACHE HIT */
    frame->local = NULL;
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->ptr);
    io_cache_file_unref (local->file);
    if (!local->is_static)
      free (local->ptr);
    free (local);
  } else {
    /*
    gf_log ("io-cache",
	    GF_LOG_DEBUG,
	    "ALMOST HIT for %lld[+%d]", offset, size);
    */
    /* ALMOST HIT (read-ahead data already on way) */
  }

  return 0;
}

int32_t 
init (struct xlator *this)
{
  io_cache_conf_t *conf;
  dict_t *options = this->options;

  if (!this->first_child || this->first_child->next_sibling) {
    gf_log ("io-cache",
	    GF_LOG_ERROR,
	    "FATAL: io-cache not configured with exactly one child");
    return -1;
  }

  conf = (void *) calloc (1, sizeof (*conf));
  conf->page_size = 1024 * 64;
  conf->page_count = 16;

  if (dict_get (options, "page-size")) {
    conf->page_size = data_to_int (dict_get (options,
					     "page-size"));
    gf_log ("io-cache",
	    GF_LOG_DEBUG,
	    "Using conf->page_size = 0x%x",
	    conf->page_size);
  }

  if (dict_get (options, "page-count")) {
    conf->page_count = data_to_int (dict_get (options,
					      "page-count"));
    gf_log ("io-cache",
	    GF_LOG_DEBUG,
	    "Using conf->page_count = 0x%x",
	    conf->page_count);
  }

  conf->files.next = &conf->files;
  conf->files.prev = &conf->files;

  /*
  conf->cache_block = malloc (conf->page_size * conf->page_count);
  conf->pages = (void *) calloc (conf->page_count,
				 sizeof (struct io_cache_page));

  {
    int i;

    for (i=0; i<conf->page_count; i++) {
      if (i < (conf->page_count - 2))
      	conf->pages[i].next = &conf->pages[i+1];
      conf->pages[i].ptr = conf->cache_block + (i * conf->page_size);
    }
  }
  */
  this->private = conf;
  return 0;
}

void
fini (struct xlator *this)
{
  io_cache_conf_t *conf = this->private;

  //  free (conf->cache_block);
  //  free (conf->pages);
  free (conf);

  this->private = NULL;
  return;
}

struct xlator_fops fops = {
  .open        = io_cache_open,
  .read        = io_cache_read,
  /*  .write       = io_cache_write,*/ /*
  .flush       = io_cache_flush,
  .fsync       = io_cache_fsync, */
  .release     = io_cache_release,
};

struct xlator_mops mops = {
};
