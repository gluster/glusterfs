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
#include "server-protocol.h"
#include <time.h>
#include <sys/uio.h>
#include "call-stub.h"

#if __WORDSIZE == 64
# define F_L64 "%l"
#else
# define F_L64 "%ll"
#endif

/* 
 * stat_to_str - convert struct stat to a ASCII string
 * @stbuf: struct stat pointer
 *
 * not for external reference
 */
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

/*
 * generic_reply - generic reply, used to send reply packet to client
 * @frame: call frame
 * @type: reply type GF_MOP_REPLY/GF_FOP_REPLY
 * @op: operation to which this reply corresponds to
 * @params: parameter dictionary, actual data of the reply packet
 *
 * not for external reference
 */
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

/*
 * server_fop_reply - called by fop callbacks to send reply to clients
 * @frame: call frame
 * @op: operation
 * @reply: reply data dictionary
 *
 * not for external reference
 */
static int32_t
server_fop_reply (call_frame_t *frame,
		  glusterfs_fop_t op,
		  dict_t *reply)
{
  return generic_reply (frame,
			GF_OP_TYPE_FOP_REPLY,
			op,
			reply);
}

/* 
 * server_mop_reply - mop reply function for server protocol.
 * @frame: call frame
 * @op: operation
 * @reply: reply dictionary
 *
 * not for external reference
 */
static int32_t
server_mop_reply (call_frame_t *frame,
	   glusterfs_fop_t op,
	   dict_t *reply)
{
  return generic_reply (frame,
			GF_OP_TYPE_MOP_REPLY,
			op,
			reply);
}

/*
 * server_lookup_cbk - lookup callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @inode:
 * @stbuf:
 *
 * not for external reference
 */
static int32_t
server_lookup_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *stbuf)
{
  if (frame->local) {
    /* we have a call stub to wind to */
    call_stub_t *stub = frame->local;
    call_resume (stub);

  } else {
    /* we are truely a lookup callback */
    dict_t *reply = get_new_dict ();
    dict_set (reply, "RET", data_from_int32 (op_ret));
    dict_set (reply, "ERRNO", data_from_int32 (op_errno));
    if (stbuf) {
      dict_set (reply, "STAT", data_from_str (stat_to_str (stbuf)));
    }
    
    server_fop_reply (frame,
		      GF_FOP_LOOKUP,
		      reply);
    
    dict_destroy (reply);
  }
  return 0;
}

/*
 * server_lookup - lookup function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
static int32_t
server_lookup (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  
  if (!path_data) {
    server_lookup_cbk (frame,
		       NULL,
		       frame->this,
		       -1,
		       EINVAL,
		       NULL,
		       NULL);
    return 0;
  }
		       
  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  
  STACK_WIND (frame,
	      server_lookup_cbk,
	      bound_xl,
	      bound_xl->fops->lookup,
	      loc);

  free (loc->path);
  free (loc);
	      
  return 0;
}


/*
 * server_forget_cbk - forget callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 * not for external reference
 */
static int32_t
server_forget_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  server_fop_reply (frame,
		    GF_FOP_FORGET,
		    reply);

  dict_destroy (reply);
  return 0;
}

/*
 * server_forget - forget function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
static int32_t
server_forget (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  data_t *inode_data = dict_get (params, "INODE");
  
  if (!inode_data) {
    server_forget_cbk (frame,
		       NULL,
		       bound_xl,
		       -1,
		       EINVAL);
    return 0;
  }

  inode_t *inode = inode_update (table, NULL, NULL, 
				 data_to_uint64 (inode_data));
  if (!inode) {
    /* we have already forgot inode */
    return 0;
  }

  STACK_WIND (frame,
	      server_forget_cbk,
	      bound_xl,
	      bound_xl->fops->forget,
	      inode);
	      
  return 0;
}

/* 
 * server_stat_cbk - stat callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 * not for external reference
 */
static int32_t
server_stat_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  char *stat_buf = stat_to_str (stbuf);
  dict_set (reply, "STAT", str_to_data (stat_buf));

  server_fop_reply (frame,
		    GF_FOP_STAT,
		    reply);

  free (stat_buf);
  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_stat - stat function for server
 * @frame: call frame
 * @bound_xl: translator this server is bound to
 * @params: parameters dictionary
 *
 * not for external reference
 */

static int32_t
server_stat (call_frame_t *frame,
	     xlator_t *bound_xl,
	     dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  struct stat buf = {0, };

  if (!path_data || !inode_data) {
    server_stat_cbk (frame,
		     NULL,
		     frame->this,
		     -1,
		     EINVAL,
		     &buf);
    return -1;
  }

  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *stat_stub = fop_stat_stub (frame, 
					    bound_xl->fops->stat,
					    loc);
    frame->local = stat_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {
    STACK_WIND (frame, 
		server_stat_cbk, 
		bound_xl,
		bound_xl->fops->stat,
		loc);
  }
  return 0;
}

/*
 * server_readlink_cbk - readlink callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret:
 * @op_errno:
 * @buf:
 *
 * not for external reference
 */
static int32_t
server_readlink_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     char *buf)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  dict_set (reply, "BUF", str_to_data (buf ? (char *) buf : "" ));

  server_fop_reply (frame,
		GF_FOP_READLINK,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}


/*
 * server_readlink - readlink function for server
 * @frame: call frame
 * @bound_xl: translator this server is bound to
 * @params: parameters dictionary
 *
 * not for external reference
 */

static int32_t
server_readlink (call_frame_t *frame,
		 xlator_t *bound_xl,
		 dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *len_data = dict_get (params, "LEN");
  
  int32_t len = data_to_int32 (len_data);

  if (!path_data || !len_data) {
    server_readlink_cbk (frame,
			 NULL,
			 frame->this,
			 -1,
			 EINVAL,
			 "");
    return -1;
  }

  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *readlink_stub = fop_readlink_stub (frame, 
						    bound_xl->fops->statfs,
						    loc,
						    len);
    frame->local = readlink_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {

    STACK_WIND (frame,
		server_readlink_cbk,
		bound_xl,
		bound_xl->fops->readlink,
		loc,
		(size_t) len);
  }

  return 0;
}


/*
 * server_create_cbk - create callback for server
 * @frame: call frame
 * @cookie:
 * @this:  translator structure
 * @op_ret: 
 * @op_errno:
 * @fd: file descriptor
 * @inode: inode structure
 * @stbuf: struct stat of created file
 *
 * not for external reference
 */
static int32_t
server_create_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd,
		   inode_t *inode,
		   struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();
  dict_t *ctx = fd->ctx;

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  dict_set (reply, "FD", data_from_ptr (fd));
  
  char *stat_buf = stat_to_str (stbuf);
  dict_set (reply, "STAT", str_to_data (stat_buf));

  if (op_ret >= 0) {
    server_proto_priv_t *priv = ((transport_t *)frame->root->state)->xl_private;
    char ctx_buf[32] = {0,};
    sprintf (ctx_buf, "%p", fd);
    dict_set (priv->open_files, ctx_buf, str_to_data (""));
  }
  
  server_fop_reply (frame,
		GF_FOP_CREATE,
		reply);
  
  free (stat_buf);
  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_create - create function for server
 * @frame: call frame
 * @bound_xl: translator this server is bound to
 * @params: parameters dictionary
 *
 * not for external reference
 */
static int32_t
server_create (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  int32_t flags = 0;
  data_t *path_data = dict_get (params, "PATH");
  data_t *mode_data = dict_get (params, "MODE");
  data_t *flag_data = dict_get (params, "FLAGS");

  if (!path_data || !mode_data) {
    struct stat buf = {0, };
    server_create_cbk (frame,
		       NULL,
		       frame->this,
		       -1,
		       EINVAL,
		       NULL,
		       NULL,
		       &buf);
    return -1;
  }

  if (flag_data) {
    flags = data_to_int32 (flag_data);
  }

  STACK_WIND (frame, 
	      server_create_cbk, 
	      bound_xl,
	      bound_xl->fops->create,
	      data_to_str (path_data),
	      flags,
	      data_to_int64 (mode_data));
  
  return 0;
}

/* 
 * server_open_cbk - open callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @fd:
 * @stbuf:
 *
 * not for external reference
 */ 
static int32_t
server_open_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 fd_t *fd,
		 struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  dict_set (reply, "FD", data_from_ptr (fd));
  
  char *stat_buf = stat_to_str (stbuf);
  dict_set (reply, "STAT", str_to_data (stat_buf));

  if (op_ret >= 0) {
    server_proto_priv_t *priv = ((transport_t *)frame->root->state)->xl_private;
    char ctx_buf[32] = {0,};
    sprintf (ctx_buf, "%p", fd);
    dict_set (priv->open_files, ctx_buf, str_to_data (""));
  }
  
  server_fop_reply (frame,
		GF_FOP_OPEN,
		reply);

  free (stat_buf);
  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_open - open function for server protocol
 * @frame: call frame
 * @bound_xl: translator this server protocol is bound to
 * @params: parameters dictionary
 *
 * not for external reference
 */
static int32_t
server_open (call_frame_t *frame,
	     xlator_t *bound_xl,
	     dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *flag_data = dict_get (params, "FLAGS");
  uint64_t flags = data_to_uint64 (flag_data);
  
  if (!path_data || !inode_data || !flag_data) {
    struct stat buf = {0, };
    server_open_cbk (frame,
		     NULL,
		     frame->this,
		     -1,
		     EINVAL,
		     NULL,
		     &buf);
    return -1;
  }

  loc_t *loc = calloc (1, sizeof (loc_t));
  char *path = data_to_str (path_data);
  loc->path = strdup (path);
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);
  
  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *open_stub = fop_open_stub (frame, 
					    bound_xl->fops->open,
					    loc,
					    flags);
    frame->local = open_stub;

    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
		
  } else {
    /* we are fine with everything, go ahead with open of our child */
    STACK_WIND (frame, 
		server_open_cbk, 
		bound_xl,
		bound_xl->fops->open,
		loc,
		flags); 
  }

  return 0;
}

/*
 * server_readv_cbk - readv callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @vector:
 * @count:
 *
 * not for external reference
 */
static int32_t
server_readv_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct iovec *vector,
		  int32_t count)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  if (op_ret >= 0)
    dict_set (reply, "BUF", data_from_iovec (vector, count));
  else
    dict_set (reply, "BUF", str_to_data (""));

  server_fop_reply (frame,
		GF_FOP_READ,
		reply);
  
  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_readv - readv function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
static int32_t
server_readv (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *fd_data = dict_get (params, "FD");
  data_t *len_data = dict_get (params, "LEN");
  data_t *off_data = dict_get (params, "OFFSET");
  
  if (!fd_data || !len_data || !off_data) {
    struct iovec vec;
    vec.iov_base = strdup ("");
    vec.iov_len = 0;
    server_readv_cbk (frame,
		   NULL,
		   frame->this,
		   -1,
		   EINVAL,
		   &vec,
		   0);
    return -1;
  }
  
  STACK_WIND (frame, 
	      server_readv_cbk,
	      bound_xl,
	      bound_xl->fops->readv,
	      data_to_ptr (fd_data),
	      data_to_int32 (len_data),
	      data_to_int64 (off_data));
  
  return 0;
}

/* 
 * server_writev_cbk - writev callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 * not for external reference
 */
static int32_t
server_writev_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  
  server_fop_reply (frame,
		GF_FOP_WRITE,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_writev - writev function for server
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference 
 */
static int32_t
server_writev (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  data_t *fd_data = dict_get (params, "FD");
  data_t *len_data = dict_get (params, "LEN");
  data_t *off_data = dict_get (params, "OFFSET");
  data_t *buf_data = dict_get (params, "BUF");
  struct iovec iov;

  if (!fd_data || !len_data || !off_data || !buf_data) {
    server_writev_cbk (frame,
		    NULL,
		    frame->this,
		    -1,
		    EINVAL);
    return -1;
  }

  iov.iov_base = buf_data->data;
  iov.iov_len = data_to_int32 (len_data);

  STACK_WIND (frame, 
	      server_writev_cbk, 
	      bound_xl,
	      bound_xl->fops->writev,
	      (fd_t *)data_to_ptr (fd_data),
	      &iov,
	      1,
	      data_to_int64 (off_data));

  return 0;
}

/*
 * server_close_cbk - close callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 * not for external reference
 */
static int32_t
server_close_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  
  server_fop_reply (frame,
		GF_FOP_CLOSE,
		reply);
  
  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_close - close function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference 
 */
static int32_t
server_close (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *fd_data = dict_get (params, "FD");

  if (!fd_data) {
    server_close_cbk (frame,
		     NULL,
		     frame->this,
		     -1,
		     EINVAL);
    return -1;
  }
  
  {
    char str[32];
    server_proto_priv_t *priv = ((transport_t *)frame->root->state)->xl_private;
    sprintf (str, "%p", data_to_ptr (fd_data));
    dict_del (priv->open_files, str);
  }
  STACK_WIND (frame, 
	      server_close_cbk, 
	      bound_xl,
	      bound_xl->fops->close,
	      (fd_t *)data_to_ptr (fd_data));

  return 0;
}

/*
 * server_fsync_cbk - fsync callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 * not for external reference
 */
static int32_t
server_fsync_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  
  server_fop_reply (frame,
		GF_FOP_FSYNC,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_fsync - fsync function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameters dictionary
 *
 * not for external reference
 */
static int32_t
server_fsync (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *fd_data = dict_get (params, "FD");
  data_t *flag_data = dict_get (params, "FLAGS");

  if (!fd_data || !flag_data) {
    server_fsync_cbk (frame,
		      NULL,
		      frame->this,
		      -1,
		      EINVAL);
    return -1;
  }

  STACK_WIND (frame, 
	      server_fsync_cbk, 
	      bound_xl,
	      bound_xl->fops->fsync,
	      (fd_t *)data_to_ptr (fd_data),
	      data_to_int64 (flag_data));

  return 0;
}

/*
 * server_flush_cbk - flush callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret:
 * @op_errno:
 *
 * not for external reference
 */
static int32_t
server_flush_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  server_fop_reply (frame,
		GF_FOP_FLUSH,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_flush - flush function for server protocol
 * @frame: call frame
 * @bound_xl: 
 * @params: parameter dictionary
 *
 * not for external reference
 */
static int32_t
server_flush (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *fd_data = dict_get (params, "FD");

  if (!fd_data) {
    server_flush_cbk (frame,
		      NULL,
		      frame->this,
		      -1,
		      EINVAL);
    return -1;
  }

  STACK_WIND (frame, 
	      server_flush_cbk, 
	      bound_xl,
	      bound_xl->fops->flush,
	      (fd_t *)data_to_ptr (fd_data));

  return 0;
}

/*
 * server_ftruncate_cbk - ftruncate callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 * not for external reference
 */
static int32_t
server_ftruncate_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  char *stat_buf = stat_to_str (stbuf);
  dict_set (reply, "STAT", str_to_data (stat_buf));
  
  server_fop_reply (frame,
		GF_FOP_FTRUNCATE,
		reply);
  free (stat_buf);
  
  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_ftruncate - ftruncate function for server protocol
 * @frame: call frame
 * @bound_xl: 
 * @params: parameters dictionary
 *
 * not for external reference
 */
static int32_t
server_ftruncate (call_frame_t *frame,
		  xlator_t *bound_xl,
		  dict_t *params)
{
  data_t *fd_data = dict_get (params, "FD");
  data_t *off_data = dict_get (params, "OFFSET");

  if (!fd_data || !off_data) {
    struct stat buf = {0, };
    server_ftruncate_cbk (frame,
			  NULL,
			  frame->this,
			  -1,
			  EINVAL,
			  &buf);
    return -1;
  }
  
  STACK_WIND (frame, 
	      server_ftruncate_cbk, 
	      bound_xl,
	      bound_xl->fops->ftruncate,
	      (fd_t *)data_to_ptr (fd_data),
	      data_to_int64 (off_data));

  return 0;
}

/*
 * server_fstat_cbk - fstat callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 * not for external reference
 */
static int32_t
server_fstat_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  
  char *stat_buf = stat_to_str (stbuf);
  dict_set (reply, "BUF", str_to_data (stat_buf));
  
  server_fop_reply (frame,
		GF_FOP_FSTAT,
		reply);
  free (stat_buf);
  
  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_fstat - fstat function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
static int32_t
server_fstat (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *fd_data = dict_get (params, "FD");

  if (!fd_data) {
    struct stat buf = {0, };
    server_fstat_cbk (frame,
		      NULL,
		      frame->this,
		      -1,
		      EINVAL,
		      &buf);
    return -1;
  }
  
  STACK_WIND (frame, 
	      server_fstat_cbk, 
	      bound_xl,
	      bound_xl->fops->fstat,
	      (fd_t *)data_to_ptr (fd_data));
  
  return 0;
}

/*
 * server_truncate_cbk - truncate callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 * not for external reference
 */
static int32_t
server_truncate_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  
  char *stat_buf = stat_to_str (stbuf);
  dict_set (reply, "STAT", str_to_data (stat_buf));
  
  server_fop_reply (frame,
		GF_FOP_TRUNCATE,
		reply);
  free (stat_buf);
  
  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_truncate - truncate function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params:
 *
 * not for external reference
 */
static int32_t
server_truncate (call_frame_t *frame,
		 xlator_t *bound_xl,
		 dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *off_data = dict_get (params, "OFFSET");
  off_t offset = data_to_uint64 (off_data);

  if (!path_data || !off_data || !inode_data) {
    struct stat buf = {0, };
    server_truncate_cbk (frame,
			 NULL,
			 frame->this,
			 -1,
			 EINVAL,
			 &buf);
    return -1;
  }

  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *truncate_stub = fop_truncate_stub (frame, 
						    bound_xl->fops->truncate,
						    loc,
						    offset);
    frame->local = truncate_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {

    STACK_WIND (frame, 
		server_truncate_cbk, 
		bound_xl,
		bound_xl->fops->truncate,
		loc,
		offset);
  }
  return 0;
}


/*
 * server_link_cbk - link callback for server protocol
 * @frame: call frame
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 * not for external reference
 */
static int32_t
server_link_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  char *stat_buf = stat_to_str (stbuf);
  dict_set (reply, "STAT", str_to_data (stat_buf));

  server_fop_reply (frame,
		GF_FOP_LINK,
		reply);
  free (stat_buf);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_link - link function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params:
 *
 * not for external reference
 */
static int32_t
server_link (call_frame_t *frame,
	     xlator_t *bound_xl,
	     dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *buf_data = dict_get (params, "BUF");

  if (!path_data || !buf_data) {
    struct stat buf = {0, };
    server_link_cbk (frame,
		     NULL,
		     frame->this,
		     -1,
		     EINVAL,
		     &buf);
    return -1;
  }
  
  STACK_WIND (frame, 
	      server_link_cbk, 
	      bound_xl,
	      bound_xl->fops->link,
	      data_to_str (path_data),
	      data_to_str (buf_data));

  return 0;
}

/*
 * server_symlink_cbk - symlink callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 *
 * not for external reference
 */
static int32_t
server_symlink_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  
  char *stat_buf = stat_to_str (stbuf);
  dict_set (reply, "STAT", str_to_data (stat_buf));
  
  server_fop_reply (frame,
		GF_FOP_SYMLINK,
		reply);
  free (stat_buf);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_symlink- symlink function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 * 
 * not for external reference
 */

static int32_t
server_symlink (call_frame_t *frame,
		xlator_t *bound_xl,
		dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INOD");
  data_t *buf_data = dict_get (params, "BUF");

  if (!path_data || !buf_data) {
    struct stat buf = {0, };
    server_symlink_cbk (frame,
			NULL,
			frame->this,
			-1,
			EINVAL,
			&buf);
    return -1;
  }

  STACK_WIND (frame, 
	      server_symlink_cbk, 
	      bound_xl,
	      bound_xl->fops->symlink,
	      data_to_str (path_data),
	      data_to_str (buf_data));

  return 0;
}

/*
 * server_unlink_cbk - unlink callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 *
 * not for external reference
 */
static int32_t
server_unlink_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  server_fop_reply (frame,
		GF_FOP_UNLINK,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_unlink - unlink function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 * 
 * not for external reference
 */

static int32_t
server_unlink (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");

  if (!path_data || !inode_data) {
    server_unlink_cbk (frame,
		       NULL,
		       frame->this,
		       -1,
		       EINVAL);
    return -1;
  }

  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *unlink_stub = fop_unlink_stub (frame, 
						bound_xl->fops->unlink,
						loc);
    frame->local = unlink_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {

    STACK_WIND (frame, 
		server_unlink_cbk, 
		bound_xl,
		bound_xl->fops->unlink,
		loc);
  }
  return 0;
}

/*
 * server_rename_cbk - rename callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 *
 * not for external reference
 */
static int32_t
server_rename_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  server_fop_reply (frame,
		GF_FOP_RENAME,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_rename - rename function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 * 
 * not for external reference
 */

static int32_t
server_rename (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *buf_data = dict_get (params, "BUF");

  if (!path_data || !buf_data) {
    server_rename_cbk (frame,
		       NULL,
		       frame->this,
		       -1,
		       EINVAL);
    return -1;
  }
  
  loc_t *oldloc = calloc (1, sizeof (loc_t));
  loc_t *newloc = calloc (1, sizeof (loc_t));
  oldloc->path = strdup (path);
  newloc->path = strdup (buf);
  oldloc->ino = data_to_uint64 (inode_data);
  oldloc->inode = inode_update (table, NULL, NULL, oldloc->ino);
  
  if (!oldloc->inode){
    /* inode data not found in table. we need to lookup for oldpath also */
    ;
  }

  /* lookup for newpath */
  ;
  
  /* continue with rename */
  ;
  STACK_WIND (frame, 
	      server_rename_cbk, 
	      bound_xl,
	      bound_xl->fops->rename,
	      data_to_str (path_data),
	      data_to_str (buf_data));
  
  return 0;
}

/*
 * server_setxattr_cbk - setxattr callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 *
 * not for external reference
 */
static int32_t
server_setxattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  server_fop_reply (frame,
		GF_FOP_SETXATTR,
		reply);
  
  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_setxattr - setxattr function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 * 
 * not for external reference
 */

static int32_t
server_setxattr (call_frame_t *frame,
		 xlator_t *bound_xl,
		 dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *name_data = dict_get (params, "NAME");
  char *name = strdup (data_to_str (name_data));
  data_t *count_data = dict_get (params, "COUNT");
  size_t size = data_to_uint64 (count_data);
  data_t *flag_data = dict_get (params, "FLAGS");
  int32_t flags = data_to_int32 (flag_data);
  data_t *value_data = dict_get (params, "VALUE"); // reused
  char *value = strdup (data_to_str (value_data));

  if (!path_data || !name_data || !count_data || !flag_data || !value_data) {
    server_setxattr_cbk (frame,
			 NULL,
			 frame->this,
			 -1,
			 EINVAL);
    return -1;
  }


  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *setxattr_stub = fop_setxattr_stub (frame, 
						    bound_xl->fops->setxattr,
						    loc,
						    name,
						    value,
						    size,
						    flags);
    frame->local = setxattr_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {

    STACK_WIND (frame, 
		server_setxattr_cbk, 
		bound_xl,
		bound_xl->fops->setxattr,
		loc,
		name,
		value,
		size,
		flags);
  }
  return 0;
}

/*
 * server_getxattr_cbk - getxattr callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 * @value:
 *
 * not for external reference
 */
static int32_t
server_getxattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     void *value)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  dict_set (reply, "BUF", str_to_data ((char *)value));
  server_fop_reply (frame,
		GF_FOP_GETXATTR,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_getxattr - getxattr function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 * 
 * not for external reference
 */

static int32_t
server_getxattr (call_frame_t *frame,
		 xlator_t *bound_xl,
		 dict_t *params)
{
  data_t *name_data = dict_get (params, "NAME");
  char *name = strdup (name_data);
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *count_data = dict_get (params, "COUNT");
  int64_t size = data_to_int64 (count_data);

  if (!path_data || !name_data || !count_data) {
    server_getxattr_cbk (frame,
			 NULL,
			 frame->this,
			 -1,
			 EINVAL,
			 NULL);
    return -1;
  }

  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *getxattr_stub = fop_getxattr_stub (frame, 
						    bound_xl->fops->getxattr,
						    loc,
						    name,
						    size);
    frame->local = getxattr_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {

    STACK_WIND (frame, 
		server_getxattr_cbk, 
		bound_xl,
		bound_xl->fops->getxattr,
		loc,
		name,
		size);
  }

  return 0;
}

/*
 * server_listxattr_cbk - listxattr callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 * @value:
 *
 * not for external reference
 */
static int32_t
server_listxattr_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      void *value)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  dict_set (reply, "BUF", str_to_data ((char *)value));

  server_fop_reply (frame,
		GF_FOP_LISTXATTR,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_listxattr - listxattr function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 * 
 * not for external reference
 */
static int32_t
server_listxattr (call_frame_t *frame,
		  xlator_t *bound_xl,
		  dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *count_data = dict_get (params, "COUNT");
  int64_t size = data_to_int64 (count_data);

  if (!path_data || !count_data) {
    server_listxattr_cbk (frame,
			  NULL,
			  frame->this,
			  -1,
			  EINVAL,
			  NULL);
    return -1;
  }


  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *listxattr_stub = fop_listxattr_stub (frame, 
						      bound_xl->fops->listxattr,
						      loc,
						      size);
    frame->local = listxattr_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {

    STACK_WIND (frame, 
		server_listxattr_cbk, 
		bound_xl,
		bound_xl->fops->listxattr,
		loc,
		size);
  }

  return 0;
}

/*
 * server_removexattr_cbk - removexattr callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 *
 * not for external reference
 */
static int32_t
server_removexattr_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  server_fop_reply (frame,
		GF_FOP_REMOVEXATTR,
		reply);
  
  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_removexattr - removexattr function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 * 
 * not for external reference
 */
static int32_t
server_removexattr (call_frame_t *frame,
		    xlator_t *bound_xl,
		    dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *name_data = dict_get (params, "NAME");
  char *name = strdup (data_to_str (name_data));

  if (!path_data || !name_data) {
    server_removexattr_cbk (frame,
			    NULL,
			    frame->this,
			    -1,
			    EINVAL);
    return -1;
  }


  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *removexattr_stub = fop_removexattr_stub (frame, 
							  bound_xl->fops->removexattr,
							  loc,
							  name);
    frame->local = removexattr_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {

    STACK_WIND (frame, 
		server_removexattr_cbk, 
		bound_xl,
		bound_xl->fops->removexattr,
		loc,
		name);
  }
  return 0;
}

/*
 * server_statfs_cbk - statfs callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 * @buf:
 *
 * not for external reference
 */
static int32_t
server_statfs_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct statvfs *buf)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

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
    
    dict_set (reply, "BUF", str_to_data (buffer));
  }

  server_fop_reply (frame,
		GF_FOP_STATFS,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_statfs - statfs function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 * 
 * not for external reference
 */
static int32_t
server_statfs (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");

  if (!path_data || !inode_data) {
    struct statvfs buf = {0,};
    server_statfs_cbk (frame,
		    NULL,
		    frame->this,
		    -1,
		    EINVAL,
		    &buf);
    return -1;
  }

  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *statfs_stub = fop_statfs_stub (frame, 
						bound_xl->fops->statfs,
						loc);
    frame->local = statfs_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {
    STACK_WIND (frame, 
		server_statfs_cbk, 
		bound_xl,
		bound_xl->fops->statfs,
		loc);
  }
  return 0;
}


/*
 * server_opendir_cbk - opendir callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 * @fd: file descriptor structure of opened directory
 *
 * not for external reference
 */
static int32_t
server_opendir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    fd_t *fd)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  dict_set (reply, "FD", data_from_ptr (fd));

  server_fop_reply (frame,
		GF_FOP_OPENDIR,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_opendir - opendir function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 * 
 * not for external reference
 */
static int32_t
server_opendir (call_frame_t *frame,
		xlator_t *bound_xl,
		dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");

  if (!path_data || !inode_data) {
    server_opendir_cbk (frame,
		     NULL,
		     frame->this,
		     -1,
		     EINVAL,
		     NULL);
    return -1;
  }

  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *opendir_stub = fop_opendir_stub (frame, 
						  bound_xl->fops->opendir,
						  loc);
    frame->local = opendir_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {
    STACK_WIND (frame, 
		server_opendir_cbk, 
		bound_xl,
		bound_xl->fops->opendir,
		loc);
  }

  return 0;
}

/*
 * server_closedir_cbk - closedir callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 *
 * not for external reference
 */
static int32_t
server_closedir_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  server_fop_reply (frame,
		GF_FOP_CLOSEDIR,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_closedir - closedir function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 * 
 * not for external reference
 */
static int32_t
server_closedir (call_frame_t *frame,
		   xlator_t *bound_xl,
		   dict_t *params)
{
  data_t *ctx_data = dict_get (params, "FD");
 
  if (!ctx_data) {
    server_closedir_cbk (frame,
			NULL,
			frame->this,
			-1,
			EINVAL);
    return -1;
  }
  
  STACK_WIND (frame, 
	      server_closedir_cbk, 
	      bound_xl,
	      bound_xl->fops->closedir,
	      (dict_t *)data_to_ptr (ctx_data));
  
  return 0;
}

//readdir

/*
 * server_readdir_cbk - readdir callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 * @entries:
 * @count:
 *
 * not for external reference
 */
static int32_t
server_readdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dir_entry_t *entries,
		    int32_t count)
{
  dict_t *reply = get_new_dict ();
  char *buffer;

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  dict_set (reply, "NR_ENTRIES", data_from_int32 (count));
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
    dict_set (reply, "BUF", str_to_data (buffer));
  }

  server_fop_reply (frame,
		GF_FOP_READDIR,
		reply);

  free (buffer);
  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_readdir - readdir function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 * 
 * not for external reference
 */
static int32_t
server_readdir (call_frame_t *frame,
		xlator_t *bound_xl,
		dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *size_data = dict_get (params, "SIZE");;
  data_t *offset_data = dict_get (params, "OFFSET");
  data_t *fd_data = dict_get (params, "FD");
  if (!path_data) {
    dir_entry_t tmp = {0,};
    server_readdir_cbk (frame,
		     NULL,
		     frame->this,
		     -1,
		     EINVAL,
		     &tmp,
		     0);
    return -1;
  }

  STACK_WIND (frame, 
	      server_readdir_cbk, 
	      bound_xl,
	      bound_xl->fops->readdir,
	      data_to_uint64 (size_data),
	      data_to_uint64 (offset_data),
	      data_to_ptr (fd_data));

  return 0;
}

/*
 * server_fsyncdir_cbk - fsyncdir callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 * not for external reference
 */
static int32_t
server_fsyncdir_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  server_fop_reply (frame,
		GF_FOP_FSYNCDIR,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_fsyncdir - fsyncdir function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 * 
 * not for external reference
 */
static int32_t
server_fsyncdir (call_frame_t *frame,
		 xlator_t *bound_xl,
		 dict_t *params)
{
  data_t *fd_data = dict_get (params, "FD");
  data_t *flag_data = dict_get (params, "FLAGS");

  if (!fd_data || !flag_data) {
    server_fsyncdir_cbk (frame,
			 NULL,
			 frame->this,
			 -1,
			 EINVAL);
    return -1;
  }

  STACK_WIND (frame, 
	      server_fsyncdir_cbk, 
	      bound_xl,
	      bound_xl->fops->fsyncdir,
	      (fd_t *)data_to_ptr (fd_data),
	      data_to_int64 (flag_data));

  return 0;
}

/*
 * server_mknod_cbk - mknod callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 * not for external reference
 */
static int32_t
server_mknod_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  char *stat_buf = stat_to_str (stbuf);
  dict_set (reply, "STAT", str_to_data (stat_buf));

  server_fop_reply (frame,
		GF_FOP_MKNOD,
		reply);
  free (stat_buf);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_mknod - mknod function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
static int32_t
server_mknod (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *mode_data = dict_get (params, "MODE");
  data_t *dev_data = dict_get (params, "DEV");

  if (!path_data || !mode_data || !dev_data) {
    struct stat buf = {0, };
    server_mknod_cbk (frame,
		   NULL,
		   frame->this,
		   -1,
		   EINVAL,
		   &buf);
    return -1;
  }

  STACK_WIND (frame, 
	      server_mknod_cbk, 
	      bound_xl,
	      bound_xl->fops->mknod,
	      data_to_str (path_data),
	      data_to_int64 (mode_data),
	      data_to_int64 (dev_data));

  return 0;
}

/*
 * server_mkdir_cbk - mkdir callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 * not for external reference
 */
static int32_t
server_mkdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();
  char *statbuf;

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  statbuf = stat_to_str (stbuf);
  dict_set (reply, "STAT", str_to_data (statbuf));

  server_fop_reply (frame,
		GF_FOP_MKDIR,
		reply);

  free (statbuf);
  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_mkdir - mkdir function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params:
 *
 * not for external reference
 */
static int32_t
server_mkdir (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *mode_data = dict_get (params, "MODE");

  if (!path_data || !mode_data) {
    server_mkdir_cbk (frame,
		      NULL,
		      frame->this,
		      -1,
		      EINVAL,
		      NULL);
    return -1;
  }
  
  STACK_WIND (frame, 
	      server_mkdir_cbk, 
	      bound_xl,
	      bound_xl->fops->mkdir,
	      data_to_str (path_data),
	      data_to_int64 (mode_data));
  
  return 0;
}

/*
 * server_rmdir_cbk - rmdir callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 * not for external reference
 */
static int32_t
server_rmdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  server_fop_reply (frame,
		GF_FOP_RMDIR,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_rmdir - rmdir function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params:
 *
 * not for external reference
 */
static int32_t
server_rmdir (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");

  if (!path_data) {
    server_rmdir_cbk (frame,
		   NULL,
		   frame->this,
		   -1,
		   EINVAL);
    return -1;
  }
  

  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *rmdir_stub = fop_rmdir_stub (frame, 
					      bound_xl->fops->rmdir,
					      loc);
    frame->local = rmdir_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {

    STACK_WIND (frame, 
		server_rmdir_cbk, 
		bound_xl,
		bound_xl->fops->rmdir,
		loc);
  }
  return 0;
}

/*
 * server_chown_cbk - chown callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 * not for external reference
 */
static int32_t
server_chown_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  char *stat_buf = stat_to_str (stbuf);
  dict_set (reply, "STAT", str_to_data (stat_buf));

  server_fop_reply (frame,
		GF_FOP_CHOWN,
		reply);
  free (stat_buf);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_chown - chown function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
static int32_t
server_chown (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *uid_data = dict_get (params, "UID");
  uid_t uid = data_to_uint64 (uid_data);
  data_t *gid_data = dict_get (params, "GID");
  gid_t gid = data_to_uint64 (gid_data);

  if (!path_data || !uid_data & !gid_data) {
    struct stat buf = {0, };
    server_chown_cbk (frame,
		      NULL,
		      frame->this,
		      -1,
		      EINVAL,
		      &buf);
    return -1;
  }


  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *chown_stub = fop_chown_stub (frame, 
					      bound_xl->fops->chown,
					      loc,
					      uid,
					      gid);
    frame->local = chown_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {

    STACK_WIND (frame, 
		server_chown_cbk, 
		bound_xl,
		bound_xl->fops->chown,
		loc,
		uid,
		gid);
  }

  return 0;
}

/*
 * server_chmod_cbk - chmod callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 * not for external reference
 */
static int32_t
server_chmod_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  
  char *stat_buf = stat_to_str (stbuf);
  dict_set (reply, "STAT", str_to_data (stat_buf));

  server_fop_reply (frame,
		GF_FOP_CHMOD,
		reply);
  free (stat_buf);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_chmod - chmod function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
static int32_t
server_chmod (call_frame_t *frame,
	      xlator_t *bound_xl,
	      dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *mode_data = dict_get (params, "MODE");
  mode_t mode = data_to_uint64 (mode_data);

  if (!path_data || !mode_data) {
    struct stat buf = {0, };
    server_chmod_cbk (frame,
		      NULL,
		      frame->this,
		      -1,
		      EINVAL,
		      &buf);
    return -1;
  }

  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *chmod_stub = fop_chmod_stub (frame, 
						bound_xl->fops->statfs,
						loc,
						mode);
    frame->local = chmod_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {

    STACK_WIND (frame, 
		server_chmod_cbk, 
		bound_xl,
		bound_xl->fops->chmod,
		loc,
		mode);
  }

  return 0;
}

/*
 * server_utimens_cbk - utimens callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 * not for external reference
 */
static int32_t
server_utimens_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *stbuf)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  char *stat_buf = stat_to_str (stbuf);
  dict_set (reply, "STAT", str_to_data (stat_buf));

  server_fop_reply (frame,
		GF_FOP_UTIMENS,
		reply);
  free (stat_buf);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_utimens - utimens function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
static int32_t
server_utimens (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *atime_sec_data = dict_get (params, "ACTIME_SEC");
  data_t *mtime_sec_data = dict_get (params, "MODTIME_SEC");
  data_t *atime_nsec_data = dict_get (params, "ACTIME_NSEC");
  data_t *mtime_nsec_data = dict_get (params, "MODTIME_NSEC");

  if (!path_data || !atime_sec_data || !mtime_sec_data) {
    struct stat buf = {0, };
    server_utimens_cbk (frame,
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

  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *utimens_stub = fop_utimens_stub (frame, 
						  bound_xl->fops->statfs,
						  loc,
						  buf);
    frame->local = utimens_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {

    STACK_WIND (frame, 
		server_utimens_cbk, 
		bound_xl,
		bound_xl->fops->utimens,
		loc,
		buf);
  }

  return 0;
}

/*
 * server_access_cbk - access callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 * not for external reference
 */
static int32_t
server_access_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  server_fop_reply (frame,
		GF_FOP_ACCESS,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * server_access - access function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
static int32_t
server_access (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  data_t *inode_data = dict_get (params, "INODE");
  data_t *mode_data = dict_get (params, "MODE");
  mode_t mode = data_to_uint64 (mode_data);

  if (!path_data || !mode_data) {
    server_access_cbk (frame,
		       NULL,
		       frame->this,
		       -1,
		       EINVAL);
    return -1;
  }
  
  loc_t *loc = calloc (1, sizeof (loc_t));
  loc->path = strdup (data_to_str (path_data));
  loc->ino = data_to_uint64 (inode_data);
  loc->inode = inode_update (table, NULL, NULL, loc->ino);

  if (!loc->inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    call_stub_t *access_stub = fop_access_stub (frame, 
						bound_xl->fops->access,
						loc,
						mode);
    frame->local = access_stub;
    
    STACK_WIND (frame,
		server_lookup_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		loc);
  } else {

    STACK_WIND (frame, 
		server_access_cbk, 
		bound_xl,
		bound_xl->fops->access,
		loc,
		mode);
  }
  return 0;
}

/*
 * server_lk_cbk - lk callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @lock:
 *
 * not for external reference
 */
static int32_t
server_lk_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct flock *lock)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  dict_set (reply, "TYPE", data_from_int16 (lock->l_type));
  dict_set (reply, "WHENCE", data_from_int16 (lock->l_whence));
  dict_set (reply, "START", data_from_int64 (lock->l_start));
  dict_set (reply, "LEN", data_from_int64 (lock->l_len));
  dict_set (reply, "PID", data_from_uint64 (lock->l_pid));

  server_fop_reply (frame,
		GF_FOP_LK,
		reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/* 
 * server_lk - lk function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
static int32_t
server_lk (call_frame_t *frame,
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

    server_lk_cbk (frame,
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
  lock.l_pid =  data_to_uint32 (pid_data);


  STACK_WIND (frame, 
	      server_lk_cbk, 
	      bound_xl,
	      bound_xl->fops->lk,
	      (fd_t *)(data_to_ptr (fd_data)),
	      cmd,
	      &lock);
  
  return 0;
}

/* Management Calls */
/*
 * mop_getspec - getspec function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params:
 *
 */
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
    
  dict_set (dict, "RET", data_from_int32 (ret));
  dict_set (dict, "ERRNO", data_from_int32 (errno));

  server_mop_reply (frame, 
	     GF_MOP_GETSPEC, 
	     dict);

  dict_destroy (dict);
  if (file_data)
    free (file_data);
  STACK_DESTROY (frame->root);

  return ret;
}

/*
 * mop_setspec - setspec function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params:
 *
 */
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
  
  dict_set (dict, "RET", data_from_int32 (ret));
  dict_set (dict, "ERRNO", data_from_int32 (remote_errno));

  server_mop_reply (frame, 
	     GF_MOP_GETSPEC, 
	     dict);

  dict_destroy (dict);
  
  return ret;
}

/*
 * server_mop_lock_cbk - lock callback for server management operation
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 *
 * not for external reference
 */
static int32_t
server_mop_lock_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  server_mop_reply (frame, 
	     GF_MOP_LOCK, 
	     reply);

  STACK_DESTROY (frame->root);
  dict_destroy (reply);
  return 0;
}

/*
 * mop_lock - lock management function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params:
 *
 */
int32_t 
mop_lock (call_frame_t *frame,
	  xlator_t *bound_xl,
	  dict_t *params)
{
  data_t *path_data = dict_get (params, "PATH");
  char *path;
  
  path_data = dict_get (params, "PATH");

  if (!path_data) {
    server_mop_lock_cbk (frame,
			 NULL,
			 frame->this,
			 -1,
			 EINVAL);
    return -1;
  }

  path = data_to_str (path_data);

  STACK_WIND (frame,
	      server_mop_lock_cbk,
	      frame->this,
	      frame->this->mops->lock,
	      path);

  return 0;
}


/*
 * server_mop_unlock_cbk - unlock callback for server management operation
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 *
 * not for external reference
 */
static int32_t
mop_unlock_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  dict_t *reply = get_new_dict ();

  dict_set (reply, "RET", data_from_int32 (op_ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

  server_mop_reply (frame, 
	     GF_MOP_UNLOCK, 
	     reply);

  dict_destroy (reply);
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * mop_unlock - unlock management function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 */
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

/*
 * mop_unlock - unlock management function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 */
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

  dict_set (dict, "RET_OP", data_from_uint64 (0xbabecafe));
  dict_set (dict, "RET", data_from_int32 (ret));
  dict_set (dict, "ERRNO", data_from_int32 (errno));

  server_mop_reply (frame, 
	     GF_MOP_LISTLOCKS, 
	     dict);
  dict_destroy (dict);
  
  STACK_DESTROY (frame->root);
  return 0;
}



/*
 * mop_unlock - unlock management function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 */
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

/*
 * mop_unlock - unlock management function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 */
int32_t 
mop_setvolume (call_frame_t *frame,
	       xlator_t *bound_xl,
	       dict_t *params)
{
  int32_t ret = -1;
  int32_t remote_errno = 0;
  dict_t *dict = get_new_dict ();
  server_proto_priv_t *priv;
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
  dict_set (dict, "RET", data_from_int32 (ret));
  dict_set (dict, "ERRNO", data_from_int32 (remote_errno));

  server_mop_reply (frame, 
	     GF_MOP_SETVOLUME, 
	     dict);
  dict_destroy (dict);

  STACK_DESTROY (frame->root);
  return ret;
}

/*
 * server_mop_stats_cbk - stats callback for server management operation
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 * @stats:
 *
 * not for external reference
 */

int32_t 
server_mop_stats_cbk (call_frame_t *frame, 
		      void *cookie,
		      xlator_t *xl, 
		      int32_t ret, 
		      int32_t op_errno, 
		      struct xlator_stats *stats)
{
  /* TODO: get this information from somewhere else, not extern */
  int32_t glusterfsd_stats_nr_clients = 0;

  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));

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
    dict_set (reply, "BUF", str_to_data (buffer));
  }

  server_mop_reply (frame, 
		    GF_MOP_STATS, 
		    reply);

  dict_destroy (reply);

  STACK_DESTROY (frame->root);

  return 0;
}


/*
 * mop_unlock - unlock management function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 */
static int32_t
mop_stats (call_frame_t *frame,
	   xlator_t *bound_xl,
	   dict_t *params)
{
  data_t *flag_data = dict_get (params, "FLAGS");

  if (!flag_data) {
    server_mop_stats_cbk (frame,
			  NULL,
			  frame->this,
			  -1,
			  EINVAL,
			  NULL);
    return -1;
  }
  
  STACK_WIND (frame, 
	      server_mop_stats_cbk, 
	      bound_xl,
	      bound_xl->mops->stats,
	      data_to_int64 (flag_data));
  
  return 0;
}

/*
 * server_mop_fsck_cbk - fsck callback for server management operation
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 *
 * not for external reference
 */

int32_t 
server_mop_fsck_cbk (call_frame_t *frame, 
		     void *cookie,
		     xlator_t *xl, 
		     int32_t ret, 
		     int32_t op_errno)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (ret));
  dict_set (reply, "ERRNO", data_from_int32 (op_errno));
  
  server_mop_reply (frame, 
	     GF_MOP_FSCK, 
	     reply);
  
  dict_destroy (reply);
  return 0;
}


/*
 * mop_unlock - unlock management function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 */
int32_t 
mop_fsck (call_frame_t *frame,
	  xlator_t *bound_xl,
	  dict_t *params)
{
  data_t *flag_data = dict_get (params, "FLAGS");

  if (!flag_data) {
    server_mop_fsck_cbk (frame,
			 NULL,
			 frame->this,
			 -1,
			 EINVAL);
    return -1;
  }
  
  STACK_WIND (frame, 
	      server_mop_fsck_cbk, 
	      bound_xl,
	      bound_xl->mops->fsck,
	      data_to_int64 (flag_data));
  
  return 0;
}

/* 
 * unknown_op_cbk - This function is called when a opcode for unknown type is called 
 *                  Helps to keep the backward/forward compatiblity
 * @frame: call frame
 * @type:
 * @opcode:
 *
 */

int32_t 
unknown_op_cbk (call_frame_t *frame, 
		int32_t type,
		int32_t opcode)
{
  dict_t *reply = get_new_dict ();
  
  dict_set (reply, "RET", data_from_int32 (-1));
  dict_set (reply, "ERRNO", data_from_int32 (ENOSYS));
  
  if (type == GF_OP_TYPE_MOP_REQUEST)
    server_mop_reply (frame, 
		      opcode, 
		      reply);
  else 
    server_fop_reply (frame, 
		      opcode, 
		      reply);
  
  dict_destroy (reply);
  return 0;
}	     

/* 
 * get_frame_for_call - create a frame into the call_ctx_t capable of generating 
 *                      and replying the reply packet by itself.
 *                      By making a call with this frame, the last UNWIND function
 *                      will have all needed state from its frame_t->root to
 *                      send reply.
 * @trans:
 * @blk:
 * @params:
 *
 * not for external reference
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
    _call->uid = (uid_t) data_to_uint64 (d);
  d = dict_get (params, "CALLER_GID");
  if (d)
    _call->gid = (gid_t) data_to_uint64 (d);
  d = dict_get (params, "CALLER_PID");
  if (d)
    _call->pid = (gid_t) data_to_uint64 (d);

  return &_call->frames;
}

/* prototype of operations function for each of mop and fop at server protocol level */
typedef int32_t (*gf_op_t) (call_frame_t *frame,
			    xlator_t *bould_xl,
			    dict_t *params);

static gf_op_t gf_fops[] = {
  server_stat,
  server_readlink,
  server_mknod,
  server_mkdir,
  server_unlink,
  server_rmdir,
  server_symlink,
  server_rename,
  server_link,
  server_chmod,
  server_chown,
  server_truncate,
  server_utimens,
  server_open,
  server_readv,
  server_writev,
  server_statfs,
  server_flush,
  server_close,
  server_fsync,
  server_setxattr,
  server_getxattr,
  server_listxattr,
  server_removexattr,
  server_opendir,
  server_readdir,
  server_closedir,
  server_fsyncdir,
  server_access,
  server_create,
  server_ftruncate,
  server_fstat,
  server_lk
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
  mop_fsck
};

/*
 * server_protocol_interpret - protocol interpreter function for server protocol
 * @trans: transport object
 * @blk: data block
 *
 */
static int32_t 
server_protocol_interpret (transport_t *trans,
			   gf_block_t *blk)
{
  int32_t ret = 0;
  dict_t *params = blk->dict;
  server_proto_priv_t *priv = trans->xl_private;
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

/*
 * server_nop_cbk - nop callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 *
 * not for external reference
 */
static int32_t
server_nop_cbk (call_frame_t *frame,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * get_frame_for_transport - get call frame for specified transport object
 * @trans: transport object
 *
 */
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

/*
 * open_file_cleanup_fn - cleanup the open file related data from private data
 * @this:
 * @key:
 * @value:
 * @data:
 *
 */
static void
open_file_cleanup_fn (dict_t *this,
		      char *key,
		      data_t *value,
		      void *data)
{
  dict_t *file_ctx;
  transport_t *trans = data;
  server_proto_priv_t *priv = trans->xl_private;
  xlator_t *bound_xl = priv->bound_xl;
  call_frame_t *frame;

  file_ctx = (dict_t *) strtoul (key, NULL, 0);
  frame = get_frame_for_transport (trans);

  gf_log ("protocol/server",
	  GF_LOG_DEBUG,
	  "force releaseing file %p", file_ctx);

  STACK_WIND (frame,
	      server_nop_cbk,
	      bound_xl,
	      bound_xl->fops->close,
	      file_ctx);
  return;
}

/* 
 * server_protocol_cleanup - cleanup function for server protocol
 * @trans: transport object
 *
 */
static int32_t
server_protocol_cleanup (transport_t *trans)
{
  server_proto_priv_t *priv = trans->xl_private;
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
	      server_nop_cbk,
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

/*
 * server_protocol_notify - notify function for server protocol
 * @this: 
 * @trans:
 * @event:
 *
 */
static int32_t
server_protocol_notify (xlator_t *this,
			transport_t *trans,
			int32_t event)
{
  int ret = 0;

  server_proto_priv_t *priv = trans->xl_private;

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
      ret = server_protocol_interpret (trans, blk);

      free (blk);
    }
  }

  if (ret || (event & (POLLERR|POLLHUP))) {
    server_protocol_cleanup (trans);
  }

  return ret;
}

/*
 * init - called during server protocol initialization
 * @this:
 *
 */
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
			  server_protocol_notify);

  this->private = trans;
  //  ((server_proto_priv_t *)(trans->xl_private))->bound_xl = FIRST_CHILD (this);

  return 0;
}

/* 
 * fini - finish function for server protocol, called before unloading server protocol
 * @this:
 *
 */
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
