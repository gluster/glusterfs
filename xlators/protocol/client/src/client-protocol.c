/*
   Copyright (c) 2006, 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "glusterfs.h"
#include "client-protocol.h"
#include "dict.h"
#include "protocol.h"
#include "transport.h"
#include "xlator.h"
#include "logging.h"
#include "timer.h"
#include "defaults.h"

#include <inttypes.h>

#if __WORDSIZE == 64
# define F_L64 "%l"
#else
# define F_L64 "%ll"
#endif

static int32_t client_protocol_interpret (transport_t *trans, gf_block_t *blk);
static int32_t client_protocol_cleanup (transport_t *trans);


typedef int32_t (*gf_op_t) (call_frame_t *frame,
			    dict_t *args);

static gf_op_t gf_fops[];
static gf_op_t gf_mops[];

#if 0
/*
 * str_to_ptr - convert a string to pointer
 * @string: string
 *
 */
static void *
str_to_ptr (char *string)
{
  void *ptr = (void *)strtoul (string, NULL, 16);
  return ptr;
}


/*
 * ptr_to_str - convert a pointer to string
 * @ptr: pointer
 *
 */
static char *
ptr_to_str (void *ptr)
{
  char *str;
  asprintf (&str, "%p", ptr);
  return str;
}
#endif

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
  client_proto_priv_t *priv = trans->xl_private;
  char buf[64];
  call_frame_t *frame = NULL;
  snprintf (buf, 64, "%"PRId64, callid);

  pthread_mutex_lock (&priv->lock);
  frame = data_to_bin (dict_get (priv->saved_frames, buf));
  dict_del (priv->saved_frames, buf);
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
	gf_log (((transport_t *)trans)->xl->name,
		GF_LOG_DEBUG,
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
	      "activating bail-out. pending frames = %d. last sent = %s. last received = %s transport-timeout = %d", priv->saved_frames->count, last_sent, last_received, priv->transport_timeout);
    }
  }
  pthread_mutex_unlock (&priv->lock);

  if (bail_out) {
    gf_log (((transport_t *)trans)->xl->name,
	  GF_LOG_CRITICAL,
	  "bailing transport");
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
		      dict_t *request)
{
  int32_t ret;
  transport_t *trans;
  client_proto_priv_t *proto_priv;

  if (!request) {
    gf_log (this->name, GF_LOG_ERROR,
	    "request is NULL");
    return -1;
  }

  trans = this->private;
  if (!trans) {
    gf_log (this->name, GF_LOG_ERROR,
	    "this->private is NULL");
    return -1;
  }
  

  proto_priv = trans->xl_private;
  if (!proto_priv) {
    gf_log (this->name, GF_LOG_ERROR,
	    "trans->xl_private is NULL");
    return -1;
  }

  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));
  dict_set (request, "CALLER_PID", data_from_uint64 (frame->root->pid));

  {
    int64_t callid;
    gf_block_t *blk;
    struct iovec *vector = NULL;
    int32_t count = 0;
    int32_t i;
    char connected = 0;
    char buf[64];

    pthread_mutex_lock (&proto_priv->lock);
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
      dict_set (proto_priv->saved_frames,
		buf,
		bin_to_data (frame, sizeof (frame)));
    }
    pthread_mutex_unlock (&proto_priv->lock);

    blk = gf_block_new (callid);
    blk->type = type;
    blk->op = op;
    blk->size = 0;    // obselete
    blk->data = NULL; // obselete
    blk->dict = request;

    count = gf_block_iovec_len (blk);
    vector = alloca (count * sizeof (*vector));
    memset (vector, 0, count * sizeof (*vector));

    gf_block_to_iovec (blk, vector, count);
    for (i=0; i<count; i++)
      if (!vector[i].iov_base)
	vector[i].iov_base = alloca (vector[i].iov_len);
    gf_block_to_iovec (blk, vector, count);

    ret = -1;

    if (connected) {
      client_proto_priv_t *priv = ((transport_t *)this->private)->xl_private;

      ret = trans->ops->writev (trans, vector, count);

      pthread_mutex_lock (&(priv->lock));
      gettimeofday (&(priv->last_sent), NULL);
      pthread_mutex_unlock (&(priv->lock));
    }

    free (blk);

    if (ret != 0) {
      if (connected) {
	gf_log (this->name, GF_LOG_ERROR,
		"transport_submit failed");

	//	transport_except (trans);
      } else {
	dict_t *reply = get_new_dict ();

	reply->is_locked = 1;
	gf_log (this->name, GF_LOG_WARNING,
		"not connected at the moment to submit frame type(%d) op(%d)",
		type, op);
	frame->root->rsp_refs = dict_ref (reply);
	if (type == GF_OP_TYPE_FOP_REQUEST)
	  gf_fops[op] (frame, reply);
	else
	  gf_mops[op] (frame, reply);
	dict_unref (reply);
	//	client_protocol_cleanup (trans);
      }
      return -1;
    }
  }
  return ret;
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
 
static int32_t 
client_create (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t flags,
	       mode_t mode,
	       fd_t *fd)
{
  dict_t *request = get_new_dict ();
  int32_t ret = 0;
  client_local_t *local = NULL;

  local = calloc (1, sizeof (client_local_t));
  local->inode = loc->inode;
  local->fd = fd;
  
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)loc->path));
  dict_set (request, "FLAGS", data_from_int64 (flags));
  dict_set (request, "MODE", data_from_int64 (mode));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CREATE,
			      request);
  dict_destroy (request);
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

static int32_t 
client_open (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t flags,
	     fd_t *fd)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  ino_t ino = 0;
  client_local_t *local = NULL;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, fd);
    return 0;
  }

  dict_set (request, "PATH", str_to_data ((char *)loc->path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "FLAGS", data_from_int64 (flags));

  local = calloc (1, sizeof (client_local_t));
  local->inode = loc->inode;
  local->fd = fd;
  
  frame->local = local;
  
  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_OPEN,
			      request);

  dict_destroy (request);
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

static int32_t 
client_stat (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  const char *path = loc->path;
  ino_t ino = 0;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  
  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_STAT,
			      request);

  dict_destroy (request);
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


static int32_t 
client_readlink (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 size_t size)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  const char *path = loc->path;
  ino_t ino = 0;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "LEN", data_from_int64 (size));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_READLINK,
			      request);

  dict_destroy (request);
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

static int32_t 
client_mknod (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode,
	      dev_t dev)
{
  dict_t *request = get_new_dict ();
  int32_t ret = 0;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  
  local->inode = loc->inode;
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)loc->path));
  dict_set (request, "MODE", data_from_int64 (mode));
  dict_set (request, "DEV", data_from_int64 (dev));
  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_MKNOD,
			      request);

  dict_destroy (request);
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


static int32_t 
client_mkdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode)
{
  dict_t *request = get_new_dict ();
  int32_t ret = 0;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  local->inode = loc->inode;
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)loc->path));
  dict_set (request, "MODE", data_from_int64 (mode));
  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_MKDIR,
			      request);

  dict_destroy (request);
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

static int32_t 
client_unlink (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  dict_t *request = NULL;
  int32_t ret = -1;
  const char *path = loc->path;
  ino_t ino = 0;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }

  request = get_new_dict ();
  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_UNLINK,
			      request);

  dict_destroy (request);
  return ret;
}

static int32_t
client_rmelem (call_frame_t *frame,
	       xlator_t *this,
	       const char *path)
{
  dict_t *request = get_new_dict();
  int32_t ret = -1;

  dict_set (request, "PATH", str_to_data ((char *)path));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_RMELEM,
			      request);
  dict_destroy (request);
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

static int32_t 
client_rmdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  const  char *path = loc->path;
  ino_t ino = 0;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_RMDIR,
			      request);

  dict_destroy (request);
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

static int32_t 
client_symlink (call_frame_t *frame,
		xlator_t *this,
		const char *linkname,
		loc_t *loc)
{
  dict_t *request = get_new_dict ();
  int32_t ret = 0;
  client_local_t *local = NULL;

  local = calloc (1, sizeof (client_local_t));
  local->inode = loc->inode;
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)loc->path));
  dict_set (request, "SYMLINK", str_to_data ((char *)linkname));
  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_SYMLINK,
			      request);

  dict_destroy (request);
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

static int32_t 
client_rename (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *oldloc,
	       loc_t *newloc)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  ino_t ino = 0, newino = 0;
  data_t *ino_data = dict_get (oldloc->inode->ctx, this->name);
  data_t *newino_data = NULL;

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  if (newloc->inode) {
    newino_data = dict_get (newloc->inode->ctx, this->name);
    if (newino_data) 
      newino = data_to_uint64 (newino_data);
  }

  dict_set (request, "PATH", str_to_data ((char *)oldloc->path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "NEWPATH", str_to_data ((char *)newloc->path));
  dict_set (request, "NEWINODE", data_from_uint64 (newino));
  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_RENAME,
			      request);

  dict_destroy (request);
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

static int32_t 
client_link (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *oldloc,
	     const char *newpath)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  const char *oldpath = oldloc->path;
  ino_t oldino = 0;
  data_t *oldino_data = dict_get (oldloc->inode->ctx, this->name);

  if (oldino_data) {
    oldino = data_to_uint64 (oldino_data);
  } else {
    TRAP_ON (oldino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, oldloc->inode, NULL);
    return 0;
  }

  local->inode = oldloc->inode;
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)oldpath));
  dict_set (request, "INODE", data_from_uint64 (oldino));
  dict_set (request, "LINK", str_to_data ((char *)newpath));
  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LINK,
			      request);

  dict_destroy (request);
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

static int32_t 
client_chmod (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  ino_t ino = 0;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "MODE", data_from_int64 (mode));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CHMOD,
			      request);

  dict_destroy (request);
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

static int32_t 
client_chown (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      uid_t uid,
	      gid_t gid)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  ino_t ino = 0;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));
  dict_set (request, "UID", data_from_uint64 (uid));
  dict_set (request, "GID", data_from_uint64 (gid));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CHOWN,
			      request);

  dict_destroy (request);
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

static int32_t 
client_truncate (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 off_t offset)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  ino_t ino = 0;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "OFFSET", data_from_int64 (offset));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_TRUNCATE,
			      request);

  dict_destroy (request);
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

static int32_t 
client_utimens (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		struct timespec *tvp)
{
  dict_t *request = get_new_dict ();
  int32_t ret = 0;
  const char *path = loc->path;
  ino_t ino = 0;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "ACTIME_SEC", data_from_int64 (tvp[0].tv_sec));
  dict_set (request, "ACTIME_NSEC", data_from_int64 (tvp[0].tv_nsec));
  dict_set (request, "MODTIME_SEC", data_from_int64 (tvp[1].tv_sec));
  dict_set (request, "MODTIME_NSEC", data_from_int64 (tvp[1].tv_nsec));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_UTIMENS,
			      request);

  dict_destroy (request);
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

static int32_t 
client_readv (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      size_t size,
	      off_t offset)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (fd->ctx, this->name);
  int32_t ret;

  if (!ctx_data) {
    struct iovec vec;
    struct stat dummy = {0, };
    vec.iov_base = "";
    vec.iov_len = 0;
    dict_destroy (request);
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, &vec, &dummy);
    return 0;
  }

  dict_set (request, "FD", str_to_data (data_to_str (ctx_data)));
  dict_set (request, "OFFSET", data_from_int64 (offset));
  dict_set (request, "LEN", data_from_int64 (size));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_READ,
			      request);

  dict_destroy (request);
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

static int32_t 
client_writev (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       struct iovec *vector,
	       int32_t count,
	       off_t offset)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (fd->ctx, this->name);
  size_t size = 0, i;
  int32_t ret = -1;
  char *fd_str = NULL;

  if (!ctx_data) {
    struct stat dummy = {0, };
    dict_destroy (request);
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, &dummy);
    return 0;
  }
 
  for (i = 0; i<count; i++)
    size += vector[i].iov_len;

  fd_str = strdup (data_to_str (ctx_data));

  dict_set (request, "FD", str_to_data (fd_str));
  dict_set (request, "OFFSET", data_from_int64 (offset));
  dict_set (request, "BUF", data_from_iovec (vector, count));
  dict_set (request, "LEN", data_from_int64 (size));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_WRITE,
			      request);

  dict_destroy (request);
  free (fd_str);
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

static int32_t 
client_statfs (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  ino_t ino = 1; /* default it to root's inode number */

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_STATFS,
			      request);

  dict_destroy (request);
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

static int32_t 
client_flush (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (fd->ctx, this->name);
  int32_t ret = -1;
  char *fd_str = NULL;

  if (!ctx_data) {
    dict_destroy (request);
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  fd_str = strdup (data_to_str (ctx_data));
  dict_set (request, "FD", str_to_data (fd_str));
  
  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FLUSH,
			      request);

  free (fd_str);
  dict_destroy (request);
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

static int32_t 
client_close (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  data_t *ctx_data = dict_get (fd->ctx, this->name);
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;
  int32_t ret = -1;
  char *key = NULL;
  char *fd_str = NULL;

  trans = frame->this->private;

  if (ctx_data) {
    dict_t *request = get_new_dict ();
  
    fd_str = strdup (data_to_str (ctx_data));
    dict_set (request, "FD", data_from_dynstr (fd_str));

    ret = client_protocol_xfer (frame,
				this,
				GF_OP_TYPE_FOP_REQUEST,
				GF_FOP_CLOSE,
				request);
    dict_destroy (request);
  } else {
    STACK_UNWIND (frame, 0, 0);
  }

  priv = trans->xl_private;
  
  asprintf (&key, "%p", fd);

  pthread_mutex_lock (&priv->lock);
  dict_del (priv->saved_fds, key); 
  pthread_mutex_unlock (&priv->lock);
  
  free (key);

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

static int32_t 
client_fsync (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (fd->ctx, this->name);
  int32_t ret = -1;
  char *fd_str = NULL;

  if (!ctx_data) {
    dict_destroy (request);
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  dict_set (request, "FLAGS", data_from_int64 (flags));
  
  fd_str = strdup (data_to_str (ctx_data));
  dict_set (request, "FD", str_to_data (fd_str));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FSYNC,
			      request);

  free (fd_str);
  dict_destroy (request);
  return ret;
}


static int32_t
client_incver (call_frame_t *frame,
	       xlator_t *this,
	       const char *path)
{
  dict_t *request = get_new_dict();
  int32_t ret = -1;

  dict_set (request, "PATH", str_to_data ((char *) path));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_INCVER,
			      request);

  dict_destroy (request);
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

static int32_t 
client_setxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 dict_t *dict,
		 int32_t flags)
{
  int32_t ret = -1;  
  dict_t *request = get_new_dict ();
  const char *path = loc->path;
  ino_t ino = 0;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "FLAGS", data_from_int64 (flags));

  {
    /* Serialize the dictionary and set it as a parameter in 'request' dict */
    int32_t len = dict_serialized_length (dict);
    char *dict_buf = alloca (len);
    dict_serialize (dict, dict_buf);
    dict_set (request, "DICT", bin_to_data (dict_buf, len));
  }


  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_SETXATTR,
			      request);

  dict_destroy (request);
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

static int32_t 
client_getxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc)
{
  int32_t ret = -1;  
  dict_t *request = get_new_dict ();
  const char *path = loc->path;
  ino_t ino = 0;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));


  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_GETXATTR,
			      request);

  dict_destroy (request);
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
	     
static int32_t 
client_removexattr (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    const char *name)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  ino_t ino = 0;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }


  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "NAME", str_to_data ((char *)name));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_REMOVEXATTR,
			      request);

  dict_destroy (request);
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

static int32_t 
client_opendir (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		fd_t *fd)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  ino_t ino = 0;
  client_local_t *local = NULL;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, fd);
    return 0;
  }

  local = calloc (1, sizeof (client_local_t));
  local->inode = loc->inode;
  local->fd = fd;
  
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_OPENDIR,
			      request);

  dict_destroy (request);
  return ret;
}


/**
 * client_readdir - readdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 *
 * external reference through client_protocol_xlator->fops->readdir
 */

static int32_t 
client_readdir (call_frame_t *frame,
		xlator_t *this,
		size_t size,
		off_t offset,
		fd_t *fd)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  data_t *fd_data = dict_get (fd->ctx, this->name);
  char *fd_str = NULL;

  if (!fd_data) {
    dict_destroy (request);
    TRAP_ON (fd_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, NULL, 0);
    return 0;
  }

  fd_str = strdup (data_to_str (fd_data));
  dict_set (request, "FD", str_to_data (fd_str));
  dict_set (request, "OFFSET", data_from_uint64 (offset));
  dict_set (request, "SIZE", data_from_uint64 (size));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_READDIR,
			      request);
  
  free (fd_str);
  dict_destroy (request);
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

static int32_t 
client_closedir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd)
{
  data_t *ctx_data = dict_get (fd->ctx, this->name);
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;
  int32_t ret = -1;
  char *key = NULL;
  char *fd_str = NULL;


  trans = frame->this->private;

  if (ctx_data) {
    dict_t *request = get_new_dict ();
  
    fd_str = strdup (data_to_str (ctx_data));
    dict_set (request, "FD", data_from_dynstr (fd_str));


    ret = client_protocol_xfer (frame,
				this,
				GF_OP_TYPE_FOP_REQUEST,
				GF_FOP_CLOSEDIR,
				request);
    dict_destroy (request);
  } else {
    STACK_UNWIND (frame, 0, 0);
  }

  priv = trans->xl_private;
  
  asprintf (&key, "%p", fd);

  pthread_mutex_lock (&priv->lock);
  dict_del (priv->saved_fds, key); 
  pthread_mutex_unlock (&priv->lock);
  
  free (key);
  //  free (data_to_str (ctx_data)); caused double free ?

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

static int32_t 
client_fsyncdir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t flags)
{
  int32_t ret = -1;
  dict_t *ctx = fd->ctx;
  data_t *ctx_data = dict_get (ctx, this->name);

  if (!ctx_data) {
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD);
    return -1;
  }

  /*  int32_t remote_errno = 0;
      struct brick_private *priv = this->private;
      dict_t *request = get_new_dict ();
      dict_t *reply = get_new_dict ();

      {
      dict_set (request, "PATH", str_to_data ((char *)path));
      dict_set (request, "FLAGS", int_to_data (datasync));
      }

      ret = fops_xfer (priv, OP_FSYNCDIR, request, reply);
      dict_destroy (request);

      if (ret != 0)
      goto ret;

      ret = data_to_int (dict_get (reply, "RET"));
      remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
      if (ret < 0) {
      errno = remote_errno;
      goto ret;      }

      ret:
      dict_destroy (reply); */

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

static int32_t 
client_access (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t mask)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  ino_t ino = 0;
  data_t *ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    ino = data_to_uint64 (ino_data);
  } else {
    TRAP_ON (ino_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
    return 0;
  }

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "MASK", data_from_int64 (mask));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_ACCESS,
			      request);

  dict_destroy (request);
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

static int32_t 
client_ftruncate (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  off_t offset)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (fd->ctx, this->name);
  int32_t ret = -1;
  char *fd_str = NULL;

  if (!ctx_data) {
    dict_destroy (request);
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }
  
  fd_str = strdup (data_to_str (ctx_data));
  dict_set (request, "FD", str_to_data (fd_str));
  dict_set (request, "OFFSET", data_from_int64 (offset));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FTRUNCATE,
			      request);
  
  free (fd_str);
  dict_destroy (request);
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

static int32_t 
client_fstat (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  dict_t *request = get_new_dict ();
  data_t *fd_data = dict_get (fd->ctx, this->name);
  int32_t ret = -1;
  char *fd_str = NULL;

  if (!fd_data) {
    dict_destroy (request);
    TRAP_ON (fd_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }
  
  fd_str = strdup (data_to_str (fd_data));
  dict_set (request, "FD", str_to_data (fd_str));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FSTAT,
			      request);
  
  free (fd_str);
  dict_destroy (request);
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

static int32_t 
client_lk (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   int32_t cmd,
	   struct flock *lock)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (fd->ctx, this->name);
  int32_t ret = -1;
  char *fd_str = NULL;

  if (!ctx_data) {
    dict_destroy (request);
    TRAP_ON (ctx_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }

  fd_str = strdup (data_to_str (ctx_data));
  dict_set (request, "FD", str_to_data (fd_str));
  dict_set (request, "CMD", data_from_int32 (cmd));
  dict_set (request, "TYPE", data_from_int16 (lock->l_type));
  dict_set (request, "WHENCE", data_from_int16 (lock->l_whence));
  dict_set (request, "START", data_from_int64 (lock->l_start));
  dict_set (request, "LEN", data_from_int64 (lock->l_len));
  dict_set (request, "PID", data_from_uint64 (lock->l_pid));
  dict_set (request, "CLIENT_PID", data_from_uint64 (getpid ()));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LK,
			      request);

  free (fd_str);
  dict_destroy (request);
  return ret;
}

/** 
 * client_writedir - 
 */
static int32_t
client_writedir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t flags,
		 dir_entry_t *entries,
		 int32_t count)
{
  int32_t ret = -1;
  char *buffer = NULL;
  dict_t *request = get_new_dict ();
  data_t *fd_data = dict_get (fd->ctx, this->name);
  char *fd_str = NULL;

  if (!fd_data) {
    dict_destroy (request);
    TRAP_ON (fd_data == NULL);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  fd_str = strdup (data_to_str (fd_data));
  dict_set (request, "FD", str_to_data (fd_str));
  dict_set (request, "FLAGS", data_from_int32 (flags));
  dict_set (request, "NR_ENTRIES", data_from_int32 (count));

  {   
    dir_entry_t *trav = entries->next;
    uint32_t len = 0;
    char *ptr = NULL;
    while (trav) {
      len += strlen (trav->name);
      len += 1;
      len += 256; // max possible for statbuf;
      trav = trav->next;
    }
    buffer = calloc (1, len);
    ptr = buffer;
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
    dict_set (request, "DENTRIES", data_from_dynstr (buffer));
  }
  
  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_WRITEDIR,
			      request);

  free (fd_str);
  dict_destroy (request);

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
static int32_t 
client_lookup (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t need_xattr)
{
  dict_t *request = get_new_dict ();
  const char *path = loc->path;
  int32_t ret = -1;
  client_local_t *local = NULL;
  ino_t ino = 0;
  data_t *ino_data = NULL;

  if (loc && loc->inode && loc->inode->ctx)
    ino_data = dict_get (loc->inode->ctx, this->name);

  if (ino_data) {
    /* revalidate */
    ino = data_to_uint64 (ino_data);
  }

  local = calloc (1, sizeof (client_local_t));

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "NEED_XATTR", data_from_int32 (need_xattr));
  local->inode = loc->inode;
  frame->local = local;

  ret = client_protocol_xfer (frame, 
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LOOKUP,
			      request);
  
  dict_destroy (request);
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
static int32_t
client_forget (call_frame_t *frame,
	       xlator_t *this,
	       inode_t *inode)
{
  int32_t ret = 0;
  call_frame_t *fr = 0;
  ino_t ino = 0;
  data_t *ino_data = dict_get (inode->ctx, this->name);

  if (ino_data) {
    dict_t *request = get_new_dict ();
    ino = data_to_uint64 (ino_data);

    fr = create_frame (this, this->ctx->pool);

    dict_set (request, "INODE", data_from_uint64 (ino));
    
    ret = client_protocol_xfer (fr, this,
				GF_OP_TYPE_FOP_REQUEST,
				GF_FOP_FORGET,
				request);

    dict_destroy (request);
  }
  return ret;
}


/*
 * client_fchmod
 *
 */
static int32_t
client_fchmod (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       mode_t mode)
{
  dict_t *request = NULL;
  data_t *remote_fd_data = NULL;
  char *remote_fd = NULL;
  int32_t ret = -1;

  request = get_new_dict ();

  remote_fd_data = dict_get (fd->ctx, this->name);
  remote_fd      = strdup (data_to_str (remote_fd_data));

  dict_set (request, "FD", str_to_data (remote_fd));
  dict_set (request, "MODE", data_from_uint64 (mode));

  ret = client_protocol_xfer (frame, 
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FCHMOD,
			      request);

  free (remote_fd);
  dict_destroy (request);
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
static int32_t
client_fchmod_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = NULL, *errno_data = NULL, *stat_data = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  char *stat_str = NULL;
  struct stat *stbuf = NULL;

  ret_data = dict_get (args, "RET");
  errno_data = dict_get (args, "ERRNO");
  stat_data = dict_get (args, "STAT");
  
  if (!ret_data || !errno_data) {
    STACK_UNWIND (frame, op_ret, op_errno, stbuf);
    return -1;
  }
  
  op_ret = data_to_uint64 (ret_data);
  op_errno = data_to_uint64 (errno_data);

  if (op_ret >= 0) {
    stat_str = data_to_str (stat_data);
    stbuf = str_to_stat (stat_str);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    free (stbuf);

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
static int32_t
client_fchown (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       uid_t uid,
	       gid_t gid)
{
  dict_t *request = NULL;
  data_t *remote_fd_data = NULL;
  char *remote_fd = NULL;
  int32_t ret = -1;

  request = get_new_dict ();

  remote_fd_data = dict_get (fd->ctx, this->name);
  remote_fd      = strdup (data_to_str (remote_fd_data));

  dict_set (request, "FD", str_to_data (remote_fd));
  dict_set (request, "UID", data_from_uint64 (uid));
  dict_set (request, "GID", data_from_uint64 (gid));

  ret = client_protocol_xfer (frame, 
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FCHMOD,
			      request);

  free (remote_fd);
  dict_destroy (request);
  return 0;

}

/*
 * client_chown_cbk - 
 * 
 * @frame:
 * @args:
 *
 * not for external reference
 */
static int32_t
client_fchown_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = NULL, *errno_data = NULL, *stat_data = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  char *stat_str = NULL;
  struct stat *stbuf = NULL;

  ret_data = dict_get (args, "RET");
  errno_data = dict_get (args, "ERRNO");
  stat_data = dict_get (args, "STAT");
  
  if (!ret_data || !errno_data) {
    STACK_UNWIND (frame, op_ret, op_errno, stbuf);
    return -1;
  }
  
  op_ret = data_to_uint64 (ret_data);
  op_errno = data_to_uint64 (errno_data);

  if (op_ret >= 0) {
    stat_str = data_to_str (stat_data);
    stbuf = str_to_stat (stat_str);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    free (stbuf);

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

static int32_t 
client_stats (call_frame_t *frame,
	      xlator_t *this, 
	      int32_t flags)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  /* without this dummy key the server crashes */
  dict_set (request, "FLAGS", data_from_int64 (0)); 
  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_STATS,
			      request);

  dict_destroy (request);

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

//TODO: make it static (currently !static because of the warning)
int32_t 
client_fsck (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)
{
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

static int32_t 
client_lock (call_frame_t *frame,
	     xlator_t *this,
	     const char *name)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;

  dict_set (request, "PATH", str_to_data ((char *)name));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_LOCK,
			      request);

  dict_destroy (request);

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

static int32_t 
client_unlock (call_frame_t *frame,
	       xlator_t *this,
	       const char *name)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;

  dict_set (request, "PATH", str_to_data ((char *)name));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_UNLOCK, request);

  dict_destroy (request);

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

static int32_t 
client_listlocks (call_frame_t *frame,
		  xlator_t *this,
		  const char *pattern)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;

  dict_set (request, "OP", data_from_uint64 (0xcafebabe));
  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_LISTLOCKS,
			      request);

  dict_destroy (request);

  return ret;
}



/* Callbacks */
#define CLIENT_PRIVATE(frame) (((transport_t *)(frame->this->private))->xl_private)
/*
 * client_create_cbk - create callback function for client protocol
 * @frame: call frame
 * @args: arguments in dictionary
 *
 * not for external reference 
 */

static int32_t 
client_create_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *stat_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *fd_data = dict_get (args, "FD");
  int32_t op_ret = 0;
  int32_t op_errno = ENOTCONN;
  client_local_t *local = frame->local;
  char *stat_buf = NULL;
  struct stat *stbuf = NULL;
  client_proto_priv_t *priv = NULL;
  inode_t *inode = NULL;
  fd_t *fd = NULL;


  fd = local->fd;
  inode = local->inode;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, fd, inode, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);


  if (op_ret >= 0) {
    /* handle fd */
    char *remote_fd = strdup (data_to_str (fd_data));
    char *key = NULL;

    stat_buf = data_to_str (stat_data);
    stbuf = str_to_stat (stat_buf);

    /* add newly created file's inode to  inode table */
    dict_set (inode->ctx, (frame->this)->name, data_from_uint64 (stbuf->st_ino));

    dict_set (fd->ctx, (frame->this)->name,  data_from_dynstr (remote_fd));

    asprintf (&key, "%p", fd);

    priv = CLIENT_PRIVATE (frame);
    pthread_mutex_lock (&priv->lock);
    dict_set (priv->saved_fds, key, str_to_data ("")); 
    pthread_mutex_unlock (&priv->lock);

    free (key);
  }

  STACK_UNWIND (frame, op_ret, op_errno, fd, inode, stbuf);
  
  if (stbuf)
    free (stbuf);

  return 0;
}

/*
 * client_open_cbk - open callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_open_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *fd_data = dict_get (args, "FD");
  client_proto_priv_t *priv = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = 0;
  client_local_t *local = frame->local;
  fd_t *fd = local->fd; 

  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, fd);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);
  
  fd = local->fd;

  if (op_ret >= 0) {
    /* handle fd */
    char *remote_fd = strdup (data_to_str (fd_data));
    char *key = NULL;

    dict_set (fd->ctx, (frame->this)->name, data_from_dynstr (remote_fd));
    
    asprintf (&key, "%p", fd);

    priv = CLIENT_PRIVATE (frame);
    pthread_mutex_lock (&priv->lock);
    dict_set (priv->saved_fds, key, str_to_data (""));
    pthread_mutex_unlock (&priv->lock);

    free (key);
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
static int32_t 
client_stat_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = 0; 
  char *buf = NULL; 
  struct stat *stbuf = NULL;
    
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  

  if (op_ret >= 0) {
    buf = data_to_str (buf_data);
    stbuf = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    free (stbuf);

  return 0;
}

/* 
 * client_utimens_cbk - utimens callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
 
static int32_t 
client_utimens_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = 0;
  char *buf = NULL;
  struct stat *stbuf = NULL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  if (op_ret >= 0) {
    buf = data_to_str (buf_data);
    stbuf = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  
  if (stbuf) 
    free (stbuf);

  return 0;
}

/*
 * client_chmod_cbk - chmod for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_chmod_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = ENOTCONN; 
  char *buf = NULL;
  struct stat *stbuf = NULL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }

  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  

  if (op_ret >= 0) {
    buf = data_to_str (buf_data);
    stbuf = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    free (stbuf);

  return 0;
}

/*
 * client_chown_cbk - chown for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference 
 */
static int32_t 
client_chown_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = ENOTCONN; 
  char *buf = NULL;
  struct stat *stbuf = NULL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }

  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  

  if (op_ret >= 0) {
    buf = data_to_str (buf_data);
    stbuf = str_to_stat (buf);
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    free (stbuf);

  return 0;
}

/* 
 * client_mknod_cbk - mknod callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_mknod_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  char *buf = NULL;
  struct stat *stbuf = NULL;
  client_local_t *local = frame->local;
  inode_t *inode = local->inode;

  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, inode, stbuf);
    return 0;
  }

  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  inode = local->inode;
  
  if (op_ret >= 0){
    /* handle inode */
    buf = data_to_str (buf_data);
    stbuf = str_to_stat (buf);
    dict_set (inode->ctx, (frame->this)->name, data_from_uint64 (stbuf->st_ino));
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);
  
  if (stbuf)
    free (stbuf);

  return 0;
}

/*
 * client_symlink_cbk - symlink callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_symlink_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *stat_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  char *stat_str = NULL;
  struct stat *stbuf = NULL;
  client_local_t *local = frame->local;
  inode_t *inode = local->inode;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, inode, stbuf);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  inode = local->inode;

  if (op_ret >= 0){
    /* handle inode */
    stat_str = data_to_str (stat_data);
    stbuf = str_to_stat (stat_str);
    dict_set (inode->ctx, (frame->this)->name, data_from_uint64 (stbuf->st_ino));
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);
  
  if (stbuf)
    free (stbuf);

  return 0;
}

/*
 * client_link_cbk - link callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference 
 */
static int32_t 
client_link_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = ENOTCONN;
  char *buf = NULL;
  struct stat *stbuf = NULL;
  client_local_t *local = frame->local;
  inode_t *inode = local->inode;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, inode, stbuf);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);
  inode = local->inode;
    
  if (op_ret >= 0){
    /* handle inode */
    buf = data_to_str (buf_data);
    stbuf = str_to_stat (buf);
    dict_set (inode->ctx, (frame->this)->name, data_from_uint64 (stbuf->st_ino));
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);

  if (stbuf)
    free (stbuf);

  return 0;
}

/* 
 * client_truncate_cbk - truncate callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference 
 */

static int32_t 
client_truncate_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = ENOTCONN;
  char *buf = NULL;
  struct stat *stbuf = NULL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, stbuf);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  

  if (op_ret >= 0) {
    buf = data_to_str (buf_data);
    stbuf = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    free (stbuf);

  return 0;
}

/* client_fstat_cbk - fstat callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

static int32_t 
client_fstat_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *stat_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = ENOTCONN;
  char *stat_buf = NULL;
  struct stat *stbuf = NULL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, stbuf);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  

  if (op_ret >= 0) {
    stat_buf = data_to_str (stat_data);
    stbuf = str_to_stat (stat_buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    free (stbuf);

  return 0;
}

/* 
 * client_ftruncate_cbk - ftruncate callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */ 
static int32_t 
client_ftruncate_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = ENOTCONN; 
  char *buf = NULL;
  struct stat *stbuf = NULL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, stbuf);
    return 0;
  }

  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  

  if (op_ret >= 0) {
    buf = data_to_str (buf_data);
    stbuf = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    free (stbuf);

  return 0;
}

/* client_readv_cbk - readv callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external referece
 */

static int32_t 
client_readv_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *stat_data = dict_get (args, "STAT");
  char *stat_str = NULL;
  struct stat *stbuf = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  char *buf = NULL;
  struct iovec vec = {0,};
  
  if (!buf_data || !ret_data || !err_data) {
    struct stat stbuf = {0,};
    STACK_UNWIND (frame, -1, ENOTCONN, NULL, 1, &stbuf);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  

  if (op_ret >= 0) {
    buf = data_to_bin (buf_data);
    stat_str = data_to_str (stat_data);
    stbuf = str_to_stat (stat_str);
    vec.iov_base = buf;
    vec.iov_len = op_ret;
  }

  STACK_UNWIND (frame, op_ret, op_errno, &vec, 1, stbuf);

  if (stbuf)
    free (stbuf);

  return 0;
}

/* 
 * client_write_cbk - write callback for client protocol
 * @frame: cal frame
 * @args: argument dictionary
 *
 * not for external reference
 */

static int32_t 
client_write_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *stat_data = dict_get (args, "STAT");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  char *stat_str = NULL;
  struct stat *stbuf = NULL;
  
  if (!ret_data || !err_data) {
    struct stat stbuf = {0,};
    STACK_UNWIND (frame, -1, ENOTCONN, &stbuf);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
 
 if (op_ret >= 0) {
    stat_str = data_to_str (stat_data);
    stbuf = str_to_stat (stat_str);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    free (stbuf);

  return 0;
}

/*
 * client_readdir_cbk - readdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_readdir_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *buf_data = dict_get (args, "DENTRIES");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *cnt_data = dict_get (args, "NR_ENTRIES");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  int32_t nr_count = 0;
  char *buf = NULL;
  
  dir_entry_t *entry = NULL;
  dir_entry_t *trav = NULL, *prev = NULL;
  int32_t count, i, bread;
  char *ender = NULL, *buffer_ptr = NULL;
  char tmp_buf[512] = {0,};
  
  if (!buf_data || !ret_data || !err_data || !cnt_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL, 0);
    return 0;
  }
  
  op_ret   = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  nr_count = data_to_int32 (cnt_data);
  buf      = data_to_str (buf_data);
  
  entry = calloc (1, sizeof (dir_entry_t));
  prev = entry;
  buffer_ptr = buf;
  
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

/*
 * client_fsync_cbk - fsync callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_fsync_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_unlink_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
client_rmelem_cbk (call_frame_t *frame,
		   dict_t *args)
{
  int32_t op_ret, op_errno;

  op_ret = data_to_int32 (dict_get (args, "RET"));
  op_errno = data_to_int32 (dict_get (args, "ERRNO"));

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
static int32_t 
client_rename_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *stat_data = dict_get (args, "STAT");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  char *buf = NULL;
  struct stat *stbuf = NULL;

  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }
  
  op_ret   = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  

  if (op_ret >= 0) {
    buf      = data_to_str (stat_data);
    stbuf    = str_to_stat (buf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    free (stbuf);

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
static int32_t 
client_readlink_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *buf_data = dict_get (args, "LINK");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  char *buf = NULL;
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }
  
  op_ret   = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf      = data_to_str (buf_data);
  
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
static int32_t 
client_mkdir_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *buf_data = dict_get (args, "STAT");
  char *stat_str = NULL;
  struct stat *stbuf = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  client_local_t *local = frame->local;
  inode_t *inode = local->inode;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, inode, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);
  inode = local->inode;

  if (op_ret >= 0) {
    stat_str = data_to_str (buf_data);
    stbuf = str_to_stat (stat_str);
    
    dict_set (inode->ctx, (frame->this)->name, data_from_uint64 (stbuf->st_ino));
  }

  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);

  if (stbuf)
    free (stbuf);

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

static int32_t 
client_flush_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_close_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;

  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_opendir_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *fd_data = dict_get (args, "FD");
  client_proto_priv_t *priv = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  client_local_t *local = frame->local;
  fd_t *fd = local->fd;

  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, fd);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);


  if (op_ret >= 0) {
    /* handle fd */
    char *key = NULL;
    char *remote_fd_str = strdup (data_to_str (fd_data));

    dict_set (fd->ctx,
	      (frame->this)->name,
	      data_from_dynstr (remote_fd_str));
    
    asprintf (&key, "%p", fd);

    priv = CLIENT_PRIVATE(frame);
    pthread_mutex_lock (&priv->lock);
    dict_set (priv->saved_fds, key, str_to_data (""));
    pthread_mutex_unlock (&priv->lock);

    free (key);
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
static int32_t
client_closedir_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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

static int32_t 
client_rmdir_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_statfs_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  char *buf = NULL;
  struct statvfs *stbuf = NULL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);
  
  if (op_ret >= 0) {
    buf = data_to_str (buf_data);
    stbuf = calloc (1, sizeof (struct statvfs));
    
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
      
      stbuf->f_bsize = bsize;
      stbuf->f_frsize = frsize;
      stbuf->f_blocks = blocks;
      stbuf->f_bfree = bfree;
      stbuf->f_bavail = bavail;
      stbuf->f_files = files;
      stbuf->f_ffree = ffree;
      stbuf->f_favail = favail;
      stbuf->f_fsid = fsid;
      stbuf->f_flag = flag;
      stbuf->f_namemax = namemax;
      
    }
  }

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  if (stbuf)
    free (stbuf);

  return 0;
}

/*
 * client_fsyncdir_cbk - fsyncdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_fsyncdir_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_access_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
client_incver_cbk (call_frame_t *frame,
		   dict_t *args)
{
  int32_t op_ret, op_errno;

  op_ret = data_to_int32 (dict_get (args, "RET"));
  op_errno = data_to_int32 (dict_get (args, "ERRNO"));

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
static int32_t 
client_setxattr_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;

  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_getxattr_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *buf_data = dict_get (args, "DICT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  dict_t *dict = NULL;

  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  

  dict = get_new_dict ();

  if (op_ret >= 0 && buf_data) {
    /* Unserialize the dictionary recieved */
    char *buf = memdup (buf_data->data, buf_data->len);
    dict_unserialize (buf, buf_data->len, &dict);
    dict->extra_free = buf;
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
static int32_t 
client_removexattr_cbk (call_frame_t *frame,
			dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_lk_cbk (call_frame_t *frame,
	       dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *type_data = dict_get (args, "TYPE");
  data_t *whence_data = dict_get (args, "WHENCE");
  data_t *start_data = dict_get (args, "START");
  data_t *len_data = dict_get (args, "LEN");
  data_t *pid_data = dict_get (args, "PID");
  struct flock lock = {0,};
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || 
      !err_data ||
      !type_data ||
      !whence_data ||
      !start_data ||
      !len_data ||
      !pid_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);
  
  if (op_ret >= 0) {
    lock.l_type =  data_to_int16 (type_data);
    lock.l_whence =  data_to_int16 (whence_data);
    lock.l_start =  data_to_int64 (start_data);
    lock.l_len =  data_to_int64 (len_data);
    lock.l_pid =  data_to_uint32 (pid_data);
  }

  STACK_UNWIND (frame, op_ret, op_errno, &lock);
  return 0;
}

/**
 * client_writedir_cbk -
 *
 * @frame:
 * @args:
 *
 * not for external reference
 */
static int32_t 
client_writedir_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_lock_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_unlock_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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

static int32_t 
client_listlocks_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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

static int32_t 
client_fsck_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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

static int32_t 
client_stats_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *buf_data = dict_get (args, "BUF");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  char *buf = NULL;
  struct xlator_stats stats = {0,};

  if (!ret_data || !err_data || !buf_data) {
    struct xlator_stats stats = {0,};
    STACK_UNWIND (frame, -1, ENOTCONN, &stats);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  if (op_ret >= 0) {
    buf = data_to_bin (buf_data);
    
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
 * client_lookup_cbk - lookup callback for client protocol
 *
 * @frame: call frame
 * @args: arguments dictionary
 * 
 * not for external reference
 */
static int32_t
client_lookup_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *stat_data = NULL;
  char *stat_buf = NULL;
  struct stat *stbuf = NULL;
  client_local_t *local = frame->local;
  inode_t *inode = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  dict_t *xattr = get_new_dict();
  data_t *xattr_data;
  inode = local->inode;

  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, inode, stbuf);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);

  if (op_ret >= 0) {
    stat_data = dict_get (args, "STAT");
    stat_buf = data_to_str (stat_data);
    stbuf = str_to_stat (stat_buf);
    dict_set (inode->ctx, (frame->this)->name, data_from_uint64 (stbuf->st_ino));
    xattr_data = dict_get (args, "DICT");
    if (xattr_data) {
      char *buf = memdup (xattr_data->data, xattr_data->len);
      dict_unserialize (buf, xattr_data->len, &xattr);
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

/*
 * client_forget_cbk - forget callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 * 
 * not for external reference
 */
static int32_t
client_forget_cbk (call_frame_t *frame,
		   dict_t *args)
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
static int32_t
client_getspec (call_frame_t *frame,
		xlator_t *this,
		int32_t flag)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;

  dict_set (request, "foo", str_to_data ("bar"));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_GETSPEC,
			      request);

  dict_destroy (request);

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

static int32_t 
client_getspec_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  data_t *spec_data = NULL;

  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  if (op_ret >= 0) {
    spec_data = dict_get (args, "spec-file-data");
  }

  STACK_UNWIND (frame, op_ret, op_errno, (spec_data?spec_data->data:""));
  return 0;
}

/*
 * client_setspec_cbk - setspec callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

static int32_t 
client_setspec_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_setvolume_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_getvolume_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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

      gf_log (trans->xl->name, GF_LOG_DEBUG,
	      "attempting reconnect");
      transport_connect (trans);

      priv->reconnect = gf_timer_call_after (trans->xl->ctx, tv,
					     client_protocol_reconnect, trans);
    } else {
      gf_log (trans->xl->name, GF_LOG_DEBUG,
	      "breaking reconnect chain");
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

  gf_log (trans->xl->name,
	  GF_LOG_WARNING,
	  "cleaning up state in transport object %p",
	  trans);

  pthread_mutex_lock (&priv->lock);
  {
    saved_frames = priv->saved_frames;
    priv->saved_frames = get_new_dict_full (1024);
    
    {
      data_pair_t *trav = (priv->saved_fds)->members_list;
      xlator_t *this = trans->xl;
      
      while (trav) {
	fd_t *tmp = (fd_t *)(long) strtoul (trav->key, NULL, 0);
	if (tmp->ctx)
	  dict_del (tmp->ctx, this->name);
	trav = trav->next;
      }
      
      dict_destroy (priv->saved_fds);
      priv->saved_fds = get_new_dict (64);
    }

    /* bailout logic cleanup */
    memset (&(priv->last_sent), 0, sizeof (priv->last_sent));
    memset (&(priv->last_recieved), 0, sizeof (priv->last_recieved));

    if (!priv->timer) {
      gf_log (trans->xl->name,
	      GF_LOG_DEBUG,
	      "priv->timer is NULL!!!!");
    }
    else {
      gf_timer_call_cancel (trans->xl->ctx, priv->timer);
      priv->timer = NULL;
    }

    if (!priv->reconnect) {
    }
  }
  pthread_mutex_unlock (&priv->lock);

  {
    data_pair_t *trav = saved_frames->members_list;
    dict_t *reply = dict_ref (get_new_dict ());
    reply->is_locked = 1;
    while (trav && trav->next)
      trav = trav->next;
    while (trav) {
      /* TODO: reply functions are different for different fops. */
      call_frame_t *tmp = (call_frame_t *) (trav->value->data);

      gf_log (trans->xl->name, GF_LOG_WARNING,
	      "forced unwinding frame type(%d) op(%d) reply=@%p", tmp->type, tmp->op, reply);
      tmp->root->rsp_refs = dict_ref (reply);
      if (tmp->type == GF_OP_TYPE_FOP_REQUEST)
	gf_fops[tmp->op] (tmp, reply);
      else
	gf_mops[tmp->op] (tmp, reply);
      dict_unref (reply);
      trav = trav->prev;
    }
    dict_unref (reply);

    dict_destroy (saved_frames);
  }

  return 0;
}

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
  client_readdir_cbk,
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
  client_writedir_cbk,
  client_rmelem_cbk,
  client_incver_cbk,
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
  client_fsck_cbk
};

/*
 * client_protocol_interpret - protocol interpreter
 * @trans: transport object
 * @blk: data block
 *
 */
static int32_t
client_protocol_interpret (transport_t *trans,
			   gf_block_t *blk)
{
  int32_t ret = 0;
  dict_t *args = blk->dict;
  dict_t *refs = NULL;
  call_frame_t *frame = NULL;

  frame = lookup_frame (trans, blk->callid);
  if (!frame) {
    gf_log (trans->xl->name, GF_LOG_DEBUG,
	    "frame not found for blk with callid: %d",
	    blk->callid);
    return -1;
  }
  frame->root->rsp_refs = refs = dict_ref (get_new_dict ());
  dict_set (refs, NULL, trans->buf);
  refs->is_locked = 1;

  switch (blk->type) {
  case GF_OP_TYPE_FOP_REPLY:
    {
      if (blk->op > GF_FOP_MAXVALUE || blk->op < 0) {
	gf_log (trans->xl->name,
		GF_LOG_DEBUG,
		"invalid opcode '%d'",
		blk->op);
	ret = -1;
	break;
      }
      
      gf_fops[blk->op] (frame, args);
      
      break;
    }
  case GF_OP_TYPE_MOP_REPLY:
    {
      if (blk->op > GF_MOP_MAXVALUE || blk->op < 0)
	return -1;
      
      gf_mops[blk->op] (frame, args);
     
      break;
    }
  default:
    gf_log (trans->xl->name, GF_LOG_DEBUG,
	    "invalid packet type: %d", blk->type);
    ret = -1;
  }

  dict_destroy (args);
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
  size_t lru_limit = 1000;
  data_t *timeout = NULL;
  int32_t transport_timeout = 0;
  data_t *lru_data = NULL;

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
  
  lru_data = dict_get (this->options, "inode-lru-limit");
  if (!lru_data){
    gf_log (this->name, GF_LOG_DEBUG,
	    "missing 'inode-lru-limit'. defaulting to 1000");
    dict_set (this->options,
	      "inode-lru-limit",
	      data_from_uint64 (lru_limit));
  } else {
    lru_limit = data_to_uint64 (lru_data);
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

  trans = transport_load (this->options, 
			  this,
			  this->notify);

  if (!trans)
    return -1;

  this->private = transport_ref (trans);
  priv = calloc (1, sizeof (client_proto_priv_t));
  priv->saved_frames = get_new_dict_full (1024);
  priv->saved_fds = get_new_dict_full (64);
  priv->callid = 1;
  memset (&(priv->last_sent), 0, sizeof (priv->last_sent));
  memset (&(priv->last_recieved), 0, sizeof (priv->last_recieved));
  priv->transport_timeout = transport_timeout;
  pthread_mutex_init (&priv->lock, NULL);
  trans->xl_private = priv;

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
  free (priv);
  return;
}


static int32_t
client_protocol_handshake_reply (transport_t *trans,
				 gf_block_t *blk)
{
  client_proto_priv_t *priv = trans->xl_private;
  dict_t *reply = NULL;
  char *remote_error;
  int32_t remote_errno;
  int32_t ret = -1;

  do {
    if (!blk) {
      gf_log (trans->xl->name, GF_LOG_ERROR,
	      "gf_block_unserialize failed during handshake");
      break;
    }

    gf_log (trans->xl->name, GF_LOG_DEBUG,
	    "reply frame has callid: %lld", blk->callid);

    reply = blk->dict;

    if (reply == NULL) {
      gf_log (trans->xl->name, GF_LOG_ERROR,
	      "dict_unserialize failed");
      ret = -1;
      break;
    }
  } while (0);

  if (dict_get (reply, "RET"))
    ret = data_to_int32 (dict_get (reply, "RET"));
  else
    ret = -2;

  if (dict_get (reply, "ERRNO"))
    remote_errno = data_to_int32 (dict_get (reply, "ERRNO"));
  else
    remote_errno = ENOENT;

  if (dict_get (reply, "ERROR"))
    remote_error = data_to_str (dict_get (reply, "ERROR"));
  else
    remote_error = "Unknown Error";
  
  if (ret < 0) {
    gf_log (trans->xl->name, GF_LOG_ERROR,
	    "SETVOLUME on remote-host failed: ret=%d error=%s",
	    ret,  remote_error);
    errno = remote_errno;
  } else {
    gf_log (trans->xl->name, GF_LOG_DEBUG,
	    "SETVOLUME on remote-host succeeded");
  }

  if (reply)
    dict_destroy (reply);

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
  dict_t *request;
  dict_t *options;
  char *remote_subvolume = NULL;

  request = get_new_dict ();

  priv = trans->xl_private;
  options = this->options;

  remote_subvolume = data_to_str (dict_get (options,
                                            "remote-subvolume"));
  
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

  dict_set (request,
            "remote-subvolume",
            data_from_dynstr (strdup (remote_subvolume)));

  {
    struct iovec *vector;
    int32_t i;
    int32_t count;

    gf_block_t *blk = gf_block_new (424242); /* "random" number */
    blk->type = GF_OP_TYPE_MOP_REQUEST;
    blk->op = GF_MOP_SETVOLUME;
    blk->size = 0;
    blk->data = 0;
    blk->dict = request;

    count = gf_block_iovec_len (blk);
    vector = alloca (count * (sizeof (*vector)));
    memset (vector, 0, count * (sizeof (*vector)));

    gf_block_to_iovec (blk, vector, count);
    for (i=0; i<count; i++)
      if (!vector[i].iov_base)
        vector[i].iov_base = alloca (vector[i].iov_len);
    gf_block_to_iovec (blk, vector, count);

    ret = trans->ops->writev (trans, vector, count);

    free (blk);
  }
  dict_destroy (request);

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
	gf_block_t *blk;
	int32_t connected = 0;

	blk = gf_block_unserialize_transport (trans);
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
	    free (blk);
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
  .lookup      = client_lookup,
  .forget      = client_forget,
  .fchmod      = client_fchmod,
  .fchown      = client_fchown,
  .writedir    = client_writedir,
};

struct xlator_mops mops = {
  .stats     = client_stats,
  .lock      = client_lock,
  .unlock    = client_unlock,
  .listlocks = client_listlocks,
  .getspec   = client_getspec
};
