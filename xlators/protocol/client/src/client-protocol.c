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
#include "client-protocol.h"
#include "dict.h"
#include "protocol.h"
#include "transport.h"
#include "xlator.h"
#include "logging.h"
#include "layout.h"
#include "timer.h"

#include <inttypes.h>

#if __WORDSIZE == 64
# define F_L64 "%l"
#else
# define F_L64 "%ll"
#endif

static int32_t client_protocol_notify (xlator_t *this, transport_t *trans, int32_t event);
static int32_t client_protocol_interpret (transport_t *trans, gf_block_t *blk);
static int32_t client_protocol_cleanup (transport_t *trans);

static call_frame_t *
lookup_frame (transport_t *trans, int64_t callid)
{
  client_proto_priv_t *priv = trans->xl_private;
  char buf[64];
  snprintf (buf, 64, "%"PRId64, callid);

  pthread_mutex_lock (&priv->lock);
  call_frame_t *frame = data_to_bin (dict_get (priv->saved_frames, buf));
  dict_del (priv->saved_frames, buf);
  pthread_mutex_unlock (&priv->lock);
  return frame;
}

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
  stbuf->st_atim.tv_nsec = atime_nsec;
  stbuf->st_mtime = mtime;
  stbuf->st_mtim.tv_nsec = mtime_nsec;
  stbuf->st_ctime = ctime;
  stbuf->st_ctim.tv_nsec = ctime_nsec;

  return stbuf;
}

static int32_t
client_protocol_xfer (call_frame_t *frame,
		      xlator_t *this,
		      glusterfs_op_type_t type,
		      int32_t op,
		      dict_t *request)
{
  int32_t ret;
  transport_t *trans;
  client_proto_priv_t *proto_priv;

  if (!this) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "'this' is NULL");
    return -1;
  }

  if (!request) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "request is NULL");
    return -1;
  }

  trans = this->private;
  if (!trans) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "this->private is NULL");
    return -1;
  }
  

  proto_priv = trans->xl_private;
  if (!proto_priv) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "trans->xl_private is NULL");
    return -1;
  }

  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));
  dict_set (request, "CALLER_PID", int_to_data (frame->root->pid));

  {
    int64_t callid;
    gf_block_t *blk;
    struct iovec *vector = NULL;
    int32_t count = 0;
    int32_t i;
    char buf[64];

    pthread_mutex_lock (&proto_priv->lock);
    callid = proto_priv->callid++;
    snprintf (buf, 64, "%"PRId64, callid);
    dict_set (proto_priv->saved_frames,
	      buf,
	      bin_to_data (frame, sizeof (frame)));
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

    ret = trans->ops->writev (trans, vector, count);

    //  transport_flush (trans);

    free (blk);

    if (ret != 0) {
      gf_log ("protocol/client: client_protocol_xfer: ",
	      GF_LOG_ERROR,
	      "transport_submit failed");
      client_protocol_cleanup (trans);
      transport_disconnect (trans);
      return -1;
    }
  }
  return ret;
}

static void
call_bail (void *trans)
{
  gf_log ("client/protocol",
	  GF_LOG_CRITICAL,
	  "bailing transport");
  transport_bail (trans);
}

#define BAIL(frame, sec) do {                                \
  struct timeval tv = {                                      \
    .tv_sec = sec,                                           \
    .tv_usec = 0                                             \
  };                                                         \
  frame->local = gf_timer_call_after (frame->this->ctx,      \
                                      tv,                    \
                                      call_bail,             \
                                      frame->this->private); \
} while (0)

static int32_t 
client_create (call_frame_t *frame,
	       xlator_t *this,
	       const char *path,
	       mode_t mode)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "MODE", int_to_data (mode));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CREATE,
			      request);
  dict_destroy (request);
  return ret;
}


static int32_t 
client_open (call_frame_t *frame,
	     xlator_t *this,
	     const char *path,
	     int32_t flags,
	     mode_t mode)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "FLAGS", int_to_data (flags));
  dict_set (request, "MODE", int_to_data (mode));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_OPEN,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_getattr (call_frame_t *frame,
		xlator_t *this,
		const char *path)
{
  dict_t *request = get_new_dict ();
  struct stat buf = {0, };
  int32_t ret;
  
  dict_set (request, "PATH", str_to_data ((char *)path));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_GETATTR,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_readlink (call_frame_t *frame,
		 xlator_t *this,
		 const char *path,
		 size_t size)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "LEN", int_to_data (size));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_READLINK,
			      request);

  dict_destroy (request);
  return ret;
}

static int32_t 
client_mknod (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      mode_t mode,
	      dev_t dev)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "MODE", int_to_data (mode));
  dict_set (request, "DEV", int_to_data (dev));
  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_MKNOD,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_mkdir (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      mode_t mode)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "MODE", int_to_data (mode));
  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_MKDIR,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_unlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *path)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_UNLINK,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_rmdir (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_RMDIR,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_symlink (call_frame_t *frame,
		xlator_t *this,
		const char *oldpath,
		const char *newpath)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)oldpath));
  dict_set (request, "BUF", str_to_data ((char *)newpath));
  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_SYMLINK,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_rename (call_frame_t *frame,
	       xlator_t *this,
	       const char *oldpath,
	       const char *newpath)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)oldpath));
  dict_set (request, "BUF", str_to_data ((char *)newpath));
  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_RENAME,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_link (call_frame_t *frame,
	     xlator_t *this,
	     const char *oldpath,
	     const char *newpath)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)oldpath));
  dict_set (request, "BUF", str_to_data ((char *)newpath));
  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LINK,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_chmod (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      mode_t mode)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "MODE", int_to_data (mode));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CHMOD,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_chown (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      uid_t uid,
	      gid_t gid)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));
  dict_set (request, "UID", int_to_data (uid));
  dict_set (request, "GID", int_to_data (gid));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CHOWN,
			      request);

  dict_destroy (request);
  return ret;
}

static int32_t 
client_truncate (call_frame_t *frame,
		 xlator_t *this,
		 const char *path,
		 off_t offset)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "OFFSET", int_to_data (offset));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_TRUNCATE,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_utimes (call_frame_t *frame,
	       xlator_t *this,
	       const char *path,
	       struct timespec *tvp)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "ACTIME_SEC", int_to_data (tvp[0].tv_sec));
  dict_set (request, "ACTIME_NSEC", int_to_data (tvp[0].tv_nsec));
  dict_set (request, "MODTIME_SEC", int_to_data (tvp[1].tv_sec));
  dict_set (request, "MODTIME_NSEC", int_to_data (tvp[1].tv_nsec));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_UTIMES,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_readv (call_frame_t *frame,
	      xlator_t *this,
	      dict_t *ctx,
	      size_t size,
	      off_t offset)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (ctx, this->name);
  int32_t ret;

  if (!ctx_data) {
    struct iovec vec;
    vec.iov_base = "";
    vec.iov_len = 0;
    dict_destroy (request);
    STACK_UNWIND (frame, -1, EBADFD, &vec);
    return 0;
  }

  dict_set (request, "FD", str_to_data (data_to_str (ctx_data)));
  dict_set (request, "OFFSET", int_to_data (offset));
  dict_set (request, "LEN", int_to_data (size));

  BAIL (frame, 60);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_READ,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_writev (call_frame_t *frame,
	       xlator_t *this,
	       dict_t *ctx,
	       struct iovec *vector,
	       int32_t count,
	       off_t offset)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (ctx, this->name);
  size_t size = 0, i;
  int32_t ret;

  if (!ctx_data) {
    dict_destroy (request);
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }
 
  for (i = 0; i<count; i++)
    size += vector[i].iov_len;

  dict_set (request, "FD", str_to_data (data_to_str (ctx_data)));
  dict_set (request, "OFFSET", int_to_data (offset));
  dict_set (request, "BUF", data_from_iovec (vector, count));
  dict_set (request, "LEN", int_to_data (size));
 
  BAIL (frame, 60);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_WRITE,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_statfs (call_frame_t *frame,
	       xlator_t *this,
	       const char *path)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_STATFS,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_flush (call_frame_t *frame,
	      xlator_t *this,
	      dict_t *ctx)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (ctx, this->name);
  int32_t ret;

  if (!ctx_data) {
    dict_destroy (request);
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  dict_set (request, "FD", str_to_data (data_to_str (ctx_data)));

  BAIL (frame, 60);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FLUSH,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_release (call_frame_t *frame,
		xlator_t *this,
		dict_t *ctx)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (ctx, this->name);
  transport_t *trans;
  client_proto_priv_t *priv;
  int32_t ret;
  char *key;

  if (!ctx_data) {
    STACK_UNWIND (frame, -1, EBADFD);
    dict_destroy (ctx);
    return 0;
  }

  dict_set (request, "FD", str_to_data (data_to_str (ctx_data)));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_RELEASE,
			      request);

  trans = frame->this->private;
  priv = trans->xl_private;
  
  asprintf (&key, "%p", ctx);

  pthread_mutex_lock (&priv->lock);
  dict_del (priv->saved_fds, key); 
  pthread_mutex_unlock (&priv->lock);

  free (key);
  free (data_to_str (ctx_data));
  dict_destroy (ctx);
  dict_destroy (request);

  return ret;
}


static int32_t 
client_fsync (call_frame_t *frame,
	      xlator_t *this,
	      dict_t *ctx,
	      int32_t flags)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (ctx, this->name);
  int32_t ret;

  if (!ctx_data) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  dict_set (request, "FLAGS", int_to_data (flags));
  dict_set (request, "FD", str_to_data (data_to_str (ctx_data)));

  BAIL (frame, 60);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FSYNC,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_setxattr (call_frame_t *frame,
		 xlator_t *this,
		 const char *path,
		 const char *name,
		 const char *value,
		 size_t size,
		 int32_t flags)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "FLAGS", int_to_data (flags));
  dict_set (request, "COUNT", int_to_data (size));
  dict_set (request, "BUF", str_to_data ((char *)name));
  dict_set (request, "FD", str_to_data ((char *)value));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_SETXATTR,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_getxattr (call_frame_t *frame,
		 xlator_t *this,
		 const char *path,
		 const char *name,
		 size_t size)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "BUF", str_to_data ((char *)name));
  dict_set (request, "COUNT", int_to_data (size));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_GETXATTR,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_listxattr (call_frame_t *frame,
		  xlator_t *this,
		  const char *path,
		  size_t size)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "COUNT", int_to_data (size));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LISTXATTR,
			      request);

  dict_destroy (request);
  return ret;
}

		     
static int32_t 
client_removexattr (call_frame_t *frame,
		    xlator_t *this,
		    const char *path,
		    const char *name)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "BUF", str_to_data ((char *)name));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_REMOVEXATTR,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_opendir (call_frame_t *frame,
		xlator_t *this,
		const char *path)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  //  dict_set (request, "FD", int_to_data ((long)tmp->context));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_OPENDIR,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_readdir (call_frame_t *frame,
		xlator_t *this,
		const char *path)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_READDIR,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_releasedir (call_frame_t *frame,
		   xlator_t *this,
		   dict_t *ctx)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (ctx, this->name);
  transport_t *trans;
  client_proto_priv_t *priv;
  int32_t ret;
  char *key;


  if (!ctx_data) {
    STACK_UNWIND (frame, -1, EBADFD);
    dict_destroy (ctx);
    return 0;
  }

  dict_set (request, "FD", str_to_data (data_to_str (ctx_data)));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_RELEASEDIR, request);

  trans = frame->this->private;
  priv = trans->xl_private;
  
  asprintf (&key, "%p", ctx);

  pthread_mutex_lock (&priv->lock);
  dict_del (priv->saved_fds, key); 
  pthread_mutex_unlock (&priv->lock);

  free (key);
  free (data_to_str (ctx_data));
  dict_destroy (ctx);
  dict_destroy (request);

  return ret;
}


static int32_t 
client_fsyncdir (call_frame_t *frame,
		 xlator_t *this,
		 dict_t *ctx,
		 int32_t flags)
{
  int32_t ret = -1;
  data_t *ctx_data = dict_get (ctx, this->name);

  if (!ctx_data) {
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
  
  STACK_UNWIND (frame, -1, ENOSYS);
  return ret;
}


static int32_t 
client_access (call_frame_t *frame,
	       xlator_t *this,
	       const char *path,
	       mode_t mode)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "MODE", int_to_data (mode));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_ACCESS,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_ftruncate (call_frame_t *frame,
		  xlator_t *this,
		  dict_t *ctx,
		  off_t offset)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (ctx, this->name);
  int32_t ret;

  if (!ctx_data) {
    dict_destroy (request);
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }

  dict_set (request, "FD", str_to_data (data_to_str (ctx_data)));
  dict_set (request, "OFFSET", int_to_data (offset));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FTRUNCATE,
			      request);

  dict_destroy (request);
  return ret;
}


static int32_t 
client_fgetattr (call_frame_t *frame,
		 xlator_t *this,
		 dict_t *ctx)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (ctx, this->name);
  int32_t ret;

  if (!ctx_data) {
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }

  dict_set (request, "FD", str_to_data (data_to_str (ctx_data)));

  BAIL (frame, 30);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FGETATTR,
			      request);

  dict_destroy (request);
  return ret;
}

static int32_t 
client_lk (call_frame_t *frame,
	   xlator_t *this,
	   dict_t *ctx,
	   int32_t cmd,
	   struct flock *lock)
{
  dict_t *request = get_new_dict ();
  data_t *ctx_data = dict_get (ctx, this->name);
  int32_t ret;

  if (!ctx_data) {
    dict_destroy (request);
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }

  dict_set (request, "FD", str_to_data (data_to_str (ctx_data)));
  dict_set (request, "CMD", int_to_data (cmd));
  dict_set (request, "TYPE", int_to_data (lock->l_type));
  dict_set (request, "WHENCE", int_to_data (lock->l_whence));
  dict_set (request, "START", int_to_data (lock->l_start));
  dict_set (request, "LEN", int_to_data (lock->l_len));
  dict_set (request, "PID", int_to_data (lock->l_pid));
  dict_set (request, "CLIENT_PID", int_to_data (getpid ()));

  BAIL (frame, 60);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LK,
			      request);

  dict_destroy (request);
  return ret;
}


/*
 * MGMT_OPS
 */

static int32_t 
client_stats (call_frame_t *frame,
	      xlator_t *this, 
	      int32_t flags)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  dict_set (request, "FLAGS", int_to_data (0)); // without this dummy key the server crashes
  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_STATS,
			      request);

  dict_destroy (request);
  if (ret == -1)
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);

  return 0;
}

//TODO: make it static (currently !static because of the warning)
int32_t 
client_fsck (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)
{
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}


static int32_t 
client_lock (call_frame_t *frame,
	     xlator_t *this,
	     const char *name)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((char *)name));

  int32_t ret = client_protocol_xfer (frame,
				      this,
				      GF_OP_TYPE_MOP_REQUEST,
				      GF_MOP_LOCK,
				      request);

  dict_destroy (request);
  if (ret == -1)
    STACK_UNWIND (frame, -1, ENOTCONN);

  return 0;
}


static int32_t 
client_unlock (call_frame_t *frame,
	       xlator_t *this,
	       const char *name)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((char *)name));

  int32_t ret = client_protocol_xfer (frame,
				      this,
				      GF_OP_TYPE_MOP_REQUEST,
				      GF_MOP_UNLOCK, request);

  dict_destroy (request);
  if (ret == -1)
    STACK_UNWIND (frame, -1, ENOTCONN);

  return 0;
}


static int32_t 
client_listlocks (call_frame_t *frame,
		  xlator_t *this,
		  const char *pattern)
{
  dict_t *request = get_new_dict ();
  
  dict_set (request, "OP", int_to_data (0xcafebabe));
  int32_t ret = client_protocol_xfer (frame,
				      this,
				      GF_OP_TYPE_MOP_REQUEST,
				      GF_MOP_LISTLOCKS,
				      request);

  dict_destroy (request);
  if (ret == -1)
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);

  return 0;
}


static int32_t 
client_nslookup (call_frame_t *frame,
		 xlator_t *this,
		 const char *path)
{
  return -1;

  dict_t *request = get_new_dict ();

  //  dict_set (request, "PATH", str_to_data ((char *)path));

  int32_t ret = client_protocol_xfer (frame,
				      this,
				      GF_OP_TYPE_MOP_REQUEST,
				      GF_MOP_NSLOOKUP,
				      request);

  dict_destroy (request);
  if (ret == -1)
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);

  return 0;
}


static int32_t 
client_nsupdate (call_frame_t *frame,
		 xlator_t *this,
		 const char *name,
		 dict_t *ns)
{
  return -1;

  dict_t *request = get_new_dict ();
  char *ns_str = calloc (1, dict_serialized_length (ns));
  dict_serialize (ns, ns_str);
  //  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "NS", str_to_data (ns_str));

  int32_t ret = client_protocol_xfer (frame,
				      this,
				      GF_OP_TYPE_FOP_REQUEST,
				      GF_MOP_NSUPDATE,
				      request);

  dict_destroy (request);
  free (ns_str);
  if (ret == -1)
    STACK_UNWIND (frame, -1, ENOTCONN);

  return 0;
}


/* Callbacks */
static int32_t 
client_create_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *fd_data = dict_get (args, "FD");
  transport_t *trans;
  client_proto_priv_t *priv;
  
  if (!buf_data || !ret_data || !err_data || !fd_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);
  
  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  dict_t *file_ctx = NULL;

  if (op_ret >= 0) {
    /* handle fd */
    char *remote_fd = strdup (data_to_str (fd_data));
    file_ctx = get_new_dict ();

    dict_set (file_ctx,
	      (frame->this)->name,
	      str_to_data(remote_fd));

    trans = frame->this->private;
    priv = trans->xl_private;

    char *key;
    asprintf (&key, "%p", file_ctx);

    pthread_mutex_lock (&priv->lock);
    dict_set (priv->saved_fds, key, str_to_data ("")); 
    pthread_mutex_unlock (&priv->lock);

    free (key);
  }

  STACK_UNWIND (frame, op_ret, op_errno, file_ctx, stbuf);
  free (stbuf);
  return 0;
}

static int32_t 
client_open_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *fd_data = dict_get (args, "FD");
  transport_t *trans;
  client_proto_priv_t *priv;
  
  if (!buf_data || !ret_data || !err_data || !fd_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);
  
  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  dict_t *file_ctx = NULL;

  if (op_ret >= 0) {
    /* handle fd */
    char *remote_fd = strdup (data_to_str (fd_data));

    file_ctx = get_new_dict ();
    dict_set (file_ctx,
	      (frame->this)->name,
	      str_to_data(remote_fd));

    trans = frame->this->private;
    priv = trans->xl_private;

    char *key;
    asprintf (&key, "%p", file_ctx);

    pthread_mutex_lock (&priv->lock);
    dict_set (priv->saved_fds, key, str_to_data (""));
    pthread_mutex_unlock (&priv->lock);

    free (key);
  }

  STACK_UNWIND (frame, op_ret, op_errno, file_ctx, stbuf);
  free (stbuf);
  return 0;
}

static int32_t 
client_getattr_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

//utimes 
static int32_t 
client_utimes_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

//chmod
static int32_t 
client_chmod_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

// chown
static int32_t 
client_chown_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

// mknod
static int32_t 
client_mknod_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

// symlink
static int32_t 
client_symlink_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

// link
static int32_t 
client_link_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

// truncate
static int32_t 
client_truncate_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

// fgetattr
static int32_t 
client_fgetattr_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

// ftruncate 
static int32_t 
client_ftruncate_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

//read
static int32_t 
client_readv_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_bin (buf_data);
  struct iovec vec;
  
  vec.iov_base = buf;
  vec.iov_len = op_ret;
  STACK_UNWIND (frame, op_ret, op_errno, &vec, 1);

  return 0;
}

//write
static int32_t 
client_write_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//readdir
static int32_t 
client_readdir_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *cnt_data = dict_get (args, "NR_ENTRIES");

  if (!buf_data || !ret_data || !err_data || !cnt_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL, 0);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  int32_t nr_count = (int32_t)data_to_int (cnt_data);
  char *buf = data_to_str (buf_data);
  
  dir_entry_t *entry = calloc (1, sizeof (dir_entry_t));
  dir_entry_t *trav, *prev = entry;
  int32_t count, i, bread;
  char *ender, *buffer_ptr = buf;
  char tmp_buf[512];
  
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
      trav->buf.st_atim.tv_nsec = atime_nsec;
      trav->buf.st_mtime = mtime;
      trav->buf.st_mtim.tv_nsec = mtime_nsec;
      trav->buf.st_ctime = ctime;
      trav->buf.st_ctim.tv_nsec = ctime_nsec;
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

//fsync
static int32_t 
client_fsync_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//unlink
static int32_t 
client_unlink_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//rename
static int32_t 
client_rename_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//readlink
static int32_t 
client_readlink_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

//mkdir
static int32_t 
client_mkdir_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *buf_data = dict_get (args, "BUF");
  struct stat *buf;
  
  if (!ret_data || !err_data || !buf_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  buf = str_to_stat (data_to_str (buf_data));
  
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  free (buf);
  return 0;
}

//flush
static int32_t 
client_flush_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//release
static int32_t 
client_release_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//opendir
static int32_t 
client_opendir_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *fd_data = dict_get (args, "FD");
  transport_t *trans;
  client_proto_priv_t *priv;
  
  if (!ret_data || !err_data || !fd_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);
  
  dict_t *file_ctx = NULL;

  if (op_ret >= 0) {
    /* handle fd */
    char *remote_fd = strdup (data_to_str (fd_data));

    file_ctx = get_new_dict ();
    dict_set (file_ctx,
	      (frame->this)->name,
	      str_to_data(remote_fd));

    trans = frame->this->private;
    priv = trans->xl_private;

    char *key;
    asprintf (&key, "%p", file_ctx);

    pthread_mutex_lock (&priv->lock);
    dict_set (priv->saved_fds, key, str_to_data (""));
    pthread_mutex_unlock (&priv->lock);

    free (key);
  }

  STACK_UNWIND (frame, op_ret, op_errno, file_ctx);
  return 0;
}

//releasedir
static int32_t 
client_releasedir_cbk (call_frame_t *frame,
		       dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//rmdir
static int32_t 
client_rmdir_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//statfs
static int32_t 
client_statfs_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  struct statvfs *stbuf = calloc (1, sizeof (struct statvfs));
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
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  free (stbuf);
  return 0;
}

//fsyncdir
static int32_t 
client_fsyncdir_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//access
static int32_t 
client_access_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//setxattr
static int32_t 
client_setxattr_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//listxattr
static int32_t 
client_listxattr_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  
  STACK_UNWIND (frame, op_ret, op_errno, buf);

  return 0;
}

//getxattr
static int32_t 
client_getxattr_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_str (buf_data);
  
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

//removexattr
static int32_t 
client_removexattr_cbk (call_frame_t *frame,
			dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//lk
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
  struct flock lock;
  
  if (!ret_data || 
      !err_data ||
      !type_data ||
      !whence_data ||
      !start_data ||
      !len_data ||
      !pid_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);

  lock.l_type = (int16_t) data_to_int (type_data);
  lock.l_whence = (int16_t) data_to_int (whence_data);
  lock.l_start = (int64_t) data_to_int (start_data);
  lock.l_len = (int64_t) data_to_int (len_data);
  lock.l_pid = (int32_t) data_to_int (pid_data);

  STACK_UNWIND (frame, op_ret, op_errno, &lock);
  return 0;
}


//lock
static int32_t 
client_lock_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//unlock
static int32_t 
client_unlock_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//listlocks
static int32_t 
client_listlocks_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno, "");
  return 0;
}

//nslookup
static int32_t 
client_nslookup_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *ns_data = dict_get (args, "NS");

  if (!ret_data || !err_data || !ns_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *ns_str = data_to_bin (ns_data);
  dict_t *ns; // = get_new_dict ();
  if (ns_str && strlen (ns_str) > 0)
    dict_unserialize (ns_str, strlen (ns_str), &ns);
  
  STACK_UNWIND (frame, op_ret, op_errno, ns);
  return 0;
}

//nsupdate
static int32_t 
client_nsupdate_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//fsck
static int32_t 
client_fsck_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//stats
static int32_t 
client_stats_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *buf_data = dict_get (args, "BUF");

  if (!ret_data || !err_data || !buf_data) {
    struct xlator_stats stats = {0,};
    STACK_UNWIND (frame, -1, EINVAL, &stats);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  char *buf = data_to_bin (buf_data);

  struct xlator_stats stats;
  sscanf (buf, "%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64"\n",
	  &stats.nr_files,
	  &stats.disk_usage,
	  &stats.free_disk,
	  &stats.read_usage,
	  &stats.write_usage,
	  &stats.disk_speed,
	  &stats.nr_clients);
  
  STACK_UNWIND (frame, op_ret, op_errno, &stats);
  return 0;
}

//getspec
static int32_t 
client_getspec_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//setspec
static int32_t 
client_setspec_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//setvolume
static int32_t 
client_setvolume_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

//getvolume
static int32_t 
client_getvolume_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  int32_t op_ret = (int32_t)data_to_int (ret_data);
  int32_t op_errno = (int32_t)data_to_int (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
client_protocol_notify (xlator_t *this,
			transport_t *trans,
			int32_t event)
{
  int ret = 0;
  //  client_proto_priv_t *priv = trans->xl_private;

  if (event & (POLLIN|POLLPRI)) {
    gf_block_t *blk;

    blk = gf_block_unserialize_transport (trans);
    if (!blk) {
      ret = -1;
    }

    if (!ret) {
      ret = client_protocol_interpret (trans, blk);

      //      free (blk->data);
      free (blk);
    }
  }

  if (ret || (event & (POLLERR|POLLHUP))) {
    client_protocol_cleanup (trans);
  }

  return ret;
}

static int32_t 
client_protocol_cleanup (transport_t *trans)
{
  client_proto_priv_t *priv = trans->xl_private;
  glusterfs_ctx_t *ctx = trans->xl->ctx;

  gf_log ("protocol/client",
	  GF_LOG_DEBUG,
	  "cleaning up state in transport object %p",
	  trans);

  pthread_mutex_lock (&priv->lock);
  {
    data_pair_t *trav = (priv->saved_frames)->members_list;
    while (trav) {
      // TODO: reply functions are different for different fops.
      call_frame_t *tmp = (call_frame_t *) (trav->value->data);
      if (tmp->local) {
	gf_timer_call_cancel (ctx, tmp->local);
	tmp->local = NULL;
      }
      STACK_UNWIND (tmp, -1, ENOTCONN, 0, 0);
      trav = trav->next;
    }
    
    dict_destroy (priv->saved_frames);
    priv->saved_frames = get_new_dict ();
  }
  {
    data_pair_t *trav = (priv->saved_fds)->members_list;
    xlator_t *this = trans->xl;
    while (trav) {
      dict_t *tmp = (dict_t *)(long) strtoul (trav->key, NULL, 0);
      dict_del (tmp, this->name);
      trav = trav->next;
    }
    dict_destroy (priv->saved_fds);
    priv->saved_fds = get_new_dict ();
  }
  pthread_mutex_unlock (&priv->lock);
  return 0;
}

typedef int32_t (*gf_op_t) (call_frame_t *frame,
			    dict_t *args);

static gf_op_t gf_fops[] = {
  client_getattr_cbk,
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
  client_utimes_cbk,
  client_open_cbk,
  client_readv_cbk,
  client_write_cbk,
  client_statfs_cbk,
  client_flush_cbk,
  client_release_cbk,
  client_fsync_cbk,
  client_setxattr_cbk,
  client_getxattr_cbk,
  client_listxattr_cbk,
  client_removexattr_cbk,
  client_opendir_cbk,
  client_readdir_cbk,
  client_releasedir_cbk,
  client_fsyncdir_cbk,
  client_access_cbk,
  client_create_cbk,
  client_ftruncate_cbk,
  client_fgetattr_cbk,
  client_lk_cbk
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
  client_nslookup_cbk,
  client_nsupdate_cbk,
  client_fsck_cbk
};


static int32_t
client_protocol_interpret (transport_t *trans,
			   gf_block_t *blk)
{
  int32_t ret = 0;
  dict_t *args = blk->dict;
  call_frame_t *frame = NULL;

  frame = lookup_frame (trans, blk->callid);
  if (!frame) {
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "frame not found for blk with callid: %d",
	    blk->callid);
    return -1;
  }
  frame->root->rsp_refs = dict_ref (args);
  dict_set (args, NULL, trans->buf);

  if (frame->local) {
    gf_timer_call_cancel (trans->xl->ctx, frame->local);
    frame->local = NULL;
  }

  switch (blk->type) {
  case GF_OP_TYPE_FOP_REPLY:
    {
      if (blk->op > GF_FOP_MAXVALUE || blk->op < 0) {
	gf_log ("protocol/client",
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
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "invalid packet type: %d",
	    blk->type);
    ret = -1;
  }

  dict_unref (args);
  return 0;
}


int32_t 
init (xlator_t *this)
{
  transport_t *trans;
  client_proto_priv_t *priv;

  if (this->children) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "FATAL: client protocol translator cannot have subvolumes");
    return -1;
  }

  if (!dict_get (this->options, "transport-type")) {
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "missing 'option transport-type'. defaulting to \"tcp/client\"");
    dict_set (this->options,
	      "transport-type",
	      str_to_data ("tcp/client"));
  }

  if (!dict_get (this->options, "remote-subvolume")) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "missing 'option remote-subvolume'.");
    return -1;
  }

  trans = transport_load (this->options, 
			  this,
			  client_protocol_notify);
  if (!trans)
    return -1;

  this->private = trans;
  priv = calloc (1, sizeof (client_proto_priv_t));
  priv->saved_frames = get_new_dict ();
  priv->saved_fds = get_new_dict ();
  priv->callid = 1;
  pthread_mutex_init (&priv->lock, NULL);
  trans->xl_private = priv;

  return 0;
}

void
fini (xlator_t *this)
{
  return;
}

struct xlator_fops fops = {
  .getattr     = client_getattr,
  .readlink    = client_readlink,
  .mknod       = client_mknod,
  .mkdir       = client_mkdir,
  .unlink      = client_unlink,
  .rmdir       = client_rmdir,
  .symlink     = client_symlink,
  .rename      = client_rename,
  .link        = client_link,
  .chmod       = client_chmod,
  .chown       = client_chown,
  .truncate    = client_truncate,
  .utimes      = client_utimes,
  .open        = client_open,
  .readv       = client_readv,
  .writev      = client_writev,
  .statfs      = client_statfs,
  .flush       = client_flush,
  .release     = client_release,
  .fsync       = client_fsync,
  .setxattr    = client_setxattr,
  .getxattr    = client_getxattr,
  .listxattr   = client_listxattr,
  .removexattr = client_removexattr,
  .opendir     = client_opendir,
  .readdir     = client_readdir,
  .releasedir  = client_releasedir,
  .fsyncdir    = client_fsyncdir,
  .access      = client_access,
  .ftruncate   = client_ftruncate,
  .fgetattr    = client_fgetattr,
  .create      = client_create,
  .lk          = client_lk
};

struct xlator_mops mops = {
  .stats     = client_stats,
  .lock      = client_lock,
  .unlock    = client_unlock,
  .listlocks = client_listlocks,
  .nslookup  = client_nslookup,
  .nsupdate  = client_nsupdate
};
