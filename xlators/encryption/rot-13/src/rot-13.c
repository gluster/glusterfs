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

#include <ctype.h>
#include <sys/uio.h>

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"

/*
 * This is a rot13 ``encryption'' xlator. It rot13's data when 
 * writing to disk and rot13's it back when reading it. 
 * This xlator is meant as an example, not for production
 *  use ;) (hence no error-checking)
 */

/* We only handle lower case letters for simplicity */
static void 
rot13 (char *buf, int len)
{
  int i;
  for (i = 0; i < len; i++) {
    if (buf[i] >= 'a' && buf[i] <= 'z')
      buf[i] = 'a' + ((buf[i] - 'a' + 13) % 26);
    else if (buf[i] >= 'A' && buf[i] <= 'Z')
      buf[i] = 'A' + ((buf[i] - 'A' + 13) % 26);
  }
}

static void
rot13_iovec (struct iovec *vector, int count)
{
  int i;
  for (i = 0; i < count; i++) {
    rot13 (vector[i].iov_base, vector[i].iov_len);
  }
}

static int32_t
rot13_readv_cbk (call_frame_t *frame,
                 call_frame_t *prev_frame,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 struct iovec *vector,
                 int32_t count)
{
  rot13_iovec (vector, count);

  STACK_UNWIND (frame, op_ret, op_errno, vector, count);
  return 0;
}

static int32_t
rot13_readv (call_frame_t *frame,
             xlator_t *this,
             dict_t *ctx,
             size_t size,
             off_t offset)
{
  STACK_WIND (frame,
              rot13_readv_cbk,
              FIRST_CHILD (this),
              FIRST_CHILD (this)->fops->readv,
              ctx, size, offset);
  return 0;
}

static int32_t
rot13_writev_cbk (call_frame_t *frame,
                  call_frame_t *prev_frame,
                  xlator_t *this,
                  int32_t op_ret,
                  int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
rot13_writev (call_frame_t *frame,
              xlator_t *this,
              dict_t *ctx,
              struct iovec *vector,
              int32_t count, 
              off_t offset)
{
  rot13_iovec (vector, count);

  STACK_WIND (frame, 
              rot13_writev_cbk,
              FIRST_CHILD (this),
              FIRST_CHILD (this)->fops->writev,
              ctx, vector, count, offset);
  return 0;
}

int32_t
init (xlator_t *this)
{
  if (!this->children) {
    gf_log ("rot13", GF_LOG_ERROR, 
            "FATAL: rot13 should have exactly one child");
    return -1;
  }

  gf_log ("rot13", GF_LOG_DEBUG, "rot13 xlator loaded");
  return 0;
}

void 
fini (xlator_t *this)
{
  return;
}

struct xlator_fops fops = {
  .readv        = rot13_readv,
  .writev       = rot13_writev
};

struct xlator_mops mops = {
};
