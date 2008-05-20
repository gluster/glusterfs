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

/* 
 * TODO: whenever inode_search() fails, we need to do dummy_inode() before diverting to lookup()s
 */

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <time.h>
#include <sys/uio.h>
#include <sys/resource.h>

#include "transport.h"
#include "fnmatch.h"
#include "xlator.h"
#include "protocol-binary.h"
#include "lock.h"
#include "server-protocol.h"
#include "call-stub.h"
#include "defaults.h"
#include "list.h"
#include "dict.h"
#include "compat.h"
#include "compat-errno.h"

#define STATE(frame) ((server_state_t *)frame->root->state)
#define TRANSPORT_OF(frame) ((transport_t *) STATE (frame)->trans)
#define SERVER_PRIV(frame) ((server_proto_priv_t *) TRANSPORT_OF(frame)->xl_private)
#define BOUND_XL(frame) ((xlator_t *) STATE (frame)->bound_xl)

static inode_t *
dummy_inode (inode_table_t *table)
{
  inode_t *dummy;

  dummy = calloc (1, sizeof (*dummy));

  dummy->table = table;

  INIT_LIST_HEAD (&dummy->list);
  INIT_LIST_HEAD (&dummy->inode_hash);
  INIT_LIST_HEAD (&dummy->fds);
  INIT_LIST_HEAD (&dummy->dentry.name_hash);
  INIT_LIST_HEAD (&dummy->dentry.inode_list);

  dummy->ref = 1;
  dummy->ctx = get_new_dict ();

  LOCK_INIT (&dummy->lock);
  return dummy;
}


/*
 * server_inode_prune - procedure to prune inode. this procedure is called
 *                      from all fop_cbks where we get a valid inode. 
 * @bound_xl: translator this transport is bound to
 *
 * not for external reference
 */
int32_t
server_inode_prune (xlator_t *bound_xl)
{
  if (!bound_xl || !bound_xl->itable)
    return 0;

  return inode_table_prune (bound_xl->itable);
}

/* 
 * stat_to_str - convert struct stat to a ASCII string
 * @stbuf: struct stat pointer
 *
 * not for external reference
 */
static char *
stat_to_str (struct stat *stbuf)
{
  char *tmp_buf = NULL;

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
	       gf_args_reply_t *reply)
{
  gf_proto_block_t blk = {0,};
  transport_t *trans;
  int32_t count, ret;
  struct iovec *vector;

  trans = TRANSPORT_OF (frame);
  
  blk.callid = frame->root->unique;
  blk.size = gf_proto_get_data_len ((gf_args_t *)reply);
  blk.type = type;
  blk.op   = op;
  blk.args = reply;

  {
    int32_t i;
    count = gf_proto_block_iovec_len (&blk);
    vector = alloca (count * sizeof (*vector));
    memset (vector, 0, count * sizeof (*vector));
    
    gf_proto_block_to_iovec (&blk, vector, &count);
    for (i=0; i<count; i++)
      if (!vector[i].iov_base)
	vector[i].iov_base = alloca (vector[i].iov_len);
    gf_proto_block_to_iovec (&blk, vector, &count);
  }

  ret = trans->ops->writev (trans, vector, count);
  gf_proto_free_args ((gf_args_t *)reply);
  if (ret != 0) {
    if (op != GF_FOP_FORGET) {
      gf_log (trans->xl->name, GF_LOG_ERROR, 
	      "Sending reply failed");
    }
    transport_except (trans);
  }

  return 0;
}

server_reply_t *
server_reply_dequeue (server_reply_queue_t *queue)
{
  server_reply_t *entry = NULL;

  pthread_mutex_lock (&queue->lock);
  {
    while (list_empty (&queue->list))
      pthread_cond_wait (&queue->cond, &queue->lock);
    
    entry = list_entry (queue->list.next, server_reply_t, list);
    list_del_init (&entry->list);
  }
  pthread_mutex_unlock (&queue->lock);

  return entry;
}


static void
server_reply_queue (server_reply_t *entry,
		    server_reply_queue_t *queue)
{
  pthread_mutex_lock (&queue->lock);
  {
    list_add_tail (&entry->list, &queue->list);
    pthread_cond_broadcast (&queue->cond);
  }
  pthread_mutex_unlock (&queue->lock);
}


static void *
server_reply_proc (void *data)
{
  server_reply_queue_t *queue = data;

  while (1) {
    server_reply_t *entry = NULL;
    server_state_t *state = NULL;
    xlator_t *bound_xl = NULL;

    entry = server_reply_dequeue (queue);

    bound_xl = BOUND_XL (entry->frame);

    generic_reply (entry->frame, entry->type, entry->op, entry->reply);

    server_inode_prune (bound_xl);

    state = STATE (entry->frame);
    {
      if (entry->refs)
	dict_unref (entry->refs);
      FREE (entry->reply);
      STACK_DESTROY (entry->frame->root);
      FREE (entry);
    }
    {
      transport_unref (state->trans);
      if (state->inode)
	inode_unref (state->inode);
      if (state->inode2)
	inode_unref (state->inode2);
      FREE (state);
    }
  }

  return NULL;
}

static void
server_reply (call_frame_t *frame,
	      int32_t type,
	      glusterfs_fop_t op,
	      gf_args_reply_t *reply,
	      dict_t *refs)
{
  server_reply_t *entry = NULL;
  transport_t *trans = ((server_private_t *)frame->this->private)->trans;
  server_conf_t *conf = NULL;
  entry = calloc (1, sizeof (*entry));
  entry->frame = frame;
  entry->type = type;
  entry->op = op;
  entry->reply = reply;
  if (refs) {
    switch (entry->op) {
    case GF_FOP_READ:
      entry->refs = dict_ref (refs);
      break;
    }
  }
  conf = trans->xl_private;

#if 1
  /* TODO: This part is removed as it is observed that, with the queuing
   * method, there is a memory leak. Need to investigate further. Till then 
   * this code will be part of #if 0 */
  server_reply_queue (entry, conf->queue);
#else
  server_state_t *state = NULL;
  xlator_t *bound_xl = NULL;
  bound_xl = BOUND_XL (entry->frame);
  
  generic_reply (entry->frame, entry->type, entry->op, entry->reply);
  
  server_inode_prune (bound_xl);
  
  state = STATE (entry->frame);
  {
    if (entry->refs)
      dict_unref (entry->refs);
    dict_destroy (entry->reply);
    STACK_DESTROY (entry->frame->root);
    FREE (entry);
  }
  {
    transport_unref (state->trans);
    if (state->inode)
      inode_unref (state->inode);
    if (state->inode2)
      inode_unref (state->inode2);
    FREE (state);
  }
#endif
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
int32_t
server_lookup_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *stbuf,
		   dict_t *dict)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  inode_t *server_inode = NULL;
  inode_t *root_inode = NULL;

  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  if (op_ret == 0) {
    root_inode = BOUND_XL(frame)->itable->root;
    if (inode == root_inode) {
      /* we just looked up root ("/") */
      stbuf->st_ino = 1;
      if (!inode->st_mode)
	inode->st_mode = stbuf->st_mode;
    }

    if (!inode->ino) {
      server_inode = inode_update (BOUND_XL(frame)->itable, NULL, NULL, stbuf);
    
      if (server_inode != inode && (!server_inode->ctx)) {
	server_inode->ctx = inode->ctx;
	inode->ctx = NULL;
	if (op_ret >= 0) {
	  server_inode->st_mode = stbuf->st_mode;
	  server_inode->generation = inode->generation;
	}
      }

      inode_lookup (server_inode);
      inode_unref (server_inode);
    }

    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;

    if (dict) {
      int32_t len;
      char *dict_buf = NULL;
      dict_set (dict, "__@@protocol_client@@__key", str_to_data ("value"));
      len = dict_serialized_length (dict);
      dict_buf = calloc (len, 1);
      dict_serialize (dict, dict_buf);
      reply->fields[1].ptr = (void *)dict_buf;
      reply->fields[1].need_free = 1;
      reply->fields[1].len = len;
      reply->fields[1].type = GF_PROTO_CHAR_TYPE;
    }
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_LOOKUP,
		reply, frame->root->rsp_refs);

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
int32_t
server_lookup (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  loc_t loc = {0,};
  server_state_t *state = STATE (frame);
  int32_t need_xattr = params->common;

  loc.ino  = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.path = ((char *)params->fields[1].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  if (loc.inode) {
    /* revalidate */
    state->inode = loc.inode;
  } else {
    /* fresh lookup or inode was previously pruned out */
    state->inode = dummy_inode (bound_xl->itable);
    loc.inode = state->inode;    
  }

  STACK_WIND (frame,
	      server_lookup_cbk,
	      bound_xl,
	      bound_xl->fops->lookup,
	      &loc,
	      need_xattr);

  return 0;
}

/*
 * server_fchmod_cbk
 */
int32_t
server_fchmod_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }
  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FCHMOD, reply,
		frame->root->rsp_refs);

  return 0;
}

/*
 * server_fchmod
 *
 */
int32_t
server_fchmod (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{  
  fd_t *fd = NULL;
  mode_t mode = 0;
  server_proto_priv_t *priv = SERVER_PRIV (frame);
  int32_t fd_no = params->common;
 
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    struct stat buf = {0, };
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);
    server_fchmod_cbk (frame, NULL, frame->this, -1, EINVAL, &buf);
    return 0;
  } 

  STACK_WIND (frame, server_fchmod_cbk, bound_xl,
	      bound_xl->fops->fchmod, fd, mode);
  
  return 0;
}


/*
 * server_fchown_cbk 
 */
int32_t
server_fchown_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FCHOWN,
		reply, frame->root->rsp_refs);

  return 0;
}

/*
 * server_fchown
 *
 */
int32_t
server_fchown (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  uid_t uid = 0;
  gid_t gid = 0;
  fd_t *fd = NULL;
  server_proto_priv_t *priv = SERVER_PRIV (frame);
  int32_t fd_no = params->common;
 
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    struct stat buf = {0, };

    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);

    server_fchown_cbk (frame, NULL, frame->this, -1, EINVAL, &buf);
    return 0;
  }

  uid = ntohl (((int32_t *)params->fields[0].ptr)[0]);
  gid = ntohl (((int32_t *)params->fields[0].ptr)[1]);

  STACK_WIND (frame, server_fchown_cbk, bound_xl,
	      bound_xl->fops->fchown, fd, uid, gid);
  
  return 0;
}

/*
 * server_writedir_cbk - writedir callback for server protocol
 * @frame: call frame
 * @cookie: 
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 *
 * not for external reference
 */
int32_t
server_setdents_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  
  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_SETDENTS,
		reply, frame->root->rsp_refs);

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
int32_t
server_lk_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct flock *lock)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  
  if (op_ret >= 0) {
    int64_t *array = calloc (1, 5 * sizeof (int64_t));
    array[0] = gf_htonl_64 (lock->l_type);
    array[1] = gf_htonl_64 (lock->l_whence);
    array[2] = gf_htonl_64 (lock->l_start);
    array[3] = gf_htonl_64 (lock->l_len);
    array[4] = gf_htonl_64 (lock->l_pid);

    reply->fields[0].ptr = (void *)array;
    reply->fields[0].len = 5 * sizeof (int64_t);
    reply->fields[0].need_free = 1;
    reply->fields[0].type = GF_PROTO_INT64_TYPE;
  }
  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_LK,
		reply, frame->root->rsp_refs);

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
int32_t
server_access_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_ACCESS,
		reply, frame->root->rsp_refs);
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
int32_t
server_utimens_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_UTIMENS,
		reply, frame->root->rsp_refs);

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
int32_t
server_chmod_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_CHMOD,
		reply, frame->root->rsp_refs);

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
int32_t
server_chown_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_CHOWN,
		reply, frame->root->rsp_refs);

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
int32_t
server_rmdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_RMDIR,
		reply, frame->root->rsp_refs);

  return 0;
}

/*
 * server_rmelem_cbk - remove a directory entry (file, dir, link, symlink...)
 */
int32_t
server_rmelem_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_RMELEM,
		reply, frame->root->rsp_refs);

  return 0;
}

/*
 * server_incver_cbk - increment version of the directory trusted.afr.version
 */

int32_t
server_incver_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_INCVER, 
		reply, frame->root->rsp_refs);

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
int32_t
server_mkdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  inode_t *server_inode = NULL;
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  if (op_ret >= 0) {
    {
      server_inode = inode_update (BOUND_XL(frame)->itable,
				   NULL,
				   NULL,
				   stbuf);
      inode_lookup (server_inode);
      server_inode->ctx = inode->ctx;
      server_inode->generation = inode->generation;
      server_inode->st_mode = stbuf->st_mode;
      inode->ctx = NULL;
      inode_unref (inode);
      inode_unref (server_inode);
    }

    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_MKDIR,
		reply, frame->root->rsp_refs);

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
int32_t
server_mknod_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  inode_t *server_inode = NULL;
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  if (op_ret >= 0) {
    {
      server_inode = inode_update (BOUND_XL(frame)->itable,
				   NULL,
				   NULL,
				   stbuf);
      inode_lookup (server_inode);
      server_inode->ctx = inode->ctx;
      server_inode->generation = inode->generation;
      server_inode->st_mode = stbuf->st_mode;

      inode->ctx = NULL;
      inode_unref (inode);
      inode_unref (server_inode);
    }

    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_MKNOD,
		reply, frame->root->rsp_refs);

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
int32_t
server_fsyncdir_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FSYNCDIR,
		reply, frame->root->rsp_refs);

  return 0;
}


/*
 * server_getdents_cbk - getdents callback for server protocol
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
int32_t
server_getdents_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     dir_entry_t *entries,
		     int32_t count)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *buffer = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  
  if (op_ret >= 0) {

    reply->dummy1 = count;
    
    {   
      dir_entry_t *trav = entries->next;
      uint32_t len = 0;
      char *tmp_buf = NULL;
      while (trav) {
	len += strlen (trav->name);
	len += 1;
	len += strlen (trav->link);
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

	/* tmp_buf will have \n before \0 */
	this_len = sprintf (ptr, "%s/%s%s\n",
			    trav->name, tmp_buf,
			    trav->link);
	FREE (tmp_buf);
	trav = trav->next;
	ptr += this_len;
      }

      reply->fields[0].ptr = (void *)buffer;
      reply->fields[0].need_free = 1;
      reply->fields[0].len = strlen (buffer);
      reply->fields[0].type = GF_PROTO_CHAR_TYPE;
    }
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_GETDENTS,
		reply, frame->root->rsp_refs);

  return 0;
}


/*
 * server_readdir_cbk - getdents callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 * not for external reference
 */
int32_t
server_readdir_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     gf_dirent_t *entries)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  
  if (op_ret >= 0) {
    char *cpy = memdup (entries, op_ret);

    reply->fields[0].ptr = (void *)cpy;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = op_ret;
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_READDIR,
		reply, frame->root->rsp_refs);

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
int32_t
server_closedir_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  fd_t *fd = frame->local;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  frame->local = NULL;

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_CLOSEDIR,
		reply, frame->root->rsp_refs);
  
  if (fd)
    fd_destroy (fd);

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
int32_t
server_opendir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    fd_t *fd)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  if (op_ret >= 0) {
    server_proto_priv_t *priv = SERVER_PRIV (frame);
    
    reply->dummy1 = gf_fd_unused_get (priv->fdtable, fd);
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_OPENDIR,
		reply, frame->root->rsp_refs);

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
int32_t
server_statfs_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct statvfs *buf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

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
    
    reply->fields[0].ptr = (void *)strdup (buffer);
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (buffer);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_STATFS,
		reply, frame->root->rsp_refs);

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
int32_t
server_removexattr_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));  

  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_REMOVEXATTR,
		reply, frame->root->rsp_refs);

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
int32_t
server_getxattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     dict_t *dict)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  if (op_ret >= 0) {
    /* Serialize the dictionary and set it as a parameter in 'reply' dict */
    int32_t len = 0;
    char *dict_buf = NULL;

    dict_set (dict, "__@@protocol_client@@__key", str_to_data ("value"));
    len = dict_serialized_length (dict);
    dict_buf = calloc (len, 1);
    dict_serialize (dict, dict_buf);

    reply->fields[0].ptr = (void *)dict_buf;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = len;
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_GETXATTR,
		reply, frame->root->rsp_refs);

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
int32_t
server_setxattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_SETXATTR,
		reply, frame->root->rsp_refs);

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
int32_t
server_rename_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }
  
  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_RENAME,
		reply, frame->root->rsp_refs);

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
int32_t
server_unlink_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_UNLINK,
		reply, frame->root->rsp_refs);

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
int32_t
server_symlink_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  inode_t *server_inode = NULL;
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  if (op_ret >= 0) {
    {
      server_inode = inode_update (BOUND_XL(frame)->itable,
				   NULL,
				   NULL,
				   stbuf);
      inode_lookup (server_inode);
      server_inode->ctx = inode->ctx;
      inode->ctx = NULL;
      inode_unref (inode);
      inode_unref (server_inode);
    }

    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }
  
  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_SYMLINK,
		reply, frame->root->rsp_refs);

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
int32_t
server_link_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
    inode_lookup (inode);
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_LINK,
		reply, frame->root->rsp_refs);

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
int32_t
server_truncate_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_TRUNCATE,
		reply, frame->root->rsp_refs);

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
int32_t
server_fstat_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }
  
  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FSTAT,
		reply, frame->root->rsp_refs);

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
int32_t
server_ftruncate_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FTRUNCATE,
		reply, frame->root->rsp_refs);

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
int32_t
server_flush_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FLUSH,
		reply, frame->root->rsp_refs);

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
int32_t
server_fsync_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  
  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FSYNC,
		reply, frame->root->rsp_refs);

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
int32_t
server_close_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  fd_t *fd = frame->local;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  
  frame->local = NULL;

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_CLOSE,
		reply, frame->root->rsp_refs);
  
  if (fd)
    fd_destroy (fd);

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
int32_t
server_writev_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_WRITE,
		reply, frame->root->rsp_refs);

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
int32_t
server_readv_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct iovec *vector,
		  int32_t count,
		  struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].need_free = 1;
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;

    reply->fields[1].ptr = iov_dup (vector, count);
    reply->fields[1].len = count;
    //reply->fields[0].need_free = 1; /* Don't uncomment, as this buffer gets freed */
    reply->fields[1].type = GF_PROTO_IOV_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_READ,
		reply, frame->root->rsp_refs);

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
 *
 * not for external reference
 */ 
int32_t
server_open_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 fd_t *fd)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  if (op_ret >= 0) {
    server_proto_priv_t *priv = SERVER_PRIV (frame);
    reply->dummy1 = gf_fd_unused_get (priv->fdtable, fd);
  }
  
  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_OPEN,
		reply, frame->root->rsp_refs);

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
int32_t
server_create_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd,
		   inode_t *inode,
		   struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  inode_t *server_inode = NULL;
  int32_t fd_no = -1;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  if (op_ret >= 0) {
    server_proto_priv_t *priv = NULL;

    priv = SERVER_PRIV (frame);

    server_inode = inode_update (BOUND_XL(frame)->itable, NULL, NULL, stbuf);
    
    {
      server_inode->ctx = inode->ctx;
      inode_lookup (server_inode);
      inode->ctx = NULL;
      
      list_del (&fd->inode_list);
      
      LOCK (&server_inode->lock);
      {
	list_add (&fd->inode_list, &server_inode->fds);
	inode_unref (fd->inode);
	inode_unref (inode);
	fd->inode = inode_ref (server_inode);
      }
      UNLOCK (&server_inode->lock);
    }

    inode_unref (server_inode);
    fd_no = gf_fd_unused_get (priv->fdtable, fd);
    if (fd_no < 0 || fd == 0) {
      op_ret = fd_no;
      op_errno = gf_errno_to_error (errno);
    }

    reply->dummy1 = fd_no;
    
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }
  
  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_CREATE,
		reply, frame->root->rsp_refs);

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
int32_t
server_readlink_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     const char *buf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {    
    reply->fields[0].ptr = (void *)(buf?strdup (buf):strdup (""));
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (buf);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_READLINK,
		reply, frame->root->rsp_refs);

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

int32_t
server_stat_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char *stat_str = NULL;
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  if (op_ret >= 0) {
    stat_str = stat_to_str (stbuf);
    reply->fields[0].ptr = (void *)stat_str;
    reply->fields[0].need_free = 1;
    reply->fields[0].len = strlen (stat_str);
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_STAT,
		reply, frame->root->rsp_refs);

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
int32_t
server_forget_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FORGET,
		reply, frame->root->rsp_refs);

  return 0;
}


/*
 * server_stub_cbk - this is callback function used whenever an fop does
 *                   STACK_WIND to fops->lookup in order to lookup the inode
 *                   for a pathname. this case of doing fops->lookup arises
 *                   when fop searches in inode table for pathname and search
 *                   fails.
 *
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
int32_t
server_stub_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *stbuf,
		 dict_t *dict)
{
  /* TODO: should inode pruning be done here or not??? */
  inode_t *server_inode = NULL;

  server_inode = inode_update (BOUND_XL(frame)->itable, NULL, NULL, stbuf);
  
  if (server_inode != inode) {
    server_inode->ctx = inode->ctx;
    server_inode->st_mode = stbuf->st_mode;
    server_inode->generation = inode->generation;
    inode->ctx = NULL;
  }
  
  if (server_inode) {
    inode_lookup (server_inode);
    inode_unref (server_inode);
  }
  
  inode_unref (inode);

  if (frame->local) {
    /* we have a call stub to wind to */
    call_stub_t *stub = (call_stub_t *)frame->local;
    
    if (stub->fop != GF_FOP_RENAME)
      /* to make sure that STACK_DESTROY() does not try to free 
       * frame->local. frame->local points to call_stub_t, which is
       * free()ed in call_resume(). */
      frame->local = NULL;
#if 0   
    /* classic bug.. :O, intentionally left for sweet memory.
     *                                              --benki
     */
    if (op_ret < 0) {
      if (stub->fop != GF_FOP_RENAME) {
	/* STACK_UNWIND helps prevent memory leak. how?? */
	gf_log (frame->this->name, GF_LOG_ERROR, 
		"returning ENOENT");

	STACK_UNWIND (stub->frame, -1, ENOENT, 0, 0);
	FREE (stub);
	return 0;
      }
    }
#endif

    switch (stub->fop)
      {
      case GF_FOP_RENAME:
	if (!stub->args.rename.old.inode) {
	  loc_t *newloc = NULL;
	  /* now we are called by lookup of oldpath. */
	  if (op_ret < 0) {
	    /* to make sure that STACK_DESTROY() does not try to free 
	     * frame->local. frame->local points to call_stub_t, which is
	     * free()ed in call_resume(). */
	    frame->local = NULL;
	    
	    /* lookup of oldpath failed, UNWIND to server_rename_cbk with
	     * ret=-1 and errno=ENOENT */
	    server_rename_cbk (stub->frame,
			       NULL,
			       stub->frame->this,
			       -1,
			       ENOENT,
			       NULL);
	    
	    FREE (stub->args.rename.old.path);
	    FREE (stub->args.rename.new.path);
	    FREE (stub);
	    return 0;
	  }
	  
	  /* store inode information of oldpath in our stub and search for 
	   * newpath in inode table. 
	   * inode_ref()ed because, we might do a STACK_WIND to fops->lookup()
	   * again to lookup for newpath */
	  stub->args.rename.old.inode = inode_ref (server_inode);
	  stub->args.rename.old.ino = stbuf->st_ino;
	  
	  /* now lookup for newpath */
	  newloc = &stub->args.rename.new;
	  newloc->inode = inode_search (BOUND_XL(frame)->itable, 
					newloc->ino,
					NULL);
	  
	  if (!newloc->inode) {
	    /* lookup for newpath */
	    newloc->inode = dummy_inode (BOUND_XL(frame)->itable);
	    STACK_WIND (stub->frame,
			server_stub_cbk,
			BOUND_XL (stub->frame),
			BOUND_XL (stub->frame)->fops->lookup,
			newloc,
			0);
	    
	    break;
	  } else {
	    /* found newpath in inode cache */
	    
	    /* to make sure that STACK_DESTROY() does not try to free 
	     * frame->local. frame->local points to call_stub_t, which is
	     * free()ed in call_resume(). */
	    frame->local = NULL;
	    call_resume (stub);
	    break;
	  }
	} else {
	  /* we are called by the lookup of newpath */
	  
	  /* to make sure that STACK_DESTROY() does not try to free 
	   * frame->local. frame->local points to call_stub_t, which is
	   * free()ed in call_resume(). */
	  frame->local = NULL;
	  
	  if (server_inode) {	  
	    stub->args.rename.new.inode = inode_ref (server_inode);
	    stub->args.rename.new.ino = stbuf->st_ino;
	  }
	}      
	
	/* after looking up for oldpath as well as newpath, 
	 * we are ready to resume */
	{
	  call_resume (stub);
	}
	break;
      case GF_FOP_OPEN:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    
	    server_open_cbk (stub->frame,
			     NULL,
			     stub->frame->this,
			     -1,
			     ENOENT,
			     NULL);
	    FREE (stub->args.open.loc.path);
	    FREE (stub);
	    return 0;
	  }
    	  stub->args.open.loc.inode = inode_ref (server_inode);
	  stub->args.open.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}
      case GF_FOP_STAT:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_stat_cbk (stub->frame,
			     NULL,
			     stub->frame->this,
			     -1,
			     ENOENT,
			     NULL);
	    FREE (stub->args.stat.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  /* TODO: reply from here only, we already have stat structure */
	  stub->args.stat.loc.inode = inode_ref (server_inode);
	  stub->args.stat.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}
	
      case GF_FOP_UNLINK:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_unlink_cbk (stub->frame,
			       NULL,
			       stub->frame->this,
			       -1,
			       ENOENT);
	    FREE (stub->args.unlink.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.unlink.loc.inode = inode_ref (server_inode);
	  stub->args.unlink.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}
	
      case GF_FOP_RMDIR:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_rmdir_cbk (stub->frame,
			       NULL,
			       stub->frame->this,
			       -1,
			       ENOENT);
	    FREE (stub->args.rmdir.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.rmdir.loc.inode = inode_ref (server_inode);
	  stub->args.rmdir.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}
	
      case GF_FOP_CHMOD:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_chmod_cbk (stub->frame,
			      NULL,
			      stub->frame->this,
			      -1,
			      ENOENT,
			      NULL);
	    FREE (stub->args.chmod.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.chmod.loc.inode = inode_ref (server_inode);
	  stub->args.chmod.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}
      case GF_FOP_CHOWN:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_chown_cbk (stub->frame,
			      NULL,
			      stub->frame->this,
			      -1,
			      ENOENT,
			      NULL);
	    FREE (stub->args.chown.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.chown.loc.inode = inode_ref (server_inode);
	  stub->args.chown.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}

      case GF_FOP_LINK:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_link_cbk (stub->frame,
			     NULL,
			     stub->frame->this,
			     -1,
			     ENOENT,
			     NULL,
			     NULL);
	    FREE (stub->args.link.oldloc.path);
	    FREE (stub->args.link.newpath);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.link.oldloc.inode = inode_ref (server_inode);
	  stub->args.link.oldloc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}

      case GF_FOP_TRUNCATE:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_truncate_cbk (stub->frame,
				 NULL,
				 stub->frame->this,
				 -1,
				 ENOENT,
				 NULL);
	    FREE (stub->args.truncate.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.truncate.loc.inode = inode_ref (server_inode);
	  stub->args.truncate.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}
	
      case GF_FOP_STATFS:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_statfs_cbk (stub->frame,
			       NULL,
			       stub->frame->this,
			       -1,
			       ENOENT,
			       NULL);
	    FREE (stub->args.statfs.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.statfs.loc.inode = inode_ref (server_inode);
	  stub->args.statfs.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}
	
      case GF_FOP_SETXATTR:
	{
	  dict_t *dict = stub->args.setxattr.dict;
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_setxattr_cbk (stub->frame,
				 NULL,
				 stub->frame->this,
				 -1,
				 ENOENT);
	    FREE (stub->args.setxattr.loc.path);
	    dict_destroy (dict);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.setxattr.loc.inode = inode_ref (server_inode);
	  stub->args.setxattr.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  //	  dict_destroy (dict);
	  break;
	}
	
      case GF_FOP_GETXATTR:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_getxattr_cbk (stub->frame,
				 NULL,
				 stub->frame->this,
				 -1,
				 ENOENT,
				 NULL);
	    FREE (stub->args.getxattr.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.getxattr.loc.inode = inode_ref (server_inode);
	  stub->args.getxattr.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}
	
      case GF_FOP_REMOVEXATTR:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_removexattr_cbk (stub->frame,
				    NULL,
				    stub->frame->this,
				    -1,
				    ENOENT);
	    FREE (stub->args.removexattr.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.removexattr.loc.inode = inode_ref (server_inode);
	  stub->args.removexattr.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}
	
      case GF_FOP_OPENDIR:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_opendir_cbk (stub->frame,
				NULL,
				stub->frame->this,
				-1,
				ENOENT,
				NULL);
	    FREE (stub->args.opendir.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.opendir.loc.inode = inode_ref (server_inode);
	  stub->args.opendir.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}
	
      case GF_FOP_ACCESS:
	{
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_access_cbk (stub->frame,
			       NULL,
			       stub->frame->this,
			       -1,
			       ENOENT);
	    FREE (stub->args.access.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.access.loc.inode = inode_ref (server_inode);
	  stub->args.access.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}
	
	
      case GF_FOP_UTIMENS:
	{	  
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_utimens_cbk (stub->frame,
				NULL,
				stub->frame->this,
				-1,
				ENOENT,
				NULL);
	    FREE (stub->args.utimens.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.utimens.loc.inode = inode_ref (server_inode);
	  stub->args.utimens.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}

      case GF_FOP_READLINK:
	{	  
	  if (op_ret < 0) {
	    gf_log (frame->this->name, GF_LOG_ERROR, 
		    "returning ENOENT: %d (%d)", op_ret, op_errno);
	    server_readlink_cbk (stub->frame,
				 NULL,
				 stub->frame->this,
				 -1,
				 ENOENT,
				 NULL);
	    FREE (stub->args.readlink.loc.path);
	    FREE (stub);
	    return 0;
	  }

	  stub->args.utimens.loc.inode = inode_ref (server_inode);
	  stub->args.utimens.loc.ino = stbuf->st_ino;
	  call_resume (stub);
	  break;
	}
	
      default:
	call_resume (stub);
      }
  } 

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
int32_t
server_forget (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  inode_t *inode = NULL;

  inode = inode_search (bound_xl->itable, 
			gf_ntohl_64_ptr (params->fields[0].ptr),
			NULL);

  if (inode) {
    inode_forget (inode, 0);
    inode_unref (inode);
  }

  server_forget_cbk (frame, NULL, bound_xl, 0, 0);

  return 0;
}



int32_t
server_stat_resume (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (loc->inode);

  STACK_WIND (frame,
	      server_stat_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->stat,
	      loc);
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

int32_t
server_stat (call_frame_t *frame,
	     xlator_t *bound_xl,
	     gf_args_request_t *params)
{
  loc_t loc = {0,};
  call_stub_t *stat_stub = NULL;

  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  stat_stub = fop_stat_stub (frame, server_stat_resume, &loc);

  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = stat_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame, server_stub_cbk, bound_xl, 
		bound_xl->fops->lookup, &loc, 0);

  } else {
    call_resume (stat_stub);
  }

  return 0;
}


int32_t
server_readlink_resume (call_frame_t *frame,
			xlator_t *this,
			loc_t *loc,
			size_t size)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (loc->inode);

  STACK_WIND (frame,
	      server_readlink_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->readlink,
	      loc,
	      size);
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

int32_t
server_readlink (call_frame_t *frame,
		 xlator_t *bound_xl,
		 gf_args_request_t *params)
{
  call_stub_t *readlink_stub = NULL;
  int32_t len = params->common;
  loc_t loc = {0,};

  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  readlink_stub = fop_readlink_stub (frame, 
				     server_readlink_resume,
				     &loc, len);
  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = readlink_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);

  } else {
    call_resume (readlink_stub);
  }

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
int32_t
server_create (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  int32_t flags = params->common;
  fd_t *fd = NULL;
  mode_t mode = 0;
  loc_t loc = {0,};

  mode = ntohl (((int32_t *)params->fields[0].ptr)[0]);
  loc.path = (char *)params->fields[1].ptr;
  loc.inode = dummy_inode (bound_xl->itable);
  fd = fd_create (loc.inode);

  LOCK (&fd->inode->lock);
  {
    list_del_init (&fd->inode_list);
  }
  UNLOCK (&fd->inode->lock);

  STACK_WIND (frame, server_create_cbk, bound_xl,
	      bound_xl->fops->create, &loc,
	      flags, mode, fd);

  return 0;
}


int32_t
server_open_resume (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    int32_t flags,
		    fd_t *fd)
{
  server_state_t *state = STATE (frame);
  fd_t *new_fd = NULL;

  state->inode = inode_ref (loc->inode);
  
  new_fd = fd_create (loc->inode);

  STACK_WIND (frame, server_open_cbk, BOUND_XL (frame),
	      BOUND_XL (frame)->fops->open, loc, flags, new_fd);

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
int32_t
server_open (call_frame_t *frame,
	     xlator_t *bound_xl,
	     gf_args_request_t *params)
{
  int32_t flags = params->common;
  loc_t loc = {0,};
  call_stub_t *open_stub = NULL;

  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  open_stub = fop_open_stub (frame, server_open_resume,
			     &loc, flags, NULL);

  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = open_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);
    
  } else {
    /* we are fine with everything, go ahead with open of our child */
    call_resume (open_stub);
  }

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
int32_t
server_readv (call_frame_t *frame,
	      xlator_t *bound_xl,
	      gf_args_request_t *params)
{
  int32_t fd_no = params->common;
  size_t size = gf_ntohl_64_ptr (params->fields[0].ptr);
  off_t offset = gf_ntohl_64_ptr (params->fields[0].ptr + 8);
  server_proto_priv_t *priv = SERVER_PRIV (frame);
  fd_t *fd = NULL;

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);
    server_readv_cbk (frame, NULL, frame->this, -1,
		      EINVAL, NULL, 0, NULL);
    return 0;
  }
  
  STACK_WIND (frame, server_readv_cbk, bound_xl,
	      bound_xl->fops->readv, fd, size, offset);

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
int32_t
server_writev (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  struct iovec iov;
  size_t len = gf_ntohl_64_ptr (params->fields[0].ptr);
  off_t offset = gf_ntohl_64_ptr (params->fields[0].ptr + 8);
  server_proto_priv_t *priv = SERVER_PRIV (frame);
  fd_t *fd = NULL;
  int32_t fd_no = params->common;
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);
    server_writev_cbk (frame, NULL, frame->this, -1, EINVAL, NULL);
    return 0;
  }

  iov.iov_base = params->fields[1].ptr;
  iov.iov_len = len;
  
  STACK_WIND (frame,  server_writev_cbk,  bound_xl,  bound_xl->fops->writev,
	      fd, &iov, 1, offset);

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
int32_t
server_close (call_frame_t *frame,
	      xlator_t *bound_xl,
	      gf_args_request_t *params)
{
  fd_t *fd = NULL;
  server_proto_priv_t *priv = SERVER_PRIV (frame);
  int32_t fd_no = params->common;

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);

    server_close_cbk (frame, NULL, frame->this, -1, EINVAL);
    return 0;
  }
  
  frame->local = fd;
  gf_fd_put (priv->fdtable, fd_no);
  STACK_WIND (frame, server_close_cbk, bound_xl,
	      bound_xl->fops->close, fd);

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
int32_t
server_fsync (call_frame_t *frame,
	      xlator_t *bound_xl,
	      gf_args_request_t *params)
{
  fd_t *fd = NULL;
  int32_t fd_no = params->common;
  int32_t flag = ntohl (((int32_t *)params->fields[0].ptr)[0]);
  server_proto_priv_t *priv = SERVER_PRIV (frame);

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);

    server_fsync_cbk (frame, NULL, frame->this, -1, EINVAL);
    return 0;
  }
  
  STACK_WIND (frame, server_fsync_cbk, bound_xl,
	      bound_xl->fops->fsync, fd, flag);

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
int32_t
server_flush (call_frame_t *frame,
	      xlator_t *bound_xl,
	      gf_args_request_t *params)
{
  fd_t *fd = NULL;
  int32_t fd_no = params->common;
  server_proto_priv_t *priv = SERVER_PRIV (frame);

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);

    server_flush_cbk (frame, NULL, frame->this, -1, EINVAL);
    return 0;
  }

  STACK_WIND (frame, server_flush_cbk, bound_xl,
	      bound_xl->fops->flush, fd);

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
int32_t
server_ftruncate (call_frame_t *frame,
		  xlator_t *bound_xl,
		  gf_args_request_t *params)
{
  fd_t *fd = NULL;
  int32_t fd_no = params->common;
  off_t offset = gf_ntohl_64_ptr (params->fields[0].ptr);
  server_proto_priv_t *priv = SERVER_PRIV (frame);

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);

    server_ftruncate_cbk (frame, NULL, frame->this, -1, EINVAL, NULL);
    return 0;
  }
  
  STACK_WIND (frame, 
	      server_ftruncate_cbk, 
	      bound_xl,
	      bound_xl->fops->ftruncate,
	      fd, offset);

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
int32_t
server_fstat (call_frame_t *frame,
	      xlator_t *bound_xl,
	      gf_args_request_t *params)
{
  fd_t *fd = NULL; 
  int32_t fd_no = params->common;
  server_proto_priv_t *priv = SERVER_PRIV (frame);

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);

    server_fstat_cbk (frame, NULL, frame->this, -1, EINVAL, NULL);
    return 0;
  }
  
  STACK_WIND (frame, 
	      server_fstat_cbk, 
	      bound_xl,
	      bound_xl->fops->fstat,
	      fd);

  return 0;
}


int32_t
server_truncate_resume (call_frame_t *frame,
			xlator_t *this,
			loc_t *loc,
			off_t offset)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (loc->inode);

  STACK_WIND (frame,
	      server_truncate_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->truncate,
	      loc,
	      offset);
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
int32_t
server_truncate (call_frame_t *frame,
		 xlator_t *bound_xl,
		 gf_args_request_t *params)
{
  off_t offset = gf_ntohl_64_ptr (params->fields[0].ptr+8);
  loc_t loc = {0,};

  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  call_stub_t *truncate_stub = fop_truncate_stub (frame, 
						  server_truncate_resume,
						  &loc,
						  offset);

  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = truncate_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame, server_stub_cbk,	bound_xl,
		bound_xl->fops->lookup,	&loc, 0);

  } else {
    call_resume (truncate_stub);
  }

  return 0;
}



int32_t
server_link_resume (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *oldloc,
		    const char *newpath)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (oldloc->inode);

  STACK_WIND (frame,
	      server_link_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->link,
	      oldloc,
	      newpath);
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
int32_t
server_link (call_frame_t *frame,
	     xlator_t *bound_xl,
	     gf_args_request_t *params)
{
  loc_t oldloc = {0,};
  char *newpath = (char *)params->fields[2].ptr;
  
  oldloc.path = (char *)params->fields[1].ptr;
  oldloc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  oldloc.inode = inode_search (bound_xl->itable, 
			       oldloc.ino, 
			       NULL);

  call_stub_t *link_stub = fop_link_stub (frame, 
					  server_link_resume,
					  &oldloc,
					  newpath);
  if (oldloc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (oldloc.inode);
  }
  
  if (!oldloc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = link_stub;
    oldloc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&oldloc,
		0);
    
  } else {
    call_resume (link_stub);
  }

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

int32_t
server_symlink (call_frame_t *frame,
		xlator_t *bound_xl,
		gf_args_request_t *params)
{
  char *link = (char *)params->fields[0].ptr;
  loc_t loc = {0,};

  loc.inode = dummy_inode (bound_xl->itable);
  loc.path = (char *)params->fields[1].ptr;
  
  STACK_WIND (frame, server_symlink_cbk, bound_xl,
	      bound_xl->fops->symlink, link, &loc);

  return 0;
}


int32_t
server_unlink_resume (call_frame_t *frame,
		      xlator_t *this,
		      loc_t *loc)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (loc->inode);

  STACK_WIND (frame,
	      server_unlink_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->unlink,
	      loc);
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

int32_t
server_unlink (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  loc_t loc = {0,};
  call_stub_t *unlink_stub = NULL;

  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  unlink_stub = fop_unlink_stub (frame, server_unlink_resume, &loc);
  
  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = unlink_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);

  } else {
    call_resume (unlink_stub);
  }

  return 0;
}



int32_t
server_rename_resume (call_frame_t *frame,
		      xlator_t *this,
		      loc_t *oldloc,
		      loc_t *newloc)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (oldloc->inode);

  if (newloc->inode)
    state->inode2 = inode_ref (newloc->inode);

  STACK_WIND (frame,
	      server_rename_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->rename,
	      oldloc,
	      newloc);
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

int32_t
server_rename (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  loc_t oldloc = {0,};
  loc_t newloc = {0,};
  call_stub_t *rename_stub = NULL;

  oldloc.path = (char *)params->fields[1].ptr;
  newloc.path = (char *)params->fields[2].ptr;

  oldloc.ino =  gf_ntohl_64_ptr (params->fields[0].ptr);
  oldloc.inode = inode_search (bound_xl->itable, oldloc.ino, NULL);

  newloc.ino = gf_ntohl_64_ptr (params->fields[0].ptr + 8);
  newloc.inode = inode_search (bound_xl->itable, newloc.ino, NULL);

  /* :O
     frame->this = bound_xl;
  */
  rename_stub = fop_rename_stub (frame, server_rename_resume,
				 &oldloc, &newloc);
  if (oldloc.inode)
    inode_unref (oldloc.inode);
  
  if (newloc.inode)
    inode_unref (newloc.inode);

  frame->local = rename_stub;
  
  if (!oldloc.inode){
    /*    search of oldpath in inode cache _failed_.
     *    we need to do a lookup for oldpath. we do a fops->lookup() for 
     * oldpath. call-back being server_stub_cbk(). server_stub_cbk() takes
     * care of searching/lookup of newpath, if it already exists. 
     * server_stub_cbk() resumes to fops->rename(), after trying to lookup 
     * for newpath also.
     *    if lookup of oldpath fails, server_stub_cbk() UNWINDs to 
     * server_rename_cbk() with ret=-1 and errno=ENOENT.
     */
    oldloc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&oldloc,
		0);
  } else if (!newloc.inode){
    /* inode for oldpath found in inode cache and search for newpath in inode
     * cache_failed_.
     * we need to lookup for newpath, with call-back being server_stub_cbk().
     * since we already have found oldpath in inode cache, server_stub_cbk()
     * continues with fops->rename(), irrespective of success or failure of
     * lookup for newpath.
     */
    newloc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&newloc,
		0);
  } else {
    /* we have found inode for both oldpath and newpath in inode cache.
     * we are continue with fops->rename() */

    frame->local = NULL;

    call_resume (rename_stub);
  }

  return 0;
}


int32_t
server_setxattr_resume (call_frame_t *frame,
			xlator_t *this,
			loc_t *loc,
			dict_t *dict,
			int32_t flags)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (loc->inode);

  STACK_WIND (frame,
	      server_setxattr_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->setxattr,
	      loc,
	      dict,
	      flags);
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

int32_t
server_setxattr (call_frame_t *frame,
		 xlator_t *bound_xl,
		 gf_args_request_t *params)
{
  int32_t flags = params->common;
  loc_t loc = {0,};
  dict_t *dict = NULL;

  {
    /* Unserialize the dictionary */
    char *buf = memdup (params->fields[2].ptr, params->fields[2].len);
    dict = get_new_dict ();
    dict_unserialize (buf, params->fields[2].len, &dict);
    dict->extra_free = buf;
  }
  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);

  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  call_stub_t *setxattr_stub = fop_setxattr_stub (frame, 
						  server_setxattr_resume,
						  &loc,
						  dict,
						  flags);

  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = setxattr_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);

  } else {
    call_resume (setxattr_stub);
  }

  return 0;
}



int32_t
server_getxattr_resume (call_frame_t *frame,
			xlator_t *this,
			loc_t *loc,
			const char *name)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (loc->inode);

  STACK_WIND (frame,
	      server_getxattr_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->getxattr,
	      loc,
	      name);
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

int32_t
server_getxattr (call_frame_t *frame,
		 xlator_t *bound_xl,
		 gf_args_request_t *params)
{
  char *name = NULL;
  loc_t loc = {0,};
  call_stub_t *getxattr_stub = NULL;

  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  if (params->fields[2].len) 
    name = params->fields[2].ptr;

  getxattr_stub = fop_getxattr_stub (frame, 
				     server_getxattr_resume,
				     &loc,
				     name);

  if (loc.inode) {
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = getxattr_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);

  } else {
    call_resume (getxattr_stub);
  }

  return 0;
}



int32_t
server_removexattr_resume (call_frame_t *frame,
			   xlator_t *this,
			   loc_t *loc,
			   const char *name)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (loc->inode);

  STACK_WIND (frame,
	      server_removexattr_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->removexattr,
	      loc,
	      name);
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
int32_t
server_removexattr (call_frame_t *frame,
		    xlator_t *bound_xl,
		    gf_args_request_t *params)
{
  char *name = NULL;
  loc_t loc = {0,};

  name = params->fields[2].ptr;
  
  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  call_stub_t *removexattr_stub = fop_removexattr_stub (frame, 
							server_removexattr_resume,
							&loc,
							name);

  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = removexattr_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);

  } else {
    call_resume (removexattr_stub);
  }

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
int32_t
server_statfs (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  loc_t loc = {0,};

  loc.path = (char *)params->fields[0].ptr;

  {
    inode_t tmp_inode = {
      .ino = 1,
    };
    
    /* no one should refer to inode in statfs call, so send a &tmp inode 
     * if inode_search failed.
     */
    if (!loc.inode) {
      loc.inode = &tmp_inode;
    }
    STACK_WIND (frame,
		server_statfs_cbk,
		BOUND_XL (frame),
		BOUND_XL (frame)->fops->statfs,
		&loc);
  }

  return 0;
}



int32_t
server_opendir_resume (call_frame_t *frame,
		       xlator_t *this,
		       loc_t *loc,
		       fd_t *fd)
{
  server_state_t *state = STATE (frame);
  fd_t *new_fd = NULL;

  state->inode = inode_ref (loc->inode);
  new_fd = fd_create (loc->inode);

  STACK_WIND (frame,
	      server_opendir_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->opendir,
	      loc,
	      new_fd);
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
int32_t
server_opendir (call_frame_t *frame,
		xlator_t *bound_xl,
		gf_args_request_t *params)
{
  loc_t loc = {0,};
  call_stub_t *opendir_stub = NULL;

  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  
  opendir_stub = fop_opendir_stub (frame, 
				   server_opendir_resume,
				   &loc,
				   NULL);
  
  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = opendir_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);
  } else {
    call_resume (opendir_stub);
  }

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
int32_t
server_closedir (call_frame_t *frame,
		 xlator_t *bound_xl,
		 gf_args_request_t *params)
{
  fd_t *fd = NULL;
  int32_t fd_no = params->common;
  server_proto_priv_t *priv = SERVER_PRIV (frame);

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d", fd_no);
    gf_log (frame->this->name, GF_LOG_ERROR, 
	    "not getting enough data, returning EINVAL");
    server_closedir_cbk (frame,
			 NULL,
			 frame->this,
			 -1,
			 EINVAL);
    return 0;
  }

  frame->local = fd;

  gf_fd_put (priv->fdtable, fd_no);
  STACK_WIND (frame, 
	      server_closedir_cbk, 
	      bound_xl,
	      bound_xl->fops->closedir,
	      fd);

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
int32_t
server_getdents (call_frame_t *frame,
		 xlator_t *bound_xl,
		 gf_args_request_t *params)
{

  size_t size = gf_ntohl_64_ptr (params->fields[0].ptr);
  off_t offset = gf_ntohl_64_ptr (params->fields[0].ptr + 8);
  int32_t flag = gf_ntohl_64_ptr (params->fields[0].ptr + 16);
  server_proto_priv_t *priv = SERVER_PRIV (frame);
  fd_t *fd = NULL; 
  int32_t fd_no = params->common;

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);
    server_getdents_cbk (frame, NULL, frame->this, -1,
			 EINVAL, NULL, 0);
    return 0;
  }

  STACK_WIND (frame, 
	      server_getdents_cbk, 
	      bound_xl,
	      bound_xl->fops->getdents,
	      fd,
	      size, offset, flag);

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
int32_t
server_readdir (call_frame_t *frame,
		xlator_t *bound_xl,
		gf_args_request_t *params)
{
  server_proto_priv_t *priv = SERVER_PRIV (frame);
  fd_t *fd = NULL; 
  int32_t fd_no = params->common;
  size_t size = gf_ntohl_64_ptr (params->fields[0].ptr);
  off_t offset = gf_ntohl_64_ptr (params->fields[0].ptr + 8);

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);

    server_readdir_cbk (frame,	NULL, frame->this,
			 -1, EINVAL, NULL);
    return 0;
  }

  STACK_WIND (frame, 
	      server_readdir_cbk, 
	      bound_xl,
	      bound_xl->fops->readdir,
	      fd,
	      size,
	      offset);
  
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
int32_t
server_fsyncdir (call_frame_t *frame,
		 xlator_t *bound_xl,
		 gf_args_request_t *params)
{
  int32_t flag = ntohl (((int32_t *)params->fields[0].ptr)[0]);
  server_proto_priv_t *priv = SERVER_PRIV (frame);
  fd_t *fd = NULL;
  int32_t fd_no = params->common;

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);

    server_fsyncdir_cbk (frame, NULL, frame->this, -1, EINVAL);
    return 0;
  }

  STACK_WIND (frame, 
	      server_fsyncdir_cbk, 
	      bound_xl,
	      bound_xl->fops->fsyncdir,
	      fd, flag);

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
int32_t
server_mknod (call_frame_t *frame,
	      xlator_t *bound_xl,
	      gf_args_request_t *params)
{
  dev_t dev = gf_ntohl_64_ptr (params->fields[0].ptr);
  mode_t mode = params->common;
  loc_t loc = {0,};

  loc.inode = dummy_inode (bound_xl->itable);
  loc.path =  (char *)params->fields[1].ptr;

  STACK_WIND (frame, 
	      server_mknod_cbk, 
	      bound_xl,
	      bound_xl->fops->mknod,
	      &loc, mode, dev);

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
int32_t
server_mkdir (call_frame_t *frame,
	      xlator_t *bound_xl,
	      gf_args_request_t *params)
{
  mode_t mode = params->common;
  loc_t loc = {0,};

  loc.inode = dummy_inode (bound_xl->itable);
  loc.path = (char *)params->fields[0].ptr;

  STACK_WIND (frame, 
	      server_mkdir_cbk, 
	      bound_xl,
	      bound_xl->fops->mkdir,
	      &loc, mode);

  return 0;
}


int32_t
server_rmdir_resume (call_frame_t *frame,
		     xlator_t *this,
		     loc_t *loc)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (loc->inode);
  
  STACK_WIND (frame,
	      server_rmdir_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->rmdir,
	      loc);
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
int32_t
server_rmdir (call_frame_t *frame,
	      xlator_t *bound_xl,
	      gf_args_request_t *params)
{
  loc_t loc = {0,};

  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  call_stub_t *rmdir_stub = fop_rmdir_stub (frame, 
					    server_rmdir_resume,
					    &loc);
  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = rmdir_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);
  } else {
    call_resume (rmdir_stub);
  }

  return 0;
}

/*
 * server_rmelem - remove directory entry - file, dir, links etc
 */

int32_t
server_rmelem (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  char *path = (char *)params->fields[0].ptr;
  STACK_WIND (frame,
	      server_rmelem_cbk,
	      bound_xl,
	      bound_xl->fops->rmelem,
	      path);

  return 0;
}

/*
 * server_incver - increment version of the directory - trusted.afr.version ext attr
 */

int32_t
server_incver (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  char *path = (char *)params->fields[0].ptr;

  STACK_WIND (frame,
	      server_incver_cbk,
	      bound_xl,
	      bound_xl->fops->incver,
	      path);

  return 0;
}

int32_t
server_chown_resume (call_frame_t *frame,
		     xlator_t *this,
		     loc_t *loc,
		     uid_t uid,
		     gid_t gid)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (loc->inode);

  STACK_WIND (frame, server_chown_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->chown,
	      loc, uid, gid);
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
int32_t
server_chown (call_frame_t *frame,
	      xlator_t *bound_xl,
	      gf_args_request_t *params)
{
  uid_t uid = 0;
  gid_t gid = 0;
  loc_t loc = {0,};
  
  uid = ntohl (((int32_t *)params->fields[2].ptr)[0]);
  gid = ntohl (((int32_t *)params->fields[2].ptr)[1]);

  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  call_stub_t *chown_stub = fop_chown_stub (frame, 
					    server_chown_resume,
					    &loc,
					    uid,
					    gid);
  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }


  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = chown_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);

  } else {
    call_resume (chown_stub);
  }

  return 0;
}


int32_t 
server_chmod_resume (call_frame_t *frame,
		     xlator_t *this,
		     loc_t *loc,
		     mode_t mode)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (loc->inode);

  STACK_WIND (frame,
	      server_chmod_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->chmod,
	      loc,
	      mode);
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
int32_t
server_chmod (call_frame_t *frame,
	      xlator_t *bound_xl,
	      gf_args_request_t *params)
{
  mode_t mode = params->common;
  loc_t loc = {0,};
  
  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  call_stub_t *chmod_stub = fop_chmod_stub (frame, 
					    server_chmod_resume,
					    &loc,
					    mode);

  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = chmod_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);

  } else {
    call_resume (chmod_stub);
  }

  return 0;
}


int32_t 
server_utimens_resume (call_frame_t *frame,
		       xlator_t *this,
		       loc_t *loc,
		       struct timespec *tv)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (loc->inode);

  STACK_WIND (frame,
	      server_utimens_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->utimens,
	      loc,
	      tv);
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
int32_t
server_utimens (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  struct timespec buf[2] = {{0,}, {0,}};
  loc_t loc = {0,};
  int32_t *array = (int32_t *)params->fields[2].ptr;

  buf[0].tv_sec  = ntohl (array[0]);
  buf[0].tv_nsec = ntohl (array[1]);
  buf[1].tv_sec  = ntohl (array[2]);
  buf[1].tv_nsec = ntohl (array[3]);

  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  call_stub_t *utimens_stub = fop_utimens_stub (frame, 
						server_utimens_resume,
						&loc,
						buf);

  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = utimens_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);
  } else {
    call_resume (utimens_stub);
  }

  return 0;
}


int32_t
server_access_resume (call_frame_t *frame,
		      xlator_t *this,
		      loc_t *loc,
		      int32_t mask)
{
  server_state_t *state = STATE (frame);

  state->inode = inode_ref (loc->inode);

  STACK_WIND (frame,
	      server_access_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->access,
	      loc,
	      mask);
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
int32_t
server_access (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  mode_t mode = params->common;
  loc_t loc = {0,};

  loc.path = (char *)params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  call_stub_t *access_stub = fop_access_stub (frame, 
					      server_access_resume,
					      &loc,
					      mode);
  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = access_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);

  } else {
    call_resume (access_stub);
  }

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
int32_t
server_lk (call_frame_t *frame,
	   xlator_t *bound_xl,
	   gf_args_request_t *params)
{
  struct flock lock = {0, };
  int32_t cmd = 0;
  int32_t type;
  fd_t *fd = NULL;
  server_proto_priv_t *priv = SERVER_PRIV (frame);
  int32_t fd_no = params->common;
 
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR, 
	    "unresolved fd %d, returning EINVAL", fd_no);

    server_lk_cbk (frame, NULL, frame->this,
		   -1, EINVAL, &lock);
    return 0;
  }
  
  cmd =  gf_ntohl_64_ptr (params->fields[0].ptr);
  switch (cmd) {
  case GF_LK_GETLK:
    cmd = F_GETLK;
    break;
  case GF_LK_SETLK:
    cmd = F_SETLK;
    break;
  case GF_LK_SETLKW:
    cmd = F_SETLKW;
    break;
  }

  type = gf_ntohl_64_ptr (params->fields[0].ptr + 8);
  switch (type) {
  case GF_LK_F_RDLCK:
    lock.l_type = F_RDLCK;
    break;
  case GF_LK_F_WRLCK:
    lock.l_type = F_WRLCK;
    break;
  case GF_LK_F_UNLCK:
    lock.l_type = F_UNLCK;
    break;
  default:
    gf_log (bound_xl->name, GF_LOG_ERROR, "Unknown lock type: %d!", type);
    break;
  }

  lock.l_whence =  gf_ntohl_64_ptr (params->fields[0].ptr+16);
  lock.l_start =  gf_ntohl_64_ptr (params->fields[0].ptr+24);
  lock.l_len =  gf_ntohl_64_ptr (params->fields[0].ptr+32);
  lock.l_pid =  gf_ntohl_64_ptr (params->fields[0].ptr+40);

  STACK_WIND (frame, server_lk_cbk, bound_xl,
	      bound_xl->fops->lk, fd, cmd, &lock);

  return 0;
}



/*
 * server_writedir -
 *
 * @frame:
 * @bound_xl:
 * @params:
 *
 */
int32_t 
server_setdents (call_frame_t *frame,
		 xlator_t *bound_xl,
		 gf_args_request_t *params)
{
  dir_entry_t *entry = NULL;
  fd_t *fd = NULL;
  server_proto_priv_t *priv = SERVER_PRIV (frame);
  int32_t fd_no = params->common;
  int32_t nr_count = ntohl (((int32_t *)params->fields[0].ptr)[1]);
  int32_t flag = ntohl (((int32_t *)params->fields[0].ptr)[0]);;
 
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  if (!fd) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d, returning EINVAL", fd_no);

    server_setdents_cbk (frame, NULL, frame->this, -1, EINVAL);
    return 0;
  }

  {
    dir_entry_t *trav = NULL, *prev = NULL;
    int32_t count, i, bread;
    char *ender = NULL, *buffer_ptr = NULL;
    char tmp_buf[512] = {0,};

    entry = calloc (1, sizeof (dir_entry_t));
    prev = entry;
    buffer_ptr = (char *)params->fields[1].ptr;
    
    for (i = 0; i < nr_count ; i++) {
      bread = 0;
      trav = calloc (1, sizeof (dir_entry_t));
      ender = strchr (buffer_ptr, '/');
      if (!ender)
	break;
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
      
      /* TODO: use str_to_stat instead */
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

      ender = strchr (buffer_ptr, '\n');
      if (!ender)
	break;
      count = ender - buffer_ptr;
      *ender = '\0';
      if (S_ISLNK (trav->buf.st_mode)) {
	trav->link = strdup (buffer_ptr);
      } else {
	trav->link = "";
      }
      bread = count + 1;
      buffer_ptr += bread;
      
      prev->next = trav;
      prev = trav;
    }
  }

  STACK_WIND (frame, 
	      server_setdents_cbk, 
	      bound_xl,
	      bound_xl->fops->setdents,
	      fd, flag, entry, nr_count);

  {
    /* Free the variables allocated in this fop here */
    dir_entry_t *trav = entry->next;
    dir_entry_t *prev = entry;
    while (trav) {
      prev->next = trav->next;
      FREE (trav->name);
      if (S_ISLNK (trav->buf.st_mode))
	FREE (trav->link);
      FREE (trav);
      trav = prev->next;
    }
    FREE (entry);
  }

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
	     gf_args_request_t *params)
{
  int32_t ret = -1;
  int32_t spec_fd = -1;
  void *file_data = NULL;
  int32_t file_data_len = 0;
  struct sockaddr_in *_sock = NULL;
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  char tmp_filename[4096] = {0,};
  char *filename = GLUSTERFSD_SPEC_PATH;
  struct stat stbuf = {0,};

  _sock = &(TRANSPORT_OF (frame))->peerinfo.sockaddr;

  if (dict_get (frame->this->options, "client-volume-filename")) {
    filename = data_to_str (dict_get (frame->this->options,
				      "client-volume-filename"));
  }
  {
    sprintf (tmp_filename, "%s.%s", filename, inet_ntoa (_sock->sin_addr));
    /* Try for ip specific client spec file. 
     * If not found, then go for, regular client file. 
     */
    ret = open (tmp_filename, O_RDONLY);
    spec_fd = ret;
    if (spec_fd < 0) {
      gf_log (TRANSPORT_OF (frame)->xl->name, GF_LOG_DEBUG,
	      "Unable to open %s (%s)", tmp_filename, strerror (errno));
      ret = open (filename, O_RDONLY);
      spec_fd = ret;
      if (spec_fd < 0) {
	gf_log (TRANSPORT_OF (frame)->xl->name, GF_LOG_ERROR,
		"Unable to open %s (%s)", filename, strerror (errno));
	goto fail;
      }
    } else {
      /* Successful */
      filename = tmp_filename;
    }
  }

  /* to allocate the proper buffer to hold the file data */
  {
    ret = stat (filename, &stbuf);
    if (ret < 0){
      gf_log (TRANSPORT_OF (frame)->xl->name, GF_LOG_ERROR,
	      "Unable to stat %s (%s)", filename, strerror (errno));
      goto fail;
    }
    
    file_data_len = stbuf.st_size;
    file_data = calloc (1, file_data_len + 1);
  }
  
  gf_full_read (spec_fd, file_data, file_data_len);

  reply->fields[0].len = file_data_len;
  reply->fields[0].ptr = (void *)file_data;
  reply->fields[0].need_free = 1;
  reply->fields[0].type = GF_PROTO_CHAR_TYPE;

 fail:
    
  reply->op_ret = ret;
  reply->op_errno = gf_errno_to_error (errno);

  server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_GETSPEC, 
		reply, frame->root->rsp_refs);

  return 0;
}

int32_t
server_checksum_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     uint8_t *fchecksum,
		     uint8_t *dchecksum)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  if (op_ret >= 0) {
    reply->fields[0].ptr = (void *)fchecksum;
    reply->fields[0].len = 4096;
    reply->fields[0].need_free = 1;
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;

    reply->fields[1].ptr = (void *)dchecksum;
    reply->fields[1].len = 4096;
    reply->fields[1].need_free = 1;
    reply->fields[1].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_CHECKSUM,
		reply, frame->root->rsp_refs);

  return 0;
}

int32_t
server_checksum (call_frame_t *frame,
		 xlator_t *bound_xl,
		 gf_args_request_t *params)
{
  loc_t loc = {0,};
  int32_t flag = params->common;

  loc.path = params->fields[1].ptr;
  loc.ino = gf_ntohl_64_ptr (params->fields[0].ptr);
  loc.inode = NULL;

  STACK_WIND (frame,
	      server_checksum_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->mops->checksum,
	      &loc,
	      flag);

  return 0;
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
	     gf_args_request_t *params)
{
  int32_t ret = -1;
  int32_t spec_fd = -1;
  int32_t remote_errno = 0;
  void *file_data = NULL;
  int32_t file_data_len = 0;
  int32_t offset = 0;
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));

  file_data = params->fields[0].ptr;
  file_data_len = params->fields[0].len;
  
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
  
  reply->op_ret = ret;
  reply->op_errno = gf_errno_to_error (remote_errno);
  server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_SETSPEC, 
		reply, frame->root->rsp_refs);  

  return 0;
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
int32_t
server_mop_lock_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_LOCK, 
		reply, frame->root->rsp_refs);

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
	  gf_args_request_t *params)
{
  char *path = params->fields[0].ptr;
  
  STACK_WIND (frame, server_mop_lock_cbk, frame->this,
	      frame->this->mops->lock, path);

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
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = op_ret;
  reply->op_errno = gf_errno_to_error (op_errno);

  server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_UNLOCK, 
		reply, frame->root->rsp_refs);

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
	    gf_args_request_t *params)
{

  char *path = params->fields[0].ptr;

  STACK_WIND (frame, mop_unlock_cbk, frame->this,
	      frame->this->mops->unlock, path);

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
	       gf_args_request_t *params)
{
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));

  /* logic to read locks and send them to the person who requested for it */

  reply->op_ret = 0;
  reply->op_errno = 0;
  reply->dummy1 = 0xcafebabe;

  server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_LISTLOCKS, 
		reply, frame->root->rsp_refs);

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
	       gf_args_request_t *params)
{
  return 0;
}

/*
 * server_mop_stats_cbk - stats callback for server management operation
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 * @stats:err
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
  int64_t glusterfsd_stats_nr_clients = 0;

  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));
  
  reply->op_ret = ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  
  if (ret >= 0) {
    char buffer[256] = {0,};
    sprintf (buffer, 
	     "%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64"\n",
	     stats->nr_files,
	     stats->disk_usage,
	     stats->free_disk,
	     stats->total_disk_size,
	     stats->read_usage,
	     stats->write_usage,
	     stats->disk_speed,
	     glusterfsd_stats_nr_clients);

    reply->fields[0].ptr = (void *)strdup (buffer);
    reply->fields[0].len = strlen (buffer);
    reply->fields[0].need_free = 1;
    reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  }

  server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_STATS, 
		reply, frame->root->rsp_refs);

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
	   gf_args_request_t *params)
{
  if (!bound_xl) {
    gf_log (frame->this->name, GF_LOG_ERROR, 
	    "request is not for authenticated volume, returning EINVAL");
    server_mop_stats_cbk (frame, NULL, frame->this, -1, EINVAL, NULL);
    return 0;
  }
  
  STACK_WIND (frame, 
	      server_mop_stats_cbk, 
	      bound_xl,
	      bound_xl->mops->stats,
	      params->common);
  

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
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));

  reply->op_ret = ret;
  reply->op_errno = gf_errno_to_error (op_errno);
  
  server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_FSCK, 
		reply, frame->root->rsp_refs);
  
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
	  gf_args_request_t *params)
{
  STACK_WIND (frame, server_mop_fsck_cbk, bound_xl,
	      bound_xl->mops->fsck, params->common);

  return 0;
}


struct __get_xl_struct {
  const char *name;
  xlator_t *reply;
};

void __check_and_set (xlator_t *each,
		      void *data)
{
  if (!strcmp (each->name,
              ((struct __get_xl_struct *) data)->name))
    ((struct __get_xl_struct *) data)->reply = each;
}
 
static xlator_t *
get_xlator_by_name (xlator_t *some_xl,
		    const char *name)
{
  struct __get_xl_struct get = {
    .name = name,
    .reply = NULL
  };
     
  xlator_foreach (some_xl, __check_and_set, &get);

  return get.reply;
}

/*
 * mop_setvolume - setvolume management function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 */
int32_t 
mop_setvolume (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_args_request_t *params)
{
  int32_t ret = -1;
  int32_t remote_errno = 0;
  server_proto_priv_t *priv;
  server_private_t *server_priv = NULL;
  char *name, *version;
  char *error;
  xlator_t *xl;
  struct sockaddr_in *_sock = NULL;
  dict_t *config_params = dict_copy (frame->this->options, NULL);
  gf_args_reply_t *reply = calloc (1, sizeof (gf_args_t));

  dict_t *dict = get_new_dict ();
  error = calloc (1, 100);
  
  priv = SERVER_PRIV (frame);

  server_priv = TRANSPORT_OF (frame)->xl->private;

  version = params->fields[0].ptr;
  if (strcmp (version, PACKAGE_VERSION)) {
    char *msg;
    asprintf (&msg, 
	      "Version mismatch: client(%s) Vs server (%s)", 
	      version, PACKAGE_VERSION);
    remote_errno = EINVAL;
    strcpy (error, msg);
    goto fail;
  }
			     
  name = params->fields[1].ptr;
  fprintf (stderr, "%s is remote-subvolume", name);
  xl = get_xlator_by_name (frame->this, name);
  
  if (!xl) {
    char *msg;
    asprintf (&msg, "remote-subvolume \"%s\" is not found", name);
    remote_errno = ENOENT;
    strcpy (error, msg);
    goto fail;
  } 

  _sock = &(TRANSPORT_OF (frame))->peerinfo.sockaddr;

  dict_set (dict, "peer-ip", str_to_data(inet_ntoa (_sock->sin_addr)));
  dict_set (dict, "peer-port", data_from_uint16 (ntohs (_sock->sin_port)));
  dict_set (dict, "remote-subvolume", str_to_data (name));

  if (!server_priv->auth_modules) {
    gf_log (TRANSPORT_OF (frame)->xl->name, 
	    GF_LOG_ERROR,
	    "Authentication module not initialized");
  }

  if (gf_authenticate (dict, config_params, server_priv->auth_modules) == AUTH_ACCEPT) {
    gf_log (TRANSPORT_OF (frame)->xl->name,  GF_LOG_DEBUG,
	    "accepted client from %s:%d",
	    inet_ntoa (_sock->sin_addr), ntohs (_sock->sin_port));
    ret = 0;
    priv->bound_xl = xl;
    strcpy (error, "Success");
  } else {
    gf_log (TRANSPORT_OF (frame)->xl->name, GF_LOG_ERROR,
	    "Cannot authenticate client from %s:%d",
	    inet_ntoa (_sock->sin_addr), ntohs (_sock->sin_port));
    ret = -1;
    remote_errno = EACCES;
    strcpy (error, "Authentication Failed");
    goto fail;
  }

  if (!priv->bound_xl) {
    ret = -1;
    remote_errno = EACCES;
    strcpy (error, "Check volume spec file and handshake options");
    goto fail;
  } 
  
 fail:
  if (priv->bound_xl && ret >= 0 && (!(priv->bound_xl->itable))) {
    /* create inode table for this bound_xl, if one doesn't already exist */
    int32_t lru_limit = 1024;
    xlator_t *xl = TRANSPORT_OF (frame)->xl;

    if (dict_get (xl->options, "inode-lru-limit")) {
      int32_t xl_limit = data_to_int32 (dict_get (xl->options,
						  "inode-lru-limit"));
      if (xl_limit)
	lru_limit = xl_limit;
    }

    if (dict_get (priv->bound_xl->options, "inode-lru-limit")) {
      int32_t xl_limit = data_to_int32 (dict_get (priv->bound_xl->options,
						  "inode-lru-limit"));
      if (xl_limit)
	lru_limit = xl_limit;
    }
    gf_log (xl->name, GF_LOG_DEBUG,
	    "creating inode table with lru_limit=%d, xlator=%s",
	    lru_limit, priv->bound_xl->name);
    priv->bound_xl->itable = inode_table_new (lru_limit, priv->bound_xl);
  }
  reply->op_ret   = ret;
  reply->op_errno = gf_errno_to_error (remote_errno);
  reply->fields[0].len = strlen (error);
  reply->fields[0].type = GF_PROTO_CHAR_TYPE;
  reply->fields[0].need_free = 1;
  reply->fields[0].ptr = (void *)error;

  server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_SETVOLUME, 
		reply, frame->root->rsp_refs);

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
  gf_args_reply_t reply = {0,};

  reply.op_ret = -1;
  reply.op_errno = gf_errno_to_error (ENOSYS);

  server_reply (frame, type, opcode, &reply, frame->root->rsp_refs);

  return 0;
}	     


/* 
 * get_frame_for_call - create a frame into the call_ctx_t capable of 
 *                      generating and replying the reply packet by itself.
 *                      By making a call with this frame, the last UNWIND 
 *                      function will have all needed state from its
 *                      frame_t->root to send reply.
 * @trans:
 * @blk:
 * @params:
 *
 * not for external reference
 */

static call_frame_t *
get_frame_for_call (transport_t *trans,
		    gf_proto_block_t *blk,
		    gf_args_request_t *params)
{
  call_pool_t *pool = trans->xl->ctx->pool;
  call_ctx_t *_call = (void *) calloc (1, sizeof (*_call));
  server_state_t *state = calloc (1, sizeof (*state));
  server_proto_priv_t *priv = trans->xl_private;

  if (!pool) {
    pool = trans->xl->ctx->pool = calloc (1, sizeof (*pool));
    LOCK_INIT (&pool->lock);
    INIT_LIST_HEAD (&pool->all_frames);
  }

  _call->pool = pool;
  LOCK (&pool->lock);
  {
    list_add (&_call->all_frames, &pool->all_frames);
  }
  UNLOCK (&pool->lock);

  state->bound_xl = priv->bound_xl;
  state->trans = transport_ref (trans);
  _call->trans = trans;
  _call->state = state;                        /* which socket */
  _call->unique = blk->callid;                 /* which call */

  _call->frames.root = _call;
  _call->frames.this = trans->xl;
  _call->frames.op = blk->op;
  _call->frames.type = blk->type;

  _call->uid = params->uid;
  _call->gid = params->gid;
  _call->pid = params->pid;

  return &_call->frames;
}

/*
 * prototype of operations function for each of mop and 
 * fop at server protocol level 
 *
 * @frame: call frame pointer
 * @bound_xl: the xlator that this frame is bound to
 * @params: parameters dictionary
 *
 * to be used by protocol interpret, _not_ for exterenal reference
 */
typedef int32_t (*gf_op_t) (call_frame_t *frame,
			    xlator_t *bould_xl,
			    gf_args_request_t *params);

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
  server_open,
  server_readv,
  server_writev,
  server_statfs,
  server_flush,
  server_close,
  server_fsync,
  server_setxattr,
  server_getxattr,
  server_removexattr,
  server_opendir,
  server_getdents,
  server_closedir,
  server_fsyncdir,
  server_access,
  server_create,
  server_ftruncate,
  server_fstat,
  server_lk,
  server_utimens,
  server_fchmod,
  server_fchown,
  server_lookup,
  server_forget,
  server_setdents,
  server_rmelem,
  server_incver,
  server_readdir
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
  mop_fsck,
  server_checksum,
};
/*
 * server_protocol_interpret - protocol interpreter function for server 
 *
 * @trans: transport object
 * @blk: data block
 *
 */
int32_t 
server_protocol_interpret (transport_t *trans,
			   gf_proto_block_t *blk)
{
  int32_t ret = 0;
  gf_args_request_t params = {0,};
  dict_t *refs = NULL;
  server_proto_priv_t *priv = trans->xl_private;
  /* the xlator to STACK_WIND into */
  xlator_t *bound_xl = (xlator_t *)priv->bound_xl; 
  call_frame_t *frame = NULL;
  
  switch (blk->type) {
  case GF_OP_TYPE_FOP_REQUEST:

    /* drop connection for unauthorized fs access */
    if (!bound_xl) {
      gf_log (trans->xl->name, GF_LOG_ERROR,
	      "request from an unauthorized client");
      ret = -1;
      break;
    }

    if (blk->op < 0) {
      gf_log (trans->xl->name, GF_LOG_ERROR,
	      "invalid operation is 0x%x", blk->op);
      ret = -1;
      break;
    }
    gf_proto_get_struct_from_buf (blk->args, (gf_args_t *)&params, blk->size);
    frame = get_frame_for_call (trans, blk, &params);

    frame->root->req_refs = refs = dict_ref (get_new_dict ());
    dict_set (refs, NULL, data_from_ptr (blk->args));
    refs->is_locked = 1;

    if (blk->op > GF_FOP_MAXVALUE) {
      gf_log (frame->this->name, GF_LOG_ERROR, 
	      "Unknown Operation requested :O");
      unknown_op_cbk (frame, GF_OP_TYPE_FOP_REQUEST, blk->op);
      break;
    }
    
    ret = gf_fops[blk->op] (frame, bound_xl, &params);
    break;
    
  case GF_OP_TYPE_MOP_REQUEST:
    
    if (blk->op < 0) {
      gf_log (trans->xl->name, GF_LOG_ERROR,
	      "invalid management operation is 0x%x", blk->op);
      ret = -1;
      break;
    }
    
    gf_proto_get_struct_from_buf (blk->args, (gf_args_t *)&params, blk->size);
    frame = get_frame_for_call (trans, blk, &params);

    frame->root->req_refs = refs = dict_ref (get_new_dict ());
    dict_set (refs, NULL, data_from_ptr(blk->args));
    refs->is_locked = 1;

    if (blk->op > GF_MOP_MAXVALUE) {
      gf_log (frame->this->name, GF_LOG_ERROR, 
	      "Unknown Operation requested :O");
      unknown_op_cbk (frame, GF_OP_TYPE_MOP_REQUEST, blk->op);
      break;
    }

    ret = gf_mops[blk->op] (frame, bound_xl, &params);

    break;
  default:
    /* There was no frame create, hence no refs */
    FREE (blk->args);
    gf_log (trans->xl->name, GF_LOG_DEBUG,
	    "Unknown packet type: %d", blk->type);
    ret = -1;
  }

  if (refs)
    dict_unref (refs);

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
int32_t
server_nop_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  /* TODO: cleanup frame->root->state */
  fd_t *fd = frame->local;

  if (fd) {
    fd_destroy (fd);
    frame->local = NULL;
  }

  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * get_frame_for_transport - get call frame for specified transport object
 *
 * @trans: transport object
 *
 */
static call_frame_t *
get_frame_for_transport (transport_t *trans)
{
  call_ctx_t *_call = (void *) calloc (1, sizeof (*_call));
  call_pool_t *pool = trans->xl->ctx->pool;
  server_proto_priv_t *priv = trans->xl_private;
  server_state_t *state;

  if (!pool) {
    pool = trans->xl->ctx->pool = calloc (1, sizeof (*pool));
    LOCK_INIT (&pool->lock);
    INIT_LIST_HEAD (&pool->all_frames);
  }

  _call->pool = pool;

  LOCK (&_call->pool->lock);
  {
    list_add (&_call->all_frames, &_call->pool->all_frames);
  }
  UNLOCK (&_call->pool->lock);

  state = calloc (1, sizeof (*state));
  state->bound_xl = priv->bound_xl;
  state->trans = transport_ref (trans);
  _call->trans = trans;
  _call->state = state;        /* which socket */
  _call->unique = 0;           /* which call */

  _call->frames.root = _call;
  _call->frames.this = trans->xl;

  return &_call->frames;
}


/* 
 * server_protocol_cleanup - cleanup function for server protocol
 *
 * @trans: transport object
 *
 */
int32_t
server_protocol_cleanup (transport_t *trans)
{
  server_proto_priv_t *priv = trans->xl_private;
  call_frame_t *frame = NULL, *unlock_frame = NULL;
  struct sockaddr_in *_sock;
  xlator_t *bound_xl;

  if (!priv)
    return 0;

  bound_xl = (xlator_t *) priv->bound_xl;

  /* trans will have ref_count = 1 after this call, but its ok since this function is 
     called in GF_EVENT_TRANSPORT_CLEANUP */
  frame = get_frame_for_transport (trans);

  /* its cleanup of transport */
  /* but, mop_unlock_impl needs transport ptr to clear locks held by it */
  /* ((server_state_t *)frame->root->state)->trans = NULL; */

  /* ->unlock () with NULL path will cleanup
     lock manager's internals by remove all
     entries related to this transport
  */
  pthread_mutex_lock (&priv->lock);
  {
    if (priv->fdtable) {
      int32_t i = 0;
      pthread_mutex_lock (&priv->fdtable->lock);
      {
	for (i=0; i < priv->fdtable->max_fds; i++) {
	  if (priv->fdtable->fds[i]) {
	    mode_t st_mode = priv->fdtable->fds[i]->inode->st_mode ;
	    fd_t *fd = priv->fdtable->fds[i];
	    call_frame_t *close_frame = copy_frame (frame);

	    close_frame->local = fd;

	    if (S_ISDIR (st_mode)) {
	      STACK_WIND (close_frame,
			  server_nop_cbk,
			  bound_xl,
			  bound_xl->fops->closedir,
			  fd);
	    } else {
	      STACK_WIND (close_frame,
			  server_nop_cbk,
			  bound_xl,
			  bound_xl->fops->close,
			  fd);
	    }
	  }
	}
      }
      pthread_mutex_unlock (&priv->fdtable->lock);
      gf_fd_fdtable_destroy (priv->fdtable);
      priv->fdtable = NULL;
    }
  }
  pthread_mutex_unlock (&priv->lock);

  unlock_frame = copy_frame (frame);

  STACK_WIND (unlock_frame,
	      server_nop_cbk,
	      trans->xl,
	      trans->xl->mops->unlock,
	      NULL);

  _sock = &trans->peerinfo.sockaddr;

  FREE (priv);
  trans->xl_private = NULL;

  STACK_DESTROY (frame->root);
  gf_log (trans->xl->name, GF_LOG_DEBUG,
	  "cleaned up transport state for client %s:%d",
	  inet_ntoa (_sock->sin_addr), ntohs (_sock->sin_port));

  return 0;
}

static void 
get_auth_types (dict_t *this,
		char *key,
		data_t *value,
		void *data)
{
  dict_t *auth_dict = data;
  char *saveptr = NULL, *tmp = NULL;
  char *key_cpy = strdup (key);

  tmp = strtok_r (key_cpy, ".", &saveptr);
  if (!strcmp (tmp, "auth")) {
    tmp = strtok_r (NULL, ".", &saveptr);
    dict_set (auth_dict, tmp, data_from_dynptr(NULL, 0));
  }

  FREE (key_cpy);
}
  
/*
 * init - called during server protocol initialization
 *
 * @this:
 *
 */
int32_t
init (xlator_t *this)
{
  transport_t *trans;
  struct rlimit lim;
  server_private_t *server_priv = NULL;
  server_conf_t *conf = NULL;
  server_reply_queue_t *queue;
  int32_t error = 0;

  gf_log (this->name, GF_LOG_DEBUG, "protocol/server xlator loaded");

  if (!this->children) {
    gf_log (this->name, GF_LOG_ERROR,
	    "protocol/server should have subvolume");
    return -1;
  }

  trans = transport_load (this->options, this, this->notify);

  if (!trans) {
    gf_log (this->name, GF_LOG_ERROR, "failed to load transport");
    return -1;
  }
  server_priv = calloc (1, sizeof (*server_priv));
  server_priv->trans = trans;

  server_priv->auth_modules = get_new_dict ();
  dict_foreach (this->options, get_auth_types, server_priv->auth_modules);
  error = gf_auth_init (server_priv->auth_modules);
  
  if (error) {
    dict_destroy (server_priv->auth_modules);
    return error;
  }

  this->private = server_priv;

  queue = calloc (1, sizeof (server_reply_queue_t));
  pthread_mutex_init (&queue->lock, NULL);
  pthread_cond_init (&queue->cond, NULL);
  INIT_LIST_HEAD (&queue->list);

  conf = calloc (1, sizeof (server_conf_t));
  conf->queue = queue;

  if (dict_get (this->options, "limits.transaction-size")) {
    conf->max_block_size = data_to_int32 (dict_get (this->options, 
						    "limits.trasaction-size"));
  } else {
    gf_log (this->name, GF_LOG_DEBUG,
	    "defaulting limits.transaction-size to %d", DEFAULT_BLOCK_SIZE);
    conf->max_block_size = DEFAULT_BLOCK_SIZE;
  }

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

  trans->xl_private = conf;
  pthread_create (&queue->thread, NULL, server_reply_proc, queue);

  return 0;
}

/* 
 * fini - finish function for server protocol, called before 
 *        unloading server protocol.
 *
 * @this:
 *
 */
void
fini (xlator_t *this)
{
  server_private_t *server_priv = this->private;
  if (server_priv->auth_modules) {
    dict_destroy (server_priv->auth_modules);
  }

  FREE (server_priv);
  this->private = NULL;

  return;
}

/*
 * server_protocol_notify - notify function for server protocol
 * @this: 
 * @trans:
 * @event:
 *
 */

int32_t
notify (xlator_t *this,
	int32_t event,
	void *data,
	...)
{
  int ret = 0;
  transport_t *trans = data;

  switch (event)
    {
    case GF_EVENT_POLLIN:
      {
	gf_proto_block_t *blk;
	server_proto_priv_t *priv = trans->xl_private;
	server_conf_t *conf = this->private;

	if (!priv) {
	  priv = (void *) calloc (1, sizeof (*priv));
	  trans->xl_private = priv;
	  priv->fdtable = gf_fd_fdtable_alloc ();
	  if (!priv->fdtable) {
	    gf_log (this->name,
		    GF_LOG_ERROR,
		    "Cannot allocate fdtable");
	    ret = ENOMEM;
	    break;
	  }
	  pthread_mutex_init (&priv->lock, NULL);
	}

	blk = gf_proto_block_unserialize_transport (trans, conf->max_block_size);
	if (!blk || !(blk->size)) {
	  ret = -1;
	}

	if (!ret) {
	  ret = server_protocol_interpret (trans, blk);
	  if (ret == -1) {
	    /* TODO: Possible loss of frame? */
	    transport_except (trans);
	  }
	  //FREE (blk->args);
	  FREE (blk);
	  break;
	} 
      }
      /* no break for ret check to happen below */
    case GF_EVENT_POLLERR:
      {
	ret = -1;
	transport_disconnect (trans);
      }
      break;

    case GF_EVENT_TRANSPORT_CLEANUP:
      {
	server_protocol_cleanup (trans);
      }
      break;

    default:
      default_notify (this, event, data);
      break;
    }

  return ret;
}


struct xlator_mops mops = {
  .lock = mop_lock_impl,
  .unlock = mop_unlock_impl
};

struct xlator_fops fops = {

};
