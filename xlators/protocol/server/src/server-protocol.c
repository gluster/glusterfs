/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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
#include "protocol.h"
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
  ERR_ABORT (dummy);

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


static void
protocol_server_reply (call_frame_t *frame,
		       int type, int op,
		       gf_hdr_common_t *hdr, size_t hdrlen,
		       struct iovec *vector, int count,
		       dict_t *refs)
{
  server_state_t *state = NULL;
  xlator_t *bound_xl = NULL;
  transport_t *trans = NULL;

  bound_xl = BOUND_XL (frame);
  state    = STATE (frame);
  trans    = state->trans;

  hdr->callid = hton64 (frame->root->unique);
  hdr->type   = hton32 (type);
  hdr->op     = hton32 (op);

  transport_submit (trans, (char *)hdr, hdrlen, vector, count, refs);

  STACK_DESTROY (frame->root);

  if (bound_xl)
    inode_table_prune (bound_xl->itable);
  if (state->inode)
    inode_unref (state->inode);
  if (state->inode2)
    inode_unref (state->inode2);
  transport_unref (trans);
  FREE (state);
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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_fchmod_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FCHMOD,
			 hdr, hdrlen, NULL, 0, NULL);

  return 0;
}

/*
 * server_fchmod
 *
 */
int32_t
server_fchmod (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{  
  gf_fop_fchmod_req_t *req = NULL;
  server_proto_priv_t *priv = NULL;
  mode_t mode = 0;
  int fd_no = -1;
  fd_t *fd = NULL; 

  req = gf_param (hdr);

  mode   = ntoh32 (req->mode);
  fd_no  = ntoh64 (req->fd);

  priv = SERVER_PRIV (frame);
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_fchmod_cbk (frame, NULL, frame->this,
			 -1, EINVAL, NULL);

      return -1;
    }

  STACK_WIND (frame, 
	      server_fchmod_cbk, 
	      bound_xl,
	      bound_xl->fops->fchmod,
	      fd,
	      mode);
  
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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_fchown_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FCHOWN,
			 hdr, hdrlen, NULL, 0, NULL);

  return 0;
}

/*
 * server_fchown
 *
 */
int32_t
server_fchown (call_frame_t *frame,
	       xlator_t *bound_xl,
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
  gf_fop_fchown_req_t *req = NULL;
  server_proto_priv_t *priv = NULL;
  uid_t uid = 0;
  gid_t gid = 0;
  int fd_no = -1;
  fd_t *fd = NULL; 

  req = gf_param (hdr);

  uid   = ntoh32 (req->uid);
  gid   = ntoh32 (req->gid);
  fd_no = ntoh64 (req->fd);

  priv = SERVER_PRIV (frame);
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_fchown_cbk (frame, NULL, frame->this,
			 -1, EINVAL, NULL);

      return -1;
    }

  STACK_WIND (frame, 
	      server_fchown_cbk, 
	      bound_xl,
	      bound_xl->fops->fchown,
	      fd,
	      uid,
	      gid);
  
  return 0;

}

/*
 * server_setdents_cbk - writedir callback for server protocol
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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_setdents_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_SETDENTS,
			 hdr, hdrlen, NULL, 0, NULL);

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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_lk_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_flock_from_flock (&rsp->flock, lock);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FCHMOD,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_access_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_ACCESS,
			 hdr, hdrlen, NULL, 0, NULL);

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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_utimens_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_UTIMENS,
			 hdr, hdrlen, NULL, 0, NULL);

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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_chmod_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_CHMOD,
			 hdr, hdrlen, NULL, 0, NULL);

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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_chown_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_CHOWN,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_rmdir_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_RMDIR,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_rmelem_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_RMELEM,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_incver_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_INCVER,
			 hdr, hdrlen, NULL, 0, NULL);
  
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
  inode_t *server_inode = NULL;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_mknod_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret >= 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);

      server_inode = inode_update (BOUND_XL(frame)->itable, NULL, NULL, stbuf);
      
      inode_lookup (server_inode);
      server_inode->ctx = inode->ctx;
      inode->ctx = NULL;
      server_inode->st_mode = stbuf->st_mode;
      server_inode->generation = inode->generation;
      inode_unref (server_inode);
      inode_unref (inode);
    } 

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_MKDIR,
			 hdr, hdrlen, NULL, 0, NULL);

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
  inode_t *server_inode = NULL;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_mknod_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret >= 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);

      server_inode = inode_update (BOUND_XL(frame)->itable, NULL, NULL, stbuf);
      
      inode_lookup (server_inode);
      server_inode->ctx = inode->ctx;
      inode->ctx = NULL;
      server_inode->st_mode = stbuf->st_mode;
      server_inode->generation = inode->generation;
      inode_unref (server_inode);
      inode_unref (inode);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_MKNOD,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_fsyncdir_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FSYNCDIR,
			 hdr, hdrlen, NULL, 0, NULL);
  
  return 0;
}


/*
 * server_getdents_cbk - readdir callback for server protocol
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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_getdents_rsp_t *rsp = NULL;
  size_t hdrlen = 0;
  char *buffer = NULL;
  int32_t buf_len = 0;

  if (op_ret >= 0)
    {  
      { 
	dir_entry_t *trav = entries->next;
	uint32_t len = 0;
	char *tmp_buf = NULL;
	while (trav) {
	  len += strlen (trav->name);
	  len += 1;
	  len += strlen (trav->link);
	  len += 1; /* for '\n' */
	  len += 256; // max possible for statbuf;
	  trav = trav->next;
	}
	
	buffer = calloc (1, len);
	ERR_ABORT (buffer);

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
	buf_len = strlen (buffer);
      }
    }

  hdrlen = gf_hdr_len (rsp, buf_len + 1);
  hdr    = gf_hdr_new (rsp, buf_len + 1);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      strcpy (rsp->buf, buffer);
      rsp->count = hton32 (count);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_GETDENTS,
			 hdr, hdrlen, NULL, 0, NULL);

  FREE (buffer);
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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_readdir_rsp_t *rsp = NULL;
  size_t hdrlen = 0;
  size_t entry_size = 0;

  if (op_ret > 0)
    entry_size = op_ret;

  hdrlen = gf_hdr_len (rsp, entry_size);
  hdr    = gf_hdr_new (rsp, entry_size);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (entry_size)
    memcpy (rsp->buf, entries, entry_size);

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_READDIR,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_closedir_rsp_t *rsp = NULL;
  fd_t *fd = NULL;
  size_t hdrlen = 0;

  fd = frame->local;
  frame->local = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_CLOSEDIR,
			 hdr, hdrlen, NULL, 0, NULL);
  
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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_opendir_rsp_t *rsp = NULL;
  size_t hdrlen = 0;
  server_proto_priv_t *priv = NULL;
  uint64_t fd_no = -1;

  if (op_ret >= 0)
    {
      priv = SERVER_PRIV (frame);
      fd_no = gf_fd_unused_get (priv->fdtable, fd);
    }

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));
  rsp->fd           = hton64 (fd_no);

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_OPENDIR,
			 hdr, hdrlen, NULL, 0, NULL);

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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_statfs_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret >= 0)
    {
      gf_statfs_from_statfs (&rsp->statfs, buf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_STATFS,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_removexattr_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_REMOVEXATTR,
			 hdr, hdrlen, NULL, 0, NULL);
  

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_getxattr_rsp_t *rsp = NULL;
  size_t hdrlen = 0;
  int32_t len = 0;

  if (op_ret >= 0) {
    dict_set (dict, "__@@protocol_client@@__key", str_to_data ("value"));
    len = dict_serialized_length (dict);
  }

  hdrlen = gf_hdr_len (rsp, len + 1);
  hdr    = gf_hdr_new (rsp, len + 1);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret >= 0) 
    {
      rsp->dict_len = hton32 (len);
      dict_serialize (dict, rsp->dict);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_GETXATTR,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_setxattr_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_SETXATTR,
			 hdr, hdrlen, NULL, 0, NULL);

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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_rename_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_RENAME,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_unlink_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_UNLINK,
			 hdr, hdrlen, NULL, 0, NULL);
  
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
  inode_t *server_inode = NULL;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_symlink_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret >= 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);

      server_inode = inode_update (BOUND_XL(frame)->itable, NULL, NULL, stbuf);
      
      inode_lookup (server_inode);
      server_inode->ctx = inode->ctx;
      inode->ctx = NULL;
      server_inode->st_mode = stbuf->st_mode;
      server_inode->generation = inode->generation;
      inode_unref (server_inode);
      inode_unref (inode);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_SYMLINK,
			 hdr, hdrlen, NULL, 0, NULL);

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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_link_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_LINK,
			 hdr, hdrlen, NULL, 0, NULL);

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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_truncate_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_TRUNCATE,
			 hdr, hdrlen, NULL, 0, NULL);

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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_fstat_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FSTAT,
			 hdr, hdrlen, NULL, 0, NULL);

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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_ftruncate_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FTRUNCATE,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_flush_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FLUSH,
			 hdr, hdrlen, NULL, 0, NULL);
  
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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_fsync_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FSYNC,
			 hdr, hdrlen, NULL, 0, NULL);
  
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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_close_rsp_t *rsp = NULL;
  fd_t *fd = NULL;
  size_t hdrlen = 0;

  fd = frame->local;
  frame->local = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_CLOSE,
			 hdr, hdrlen, NULL, 0, NULL);
  
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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_write_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret >= 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame,
			 GF_OP_TYPE_FOP_REPLY, GF_FOP_WRITE,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_read_rsp_t *rsp = NULL;
  size_t hdrlen = 0;


  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret >= 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_READ,
			 hdr, hdrlen, vector, count, frame->root->rsp_refs);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_open_rsp_t *rsp = NULL;
  size_t hdrlen = 0;
  server_proto_priv_t *priv = NULL;
  int fd_no = -1;

  if (op_ret >= 0)
    {
      priv = SERVER_PRIV (frame);
      fd_no = gf_fd_unused_get (priv->fdtable, fd);
    }

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));
  rsp->fd           = hton64 (fd_no);

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_OPEN,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_create_rsp_t *rsp = NULL;
  size_t hdrlen = 0;
  inode_t *server_inode = NULL;
  int32_t fd_no = -1;
  server_proto_priv_t *priv = NULL;

  if (op_ret >= 0)
    {
      priv = SERVER_PRIV (frame);

      server_inode = inode_update (BOUND_XL(frame)->itable,
				   NULL, NULL, stbuf);
    
      {
	server_inode->ctx = inode->ctx;
	server_inode->st_mode = stbuf->st_mode;
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

      if (fd_no < 0 || fd == 0)
	{
	  op_ret = fd_no;
	  op_errno = errno;
	}
    }

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));
  rsp->fd           = hton64 (fd_no);
  gf_stat_from_stat (&rsp->stat, stbuf);
  
  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_CREATE,
			 hdr, hdrlen, NULL, 0, NULL);
  
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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_readlink_rsp_t *rsp = NULL;
  size_t hdrlen = 0;
  size_t linklen = 0;

  if (op_ret >= 0)
    linklen = strlen (buf) + 1;

  hdrlen = gf_hdr_len (rsp, linklen);
  hdr    = gf_hdr_new (rsp, linklen);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));
  if (op_ret >= 0)
    strcpy (rsp->path, buf);

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_READLINK,
			hdr, hdrlen, NULL, 0, NULL);

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
  size_t hdrlen = 0;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_stat_rsp_t *rsp = NULL;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
    }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_STAT,
			 hdr, hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_forget_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FORGET,
			 hdr, hdrlen, NULL, 0, NULL);

  return 0;
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
  inode_t *server_inode = NULL;
  inode_t *root_inode = NULL;
  int dict_len = 0;
  gf_hdr_common_t *hdr = NULL;
  size_t hdrlen = 0;
  gf_fop_lookup_rsp_t *rsp = NULL;

  if (dict)
    {
      dict_set (dict, "__@@protocol_client@@__key", str_to_data ("value"));
      dict_len = dict_serialized_length (dict);
    }

  hdrlen = gf_hdr_len (rsp, dict_len);
  hdr    = gf_hdr_new (rsp, dict_len);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret == 0)
    {
      gf_stat_from_stat (&rsp->stat, stbuf);
      rsp->dict_len = hton32 (dict_len);
      if (dict)
	dict_serialize (dict, rsp->dict);
    }

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

  }

  protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_LOOKUP,
			 hdr, hdrlen, NULL, 0, NULL);

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
      /* to make sure that STACK_DESTROY() does not try to FREE 
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
	    /* to make sure that STACK_DESTROY() does not try to FREE 
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
	    
	    /* to make sure that STACK_DESTROY() does not try to FREE 
	     * frame->local. frame->local points to call_stub_t, which is
	     * free()ed in call_resume(). */
	    frame->local = NULL;
	    call_resume (stub);
	    break;
	  }
	} else {
	  /* we are called by the lookup of newpath */
	  
	  /* to make sure that STACK_DESTROY() does not try to FREE 
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
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
  gf_fop_lookup_req_t *req = NULL;
  loc_t loc = {0,};
  server_state_t *state = STATE (frame);
  int32_t need_xattr = 0;

  req = gf_param (hdr);

  need_xattr = ntoh32 (req->flags);
  loc.ino    = ntoh64 (req->ino);
  loc.path   = req->path;

  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  if (loc.inode) {
    /* revalidate */
    state->inode = loc.inode;
  } else {
    /* fresh lookup or inode was previously pruned out */
    state->inode = dummy_inode (bound_xl->itable);
    loc.inode = state->inode;    
  }

  STACK_WIND (frame,	      server_lookup_cbk,
	      bound_xl,
	      bound_xl->fops->lookup,
	      &loc,
	      need_xattr);

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
server_forget (call_frame_t *frame, xlator_t *bound_xl,
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
  gf_fop_forget_req_t *req = NULL;
  ino_t ino = 0;
  inode_t *inode = NULL;

  req = gf_param (hdr);
  ino = ntoh64 (req->ino);

  inode = inode_search (bound_xl->itable, ino, NULL);

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
	     gf_hdr_common_t *hdr, size_t hdrlen,
	     char *buf, size_t buflen)
{
  loc_t loc = {0,};
  call_stub_t *stat_stub = NULL;
  gf_fop_stat_req_t *req = NULL;

  req = gf_param (hdr);

  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  
  stat_stub = fop_stat_stub (frame, 
			     server_stat_resume,
			     &loc);
  
  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }

  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = stat_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);
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
		 gf_hdr_common_t *hdr, size_t hdrlen,
		 char *buf, size_t buflen)
{
  size_t size = 0;
  loc_t loc = {0,};
  call_stub_t *readlink_stub = NULL;
  gf_fop_readlink_req_t *req = NULL;

  req       = gf_param (hdr);
  size      = ntoh32 (req->size);
  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  
  readlink_stub = fop_readlink_stub (frame, 
				     server_readlink_resume,
				     &loc,
				     size);
  
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
server_create (call_frame_t *frame, xlator_t *bound_xl,
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
  gf_fop_create_req_t *req = NULL;
  fd_t *fd = NULL;
  loc_t loc = {0,};
  mode_t mode = 0;
  int flags = 0;
  char *path = NULL;

  req = gf_param (hdr);

  mode  = ntoh32 (req->mode);
  flags = ntoh32 (req->flags);
  path  = req->path;


  loc.path = path;
  loc.inode = dummy_inode (bound_xl->itable);
  fd = fd_create (loc.inode);


  LOCK (&fd->inode->lock);
  {
    list_del_init (&fd->inode_list);
  }
  UNLOCK (&fd->inode->lock);

  STACK_WIND (frame, server_create_cbk, 
	      bound_xl, bound_xl->fops->create,
	      &loc, flags, mode, fd);
  
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

  STACK_WIND (frame,
	      server_open_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->open,
	      loc,
	      flags,
	      new_fd);

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
server_open (call_frame_t *frame, xlator_t *bound_xl,
	     gf_hdr_common_t *hdr, size_t hdrlen,
	     char *buf, size_t buflen)
{
  loc_t loc = {0,};
  call_stub_t *open_stub = NULL;
  gf_fop_open_req_t *req = NULL;
  int flags = 0;

  req = gf_param (hdr);

  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  flags     = ntoh32 (req->flags);
  
  open_stub = fop_open_stub (frame, server_open_resume, &loc, flags, NULL);
  
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
server_readv (call_frame_t *frame, xlator_t *bound_xl,
	      gf_hdr_common_t *hdr, size_t hdrlen,
	      char *buf, size_t buflen)
{
  gf_fop_read_req_t *req = NULL;
  server_proto_priv_t *priv = NULL;
  int32_t fd_no = -1;
  fd_t *fd = NULL;
  size_t size = 0;
  off_t offset = 0;

  priv = SERVER_PRIV (frame);

  req = gf_param (hdr);

  fd_no   = ntoh64 (req->fd);
  size    = ntoh32 (req->size);
  offset  = ntoh64 (req->offset);

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_readv_cbk (frame, NULL, frame->this,
			-1, EINVAL, NULL, 0, NULL);
      return -1;
    }
  
  STACK_WIND (frame, 
	      server_readv_cbk,
	      bound_xl,
	      bound_xl->fops->readv,
	      fd, size, offset);
  
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
server_writev (call_frame_t *frame, xlator_t *bound_xl,
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
  gf_fop_write_req_t *req = NULL;
  struct iovec iov = {0, };
  server_proto_priv_t *priv = NULL;
  fd_t *fd = NULL;
  int32_t fd_no = -1;
  dict_t *refs = NULL;
  off_t offset = 0;

  req = gf_param (hdr);

  priv = SERVER_PRIV (frame);

  offset = ntoh64 (req->offset);
  fd_no  = ntoh64 (req->fd);
  
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_writev_cbk (frame, NULL, frame->this,
			 -1, EINVAL, NULL);
      return -1;
    }

  iov.iov_base = buf;
  iov.iov_len = buflen;

  refs = get_new_dict ();
  dict_set (refs, NULL, data_from_dynptr (buf, 0));
  frame->root->req_refs = dict_ref (refs);
  
  STACK_WIND (frame, 
	      server_writev_cbk, 
	      bound_xl,
	      bound_xl->fops->writev,
	      fd, &iov, 1, offset);
  
  dict_unref (refs);

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
server_close (call_frame_t *frame, xlator_t *bound_xl,
	      gf_hdr_common_t *hdr, size_t hdrlen,
	      char *buf, size_t buflen)
{
  gf_fop_close_req_t *req = NULL;
  fd_t *fd = NULL;
  int32_t fd_no = -1;
  server_proto_priv_t *priv = NULL;

  req = gf_param (hdr);

  fd_no = ntoh64 (req->fd);

  priv = SERVER_PRIV (frame);
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_close_cbk (frame, NULL, frame->this,
			-1, EINVAL);
      return -1;
    }

  frame->local = fd;

  gf_fd_put (priv->fdtable, fd_no);

  STACK_WIND (frame, 
	      server_close_cbk, 
	      bound_xl,
	      bound_xl->fops->close,
	      fd);
  
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
	      gf_hdr_common_t *hdr, size_t hdrlen,
	      char *buf, size_t buflen)
{
  gf_fop_fsync_req_t *req = NULL;
  server_proto_priv_t *priv = NULL;
  int32_t fd_no = -1;
  fd_t *fd = NULL;
  int32_t flags = 0;

  priv = SERVER_PRIV (frame);

  req = gf_param (hdr);

  fd_no = ntoh64 (req->fd);
  flags = ntoh32 (req->data);

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_fsync_cbk (frame, NULL, frame->this,
			-1, EINVAL);
      return -1;
    }
  
  STACK_WIND (frame, 
	      server_fsync_cbk,
	      bound_xl,
	      bound_xl->fops->fsync,
	      fd, flags);

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
server_flush (call_frame_t *frame, xlator_t *bound_xl,
	      gf_hdr_common_t *hdr, size_t hdrlen,
	      char *buf, size_t buflen)
{
  gf_fop_flush_req_t *req = NULL;
  fd_t *fd = NULL;
  int32_t fd_no = -1;
  server_proto_priv_t *priv = SERVER_PRIV (frame);

  req = gf_param (hdr);

  fd_no = ntoh64 (req->fd);

  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_flush_cbk (frame, NULL, frame->this,
			   -1, EINVAL);
      return -1;
    }


  STACK_WIND (frame, 
	      server_flush_cbk, 
	      bound_xl,
	      bound_xl->fops->flush,
	      fd);
  
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
		  gf_hdr_common_t *hdr, size_t hdrlen,
		  char *buf, size_t buflen)
{
  gf_fop_ftruncate_req_t *req = NULL;
  server_proto_priv_t *priv = NULL;
  off_t offset = 0;
  int fd_no = -1;
  fd_t *fd = NULL; 

  req = gf_param (hdr);

  offset = ntoh64 (req->offset);
  fd_no  = ntoh64 (req->fd);

  priv = SERVER_PRIV (frame);
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_ftruncate_cbk (frame, NULL, frame->this,
			    -1, EINVAL, NULL);

      return -1;
    }

  STACK_WIND (frame, 
	      server_ftruncate_cbk, 
	      bound_xl,
	      bound_xl->fops->ftruncate,
	      fd,
	      offset);

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
	      gf_hdr_common_t *hdr, size_t hdrlen,
	      char *buf, size_t buflen)
{
  gf_fop_fstat_req_t *req = NULL;
  server_proto_priv_t *priv = NULL;
  int fd_no = -1;
  fd_t *fd = NULL; 

  req = gf_param (hdr);

  fd_no  = ntoh64 (req->fd);

  priv = SERVER_PRIV (frame);
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_fstat_cbk (frame, NULL, frame->this,
			-1, EINVAL, NULL);

      return -1;
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
		 gf_hdr_common_t *hdr, size_t hdrlen,
		 char *buf, size_t buflen)
{
  off_t offset = 0;
  loc_t loc = {0,};
  call_stub_t *truncate_stub = NULL;
  gf_fop_truncate_req_t *req = NULL;

  req = gf_param (hdr);

  offset    = ntoh64 (req->offset);
  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  
  truncate_stub = fop_truncate_stub (frame, 
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

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);
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
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
  loc_t loc = {0,};
  call_stub_t *unlink_stub = NULL;
  gf_fop_unlink_req_t *req = NULL;

  req = gf_param (hdr);

  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  
  unlink_stub = fop_unlink_stub (frame, 
				 server_unlink_resume,
				 &loc);
  
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
		 gf_hdr_common_t *hdr, size_t hdrlen,
		 char *buf, size_t buflen)
{
  int32_t flags = 0;
  loc_t loc = {0,};
  call_stub_t *setxattr_stub = NULL;
  gf_fop_setxattr_req_t *req = NULL;
  dict_t *dict = NULL;
  int32_t dict_len = 0;

  req = gf_param (hdr);
  dict_len = ntoh32 (req->dict_len);

  /* NOTE: (req->dict + dict_len) will be the memory location which houses loc->path,
   * in the protocol data.
   */
  loc.path  = req->dict + dict_len;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  flags = ntoh32 (req->flags);
  {
    /* Unserialize the dictionary */
    char *buf = memdup (req->dict, dict_len);
    dict = get_new_dict ();
    dict_unserialize (buf, dict_len, &dict);
    dict->extra_free = buf;
  }

  setxattr_stub = fop_setxattr_stub (frame, 
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
		 gf_hdr_common_t *hdr, size_t hdrlen,
		 char *buf, size_t buflen)
{
  int32_t name_len = 0;
  char *name = NULL;
  loc_t loc = {0,};
  gf_fop_getxattr_req_t *req = NULL;
  call_stub_t *getxattr_stub = NULL;

  req = gf_param (hdr);

  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  name_len = ntoh32 (req->name_len);
  if (name_len)
    name = (req->name + strlen (req->path) + 1);

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
		    gf_hdr_common_t *hdr, size_t hdrlen,
		    char *buf, size_t buflen)
{
  char *name = NULL;
  loc_t loc = {0,};
  gf_fop_removexattr_req_t *req = NULL;
  call_stub_t *removexattr_stub = NULL;

  req = gf_param (hdr);

  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);

  name = (req->name + strlen (req->path) + 1);

  removexattr_stub = fop_removexattr_stub (frame, 
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
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
  loc_t loc = {0,};
  gf_fop_statfs_req_t *req = NULL;

  req = gf_param (hdr);

  loc.path  = req->path;
  loc.ino   = 1;
  loc.inode = dummy_inode (BOUND_XL(frame)->itable);

  STACK_WIND (frame,
	      server_statfs_cbk,
	      BOUND_XL (frame),
	      BOUND_XL (frame)->fops->statfs,
	      &loc);

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
server_opendir (call_frame_t *frame, xlator_t *bound_xl,
		gf_hdr_common_t *hdr, size_t hdrlen,
		char *buf, size_t buflen)
{
  loc_t loc = {0,};
  call_stub_t *opendir_stub = NULL;
  gf_fop_opendir_req_t *req = NULL;

  req = gf_param (hdr);

  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
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
server_closedir (call_frame_t *frame, xlator_t *bound_xl,
		 gf_hdr_common_t *hdr, size_t hdrlen,
		 char *buf, size_t buflen)
{
  gf_fop_closedir_req_t *req = NULL;
  fd_t *fd = NULL;
  int32_t fd_no = -1;
  server_proto_priv_t *priv = NULL;

  req = gf_param (hdr);

  fd_no = ntoh64 (req->fd);

  priv = SERVER_PRIV (frame);
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_closedir_cbk (frame, NULL, frame->this,
			   -1, EINVAL);
      return -1;
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
		 gf_hdr_common_t *hdr, size_t hdrlen,
		 char *buf, size_t buflen)
{
  gf_fop_getdents_req_t *req = NULL;
  server_proto_priv_t *priv = NULL;
  off_t offset = 0;
  int fd_no = -1;
  fd_t *fd = NULL; 

  req = gf_param (hdr);

  offset = ntoh64 (req->offset);
  fd_no  = ntoh64 (req->fd);

  priv = SERVER_PRIV (frame);
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_getdents_cbk (frame, NULL, frame->this,
			   -1, EINVAL, NULL, 0);

      return -1;
    }


  STACK_WIND (frame, 
	      server_getdents_cbk, 
	      bound_xl,
	      bound_xl->fops->getdents,
	      fd,
	      ntoh32 (req->size),
	      ntoh64 (req->offset),
	      ntoh32 (req->flags));

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
server_readdir (call_frame_t *frame, xlator_t *bound_xl,
		gf_hdr_common_t *hdr, size_t hdrlen,
		char *buf, size_t buflen)
{
  gf_fop_readdir_req_t *req = NULL;
  size_t size = 0;
  off_t offset = 0;
  int fd_no = -1;
  server_proto_priv_t *priv = NULL;
  fd_t *fd = NULL; 

  req = gf_param (hdr);

  size   = ntoh32 (req->size);
  offset = ntoh64 (req->offset);
  fd_no  = ntoh64 (req->fd);


  priv = SERVER_PRIV (frame);
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_readdir_cbk (frame, NULL, frame->this,
			 -1, EINVAL, NULL);

      return -1;
    }

  STACK_WIND (frame, 
	      server_readdir_cbk, 
	      bound_xl,
	      bound_xl->fops->readdir,
	      fd, size, offset);
  
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
		 gf_hdr_common_t *hdr, size_t hdrlen,
		 char *buf, size_t buflen)
{
  gf_fop_fsyncdir_req_t *req = NULL;
  server_proto_priv_t *priv = NULL;
  int32_t fd_no = -1;
  fd_t *fd = NULL;
  int32_t flags = 0;
  
  priv = SERVER_PRIV (frame);
  
  req = gf_param (hdr);

  //fd_no = ntoh32 (req->fd);
  flags = ntoh32 (req->data);
  
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);
  
  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);
      
      server_fsyncdir_cbk (frame, NULL, frame->this,
			   -1, EINVAL);
      return -1;
    }
  
  STACK_WIND (frame, 
	      server_fsyncdir_cbk,
	      bound_xl,
	      bound_xl->fops->fsyncdir,
	      fd, flags);
  
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
	      gf_hdr_common_t *hdr, size_t hdrlen,
	      char *buf, size_t buflen)
{
  mode_t mode = 0;
  dev_t dev = 0;
  loc_t loc = {0,};
  gf_fop_mknod_req_t *req = NULL;

  req = gf_param (hdr);

  mode = ntoh32 (req->mode);
  dev = ntoh64 (req->dev);
  loc.path  = req->path;
  loc.inode = dummy_inode (bound_xl->itable);

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
	      gf_hdr_common_t *hdr, size_t hdrlen,
	      char *buf, size_t buflen)
{
  mode_t mode = 0;
  loc_t loc = {0,};
  gf_fop_mkdir_req_t *req = NULL;

  req = gf_param (hdr);
  
  mode = ntoh32 (req->mode);
  loc.path  = req->path;
  loc.inode = dummy_inode (bound_xl->itable);

  STACK_WIND (frame, 
	      server_mkdir_cbk, 
	      bound_xl,
	      bound_xl->fops->mkdir,
	      &loc,
	      mode);
  
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
	      gf_hdr_common_t *hdr, size_t hdrlen,
	      char *buf, size_t buflen)
{
  loc_t loc = {0,};
  call_stub_t *rmdir_stub = NULL;
  gf_fop_rmdir_req_t *req = NULL;

  req = gf_param (hdr);

  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  
  rmdir_stub = fop_rmdir_stub (frame, 
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
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
  char *path = NULL;
  gf_fop_rmelem_req_t *req = NULL;

  req  = gf_param (hdr);
  path = req->path;

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
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
  char *path = NULL;
  gf_fop_incver_req_t *req = NULL;
  server_proto_priv_t *priv = NULL;
  int32_t fd_no = 0;
  fd_t *fd = NULL;

  req = gf_param (hdr);
  path  = req->path;
  fd_no = ntoh64 (req->fd);

  priv = SERVER_PRIV (frame);
  if (fd_no)
    fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (fd_no && fd == NULL) {
    gf_log (frame->this->name, GF_LOG_ERROR,
	    "unresolved fd %d", fd_no);
    server_incver_cbk (frame, NULL, frame->this, -1, EINVAL);
    return 0;
  }

  STACK_WIND (frame,
	      server_incver_cbk,
	      bound_xl,
	      bound_xl->fops->incver,
	      path,
	      fd);

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
	      gf_hdr_common_t *hdr, size_t hdrlen,
	      char *buf, size_t buflen)
{
  uid_t uid = 0;
  gid_t gid = 0;
  loc_t loc = {0,};
  call_stub_t *chown_stub = NULL;
  gf_fop_chown_req_t *req = NULL;

  req = gf_param (hdr);
  uid      = ntoh32 (req->uid);
  gid      = ntoh32 (req->gid);
  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  
  chown_stub = fop_chown_stub (frame, 
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
	      gf_hdr_common_t *hdr, size_t hdrlen,
	      char *buf, size_t buflen)
{
  mode_t mode = 0;
  loc_t loc = {0,};
  call_stub_t *chmod_stub = NULL;
  gf_fop_chmod_req_t *req = NULL;

  req       = gf_param (hdr);
  mode      = ntoh32 (req->mode);
  loc.path  = req->path;
  loc.ino   = ntoh64 (req->oldino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  
  chmod_stub = fop_chmod_stub (frame, 
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
		gf_hdr_common_t *hdr, size_t hdrlen,
		char *buf, size_t buflen)
{
  struct timespec tv[2];
  loc_t loc = {0,};
  call_stub_t *utimens_stub = NULL;
  gf_fop_utimens_req_t *req = NULL;

  req = gf_param (hdr);

  tv[0].tv_sec  = ntoh32 (req->tv[0].tv_sec);
  tv[0].tv_nsec = ntoh32 (req->tv[0].tv_nsec);
  tv[1].tv_sec  = ntoh32 (req->tv[1].tv_sec);
  tv[1].tv_nsec = ntoh32 (req->tv[1].tv_nsec);

  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  
  utimens_stub = fop_utimens_stub (frame, 
				   server_utimens_resume,
				   &loc,
				   tv);
  
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
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
  int32_t mask = 0;
  loc_t loc = {0,};
  call_stub_t *access_stub = NULL;
  gf_fop_access_req_t *req = NULL;

  req = gf_param (hdr);
  mask      = ntoh32 (req->mask);
  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  
  access_stub = fop_access_stub (frame, 
				 server_access_resume,
				 &loc,
				 mask);
  
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
		gf_hdr_common_t *hdr, size_t hdrlen,
		char *buf, size_t buflen)
{
  char *link = NULL;
  loc_t loc = {0,};
  gf_fop_symlink_req_t *req = NULL;

  req       = gf_param (hdr);

  link      = (req->newpath + 1 + strlen (req->oldpath));
  loc.path  = req->oldpath;
  loc.inode = dummy_inode (bound_xl->itable);
  
  STACK_WIND (frame, 
	      server_symlink_cbk, 
	      bound_xl,
	      bound_xl->fops->symlink,
	      link,
	      &loc);

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
	     gf_hdr_common_t *hdr, size_t hdrlen,
	     char *buf, size_t buflen)
{
  char *linkname = NULL;
  loc_t loc = {0,};
  call_stub_t *link_stub = NULL;
  gf_fop_link_req_t *req = NULL;

  req       = gf_param (hdr);

  linkname  = (req->newpath + strlen (req->oldpath) + 1);
  loc.path  = req->oldpath;
  loc.ino   = ntoh64 (req->oldino);
  loc.inode = inode_search (bound_xl->itable, loc.ino, NULL);
  
  link_stub = fop_link_stub (frame, 
			     server_link_resume,
			     &loc,
			     linkname);
  
  if (loc.inode) {
    /* unref()ing ref() from inode_search(), since fop_open_stub has kept
     * a reference for inode */
    inode_unref (loc.inode);
  }
  
  if (!loc.inode) {
    /* make a call stub and call lookup to get the inode structure.
     * resume call after lookup is successful */
    frame->local = link_stub;
    loc.inode = dummy_inode (BOUND_XL(frame)->itable);

    STACK_WIND (frame,
		server_stub_cbk,
		bound_xl,
		bound_xl->fops->lookup,
		&loc,
		0);
  } else {
    call_resume (link_stub);
  }

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
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
  loc_t oldloc = {0,};
  loc_t newloc = {0,};
  call_stub_t *rename_stub = NULL;
  gf_fop_rename_req_t *req = NULL;

  req = gf_param (hdr);

  oldloc.path  = req->oldpath;
  newloc.path  = (req->newpath + strlen (req->oldpath) + 1);

  oldloc.ino   = ntoh64 (req->oldino); 
  oldloc.inode = inode_search (bound_xl->itable, oldloc.ino, NULL);

  newloc.ino   = ntoh64 (req->newino);
  newloc.inode = inode_search (bound_xl->itable, newloc.ino, NULL);

  /* :O
     frame->this = bound_xl;
  */
  rename_stub = fop_rename_stub (frame,
				 server_rename_resume,
				 &oldloc,
				 &newloc);
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
	   gf_hdr_common_t *hdr, size_t hdrlen,
	   char *buf, size_t buflen)
{
  struct flock lock = {0, };
  gf_fop_lk_req_t *req = NULL;
  server_proto_priv_t *priv = NULL;
  int fd_no = -1;
  fd_t *fd = NULL; 
  int cmd, type;

  req = gf_param (hdr);

  fd_no  = ntoh64 (req->fd);

  priv = SERVER_PRIV (frame);
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_lk_cbk (frame, NULL, frame->this,
		     -1, EINVAL, NULL);

      return -1;
    }

  cmd =  ntoh32 (req->cmd);
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

  type = ntoh32 (req->type);

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

  gf_flock_to_flock (&req->flock, &lock);

  STACK_WIND (frame, 
	      server_lk_cbk, 
	      bound_xl,
	      bound_xl->fops->lk,
	      fd,
	      cmd,
	      &lock);

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
		 gf_hdr_common_t *hdr, size_t hdrlen,
		 char *buf, size_t buflen)
{
  gf_fop_setdents_req_t *req = NULL;
  server_proto_priv_t *priv = NULL;
  dir_entry_t *entry = NULL;
  int fd_no = -1;
  fd_t *fd = NULL; 
  int32_t nr_count = 0;
  int32_t flag = 0;

  req = gf_param (hdr);

  fd_no  = ntoh64 (req->fd);

  priv = SERVER_PRIV (frame);
  fd = gf_fd_fdptr_get (priv->fdtable, fd_no);

  if (!fd)
    {
      gf_log (frame->this->name, GF_LOG_ERROR,
	      "unresolved fd %d", fd_no);

      server_setdents_cbk (frame, NULL, frame->this,
			    -1, EINVAL);

      return -1;
    }
  
  nr_count = ntoh32 (req->count);

  {
    dir_entry_t *trav = NULL, *prev = NULL;
    int32_t count, i, bread;
    char *ender = NULL, *buffer_ptr = NULL;
    char tmp_buf[512] = {0,};

    entry = calloc (1, sizeof (dir_entry_t));
    ERR_ABORT (entry);
    prev = entry;
    buffer_ptr = req->buf;
    
    for (i = 0; i < nr_count ; i++) {
      bread = 0;
      trav = calloc (1, sizeof (dir_entry_t));
      ERR_ABORT (trav);

      ender = strchr (buffer_ptr, '/');
      if (!ender)
	break;
      count = ender - buffer_ptr;
      trav->name = calloc (1, count + 2);
      ERR_ABORT (trav->name);

      strncpy (trav->name, buffer_ptr, count);
      bread = count + 1;
      buffer_ptr += bread;
      
      ender = strchr (buffer_ptr, '\n');
      if (!ender)
	break;
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
      } else 
	trav->link = "";
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
	      fd,
	      flag,
	      entry,
	      nr_count);

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



/* xxx_MOPS */

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
	     gf_hdr_common_t *hdr, size_t hdrlen,
	     char *buf, size_t buflen)
{
  int32_t ret = -1;
  int32_t op_errno = ENOENT;
  int32_t spec_fd = -1;
  char tmp_filename[4096] = {0,};
  char *filename = GLUSTERFSD_SPEC_PATH;
  int32_t file_len = 0;
  struct stat stbuf = {0,};
  struct sockaddr_in *_sock = NULL;
  gf_hdr_common_t *_hdr = NULL;
  gf_mop_getspec_rsp_t *rsp = NULL;
  size_t _hdrlen = 0;

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
    
    file_len = stbuf.st_size;
  }

 fail:
  op_errno = errno;

  _hdrlen = gf_hdr_len (rsp, file_len + 1);
  _hdr    = gf_hdr_new (rsp, file_len + 1);
  rsp     = gf_param (_hdr);

  _hdr->rsp.op_ret   = hton32 (ret);
  _hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (file_len) {
    gf_full_read (spec_fd, rsp->spec, file_len);
    close (spec_fd);
  }
  protocol_server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_GETSPEC,
			 _hdr, _hdrlen, NULL, 0, NULL);

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
  gf_hdr_common_t *hdr = NULL;
  gf_mop_checksum_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  if (op_ret >= 0) {
    memcpy (rsp->fchecksum, fchecksum, 4096);
    memcpy (rsp->dchecksum, dchecksum, 4096);
  }

  protocol_server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_CHECKSUM,
			 hdr, hdrlen, NULL, 0, NULL);

  return 0;
}

int32_t
server_checksum (call_frame_t *frame,
		 xlator_t *bound_xl,
		 gf_hdr_common_t *hdr, size_t hdrlen,
		 char *buf, size_t buflen)
{
  loc_t loc = {0,};
  int32_t flag = 0;
  gf_mop_checksum_req_t *req = NULL;

  req = gf_param (hdr);

  loc.path  = req->path;
  loc.ino   = ntoh64 (req->ino);
  loc.inode = NULL;
  flag      = ntoh32 (req->flag);

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
	     gf_hdr_common_t *hdr, size_t hdrlen,
	     char *buf, size_t buflen)
{
#if 0
  gf_mop_setspec_rsp_t *rsp = NULL;
  int32_t ret = -1;
  int32_t spec_fd = -1;
  int32_t remote_errno = 0;
  void *file_data = NULL;
  int32_t file_data_len = 0;
  int32_t offset = 0;

  /* TODO : 
  file_data = req->buf;
  file_data_len = req->size;
  */

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
  
  {
    gf_hdr_common_t *_hdr = NULL;
    size_t _hdrlen = 0;
    
    _hdrlen = gf_hdr_len (rsp, 0);
    _hdr    = gf_hdr_new (rsp, 0);
    rsp    = gf_param (_hdr);

    _hdr->rsp.op_ret   = hton32 (ret);
    _hdr->rsp.op_errno = hton32 (gf_errno_to_error (remote_errno));
    
    protocol_server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_SETSPEC,
			   _hdr, _hdrlen, NULL, 0, NULL);
  }

#endif /* if 0 */

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
  gf_hdr_common_t *hdr = NULL;
  gf_mop_lock_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_LOCK,
			 hdr, hdrlen, NULL, 0, NULL);

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
	  gf_hdr_common_t *hdr, size_t hdrlen,
	  char *buf, size_t buflen)
{
  char *path = NULL;
  gf_mop_lock_req_t *req = NULL;

  req = gf_param (hdr);

  path = req->name;

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
  gf_hdr_common_t *hdr = NULL;
  gf_mop_unlock_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (op_ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_UNLOCK,
			 hdr, hdrlen, NULL, 0, NULL);

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
	    gf_hdr_common_t *hdr, size_t hdrlen,
	    char *buf, size_t buflen)
{
  char *path = NULL;
  gf_mop_lock_req_t *req = NULL;

  req = gf_param (hdr);

  path = req->name;

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
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
  gf_hdr_common_t *_hdr = NULL;
  gf_mop_listlocks_rsp_t *rsp = NULL;
  size_t _hdrlen = 0;

  _hdrlen = gf_hdr_len (rsp, 0);
  _hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (_hdr);

  hdr->rsp.op_ret   = hton32 (-1);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (ENOSYS));

  protocol_server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_LISTLOCKS,
			 _hdr, _hdrlen, NULL, 0, NULL);

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
	       gf_hdr_common_t *hdr, size_t hdrlen,
	       char *buf, size_t buflen)
{
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
	       gf_hdr_common_t *req_hdr,
	       size_t req_hdrlen,
	       char *req_buf,
	       size_t req_buflen)
{
  int32_t ret = -1;
  int32_t remote_errno = 0;
  dict_t *reply = NULL;
  server_proto_priv_t *priv = NULL;
  server_private_t *server_priv = NULL;
  data_t *name_data = NULL, *version_data = NULL;
  char *name = NULL, *version = NULL;
  xlator_t *xl = NULL;
  struct sockaddr_in *_sock = NULL;
  dict_t *config_params = NULL;
  dict_t *params = NULL;
  gf_hdr_common_t *rsp_hdr = NULL;
  size_t rsp_hdrlen = -1;
  gf_mop_setvolume_req_t *req = NULL;
  gf_mop_setvolume_rsp_t *rsp = NULL;
  size_t dict_len = -1;

  params = get_new_dict ();
  reply  = get_new_dict ();
  req    = gf_param (req_hdr);
  config_params = dict_copy (frame->this->options, NULL);

  dict_unserialize (req->buf, ntoh32 (req_hdr->size), &params);

  priv = SERVER_PRIV (frame);

  server_priv = TRANSPORT_OF (frame)->xl->private;

  version_data = dict_get (params, "version");
  if (!version_data) {
    remote_errno = EINVAL;
    dict_set (reply, "ERROR",
	      str_to_data ("No version number specified"));
    goto fail;
  }

  version = data_to_str (version_data);
  if (strcmp (version, PACKAGE_VERSION)) {
    char *msg;
    asprintf (&msg, 
	      "Version mismatch: client(%s) Vs server (%s)", 
	      version, PACKAGE_VERSION);
    remote_errno = EINVAL;
    dict_set (reply, "ERROR", data_from_dynstr (msg));
    goto fail;
  }
  
 
  name_data = dict_get (params,
			"remote-subvolume");
  if (!name_data) {
    remote_errno = EINVAL;
    dict_set (reply, "ERROR", 
	      str_to_data ("No remote-subvolume option specified"));
    goto fail;
  }

  name = data_to_str (name_data);
  xl = get_xlator_by_name (frame->this, name);

  if (!xl) {
    char *msg;
    asprintf (&msg, "remote-subvolume \"%s\" is not found", name);
    dict_set (reply, "ERROR", data_from_dynstr (msg));
    remote_errno = ENOENT;
    goto fail;
  } 

  _sock = &(TRANSPORT_OF (frame))->peerinfo.sockaddr;
  dict_set (params, "peer-ip", str_to_data(inet_ntoa (_sock->sin_addr)));
  dict_set (params, "peer-port", data_from_uint16 (ntohs (_sock->sin_port)));

  if (!server_priv->auth_modules) {
    gf_log (TRANSPORT_OF (frame)->xl->name, 
	    GF_LOG_ERROR,
	    "Authentication module not initialized");
  }

  if (gf_authenticate (params, config_params, server_priv->auth_modules) == AUTH_ACCEPT) {
    gf_log (TRANSPORT_OF (frame)->xl->name,  GF_LOG_DEBUG,
	    "accepted client from %s:%d",
	    inet_ntoa (_sock->sin_addr), ntohs (_sock->sin_port));
    ret = 0;
    priv->bound_xl = xl;
    dict_set (reply, "ERROR", str_to_data ("Success"));
  } else {
    gf_log (TRANSPORT_OF (frame)->xl->name, GF_LOG_ERROR,
	    "Cannot authenticate client from %s:%d",
	    inet_ntoa (_sock->sin_addr), ntohs (_sock->sin_port));
    ret = -1;
    remote_errno = EACCES;
    dict_set (reply, "ERROR", str_to_data ("Authentication failed"));
    goto fail;
  }

  if (!priv->bound_xl) {
    dict_set (reply, "ERROR", 
	      str_to_data ("Check volume spec file and handshake options"));
    ret = -1;
    remote_errno = EACCES;
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

  dict_set (reply, "RET", data_from_int32 (ret));
  dict_set (reply, "ERRNO", data_from_int32 (gf_errno_to_error (remote_errno)));

  dict_len = dict_serialized_length (reply);
  rsp_hdr = gf_hdr_new (rsp, dict_len);
  rsp_hdrlen = gf_hdr_len (rsp, dict_len);
  rsp = gf_param (rsp_hdr);
  dict_serialize (reply, rsp->buf);


  rsp_hdr->rsp.op_ret = hton32 (ret);
  rsp_hdr->rsp.op_errno = hton32 (gf_errno_to_error (remote_errno));

  protocol_server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_SETVOLUME, 
			 rsp_hdr, rsp_hdrlen, NULL, 0, NULL);

  dict_destroy (params);
  dict_destroy (reply);
  dict_destroy (config_params);

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

  gf_hdr_common_t *hdr = NULL;
  gf_mop_stats_rsp_t *rsp = NULL;
  char buffer[256] = {0,};
  size_t hdrlen = 0;
  int buf_len = 0;

  if (ret >= 0)
    {  	
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

      buf_len = strlen (buffer);
  }

  hdrlen = gf_hdr_len (rsp, buf_len + 1);
  hdr    = gf_hdr_new (rsp, buf_len + 1);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  strcpy (rsp->buf, buffer);

  protocol_server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_STATS,
			 hdr, hdrlen, NULL, 0, NULL);

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
	   gf_hdr_common_t *hdr, size_t hdrlen,
	   char *buf, size_t buflen)
{
  int32_t flag = 0;
  gf_mop_stats_req_t *req = NULL;

  req = gf_param (hdr);

  flag = ntoh32 (req->flags);
  
  STACK_WIND (frame, 
	      server_mop_stats_cbk, 
	      bound_xl,
	      bound_xl->mops->stats,
	      flag);
  
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
#if 0
  gf_hdr_common_t *hdr = NULL;
  gf_mop_fsck_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (ret);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

  protocol_server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_FSCK,
			 hdr, hdrlen, NULL, 0, NULL);
#endif /* if 0 */

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
	  gf_hdr_common_t *hdr, size_t hdrlen,
	  char *buf, size_t buflen)
{
#if 0
  int flag = 0;
  gf_mop_fsck_req_t *req = NULL;

  req = gf_param (hdr);

  flag = ntoh32 (req->flag);

  STACK_WIND (frame, 
	      server_mop_fsck_cbk, 
	      bound_xl,
	      bound_xl->mops->fsck,
	      flag);
#endif /* if 0 */

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
  gf_hdr_common_t *hdr = NULL;
  gf_fop_forget_rsp_t *rsp = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (rsp, 0);
  hdr    = gf_hdr_new (rsp, 0);
  rsp    = gf_param (hdr);

  hdr->rsp.op_ret   = hton32 (-1);
  hdr->rsp.op_errno = hton32 (gf_errno_to_error (ENOSYS));
  
  protocol_server_reply (frame, type, opcode,
			 hdr, hdrlen, NULL, 0, NULL);

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
get_frame_for_call (transport_t *trans, gf_hdr_common_t *hdr)
{
  call_pool_t *pool = NULL;
  call_ctx_t *_call = NULL;
  server_state_t *state = NULL;
  server_proto_priv_t *priv = NULL;

  priv = trans->xl_private;
  pool = trans->xl->ctx->pool;

  if (!pool)
    {
      pool = trans->xl->ctx->pool = calloc (1, sizeof (*pool));
      if (!pool)
	{
	  gf_log (trans->xl->name, GF_LOG_ERROR, "could not malloc");
	  return NULL;
	}
      LOCK_INIT (&pool->lock);
      INIT_LIST_HEAD (&pool->all_frames);
    }

  state = calloc (1, sizeof (*state));
  if (!state)
    {
      gf_log (trans->xl->name, GF_LOG_ERROR, "could not malloc");
      return NULL;
    }

  _call = (void *) calloc (1, sizeof (*_call));
  if (!_call)
    {
      FREE (state);
      gf_log (trans->xl->name, GF_LOG_ERROR, "could not malloc");
      return NULL;
    }

  _call->pool = pool;

  LOCK (&pool->lock);
  {
    list_add (&_call->all_frames, &pool->all_frames);
  }
  UNLOCK (&pool->lock);

  state->bound_xl = priv->bound_xl;
  state->trans    = transport_ref (trans);

  _call->trans    = trans;
  _call->state    = state;                        /* which socket */
  _call->unique   = ntoh64 (hdr->callid);         /* which call */

  _call->frames.root = _call;
  _call->frames.this = trans->xl;
  _call->frames.op   = ntoh32 (hdr->op);
  _call->frames.type = ntoh32 (hdr->type);
  _call->uid         = ntoh32 (hdr->req.uid);
  _call->gid         = ntoh32 (hdr->req.gid);
  _call->pid         = ntoh32 (hdr->req.pid);

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
typedef int32_t (*gf_op_t) (call_frame_t *frame, xlator_t *bould_xl,
			    gf_hdr_common_t *hdr, size_t hdrlen,
			    char *buf, size_t buflen);


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



int
protocol_server_interpret (xlator_t *this, transport_t *trans,
			   char *hdr_p, size_t hdrlen, char *buf, size_t buflen)
{
  int ret = -1;
  gf_hdr_common_t *hdr = NULL;
  xlator_t *bound_xl = NULL;
  server_proto_priv_t *priv = NULL;
  call_frame_t *frame = NULL;
  int type = -1, op = -1;

  hdr  = (gf_hdr_common_t *)hdr_p;
  type = ntoh32 (hdr->type);
  op   = ntoh32 (hdr->op);

  priv = trans->xl_private;
  bound_xl = priv->bound_xl;

  switch (type)
    {
    case GF_OP_TYPE_FOP_REQUEST:
      if (op < 0 || op > GF_FOP_MAXVALUE)
	{
	  gf_log (this->name, GF_LOG_ERROR,
		  "invalid fop %d from client %s", op,
		  inet_ntoa (trans->peerinfo.sockaddr.sin_addr));
	  break;
	}
      if (bound_xl == NULL)
	{
	  gf_log (this->name, GF_LOG_ERROR,
		  "Received fop %d before authentication.", op);
	  break;
	}
      frame = get_frame_for_call (trans, hdr);
      ret = gf_fops[op] (frame, bound_xl, hdr, hdrlen, buf, buflen);
      break;

    case GF_OP_TYPE_MOP_REQUEST:

      if (op < 0 || op > GF_MOP_MAXVALUE)
	{
	  gf_log (this->name, GF_LOG_ERROR,
		  "invalid mop %d from client %s", op,
		  inet_ntoa (trans->peerinfo.sockaddr.sin_addr));
	  break;
	}
      frame = get_frame_for_call (trans, hdr);
      ret = gf_mops[op] (frame, bound_xl, hdr, hdrlen, buf, buflen);
      break;

    default:
      break;
    }

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
  call_ctx_t *_call = NULL;
  call_pool_t *pool = trans->xl->ctx->pool;
  server_proto_priv_t *priv = trans->xl_private;
  server_state_t *state;

  _call = (void *) calloc (1, sizeof (*_call));
  ERR_ABORT (_call);

  if (!pool) {
    pool = trans->xl->ctx->pool = calloc (1, sizeof (*pool));
    ERR_ABORT (pool);

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
  ERR_ABORT (state);

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
  int32_t error = 0;

  gf_log (this->name, GF_LOG_DEBUG, "protocol/server xlator loaded");

  if (!this->children) {
    gf_log (this->name, GF_LOG_ERROR,
	    "protocol/server should have subvolume");
    return -1;
  }

  if (!dict_get (this->options, "transport-type")) {
    gf_log (this->name, GF_LOG_DEBUG,
	    "missing 'option transport-type'. defaulting to \"tcp\"");
    dict_set (this->options, "transport-type", str_to_data ("tcp"));
  }
  trans = transport_load (this->options, this);

  if (!trans) {
    gf_log (this->name, GF_LOG_ERROR, "failed to load transport");
    return -1;
  }

  transport_listen (trans);

  server_priv = calloc (1, sizeof (*server_priv));
  ERR_ABORT (server_priv);

  server_priv->trans = trans;

  server_priv->auth_modules = get_new_dict ();
  dict_foreach (this->options, get_auth_types, server_priv->auth_modules);
  error = gf_auth_init (server_priv->auth_modules);
  
  if (error) {
    dict_destroy (server_priv->auth_modules);
    return error;
  }

  this->private = server_priv;

  conf = calloc (1, sizeof (server_conf_t));
  ERR_ABORT (conf);

  if (dict_get (this->options, "limits.transaction-size")) {
    conf->max_block_size = data_to_int32 (dict_get (this->options, 
						    "limits.trasaction-size"));
  } else {
    gf_log (this->name, GF_LOG_DEBUG,
	    "defaulting limits.transaction-size to %d", DEFAULT_BLOCK_SIZE);
    conf->max_block_size = DEFAULT_BLOCK_SIZE;
  }

#ifndef GF_DARWIN_HOST_OS
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
#endif

  trans->xl_private = conf;

  return 0;
}


int
protocol_server_pollin (xlator_t *this, transport_t *trans)
{
  char *hdr = NULL;
  size_t hdrlen = 0;
  char *buf = NULL;
  size_t buflen = 0;
  server_proto_priv_t *priv = NULL;
  server_conf_t *conf = NULL;
  int ret = -1;

  priv = trans->xl_private;
  conf = this->private;

  if (!priv)
    {
      priv = (void *) calloc (1, sizeof (*priv));
      ERR_ABORT (priv);

      trans->xl_private = priv;

      priv->fdtable = gf_fd_fdtable_alloc ();
      if (!priv->fdtable)
	{
	    gf_log (this->name, GF_LOG_ERROR,  "Cannot allocate fdtable");
	    return -1;
	}
      pthread_mutex_init (&priv->lock, NULL);
    }

  ret = transport_receive (trans, &hdr, &hdrlen, &buf, &buflen);

  if (ret == 0)
    ret = protocol_server_interpret (this, trans, hdr, hdrlen, buf, buflen);

  /* TODO: use mem-pool */
  FREE (hdr);

  return ret;
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
      ret = protocol_server_pollin (this, trans);
      break;
#if 0
      {
	gf_block_t *blk;
#endif
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
