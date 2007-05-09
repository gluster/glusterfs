/*
  (C) 2006, 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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

#include "transport.h"
#include "fnmatch.h"
#include "xlator.h"
#include "protocol.h"
#include "lock.h"
#include "ns.h"
#include "proto-srv.h"
#include <time.h>
#include <sys/uio.h>

#if __WORDSIZE == 64
# define F_L64 "%l"
#else
# define F_L64 "%ll"
#endif

static char *
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


static int32_t
generic_reply (call_frame_t *frame,
	       int32_t type,
	       glusterfs_fop_t op,
	       dict_t *params)
{
  gf_block_t *blk;
  transport_t *trans;
  int32_t count, i, ret;
  struct iovec *vector;

  trans = frame->root->state;
  
  blk = gf_block_new (frame->root->unique);
  blk->data = NULL;
  blk->size = 0;
  blk->type = type;
  blk->op   = op;
  blk->dict = params;

  count = gf_block_iovec_len (blk);
  vector = alloca (count * sizeof (*vector));
  memset (vector, 0, count * sizeof (*vector));

  gf_block_to_iovec (blk, vector, count);
  for (i=0; i<count; i++)
    if (!vector[i].iov_base)
      vector[i].iov_base = alloca (vector[i].iov_len);
  gf_block_to_iovec (blk, vector, count);

  free (blk);

  ret = trans->ops->writev (trans, vector, count);
  if (ret != 0) {
    gf_log ("protocol/server", 
	    GF_LOG_ERROR,
	    "transport_writev failed");
    transport_except (trans);
  }

  transport_unref (trans);

  return 0;
}

static int32_t
fop_reply (call_frame_t *frame,
	   glusterfs_fop_t op,
	   dict_t *params)
{
  return generic_reply (frame,
			GF_OP_TYPE_FOP_REPLY,
			op,
			params);
}

static int32_t
mop_reply (call_frame_t *frame,
	   glusterfs_fop_t op,
	   dict_t *params)
{
  return generic_reply (frame,
			GF_OP_TYPE_MOP_REPLY,
			op,
			params);
}

static int32_t
fop_getattr_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  char *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));

  fop_reply (frame,
	     GF_FOP_GETATTR,
	     dict);

  free (stat_buf);
  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_getattr (call_frame_t *frame,
	     xlator_t *bound_xl,
	     dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  struct stat buf = {0, };

  if (!path_data) {
    fop_getattr_cbk (frame,
		     NULL,
		     frame->this,
		     -1,
		     EINVAL,
		     &buf);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_getattr_cbk, 
	      bound_xl,
	      bound_xl->fops->getattr,
	      data_to_str (path_data));

  return 0;
}

static int32_t
fop_readlink_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  char *buf)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "BUF", str_to_data (buf ? (char *) buf : "" ));

  fop_reply (frame,
	     GF_FOP_READLINK,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_readlink (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *len_data = dict_get (params, "LEN");

  if (!path_data || !len_data) {
    fop_readlink_cbk (frame,
		      NULL,
		      frame->this,
		      -1,
		      EINVAL,
		      "");
    return -1;
  }

  STACK_WIND (frame,
	      fop_readlink_cbk,
	      bound_xl,
	      bound_xl->fops->readlink,
	      data_to_str (path_data),
	      (size_t) data_to_int32 (len_data));

  return 0;
}


/* create */
static int32_t
fop_create_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		dict_t *ctx,
		struct stat *buf)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "FD", int_to_data ((long)ctx));
  
  char *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));

  if (op_ret >= 0) {
    struct proto_srv_priv *priv = ((transport_t *)frame->root->state)->xl_private;
    char ctx_buf[32] = {0,};
    sprintf (ctx_buf, "%p", ctx);
    dict_set (priv->open_files, ctx_buf, str_to_data (""));
  }
  
  fop_reply (frame,
	     GF_FOP_CREATE,
	     dict);
  
  free (stat_buf);
  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_create (call_frame_t *frame,
	    xlator_t *bound_xl,
	    dict_t *params)
{
  int32_t flags = 0;
  data_t *path_data = dict_get (params, "PATH");
  data_t *mode_data = dict_get (params, "MODE");
  data_t *flag_data = dict_get (params, "FLAGS");

  if (!path_data || !mode_data) {
    struct stat buf = {0, };
    fop_create_cbk (frame,
		    NULL,
		    frame->this,
		    -1,
		    EINVAL,
		    NULL,
		    &buf);
    return -1;
  }

  if (flag_data) {
    flags = data_to_int32 (flag_data);
  }

  STACK_WIND (frame, 
	      fop_create_cbk, 
	      bound_xl,
	      bound_xl->fops->create,
	      data_to_str (path_data),
	      flags,
	      data_to_int64 (mode_data));
  
  return 0;
}

/*open*/
static int32_t
fop_open_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      dict_t *ctx,
	      struct stat *buf)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "FD", int_to_data ((long)ctx));
  
  char *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));

  if (op_ret >= 0) {
    struct proto_srv_priv *priv = ((transport_t *)frame->root->state)->xl_private;
    char ctx_buf[32] = {0,};
    sprintf (ctx_buf, "%p", ctx);
    dict_set (priv->open_files, ctx_buf, str_to_data (""));
  }
  
  fop_reply (frame,
	     GF_FOP_OPEN,
	     dict);

  free (stat_buf);
  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_open (call_frame_t *frame,
	  xlator_t *bound_xl,
	  dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *mode_data = dict_get (params, "MODE");
  data_t *flag_data = dict_get (params, "FLAGS");
  
  if (!path_data || !mode_data || !flag_data) {
    struct stat buf = {0, };
    fop_open_cbk (frame,
		  NULL,
		  frame->this,
		  -1,
		  EINVAL,
		  NULL,
		  &buf);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_open_cbk, 
	      bound_xl,
	      bound_xl->fops->open,
	      data_to_str (path_data),
	      data_to_int64 (flag_data),
	      data_to_int64 (mode_data));

  return 0;
}

/*read*/
static int32_t
fop_readv_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct iovec *vector,
	       int32_t count)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  if (op_ret >= 0)
    dict_set (dict, "BUF", data_from_iovec (vector, count));
  else
    dict_set (dict, "BUF", str_to_data (""));

  fop_reply (frame,
	     GF_FOP_READ,
	     dict);
  
  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_readv (call_frame_t *frame,
	   xlator_t *bound_xl,
	   dict_t *params)
{
  data_t *ctx_data = dict_get (params, "FD");
  data_t *len_data = dict_get (params, "LEN");
  data_t *off_data = dict_get (params, "OFFSET");
  
  if (!ctx_data || !len_data || !off_data) {
    struct iovec vec;
    vec.iov_base = strdup ("");
    vec.iov_len = 0;
    fop_readv_cbk (frame,
		   NULL,
		   frame->this,
		   -1,
		   EINVAL,
		   &vec,
		   0);
    return -1;
  }
  
  STACK_WIND (frame, 
	      fop_readv_cbk,
	      bound_xl,
	      bound_xl->fops->readv,
	      data_to_ptr (ctx_data),
	      data_to_int32 (len_data),
	      data_to_int64 (off_data));
  
  return 0;
}

/*write*/
static int32_t
fop_writev_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  fop_reply (frame,
	     GF_FOP_WRITE,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_writev (call_frame_t *frame,
	    xlator_t *bound_xl,
	    dict_t *params)
{
  data_t *ctx_data = dict_get (params, "FD");
  data_t *len_data = dict_get (params, "LEN");
  data_t *off_data = dict_get (params, "OFFSET");
  data_t *buf_data = dict_get (params, "BUF");
  struct iovec iov;

  if (!ctx_data || !len_data || !off_data || !buf_data) {
    fop_writev_cbk (frame,
		    NULL,
		    frame->this,
		    -1,
		    EINVAL);
    return -1;
  }

  iov.iov_base = buf_data->data;
  iov.iov_len = data_to_int32 (len_data);

  STACK_WIND (frame, 
	      fop_writev_cbk, 
	      bound_xl,
	      bound_xl->fops->writev,
	      (dict_t *)data_to_ptr (ctx_data),
	      &iov,
	      1,
	      data_to_int64 (off_data));

  return 0;
}

/*release*/
static int32_t
fop_release_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  fop_reply (frame,
	     GF_FOP_RELEASE,
	     dict);
  
  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_release (call_frame_t *frame,
	     xlator_t *bound_xl,
	     dict_t *params)
{
  data_t *ctx_data = dict_get (params, "FD");

  if (!ctx_data) {
    fop_release_cbk (frame,
		     NULL,
		     frame->this,
		     -1,
		     EINVAL);
    return -1;
  }
  
  {
    char str[32];
    struct proto_srv_priv *priv = ((transport_t *)frame->root->state)->xl_private;
    sprintf (str, "%p", data_to_ptr (ctx_data));
    dict_del (priv->open_files, str);
  }
  STACK_WIND (frame, 
	      fop_release_cbk, 
	      bound_xl,
	      bound_xl->fops->release,
	      (dict_t *)data_to_ptr (ctx_data));

  return 0;
}

//fsync
static int32_t
fop_fsync_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  fop_reply (frame,
	     GF_FOP_FSYNC,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_fsync (call_frame_t *frame,
	   xlator_t *bound_xl,
	   dict_t *params)
{
  data_t *ctx_data = dict_get (params, "FD");
  data_t *flag_data = dict_get (params, "FLAGS");

  if (!ctx_data || !flag_data) {
    fop_fsync_cbk (frame,
		   NULL,
		   frame->this,
		   -1,
		   EINVAL);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_fsync_cbk, 
	      bound_xl,
	      bound_xl->fops->fsync,
	      (dict_t *)data_to_ptr (ctx_data),
	      data_to_int64 (flag_data));

  return 0;
}

//flush
static int32_t
fop_flush_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  fop_reply (frame,
	     GF_FOP_FLUSH,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_flush (call_frame_t *frame,
	   xlator_t *bound_xl,
	   dict_t *params)
{
  data_t *ctx_data = dict_get (params, "FD");

  if (!ctx_data) {
    fop_flush_cbk (frame,
		   NULL,
		   frame->this,
		   -1,
		   EINVAL);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_flush_cbk, 
	      bound_xl,
	      bound_xl->fops->flush,
	      (dict_t *)data_to_ptr (ctx_data));

  return 0;
}

//ftruncate
static int32_t
fop_ftruncate_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  char *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  fop_reply (frame,
	     GF_FOP_FTRUNCATE,
	     dict);
  free (stat_buf);
  
  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_ftruncate (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  data_t *ctx_data = dict_get (params, "FD");
  data_t *off_data = dict_get (params, "OFFSET");

  if (!ctx_data || !off_data) {
    struct stat buf = {0, };
    fop_ftruncate_cbk (frame,
		       NULL,
		       frame->this,
		       -1,
		       EINVAL,
		       &buf);
    return -1;
  }
  
  STACK_WIND (frame, 
	      fop_ftruncate_cbk, 
	      bound_xl,
	      bound_xl->fops->ftruncate,
	      (dict_t *)data_to_ptr (ctx_data),
	      data_to_int64 (off_data));

  return 0;
}

//fgetattr
static int32_t
fop_fgetattr_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  char *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  fop_reply (frame,
	     GF_FOP_FGETATTR,
	     dict);
  free (stat_buf);
  
  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_fgetattr (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *ctx_data = dict_get (params, "FD");

  if (!ctx_data) {
    struct stat buf = {0, };
    fop_fgetattr_cbk (frame,
		      NULL,
		      frame->this,
		      -1,
		      EINVAL,
		      &buf);
    return -1;
  }
  
  STACK_WIND (frame, 
	      fop_fgetattr_cbk, 
	      bound_xl,
	      bound_xl->fops->fgetattr,
	      (dict_t *)data_to_ptr (ctx_data));
  
  return 0;
}

//truncate
static int32_t
fop_truncate_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  char *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  fop_reply (frame,
	     GF_FOP_TRUNCATE,
	     dict);
  free (stat_buf);
  
  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_truncate (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *off_data = dict_get (params, "OFFSET");

  if (!path_data || !off_data) {
    struct stat buf = {0, };
    fop_truncate_cbk (frame,
		      NULL,
		      frame->this,
		      -1,
		      EINVAL,
		      &buf);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_truncate_cbk, 
	      bound_xl,
	      bound_xl->fops->truncate,
	      data_to_str (path_data),
	      data_to_int64 (off_data));
  
  return 0;
}


//link
static int32_t
fop_link_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct stat *buf)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  char *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));

  fop_reply (frame,
	     GF_FOP_LINK,
	     dict);
  free (stat_buf);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_link (call_frame_t *frame,
	  xlator_t *bound_xl,
	  dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *buf_data = dict_get (params, "BUF");

  if (!path_data || !buf_data) {
    struct stat buf = {0, };
    fop_link_cbk (frame,
		  NULL,
		  frame->this,
		  -1,
		  EINVAL,
		  &buf);
    return -1;
  }
  
  STACK_WIND (frame, 
	      fop_link_cbk, 
	      bound_xl,
	      bound_xl->fops->link,
	      data_to_str (path_data),
	      data_to_str (buf_data));

  return 0;
}

//symlink
static int32_t
fop_symlink_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  char *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  fop_reply (frame,
	     GF_FOP_SYMLINK,
	     dict);
  free (stat_buf);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_symlink (call_frame_t *frame,
	     xlator_t *bound_xl,
	     dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *buf_data = dict_get (params, "BUF");

  if (!path_data || !buf_data) {
    struct stat buf = {0, };
    fop_symlink_cbk (frame,
		     NULL,
		     frame->this,
		     -1,
		     EINVAL,
		     &buf);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_symlink_cbk, 
	      bound_xl,
	      bound_xl->fops->symlink,
	      data_to_str (path_data),
	      data_to_str (buf_data));

  return 0;
}

//unlink
static int32_t
fop_unlink_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  fop_reply (frame,
	     GF_FOP_UNLINK,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_unlink (call_frame_t *frame,
	    xlator_t *bound_xl,
	    dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");

  if (!path_data) {
    fop_unlink_cbk (frame,
		    NULL,
		    frame->this,
		    -1,
		    EINVAL);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_unlink_cbk, 
	      bound_xl,
	      bound_xl->fops->unlink,
	      data_to_str (path_data));

  return 0;
}

//rename
static int32_t
fop_rename_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  fop_reply (frame,
	     GF_FOP_RENAME,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_rename (call_frame_t *frame,
	    xlator_t *bound_xl,
	    dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *buf_data = dict_get (params, "BUF");

  if (!path_data || !buf_data) {
    fop_rename_cbk (frame,
		    NULL,
		    frame->this,
		    -1,
		    EINVAL);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_rename_cbk, 
	      bound_xl,
	      bound_xl->fops->rename,
	      data_to_str (path_data),
	      data_to_str (buf_data));
  
  return 0;
}

//setxattr
static int32_t
fop_setxattr_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  fop_reply (frame,
	     GF_FOP_SETXATTR,
	     dict);
  
  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_setxattr (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *buf_data = dict_get (params, "BUF");
  data_t *count_data = dict_get (params, "COUNT");
  data_t *flag_data = dict_get (params, "FLAGS");
  data_t *fd_data = dict_get (params, "FD"); // reused

  if (!path_data || !buf_data || !count_data || !flag_data || !fd_data) {
    fop_setxattr_cbk (frame,
		      NULL,
		      frame->this,
		      -1,
		      EINVAL);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_setxattr_cbk, 
	      bound_xl,
	      bound_xl->fops->setxattr,
	      data_to_str (path_data),
	      data_to_str (buf_data),
	      data_to_str (fd_data),
	      data_to_int64 (count_data),
	      data_to_int64 (flag_data));

  return 0;
}

//getxattr
static int32_t
fop_getxattr_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  void *value)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "BUF", str_to_data ((char *)value));
  fop_reply (frame,
	     GF_FOP_GETXATTR,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_getxattr (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *buf_data = dict_get (params, "BUF");
  data_t *path_data = dict_get (params, "PATH");
  data_t *count_data = dict_get (params, "COUNT");

  if (!path_data || !buf_data || !count_data) {
    fop_getxattr_cbk (frame,
		      NULL,
		      frame->this,
		      -1,
		      EINVAL,
		      NULL);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_getxattr_cbk, 
	      bound_xl,
	      bound_xl->fops->getxattr,
	      data_to_str (path_data),
	      data_to_str (buf_data),
	      data_to_int64 (count_data));

  return 0;
}

//listxattr
static int32_t
fop_listxattr_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   void *value)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "BUF", str_to_data ((char *)value));

  fop_reply (frame,
	     GF_FOP_LISTXATTR,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_listxattr (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *count_data = dict_get (params, "COUNT");

  if (!path_data || !count_data) {
    fop_listxattr_cbk (frame,
		       NULL,
		       frame->this,
		       -1,
		       EINVAL,
		       NULL);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_listxattr_cbk, 
	      bound_xl,
	      bound_xl->fops->listxattr,
	      data_to_str (path_data),
	      data_to_int64 (count_data));

  return 0;
}

//removexattr
static int32_t
fop_removexattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  fop_reply (frame,
	     GF_FOP_REMOVEXATTR,
	     dict);
  
  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_removexattr (call_frame_t *frame,
		 xlator_t *bound_xl,
		 dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *buf_data = dict_get (params, "BUF");

  if (!path_data || !buf_data) {
    fop_removexattr_cbk (frame,
			 NULL,
			 frame->this,
			 -1,
			 EINVAL);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_removexattr_cbk, 
	      bound_xl,
	      bound_xl->fops->removexattr,
	      data_to_str (path_data),
	      data_to_str (buf_data));
  
  return 0;
}

//statfs
static int32_t
fop_statfs_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct statvfs *buf)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  if (op_ret == 0) {
    char buffer[256] = {0,};
    
    uint32_t bsize = buf->f_bsize;
    uint32_t frsize = buf->f_frsize;
    uint64_t blocks = buf->f_blocks;
    uint64_t bfree = buf->f_bfree;
    uint64_t bavail = buf->f_bavail;
    uint64_t files = buf->f_files;
    uint64_t ffree = buf->f_ffree;
    uint64_t favail = buf->f_favail;
    uint32_t fsid = buf->f_fsid;
    uint32_t flag = buf->f_flag;
    uint32_t namemax = buf->f_namemax;
    
    sprintf (buffer, GF_STATFS_PRINT_FMT_STR,
	     bsize,
	     frsize,
	     blocks,
	     bfree,
	     bavail,
	     files,
	     ffree,
	     favail,
	     fsid,
	     flag,
	     namemax);
    
    dict_set (dict, "BUF", str_to_data (buffer));
  }

  fop_reply (frame,
	     GF_FOP_STATFS,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_statfs (call_frame_t *frame,
	    xlator_t *bound_xl,
	    dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");

  if (!path_data) {
    struct statvfs buf = {0,};
    fop_statfs_cbk (frame,
		    NULL,
		    frame->this,
		    -1,
		    EINVAL,
		    &buf);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_statfs_cbk, 
	      bound_xl,
	      bound_xl->fops->statfs,
	      data_to_str (path_data));

  return 0;
}


//opendir
static int32_t
fop_opendir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dict_t *ctx)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "FD", int_to_data ((long)ctx));

  fop_reply (frame,
	     GF_FOP_OPENDIR,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_opendir (call_frame_t *frame,
	     xlator_t *bound_xl,
	     dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");

  if (!path_data) {
    fop_opendir_cbk (frame,
		     NULL,
		     frame->this,
		     -1,
		     EINVAL,
		     NULL);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_opendir_cbk, 
	      bound_xl,
	      bound_xl->fops->opendir,
	      data_to_str (path_data));

  return 0;
}

//releasedir
static int32_t
fop_releasedir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  fop_reply (frame,
	     GF_FOP_RELEASEDIR,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_releasedir (call_frame_t *frame,
		xlator_t *bound_xl,
		dict_t *params)
{
  data_t *ctx_data = dict_get (params, "FD");
 
  if (!ctx_data) {
    fop_releasedir_cbk (frame,
			NULL,
			frame->this,
			-1,
			EINVAL);
    return -1;
  }
  
  STACK_WIND (frame, 
	      fop_releasedir_cbk, 
	      bound_xl,
	      bound_xl->fops->releasedir,
	      (dict_t *)data_to_ptr (ctx_data));
  
  return 0;
}

//readdir
static int32_t
fop_readdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dir_entry_t *entries,
		 int32_t count)
{
  dict_t *dict = get_new_dict ();
  char *buffer;

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "NR_ENTRIES", int_to_data (count));
  {   

    dir_entry_t *trav = entries->next;
    uint32_t len = 0;
    char *tmp_buf = NULL;
    while (trav) {
      len += strlen (trav->name);
      len += 1;
      len += 256; // max possible for statbuf;
      trav = trav->next;
    }

    buffer = calloc (1, len);
    char *ptr = buffer;
    trav = entries->next;
    while (trav) {
      int this_len;
      tmp_buf = stat_to_str (&trav->buf);
      this_len = sprintf (ptr, "%s/%s", 
			  trav->name,
			  tmp_buf);

      free (tmp_buf);
      trav = trav->next;
      ptr += this_len;
    }
    dict_set (dict, "BUF", str_to_data (buffer));
  }

  fop_reply (frame,
	     GF_FOP_READDIR,
	     dict);

  free (buffer);
  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_readdir (call_frame_t *frame,
	     xlator_t *bound_xl,
	     dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");

  if (!path_data) {
    dir_entry_t tmp = {0,};
    fop_readdir_cbk (frame,
		     NULL,
		     frame->this,
		     -1,
		     EINVAL,
		     &tmp,
		     0);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_readdir_cbk, 
	      bound_xl,
	      bound_xl->fops->readdir,
	      data_to_str (path_data));

  return 0;
}

//fsyncdir
static int32_t
fop_fsyncdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  fop_reply (frame,
	     GF_FOP_FSYNCDIR,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_fsyncdir (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *ctx_data = dict_get (params, "FD");
  data_t *flag_data = dict_get (params, "FLAGS");

  if (!ctx_data || !flag_data) {
    fop_fsyncdir_cbk (frame,
		      NULL,
		      frame->this,
		      -1,
		      EINVAL);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_fsyncdir_cbk, 
	      bound_xl,
	      bound_xl->fops->fsyncdir,
	      (dict_t *)data_to_ptr (ctx_data),
	      data_to_int64 (flag_data));

  return 0;
}

//mknod
static int32_t
fop_mknod_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  char *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));

  fop_reply (frame,
	     GF_FOP_MKNOD,
	     dict);
  free (stat_buf);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_mknod (call_frame_t *frame,
	   xlator_t *bound_xl,
	   dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *mode_data = dict_get (params, "MODE");
  data_t *dev_data = dict_get (params, "DEV");

  if (!path_data || !mode_data || !dev_data) {
    struct stat buf = {0, };
    fop_mknod_cbk (frame,
		   NULL,
		   frame->this,
		   -1,
		   EINVAL,
		   &buf);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_mknod_cbk, 
	      bound_xl,
	      bound_xl->fops->mknod,
	      data_to_str (path_data),
	      data_to_int64 (mode_data),
	      data_to_int64 (dev_data));

  return 0;
}

//mkdir
static int32_t
fop_mkdir_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
  dict_t *dict = get_new_dict ();
  char *statbuf;

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  statbuf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (statbuf));

  fop_reply (frame,
	     GF_FOP_MKDIR,
	     dict);

  free (statbuf);
  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_mkdir (call_frame_t *frame,
	   xlator_t *bound_xl,
	   dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *mode_data = dict_get (params, "MODE");

  if (!path_data || !mode_data) {
    fop_mkdir_cbk (frame,
		   NULL,
		   frame->this,
		   -1,
		   EINVAL,
		   NULL);
    return -1;
  }
  
  STACK_WIND (frame, 
	      fop_mkdir_cbk, 
	      bound_xl,
	      bound_xl->fops->mkdir,
	      data_to_str (path_data),
	      data_to_int64 (mode_data));
  
  return 0;
}

//rmdir
static int32_t
fop_rmdir_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  fop_reply (frame,
	     GF_FOP_RMDIR,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_rmdir (call_frame_t *frame,
	   xlator_t *bound_xl,
	   dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");

  if (!path_data) {
    fop_rmdir_cbk (frame,
		   NULL,
		   frame->this,
		   -1,
		   EINVAL);
    return -1;
  }
  
  STACK_WIND (frame, 
	      fop_rmdir_cbk, 
	      bound_xl,
	      bound_xl->fops->rmdir,
	      data_to_str (path_data));
  
  return 0;
}

//chown
static int32_t
fop_chown_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  char *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));

  fop_reply (frame,
	     GF_FOP_CHOWN,
	     dict);
  free (stat_buf);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_chown (call_frame_t *frame,
	   xlator_t *bound_xl,
	   dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *uid_data = dict_get (params, "UID");
  data_t *gid_data = dict_get (params, "GID");

  if (!path_data || !uid_data & !gid_data) {
    struct stat buf = {0, };
    fop_chown_cbk (frame,
		   NULL,
		   frame->this,
		   -1,
		   EINVAL,
		   &buf);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_chown_cbk, 
	      bound_xl,
	      bound_xl->fops->chown,
	      data_to_str (path_data),
	      data_to_int64 (uid_data),
	      data_to_int64 (gid_data));

  return 0;
}

//chmod
static int32_t
fop_chmod_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  char *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));

  fop_reply (frame,
	     GF_FOP_CHMOD,
	     dict);
  free (stat_buf);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_chmod (call_frame_t *frame,
	   xlator_t *bound_xl,
	   dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *mode_data = dict_get (params, "MODE");

  if (!path_data || !mode_data) {
    struct stat buf = {0, };
    fop_chmod_cbk (frame,
		   NULL,
		   frame->this,
		   -1,
		   EINVAL,
		   &buf);
    return -1;
  }

  STACK_WIND (frame, 
	      fop_chmod_cbk, 
	      bound_xl,
	      bound_xl->fops->chmod,
	      data_to_str (path_data),
	      data_to_int64 (mode_data));

  return 0;
}

//utimes
static int32_t
fop_utimes_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  char *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));

  fop_reply (frame,
	     GF_FOP_UTIMES,
	     dict);
  free (stat_buf);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_utimes (call_frame_t *frame,
	    xlator_t *bound_xl,
	    dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *atime_sec_data = dict_get (params, "ACTIME_SEC");
  data_t *mtime_sec_data = dict_get (params, "MODTIME_SEC");
  data_t *atime_nsec_data = dict_get (params, "ACTIME_NSEC");
  data_t *mtime_nsec_data = dict_get (params, "MODTIME_NSEC");

  if (!path_data || !atime_sec_data || !mtime_sec_data) {
    struct stat buf = {0, };
    fop_utimes_cbk (frame,
		    NULL,
		    frame->this,
		    -1,
		    EINVAL,
		    &buf);
    return -1;
  }

  struct timespec buf[2];
  buf[0].tv_sec  = data_to_int64 (atime_sec_data);
  buf[0].tv_nsec = data_to_int64 (atime_nsec_data);
  buf[1].tv_sec  = data_to_int64 (mtime_sec_data);
  buf[1].tv_nsec = data_to_int64 (mtime_nsec_data);

  STACK_WIND (frame, 
	      fop_utimes_cbk, 
	      bound_xl,
	      bound_xl->fops->utimes,
	      data_to_str (path_data),
	      buf);

  return 0;
}

//access
static int32_t
fop_access_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  fop_reply (frame,
	     GF_FOP_ACCESS,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_access (call_frame_t *frame,
	    xlator_t *bound_xl,
	    dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *mode_data = dict_get (params, "MODE");

  if (!path_data || !mode_data) {
    fop_access_cbk (frame,
		    NULL,
		    frame->this,
		    -1,
		    EINVAL);
    return -1;
  }
  
  STACK_WIND (frame, 
	      fop_access_cbk, 
	      bound_xl,
	      bound_xl->fops->access,
	      data_to_str (path_data),
	      data_to_int64 (mode_data));
  
  return 0;
}

//lk
static int32_t
fop_lk_cbk (call_frame_t *frame,
	    void *cookie,
	    xlator_t *this,
	    int32_t op_ret,
	    int32_t op_errno,
	    struct flock *lock)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "TYPE", int_to_data (lock->l_type));
  dict_set (dict, "WHENCE", int_to_data (lock->l_whence));
  dict_set (dict, "START", int_to_data (lock->l_start));
  dict_set (dict, "LEN", int_to_data (lock->l_len));
  dict_set (dict, "PID", int_to_data (lock->l_pid));

  fop_reply (frame,
	     GF_FOP_LK,
	     dict);

  dict_destroy (dict);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
fop_lk (call_frame_t *frame,
	xlator_t *bound_xl,
	dict_t *params)
{
  data_t *fd_data = dict_get (params, "FD");
  data_t *cmd_data = dict_get (params, "CMD");
  data_t *type_data = dict_get (params, "TYPE");
  data_t *whence_data = dict_get (params, "WHENCE");
  data_t *start_data = dict_get (params, "START");
  data_t *len_data = dict_get (params, "LEN");
  data_t *pid_data = dict_get (params, "PID");
  struct flock lock = {0, };
  int32_t cmd;

  if (!fd_data ||
      !cmd_data ||
      !type_data ||
      !whence_data ||
      !start_data ||
      !len_data ||
      !pid_data) {

    fop_lk_cbk (frame,
		NULL,
		frame->this,
		-1,
		EINVAL,
		&lock);
    return -1;
  }
  
  cmd =  data_to_int32 (cmd_data);
  lock.l_type =  data_to_int16 (type_data);
  lock.l_whence =  data_to_int16 (whence_data);
  lock.l_start =  data_to_int64 (start_data);
  lock.l_len =  data_to_int64 (len_data);
  lock.l_pid =  data_to_int32 (pid_data);


  STACK_WIND (frame, 
	      fop_lk_cbk, 
	      bound_xl,
	      bound_xl->fops->lk,
	      (dict_t *)(data_to_ptr (fd_data)),
	      cmd,
	      &lock);
  
  return 0;
}

/* Management Calls */
int32_t 
mop_getspec (call_frame_t *frame,
	     xlator_t *bound_xl,
	     dict_t *params)
{
  int32_t ret = -1;
  int32_t spec_fd = -1;
  
  void *file_data = NULL;
  int32_t file_data_len = 0;
  dict_t *dict = get_new_dict ();
  char *filename = GLUSTERFSD_SPEC_PATH;
  struct stat *stbuf = alloca (sizeof (struct stat));

  if (dict_get (frame->this->options,
		"client-volume-filename")) {
    filename = data_to_str (dict_get (frame->this->options,
				      "client-volume-filename"));
  }
  ret = open (filename, O_RDONLY);
  spec_fd = ret;
  if (spec_fd < 0){
    gf_log ("protocol/server",
	    GF_LOG_DEBUG,
	    "Unable to open %s (%s)",
	    filename,
	    strerror (errno));
    goto fail;
  }
  
  /* to allocate the proper buffer to hold the file data */
  {
    ret = stat (filename, stbuf);
    if (ret < 0){
      goto fail;
    }
    
    file_data_len = stbuf->st_size;
    file_data = calloc (1, file_data_len);
  }
  
  gf_full_read (spec_fd, file_data, file_data_len);
  dict_set (dict, "spec-file-data",
	    bin_to_data (file_data, stbuf->st_size));
 
 fail:
    
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  mop_reply (frame, GF_MOP_GETSPEC, dict);

  dict_destroy (dict);
  if (file_data)
    free (file_data);
  STACK_DESTROY (frame->root);

  return ret;
}

int32_t 
mop_setspec (call_frame_t *frame,
	     xlator_t *bound_xl,
	     dict_t *params)
{
  int32_t ret = -1;
  int32_t spec_fd = -1;
  int32_t remote_errno = 0;

  dict_t *dict = get_new_dict ();

  data_t *data = dict_get (params, "spec-file-data");
  if (!data) {
    goto fail;
  }
  void *file_data = data_to_bin (data);
  int32_t file_data_len = data->len;
  int32_t offset = 0;

  ret = mkdir (GLUSTERFSD_SPEC_DIR, 0x777);
  
  if (ret < 0 && errno != EEXIST){
    remote_errno = errno;
    goto fail;
  }
  
  ret = open (GLUSTERFSD_SPEC_PATH, O_WRONLY | O_CREAT | O_SYNC);
  spec_fd = ret;
  if (spec_fd < 0){
    remote_errno = errno;
    goto fail;
  }

  while ((ret = write (spec_fd, file_data + offset, file_data_len))){
    if (ret < 0){
      remote_errno = errno;
      goto fail;
    }
    
    if (ret < file_data_len){
      offset = ret + 1;
      file_data_len = file_data_len - ret;
    }
  }
      
 fail:
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (remote_errno));

  mop_reply (frame, GF_MOP_GETSPEC, dict);

  dict_destroy (dict);
  
  return ret;
}


static int32_t
mop_lock_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
  dict_t *params = get_new_dict ();

  dict_set (params, "RET", int_to_data (op_ret));
  dict_set (params, "ERRNO", int_to_data (op_errno));

  mop_reply (frame, GF_MOP_LOCK, params);

  STACK_DESTROY (frame->root);
  dict_destroy (params);
  return 0;
}

int32_t 
mop_lock (call_frame_t *frame,
	  xlator_t *bound_xl,
	  dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  char *path;
  
  path_data = dict_get (params, "PATH");

  if (!path_data) {
    mop_lock_cbk (frame,
		  NULL,
		  frame->this,
		  -1,
		  EINVAL);
    return -1;
  }

  path = data_to_str (path_data);

  STACK_WIND (frame,
	      mop_lock_cbk,
	      frame->this,
	      frame->this->mops->lock,
	      path);

  return 0;
}


static int32_t
mop_unlock_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  dict_t *params = get_new_dict ();

  dict_set (params, "RET", int_to_data (op_ret));
  dict_set (params, "ERRNO", int_to_data (op_errno));

  mop_reply (frame, GF_MOP_UNLOCK, params);

  dict_destroy (params);
  STACK_DESTROY (frame->root);
  return 0;
}

int32_t 
mop_unlock (call_frame_t *frame,
	    xlator_t *bound_xl,
	    dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  char *path;
  
  path_data = dict_get (params, "PATH");

  if (!path_data) {
    mop_unlock_cbk (frame,
		    NULL,
		    frame->this,
		    -1,
		    EINVAL);
    return -1;
  }

  path = data_to_str (path_data);

  STACK_WIND (frame,
	      mop_unlock_cbk,
	      frame->this,
	      frame->this->mops->unlock,
	      path);

  return 0;
}

int32_t 
mop_listlocks (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  int32_t ret = -1;
  dict_t *dict = get_new_dict ();

  /* logic to read the locks and send them to the person who requested for it */
#if 0 //I am confused about what is junk... <- bulde
  {
    int32_t junk = data_to_int32 (dict_get (params, "OP"));
    gf_log ("protocol/server",
	    GF_LOG_DEBUG, "mop_listlocks: junk is %x", junk);
    gf_log ("protocol/server",
	    GF_LOG_DEBUG, "mop_listlocks: listlocks called");
    ret = gf_listlocks ();
    
  }
#endif

  errno = 0;

  dict_set (dict, "RET_OP", int_to_data (0xbabecafe));
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  mop_reply (frame, GF_MOP_LISTLOCKS, dict);
  dict_destroy (dict);
  
  STACK_DESTROY (frame->root);
  return 0;
}


int32_t 
mop_nslookup (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  int32_t ret = -1;
  int32_t remote_errno = -ENOENT;
  dict_t *dict = get_new_dict ();

  data_t *path_data = dict_get (params, "PATH");

  if (!path_data) {
    remote_errno = EINVAL;
    goto fail;
  }
  char *path = data_to_str (path_data);
  char *ns = ns_lookup (path);
  
  ns = ns ? (ret = 0, remote_errno = 0, (char *)ns) : "";

  dict_set (dict, "NS", str_to_data (ns));

 fail:
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (remote_errno));

  mop_reply (frame, GF_MOP_NSLOOKUP, dict);
  dict_destroy (dict);
  
  STACK_DESTROY (frame->root);
  return 0;
}

int32_t 
mop_nsupdate (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  int ret = -1;
  int op_errno = 0;
  dict_t *dict = get_new_dict ();

  data_t *path_data = dict_get (params, "PATH");
  data_t *ns_data = dict_get (params, "NS");
  if (!path_data && !ns_data) {
    op_errno = EINVAL;
    goto fail;
  }
  
  char *path = data_to_str (path_data);
  ret = ns_update (path, data_to_str (ns_data));

 fail:
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  mop_reply (frame, GF_MOP_NSUPDATE, dict);
  dict_destroy (dict);
  
  STACK_DESTROY (frame->root);
  return 0;
}


int32_t 
mop_getvolume (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  return 0;
}


static xlator_t *
get_xlator_by_name (xlator_t *some_xl,
		    const char *name)
{
  auto void check_and_set (xlator_t *, void *);

  struct get_xl_struct {
    const char *name;
    xlator_t *reply;
  } get = {
    .name = name,
    .reply = NULL
  };

  void check_and_set (xlator_t *each,
		      void *data)
    {
      if (!strcmp (each->name,
		   ((struct get_xl_struct *) data)->name))
	((struct get_xl_struct *) data)->reply = each;
    }
      
  xlator_foreach (some_xl, check_and_set, &get);

  return get.reply;
}

int32_t 
mop_setvolume (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  int32_t ret = -1;
  int32_t remote_errno = 0;
  dict_t *dict = get_new_dict ();
  struct proto_srv_priv *priv;
  data_t *name_data;
  char *name;
  xlator_t *xl;

  priv = ((transport_t *)frame->root->state)->xl_private;

  name_data = dict_get (params,
			"remote-subvolume");
  if (!name_data) {
    remote_errno = EINVAL;
    dict_set (dict, "ERROR", str_to_data ("No remote-subvolume option specified"));
    goto fail;
  }

  name = data_to_str (name_data);
  xl = get_xlator_by_name (frame->this,
			   name);

  if (!xl) {
    char msg[256] = {0,};
    sprintf (msg, "remote-subvolume \"%s\" is not found", name);
    dict_set (dict, "ERROR", str_to_data (msg));
    remote_errno = ENOENT;
    goto fail;
  } else {
    char *searchstr;
    struct sockaddr_in *_sock = &((transport_t *)frame->root->state)->peerinfo.sockaddr;
    
    asprintf (&searchstr, "auth.ip.%s.allow", xl->name);
    data_t *allow_ip = dict_get (frame->this->options,
				 searchstr);
    
    free (searchstr);
    
    if (allow_ip) {
      
      gf_log ("server-protocol",
	      GF_LOG_DEBUG,
	      "mop_setvolume: received port = %d",
	      ntohs (_sock->sin_port));
      
      if (ntohs (_sock->sin_port) < 1024) {
	char *ip_addr_str = NULL;
	char *tmp;
	char *ip_addr_cpy = strdup (allow_ip->data);

	ip_addr_str = strtok_r (ip_addr_cpy,
				",",
				&tmp);

	while (ip_addr_str) {
	  gf_log ("server-protocol",
		  GF_LOG_DEBUG,
		  "mop_setvolume: IP addr = %s, received ip addr = %s", 
		  ip_addr_str, 
		  inet_ntoa (_sock->sin_addr));
	  if (fnmatch (ip_addr_str,
		       inet_ntoa (_sock->sin_addr),
		       0) == 0) {
	    ret = 0;
	    priv->bound_xl = xl; 

	    gf_log ("server-protocol",
		    GF_LOG_DEBUG,
		    "mop_setvolume: accepted client from %s",
		     inet_ntoa (_sock->sin_addr));

	    dict_set (dict, "ERROR", str_to_data ("Success"));
	    break;
	  }
	  ip_addr_str = strtok_r (NULL,
				  ",",
				  &tmp);
	}
	if (ret != 0) {
	  dict_set (dict, "ERROR", 
		    str_to_data ("Authentication Failed: IP address not allowed"));
	}
	free (ip_addr_cpy);
	goto fail;
      } else {
	dict_set (dict, "ERROR", 
		  str_to_data ("Authentication Range not specified in volume spec"));
	goto fail;
      }
    } else {
      char msg[256] = {0,};
      sprintf (msg, 
	       "Volume \"%s\" is not attachable from host %s", 
	       xl->name, 
	       inet_ntoa (_sock->sin_addr));
      dict_set (dict, "ERROR", str_to_data (msg));
      goto fail;
    }
    if (!priv->bound_xl) {
      dict_set (dict, "ERROR", 
		str_to_data ("Check volume spec file and handshake options"));
      ret = -1;
      remote_errno = EACCES;
      goto fail;
    }
  }
  
 fail:
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (remote_errno));

  mop_reply (frame, GF_MOP_SETVOLUME, dict);
  dict_destroy (dict);

  STACK_DESTROY (frame->root);
  return ret;
}


int32_t 
mop_stats_cbk (call_frame_t *frame, 
	       void *cookie,
	       xlator_t *xl, 
	       int32_t ret, 
	       int32_t op_errno, 
	       struct xlator_stats *stats)
{
  /* TODO: get this information from somewhere else, not extern */
  int32_t glusterfsd_stats_nr_clients = 0;

  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  if (ret == 0) {
    char buffer[256] = {0,};
    sprintf (buffer, "%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64"\n",
	     (int64_t)stats->nr_files,
	     (int64_t)stats->disk_usage,
	     (int64_t)stats->free_disk,
	     (int64_t)stats->read_usage,
	     (int64_t)stats->write_usage,
	     (int64_t)stats->disk_speed,
	     (int64_t)glusterfsd_stats_nr_clients);
    dict_set (dict, "BUF", str_to_data (buffer));
  }

  mop_reply (frame, GF_MOP_STATS, dict);

  dict_destroy (dict);

  STACK_DESTROY (frame->root);

  return 0;
}


static int32_t
mop_stats (call_frame_t *frame,
	   xlator_t *bound_xl,
	   dict_t *params)
{
  data_t *flag_data = dict_get (params, "FLAGS");

  if (!flag_data) {
    mop_stats_cbk (frame,
		   NULL,
		   frame->this,
		   -1,
		   EINVAL,
		   NULL);
    return -1;
  }
  
  STACK_WIND (frame, 
	      mop_stats_cbk, 
	      bound_xl,
	      bound_xl->mops->stats,
	      data_to_int64 (flag_data));
  
  return 0;
}


int32_t 
mop_fsck_cbk (call_frame_t *frame, 
	      void *cookie,
	      xlator_t *xl, 
	      int32_t ret, 
	      int32_t op_errno)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  mop_reply (frame, GF_MOP_FSCK, dict);
  
  dict_destroy (dict);
  return 0;
}


int32_t 
mop_fsck (call_frame_t *frame,
	  xlator_t *bound_xl,
	  dict_t *params)
{
  data_t *flag_data = dict_get (params, "FLAGS");

  if (!flag_data) {
    mop_fsck_cbk (frame,
		  NULL,
		  frame->this,
		  -1,
		  EINVAL);
    return -1;
  }
  
  STACK_WIND (frame, 
	      mop_fsck_cbk, 
	      bound_xl,
	      bound_xl->mops->fsck,
	      data_to_int64 (flag_data));
  
  return 0;
}

/* This function is called when a opcode for unknown type is called 
   Helps to keep the backward/forward compatiblity */

int32_t 
unknown_op_cbk (call_frame_t *frame, 
		int32_t type,
		int32_t opcode)
{
  dict_t *dict = get_new_dict ();
  
  dict_set (dict, "RET", int_to_data (-1));
  dict_set (dict, "ERRNO", int_to_data (ENOSYS));
  
  if (type == GF_OP_TYPE_MOP_REQUEST)
    mop_reply (frame, opcode, dict);
  else 
    fop_reply (frame, opcode, dict);
  
  dict_destroy (dict);
  return 0;
}	     
/* 
   create a frame into the call_ctx_t capable of generating 
   and replying the reply packet by itself.
   By making a call with this frame, the last UNWIND function
   will have all needed state from its frame_t->root to
   send reply
*/

static call_frame_t *
get_frame_for_call (transport_t *trans,
		    gf_block_t *blk,
		    dict_t *params)
{
  call_ctx_t *_call = (void *) calloc (1, sizeof (*_call));
  data_t *d;

  _call->state = transport_ref (trans);        /* which socket */
  _call->unique = blk->callid; /* which call */

  _call->frames.root = _call;
  _call->frames.this = trans->xl;

  d = dict_get (params, "CALLER_UID");
  if (d)
    _call->uid = (uid_t) data_to_int64 (d);
  d = dict_get (params, "CALLER_GID");
  if (d)
    _call->gid = (gid_t) data_to_int64 (d);
  d = dict_get (params, "CALLER_PID");
  if (d)
    _call->pid = (gid_t) data_to_int64 (d);

  return &_call->frames;
}

typedef int32_t (*gf_op_t) (call_frame_t *frame,
			    xlator_t *bould_xl,
			    dict_t *params);

static gf_op_t gf_fops[] = {
  fop_getattr,
  fop_readlink,
  fop_mknod,
  fop_mkdir,
  fop_unlink,
  fop_rmdir,
  fop_symlink,
  fop_rename,
  fop_link,
  fop_chmod,
  fop_chown,
  fop_truncate,
  fop_utimes,
  fop_open,
  fop_readv,
  fop_writev,
  fop_statfs,
  fop_flush,
  fop_release,
  fop_fsync,
  fop_setxattr,
  fop_getxattr,
  fop_listxattr,
  fop_removexattr,
  fop_opendir,
  fop_readdir,
  fop_releasedir,
  fop_fsyncdir,
  fop_access,
  fop_create,
  fop_ftruncate,
  fop_fgetattr,
  fop_lk
};

static gf_op_t gf_mops[] = {
  mop_setvolume,
  mop_getvolume,
  mop_stats,
  mop_setspec,
  mop_getspec,
  mop_lock,
  mop_unlock,
  mop_listlocks,
  mop_nslookup,
  mop_nsupdate,
  mop_fsck
};

static int32_t 
proto_srv_interpret (transport_t *trans,
		     gf_block_t *blk)
{
  int32_t ret = 0;
  dict_t *params = blk->dict;
  struct proto_srv_priv *priv = trans->xl_private;
  xlator_t *bound_xl = priv->bound_xl; /* the xlator to STACK_WIND into */
  call_frame_t *frame = NULL;
  
  switch (blk->type) {
  case GF_OP_TYPE_FOP_REQUEST:

    /* drop connection for unauthorized fs access */
    if (!bound_xl) {
      ret = -1;
      break;
    }

    if (blk->op < 0) {
      ret = -1;
      break;
    }

    /*
      gf_log ("protocol/server",
      GF_LOG_DEBUG,
      "opcode = 0x%x",
      blk->op);
    */
    frame = get_frame_for_call (trans, blk, params);
    frame->root->req_refs = dict_ref (params);
    dict_set (params, NULL, trans->buf);

    if (blk->op > GF_FOP_MAXVALUE) {
      unknown_op_cbk (frame, GF_OP_TYPE_FOP_REQUEST, blk->op);
      break;
    }
    
    ret = gf_fops[blk->op] (frame, bound_xl, params);
    break;
    
  case GF_OP_TYPE_MOP_REQUEST:
    
    if (blk->op < 0) {
      ret = -1;
      break;
    }
    
    frame = get_frame_for_call (trans, blk, params);
    frame->root->req_refs = dict_ref (params);
    dict_set (params, NULL, trans->buf);

    if (blk->op > GF_MOP_MAXVALUE) {
      unknown_op_cbk (frame, GF_OP_TYPE_MOP_REQUEST, blk->op);
      break;
    }

    gf_mops[blk->op] (frame, bound_xl, params);

    break;
  default:
    gf_log ("protocol/server",
	    GF_LOG_DEBUG,
	    "Unknown packet type: %d", blk->type);
    ret = -1;
  }

  dict_unref (params);
  return ret;  
}

static int32_t
nop_cbk (call_frame_t *frame,
	 xlator_t *this,
	 int32_t op_ret,
	 int32_t op_errno)
{
  STACK_DESTROY (frame->root);
  return 0;
}

static call_frame_t *
get_frame_for_transport (transport_t *trans)
{
  call_ctx_t *_call = (void *) calloc (1, sizeof (*_call));

  _call->state = trans;        /* which socket */
  _call->unique = 0;           /* which call */

  _call->frames.root = _call;
  _call->frames.this = trans->xl;

  return &_call->frames;
}

static void
open_file_cleanup_fn (dict_t *this,
		      char *key,
		      data_t *value,
		      void *data)
{
  dict_t *file_ctx;
  transport_t *trans = data;
  struct proto_srv_priv *priv = trans->xl_private;
  xlator_t *bound_xl = priv->bound_xl;
  call_frame_t *frame;

  file_ctx = (dict_t *) strtoul (key, NULL, 0);
  frame = get_frame_for_transport (trans);

  gf_log ("protocol/server",
	  GF_LOG_DEBUG,
	  "force releaseing file %p", file_ctx);

  STACK_WIND (frame,
	      nop_cbk,
	      bound_xl,
	      bound_xl->fops->release,
	      file_ctx);
  return;
}

static int32_t
proto_srv_cleanup (transport_t *trans)
{
  struct proto_srv_priv *priv = trans->xl_private;
  call_frame_t *frame;

  priv->disconnected = 1;
  if (priv->open_files) {
    dict_foreach (priv->open_files,
		  open_file_cleanup_fn,
		  trans);
    dict_destroy (priv->open_files);
    priv->open_files = NULL;
  }

  /* ->unlock () with NULL path will cleanup
     lock manager's internals by remove all
     entries related to this transport
  */

  frame = get_frame_for_transport (trans);

  STACK_WIND (frame,
	      nop_cbk,
	      trans->xl,
	      trans->xl->mops->unlock,
	      NULL);

  gf_log ("protocol/server",
	  GF_LOG_DEBUG,
	  "cleaned up xl_private of %p",
	  trans);
  free (priv);
  trans->xl_private = NULL;
  return 0;
}


static int32_t
proto_srv_notify (xlator_t *this,
		  transport_t *trans,
		  int32_t event)
{
  int ret = 0;

  struct proto_srv_priv *priv = trans->xl_private;

  if (!priv) {
    priv = (void *) calloc (1, sizeof (*priv));
    trans->xl_private = priv;
    priv->open_files = get_new_dict ();
  }

  if (event & (POLLIN|POLLPRI)) {
    gf_block_t *blk;

    blk = gf_block_unserialize_transport (trans);
    if (!blk) {
      ret = -1;
    }

    if (!ret) {
      ret = proto_srv_interpret (trans, blk);

      free (blk);
    }
  }

  if (ret || (event & (POLLERR|POLLHUP))) {
    proto_srv_cleanup (trans);
  }

  return ret;
}

int32_t
init (xlator_t *this)
{
  transport_t *trans;

  gf_log ("protocol/server",
	  GF_LOG_DEBUG,
	  "protocol/server xlator loaded");

  if (!this->children) {
    gf_log ("protocol/server",
	    GF_LOG_ERROR,
	    "FATAL: protocol/server should have subvolume");
    return -1;
  }
  trans = transport_load (this->options,
			  this,
			  proto_srv_notify);

  this->private = trans;
  //  ((struct proto_srv_priv *)(trans->xl_private))->bound_xl = FIRST_CHILD (this);

  return 0;
}

void
fini (xlator_t *this)
{

  return;
}

struct xlator_mops mops = {
  .lock = mop_lock_impl,
  .unlock = mop_unlock_impl
};

struct xlator_fops fops = {

};
