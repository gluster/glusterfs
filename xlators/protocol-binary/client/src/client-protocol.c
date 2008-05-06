/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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
#include "client-protocol.h"
#include "compat.h"
#include "dict.h"
#include "protocol-binary.h"
#include "transport.h"
#include "xlator.h"
#include "logging.h"
#include "timer.h"
#include "defaults.h"

#include <sys/resource.h>
#include <inttypes.h>

static int32_t client_protocol_interpret (transport_t *trans, gf_proto_block_t *blk);
static int32_t client_protocol_cleanup (transport_t *trans);


typedef int32_t (*gf_op_t) (call_frame_t *frame,
			    gf_args_reply_t *args);

static gf_op_t gf_fops[];
static gf_op_t gf_mops[];

/* 
 * lookup_frame - lookup call frame corresponding to a given callid
 * @trans: transport object
 * @callid: call id of the frame
 *
 * not for external reference
 */
static call_frame_t *
lookup_frame (transport_t *trans, int64_t callid)
{
  char buf[64];
  call_frame_t *frame = NULL;
  client_proto_priv_t *priv = NULL;

  if (!trans) {
    gf_log ("", GF_LOG_ERROR, "!trans, that means, not connected at this moment");
    return NULL;
  }

  snprintf (buf, 64, "%"PRId64, callid);
  priv = trans->xl_private;

  pthread_mutex_lock (&priv->lock);
  {
    frame = data_to_bin (dict_get (priv->saved_frames, buf));
    dict_del (priv->saved_frames, buf);
  }
  pthread_mutex_unlock (&priv->lock);

  return frame;
}

/*
 * str_to_stat - convert a ASCII string to a struct stat
 * @buf: string
 *
 * not for external reference
 */
static struct stat *
str_to_stat (char *buf)
{
  struct stat *stbuf = calloc (1, sizeof (*stbuf));

  uint64_t dev;
  uint64_t ino;
  uint32_t mode;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid;
  uint64_t rdev;
  uint64_t size;
  uint32_t blksize;
  uint64_t blocks;
  uint32_t atime;
  uint32_t atime_nsec;
  uint32_t mtime;
  uint32_t mtime_nsec;
  uint32_t ctime;
  uint32_t ctime_nsec;

  sscanf (buf, GF_STAT_PRINT_FMT_STR,
	  &dev,
	  &ino,
	  &mode,
	  &nlink,
	  &uid,
	  &gid,
	  &rdev,
	  &size,
	  &blksize,
	  &blocks,
	  &atime,
	  &atime_nsec,
	  &mtime,
	  &mtime_nsec,
	  &ctime,
	  &ctime_nsec);

  stbuf->st_dev = dev;
  stbuf->st_ino = ino;
  stbuf->st_mode = mode;
  stbuf->st_nlink = nlink;
  stbuf->st_uid = uid;
  stbuf->st_gid = gid;
  stbuf->st_rdev = rdev;
  stbuf->st_size = size;
  stbuf->st_blksize = blksize;
  stbuf->st_blocks = blocks;

  stbuf->st_atime = atime;
  stbuf->st_mtime = mtime;
  stbuf->st_ctime = ctime;

#if HAVE_TV_NSEC
  stbuf->st_atim.tv_nsec = atime_nsec;
  stbuf->st_mtim.tv_nsec = mtime_nsec;
  stbuf->st_ctim.tv_nsec = ctime_nsec;
#endif

  return stbuf;
}

static void
call_bail (void *trans)
{
  client_proto_priv_t *priv = NULL;
  struct timeval current;
  int32_t bail_out = 0;

  if (!trans) {
    gf_log ("", GF_LOG_ERROR, "!trans, that means, already disconnected :O");
    return;
  }

  priv = ((transport_t *)trans)->xl_private;

  gettimeofday (&current, NULL);
  pthread_mutex_lock (&priv->lock);
  {
    /* Chaining to get call-always functionality from call-once timer */
    if (priv->timer) {
      struct timeval timeout = {0,};
      timeout.tv_sec = priv->transport_timeout;
      timeout.tv_usec = 0;
      gf_timer_cbk_t timer_cbk = priv->timer->cbk;
      gf_timer_call_cancel (((transport_t *) trans)->xl->ctx, priv->timer);
      priv->timer = gf_timer_call_after (((transport_t *) trans)->xl->ctx,
					 timeout,
					 timer_cbk,
					 trans);
      if (!priv->timer) {
	gf_log (((transport_t *)trans)->xl->name, GF_LOG_DEBUG,
		"Cannot create timer");
      }
    }

    if ((priv->saved_frames->count > 0)
	&& (((unsigned long long)priv->last_recieved.tv_sec + priv->transport_timeout) < current.tv_sec)
	&& (((unsigned long long)priv->last_sent.tv_sec + priv->transport_timeout ) < current.tv_sec)) {
      struct tm last_sent_tm, last_received_tm;
      char last_sent[32], last_received[32];

      bail_out = 1;
      localtime_r (&priv->last_sent.tv_sec, &last_sent_tm);
      localtime_r (&priv->last_recieved.tv_sec, &last_received_tm);
      strftime (last_sent, 32, "%Y-%m-%d %H:%M:%S", &last_sent_tm);
      strftime (last_received, 32, "%Y-%m-%d %H:%M:%S", &last_received_tm);
      gf_log (((transport_t *)trans)->xl->name, GF_LOG_WARNING,
	      "activating bail-out. pending frames = %d. last sent = %s. last received = %s transport-timeout = %d", 
	      priv->saved_frames->count, last_sent, last_received, 
	      priv->transport_timeout);
    }
  }
  pthread_mutex_unlock (&priv->lock);

  if (bail_out) {
    gf_log (((transport_t *)trans)->xl->name, GF_LOG_CRITICAL,
	  "bailing transport due to inactivity timeout");
    transport_bail (trans);
  }
}

/* 
 * client_protocol_xfer - client protocol transfer routine. called to send 
 *                        request packet to server
 * @frame: call frame
 * @this:
 * @type: operation type
 * @op: operation
 * @request: request data
 *
 * not for external reference
 */
int32_t
client_protocol_xfer (call_frame_t *frame,
		      xlator_t *this,
		      glusterfs_op_type_t type,
		      glusterfs_fop_t op,
		      gf_args_request_t *request)
{
  int32_t ret = -1;
  transport_t *trans;
  client_proto_priv_t *proto_priv;

  if (!request) {
    gf_log (this->name, GF_LOG_ERROR, "request to send a empty 'request'");
    return -1;
  }

  trans = this->private;
  if (!trans) {
    gf_log (this->name, GF_LOG_ERROR, "!trans, means, not connected at the moment");
    return -1;
  }

  proto_priv = trans->xl_private;
  if (!proto_priv) {
    gf_log (this->name, GF_LOG_ERROR, "trans->xl_private is NULL (init() not done?)");
    return -1;
  }

  request->uid = frame->root->uid;
  request->gid = frame->root->gid;
  request->pid = frame->root->pid;

  {
    int64_t callid;
    gf_proto_block_t blk;
    struct iovec *vector = NULL;
    int32_t count = 0;
    char connected = 0;
    char buf[64];

    pthread_mutex_lock (&proto_priv->lock);
    {
      callid = proto_priv->callid++;
      connected = proto_priv->connected;
      if (!connected) {
	/* tricky code - taking chances:
	   cause pipelining of handshake packet and this frame */
	connected = (transport_connect (trans) == 0);
	if (connected)
	  gf_log (this->name, GF_LOG_WARNING,
		  "attempting to pipeline request type(%d) op(%d) with handshake",
		  type, op);
      }
      if (connected) {
	snprintf (buf, 64, "%"PRId64, callid);
	frame->op = op;
	frame->type = type;
	dict_set (proto_priv->saved_frames, buf,
		  bin_to_data (frame, sizeof (frame)));
      }
    }
    pthread_mutex_unlock (&proto_priv->lock);

    blk.callid = callid;
    blk.type = type;
    blk.op   = op;
    blk.size = gf_proto_get_data_len((gf_args_t *)request);
    blk.args = request;

    {
      int32_t i;
      count = 7;
      vector = alloca (count * sizeof (*vector));
      memset (vector, 0, count * sizeof (*vector));
      
      gf_proto_block_to_iovec (&blk, vector, &count);
      for (i=0; i<count; i++)
	if (!vector[i].iov_base)
	  vector[i].iov_base = alloca (vector[i].iov_len);
      gf_proto_block_to_iovec (&blk, vector, &count);
    }

    if (connected) {
      client_proto_priv_t *priv = ((transport_t *)this->private)->xl_private;

      ret = trans->ops->writev (trans, vector, count);
      gf_proto_free_args ((gf_args_t *)request);

      pthread_mutex_lock (&(priv->lock));
      {
	gettimeofday (&(priv->last_sent), NULL);
      }
      pthread_mutex_unlock (&(priv->lock));
    }

    if (ret != 0) {
      if (connected) {
	gf_log (this->name, GF_LOG_ERROR, "transport_submit failed");
      } else {
	gf_args_reply_t reply;

	gf_log (this->name, GF_LOG_WARNING,
		"not connected at the moment to submit frame type(%d) op(%d)",
		type, op);
	reply.op_ret = -1;
	reply.op_errno = ENOTCONN;
	if (type == GF_OP_TYPE_FOP_REQUEST)
	  gf_fops[op] (frame, &reply);
	else
	  gf_mops[op] (frame, &reply);
      }
      return -1;
    }
  }
  return ret;
}

/*
 * client_lookup - lookup function for client protocol
 * @frame: call frame
 * @this:
 * @loc: location
 *
 * not for external reference
 */
int32_t 
client_lookup (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t need_xattr)
{
  int64_t ino = 0;
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;
  client_local_t *local = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    /* revalidate */
    ino = data_to_uint64 (ino_data);
  } else if (loc->ino == 1) {
    ino = 1;
  }

  local = calloc (1, sizeof (client_local_t));
  local->inode = loc->inode;
  frame->local = local;

  request.common = need_xattr;

  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;


  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LOOKUP, &request);
  
  return ret;
}

/* 
 * client_lookup_cbk - lookup callback for client protocol
 *
 * @frame: call frame
 * @args: arguments dictionary
 * 
 * not for external reference
 */
int32_t
client_lookup_cbk (call_frame_t *frame,
		   gf_args_reply_t *args)
{
  char *stat_buf = NULL;
  struct stat *stbuf = NULL;
  client_local_t *local = frame->local;
  inode_t *inode = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  dict_t *xattr = NULL;

  inode = local->inode;

  op_ret = args->op_ret;
  op_errno = args->op_errno;

  if (op_ret >= 0) {
    data_t *old_ino_data = dict_get (inode->ctx, frame->this->name);
    
    stat_buf = args->fields[0].ptr;
    stbuf = str_to_stat (stat_buf);
    
    if (!old_ino_data) {
      dict_set (inode->ctx, (frame->this)->name, 
		data_from_uint64 (stbuf->st_ino));
    } else {
      if (data_to_uint64 (old_ino_data) != stbuf->st_ino)
	dict_set (inode->ctx, (frame->this)->name, 
		  data_from_uint64 (stbuf->st_ino));
    }
    
    if (args->fields[1].len) {
      char *buf = args->fields[1].ptr;
      xattr = get_new_dict();
      dict_unserialize (buf, args->fields[1].len, &xattr);
      xattr->extra_free = buf;
    }
  }

  if (xattr)
    dict_ref (xattr);

  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf, xattr);
  
  if (xattr)
    dict_unref (xattr);

  if (stbuf)
    free (stbuf);

  return 0;
}

/**
 * client_create - create function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @path: complete path to file
 * @flags: create flags
 * @mode: create mode
 *
 * external reference through client_protocol_xlator->fops->create
 */
 
int32_t 
client_create (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t flags,
	       mode_t mode,
	       fd_t *fd)
{
  int32_t ret = -1;
  gf_args_request_t request = {0, };
  client_local_t *local = NULL;

  int32_t mode_tmp = htonl (mode);
  local = calloc (1, sizeof (client_local_t));
  local->inode = loc->inode;
  local->fd = fd;
  
  frame->local = local;

  request.common = flags;
  request.fields[0].type = GF_PROTO_INT32_TYPE;
  request.fields[0].len = 4;
  request.fields[0].ptr = (void *)&mode_tmp;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CREATE,
			      &request);
  return ret;
}

/**
 * client_open - open function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location of file
 * @flags: open flags
 * @mode: open modes
 *
 * external reference through client_protocol_xlator->fops->open
 */

int32_t 
client_open (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t flags,
	     fd_t *fd)
{
  int64_t ino = 0;
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;
  client_local_t *local = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);
  
  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    STACK_UNWIND (frame, -1, EINVAL, fd);
    return 0;
  }

  request.common = flags;
  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  local = calloc (1, sizeof (client_local_t));
  local->inode = loc->inode;
  local->fd = fd;
  
  frame->local = local;
  
  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_OPEN, &request);

  return ret;
}


/**
 * client_stat - stat function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 *
 * external reference through client_protocol_xlator->fops->stat
 */

int32_t 
client_stat (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
  int64_t ino = 0;
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_STAT, &request);

  return ret;
}


/**
 * client_readlink - readlink function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @size: 
 *
 * external reference through client_protocol_xlator->fops->readlink
 */


int32_t 
client_readlink (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 size_t size)
{
  int64_t ino = 0;
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  request.common = size;
  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_READLINK, &request);

  return ret;
}


/**
 * client_mknod - mknod function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @path: pathname of node
 * @mode: 
 * @dev:
 *
 * external reference through client_protocol_xlator->fops->mknod
 */

int32_t 
client_mknod (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode,
	      dev_t dev)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  client_local_t *local = NULL;

  local = calloc (1, sizeof (client_local_t));
  local->inode = loc->inode;
  frame->local = local;

  request.common = mode;
  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&dev;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_MKNOD, &request);

  
  return ret;
}


/**
 * client_mkdir - mkdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @path: pathname of directory
 * @mode:
 *
 * external reference through client_protocol_xlator->fops->mkdir
 */


int32_t 
client_mkdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  client_local_t *local = NULL;

  local = calloc (1, sizeof (client_local_t));
  local->inode = loc->inode;
  frame->local = local;

  request.common = mode;
  request.fields[0].type = GF_PROTO_CHAR_TYPE;
  request.fields[0].len = strlen (loc->path);
  request.fields[0].ptr = (void *)loc->path;
  
  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_MKDIR, &request);

  
  return ret;
}



/**
 * client_unlink - unlink function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location of file
 *
 * external reference through client_protocol_xlator->fops->unlink
 */

int32_t 
client_unlink (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  int64_t ino = 0;
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);
  
  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }

  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;
  
  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_UNLINK, &request);

  
  return ret;
}

int32_t
client_rmelem (call_frame_t *frame,
	       xlator_t *this,
	       const char *path)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
    
  request.fields[0].type = GF_PROTO_CHAR_TYPE;
  request.fields[0].len = strlen (path);
  request.fields[0].ptr = (void *)path;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_RMELEM, &request);
  
  return ret;
}

/**
 * client_rmdir - rmdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 *
 * external reference through client_protocol_xlator->fops->rmdir
 */

int32_t 
client_rmdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  int64_t ino = 0;
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }

  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_RMDIR, &request);

  
  return ret;
}



/**
 * client_symlink - symlink function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @oldpath: pathname of target
 * @newpath: pathname of symlink
 *
 * external reference through client_protocol_xlator->fops->symlink
 */

int32_t 
client_symlink (call_frame_t *frame,
		xlator_t *this,
		const char *linkname,
		loc_t *loc)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  client_local_t *local = NULL;

  local = calloc (1, sizeof (client_local_t));
  local->inode = loc->inode;
  frame->local = local;

  request.fields[0].type = GF_PROTO_CHAR_TYPE;
  request.fields[0].len = strlen (linkname);
  request.fields[0].ptr = (void *)linkname;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_SYMLINK, &request);

  
  return ret;
}


/**
 * client_rename - rename function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @oldloc: location of old pathname
 * @newloc: location of new pathname
 *
 * external reference through client_protocol_xlator->fops->rename
 */

int32_t 
client_rename (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *oldloc,
	       loc_t *newloc)
{
  int32_t ret = -1;
  int64_t ino = 0, newino = 0;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;
  data_t *newino_data = NULL;
  
  if (oldloc && oldloc->inode && oldloc->inode->ctx)
    ino_data = dict_get (oldloc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s -> %s: returning EINVAL", 
	    oldloc->path, newloc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  if (newloc && newloc->inode && newloc->inode->ctx) {
    newino_data = dict_get (newloc->inode->ctx, this->name);
    if (newino_data) 
      newino = data_to_uint64 (newino_data);
  }

  
  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_INT64_TYPE;
  request.fields[1].len = 8;
  request.fields[1].ptr = (void *)&newino;
  
  request.fields[2].type = GF_PROTO_CHAR_TYPE;
  request.fields[2].len = strlen (oldloc->path);
  request.fields[2].ptr = (void *)oldloc->path;

  request.fields[3].type = GF_PROTO_CHAR_TYPE;
  request.fields[3].len = strlen (newloc->path);
  request.fields[3].ptr = (void *)newloc->path;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_RENAME, &request);

  
  return ret;
}



/**
 * client_link - link function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @oldloc: location of old pathname
 * @newpath: new pathname
 *
 * external reference through client_protocol_xlator->fops->link
 */

int32_t 
client_link (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *oldloc,
	     const char *newpath)
{
  int64_t oldino = 0;
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *oldino_data = NULL;
  client_local_t *local = NULL;

  if (oldloc && oldloc->inode && oldloc->inode->ctx)
    oldino_data = dict_get (oldloc->inode->ctx, this->name);

  if (oldino_data) {
    oldino = data_to_uint64 (oldino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, 
	    "%s -> %s: returning EINVAL", oldloc->path, newpath);
    TRAP_ON (oldino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
    return 0;
  }

  local = calloc (1, sizeof (client_local_t));
  local->inode = oldloc->inode;
  frame->local = local;

  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&oldino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (oldloc->path);
  request.fields[1].ptr = (void *)oldloc->path;

  request.fields[2].type = GF_PROTO_CHAR_TYPE;
  request.fields[2].len = strlen (newpath);
  request.fields[2].ptr = (void *)newpath;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LINK, &request);
  
  return ret;
}



/**
 * client_chmod - chmod function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location 
 * @mode:
 *
 * external reference through client_protocol_xlator->fops->chmod
 */

int32_t 
client_chmod (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode)
{
  int64_t ino = 0;
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  request.common = mode;
  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CHMOD, &request);

  
  return ret;
}


/**
 * client_chown - chown function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @uid: uid of new owner
 * @gid: gid of new owner group
 *
 * external reference through client_protocol_xlator->fops->chown
 */

int32_t 
client_chown (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      uid_t uid,
	      gid_t gid)
{
  int64_t ino = 0;
  int32_t ret = -1;
  int32_t uid_gid_array[2];
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  uid_gid_array[0] = htonl (uid);
  uid_gid_array[1] = htonl (gid);

  request.fields[0].type = GF_PROTO_INT32_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)uid_gid_array;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CHOWN, &request);

  
  return ret;
}

/**
 * client_truncate - truncate function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @offset:
 *
 * external reference through client_protocol_xlator->fops->truncate
 */

int32_t 
client_truncate (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 off_t offset)
{
  int64_t ino_off_array[2] = {0,};
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino_off_array[0] = gf_htonl_64 (data_to_uint64 (ino_data));
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  ino_off_array[1] = gf_htonl_64 (offset);

  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 16;
  request.fields[0].ptr = (void *)ino_off_array;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_TRUNCATE, &request);

  
  return ret;
}



/**
 * client_utimes - utimes function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @tvp:
 *
 * external reference through client_protocol_xlator->fops->utimes
 */

int32_t 
client_utimens (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		struct timespec *tvp)
{
  int64_t ino = 0;
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;
  
  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;
  

  {
    int32_t tim_array[4] = {0};
    tim_array[0] = htonl (tvp[0].tv_sec);
    tim_array[1] = htonl (tvp[0].tv_nsec);
    tim_array[2] = htonl (tvp[1].tv_sec);
    tim_array[3] = htonl (tvp[1].tv_nsec);
    
    request.fields[2].type = GF_PROTO_INT32_TYPE;
    request.fields[2].len = 16;
    request.fields[2].ptr = (void *)tim_array;
  }

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_UTIMENS, &request);

  
  return ret;
}



/**
 * client_readv - readv function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @size:
 * @offset:
 *
 * external reference through client_protocol_xlator->fops->readv
 */

int32_t 
client_readv (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      size_t size,
	      off_t offset)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ctx_data = NULL;

  if (fd && fd->ctx)
    ctx_data = dict_get (fd->ctx, this->name);

  if (!ctx_data) {
    struct iovec vec;
    struct stat dummy = {0, };
    vec.iov_base = "";
    vec.iov_len = 0;
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, &vec, &dummy);
    return 0;
  }

  request.common = data_to_int32 (ctx_data);

  {
    int64_t array[2] = {0};
    array[0] = gf_htonl_64 (size);
    array[1] = gf_htonl_64 (offset);
    
    request.fields[0].type = GF_PROTO_INT64_TYPE;
    request.fields[0].len = 16;
    request.fields[0].ptr = (void *)array;
  }

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_READ, &request);
  
  return ret;
}


/**
 * client_writev - writev function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @vector:
 * @count:
 * @offset:
 *
 * external reference through client_protocol_xlator->fops->writev
 */

int32_t 
client_writev (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       struct iovec *vector,
	       int32_t count,
	       off_t offset)
{
  int32_t ret = -1;
  size_t size = 0, i;
  gf_args_request_t request = {0,};
  data_t *ctx_data = NULL;

  if (fd && fd->ctx)
    ctx_data = dict_get (fd->ctx, this->name);

  if (!ctx_data) {
    struct stat dummy = {0, };
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, &dummy);
    return 0;
  }

  for (i = 0; i<count; i++)
    size += vector[i].iov_len;

  request.common = data_to_int32 (ctx_data);
  
  {
    int64_t array[2] = {0};
    array[0] = gf_htonl_64 (size);
    array[1] = gf_htonl_64 (offset);
    
    request.fields[0].type = GF_PROTO_INT64_TYPE;
    request.fields[0].len = 16;
    request.fields[0].ptr = (void *)array;
    
    if (vector[0].iov_len) {
      request.fields[1].type = GF_PROTO_CHAR_TYPE;
      request.fields[1].len = vector[0].iov_len;
      request.fields[1].ptr = vector[0].iov_base;
    }
  }

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_WRITE, &request);

  
  return ret;
}


/**
 * client_statfs - statfs function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 *
 * external reference through client_protocol_xlator->fops->statfs
 */

int32_t 
client_statfs (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};

  request.common = 1;
  request.fields[0].type = GF_PROTO_CHAR_TYPE;
  request.fields[0].len = strlen (loc->path);
  request.fields[0].ptr = (void *)loc->path;

#if 0
  /* Send Inode number when we see them, currently 1 is sent all the time */
  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
#endif 

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_STATFS, &request);

  
  return ret;
}


/**
 * client_flush - flush function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->fops->flush
 */

int32_t 
client_flush (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ctx_data = NULL;

  if (fd && fd->ctx)
    ctx_data = dict_get (fd->ctx, this->name);

  if (!ctx_data) {    
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  request.common = data_to_int32 (ctx_data);
  
  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FLUSH, &request);

  
  return ret;
}


/**
 * client_close - close function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->fops->close
 *
 * TODO: fd_t is top-down now... no need to do anything destructive. Also need to look into 
 *      cleanup().
 */

int32_t 
client_close (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  int32_t ret = -1;
  char *key = NULL;
  data_t *ctx_data = NULL;
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;

  trans = frame->this->private;

  if (fd && fd->ctx)
    ctx_data = dict_get (fd->ctx, this->name);

  if (ctx_data) {
    gf_args_request_t request = {0,};
  
    request.common = data_to_int32 (ctx_data);
    ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
				GF_FOP_CLOSE, &request);
    
  } else {
    gf_log (this->name, GF_LOG_WARNING, "no valid fd found, returning");
    STACK_UNWIND (frame, 0, 0);
  }

  priv = trans->xl_private;
  asprintf (&key, "%p", fd);

  pthread_mutex_lock (&priv->lock);
  {
    dict_del (priv->saved_fds, key); 
  }
  pthread_mutex_unlock (&priv->lock);
  
  freee (key);

  return ret;
}


/**
 * client_fsync - fsync function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @flags:
 *
 * external reference through client_protocol_xlator->fops->fsync
 */

int32_t 
client_fsync (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ctx_data = NULL;

  if (fd && fd->ctx)
    ctx_data = dict_get (fd->ctx, this->name);

  if (!ctx_data) {
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }
  
  request.common = data_to_int32 (ctx_data);
  request.fields[0].type = GF_PROTO_INT32_TYPE;
  request.fields[0].len = 4;
  request.fields[0].ptr = (void *)&flags;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FSYNC, &request);
  
  return ret;
}


int32_t
client_incver (call_frame_t *frame,
	       xlator_t *this,
	       const char *path)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};

  request.fields[0].type = GF_PROTO_CHAR_TYPE;
  request.fields[0].len = strlen (path);
  request.fields[0].ptr = (void *)path;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_INCVER, &request);

  return ret;
}


/**
 * client_setxattr - setxattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @dict: dictionary which contains key:value to be set.
 * @flags:
 *
 * external reference through client_protocol_xlator->fops->setxattr
 */

int32_t 
client_setxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 dict_t *dict,
		 int32_t flags)
{
  int64_t ino = 0;
  int32_t ret = -1;  
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;

  if (loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    if (!strncmp (loc->path, "/", 2)) {
      ino = 1;
    } else {
      gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
      TRAP_ON (ino_data == NULL);
      frame->root->rsp_refs = NULL;
      STACK_UNWIND (frame, -1, EINVAL);
      return 0;
    }
  }

  request.common = flags;
  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;
  
  {
    /* Serialize the dictionary and set it as a parameter in 'request' dict */
    int32_t len = dict_serialized_length (dict);
    char *dict_buf = alloca (len);
    dict_serialize (dict, dict_buf);

    request.fields[2].type = GF_PROTO_CHAR_TYPE;
    request.fields[2].len = len;
    request.fields[2].ptr = (void *)dict_buf;
  }

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_SETXATTR, &request);

  return ret;
}


/**
 * client_getxattr - getxattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 *
 * external reference through client_protocol_xlator->fops->getxattr
 */

int32_t 
client_getxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name)
{
  int64_t ino = 0;
  int32_t ret = -1;  
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }


  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  if (name) {
    request.fields[2].type = GF_PROTO_CHAR_TYPE;
    request.fields[2].len = strlen (name);
    request.fields[2].ptr = (void *)name;
  }

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_GETXATTR, &request);

  
  return ret;
}

/**
 * client_removexattr - removexattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 * @name:
 *
 * external reference through client_protocol_xlator->fops->removexattr
 */
	     
int32_t 
client_removexattr (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    const char *name)
{
  int64_t ino = 0;
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }

  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  request.fields[2].type = GF_PROTO_CHAR_TYPE;
  request.fields[2].len = strlen (name);
  request.fields[2].ptr = (void *)name;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_REMOVEXATTR, &request);

  
  return ret;
}


/**
 * client_opendir - opendir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 *
 * external reference through client_protocol_xlator->fops->opendir
 */

int32_t 
client_opendir (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		fd_t *fd)
{
  int32_t ret = -1;
  int64_t ino = 0;
  data_t *ino_data = NULL;
  gf_args_request_t request = {0,};
  client_local_t *local = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, fd);
    return 0;
  }

  local = calloc (1, sizeof (client_local_t));
  local->inode = loc->inode;
  local->fd = fd;
  frame->local = local;

  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;
  
  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_OPENDIR, &request);

  return ret;
}


/**
 * client_readdir - readdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 *
 * external reference through client_protocol_xlator->fops->readdir
 */

int32_t 
client_getdents (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 size_t size,
		 off_t offset,
		 int32_t flag)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *fd_data = NULL;

  if (fd && fd->ctx)
    fd_data = dict_get (fd->ctx, this->name);

  if (!fd_data) {
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (fd_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, NULL, 0);
    return 0;
  }

  request.common = data_to_int32 (fd_data);

  {
    int64_t array[3] = {0};
    array[0] = gf_htonl_64 (size);
    array[1] = gf_htonl_64 (offset);
    array[2] = gf_htonl_64 (flag);

    request.fields[0].type = GF_PROTO_INT64_TYPE;
    request.fields[0].len = 24;
    request.fields[0].ptr = (void *)array;
  }

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_GETDENTS, &request);
  
  return ret;
}

/**
 * client_readdir - readdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 *
 * external reference through client_protocol_xlator->fops->readdir
 */

int32_t 
client_readdir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 size_t size,
		 off_t offset)
{
  int32_t ret = -1;
  data_t *fd_data = NULL;
  gf_args_request_t request = {0,};

  if (fd && fd->ctx)
    fd_data = dict_get (fd->ctx, this->name);
  
  if (!fd_data) {
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (fd_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, NULL, 0);
    return 0;
  }

  request.common = data_to_int32 (fd_data);

  {
    int64_t array[2] = {0};
    array[0] = gf_htonl_64 (size);
    array[1] = gf_htonl_64 (offset);
    
    request.fields[0].type = GF_PROTO_INT64_TYPE;
    request.fields[0].len = 16;
    request.fields[0].ptr = (void *)array;
  }

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_READDIR, &request);

  return ret;
}


/**
 * client_closedir - closedir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->fops->closedir
 */

int32_t 
client_closedir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd)
{
  int32_t ret = -1;
  char *key = NULL;
  data_t *ctx_data = NULL;
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;

  trans = frame->this->private;

  if (fd && fd->ctx)
    ctx_data = dict_get (fd->ctx, this->name);

  if (ctx_data) {
    gf_args_request_t request = {0,};
  
    request.common = data_to_int32 (ctx_data);

    ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
				GF_FOP_CLOSEDIR, &request);
    
  } else {
    gf_log (this->name, GF_LOG_WARNING, "no proper fd found, returning");
    STACK_UNWIND (frame, 0, 0);
  }

  priv = trans->xl_private;
  asprintf (&key, "%p", fd);

  pthread_mutex_lock (&priv->lock);
  {
    dict_del (priv->saved_fds, key); 
  }
  pthread_mutex_unlock (&priv->lock);
  
  free (key);

  return ret;
}


/**
 * client_fsyncdir - fsyncdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @flags:
 *
 * external reference through client_protocol_xlator->fops->fsyncdir
 */

int32_t 
client_fsyncdir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t flags)
{
  int32_t ret = -1;
  data_t *ctx_data = NULL;

  if (fd && fd->ctx)
    ctx_data = dict_get (fd->ctx, this->name);

  if (!ctx_data) {
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD);
    return -1;
  }

  gf_log (this->name, GF_LOG_ERROR, "Function not implemented");

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS);
  return ret;
}


/**
 * client_access - access function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 * @mode: 
 *
 * external reference through client_protocol_xlator->fops->access
 */

int32_t 
client_access (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t mask)
{
  int64_t ino = 0;
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
    return 0;
  }

  request.common = mask;
  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_ACCESS, &request);
  
  return ret;
}


/**
 * client_ftrucate - ftruncate function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @offset: offset to truncate to
 *
 * external reference through client_protocol_xlator->fops->ftruncate
 */

int32_t 
client_ftruncate (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  off_t offset)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ctx_data = NULL;

  if (fd && fd->ctx)
    ctx_data = dict_get (fd->ctx, this->name);

  if (!ctx_data) {
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }

  request.common = data_to_int32 (ctx_data);
  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&offset;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FTRUNCATE, &request);
  
  return ret;
}


/**
 * client_fstat - fstat function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure 
 *
 * external reference through client_protocol_xlator->fops->fstat
 */

int32_t 
client_fstat (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *fd_data = NULL;

  if (fd && fd->ctx)
    fd_data = dict_get (fd->ctx, this->name);

  if (!fd_data) {
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (fd_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }
  request.common = data_to_int32 (fd_data);
  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FSTAT, &request);
  
  return ret;
}

/**
 * client_lk - lk function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @cmd: lock command
 * @lock: 
 *
 * external reference through client_protocol_xlator->fops->lk
 */

int32_t 
client_lk (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   int32_t cmd,
	   struct flock *lock)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *ctx_data = NULL;
  int32_t gf_cmd = 0;
  int32_t gf_type = 0;

  if (fd && fd->ctx)
    ctx_data = dict_get (fd->ctx, this->name);

  if (!ctx_data) {
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }


  if (cmd == F_GETLK || cmd == F_GETLK64)
    gf_cmd = GF_LK_GETLK;
  else if (cmd == F_SETLK || cmd == F_SETLK64)
    gf_cmd = GF_LK_SETLK;
  else if (cmd == F_SETLKW || cmd == F_SETLKW64)
    gf_cmd = GF_LK_SETLKW;
  else
    gf_log (this->name, GF_LOG_ERROR, "Unknown cmd (%d)!", gf_cmd);

  switch (lock->l_type) {
  case F_RDLCK: gf_type = GF_LK_F_RDLCK; break;
  case F_WRLCK: gf_type = GF_LK_F_WRLCK; break;
  case F_UNLCK: gf_type = GF_LK_F_UNLCK; break;
  }

  request.common = data_to_int32 (ctx_data);

  {
    int64_t array[7] = {0};
    array[0] = gf_cmd;
    array[1] = gf_type;
    array[2] = lock->l_whence;
    array[3] = lock->l_start;
    array[4] = lock->l_len;
    array[5] = lock->l_pid;
    array[6] = getpid();

    request.fields[0].type = GF_PROTO_INT64_TYPE;
    request.fields[0].len = 56;
    request.fields[0].ptr = (void *)array;
  }

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LK, &request);
  
  return ret;
}

/** 
 * client_writedir - 
 */
int32_t
client_setdents (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t flags,
		 dir_entry_t *entries,
		 int32_t count)
{
  int32_t ret = -1;
  char *buffer = NULL;
  gf_args_request_t request = {0,};
  data_t *fd_data = NULL;

  if (fd && fd->ctx)
    fd_data = dict_get (fd->ctx, this->name);

  if (!fd_data) {
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (fd_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  request.common = data_to_int32 (fd_data);

  {
    int32_t array[2] = {0};
    array[0] = htonl (flags);
    array[1] = htonl (count);
    
    request.fields[0].type = GF_PROTO_INT32_TYPE;
    request.fields[0].len = 8;
    request.fields[0].ptr = (void *)array;
  }

  {   
    dir_entry_t *trav = entries->next;
    uint32_t len = 0;
    char *ptr = (void *)NULL;
    while (trav) {
      len += strlen (trav->name);
      len += 1;
      len += 256; // max possible for statbuf;
      trav = trav->next;
    }
    buffer = calloc (1, len);
    ptr = (void *)buffer;
    trav = entries->next;
    while (trav) {
      int32_t this_len = 0;
      char *tmp_buf = NULL;
      struct stat *stbuf = &trav->buf;
      {
	/* Convert the stat buf to string */
	uint64_t dev = stbuf->st_dev;
	uint64_t ino = stbuf->st_ino;
	uint32_t mode = stbuf->st_mode;
	uint32_t nlink = stbuf->st_nlink;
	uint32_t uid = stbuf->st_uid;
	uint32_t gid = stbuf->st_gid;
	uint64_t rdev = stbuf->st_rdev;
	uint64_t size = stbuf->st_size;
	uint32_t blksize = stbuf->st_blksize;
	uint64_t blocks = stbuf->st_blocks;

	uint32_t atime = stbuf->st_atime;
	uint32_t mtime = stbuf->st_mtime;
	uint32_t ctime = stbuf->st_ctime;

#ifdef HAVE_TV_NSEC
	uint32_t atime_nsec = stbuf->st_atim.tv_nsec;
	uint32_t mtime_nsec = stbuf->st_mtim.tv_nsec;
	uint32_t ctime_nsec = stbuf->st_ctim.tv_nsec;
#else
	uint32_t atime_nsec = 0;
	uint32_t mtime_nsec = 0;
	uint32_t ctime_nsec = 0;
#endif

	asprintf (&tmp_buf,
		  GF_STAT_PRINT_FMT_STR,
		  dev,
		  ino,
		  mode,
		  nlink,
		  uid,
		  gid,
		  rdev,
		  size,
		  blksize,
		  blocks,
		  atime,
		  atime_nsec,
		  mtime,
		  mtime_nsec,
		  ctime,
		  ctime_nsec);
      }
      this_len = sprintf (ptr, "%s/%s", 
			  trav->name,
			  tmp_buf);
      
      free (tmp_buf);
      trav = trav->next;
      ptr += this_len;
    }
    
    request.fields[1].type = GF_PROTO_CHAR_TYPE;
    request.fields[1].len = strlen (buffer);
    request.fields[1].need_free = 1;
    request.fields[1].ptr = (void *)buffer;
  }

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_SETDENTS, &request);

  return ret;
}


/*
 * client_forget - forget function for client protocol
 * @frame: call frame
 * @this:
 * @inode:
 * 
 * not for external reference
 */
int32_t
client_forget (call_frame_t *frame,
	       xlator_t *this,
	       inode_t *inode)
{
  int64_t ino = 0;
  int32_t ret = 0;
  data_t *ino_data = NULL;
  call_frame_t *fr = NULL;

  if (inode && inode->ctx)
    ino_data = dict_get (inode->ctx, this->name);

  if (ino_data) {
    gf_args_request_t request = {0,};
    ino = data_to_uint64 (ino_data);

    fr = create_frame (this, this->ctx->pool);
    request.fields[0].type = GF_PROTO_INT64_TYPE;
    request.fields[0].len = 8;
    request.fields[0].ptr = (void *)&ino;

    ret = client_protocol_xfer (fr, this, GF_OP_TYPE_FOP_REQUEST,
				GF_FOP_FORGET, &request);
  }

  return ret;
}


/*
 * client_fchmod
 *
 */
int32_t
client_fchmod (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       mode_t mode)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *fd_data = NULL;

  if (fd && fd->ctx) {
    fd_data = dict_get (fd->ctx, this->name);
  }

  if (!fd_data) {
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (fd_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }
  request.common = data_to_int32 (fd_data);

  request.fields[0].type = GF_PROTO_INT32_TYPE;
  request.fields[0].len = 4;
  request.fields[0].ptr = (void *)&mode;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FCHMOD, &request);
  
  return 0;
}


/*
 * client_fchown -
 *
 * @frame:
 * @this:
 * @fd:
 * @uid:
 * @gid:
 *
 */
int32_t
client_fchown (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       uid_t uid,
	       gid_t gid)
{
  int32_t ret = -1;
  gf_args_request_t request = {0,};
  data_t *fd_data = NULL;

  if (fd && fd->ctx)
    fd_data = dict_get (fd->ctx, this->name);

  if (!fd_data) {
    gf_log (this->name, GF_LOG_ERROR, ": returning EBADFD");
    TRAP_ON (fd_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  request.common = data_to_int32 (fd_data);
  {
    int32_t array[2] = {0};
    array[0] = htonl (uid);
    array[1] = htonl (gid);
    
    request.fields[0].type = GF_PROTO_INT32_TYPE;
    request.fields[0].len = 8;
    request.fields[0].ptr = (void *)array;
  }

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FCHOWN, &request);

  return 0;

}

/*
 * MGMT_OPS
 */

/**
 * client_stats - stats function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @flags:
 *
 * external reference through client_protocol_xlator->mops->stats
 */

int32_t 
client_stats (call_frame_t *frame,
	      xlator_t *this, 
	      int32_t flags)
{
  int32_t ret = 0;
  gf_args_request_t request = {0,};

  /* without this dummy key the server crashes */
  request.common = flags;
  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_STATS, &request);

  return ret;
}


/**
 * client_fsck - fsck (file system check) function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @flags: 
 *
 * external reference through client_protocol_xlator->mops->fsck
 */

int32_t 
client_fsck (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)
{
  gf_log (this->name, GF_LOG_ERROR, "Function not implemented");
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}


/**
 * client_lock - lock function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @name: pathname of file to lock
 *
 * external reference through client_protocol_xlator->fops->lock
 */

int32_t 
client_lock (call_frame_t *frame,
	     xlator_t *this,
	     const char *name)
{
  gf_args_request_t request = {0,};
  int32_t ret = -1;
  
  request.fields[0].type = GF_PROTO_CHAR_TYPE;
  request.fields[0].len = strlen (name);
  request.fields[0].ptr = (void *)name;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_LOCK, &request);

  return ret;
}


/**
 * client_unlock - management function of client protocol to unlock
 * @frame: call frame
 * @this: this translator structure
 * @name: pathname of the file whose lock has to be released
 *
 * external reference through client_protocol_xlator->mops->unlock
 */

int32_t 
client_unlock (call_frame_t *frame,
	       xlator_t *this,
	       const char *name)
{
  gf_args_request_t request = {0,};
  int32_t ret = -1;
  
  request.fields[0].type = GF_PROTO_CHAR_TYPE;
  request.fields[0].len = strlen (name);
  request.fields[0].ptr = (void *)name;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_UNLOCK, &request);

  return ret;
}


/**
 * client_listlocks - management function of client protocol to list locks
 * @frame: call frame
 * @this: this translator structure
 * @pattern: 
 *
 * external reference through client_protocol_xlator->mops->listlocks
 */

int32_t 
client_listlocks (call_frame_t *frame,
		  xlator_t *this,
		  const char *pattern)
{
  gf_args_request_t request = {0,};
  int32_t ret = -1;
  
  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_LISTLOCKS, &request);

  return ret;
}



/* Callbacks */
#define CLIENT_PRIVATE(frame) (((transport_t *)(frame->this->private))->xl_private)

/*
 * client_chown_cbk - 
 * 
 * @frame:
 * @args:
 *
 * not for external reference
 */
int32_t
client_fchown_cbk (call_frame_t *frame,
		   gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct stat *stbuf = NULL;

  if (op_ret >= 0) {
    char *stat_str = args->fields[0].ptr;
    stbuf = str_to_stat (stat_str);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}


/*
 * client_fchmod_cbk 
 *
 * @frame:
 * @args:
 *
 * not for external reference
 */
int32_t
client_fchmod_cbk (call_frame_t *frame,
		   gf_args_reply_t *args)
{
  struct stat *stbuf = NULL;
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;

  if (op_ret >= 0) {
    char *stat_str = args->fields[0].ptr;
    stbuf = str_to_stat (stat_str);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}


/*
 * client_create_cbk - create callback function for client protocol
 * @frame: call frame
 * @args: arguments in dictionary
 *
 * not for external reference 
 */

int32_t 
client_create_cbk (call_frame_t *frame,
		   gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  char *stat_buf = NULL;
  struct stat *stbuf = NULL;
  client_proto_priv_t *priv = NULL;
  inode_t *inode = NULL;
  fd_t *fd = NULL;
  client_local_t *local = frame->local;

  fd = local->fd;
  inode = local->inode;
  
  if (op_ret >= 0) {
    /* handle fd */
    int32_t remote_fd = args->dummy1;
    char *key = NULL;

    stat_buf = args->fields[0].ptr;
    stbuf = str_to_stat (stat_buf);

    /* add newly created file's inode to  inode table */
    dict_set (inode->ctx, (frame->this)->name, 
	      data_from_uint64 (stbuf->st_ino));
    
    dict_set (fd->ctx, (frame->this)->name,  data_from_int32 (remote_fd));

    asprintf (&key, "%p", fd);
    
    priv = CLIENT_PRIVATE (frame);
    pthread_mutex_lock (&priv->lock);
    {
      dict_set (priv->saved_fds, key, str_to_data ("")); 
    }
    pthread_mutex_unlock (&priv->lock);
    
    freee (key);
  }

  STACK_UNWIND (frame, op_ret, op_errno, fd, inode, stbuf);
  
  if (stbuf)
    freee (stbuf);

  return 0;
}

/*
 * client_open_cbk - open callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_open_cbk (call_frame_t *frame,
		 gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  client_proto_priv_t *priv = NULL;
  client_local_t *local = frame->local;
  fd_t *fd = local->fd; 

  if (op_ret >= 0) {
    /* handle fd */
    int32_t remote_fd = args->dummy1;
    char *key = NULL;
    
    dict_set (fd->ctx, (frame->this)->name, data_from_int32 (remote_fd));
    
    asprintf (&key, "%p", fd);
    
    priv = CLIENT_PRIVATE (frame);
    pthread_mutex_lock (&priv->lock);
    {
      dict_set (priv->saved_fds, key, str_to_data (""));
    }
    pthread_mutex_unlock (&priv->lock);
    
    freee (key);
  }

  STACK_UNWIND (frame, op_ret, op_errno, fd);
  return 0;
}

/* 
 * client_stat_cbk - stat callback for client protocol
 * @frame: call frame
 * @args: arguments dictionary
 *
 * not for external reference
 */
int32_t 
client_stat_cbk (call_frame_t *frame,
		 gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct stat *stbuf = NULL;
    
  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}

/* 
 * client_utimens_cbk - utimens callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
 
int32_t 
client_utimens_cbk (call_frame_t *frame,
		    gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct stat *stbuf = NULL;
  
  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  
  if (stbuf) 
    freee (stbuf);

  return 0;
}

/*
 * client_chmod_cbk - chmod for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_chmod_cbk (call_frame_t *frame,
		  gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct stat *stbuf = NULL;
  
  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}

/*
 * client_chown_cbk - chown for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference 
 */
int32_t 
client_chown_cbk (call_frame_t *frame,
		  gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct stat *stbuf = NULL;
  
  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}

/* 
 * client_mknod_cbk - mknod callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_mknod_cbk (call_frame_t *frame,
		  gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  client_local_t *local = frame->local;
  inode_t *inode = local->inode;
  struct stat *stbuf = NULL;
  
  inode = local->inode;
  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
    dict_set (inode->ctx, (frame->this)->name, 
	      data_from_uint64 (stbuf->st_ino));
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);
  
  if (stbuf)
    freee (stbuf);

  return 0;
}

/*
 * client_symlink_cbk - symlink callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_symlink_cbk (call_frame_t *frame,
		    gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  client_local_t *local = frame->local;
  inode_t *inode = local->inode;
  struct stat *stbuf = NULL;
  
  inode = local->inode;
  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
    dict_set (inode->ctx, (frame->this)->name, 
	      data_from_uint64 (stbuf->st_ino));
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);
  
  if (stbuf)
    freee (stbuf);

  return 0;
}

/*
 * client_link_cbk - link callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference 
 */
int32_t 
client_link_cbk (call_frame_t *frame,
		 gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct stat *stbuf = NULL;
  client_local_t *local = frame->local;
  inode_t *inode = local->inode;
  
  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}

/* 
 * client_truncate_cbk - truncate callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference 
 */

int32_t 
client_truncate_cbk (call_frame_t *frame,
		     gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct stat *stbuf = NULL;
  
  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
  }


  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}

/* client_fstat_cbk - fstat callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t 
client_fstat_cbk (call_frame_t *frame,
		  gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct stat *stbuf = NULL;
  
  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
  }


  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}

/* 
 * client_ftruncate_cbk - ftruncate callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */ 
int32_t 
client_ftruncate_cbk (call_frame_t *frame,
		      gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct stat *stbuf = NULL;
  
  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}

/* client_readv_cbk - readv callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external referece
 */

int32_t 
client_readv_cbk (call_frame_t *frame,
		  gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct stat *stbuf = NULL;
  struct iovec vec = {0,};
  
  if (op_ret >= 0) {
    vec.iov_base = args->fields[0].ptr;
    vec.iov_len = op_ret;
    char *buf = args->fields[1].ptr;
    stbuf = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, &vec, 1, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}

/* 
 * client_write_cbk - write callback for client protocol
 * @frame: cal frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t 
client_write_cbk (call_frame_t *frame,
		  gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct stat *stbuf = NULL;

  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}

/*
 * client_getdents_cbk - readdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_getdents_cbk (call_frame_t *frame,
		    gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;

  if (op_ret >= 0) {
    dir_entry_t *entry = NULL;
    dir_entry_t *trav = NULL, *prev = NULL;
    int32_t count, i, bread;
    char *ender = NULL, *buffer_ptr = (void *)NULL;
    char tmp_buf[512] = {0,};
    
    int32_t nr_count = args->dummy1;
    char *buf = args->fields[0].ptr;
    
    entry = calloc (1, sizeof (dir_entry_t));
    prev = entry;
    buffer_ptr = (void *)buf;
    
    for (i = 0; i < nr_count ; i++) {
      bread = 0;
      trav = calloc (1, sizeof (dir_entry_t));
      ender = strchr (buffer_ptr, '/');
      count = ender - buffer_ptr;
      trav->name = calloc (1, count + 2);
      strncpy (trav->name, buffer_ptr, count);
      bread = count + 1;
      buffer_ptr += bread;
      
      ender = strchr (buffer_ptr, '\n');
      count = ender - buffer_ptr;
      strncpy (tmp_buf, buffer_ptr, count);
      bread = count + 1;
      buffer_ptr += bread;
      
      {
	uint64_t dev;
	uint64_t ino;
	uint32_t mode;
	uint32_t nlink;
	uint32_t uid;
	uint32_t gid;
	uint64_t rdev;
	uint64_t size;
	uint32_t blksize;
	uint64_t blocks;
	uint32_t atime;
	uint32_t atime_nsec;
	uint32_t mtime;
	uint32_t mtime_nsec;
	uint32_t ctime;
	uint32_t ctime_nsec;
	
	sscanf (tmp_buf, GF_STAT_PRINT_FMT_STR,
		&dev,
		&ino,
		&mode,
		&nlink,
		&uid,
		&gid,
		&rdev,
		&size,
		&blksize,
		&blocks,
		&atime,
		&atime_nsec,
		&mtime,
		&mtime_nsec,
		&ctime,
		&ctime_nsec);
	
	trav->buf.st_dev = dev;
	trav->buf.st_ino = ino;
	trav->buf.st_mode = mode;
	trav->buf.st_nlink = nlink;
	trav->buf.st_uid = uid;
	trav->buf.st_gid = gid;
	trav->buf.st_rdev = rdev;
	trav->buf.st_size = size;
	trav->buf.st_blksize = blksize;
	trav->buf.st_blocks = blocks;
	
	trav->buf.st_atime = atime;
	trav->buf.st_mtime = mtime;
	trav->buf.st_ctime = ctime;

#ifdef HAVE_TV_NSEC
	trav->buf.st_atim.tv_nsec = atime_nsec;
	trav->buf.st_mtim.tv_nsec = mtime_nsec;
	trav->buf.st_ctim.tv_nsec = ctime_nsec;
#endif

      }    
      prev->next = trav;
      prev = trav;
    }
  
    STACK_UNWIND (frame, op_ret, op_errno, entry, nr_count);    
    
    prev = entry;
    trav = entry->next;
    while (trav) {
      prev->next = trav->next;
      free (trav->name);
      free (trav);
      trav = prev->next;
    }
    free (entry);
    return 0;
  }

  STACK_UNWIND (frame, op_ret, op_errno, NULL, 0);
  return 0;
}


int32_t 
client_readdir_cbk (call_frame_t *frame,
		    gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  char *buf = NULL;
  
  if (op_ret >= 0) {
    buf = memdup (args->fields[0].ptr, op_ret);
  }

  STACK_UNWIND (frame, op_ret, op_errno, buf);

  return 0;
}

/*
 * client_fsync_cbk - fsync callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_fsync_cbk (call_frame_t *frame,
		  gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/* 
 * client_unlink_cbk - unlink callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_unlink_cbk (call_frame_t *frame,
		   gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t
client_rmelem_cbk (call_frame_t *frame,
		   gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


/*
 * client_rename_cbk - rename callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_rename_cbk (call_frame_t *frame,
		   gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct stat *stbuf = NULL;

  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}

/*
 * client_readlink_cbk - readlink callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_readlink_cbk (call_frame_t *frame,
		    gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  char *buf = (args->fields[0].len)?args->fields[0].ptr:NULL;
  
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

/*
 * client_mkdir_cbk - mkdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_mkdir_cbk (call_frame_t *frame,
		  gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  client_local_t *local = frame->local;
  inode_t *inode = local->inode;
  struct stat *stbuf = NULL;
  
  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    stbuf = str_to_stat (buf);
    dict_set (inode->ctx, (frame->this)->name, 
	      data_from_uint64 (stbuf->st_ino));
  }

  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);

  if (stbuf)
    freee (stbuf);

  return 0;
}

/*
 * client_flush_cbk - flush callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t 
client_flush_cbk (call_frame_t *frame,
		  gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_close_cbk - close callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_close_cbk (call_frame_t *frame,
		  gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);
  
  return 0;
}

/*
 * client_opendir_cbk - opendir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_opendir_cbk (call_frame_t *frame,
		    gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  client_proto_priv_t *priv = NULL;
  client_local_t *local = frame->local;
  fd_t *fd = local->fd;

  if (op_ret >= 0) {
    
    /* handle fd */
    char *key = NULL;
    int32_t remote_fd = args->dummy1;
    
    dict_set (fd->ctx, (frame->this)->name,
	      data_from_int32 (remote_fd));
    
    asprintf (&key, "%p", fd);
    
    priv = CLIENT_PRIVATE(frame);
    pthread_mutex_lock (&priv->lock);
    {
      dict_set (priv->saved_fds, key, str_to_data (""));
    }
    pthread_mutex_unlock (&priv->lock);
    
    freee (key);
  }

  STACK_UNWIND (frame, op_ret, op_errno, fd);

  return 0;
}

/*
 * client_closedir_cbk - closedir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_closedir_cbk (call_frame_t *frame,
		     gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_rmdir_cbk - rmdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t 
client_rmdir_cbk (call_frame_t *frame,
		  gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_statfs_cbk - statfs callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_statfs_cbk (call_frame_t *frame,
		   gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  char *buf = NULL;
  struct statvfs *stvbuf = NULL;
  
  if (op_ret >= 0) {
    buf = args->fields[0].ptr;
    stvbuf = calloc (1, sizeof (struct statvfs));
    
    {
      uint32_t bsize;
      uint32_t frsize;
      uint64_t blocks;
      uint64_t bfree;
      uint64_t bavail;
      uint64_t files;
      uint64_t ffree;
      uint64_t favail;
      uint32_t fsid;
      uint32_t flag;
      uint32_t namemax;
      
      sscanf (buf, GF_STATFS_SCAN_FMT_STR,
	      &bsize,
	      &frsize,
	      &blocks,
	      &bfree,
	      &bavail,
	      &files,
	      &ffree,
	      &favail,
	      &fsid,
	      &flag,
	      &namemax);
	
      stvbuf->f_bsize = bsize;
      stvbuf->f_frsize = frsize;
      stvbuf->f_blocks = blocks;
      stvbuf->f_bfree = bfree;
      stvbuf->f_bavail = bavail;
      stvbuf->f_files = files;
      stvbuf->f_ffree = ffree;
      stvbuf->f_favail = favail;
      stvbuf->f_fsid = fsid;
      stvbuf->f_flag = flag;
      stvbuf->f_namemax = namemax;
    }
  }

  STACK_UNWIND (frame, op_ret, op_errno, stvbuf);

  if (stvbuf)
    freee (stvbuf);

  return 0;
}

/*
 * client_fsyncdir_cbk - fsyncdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_fsyncdir_cbk (call_frame_t *frame,
		     gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_access_cbk - access callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_access_cbk (call_frame_t *frame,
		   gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t
client_incver_cbk (call_frame_t *frame,
		   gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


/*
 * client_setxattr_cbk - setxattr callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_setxattr_cbk (call_frame_t *frame,
		     gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_getxattr_cbk - getxattr callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_getxattr_cbk (call_frame_t *frame,
		     gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  char *buf = (args->fields[0].len)?args->fields[0].ptr:NULL;
  dict_t *dict = get_new_dict ();

  if (op_ret >= 0 && buf) {
    /* Unserialize the dictionary recieved */
    dict_unserialize (buf, args->fields[0].len, &dict);
    dict_del (dict, "__@@protocol_client@@__key"); //hack
  }

  if (dict)
    dict_ref (dict);

  STACK_UNWIND (frame, op_ret, op_errno, dict);

  if (dict) 
    dict_unref (dict);

  return 0;
}

/*
 * client_removexattr_cbk - removexattr callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 * 
 * not for external reference
 */
int32_t 
client_removexattr_cbk (call_frame_t *frame,
			gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_lk_cbk - lk callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_lk_cbk (call_frame_t *frame,
	       gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct flock lock = {0,};

  if (op_ret >= 0) {
    lock.l_type =  gf_ntohl_64_ptr (args->fields[0].ptr);
    lock.l_whence =  gf_ntohl_64_ptr (args->fields[0].ptr + 8);
    lock.l_start =  gf_ntohl_64_ptr (args->fields[0].ptr + 16);
    lock.l_len =  gf_ntohl_64_ptr (args->fields[0].ptr + 24);
    lock.l_pid =  gf_ntohl_64_ptr (args->fields[0].ptr+ 32);
  }

  STACK_UNWIND (frame, op_ret, op_errno, &lock);
  return 0;
}

/**
 * client_setdents_cbk -
 *
 * @frame:
 * @args:
 *
 * not for external reference
 */
int32_t 
client_setdents_cbk (call_frame_t *frame,
		     gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


/* 
 * client_lock_cbk - lock callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_lock_cbk (call_frame_t *frame,
		 gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_unlock_cbk - unlock callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_unlock_cbk (call_frame_t *frame,
		   gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_listlocks_cbk - listlocks callback for client protocol
 *
 * @frame: call frame
 * @args: arguments dictionary
 *
 * not for external reference
 */

int32_t 
client_listlocks_cbk (call_frame_t *frame,
		      gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  STACK_UNWIND (frame, op_ret, op_errno, "");
  return 0;
}

/*
 * client_fsck_cbk - fsck callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t 
client_fsck_cbk (call_frame_t *frame,
		 gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/* 
 * client_stats_cbk - stats callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t 
client_stats_cbk (call_frame_t *frame,
		  gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  struct xlator_stats stats = {0,};
  
  if (op_ret >= 0) {
    char *buf = args->fields[0].ptr;
    
    sscanf (buf, "%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64"\n",
	    &stats.nr_files,
	    &stats.disk_usage,
	    &stats.free_disk,
	    &stats.total_disk_size,
	    &stats.read_usage,
	    &stats.write_usage,
	    &stats.disk_speed,
	    &stats.nr_clients);
  }

  STACK_UNWIND (frame, op_ret, op_errno, &stats);
  return 0;
}

/*
 * client_forget_cbk - forget callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 * 
 * not for external reference
 */
int32_t
client_forget_cbk (call_frame_t *frame,
		   gf_args_reply_t *args)
{
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * client_getspec - getspec function for client protocol
 * @frame: call frame
 * @this: client protocol xlator structure
 * @flag: 
 *
 * external reference through client_protocol_xlator->fops->getspec
 */
int32_t
client_getspec (call_frame_t *frame,
		xlator_t *this,
		int32_t flag)
{
  gf_args_request_t request = {0,};
  int32_t ret = -1;

  request.common = flag;
  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_GETSPEC, &request);  

  return ret;
}


/* 
 * client_getspec_cbk - getspec callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t 
client_getspec_cbk (call_frame_t *frame,
		    gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  char *spec_data = NULL;
  
  if (op_ret >= 0) {
    spec_data = (args->fields[0].len)?args->fields[0].ptr:NULL;
  }

  STACK_UNWIND (frame, op_ret, op_errno, (spec_data?spec_data:""));
  return 0;
}

int32_t
client_checksum (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 int32_t flag)
{
  int32_t ret = -1;
  int64_t ino = 0;
  gf_args_request_t request = {0,};
  data_t *ino_data = NULL;

  if (loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    gf_log (this->name, GF_LOG_ERROR, "%s: returning EINVAL", loc->path);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  request.common = flag;

  request.fields[0].type = GF_PROTO_INT64_TYPE;
  request.fields[0].len = 8;
  request.fields[0].ptr = (void *)&ino;
  
  request.fields[1].type = GF_PROTO_CHAR_TYPE;
  request.fields[1].len = strlen (loc->path);
  request.fields[1].ptr = (void *)loc->path;

  ret = client_protocol_xfer (frame, this, GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_CHECKSUM, &request);

  return ret;
}

int32_t 
client_checksum_cbk (call_frame_t *frame,
		    gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  uint8_t *fchecksum = NULL;
  uint8_t *dchecksum = NULL;

  if (op_ret >= 0) {
    fchecksum = args->fields[0].ptr;
    dchecksum = args->fields[1].ptr;
  }

  STACK_UNWIND (frame, op_ret, op_errno, fchecksum, dchecksum);
  return 0;
}


/*
 * client_setspec_cbk - setspec callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t 
client_setspec_cbk (call_frame_t *frame,
		    gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/* 
 * client_setvolume_cbk - setvolume callback for client protocol
 * @frame:  call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_setvolume_cbk (call_frame_t *frame,
		      gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  if (args->fields[0].len)
    gf_log (frame->this->name, GF_LOG_WARNING, "%s", args->fields[0].ptr);

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_getvolume_cbk - getvolume callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t 
client_getvolume_cbk (call_frame_t *frame,
		      gf_args_reply_t *args)
{
  int32_t op_ret = args->op_ret;
  int32_t op_errno = args->op_errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

void
client_protocol_reconnect (void *trans_ptr)
{
  transport_t *trans = trans_ptr;
  client_proto_priv_t *priv = trans->xl_private;
  struct timeval tv = {0, 0};

  pthread_mutex_lock (&priv->lock);
  {
    if (priv->reconnect)
      gf_timer_call_cancel (trans->xl->ctx, priv->reconnect);
    priv->reconnect = 0;

    if (!priv->connected) {
      uint32_t n_plus_1 = priv->n_minus_1 + priv->n;

      priv->n_minus_1 = priv->n;
      priv->n = n_plus_1;
      tv.tv_sec = n_plus_1;

      gf_log (trans->xl->name, GF_LOG_DEBUG, "attempting reconnect");
      transport_connect (trans);

      priv->reconnect = gf_timer_call_after (trans->xl->ctx, tv,
					     client_protocol_reconnect, trans);
    } else {
      gf_log (trans->xl->name, GF_LOG_DEBUG, "breaking reconnect chain");
      priv->n_minus_1 = 0;
      priv->n = 1;
    }
  }
  pthread_mutex_unlock (&priv->lock);
}

/*
 * client_protocol_cleanup - cleanup function
 * @trans: transport object
 *
 */
static int32_t 
client_protocol_cleanup (transport_t *trans)
{
  client_proto_priv_t *priv = trans->xl_private;
  //  glusterfs_ctx_t *ctx = trans->xl->ctx;
  dict_t *saved_frames = NULL;

  gf_log (trans->xl->name, GF_LOG_WARNING,
	  "cleaning up state in transport object %p", trans);

  pthread_mutex_lock (&priv->lock);
  {
    saved_frames = priv->saved_frames;
    priv->saved_frames = get_new_dict_full (1024);
    data_pair_t *trav = priv->saved_fds->members_list;
    xlator_t *this = trans->xl;
      
    while (trav) {
      fd_t *tmp = (fd_t *)(long) strtoul (trav->key, NULL, 0);
      if (tmp->ctx)
	dict_del (tmp->ctx, this->name);
      trav = trav->next;
    }
      
    dict_destroy (priv->saved_fds);

    priv->saved_fds = get_new_dict_full (64);

  
    /* bailout logic cleanup */
    memset (&(priv->last_sent), 0, sizeof (priv->last_sent));
    memset (&(priv->last_recieved), 0, sizeof (priv->last_recieved));

    if (!priv->timer) {
      gf_log (trans->xl->name, GF_LOG_DEBUG, "priv->timer is NULL!!!!");
    } else {
      gf_timer_call_cancel (trans->xl->ctx, priv->timer);
      priv->timer = NULL;
    }

    if (!priv->reconnect) {
      /* :O This part is empty.. any thing missing? */
    }
  }
  pthread_mutex_unlock (&priv->lock);

  {
    data_pair_t *trav = saved_frames->members_list;
    gf_args_reply_t reply = {0,};
    reply.op_ret = -1;
    reply.op_errno = ENOTCONN;

    while (trav && trav->next)
      trav = trav->next;
    while (trav) {
      /* TODO: reply functions are different for different fops. */
      call_frame_t *tmp = (call_frame_t *) (trav->value->data);

      gf_log (trans->xl->name, GF_LOG_ERROR,
	      "forced unwinding frame type(%d) op(%d)", 
	      tmp->type, tmp->op);

      if (tmp->type == GF_OP_TYPE_FOP_REQUEST)
	gf_fops[tmp->op] (tmp, &reply);
      else
	gf_mops[tmp->op] (tmp, &reply);

      trav = trav->prev;
    }

    dict_destroy (saved_frames);
  }

  return 0;
}

/*
 * client_protocol_interpret - protocol interpreter
 * @trans: transport object
 * @blk: data block
 *
 */
static int32_t
client_protocol_interpret (transport_t *trans,
			   gf_proto_block_t *blk)
{
  int32_t ret = 0;
  gf_args_reply_t reply = {0,};
  dict_t *refs = NULL;
  call_frame_t *frame = NULL;

  frame = lookup_frame (trans, blk->callid);
  if (!frame) {
    gf_log (trans->xl->name, GF_LOG_WARNING,
	    "frame not found for blk with callid: %d",
	    blk->callid);
    return -1;
  }
  /*
  frame->root->rsp_refs = refs = dict_ref (get_new_dict ());
  dict_set (refs, NULL, trans->buf);
  refs->is_locked = 1;
  */
  gf_log ("", GF_LOG_DEBUG, "type %d, op %d, size %d, callid %lld", 
	  blk->type, blk->op, blk->size, blk->callid);

  switch (blk->type) {
  case GF_OP_TYPE_FOP_REPLY:
    {
      if (blk->op > GF_FOP_MAXVALUE || blk->op < 0) {
	gf_log (trans->xl->name, GF_LOG_WARNING,
		"invalid opcode '%d'", blk->op);
	ret = -1;
	break;
      }
      
      if (gf_proto_get_struct_from_buf (blk->args, (gf_args_t *)&reply, blk->size)) {
	ret = -1;
	break;
      }
      gf_fops[blk->op] (frame, &reply);
      
      break;
    }
  case GF_OP_TYPE_MOP_REPLY:
    {
      if (blk->op > GF_MOP_MAXVALUE || blk->op < 0) {
	gf_log (trans->xl->name, GF_LOG_WARNING,
		"invalid opcode '%d'", blk->op);
	return -1;
      }
      if (gf_proto_get_struct_from_buf (blk->args, (gf_args_t *)&reply, blk->size)) {
	ret = -1;
	break;
      }
      gf_mops[blk->op] (frame, &reply);
     
      break;
    }
  default:
    gf_log (trans->xl->name, GF_LOG_WARNING,
	    "invalid packet type: %d", blk->type);
    ret = -1;
  }

  if (refs)
    dict_unref (refs);
  return 0;
}

/*
 * init - initiliazation function. called during loading of client protocol
 * @this:
 *
 */
int32_t 
init (xlator_t *this)
{
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;
  struct rlimit lim;
  data_t *timeout = NULL;
  int32_t transport_timeout = 0;
  data_t *max_block_size_data = NULL;

  if (this->children) {
    gf_log (this->name, 
	    GF_LOG_ERROR,
	    "FATAL: client protocol translator cannot have subvolumes");
    return -1;
  }

  if (!dict_get (this->options, "transport-type")) {
    gf_log (this->name, GF_LOG_DEBUG,
	    "missing 'option transport-type'. defaulting to \"tcp/client\"");
    dict_set (this->options,
	      "transport-type",
	      str_to_data ("tcp/client"));
  }

  if (!dict_get (this->options, "remote-subvolume")) {
    gf_log (this->name, GF_LOG_ERROR,
	    "missing 'option remote-subvolume'.");
    return -1;
  }
  
  timeout = dict_get (this->options, "transport-timeout");
  if (timeout) {
    transport_timeout = data_to_int32 (timeout);
    gf_log (this->name, GF_LOG_DEBUG,
	    "setting transport-timeout to %d", transport_timeout);
  }
  else {
    gf_log (this->name, GF_LOG_DEBUG,
	    "defaulting transport-timeout to 108");
    transport_timeout = 108;
  }

  trans = transport_load (this->options, this, this->notify);

  if (!trans) {
    gf_log (this->name, GF_LOG_ERROR, "Failed to load transport");
    return -1;
  }

  this->private = transport_ref (trans);
  priv = calloc (1, sizeof (client_proto_priv_t));
  priv->saved_frames = get_new_dict_full (1024);
  priv->saved_fds = get_new_dict_full (64);
  priv->callid = 1;
  memset (&(priv->last_sent), 0, sizeof (priv->last_sent));
  memset (&(priv->last_recieved), 0, sizeof (priv->last_recieved));
  priv->transport_timeout = transport_timeout;
  pthread_mutex_init (&priv->lock, NULL);

  max_block_size_data = dict_get (this->options, "limits.transaction-size");

  if (max_block_size_data) {
    priv->max_block_size = gf_str_to_long_long (max_block_size_data->data);
  } else {
    gf_log (this->name, GF_LOG_DEBUG,
	    "defaulting limits.transaction-size to %d", DEFAULT_BLOCK_SIZE);
    priv->max_block_size = DEFAULT_BLOCK_SIZE;
  }
    
  trans->xl_private = priv;

  lim.rlim_cur = 1048576;
  lim.rlim_max = 1048576;

  if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
    gf_log (this->name, GF_LOG_WARNING, "WARNING: Failed to set 'ulimit -n 1048576': %s",
	    strerror(errno));
    lim.rlim_cur = 65536;
    lim.rlim_max = 65536;
  
    if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
      gf_log (this->name, GF_LOG_ERROR, "Failed to set max open fd to 64k: %s", strerror(errno));
    } else {
      gf_log (this->name, GF_LOG_ERROR, "max open fd set to 64k");
    }
  }
  this->notify (this, GF_EVENT_PARENT_UP, this);
  return 0;
}

/*
 * fini - finish function called during unloading of client protocol
 * @this:
 *
 */
void
fini (xlator_t *this)
{
  /* TODO: Check if its enough.. how to call transport's fini () */
  client_proto_priv_t *priv = this->private;

  dict_destroy (priv->saved_frames);
  dict_destroy (priv->saved_fds);
  freee (priv);
  return;
}


static int32_t
client_protocol_handshake_reply (transport_t *trans,
				 gf_proto_block_t *blk)
{
  client_proto_priv_t *priv = trans->xl_private;
  gf_args_reply_t reply;
  void *buf = blk->args;
  char *remote_error;
  int32_t remote_errno;
  int32_t ret = 0;

  if (!blk) {
    gf_log (trans->xl->name, GF_LOG_ERROR,
	    "gf_proto_block_unserialize failed during handshake");
    ret = -1;
  }

  gf_log (trans->xl->name, GF_LOG_DEBUG,
	  "reply frame has callid: %lld", blk->callid);
  
  if (buf == NULL) {
    gf_log (trans->xl->name, GF_LOG_ERROR, "buffer not found");
    ret = -1;
  }
  
  gf_proto_get_struct_from_buf (buf, (gf_args_t *)&reply, blk->size);

  if (!ret)
    ret = reply.op_ret;
  
  if (reply.op_errno)
    remote_errno = reply.op_errno;
  else
    remote_errno = ENOENT;
  
  if (reply.fields[0].ptr)
    remote_error = reply.fields[0].ptr;
  else
    remote_error = "Unknown Error";

  if (ret < 0) {
    gf_log (trans->xl->name, GF_LOG_ERROR,
	    "SETVOLUME on remote-host failed: ret=%d error=%s",
	    ret,  remote_error);
    errno = remote_errno;
  } else {
    gf_log (trans->xl->name, GF_LOG_DEBUG, "SETVOLUME on remote-host succeeded");
  }

  if (!ret) {
    pthread_mutex_lock (&(priv->lock));
    {
      priv->connected = 1;
    }
    pthread_mutex_unlock (&(priv->lock));
  }
  if (trans->xl->parent)
    trans->xl->parent->notify (trans->xl->parent, 
			       GF_EVENT_CHILD_UP, 
			       trans->xl);
  return ret;
}


static int32_t
client_protocol_handshake (xlator_t *this,
			   transport_t *trans)
{
  int32_t ret;
  client_proto_priv_t *priv;
  gf_args_request_t request = {0,};
  dict_t *options;

  priv = trans->xl_private;
  options = this->options;
  
  {
    struct timeval timeout;
    timeout.tv_sec = priv->transport_timeout;
    timeout.tv_usec = 0;
    if (!priv->timer)
      priv->timer = gf_timer_call_after (trans->xl->ctx,
					 timeout,
					 call_bail,
					 (void *)trans);
    else
      gf_log (this->name,
	      GF_LOG_DEBUG,
	      "timer is already registered!!!!");
    
    if (!priv->timer) {
      gf_log (this->name,
	      GF_LOG_DEBUG,
	      "timer creation failed");
    }
  }
  //request.args.setvolume.version_len = strlen (PACKAGE_VERSION);
  request.fields[0].ptr = (void *)PACKAGE_VERSION;
  request.fields[0].len = strlen (PACKAGE_VERSION);
  request.fields[1].ptr = (void *)data_to_str (dict_get (options, "remote-subvolume"));
  request.fields[1].len = strlen (request.fields[1].ptr);
  {
    struct iovec *vector;
    int32_t count;
    int32_t i;
    gf_proto_block_t blk = {0,};

    blk.callid = 42424242;
    blk.type = GF_OP_TYPE_MOP_REQUEST;
    blk.op = GF_MOP_SETVOLUME;
    blk.size = gf_proto_get_data_len ((gf_args_t *)&request);
    blk.args = &request;

    request.uid = 100;
    request.gid = 1000;
    request.pid = 0xff;
    
    /* TODO */
    count = 7;
    vector = alloca (count * (sizeof (*vector)));
    memset (vector, 0, count * (sizeof (*vector)));
    
    gf_proto_block_to_iovec (&blk, vector, &count);
    for (i=0; i<count; i++)
      if (!vector[i].iov_base)
        vector[i].iov_base = alloca (vector[i].iov_len);
    gf_proto_block_to_iovec (&blk, vector, &count);
    ret = trans->ops->writev (trans, vector, count);
    gf_proto_free_args ((gf_args_t *)&request);
  }

  return ret;
}

/*
 * client_protocol_notify - notify function for client protocol
 * @this:
 * @trans: transport object
 * @event
 *
 */

int32_t
notify (xlator_t *this,
	int32_t event,
	void *data,
	...)
{
  int ret = 0;

  switch (event)
    {
    case GF_EVENT_POLLIN:
      {
	transport_t *trans = data;
	client_proto_priv_t *priv = trans->xl_private;
	gf_proto_block_t *blk;
	int32_t connected = 0;

	blk = gf_proto_block_unserialize_transport (trans, priv->max_block_size);
	if (!blk) {
	  ret = -1;
	}

	if (!ret) {
	  pthread_mutex_lock (&(priv->lock));
	  {
	    connected = priv->connected;
	    if (connected)
	      gettimeofday (&(priv->last_recieved), NULL);
	  }
	  pthread_mutex_unlock (&(priv->lock));

	  if (connected) 
	    ret = client_protocol_interpret (trans, blk);
	  else
	    ret = client_protocol_handshake_reply (trans, blk);

	  if (!ret) {
	    freee (blk->args);
	    freee (blk);
	    break;
	  }
	} 
      }
      /* no break for ret check to happen below */
    case GF_EVENT_POLLERR:
      {
	transport_t *trans = data;
	ret = -1;
	client_protocol_cleanup (trans); 
	transport_disconnect (trans);
      }
      client_proto_priv_t *priv = ((transport_t *)data)->xl_private;

      if (priv->connected) {
	transport_t *trans = data;
	struct timeval tv = {0, 0};
	client_proto_priv_t *priv = trans->xl_private;

	if (this->parent)
	  this->parent->notify (this->parent, GF_EVENT_CHILD_DOWN, this);

	priv->n_minus_1 = 0;
	priv->n = 1;

	pthread_mutex_lock (&priv->lock);
	{
	  if (!priv->reconnect)
	    priv->reconnect = gf_timer_call_after (trans->xl->ctx, tv,
						   client_protocol_reconnect,
						   trans);

	  priv->connected = 0;
	}
	pthread_mutex_unlock (&priv->lock);

      }
      break;

    case GF_EVENT_PARENT_UP:
      {
	transport_t *trans = this->private;
	if (!trans) {
	  gf_log (this->name,
		  GF_LOG_DEBUG,
		  "transport init failed");
	  return -1;
	}
	client_proto_priv_t *priv = trans->xl_private;
	struct timeval tv = {0, 0};

	gf_log (this->name, GF_LOG_DEBUG,
		"got GF_EVENT_PARENT_UP, attempting connect on transport");

	//	ret = transport_connect (trans);

	priv->n_minus_1 = 0;
	priv->n = 1;
	priv->reconnect = gf_timer_call_after (trans->xl->ctx, tv,
					       client_protocol_reconnect,
					       trans);

	if (ret) {
	  /* TODO: schedule reconnection with timer */
	}
      }
      break;

    case GF_EVENT_CHILD_UP:
      {
	transport_t *trans = data;
	data_t *handshake = dict_get (this->options, "disable-handshake");
	
	gf_log (this->name, GF_LOG_DEBUG,
		"got GF_EVENT_CHILD_UP");
	if (!handshake || 
	    (strcasecmp (data_to_str (handshake), "on"))) {
	  ret = client_protocol_handshake (this, trans);
	} else {
	  ((client_proto_priv_t *)trans->xl_private)->connected = 1;
	}	  

	if (ret) {
	  transport_disconnect (trans);
	}
      }
      break;

    default:
      gf_log (this->name, GF_LOG_DEBUG,
	      "got %d, calling default_notify ()", event);

      default_notify (this, event, data);
      break;
    }

  return ret;
}


struct xlator_fops fops = {
  .lookup      = client_lookup,
  .stat        = client_stat,
  .readlink    = client_readlink,
  .mknod       = client_mknod,
  .mkdir       = client_mkdir,
  .unlink      = client_unlink,
  .rmdir       = client_rmdir,
  .rmelem      = client_rmelem,
  .symlink     = client_symlink,
  .rename      = client_rename,
  .link        = client_link,
  .chmod       = client_chmod,
  .chown       = client_chown,
  .truncate    = client_truncate,
  .utimens     = client_utimens,
  .open        = client_open,
  .readv       = client_readv,
  .writev      = client_writev,
  .statfs      = client_statfs,
  .flush       = client_flush,
  .close       = client_close,
  .fsync       = client_fsync,
  .incver      = client_incver,
  .setxattr    = client_setxattr,
  .getxattr    = client_getxattr,
  .removexattr = client_removexattr,
  .opendir     = client_opendir,
  .readdir     = client_readdir,
  .closedir    = client_closedir,
  .fsyncdir    = client_fsyncdir,
  .access      = client_access,
  .ftruncate   = client_ftruncate,
  .fstat       = client_fstat,
  .create      = client_create,
  .lk          = client_lk,
  .forget      = client_forget,
  .fchmod      = client_fchmod,
  .fchown      = client_fchown,
  .setdents    = client_setdents,
  .getdents    = client_getdents
};

struct xlator_mops mops = {
  .stats     = client_stats,
  .lock      = client_lock,
  .unlock    = client_unlock,
  .listlocks = client_listlocks,
  .getspec   = client_getspec,
  .checksum  = client_checksum
};

static gf_op_t gf_fops[] = {
  client_stat_cbk,
  client_readlink_cbk,
  client_mknod_cbk,
  client_mkdir_cbk,
  client_unlink_cbk,
  client_rmdir_cbk,
  client_symlink_cbk,
  client_rename_cbk,
  client_link_cbk,
  client_chmod_cbk,
  client_chown_cbk,
  client_truncate_cbk,
  client_open_cbk,
  client_readv_cbk,
  client_write_cbk,
  client_statfs_cbk,
  client_flush_cbk,
  client_close_cbk,
  client_fsync_cbk,
  client_setxattr_cbk,
  client_getxattr_cbk,
  client_removexattr_cbk,
  client_opendir_cbk,
  client_getdents_cbk,
  client_closedir_cbk,
  client_fsyncdir_cbk,
  client_access_cbk,
  client_create_cbk,
  client_ftruncate_cbk,
  client_fstat_cbk,
  client_lk_cbk,
  client_utimens_cbk,
  client_fchmod_cbk,
  client_fchown_cbk,
  client_lookup_cbk,
  client_forget_cbk,
  client_setdents_cbk,
  client_rmelem_cbk,
  client_incver_cbk,
  client_readdir_cbk
};

static gf_op_t gf_mops[] = {
  client_setvolume_cbk,
  client_getvolume_cbk,
  client_stats_cbk,
  client_setspec_cbk,
  client_getspec_cbk,
  client_lock_cbk,
  client_unlock_cbk,
  client_listlocks_cbk,
  client_fsck_cbk,
  client_checksum_cbk,
};

