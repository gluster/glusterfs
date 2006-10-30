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

#include <signal.h>

#if __WORDSIZE == 64
# define F_L64 "%l"
#else
# define F_L64 "%ll"
#endif

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

  int32_t dict_len = dict_serialized_length (request);
  int8_t *dict_buf = malloc (dict_len);
  dict_serialize (request, dict_buf);

  gf_block *blk = gf_block_new ();
  blk->type =  type;
  blk->op = op;
  blk->size = dict_len;
  blk->data = dict_buf;

  int32_t blk_len = gf_block_serialized_length (blk);
  int8_t *blk_buf = malloc (blk_len);
  gf_block_serialize (blk, blk_buf);

  transport_t *trans = this->private;
  if (!trans) {
    gf_log ("protocol/client: client_protocol_xfer: ", GF_LOG_ERROR, "this->private is NULL");
    return -1;
  }

  int ret = transport_submit (trans, blk_buf, blk_len);
  free (blk_buf);
  free (dict_buf);
  free (blk);

  if (ret != blk_len) {
    gf_log ("protocol/client: client_protocol_xfer: ", GF_LOG_ERROR, "transport_submit failed");
    return -1;
  }

  return 0;
}

int32_t
client_create (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path,
	       mode_t mode)
{
}

int32_t 
client_create_rsp (call_frame_t *frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   file_ctx_t *ctx,
		   struct stat *buf)
{
}

int32_t 
client_open (call_frame_t *frame,
	     xlator_t *this,
	     int32_t *flags,
	     mode_t mode)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "FLAGS", int_to_data (flags));
  dict_set (request, "MODE", int_to_data (mode));

  STACK_WIND (frame, client_open_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_OPEN, request);

  dict_destroy (request);
  return 0;
}

int32_t 
client_open_rsp (call_frame_t *frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 file_ctx_t *ctx,
		 struct stat *buf)
{
}

static int8_t *
stat_to_str (struct stat *stbuf)
{
  char *tmp_buf;

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
  uint32_t atime_nsec = stbuf->st_atim.tv_nsec;
  uint32_t mtime = stbuf->st_mtime;
  uint32_t mtime_nsec = stbuf->st_mtim.tv_nsec;
  uint32_t ctime = stbuf->st_ctime;
  uint32_t ctime_nsec = stbuf->st_ctim.tv_nsec;

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
  
  return tmp_buf;
}

int32_t 
client_getattr (call_frame_t *frame,
		xlator_t *this,
		const int8_t *path)
{
  dict_t *request = get_new_dict ();
  
  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  STACK_WIND (frame, client_getattr_rsp, this,
	      client_protocol_xfer,
	      OP_TYPE_FOP_REQUEST, OP_GETATTR, request);
  dict_destroy (request);
  return 0;
}

int32_t 
client_getattr_rsp (call_frame_t *frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
}

int32_t 
client_readlink (call_frame_t *frame,
		 xlator_t *xl,
		 const int8_t *path)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "LEN", int_to_data (size));

  STACK_WIND (frame, client_readlink_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_READLINK, request);

  dict_destroy (request);
  return 0;
}

int32_t 
client_readlink_rsp (call_frame_t *frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     int8_t *buf)
{
}

int32_t 
client_mknod (call_frame_t *frame,
	      xlator_t *xl,
	      const int8_t *path,
	      mode_t mode,
	      dev_t dev)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "MODE", int_to_data (mode));
  dict_set (request, "DEV", int_to_data (dev));
  //  dict_set (request, "UID", int_to_data (uid));
  //  dict_set (request, "GID", int_to_data (gid));

  STACK_WIND (frame, client_mknod_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_MKNOD, request);
  return 0;
}

int32_t
client_mknod_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
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
  //    dict_set (request, "UID", int_to_data (uid));
  //    dict_set (request, "GID", int_to_data (gid));

  STACK_WIND (frame, client_mkdir_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_MKDIR, request);
  return 0;
}

int32_t 
client_mkdir_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
}

int32_t 
client_unlink (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;

  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  STACK_WIND (frame, client_unlink_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_UNLINK, request);
  dict_destroy (request);
  return 0;
}

int32_t 
client_unlink_rsp (call_frame_t *frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
client_rmdir (call_frame_t *frame
	      xlator_t *this,
	      const int8_t *path)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  STACK_WIND (frame, client_rmdir_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_RMDIR, request);
  return 0;
}

int32_t
client_rmdir_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
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
  //    dict_set (request, "UID", int_to_data (uid));
  //    dict_set (request, "GID", int_to_data (gid));

  STACK_WIND (frame, client_symlink_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_SYMLINK, request);
  return 0;
}

int32_t 
client_symlink_rsp (call_frame_t *frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
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
  dict_set (request, "UID", int_to_data (uid));
  dict_set (request, "GID", int_to_data (gid));

  STACK_WIND (frame, client_rename_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_RENAME, request);
  return 0;
}

int32_t 
client_rename_rsp (call_frame_t *frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
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
  dict_set (request, "UID", int_to_data (uid));
  dict_set (request, "GID", int_to_data (gid));

  STACK_WIND (frame, client_link_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_LINK, request);

  dict_destroy (request);
  return 0;
}

int32_t 
client_link_rsp (call_frame_t *frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
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

  STACK_WIND (frame, client_chmod_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_CHMOD, request);

  dict_destroy (request);
  return 0;
}

int32_t
client_chmod_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
}

int32_t 
client_chown (call_frame_t *frame,
	      xlator_t *xl,
	      const int8_t *path)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "UID", int_to_data (uid));
  dict_set (request, "GID", int_to_data (gid));

  STACK_WIND (frame, client_chown_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_CHOWN, request);

  dict_destroy (request);
  return 0;
}

int32_t
client_chown_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
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

  STACK_WIND (frame, client_truncate_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_TRUNCATE, request);

  dict_destroy (request);
  return 0;
}

int32_t client_truncate_rsp (call_frame_t *frame,
			     xlator_t *this,
			     int32_t op_ret,
			     int32_t op_errno,
			     struct stat *buf)
{
}

int32_t 
brick_utime (call_frame_t *frame,
	     xlator_t *this,
	     const int8_t *path,
	     struct utimbuf *buf)
{
  dict_t *request = get_new_dict ();

    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "ACTIME", int_to_data (buf->actime));
    dict_set (request, "MODTIME", int_to_data (buf->modtime));

  STACK_WIND (frame, client_utime_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_UTIME, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_utime_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
}

int32_t 
client_read (call_frame_t *frame,
	     xlator_t *this,
	     const int8_t *path,
	     int8_t *buf,
	     size_t size,
	     off_t offset)
{
  dict_t *request = get_new_dict ();

    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "FD", int_to_data (fd));
    dict_set (request, "OFFSET", int_to_data (offset));
    dict_set (request, "LEN", int_to_data (size));

  STACK_WIND (frame, client_read_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_READ, request);

  dict_destroy (request);
  return 0;
}

int32_t
client_read_rsp (call_frame_t *frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 int8_t *buf)
{
}

int32_t 
client_write (call_frame_t *frame,
	      xlator_t *this,
	      const int8_t *path,
	      const int8_t *buf,
	      size_t size,
	      off_t offset)
{
   dict_t *request = get_new_dict ();
 
   dict_set (request, "PATH", str_to_data ((int8_t *)path));
   dict_set (request, "OFFSET", int_to_data (offset));
   dict_set (request, "FD", int_to_data (fd));
   dict_set (request, "BUF", bin_to_data ((void *)buf, size));
 
   STACK_WIND (frame, client_write_rsp, this, 
	       client_protocol_xfer, 
	       OP_TYPE_FOP_REQUEST, OP_WRITE, request);

   dict_destroy (request);

   return 0;
}


int32_t
client_write_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
}

int32_t 
client_statfs (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  STACK_WIND (frame, client_statfs_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_STATFS, request);


  dict_destroy (request);
  return 0;
}

int32_t
client_statfs_rsp (call_frame_t *frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct statvfs *buf)
{
}

int32_t 
client_flush (call_frame_t *frame,
	      xlator_t *this,
	      file_ctx_t *ctx)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "FD", int_to_data (fd));

  STACK_WIND (frame, client_flush_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_FLUSH, request);


  dict_destroy (request);
  return 0;
}

int32_t 
client_flush_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
}

int32_t 
client_release (call_frame_t *frame,
		xlator_t *this,
		file_ctx_t *ctx)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "FD", int_to_data (fd));

  STACK_WIND (frame, client_release_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_UNLINK, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_release_rsp (call_frame_t *frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
}

int32_t 
client_fsync (call_frame_t *frame,
	      xlator_t *this,
	      file_ctx_t *ctx,
	      int32_t flags)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "FLAGS", int_to_data (datasync));
  dict_set (request, "FD", int_to_data (fd));

  STACK_WIND (frame, client_fsync_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_FSYNC, request);

  dict_destroy (request);
  return 0;

}

int32_t 
client_fsync_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
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

  STACK_WIND (frame, client_setxattr_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_SETXATTR, request);
  return 0;
}

int32_t 
client_setxattr_rsp (call_frame_t *frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
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

  STACK_WIND (frame, client_getxattr_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_GETXATTR, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_getxattr_rsp (call_frame_t *frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     void *value)
{
}

int32_t 
brick_listxattr (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *path,
		 size_t size)
{
  dict_t *request = get_new_dict ();

    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "COUNT", int_to_data (size));

  STACK_WIND (frame, client_listxattr_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_LISTXATTR, request);

  dict_destroy (request);
  return 0;
}

int32_t 
client_listxattr_rsp (call_frame_t *frame,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      void *value)
{
}
		     
int32_t 
brick_removexattr (call_frame_t *frame,
		   xlator_t *this,
		   const int8_t *path,
		   const int8_t *name)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "BUF", str_to_data ((int8_t *)name));

  STACK_WIND (frame, client_removexattr_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_REMOVEXATTR, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_removexattr_rsp (call_frame_t *frame,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
}

int32_t 
client_opendir (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "FD", int_to_data ((long)tmp->context));

  STACK_WIND (frame, client_opendir_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_OPENDIR, request);

  dict_destroy (request);
  return 0;
}

int32_t 
client_opendir_rsp (call_frame_t *frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    file_ctx_t *ctx)
{
}

static int8_t *
client_readdir (call_frame_t *frame,
		xlator_t *this,
		const int8_t *path)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "OFFSET", int_to_data (offset));

  STACK_WIND (frame, client_readdir_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_READDIR, request);

  dict_destroy (request);
  return 0;
}

int32_t 
client_readdir_rsp (call_frame_t *frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dir_entry_t *entries,
		    int32_t count)
{
}

int32_t 
client_releasedir (call_frame_t *frame,
		  xlator_t *this,
		  file_ctx_t *ctx)
{
  return 0;
}

int32_t 
client_releasedir_rsp (call_frame_t *frame,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
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
  return ret;
}


int32_t 
client_fsyncdir_rsp (call_frame_t *frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
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

  STACK_WIND (frame, client_access_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_ACCESS, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_access_rsp (call_frame_t *frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
}

int32_t 
client_ftruncate (call_frame_t *frame,
		 xlator_t *this,
		 file_ctx_t *ctx,
		 off_t offset)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "FD", int_to_data (fd));
  dict_set (request, "OFFSET", int_to_data (offset));

  STACK_WIND (frame, client_ftruncate_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_FTRUNCATE, request);

  dict_destroy (request);
  return 0;
}


int32_t 
client_ftruncate_rsp (call_frame_t *frame,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf)
{
}

int32_t 
client_fgetattr (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *path,
		 struct stat *stbuf,
		 struct file_context *ctx)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "FD", int_to_data ((long)tmp->context));

  STACK_WIND (frame, client_fgetattr_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_FGETATTR, request);

  dict_destroy (request);
  return 0;
}

int32_t 
client_fgetattr_rsp (call_frame_t *frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
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
  STACK_WIND (frame, client_stats_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_MGMT_REQUEST, OP_STATS, request);

  dict_destroy (request);
  return 0;
}

int32_t
client_stats_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct xlator_stats *stats);
{
}

int32_t
client_fsck (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)
{
  return 0;
}

int32_t
client_fsck_rsp (call_frame_t *frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
}

int32_t 
client_lock (call_frame_t *frame,
	     xlator_t *this,
	     const int8_t *name)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)name));

  STACK_WIND (frame, client_unlink_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_MGMT_REQUEST, OP_LOCK, request);

  dict_destroy (request);
  return 0;
}

int32_t
client_lock_rsp (call_frame_t *frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
}

int32_t 
client_unlock (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *name)
{
  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)name));

  STACK_WIND (frame, client_unlock_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_MGMT_REQUEST, OP_UNLINK, request);

  dict_destroy (request);
  return 0;
}

int32_t
client_unlock_rsp (call_frame_t *frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
}

int32_t 
client_listlocks (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *pattern)
{
  dict_t *request = get_new_dict ();
  
  dict_set (request, "OP", int_to_data (0xcafebabe));
  STACK_WIND (frame, client_listlocks_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_MGMT_REQUEST, OP_UNLINK, request);

  dict_destroy (request);

  return 0;
}

int32_t
client_listlocks_rsp (call_frame_t *frame,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
}

int32_t 
client_nslookup (call_frame_t *frame
		 xlator_t *this,
		 const int8_t *path,
		 dict_t *ns)
{
  return -1;

  dict_t *request = get_new_dict ();

  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  STACK_WIND (frame, client_nslookup_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_MGMT_REQUEST, OP_UNLINK, request);

  dict_destroy (request);
  return 0;
}

int32_t 
client_nslookup_rsp (call_frame_t *frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     dict_t *ns)
{
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
  dict_set (request, "PATH", str_to_data ((int8_t *)path));
  dict_set (request, "NS", str_to_data (ns_str));

  STACK_WIND (frame, client_nsupdate_rsp, this, 
	      client_protocol_xfer, 
	      OP_TYPE_FOP_REQUEST, OP_NSUPDATE, request);

  dict_destroy (request);
  free (ns_str);
  return 0;
}

int32_t
client_nsupdate_rsp (call_frame_t *frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
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

  return ret;
}

int32_t 
init (xlator_t *this)
{
  transport_t *trans;
  trans = transport_load (this->options, 
			  this,
			  client_protocol_notify);
  this->private = trans;
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
  .fgetattr    = client_fgetattr,
  .bulk_getattr = client_bulk_getattr
};

struct xlator_fop_rsps fop_rsps = {
  .getattr     = client_getattr_rsp,
  .readlink    = client_readlink_rsp,
  .mknod       = client_mknod_rsp,
  .mkdir       = client_mkdir_rsp,
  .unlink      = client_unlink_rsp,
  .rmdir       = client_rmdir_rsp,
  .symlink     = client_symlink_rsp,
  .rename      = client_rename_rsp,
  .link        = client_link_rsp,
  .chmod       = client_chmod_rsp,
  .chown       = client_chown_rsp,
  .truncate    = client_truncate_rsp,
  .utime       = client_utime_rsp,
  .open        = client_open_rsp,
  .read        = client_read_rsp,
  .write       = client_write_rsp,
  .statfs      = client_statfs_rsp,
  .flush       = client_flush_rsp,
  .release     = client_release_rsp,
  .fsync       = client_fsync_rsp,
  .setxattr    = client_setxattr_rsp,
  .getxattr    = client_getxattr_rsp,
  .listxattr   = client_listxattr_rsp,
  .removexattr = client_removexattr_rsp,
  .opendir     = client_opendir_rsp,
  .readdir     = client_readdir_rsp,
  .releasedir  = client_releasedir_rsp,
  .fsyncdir    = client_fsyncdir_rsp,
  .access      = client_access_rsp,
  .ftruncate   = client_ftruncate_rsp,
  .fgetattr    = client_fgetattr_rsp,
  .bulk_getattr = client_bulk_getattr_rsp
};

struct xlator_mops mgmt_ops = {
  .stats = client_stats,
  .lock = client_lock,
  .unlock = client_unlock,
  .listlocks = client_listlocks,
  .nslookup = client_nslookup,
  .nsupdate = client_nsupdate
};

struct xlator_mgmt_rsps mgmt_op_rsps = {
  .stats = client_stats_rsp,
  .lock = client_lock_rsp,
  .unlock = client_unlock_rsp,
  .listlocks = client_listlocks_rsp,
  .nslookup = client_nslookup_rsp,
  .nsupdate = client_nsupdate_rsp
};
