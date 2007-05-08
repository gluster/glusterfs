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

#include <unistd.h>
#include <sys/uio.h>

#include "xlator.h"
#include "meta.h"

#define min(x,y)   ((x) < (y) ? (x) : (y))

/* /.meta/version */
static const char *version_str = PACKAGE_NAME " " PACKAGE_VERSION "\n";

int32_t
meta_version_readv (call_frame_t *frame, xlator_t *this,
		    dict_t *fd, size_t size, off_t offset)
{
  static int version_size;
  version_size = strlen (version_str);
  
  struct iovec vec;
  vec.iov_base = version_str + offset;
  vec.iov_len  = min (version_size - offset, size);

  STACK_UNWIND (frame, vec.iov_len, 0, &vec, 1);
  return 0;
}

int32_t
meta_version_getattr (call_frame_t *frame, 
			      xlator_t *this,
			      const char *path)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  meta_dirent_t *file = lookup_meta_entry (root, path, NULL);
  file->stbuf->st_size = strlen (version_str);
  STACK_UNWIND (frame, 0, 0, file->stbuf);
}

struct xlator_fops meta_version_fops = {
  .readv   = meta_version_readv,
  .getattr = meta_version_getattr
};

