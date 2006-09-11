/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
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
#include "write-aggregate.h"
#include "dict.h"
#include "xlator.h"

#define DEFAULT_BUFFER_SIZE	65536
static int buffer_size;

static int
flush_buffer (struct file_context *ctx, struct xlator *this, const char *path)
{
  write_buf_t *write_buf = (write_buf_t *) ctx->context;
  int ret = 0;
  if (!(write_buf->flushed)) {
    ret = this->first_child->fops->write (this->first_child, path, write_buf->buf, 
					  write_buf->size, write_buf->offset, ctx);
    if (ret != write_buf->size)
      return -1;
  }

  write_buf->flushed = 1;
  write_buf->offset = -1;
  write_buf->size = 0;

  return ret;
}

static int
write_aggregate_open (struct xlator *this,
		      const char *path,
		      int flags,
		      mode_t mode,
		      struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  struct file_context *my_ctx = calloc (1, sizeof (struct file_context));
  my_ctx->volume = this;
  my_ctx->next = ctx->next;
  ctx->next = my_ctx;

  my_ctx->context = malloc (sizeof (write_buf_t));
  write_buf_t *write_buf = (write_buf_t *)my_ctx->context;
  write_buf->buf = malloc (buffer_size);
  write_buf->offset = -1;
  write_buf->size = 0;
  write_buf->flushed = 1;

  if (this->first_child)
    return this->first_child->fops->open (this->first_child, path, flags, mode, ctx);
  else {
    free (write_buf->buf);
    free (my_ctx->context);
    free (my_ctx);
  }

  return -1;
}

static int
write_aggregate_read (struct xlator *this,
		      const char *path,
		      char *buf,
		      size_t size,
		      off_t offset,
		      struct file_context *ctx)		      
{
  struct file_context *my_ctx;
  FILL_MY_CTX (my_ctx, ctx, this);

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (buf);
  GF_ERROR_IF_NULL (ctx);
  GF_ERROR_IF_NULL (my_ctx);

  int ret = flush_buffer (my_ctx, this, path);
  if (ret == -1) {
    return -1;
  }

  GF_ERROR_IF_NULL (this->first_child);
  return this->first_child->fops->read (this->first_child, path, buf, size, offset, ctx);
}

static int
write_aggregate_write (struct xlator *this,
		       const char *path,
		       const char *buf,
		       size_t size,
		       off_t offset,
		       struct file_context *ctx)
{
  if (!this || !path || !buf || (size < 1) || !ctx || !ctx->context)
    return -1;

  write_buf_t *write_buf = (write_buf_t *) ctx->context;
  if ((offset != (write_buf->offset + write_buf->size + 1)) && !write_buf->flushed) {
    /* The write is not sequential, flush the buffer first */
    int ret = flush_buffer (ctx, this, path);
    if (ret == -1)
      return -1;
    return write_aggregate_write (this, path, buf, size, offset, ctx);
  }
  else if ((buffer_size - write_buf->size) < size) {
    /* Not enough space, flush it */
    int ret = flush_buffer (ctx, this, path);
    if (ret == -1)
      return -1;
    return write_aggregate_write (this, path, buf, size, offset, ctx);
  }
  else {
    /* Write it to the buf */
    write_buf->flushed = 0;
    if (write_buf->offset == -1) {
      write_buf->offset = offset;
    }
    memcpy (write_buf->buf + size, buf, size);
    write_buf->size += size;
    return size;
  }

  return size;
}

static int
write_aggregate_release  (struct xlator *this,
			  const char *path,
			  struct file_context *ctx)
{
  if (!this || !path || !ctx || !ctx->context)
    return -1;

  write_buf_t *write_buf = (write_buf_t *)ctx->context;

  int retval = flush_buffer (ctx, this, path);
  if (retval == -1)
      goto ret;

  if (this->first_child)
    retval = this->first_child->fops->release (this->first_child, path, ctx);

 ret:  
  free (write_buf->buf);
  free (write_buf);
  return retval;
}

static int
write_aggregate_flush (struct xlator *this,
		       const char *path,
		       struct file_context *ctx)
{
  if (!this || !path || !ctx || !ctx->context)
    return -1;

  int ret = flush_buffer (ctx, this, path);
  if (ret == -1)
    return -1;

  if (this->first_child)
    return this->first_child->fops->flush (this->first_child, path, ctx);

  return 0;
}

static int
write_aggregate_fsync (struct xlator *this,
		       const char *path,
		       int datasync,
		       struct file_context *ctx)
{
  if (!this || !path || !ctx || !ctx->context)
    return -1;

  int ret = flush_buffer (ctx, this, path);
  if (ret == -1)
    return -1;

  if (this->first_child)
    return this->first_child->fops->fsync (this->first_child, path, datasync, ctx);

  return 0;
}

int
init (struct xlator *this)
{
  data_t *buf_size = dict_get (this->options, "buffer-size");
  if (buf_size)
    buffer_size = data_to_int (buf_size);
  else 
    buffer_size = DEFAULT_BUFFER_SIZE;

  gf_log ("write-aggregate", GF_LOG_NORMAL, "initialized with buffer size of %d\n", buffer_size);
  gf_log ("write-aggregate", GF_LOG_NORMAL, "initialized with buffer size of %d\n", buffer_size);
  return 0;
}

void
fini (struct xlator *this)
{
  return;
}

struct xlator_fops fops = {
  .open        = write_aggregate_open,
  .read	       = write_aggregate_read,
  .write       = write_aggregate_write,
  .release     = write_aggregate_release,
  .flush       = write_aggregate_flush,
  .fsync       = write_aggregate_fsync,
};

struct xlator_mgmt_ops mgmt_ops = {
};
