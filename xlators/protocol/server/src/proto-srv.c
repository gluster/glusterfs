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



#include "transport.h"
#include "fnmatch.h"
#include "xlator.h"
#include "protocol.h"
#include "lock.h"
#include "ns.h"
#include "proto-srv.h"
#include <time.h>

#if __WORDSIZE == 64
# define F_L64 "%l"
#else
# define F_L64 "%ll"
#endif

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


#if 0
void
dict_to_list (struct sock_private *priv, dict_t *dict, int32_t op, int32_t type)
{
  int32_t dict_len = dict_serialized_length (dict);
  int8_t *dict_buf = calloc (1, dict_len);
  dict_serialize (dict, dict_buf);

  gf_block *blk = gf_block_new ();
  blk->type = type;
  blk->op   = op;
  blk->size = dict_len;
  blk->data = dict_buf;

  int32_t blk_len = gf_block_serialized_length (blk);
  int8_t *blk_buf = calloc (1, blk_len);
  gf_block_serialize (blk, blk_buf);

  free (blk);
  free (dict_buf);

  struct write_list *_new = calloc (1, sizeof (struct write_list));
  struct write_list *trav = priv->send_list;
  if (!trav) {
    priv->send_list = _new;
  } else {
    while (trav->next) {
      trav = trav->next;
    }
    trav->next = _new;
  }

  _new->buf = blk_buf;
  _new->len = blk_len;
  priv->send_buf_count++;
}

/* Responses */

int32_t 
server_proto_getattr_rsp (call_frame_t *frame,
			  xlator_t *xl, 
			  int32_t ret, 
			  int32_t op_errno, 
			  struct stat *stbuf) 
{

}

int32_t 
server_proto_readlink_rsp (call_frame_t *frame,
			   xlator_t *xl, 
			   int32_t ret, 
			   int32_t op_errno, 
			   int8_t *buf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "BUF", str_to_data (buf));
  
  dict_to_list (sock_priv, dict, OP_READLINK, OP_TYPE_FOP_REPLY);

  free (buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_mknod_rsp (call_frame_t *frame,
			xlator_t *xl, 
			int32_t ret, 
			int32_t op_errno, 
			struct stat *stbuf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  int8_t *stat_buf = convert_stbuf_to_str (stbuf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  dict_to_list (sock_priv, dict, OP_MKNOD, OP_TYPE_FOP_REPLY);

  free (stat_buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_mkdir_rsp (call_frame_t *frame,
			xlator_t *xl, 
			int32_t ret, 
			int32_t op_errno, 
			struct stat *stbuf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  int8_t *stat_buf = convert_stbuf_to_str (stbuf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  dict_to_list (sock_priv, dict, OP_MKDIR, OP_TYPE_FOP_REPLY);

  free (stat_buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_unlink_rsp (call_frame_t *frame,
			 xlator_t *xl, 
			 int32_t ret, 
			 int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_UNLINK, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_rmdir_rsp (call_frame_t *frame,
			xlator_t *xl, 
			int32_t ret, 
			int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_RMDIR, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_symlink_rsp (call_frame_t *frame,
			  xlator_t *xl, 
			  int32_t ret, 
			  int32_t op_errno, 
			  struct stat *stbuf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  int8_t *stat_buf = convert_stbuf_to_str (stbuf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  dict_to_list (sock_priv, dict, OP_SYMLINK, OP_TYPE_FOP_REPLY);

  free (stat_buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_rename_rsp (call_frame_t *frame,
			 xlator_t *xl, 
			 int32_t ret, 
			 int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_RENAME, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_link_rsp (call_frame_t *frame,
		       xlator_t *xl, 
		       int32_t ret, 
		       int32_t op_errno, 
		       struct stat *stbuf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  int8_t *stat_buf = convert_stbuf_to_str (stbuf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  dict_to_list (sock_priv, dict, OP_LINK, OP_TYPE_FOP_REPLY);

  free (stat_buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_chmod_rsp (call_frame_t *frame,
			xlator_t *xl, 
			int32_t ret, 
			int32_t op_errno, 
			struct stat *stbuf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  int8_t *stat_buf = convert_stbuf_to_str (stbuf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  dict_to_list (sock_priv, dict, OP_CHMOD, OP_TYPE_FOP_REPLY);

  free (stat_buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_chown_rsp (call_frame_t *frame,
			xlator_t *xl, 
			int32_t ret, 
			int32_t op_errno, 
			struct stat *stbuf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  int8_t *stat_buf = convert_stbuf_to_str (stbuf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  dict_to_list (sock_priv, dict, OP_CHOWN, OP_TYPE_FOP_REPLY);

  free (stat_buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_truncate_rsp (call_frame_t *frame,
			   xlator_t *xl, 
			   int32_t ret, 
			   int32_t op_errno, 
			   struct stat *stbuf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  int8_t *stat_buf = convert_stbuf_to_str (stbuf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  dict_to_list (sock_priv, dict, OP_TRUNCATE, OP_TYPE_FOP_REPLY);

  free (stat_buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_utime_rsp (call_frame_t *frame,
			xlator_t *xl, 
			int32_t ret, 
			int32_t op_errno, 
			struct stat *stbuf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_UTIME, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_open_rsp (call_frame_t *frame,
		       xlator_t *xl, 
		       int32_t ret, 
		       int32_t op_errno, 
		       file_ctx_t *ctx,
		       struct stat *stbuf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }
  
  if (ret >= 0) {
    struct file_ctx_list *fctxl = calloc (1, sizeof (struct file_ctx_list));
    fctxl->ctx = ctx;
    //no path present;
    fctxl->next = (sock_priv->fctxl)->next;
    (sock_priv->fctxl)->next = fctxl;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "FD", int_to_data ((long)ctx));

  int8_t *stat_buf = convert_stbuf_to_str (stbuf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  dict_to_list (sock_priv, dict, OP_OPEN, OP_TYPE_FOP_REPLY);

  free (stat_buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_read_rsp (call_frame_t *frame,
		       xlator_t *xl, 
		       int32_t ret, 
		       int32_t op_errno,
		       int8_t *buf)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "BUF", bin_to_data (buf, ret));
  
  dict_to_list (sock_priv, dict, OP_READ, OP_TYPE_FOP_REPLY);

  //  free (buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_write_rsp (call_frame_t *frame,
			xlator_t *xl, 
			int32_t ret, 
			int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_WRITE, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_statfs_rsp (call_frame_t *frame,
			 xlator_t *xl, 
			 int32_t ret, 
			 int32_t op_errno, 
			 struct statvfs *buf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  if (ret == 0) {
    int8_t buffer[256] = {0,};
    
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
  
  dict_to_list (sock_priv, dict, OP_STATFS, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_flush_rsp (call_frame_t *frame,
			xlator_t *xl, 
			int32_t ret, 
			int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_FLUSH, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_release_rsp (call_frame_t *frame,
			  xlator_t *xl, 
			  int32_t ret, 
			  int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  struct file_ctx_list *trav_fctxl = sock_priv->fctxl;
  file_ctx_t *tmp_ctx = (file_ctx_t *)(long)data_to_int (dict_get (frame->local, "file-ctx"));;

  while (trav_fctxl->next) {
    if ((trav_fctxl->next)->ctx == tmp_ctx) {
      struct file_ctx_list *fcl = trav_fctxl->next;
      trav_fctxl->next = fcl->next;
      // free (fcl->path); //not set in open :(
      free (fcl->ctx);
      free (fcl);
      break;
    }
    trav_fctxl = trav_fctxl->next;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_RELEASE, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_fsync_rsp (call_frame_t *frame,
			xlator_t *xl, 
			int32_t ret, 
			int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_FSYNC, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_setxattr_rsp (call_frame_t *frame,
			   xlator_t *xl, 
			   int32_t ret, 
			   int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_SETXATTR, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_getxattr_rsp (call_frame_t *frame,
			   xlator_t *xl, 
			   int32_t ret, 
			   int32_t op_errno, 
			   void *value) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_GETXATTR, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_listxattr_rsp (call_frame_t *frame,
			    xlator_t *xl, 
			    int32_t ret, 
			    int32_t op_errno, 
			    void *value)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_LISTXATTR, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_removexattr_rsp (call_frame_t *frame,
			      xlator_t *xl, 
			      int32_t ret, 
			      int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_REMOVEXATTR, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_opendir_rsp (call_frame_t *frame,
			  xlator_t *xl, 
			  int32_t ret, 
			  int32_t op_errno,
			  file_ctx_t *ctx) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "FD", int_to_data ((long)ctx));
  
  dict_to_list (sock_priv, dict, OP_OPENDIR, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_readdir_rsp (call_frame_t *frame,
			  xlator_t *xl, 
			  int32_t ret, 
			  int32_t op_errno, 
			  dir_entry_t *entries,
			  int32_t count) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "NR_ENTRIES", int_to_data (count));
  {   
    int8_t buffer[64 * 1024] = {0,};
    dir_entry_t *trav = entries->next;
    int8_t *tmp_buf = NULL;
    while (trav) {
      strcat (buffer, trav->name);
      strcat (buffer, "/");
      //      tmp_buf = convert_stbuf_to_str (&trav->buf);
      //      strcat (buffer, tmp_buf);
      //      free (tmp_buf);
      trav = trav->next;
    }
    dict_set (dict, "BUF", str_to_data (buffer));
  }

  dict_to_list (sock_priv, dict, OP_READDIR, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_releasedir_rsp (call_frame_t *frame,
			     xlator_t *xl, 
			     int32_t ret, 
			     int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_RELEASEDIR, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_fsyncdir_rsp (call_frame_t *frame,
			   xlator_t *xl, 
			   int32_t ret, 
			   int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_FSYNCDIR, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_access_rsp (call_frame_t *frame,
			 xlator_t *xl, 
			 int32_t ret, 
			 int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  
  dict_to_list (sock_priv, dict, OP_ACCESS, OP_TYPE_FOP_REPLY);

  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_create_rsp (call_frame_t *frame,
			 xlator_t *xl, 
			 int32_t ret, 
			 int32_t op_errno, 
			 file_ctx_t *ctx,
			 struct stat *stbuf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }
  //create fctxl and link ctx to it.
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "FD", int_to_data ((long)ctx));

  int8_t *stat_buf = convert_stbuf_to_str (stbuf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  dict_to_list (sock_priv, dict, OP_CREATE, OP_TYPE_FOP_REPLY);

  free (stat_buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_ftruncate_rsp (call_frame_t *frame,
			    xlator_t *xl, 
			    int32_t ret, 
			    int32_t op_errno, 
			    struct stat *stbuf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  int8_t *stat_buf = convert_stbuf_to_str (stbuf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  dict_to_list (sock_priv, dict, OP_FTRUNCATE, OP_TYPE_FOP_REPLY);

  free (stat_buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_fgetattr_rsp (call_frame_t *frame,
			   xlator_t *xl, 
			   int32_t ret, 
			   int32_t op_errno,
			   struct stat *stbuf) 
{
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  int8_t *stat_buf = convert_stbuf_to_str (stbuf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  dict_to_list (sock_priv, dict, OP_FGETATTR, OP_TYPE_FOP_REPLY);
  
  free (stat_buf);
  dict_destroy (dict);
  return 0;
}

/* Management Calls */
int32_t 
server_proto_getspec (struct sock_private *sock_priv)
{
  int32_t ret = -1;
  int32_t spec_fd = -1;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  void *file_data = NULL;
  int32_t file_data_len = 0;
  int32_t offset = 0;

  struct stat *stbuf = alloca (sizeof (struct stat));

  ret = open (GLUSTERFSD_SPEC_PATH, O_RDONLY);
  spec_fd = ret;
  if (spec_fd < 0){
    goto fail;
  }
  
  /* to allocate the proper buffer to hold the file data */
  {
    ret = stat (GLUSTERFSD_SPEC_PATH, stbuf);
    if (ret < 0){
      goto fail;
    }
    
    file_data_len = stbuf->st_size;
    file_data = calloc (1, file_data_len);
  }
  
  while ((ret = read (spec_fd, file_data + offset, file_data_len))){
    if (ret < 0){
      goto fail;
    }
    
    if (ret < file_data_len){
      offset = offset + ret + 1;
      file_data_len = file_data_len - ret;
    }
  }
  
  dict_set (dict, "spec-file-data", bin_to_data (file_data, stbuf->st_size));
 
 fail:
    
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MOP_REPLY);
  dict_destroy (dict);
  
  return ret;

}

int32_t 
server_proto_setspec (struct sock_private *sock_priv)
{
  int32_t ret = -1;
  int32_t spec_fd = -1;
  int32_t remote_errno = 0;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  data_t *data = dict_get (dict, "spec-file-data");
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
  dict_del (dict, "spec-file-data");
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (remote_errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MOP_REPLY);
  dict_destroy (dict);
  
  return ret;
}

int32_t 
server_proto_lock (struct sock_private *sock_priv)
{
  int32_t ret = -1;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  data_t *path_data = dict_get (dict, "PATH");

  if (!path_data) {
    dict_set (dict, "RET", int_to_data (-1));
    dict_set (dict, "ERRNO", int_to_data (ENOENT));
    dict_destroy (dict);
    return -1;
  }

  int8_t *path = data_to_str (path_data);

  ret = gf_lock_try_acquire (path);

  if (!ret) {
    path_data->is_static = 1;

    struct held_locks *newlock = calloc (1, sizeof (*newlock));
    newlock->next = sock_priv->locks;
    sock_priv->locks = newlock;
    newlock->path = strdup (path);
  }
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
server_proto_unlock (struct sock_private *sock_priv)
{
  int32_t ret = -1;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  data_t *path_data = dict_get (dict, "PATH");
  int8_t *path = data_to_str (path_data);


  if (!path_data) {
    dict_set (dict, "RET", int_to_data (-1));
    dict_set (dict, "ERRNO", int_to_data (ENOENT));
    dict_destroy (dict);
    return -1;
  }

  path = data_to_str (path_data);

  ret = gf_lock_release (path);

  {
    struct held_locks *l = sock_priv->locks;
    struct held_locks *p = NULL;

    while (l) {
      if (!strcmp (l->path, path))
	break;
      p = l;
      l = l->next;
    }

    if (l) {
      if (p)
	p->next = l->next;
      else
	sock_priv->locks = l->next;

      free (l->path);
      free (l);
    }
  }
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));


  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
server_proto_listlocks (struct sock_private *sock_priv)
{
  int32_t ret = -1;
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict || !blk){
    gf_log ("server-protocol", GF_LOG_CRITICAL, "server_proto_listlocks: get_new_dict failed");
    ret = -1;
    errno = 0;
    goto fail;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  if (!dict){
    gf_log ("server-protocol", GF_LOG_CRITICAL, "server_proto_listlocks: dict_unserialised failed");
    ret = -1;
    errno = 0;
    goto fail;
  }

  /* logic to read the locks and send them to the person who requested for it */
  {
    int32_t junk = data_to_int (dict_get (dict, "OP"));
    gf_log ("server-protocol", GF_LOG_DEBUG, "server_proto_listlocks: junk is %x", junk);
    gf_log ("server-protocol", GF_LOG_DEBUG, "server_proto_listlocks: listlocks called");
    ret = gf_listlocks ();
    
  }

  free (blk->data);
  


  errno = 0;

  dict_set (dict, "RET_OP", int_to_data (0xbabecafe));
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

 fail:
  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}


int32_t 
server_proto_nslookup (struct sock_private *sock_priv)
{
  int32_t ret = -1;
  int32_t remote_errno = -ENOENT;
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);  

  data_t *path_data = dict_get (dict, "PATH");
  int8_t *path = data_to_str (path_data);
  char *ns = ns_lookup (path);

  ns = ns ? (ret = 0, remote_errno = 0, (char *)ns) : "";
  
  dict_set (dict, "NS", str_to_data (ns));

  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (remote_errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
server_proto_nsupdate (struct sock_private *sock_priv)
{
  int32_t ret = -1;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  data_t *path_data = dict_get (dict, "PATH");
  int8_t *path = data_to_str (path_data);
  data_t *ns_data = dict_get (dict, "NS");
  ns_data->is_static = 1;
  path_data->is_static = 1;

  ret = ns_update (path, data_to_str (ns_data));

  dict_del (dict, "PATH");
  dict_del (dict, "NS");
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}


int32_t 
server_proto_getvolume (struct sock_private *sock_priv)
{
  return 0;
}

int32_t 
server_proto_setvolume (struct sock_private *sock_priv)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  int8_t *name = data_to_str (dict_get (dict, "remote-subvolume"));
  struct xlator *xl = gf_get_xlator_tree_node ();
  FUNCTION_CALLED;

  while (xl) {
    if (strcmp (xl->name, name) == 0)
      break;
    xl = xl->next;
  }
  
  if (!xl) {
    ret = -1;
    remote_errno = ENOENT;
    sock_priv->xl = NULL;
  } else {
    data_t *allow_ip = dict_get (xl->options, "allow-ip");
    int32_t flag = 0;
    if (allow_ip) {
      // check IP range and decide whether the client can do this or not
      socklen_t sock_len = sizeof (struct sockaddr);
      struct sockaddr_in *_sock = calloc (1, sizeof (struct sockaddr_in));
      getpeername (sock_priv->fd, _sock, &sock_len);
      gf_log ("server-protocol", GF_LOG_DEBUG, "server_proto_setvolume: received port = %d\n", ntohs (_sock->sin_port));
      if (ntohs (_sock->sin_port) < 1024) {
	char *ip_addr_str = NULL;
	char *tmp;
	char *ip_addr_cpy = strdup (allow_ip->data);
	ip_addr_str = strtok_r (ip_addr_cpy , ",", &tmp);
	while (ip_addr_str) {
	  gf_log ("server-protocol", GF_LOG_DEBUG, "server_proto_setvolume: IP addr = %s, received ip addr = %s\n", 
		  ip_addr_str, 
		  inet_ntoa (_sock->sin_addr));
	  if (fnmatch (ip_addr_str, inet_ntoa (_sock->sin_addr), 0) == 0) {
	    xlator_t *top = calloc (1, sizeof (xlator_t));
	    top->first_child = xl;
	    top->next_sibling = NULL;
	    top->next = xl;
	    top->name = strdup ("server-protocol");
	    xl->parent = top;

	    sock_priv->xl = top;
	    gf_log ("server-protocol", GF_LOG_DEBUG, "server_proto_setvolume: accepted client from %s\n", inet_ntoa (_sock->sin_addr));
	    flag = 1;
	    break;
	  }
	  ip_addr_str = strtok_r (NULL, ",", &tmp);
	}
	free (ip_addr_cpy);
      }
    }
    if (!flag) {
      ret = -1;
      remote_errno = EACCES;
      sock_priv->xl = NULL;
    }
  }
  dict_del (dict, "remote-subvolume");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (remote_errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MOP_REPLY);
  dict_destroy (dict);
  
  return ret;
}


int32_t 
server_proto_stats_rsp (call_frame_t *frame, 
			xlator_t xl, 
			int32_t ret, 
			int32_t op_errno, 
			struct xlator_stats *stats)
{
  extern int32_t glusterfsd_stats_nr_clients;
  struct sock_private *sock_priv = (struct sock_private *)frame->local;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": get_new_dict() returned NULL");
    return -1;
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  if (ret == 0) {
    int8_t buffer[256] = {0,};
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

  dict_to_list (sock_priv, dict, OP_STATS, OP_TYPE_MOP_REPLY);

n  dict_destroy (dict);
  
  return 0;
}
#endif


static int32_t
generic_reply (call_frame_t *frame,
	       int32_t type,
	       glusterfs_fop_t op,
	       dict_t *params)
{
  gf_block_t *blk;
  int32_t dict_len;
  int8_t *dict_buf;
  int32_t blk_len;
  int8_t *blk_buf;
  transport_t *trans;
  
  dict_len = dict_serialized_length (params);
  dict_buf = calloc (1, dict_len);
  dict_serialize (params, dict_buf);

  blk = gf_block_new (frame->root->unique);
  blk->data = dict_buf;
  blk->size = dict_len;
  blk->type = type;

  blk_len = gf_block_serialized_length (blk);
  blk_buf = calloc (1, blk_len);
  gf_block_serialize (blk, blk_buf);

  free (dict_buf);
  free (blk);

  trans = frame->root->state;

  transport_submit (trans, blk_buf, blk_len);

  free (blk_buf);

  return 0;
}

static int32_t
fop_reply (call_frame_t *frame,
	   glusterfs_fop_t op,
	   dict_t *params)
{
  return generic_reply (frame,
			OP_TYPE_FOP_REPLY,
			op,
			params);
}

static int32_t
mop_reply (call_frame_t *frame,
	   glusterfs_fop_t op,
	   dict_t *params)
{
  return generic_reply (frame,
			OP_TYPE_MOP_REPLY,
			op,
			params);
}

static int32_t
fop_getattr_cbk (call_frame_t *frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  int8_t *stat_buf = stat_to_str (buf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  free (stat_buf);

  fop_reply (frame,
	     OP_GETATTR,
	     dict);

  dict_destroy (dict);
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
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  int8_t *buf)
{
  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (op_ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));
  dict_set (dict, "BUF", str_to_data (buf ? (char *) buf : "" ));

  fop_reply (frame,
	     OP_READLINK,
	     dict);

  dict_destroy (dict);
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
	      (size_t) data_to_int (len_data));

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

  _call->state = trans;        /* which socket */
  _call->unique = blk->callid; /* which call */

  _call->frames.root = _call;
  _call->frames.this = trans->xl;

  d = dict_get (params, "CALLER_UID");
  if (d)
    _call->uid = (uid_t) data_to_int (d);
  d = dict_get (params, "CALLER_GID");
  if (d)
    _call->gid = (gid_t) data_to_int (d);

  return &_call->frames;
}

typedef int32_t (*gf_op_t) (call_frame_t *frame,
			    xlator_t *bould_xl,
			    dict_t *params);

static gf_op_t gf_fops[] = {
  fop_getattr,
  fop_readlink,
#if 0
  fop_mknod,
  fop_mkdir,
  fop_unlink
  fop_rmdir,
  fop_symlink,
  fop_rename,
  fop_link,
  fop_chmod,
  fop_chown,
  fop_truncate,
  fop_utime,
  fop_open,
  fop_read,
  fop_write,
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
  fop_fgetattr
#endif
};

static gf_op_t gf_mops[] = {
#if 0
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
#endif
};

static int32_t 
proto_srv_interpret (transport_t *trans,
		     gf_block_t *blk)
{
  int32_t ret = 0;
  dict_t *params = get_new_dict ();
  struct proto_srv_priv *priv = trans->xl_private;
  xlator_t *bound_xl = priv->bound_xl; /* the xlator to STACK_WIND into */
  call_frame_t *frame = NULL;
  
  if (!params) {
    gf_log ("server-protocol",
	    GF_LOG_DEBUG,
	    "server_proto_open: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &params);

  if (!params) {
    return -1;
  }
  
  switch (blk->type) {
  case OP_TYPE_FOP_REQUEST:

    /* drop connection for unauthorized fs access */
    if (!bound_xl) {
      ret = -1;
      break;
    }

    if (blk->op > FOP_MAXVALUE || blk->op < 0) {
      ret = -1;
      break;
    }

    frame = get_frame_for_call (trans, blk, params);

    ret = gf_fops[blk->op] (frame, bound_xl, params);
#if 0
    switch (blk->op) {
    case OP_GETATTR:
      {
	break;
      }
    case OP_READLINK:
      {
	STACK_WIND (frame, 
		    server_proto_readlink_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->readlink, 
		    data_to_str (dict_get (dict, "PATH")),
		    data_to_int (dict_get (dict, "LEN")));

	break;
      }
    case OP_MKNOD:
      {
	STACK_WIND (frame, 
		    server_proto_mknod_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->mknod, 
		    data_to_bin (dict_get (dict, "PATH")),
		    data_to_int (dict_get (dict, "MODE")),
		    data_to_int (dict_get (dict, "DEV")));

	break;
      }
    case OP_MKDIR:
      {
	STACK_WIND (frame, 
		    server_proto_mkdir_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->mkdir, 
		    data_to_bin (dict_get (dict, "PATH")),
		    data_to_int (dict_get (dict, "MODE")));
	
	break;
      }
    case OP_UNLINK:
      {
	STACK_WIND (frame, 
		    server_proto_unlink_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->unlink, 
		    data_to_bin (dict_get (dict, "PATH")));
	
	break;
      }
    case OP_RMDIR:
      {
	STACK_WIND (frame, 
		    server_proto_rmdir_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->rmdir, 
		    data_to_bin (dict_get (dict, "PATH")));
	
	break;
      }
    case OP_SYMLINK:
      {
	STACK_WIND (frame, 
		    server_proto_symlink_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->symlink, 
		    data_to_str (dict_get (dict, "PATH")),
		    data_to_str (dict_get (dict, "BUF")));
	
	break;
      }
    case OP_RENAME:
      {
	STACK_WIND (frame, 
		    server_proto_rename_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->rename, 
		    data_to_bin (dict_get (dict, "PATH")),
		    data_to_str (dict_get (dict, "BUF")));
	
	break;
      }
    case OP_LINK:
      {
	STACK_WIND (frame, 
		    server_proto_link_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->link, 
		    data_to_str (dict_get (dict, "PATH")),
		    data_to_str (dict_get (dict, "BUF")));
	
	break;
      }
    case OP_CHMOD:
      {
	STACK_WIND (frame, 
		    server_proto_chmod_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->chmod, 
		    data_to_str (dict_get (dict, "PATH")),
		    data_to_int (dict_get (dict, "MODE")));
	
	break;
      }
    case OP_CHOWN:
      {
	STACK_WIND (frame, 
		    server_proto_chown_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->chown, 
		    data_to_str (dict_get (dict, "PATH")),
		    frame->root->uid,
		    frame->root->gid);
	
	break;
      }
    case OP_TRUNCATE:
      {
	STACK_WIND (frame, 
		    server_proto_truncate_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->truncate, 
		    data_to_bin (dict_get (dict, "PATH")),
		    data_to_int (dict_get (dict, "OFFSET")));
	
	break;
      }
    case OP_UTIME:
      {
	struct utimbuf buf;
	buf.actime  = data_to_int (dict_get (dict, "ACTIME"));
	buf.modtime =  data_to_int (dict_get (dict, "MODTIME"));

	STACK_WIND (frame, 
		    server_proto_utime_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->utime, 
		    data_to_str (dict_get (dict, "PATH")),
		    &buf);
	
	break;
      }
    case OP_OPEN:
      {
	//create fctxl and file_ctx;
	STACK_WIND (frame, 
		    server_proto_open_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->open, 
		    data_to_str (dict_get (dict, "PATH")),
		    data_to_int (dict_get (dict, "FLAGS")),
		    data_to_int (dict_get (dict, "MODE")));
	
	break;
      }
    case OP_READ:
      {
	file_ctx_t *tmp_ctx = (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD"));
	
	{
	  struct file_ctx_list *fctxl = sock_priv->fctxl;
	  
	  while (fctxl) {
	    if (fctxl->ctx == tmp_ctx)
	      break;
	    fctxl = fctxl->next;
	  }
	  if (!fctxl)
	    /* TODO: write error to socket instead of returning */
	    return -1;
	}

	STACK_WIND (frame, 
		    server_proto_read_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->read, 
		    tmp_ctx,
		    data_to_int (dict_get (dict, "LEN")),
		    data_to_int (dict_get (dict, "OFFSET")));
	
	break;
      }
    case OP_WRITE:
      {

	file_ctx_t *tmp_ctx = (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD"));
	data_t *buf = dict_get (dict, "BUF");

	{
	  struct file_ctx_list *fctxl = sock_priv->fctxl;
	  
	  while (fctxl) {
	    if (fctxl->ctx == tmp_ctx)
	      break;
	    fctxl = fctxl->next;
	  }	  
	  if (!fctxl)
	    /* TODO: write error to socket instead of returning */
	    return -1;
	}

	STACK_WIND (frame, 
		    server_proto_write_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->write, 
		    tmp_ctx,
		    buf->data,
		    buf->len,
		    data_to_int (dict_get (dict, "OFFSET")));
	
	break;
      }
    case OP_STATFS:
      {
	STACK_WIND (frame, 
		    server_proto_statfs_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->statfs, 
		    data_to_str (dict_get (dict, "PATH")));
	
	break;
      }
    case OP_FLUSH:
      {
	STACK_WIND (frame, 
		    server_proto_flush_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->flush, 
		    (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD")));
	
	break;
      }
    case OP_RELEASE:
      {
	struct file_ctx_list *trav_fctxl = sock_priv->fctxl;
	file_ctx_t *tmp_ctx = (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD"));

	while (trav_fctxl) {
	  if (tmp_ctx == trav_fctxl->ctx)
	    break;
	  trav_fctxl = trav_fctxl->next;
	}
	if (!(trav_fctxl && trav_fctxl->ctx == tmp_ctx))
	  return -1;

	dict_set (frame->local, "file-ctx", int_to_data ((long)tmp_ctx)); // to be used in rsp
	
	STACK_WIND (frame, 
		    server_proto_release_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->release, 
		    tmp_ctx);
	
	break;
      }
    case OP_FSYNC:
      {
	STACK_WIND (frame, 
		    server_proto_fsync_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->fsync, 
		    (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD")),
		    data_to_int (dict_get (dict, "FLAGS")));
	
	break;
      }
    case OP_SETXATTR:
      {
	STACK_WIND (frame, 
		    server_proto_setxattr_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->setxattr, 
		    data_to_str (dict_get (dict, "PATH")),
		    data_to_str (dict_get (dict, "BUF")),
		    data_to_str (dict_get (dict, "FD")),
		    data_to_int (dict_get (dict, "COUNT")),
		    data_to_int (dict_get (dict, "FLAGS")));
	
	break;
      }
    case OP_GETXATTR:
      {
	STACK_WIND (frame, 
		    server_proto_getxattr_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->getxattr, 
		    data_to_str (dict_get (dict, "PATH")),
		    data_to_str (dict_get (dict, "BUF")),
		    data_to_int (dict_get (dict, "COUNT")));
	
	break;
      }
    case OP_LISTXATTR:
      {
	STACK_WIND (frame, 
		    server_proto_listxattr_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->listxattr, 
		    data_to_str (dict_get (dict, "PATH")),
		    data_to_int (dict_get (dict, "COUNT")));
	
	break;
      }
    case OP_REMOVEXATTR:
      {
	STACK_WIND (frame, 
		    server_proto_removexattr_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->removexattr, 
		    data_to_str (dict_get (dict, "PATH")),
		    data_to_str (dict_get (dict, "BUF")));
	
	break;
      }
    case OP_OPENDIR:
      {
	STACK_WIND (frame, 
		    server_proto_opendir_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->opendir, 
		    data_to_str (dict_get (dict, "PATH")));
	
	break;
      }
    case OP_READDIR:
      {
	STACK_WIND (frame, 
		    server_proto_readdir_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->readdir, 
		    data_to_str (dict_get (dict, "PATH")));
	
	break;
      }
    case OP_RELEASEDIR:
      {
	STACK_WIND (frame, 
		    server_proto_releasedir_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->releasedir, 
		    (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD")));
	
	break;
      }
    case OP_FSYNCDIR:
      {
	STACK_WIND (frame, 
		    server_proto_fsync_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->fsync, 
		    (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD")),
		    data_to_int (dict_get (dict, "FLAGS")));
	
	break;
      }
    case OP_ACCESS:
      {
	STACK_WIND (frame, 
		    server_proto_access_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->access, 
		    data_to_str (dict_get (dict, "PATH")),
		    data_to_int (dict_get (dict, "MODE")));
	
	break;
      }
    case OP_CREATE:
      {
	STACK_WIND (frame, 
		    server_proto_create_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->create, 
		    data_to_str (dict_get (dict, "PATH")),
		    data_to_int (dict_get (dict, "MODE")));
	
	break;
      }
    case OP_FTRUNCATE:
      {
	STACK_WIND (frame, 
		    server_proto_ftruncate_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->ftruncate, 
		    (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD")),
		    data_to_int (dict_get (dict, "OFFSET")));
	
	break;
      }
    case OP_FGETATTR:
      {
	STACK_WIND (frame, 
		    server_proto_fgetattr_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->fgetattr, 
		    (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD")));
	
	break;
      }
    }
#endif
    break;
  case OP_TYPE_MOP_REQUEST:

    if (blk->op > MOP_MAXVALUE || blk->op < 0)
      return -1;

    gf_mops[blk->op] (frame, bound_xl, params);
#if 0
    switch (blk->op) {
      case OP_SETVOLUME:
      {
	/* setvolume */
	server_proto_setvolume (sock_priv);
	break;
      }
    case OP_GETVOLUME:
      {
	server_proto_getvolume (sock_priv);
	break;
      }
    case OP_STATS:
      {
	dict_unserialize (blk->data, blk->size, &dict);
	free (blk->data);

	xlator_t *xl = sock_priv->xl;
	call_frame_t *frame = &(cctx->frames);
	cctx->frames.root = cctx;
	cctx->frames.this = xl;
	cctx->unique = data_to_int (dict_get (dict, "CCTX"));
	cctx->uid    = data_to_int (dict_get (dict, "UID"));
	cctx->gid    = data_to_int (dict_get (dict, "GID"));

	frame->local = (void *)sock_priv;

	STACK_WIND (frame, 
		    server_proto_stats_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->stats, 
		    data_to_int (dict_get (dict, "FLAGS")));
	
	break;
      }
    case OP_SETSPEC:
      {
	server_proto_setspec (sock_priv);
	break;
      }
    case OP_GETSPEC:
      {
	server_proto_getspec (sock_priv);
	break;
      }
    case OP_LOCK:
      {
	server_proto_lock (sock_priv);
	break;
      }
    case OP_UNLOCK:
      {
	server_proto_unlock (sock_priv);
	break;
      }
    case OP_LISTLOCKS:
      {
	server_proto_listlocks (sock_priv);
	break;
      }
    case OP_NSLOOKUP:
      {
	server_proto_nslookup (sock_priv);
	break;
      }
    case OP_NSUPDATE:
      {
	server_proto_nsupdate (sock_priv);
	break;
      }
    case OP_FSCK:
      {
	/*	server_proto_fsck (sock_priv);

        STACK_WIND (frame, 
		    server_proto_fsck_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->fsck, 
		    data_to_int (dict_get (dict, "FLAGS"))); */
	
	break;
      }      
    }
#endif
    break;
  default:
    ret = -1;
  }

  dict_destroy (params);

  return ret;  
}

static int32_t
proto_srv_cleanup (transport_t *trans)
{
  struct proto_srv_priv *priv = trans->xl_private;

  if (priv->fctxl) {
    /* TODO: close all open files */
  }

  if (priv->locks) {
    /* TODO: unlock all locks held */
  }

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
  }

  if (event & (POLLIN|POLLPRI)) {
    gf_block_t *blk;

    blk = gf_block_unserialize_transport (trans);
    if (!blk) {
      proto_srv_cleanup (trans);
      return -1;
    }

    ret = proto_srv_interpret (trans, blk);

    free (blk->data);
    free (blk);
  }

  if (event & (POLLERR|POLLHUP)) {
    proto_srv_cleanup (trans);
  }

  return ret;
}

int32_t
init (xlator_t *this)
{
  transport_t *trans;
  trans = transport_load (this->options,
			  this,
			  proto_srv_notify);

  this->private = trans;
  return 0;
}

void
fini (xlator_t *this)
{

  return;
}

struct xlator_mops mops = {

};

struct xlator_fops fops = {

};
