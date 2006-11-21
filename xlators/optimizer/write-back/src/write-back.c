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

static int32_t
write_back_write_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  dict_t *file_ctx = frame->local;

  frame->local = NULL;
  if (op_ret == -1)
    /* for delayed error deliver, mark the file context with errno */
    dict_set (file_ctx, this->name, int_to_data (op_errno));

  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t 
write_back_write (call_frame_t *frame,
		  xlator_t *this,
		  dict_t *file_ctx,
		  char *buf,
		  size_t size,
		  off_t offset)
{
  data_t *error_data = dict_get (file_ctx, this->name);
  call_frame_t *through_frame;

  if (error_data) {
    /* delayed error delivery */
    int32_t op_errno = data_to_int (error_data);

    dict_del (file_ctx, this->name);
    STACK_UNWIND (frame, -1, op_errno);
    return 0;
  }

  through_frame = copy_frame (frame);
  through_frame->local = file_ctx;

  STACK_UNWIND (frame, size, 0); /* liar! liar! :O */

  STACK_WIND (through_frame,
	      write_back_write_cbk,
	      this->first_child,
	      this->first_child->fops->write,
	      file_ctx,
	      buf,
	      size,
	      offset);
  return 0;
}



int32_t 
init (struct xlator *this)
{
  if (!this->first_child || this->first_child->next_sibling) {
    gf_log ("write-back",
	    GF_LOG_ERROR,
	    "FATAL: write-back not configured with exactly one child");
    return -1;
  }
  return 0;
}

void
fini (struct xlator *this)
{
  return;
}

struct xlator_fops fops = {
  .write       = write_back_write,
};

struct xlator_mops mops = {
};
