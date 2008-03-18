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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "xlator.h"
#include "dict.h"
#include "logging.h"
#include "transport.h"
#include "protocol.h"
#include "booster.h"
#include <string.h>

/* TODO:
   - make BOOSTER_LISTEN_PATH acceptable from xlator options
   - authorize transport to use an inode/fd
   - make get_frame_for_transport() library function
*/ 

struct booster_private {
  dict_t *client_options;
  dict_t *server_options;
  transport_t *transport;
};

#define BOOSTER_LISTEN_PATH  "/tmp/glusterfs-booster-server"

static call_frame_t *
get_frame_for_transport (transport_t *trans)
{
  call_ctx_t *_call = (void *) calloc (1, sizeof (*_call));
  call_pool_t *pool = trans->xl->ctx->pool;

  if (!pool) {
    pool = trans->xl->ctx->pool = calloc (1, sizeof (*pool));
    LOCK_INIT (&pool->lock);
    INIT_LIST_HEAD (&pool->all_frames);
  }

  _call->pool = pool;

  LOCK (&_call->pool->lock);
  list_add (&_call->all_frames, &_call->pool->all_frames);
  UNLOCK (&_call->pool->lock);

  _call->trans = trans;
  _call->unique = 0;           /* which call */

  _call->frames.root = _call;
  _call->frames.this = trans->xl;

  return &_call->frames;
}

int32_t
booster_getxattr_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      dict_t *dict)
{
  dict_t *ret_options = NULL;
  int len;
  char *buf;
  loc_t *loc = (loc_t *)cookie;
  char handle[20];

  if (op_ret >= 0) {
    ret_options = get_new_dict ();
    dict_copy (this->private, ret_options);

    len = dict_serialized_length (ret_options);
    buf = calloc (1, len);
    dict_serialize (ret_options, buf);

    dict_set (dict, "user.glusterfs-booster-transport-options", 
	      data_from_dynptr (buf, len));
    sprintf (handle, "%p", loc->inode);
    gf_log (this->name, GF_LOG_DEBUG, "handle is %s for inode %"PRId64,
	    handle, loc->inode->ino);
    dict_set (dict, "user.glusterfs-booster-handle",
	      data_from_dynstr (strdup (handle)));

    if (op_ret < 0)
      op_ret = 2;
    else
      op_ret += 2;
  }

  STACK_UNWIND (frame, op_ret, op_errno, dict);
  return 0;
}

int32_t
booster_getxattr (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc)
{
  STACK_WIND_COOKIE (frame, booster_getxattr_cbk,
	       loc, FIRST_CHILD (this), FIRST_CHILD (this)->fops->getxattr,
	       loc);
  return 0;
}

int32_t
booster_readv_cbk (call_frame_t *frame, 
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct iovec *vector,
		   int32_t count,
		   struct stat *stbuf)
{
  struct glusterfs_booster_protocol_header hdr = {0, };
  transport_t *trans = frame->root->trans;
  struct iovec *hdrvec;

  hdrvec = alloca (sizeof (*hdrvec) * (1 + count));
  memset (hdrvec, 0, sizeof (*hdrvec) * (1 + count));

  hdr.op_ret = op_ret;
  hdr.op_errno = op_errno;
  hdrvec[0].iov_base = &hdr;
  hdrvec[0].iov_len = sizeof (hdr);
  if (op_ret != -1)
    memcpy (&hdrvec[1], vector, sizeof (*hdrvec) * count);

  trans->ops->writev (trans, hdrvec, (count + 1));  

  //  transport_unref (trans);

  STACK_DESTROY (frame->root);
  return 0;
}

int32_t
booster_writev_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *stbuf)
{
  struct glusterfs_booster_protocol_header hdr = {0, };
  transport_t *trans = frame->root->trans;
  struct iovec hdrvec;

  hdr.op_ret = op_ret;
  hdr.op_errno = op_errno;
  hdrvec.iov_base = &hdr;
  hdrvec.iov_len = sizeof (hdr);

  trans->ops->writev (trans, &hdrvec, 1);

  //  transport_unref (trans);

  STACK_DESTROY (frame->root);
  return 0;
}


int32_t
booster_interpret (transport_t *trans)
{
  struct glusterfs_booster_protocol_header hdr;
  int ret;
  inode_t *inode;
  fd_t *fd = NULL;
  call_frame_t *frame;

  ret = trans->ops->recieve (trans, (char *) &hdr, sizeof (hdr));

  if (ret != 0)
    return -1;

  gf_log (trans->xl->name, GF_LOG_DEBUG,
	  "op=%d off=%"PRId64" size=%"PRId64" handle=%s",
	  hdr.op, hdr.offset, hdr.size, hdr.handle);

  sscanf (hdr.handle, "%p", &inode);
  gf_log (trans->xl->name, GF_LOG_DEBUG,
	  "inode number = %"PRId64, inode->ino);

  /* TODO: hold inode lock and check for fd */
  if (!list_empty (&inode->fds))
    fd = list_entry (inode->fds.next, fd_t, inode_list);
  if (!fd) {
    gf_log (trans->xl->name, GF_LOG_DEBUG,
	    "no fd found for handle %p", inode);
    return -1;
  } else {
    gf_log (trans->xl->name, GF_LOG_DEBUG,
	    "using fd %p for handle %p", fd, inode);
  }

  frame = get_frame_for_transport (trans);
  switch (hdr.op) {
  case GF_FOP_READ:
    STACK_WIND (frame, booster_readv_cbk,
		FIRST_CHILD (frame->this), FIRST_CHILD (frame->this)->fops->readv,
		fd, hdr.size, hdr.offset);
    break;
  case GF_FOP_CLOSE:
    {
      struct iovec vector;
      
      vector.iov_base = &hdr;
      vector.iov_len = sizeof (hdr);
      trans->ops->writev (trans, &vector, 1);
    }
    break;
  case GF_FOP_WRITE:
    {
      char *write_buf;
      data_t *ref_data;
      dict_t *ref_dict;
      struct iovec vector;
      int count = 1;

      /* TODO:
	 - implement limit on hdr.size
      */
      write_buf = malloc (hdr.size);
      ret = trans->ops->recieve (trans, write_buf, hdr.size);
      if (ret == 0) {
	vector.iov_base = write_buf;
	vector.iov_len = hdr.size;

	ref_data = data_from_dynptr (write_buf, hdr.size);
	ref_dict = get_new_dict ();
	ref_dict->is_locked = 1;
	ref_data->is_locked = 1;
	dict_set (ref_dict, NULL, ref_data);
	frame->root->req_refs = dict_ref (ref_dict);

	STACK_WIND (frame, booster_writev_cbk,
		    FIRST_CHILD (frame->this), FIRST_CHILD (frame->this)->fops->writev,
		    fd, &vector, count, hdr.offset);

	dict_unref (ref_dict);
      }
    }
    break;
  }

  return 0;
}

int32_t
notify (xlator_t *this,
        int32_t event,
        void * data,
        ...)
{
  int ret = 0;

  switch (event) {
  case GF_EVENT_POLLIN:
    ret = booster_interpret (data);
    if (ret != 0)
      transport_disconnect (data);
    break;
  case GF_EVENT_POLLERR:
    transport_disconnect (data);
    break;
  }

  return ret;
}

int32_t
init (xlator_t *this)
{
  transport_t *trans;
  dict_t *client_options, *server_options;
  char *transport_type = NULL;
  char *path = NULL;

  if (!this->children || this->children->next) {
    gf_log (this->name, GF_LOG_ERROR,
	    "FATAL: booster not configured with exactly one child");
    return -1;
  }

  client_options = get_new_dict ();
  server_options = get_new_dict ();
  dict_copy (this->options, client_options);
  dict_copy (this->options, server_options);

  if (dict_get (this->options, "transport-type")) {
    transport_type = strdup (data_to_ptr (dict_get (this->options,
						    "transport-type")));
    if (strchr (transport_type, '/'))
      *(strchr (transport_type, '/')) = 0;
  } else {
    transport_type = strdup ("unix");

    asprintf (&path, "/tmp/glusterfs-booster-server.%d", getpid ());
    dict_set (client_options, "connect-path", data_from_dynstr (path));
    dict_set (server_options, "listen-path", data_from_dynstr (path));
  }

  {
    char *type = NULL;

    asprintf (&type, "%s/client", transport_type);
    dict_set (client_options, "transport-type", data_from_dynstr (type));

    asprintf (&type, "%s/server", transport_type);
    dict_set (server_options, "transport-type", data_from_dynstr (type));
  }

  trans = transport_load (server_options, this, this->notify);
  this->private = client_options;

  return 0;
}

void
fini (xlator_t *this)
{
}

struct xlator_fops fops = {
  .getxattr = booster_getxattr,
};

struct xlator_mops mops = {
};
