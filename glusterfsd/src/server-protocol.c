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


#include "glusterfsd.h"
#include "xlator.h"

#include <time.h>

#if __WORDSIZE == 64
# define F_L64 "%l"
#else
# define F_L64 "%ll"
#endif


int8_t *
convert_stbuf_to_str (struct stat *stbuf)
{
  int8_t *tmp_buf = calloc (1, 1024);
  {
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

    // convert stat structure to ASCII values (solving endian problem)
    sprintf (tmp_buf, GF_STAT_PRINT_FMT_STR,
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
  
  return tmp_buf;
}

void
dict_to_list (struct sock_private *priv, dict_t *dict, int32_t op, int32_t type)
{
  int32_t dict_len = dict_serialized_length (dict);
  int8_t *dict_buf = malloc (dict_len);
  dict_serialize (dict, dict_buf);

  gf_block *blk = gf_block_new ();
  blk->type = type;
  blk->op   = op;
  blk->size = dict_len;
  blk->data = dict_buf;

  int32_t blk_len = gf_block_serialized_length (blk);
  int8_t *blk_buf = malloc (blk_len);
  gf_block_serialize (blk, blk_buf);

  free (blk);
  free (dict_buf);

  struct write_list *_new = calloc (1, sizeof (struct write_list));
  struct write_list *trav = priv->send_list;
  if (!trav) {
    trav = _new;
  } else {
    while (trav->next) {
      trav = trav->next;
    }
    trav->next = _new;
  }

  _new->buf = blk_buf;
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
  struct sock_private *sock_priv = (struct sock_private *)(long)
                                   data_to_int (dict_get (frame->local, "sock-priv"));
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, ": invalid argument");
    return -1;
  }

  dict_t *dict = get_new_dict ();

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (op_errno));

  int8_t *stat_buf = convert_stbuf_to_str (stbuf);
  dict_set (dict, "BUF", str_to_data (stat_buf));
  
  dict_to_list (sock_priv, dict, OP_GETATTR, OP_TYPE_FOP_REPLY);

  free (stat_buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_readlink_rsp (call_frame_t *frame,
			   xlator_t *xl, 
			   int32_t ret, 
			   int32_t op_errno, 
			   int8_t *buf) 
{
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  free (stat_buf);
  
  dict_to_list (sock_priv, dict, OP_LINK, OP_TYPE_FOP_REPLY);

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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  
  dict_to_list (sock_priv, dict, OP_READ, OP_TYPE_FOP_REPLY);

  free (buf);
  dict_destroy (dict);
  return 0;
}

int32_t 
server_proto_write_rsp (call_frame_t *frame,
			xlator_t *xl, 
			int32_t ret, 
			int32_t op_errno)
{
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)data_to_int (dict_get (frame->local, "sock-priv"));
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
  struct sock_private *sock_priv = (struct sock_private *)(long)
                                   data_to_int (dict_get (frame->local, "sock-priv"));
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

int32_t 
server_proto_requests (struct sock_private *sock_priv)
{
  int32_t ret = 0;
  if (!sock_priv) {
    gf_log ("server-protocol", GF_LOG_DEBUG, "server_proto_open: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("server-protocol", GF_LOG_DEBUG, "server_proto_open: get_new_dict() returned NULL");
    return -1;
  }
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  xlator_t *xl = sock_priv->xl;
  call_ctx_t *cctx = calloc (1, sizeof (call_ctx_t));
  call_frame_t *frame = &(cctx->frames);
  cctx->frames.root = cctx;
  cctx->frames.this = xl;
  cctx->unique = data_to_int (dict_get (dict, "CCTX"));
  cctx->uid    = data_to_int (dict_get (dict, "UID"));
  cctx->gid    = data_to_int (dict_get (dict, "GID"));
  
  if (blk->type == OP_TYPE_FOP_REQUEST) {
    switch (blk->op) {
    case OP_GETATTR:
      {
	STACK_WIND (frame, 
		    server_proto_getattr_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->getattr, 
		    data_to_str (dict_get (dict, "PATH")));
	
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
	STACK_WIND (frame, 
		    server_proto_read_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->read, 
		    (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD")),
		    data_to_int (dict_get (dict, "COUNT")),
		    data_to_int (dict_get (dict, "OFFSET")));
	
	break;
      }
    case OP_WRITE:
      {
	STACK_WIND (frame, 
		    server_proto_write_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->write, 
		    (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD")),
		    data_to_str (dict_get (dict, "BUF")),
		    data_to_int (dict_get (dict, "COUNT")),
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
	STACK_WIND (frame, 
		    server_proto_release_rsp, 
		    xl->first_child, 
		    xl->first_child->fops->release, 
		    (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD")));
	
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
  } else if (blk->type == OP_TYPE_MOP_REQUEST) {
    switch (blk->op) {
      /*case OP_SETVOLUME:
      {
	STACK_WIND (frame, 
		    server_proto_setvolume_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->setvolume, 
		    data_to_str (dict_get (dict, "PATH")));
	
	break;
      }
    case OP_GETVOLUME:
      {
	STACK_WIND (frame, 
		    server_proto_getvolume_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->getvolume, 
		    data_to_str (dict_get (dict, "PATH")));
	
	break;
      }
    case OP_STATS:
      {
	STACK_WIND (frame, 
		    server_proto_stats_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->stats, 
		    data_to_int (dict_get (dict, "FLAGS")));
	
	break;
      }
    case OP_SETSPEC:
      {
	STACK_WIND (frame, 
		    server_proto_setspec_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->setspec, 
		    (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD")));
	
	break;
      }
    case OP_GETSPEC:
      {
	STACK_WIND (frame, 
		    server_proto_getspec_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->getspec, 
		    (file_ctx_t *)(long)data_to_int (dict_get (dict, "FD")));
	
	break;
      }
    case OP_LOCK:
      {
	STACK_WIND (frame, 
		    server_proto_lock_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->lock, 
		    data_to_str (dict_get (dict, "PATH")));
	
	break;
      }
    case OP_UNLOCK:
      {
	STACK_WIND (frame, 
		    server_proto_unlock_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->unlock, 
		    data_to_str (dict_get (dict, "PATH")));
	
	break;
      }
    case OP_LISTLOCKS:
      {
	STACK_WIND (frame, 
		    server_proto_listlocks_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->listlocks, 
		    data_to_str (dict_get (dict, "PATH"))); //pattern ? 
	
	break;
      }
    case OP_NSLOOKUP:
      {
	STACK_WIND (frame, 
		    server_proto_nslookup_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->nslookup, 
		    data_to_str (dict_get (dict, "PATH")));
	
	break;
      }
    case OP_NSUPDATE:
      {
	STACK_WIND (frame, 
		    server_proto_nsupdate_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->nsupdate, 
		    data_to_str (dict_get (dict, "PATH")));
	
	break;
      }
    case OP_FSCK:
      {
	STACK_WIND (frame, 
		    server_proto_fsck_rsp, 
		    xl->first_child, 
		    xl->first_child->mops->fsck, 
		    data_to_int (dict_get (dict, "FLAGS")));
	
	break;
	} */     
    }
  } else {
    ret = -1;
  }
  dict_destroy (dict);
  return ret;  
}

