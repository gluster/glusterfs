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
#include "client-protocol.h"
#include "dict.h"
#include "protocol.h"
#include "transport.h"
#include "xlator.h"
#include "logging.h"
#include "layout.h"

#include <inttypes.h>

#if __WORDSIZE == 64
# define F_L64 "%l"
#else
# define F_L64 "%ll"
#endif

static int32_t client_protocol_notify (xlator_t *this, transport_t *trans, int32_t event);
static int32_t client_protocol_interpret (transport_t *trans, gf_block_t *blk);
static int32_t client_protocol_cleanup (transport_t *trans);

call_frame_t *
lookup_frame (transport_t *trans, int64_t callid)
{
  client_proto_priv_t *priv = trans->xl_private;
  char buf[64];
  snprintf (buf, 64, "%"PRId64, callid);

  call_frame_t *frame = data_to_bin (dict_get (priv->saved_frames, buf));
  return frame;
}

static struct stat *
str_to_stat (int8_t *buf)
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
  if (!this) {
    gf_log ("protocol/client: client_protocol_xfer: ", GF_LOG_ERROR, "'this' is NULL");
    return -1;
  }

  if (!request) {
    gf_log ("protocol/client: client_protocol_xfer: ", GF_LOG_ERROR, "request is NULL");
    return -1;
  }

  transport_t *trans = this->private;
  if (!trans) {
    gf_log ("protocol/client: client_protocol_xfer: ", GF_LOG_ERROR, "this->private is NULL");
    return -1;
  }
  client_proto_priv_t *proto_priv = trans->xl_private;
  
  int32_t dict_len = dict_serialized_length (request);
  int8_t *dict_buf = malloc (dict_len);
  dict_serialize (request, dict_buf);

  int64_t callid = proto_priv->callid++;
  gf_block_t *blk = gf_block_new (callid);
  blk->type = type;
  blk->op = op;
  blk->size = dict_len;
  blk->data = dict_buf;

  int32_t blk_len = gf_block_serialized_length (blk);
  int8_t *blk_buf = malloc (blk_len);
  gf_block_serialize (blk, blk_buf);

  int ret = transport_submit (trans, blk_buf, blk_len);
  transport_flush (trans);

  free (blk_buf);
  free (dict_buf);
  free (blk);

  if (ret != blk_len) {
    gf_log ("protocol/client: client_protocol_xfer: ", GF_LOG_ERROR, "transport_submit failed");
    return -1;
  }

  char buf[64];
  snprintf (buf, 64, "%"PRId64, callid);
  dict_set (proto_priv->saved_frames, buf, bin_to_data (frame, sizeof (frame)));
  return 0;
}

int32_t
client_create (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path,
	       mode_t mode)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "MODE", int_to_data (mode));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_CREATE, request);
  
  dict_destroy (request);
  return 0;
}


int32_t 
client_open (call_frame_t *frame,
	     xlator_t *this,
	     const int8_t *path,
	     int32_t flags,
	     mode_t mode)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "FLAGS", int_to_data (flags));
  dict_set (request, "MODE", int_to_data (mode));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_OPEN, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_getattr (call_frame_t *frame,
		xlator_t *this,
		const int8_t *path)
{
  dict_t *request = get_new_dict ();
  
  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_GETATTR, request);
  dict_destroy (request);
  return 0;
}


int32_t 
client_readlink (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *path,
		 size_t size)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "LEN", int_to_data (size));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_READLINK, request);

  dict_destroy (request);
  return 0;
}

int32_t 
client_mknod (call_frame_t *frame,
	      xlator_t *this,
	      const int8_t *path,
	      mode_t mode,
	      dev_t dev)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "MODE", int_to_data (mode));
  dict_set (request, "DEV", int_to_data (dev));
  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_MKNOD, request);

  return 0;
}


int32_t 
client_mkdir (call_frame_t *frame,
	      xlator_t *this,
	      const int8_t *path,
	      mode_t mode)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "MODE", int_to_data (mode));
  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_MKDIR, request);

  return 0;
}


int32_t 
client_unlink (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_UNLINK, request);
  dict_destroy (request);
  return 0;
}


int32_t 
client_rmdir (call_frame_t *frame,
	      xlator_t *this,
	      const int8_t *path)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_RMDIR, request);
  return 0;
}


int32_t 
client_symlink (call_frame_t *frame,
		xlator_t *this,
		const int8_t *oldpath,
		const int8_t *newpath)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)oldpath));
  dict_set (request, "BUF", str_to_data ((int8_t *)newpath));
  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_SYMLINK, request);
  return 0;
}


int32_t 
client_rename (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *oldpath,
	       const int8_t *newpath)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)oldpath));
  dict_set (request, "BUF", str_to_data ((int8_t *)newpath));
  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_RENAME, request);
  return 0;
}


int32_t 
client_link (call_frame_t *frame,
	     xlator_t *this,
	     const int8_t *oldpath,
	     const int8_t *newpath)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)oldpath));
  dict_set (request, "BUF", str_to_data ((int8_t *)newpath));
  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_LINK, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_chmod (call_frame_t *frame,
	      xlator_t *this,
	      const int8_t *path,
	      mode_t mode)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "MODE", int_to_data (mode));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_CHMOD, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_chown (call_frame_t *frame,
	      xlator_t *this,
	      const int8_t *path,
	      uid_t uid,
	      gid_t gid)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "CALLER_UID", int_to_data (frame->root->uid));
  dict_set (request, "CALLER_GID", int_to_data (frame->root->gid));
  dict_set (request, "UID", int_to_data (uid));
  dict_set (request, "GID", int_to_data (gid));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_CHOWN, request);

  dict_destroy (request);
  return 0;
}

int32_t 
client_truncate (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *path,
		 off_t offset)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "OFFSET", int_to_data (offset));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_TRUNCATE, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_utime (call_frame_t *frame,
	      xlator_t *this,
	      const int8_t *path,
	      struct utimbuf *buf)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "ACTIME", int_to_data (buf->actime));
  dict_set (request, "MODTIME", int_to_data (buf->modtime));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_UTIME, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_read (call_frame_t *frame,
	     xlator_t *this,
	     file_ctx_t *ctx,
	     size_t size,
	     off_t offset)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)ctx->path));
  dict_set (request, "FD", int_to_data ((int)ctx->context));
  dict_set (request, "OFFSET", int_to_data (offset));
  dict_set (request, "LEN", int_to_data (size));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_READ, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_write (call_frame_t *frame,
	      xlator_t *this,
	      file_ctx_t *ctx,
	      int8_t *buf,
	      size_t size,
	      off_t offset)
{
  dict_t *request = get_new_dict ();
 
  dict_set (request, "PATH", str_to_data (ctx->path));
  dict_set (request, "OFFSET", int_to_data (offset));
  dict_set (request, "FD", int_to_data ((int)ctx->context));
  dict_set (request, "BUF", bin_to_data ((void *)buf, size));
 
  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_WRITE, request);

  dict_destroy (request);

  return 0;
}


int32_t 
client_statfs (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_STATFS, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_flush (call_frame_t *frame,
	      xlator_t *this,
	      file_ctx_t *ctx)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)ctx->path));
  dict_set (request, "FD", int_to_data ((int)ctx->context));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_FLUSH, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_release (call_frame_t *frame,
		xlator_t *this,
		file_ctx_t *ctx)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)ctx->path));
  dict_set (request, "FD", int_to_data ((int)ctx->context));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_UNLINK, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_fsync (call_frame_t *frame,
	      xlator_t *this,
	      file_ctx_t *ctx,
	      int32_t flags)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)ctx->path));
  dict_set (request, "FLAGS", int_to_data (flags));
  dict_set (request, "FD", int_to_data ((int)ctx->context));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_FSYNC, request);

  dict_destroy (request);
  return 0;

}


int32_t 
client_setxattr (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *path,
		 const int8_t *name,
		 const int8_t *value,
		 size_t size,
		 int32_t flags)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "FLAGS", int_to_data (flags));
  dict_set (request, "COUNT", int_to_data (size));
  dict_set (request, "BUF", str_to_data ((int8_t *)name));
  dict_set (request, "FD", str_to_data ((int8_t *)value));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_SETXATTR, request);

  return 0;
}


int32_t 
client_getxattr (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *path,
		 const int8_t *name,
		 size_t size)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "BUF", str_to_data ((int8_t *)name));
  dict_set (request, "COUNT", int_to_data (size));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_GETXATTR, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_listxattr (call_frame_t *frame,
		  xlator_t *this,
		  const int8_t *path,
		  size_t size)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "COUNT", int_to_data (size));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_LISTXATTR, request);

  dict_destroy (request);
  return 0;
}

		     
int32_t 
client_removexattr (call_frame_t *frame,
		    xlator_t *this,
		    const int8_t *path,
		    const int8_t *name)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "BUF", str_to_data ((int8_t *)name));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_REMOVEXATTR, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_opendir (call_frame_t *frame,
		xlator_t *this,
		const int8_t *path)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  //  dict_set (request, "FD", int_to_data ((long)tmp->context));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_OPENDIR, request);

  dict_destroy (request);
  return 0;
}


static int32_t 
client_readdir (call_frame_t *frame,
		xlator_t *this,
		const int8_t *path)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_READDIR, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_releasedir (call_frame_t *frame,
		   xlator_t *this,
		   file_ctx_t *ctx)
{
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}


int32_t 
client_fsyncdir (call_frame_t *frame,
		 xlator_t *this,
		 file_ctx_t *ctx,
		 int32_t flags)
{
  int32_t ret = 0;
  /*  int32_t remote_errno = 0;
      struct brick_private *priv = this->private;
      dict_t *request = get_new_dict ();
      dict_t *reply = get_new_dict ();

      if (priv->is_debug) {
      FUNCTION_CALLED;
      }

      {
      dict_set (request, "PATH", str_to_data ((int8_t *)path));
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
      goto ret;
      }

      ret:
      dict_destroy (reply); */
  STACK_UNWIND (frame, -1, ENOSYS);
  return ret;
}


int32_t 
client_access (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path,
	       mode_t mode)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "MODE", int_to_data (mode));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_ACCESS, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_ftruncate (call_frame_t *frame,
		  xlator_t *this,
		  file_ctx_t *ctx,
		  off_t offset)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)ctx->path));
  dict_set (request, "FD", int_to_data ((int)ctx->context));
  dict_set (request, "OFFSET", int_to_data (offset));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_FTRUNCATE, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_fgetattr (call_frame_t *frame,
		 xlator_t *this,
		 file_ctx_t *ctx)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)ctx->path));
  dict_set (request, "FD", int_to_data ((long)ctx->context));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_FGETATTR, request);

  dict_destroy (request);
  return 0;
}


/*
 * MGMT_OPS
 */

int32_t 
client_stats (call_frame_t *frame,
	      xlator_t *this, 
	      int32_t flags)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "LEN", int_to_data (0)); // without this dummy key the server crashes
  client_protocol_xfer (frame, this, OP_TYPE_MOP_REQUEST, OP_STATS, request);

  dict_destroy (request);
  return 0;
}


int32_t
client_fsck (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)
{
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}


int32_t 
client_lock (call_frame_t *frame,
	     xlator_t *this,
	     const int8_t *name)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)name));

  client_protocol_xfer (frame, this, OP_TYPE_MOP_REQUEST, OP_LOCK, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_unlock (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *name)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)name));

  client_protocol_xfer (frame, this, OP_TYPE_MOP_REQUEST, OP_UNLINK, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_listlocks (call_frame_t *frame,
		  xlator_t *this,
		  const int8_t *pattern)
{
  dict_t *request = get_new_dict ();
  
  dict_set (request, "OP", int_to_data (0xcafebabe));
  client_protocol_xfer (frame, this, OP_TYPE_MOP_REQUEST, OP_UNLINK, request);

  dict_destroy (request);

  return 0;
}


int32_t 
client_nslookup (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *path)
{
  return -1;

  dict_t *request = get_new_dict ();

  //  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  client_protocol_xfer (frame, this, OP_TYPE_MOP_REQUEST, OP_UNLINK, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_nsupdate (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *name,
		 dict_t *ns)
{
  return -1;

  dict_t *request = get_new_dict ();
  int8_t *ns_str = calloc (1, dict_serialized_length (ns));
  dict_serialize (ns, ns_str);
  //  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "NS", str_to_data (ns_str));

  client_protocol_xfer (frame, this, OP_TYPE_FOP_REQUEST, OP_NSUPDATE, request);

  dict_destroy (request);
  free (ns_str);
  return 0;
}


static int32_t
client_protocol_notify (xlator_t *this,
			transport_t *trans,
			int32_t event)
{
  int ret = 0;
  client_proto_priv_t *priv = trans->xl_private;

  if (!priv) {
    priv = (void *) calloc (1, sizeof (*priv));
    priv->callid = 1;
    trans->xl_private = priv;
  }

  if (event & (POLLIN|POLLPRI)) {
    gf_block_t *blk;

    blk = gf_block_unserialize_transport (trans);
    if (!blk) {
      client_protocol_cleanup (trans);
      return -1;
    }

    ret = client_protocol_interpret (trans, blk);

    free (blk->data);
    free (blk);
  }

  if (event & (POLLERR|POLLHUP)) {
    client_protocol_cleanup (trans);
  }

  gf_log ("protocol/client",
	  GF_LOG_DEBUG,
	  "notify returning: %d",
	  ret);
  return ret;
}

static int32_t 
client_protocol_cleanup (transport_t *trans)
{
  return 0;
}

static int32_t
client_protocol_interpret (transport_t *trans,
			   gf_block_t *blk)
{
  int32_t ret = 0;
  dict_t *args = get_new_dict ();
  call_frame_t *frame = NULL;

  if (!args) {
    gf_log ("client/protocol",
	    GF_LOG_DEBUG,
	    "client_protocol_interpret: get_new_dict () returned NULL");
    return -1;
  }
  
  dict_unserialize (blk->data, blk->size, &args);

  if (!args) {
    return -1;
  }

  switch (blk->type) {
  case OP_TYPE_FOP_REPLY:
    if (blk->op > FOP_MAXVALUE || blk->op < 0) {
      gf_log ("protocol/client",
	      GF_LOG_DEBUG,
	      "invalid opcode '%d'",
	      blk->op);
      ret = -1;
      break;
    }

    frame = lookup_frame (trans, blk->callid);

    switch (blk->op) {
    case OP_CREATE:
      {
	char *buf = data_to_str (dict_get (args, "BUF"));
	struct stat *stbuf = str_to_stat (buf);

	file_ctx_t *ctx = calloc (1, sizeof (*ctx));

	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      ctx,
		      stbuf);
	free (stbuf);
      }
    case OP_OPEN:
      {
	char *buf = data_to_str (dict_get (args, "BUF"));
	struct stat *stbuf = str_to_stat (buf);

	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      data_to_int (dict_get (args, "CTX")),
		      stbuf);

	free (stbuf);
	break;
      }
    case OP_GETATTR:
      {
	int8_t *buf = data_to_str (dict_get (args, "BUF"));
	struct stat *stbuf = str_to_stat (buf);

	STACK_UNWIND (frame, 
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      stbuf); 
	free (stbuf);
	break;
      }
    case OP_READ:
      {
	int8_t *buf = data_to_str (dict_get (args, "BUF"));
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      buf);
	break;
      }
    case OP_WRITE:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_READDIR:
      {
	dir_entry_t *entry = calloc (1, sizeof (dir_entry_t));
	dir_entry_t *trav, *prev = entry;
	int32_t nr_count = data_to_int (dict_get (args, "NR_ENTRIES"));
	int32_t count, i, bread;
	int8_t *buf = data_to_str (dict_get (args, "BUF"));
	int8_t *ender, *buffer_ptr = buf;
	int8_t tmp_buf[512];

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
	
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      entry,
		      nr_count);

	// free entries;
	prev = entry;
	trav = entry->next;
	while (trav) {
	  prev->next = trav->next;
	  free (trav->name);
	  free (trav);
	  trav = prev->next;
	}
	free (entry);
	break;
      }
    case OP_FSYNC:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_CHOWN:
      {
	char *buf = data_to_str (dict_get (args, "BUF"));
	struct stat *stbuf = str_to_stat (buf);

	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      stbuf);
	free (stbuf);
	break;
      }
    case OP_CHMOD:
      {
	char *buf = data_to_str (dict_get (args, "BUF"));
	struct stat *stbuf = str_to_stat (buf);

	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      stbuf);
	free (stbuf);
	break;
      }
    case OP_UNLINK:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_RENAME:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_READLINK:
      {
	STACK_UNWIND (frame, 
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      data_to_str (dict_get (args, "BUF")));
	break;
      }
    case OP_SYMLINK:
      {
	char *buf = data_to_str (dict_get (args, "BUF"));
	struct stat *stbuf = str_to_stat (buf);

	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      stbuf);
	free (stbuf);
	break;
      }
    case OP_MKNOD:
      {
	char *buf = data_to_str (dict_get (args, "BUF"));
	struct stat *stbuf = str_to_stat (buf);

	STACK_UNWIND (frame, 
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      stbuf);
	free (stbuf);

	break;
      }
    case OP_MKDIR:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_LINK:
      {
	char *buf = data_to_str (dict_get (args, "BUF"));
	struct stat *stbuf = str_to_stat (buf);

	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      stbuf);
	free (stbuf);
	break;
      }
    case OP_FLUSH:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_RELEASE:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_OPENDIR:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      data_to_bin (dict_get (args, "CTX")));
	break;
      }
    case OP_RMDIR:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_TRUNCATE:
      {
	char *buf = data_to_str (dict_get (args, "BUF"));
	struct stat *stbuf = str_to_stat (buf);

	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      stbuf);
	free (stbuf);
	break;
      }
    case OP_UTIME:
      {
	char *buf = data_to_str (dict_get (args, "BUF"));
	struct stat *stbuf = str_to_stat (buf);

	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      stbuf);
	free (stbuf);
	break;
      }
    case OP_STATFS:
      {
	struct statvfs *stbuf = calloc (1, sizeof (struct statvfs));
	int8_t *buf = data_to_str (dict_get (args, "BUF"));
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
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      stbuf);
	free (stbuf);
	break;
      }
    case OP_SETXATTR:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_GETXATTR:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      (void *)0); // FIXME
	break;
      }
    case OP_LISTXATTR:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_REMOVEXATTR:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_RELEASEDIR:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_FSYNCDIR:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_ACCESS:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_FTRUNCATE:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_FGETATTR:
      {
	char *buf = data_to_str (dict_get (args, "BUF"));
	struct stat *stbuf = str_to_stat (buf);

	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	free (stbuf);
	break;
      }
    }
    break;
    
  case OP_TYPE_MOP_REPLY:

    if (blk->op > MOP_MAXVALUE || blk->op < 0)
      return -1;

    //    gf_mops[blk->op] (frame, bound_xl, args);

    switch (blk->op) {
    case OP_STATS:
      {
	struct xlator_stats stats;
	int8_t *buf = data_to_bin (dict_get (args, "BUF"));
	sscanf (buf, "%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64"\n",
		&stats.nr_files,
		&stats.disk_usage,
		&stats.free_disk,
		&stats.read_usage,
		&stats.write_usage,
		&stats.disk_speed,
		&stats.nr_clients);

	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")),
		      stats);
	break;
      }
    case OP_FSCK:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }      
    case OP_LOCK:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_UNLOCK:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    case OP_LISTLOCKS:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	/* FIXME: locks */
	break;
      }
    case OP_NSLOOKUP:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	/* FIXME: ns */
	break;
      }
    case OP_NSUPDATE:
      {
	STACK_UNWIND (frame,
		      data_to_int (dict_get (args, "RET")),
		      data_to_int (dict_get (args, "ERRNO")));
	break;
      }
    }
    break;
  default:
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "invalid packet type: %d",
	    blk->type);
    ret = -1;
  }

  dict_destroy (args);
  return 0;
}


int32_t 
init (xlator_t *this)
{
  transport_t *trans;
  client_proto_priv_t *priv;

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
  .utime       = client_utime,
  .open        = client_open,
  .read        = client_read,
  .write       = client_write,
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
  .fgetattr    = client_fgetattr
};

struct xlator_mops mops = {
  .stats = client_stats,
  .lock = client_lock,
  .unlock = client_unlock,
  .listlocks = client_listlocks,
  .nslookup = client_nslookup,
  .nsupdate = client_nsupdate
};
