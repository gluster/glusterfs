/*
  Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include <ctype.h>
#include <sys/uio.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"

#include "rot-13.h"

/*
 * This is a rot13 ``encryption'' xlator. It rot13's data when 
 * writing to disk and rot13's it back when reading it. 
 * This xlator is meant as an example, NOT FOR PRODUCTION
 * USE ;) (hence no error-checking)
 */

void 
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

void
rot13_iovec (struct iovec *vector, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		rot13 (vector[i].iov_base, vector[i].iov_len);
	}
}

int32_t
rot13_readv_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 struct iovec *vector,
                 int32_t count,
		 struct stat *stbuf)
{
	rot_13_private_t *priv = (rot_13_private_t *)this->private;
  
	if (priv->decrypt_read)
		rot13_iovec (vector, count);

	STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf);
	return 0;
}

int32_t
rot13_readv (call_frame_t *frame,
             xlator_t *this,
             fd_t *fd,
             size_t size,
             off_t offset)
{
	STACK_WIND (frame,
		    rot13_readv_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->readv,
		    fd, size, offset);
	return 0;
}

int32_t
rot13_writev_cbk (call_frame_t *frame,
                  void *cookie,
                  xlator_t *this,
                  int32_t op_ret,
                  int32_t op_errno,
		  struct stat *stbuf)
{
	STACK_UNWIND (frame, op_ret, op_errno, stbuf);
	return 0;
}

int32_t
rot13_writev (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd,
              struct iovec *vector,
              int32_t count, 
              off_t offset)
{
	rot_13_private_t *priv = (rot_13_private_t *)this->private;
	if (priv->encrypt_write)
		rot13_iovec (vector, count);

	STACK_WIND (frame, 
		    rot13_writev_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->writev,
		    fd, vector, count, offset);
	return 0;
}

int32_t
init (xlator_t *this)
{
	data_t *data = NULL;
	rot_13_private_t *priv = NULL;

	if (!this->children || this->children->next) {
		gf_log ("rot13", GF_LOG_ERROR, 
			"FATAL: rot13 should have exactly one child");
		return -1;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}
  
	priv = CALLOC (sizeof (rot_13_private_t), 1);
	ERR_ABORT (priv);
	priv->decrypt_read = 1;
	priv->encrypt_write = 1;

	data = dict_get (this->options, "encrypt-write");
	if (data) {
		if (gf_string2boolean (data->data, &priv->encrypt_write) == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"encrypt-write takes only boolean options");
			return -1;
		}
	}

	data = dict_get (this->options, "decrypt-read");
	if (data) {
		if (gf_string2boolean (data->data, &priv->decrypt_read) == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"decrypt-read takes only boolean options");
			return -1;
		}
	}

	this->private = priv;
	gf_log ("rot13", GF_LOG_DEBUG, "rot13 xlator loaded");
	return 0;
}

void 
fini (xlator_t *this)
{
	rot_13_private_t *priv = this->private;
	
	FREE (priv);
	
	return;
}

struct xlator_fops fops = {
	.readv        = rot13_readv,
	.writev       = rot13_writev
};

struct xlator_mops mops = {
};


struct xlator_options options[] = {
	{ "encrypt-write", GF_OPTION_TYPE_BOOL, 0,  },
	{ "decrypt-read", GF_OPTION_TYPE_BOOL, 0,  },
	{ NULL, 0, },
};
