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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif
#include <time.h>
#include <sys/uio.h>
#include <sys/resource.h>

#include <libgen.h>

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

#define TRANSPORT_OF(frame) ((transport_t *) STATE (frame)->trans)
#define CONNECTION_PRIVATE(frame)  ((connection_private_t *) TRANSPORT_OF(frame)->xl_private)

#define __TRANSPORT_OF(this)    ((((server_private_t *)this->private))->trans)

#define INODE_LRU_LIMIT(this)						\
	(((server_conf_t *)(__TRANSPORT_OF(this)->xl_private))->inode_lru_limit)

#define IS_ROOT_INODE(inode) (inode == inode->table->root)



server_state_t *
server_state_fill (call_frame_t *frame,
		   void *request, 
		   int type)
{
	server_state_t *state = STATE (frame);
	connection_private_t *connection_priv = CONNECTION_PRIVATE (frame);
	
	state->itable = BOUND_XL(frame)->itable;

	switch (type){
	case GF_FOP_LOOKUP:
	{
		gf_fop_lookup_req_t *req = request;
		state->need_xattr = ntoh32 (req->flags);
		state->ino    = ntoh64 (req->ino);
		state->path = req->path;
	}
	break;
	case GF_FOP_STAT:
	{
		gf_fop_stat_req_t *req = request;
		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);
	}
	break;
	case GF_FOP_STATFS:
	{
		gf_fop_statfs_req_t *req = request;
		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);
	}
	break;
	case GF_FOP_READLINK:
	{
		gf_fop_readlink_req_t *req = request;
		state->size  = ntoh32 (req->size);
		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);
	}
	break;
	case GF_FOP_UNLINK:
	{
		gf_fop_unlink_req_t *req = request;
		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);
	}
	break;
	case GF_FOP_ACCESS:
	{
		gf_fop_access_req_t *req = request;
		state->mask  = ntoh32 (req->mask);
		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);
	}
	break;
	case GF_FOP_RENAME:
	{
		gf_fop_rename_req_t *req = request;
		state->path  = req->oldpath;
		state->path2 = (req->newpath + strlen (req->oldpath) + 1);

		state->ino   = ntoh64 (req->oldino);
		state->ino2  = ntoh64 (req->newino);
	}
	break;
	case GF_FOP_LINK:
	{
		gf_fop_link_req_t *req = request;
		state->name  = (req->newpath + strlen (req->oldpath) + 1);
		state->path  = req->oldpath;
		state->ino   = ntoh64 (req->oldino);
	}
	break;
	case GF_FOP_SYMLINK:
	{
		gf_fop_symlink_req_t *req = request;
		state->name  = (req->newpath + strlen (req->oldpath) + 1);
		state->path  = req->oldpath;
	}
	break;
	case GF_FOP_SETXATTR:
	{
		gf_fop_setxattr_req_t *req = request;

		state->dict_len = ntoh32 (req->dict_len);

		/* NOTE: (req->dict + dict_len) will be the memory location which houses loc->path,
		 * in the protocol data.
		 */
		state->path  = req->dict + state->dict_len;
		state->ino   = ntoh64 (req->ino);
		state->flags = ntoh32 (req->flags);
	}
	break;
	case GF_FOP_GETXATTR:
	{
		gf_fop_getxattr_req_t *req = request;

		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);

		state->name_len = ntoh32 (req->name_len);
		if (state->name_len)
			state->name = (req->name + strlen (state->path) + 1);
	}
	break;
	case GF_FOP_REMOVEXATTR:
	{
		gf_fop_removexattr_req_t *req = request;
		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);
		
		state->name = (req->name + strlen (state->path) + 1);
	}
	break;
	case GF_FOP_MKNOD:
	{
		gf_fop_mknod_req_t *req = request;

		state->mode = ntoh32 (req->mode);
		state->dev = ntoh64 (req->dev);
		state->path  = req->path;
	}
	break;
	case GF_FOP_OPEN:
	{
		gf_fop_open_req_t *req = request;
		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);
		state->flags     = ntoh32 (req->flags);
	}
	break;
	case GF_FOP_CREATE:
	{
		gf_fop_create_req_t *req = request;
		
		state->mode  = ntoh32 (req->mode);
		state->flags = ntoh32 (req->flags);
		state->path  = req->path;
	}
	break;
	case GF_FOP_CHOWN:
	{
		gf_fop_chown_req_t *req = request;

		state->uid   = ntoh32 (req->uid);
		state->gid   = ntoh32 (req->gid);
		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);
	}
	break;
	case GF_FOP_CHMOD:
	{
		gf_fop_chmod_req_t *req = request;
		state->mode      = ntoh32 (req->mode);
		state->path  = req->path;
		state->ino   = ntoh64 (req->oldino);
	}
	break;
	case GF_FOP_UTIMENS:
	{
		gf_fop_utimens_req_t *req = request;

		gf_timespec_to_timespec (req->tv, state->tv);

		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);
	}
	break;
	case GF_FOP_MKDIR:
	{
		gf_fop_mkdir_req_t *req = request;
		state->mode = ntoh32 (req->mode);
		state->path  = req->path;
	}
	break;
	case GF_FOP_RMDIR:
	{
		gf_fop_rmdir_req_t *req = request;
		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);
	}
	break;
	case GF_FOP_OPENDIR:
	{
		gf_fop_opendir_req_t *req = request;
		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);
	}
	break;
	case GF_FOP_FLUSH:
	{
		gf_fop_flush_req_t *req = request;

		state->fd_no = ntoh64 (req->fd);
		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
	}
	break;
	case GF_FOP_FSYNC:
	{
		gf_fop_fsync_req_t *req = request;

		state->fd_no = ntoh64 (req->fd);
		state->flags = ntoh32 (req->data);
		
		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
	}
	break;
	case GF_FOP_FTRUNCATE:
	{
		gf_fop_ftruncate_req_t *req = request;
		state->offset = ntoh64 (req->offset);
		state->fd_no  = ntoh64 (req->fd);
		
		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
	}
	break;
	case GF_FOP_FCHMOD:
	{
		gf_fop_fchmod_req_t *req = request;
		state->mode   = ntoh32 (req->mode);
		state->fd_no  = ntoh64 (req->fd);

		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
	}
	break;
	case GF_FOP_FCHOWN:
	{
		gf_fop_fchown_req_t *req = request;
		state->uid   = ntoh32 (req->uid);
		state->gid   = ntoh32 (req->gid);
		state->fd_no = ntoh64 (req->fd);
		
		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
	}
	break;
	case GF_FOP_FSTAT:
	{
		gf_fop_fstat_req_t *req = request;
		state->fd_no  = ntoh64 (req->fd);

		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
	}
	break;
	break;
	case GF_FOP_TRUNCATE:
	{
		gf_fop_truncate_req_t *req = request;
		state->offset    = ntoh64 (req->offset);
		state->path  = req->path;
		state->ino   = ntoh64 (req->ino);
	}
	break;
	break;
	case GF_FOP_READ:
	{
		gf_fop_read_req_t *req = request;

		state->fd_no   = ntoh64 (req->fd);
		state->size    = ntoh32 (req->size);
		state->offset  = ntoh64 (req->offset);
		
		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
	}
	break;
	case GF_FOP_WRITE:
	{
		gf_fop_write_req_t *req = request;

		state->offset = ntoh64 (req->offset);
		state->fd_no  = ntoh64 (req->fd);
		
		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
	}
	break;
	case GF_FOP_LK:
	{
		gf_fop_lk_req_t *req = request;
		state->fd_no  = ntoh64 (req->fd);
		
		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);

		state->cmd =  ntoh32 (req->cmd);
		state->type = ntoh32 (req->type);
	}
	break;
	case GF_FOP_READDIR:
	{
		gf_fop_readdir_req_t *req = request;
		state->size   = ntoh32 (req->size);
		state->offset = ntoh64 (req->offset);
		state->fd_no  = ntoh64 (req->fd);
		
		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
	}
	break;
	case GF_FOP_GETDENTS:
	{
		gf_fop_getdents_req_t *req = request;

		state->size = ntoh32 (req->size);
		state->offset = ntoh64 (req->offset);
		state->flags = ntoh32 (req->flags);
		state->fd_no  = ntoh64 (req->fd);

		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);

	}
	break;
	case GF_FOP_SETDENTS:
	{
		gf_fop_setdents_req_t *req = request;

		state->fd_no  = ntoh64 (req->fd);
		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
		state->nr_count = ntoh32 (req->count);
	}
	break;
	case GF_FOP_FSYNCDIR:
	{
		gf_fop_fsyncdir_req_t *req = request;
		state->fd_no = ntoh64 (req->fd);
		state->flags = ntoh32 (req->data);

		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
	}
	break;
	case GF_FOP_XATTROP:
	{
		gf_fop_xattrop_req_t *req = request;
		
		state->dict_len = ntoh32 (req->dict_len);

		/* NOTE: (req->dict + dict_len) will be the memory location which houses loc->path,
		 * in the protocol data.
		 */
		state->path  = req->dict + state->dict_len;
		state->flags = ntoh32 (req->flags);
		state->fd_no = ntoh64 (req->fd);
		if (state->fd_no != -1)
			state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
	}
	break;
	case GF_FOP_ENTRYLK:
	{
		gf_fop_entrylk_req_t *req = request;
		
		state->cmd  = ntoh32 (req->cmd);
		state->type = ntoh32 (req->type);
		state->ino  = ntoh64 (req->ino);
		state->path = req->path;
		state->basename = (req->basename + strlen (state->path) + 1);
	}
	break;
	case GF_FOP_FENTRYLK:
	{
		gf_fop_fentrylk_req_t *req = request;
		
		state->cmd   = ntoh32 (req->cmd);
		state->type  = ntoh32 (req->type);
		state->ino   = ntoh64 (req->ino);
		state->fd_no = ntoh64 (req->fd);
		state->fd    = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);

		state->basename = req->basename;
	}
	break;

	case GF_FOP_INODELK:
	{
		gf_fop_inodelk_req_t *req = request;
		
		state->cmd = ntoh32 (req->cmd);
		switch (state->cmd) {
		case GF_LK_GETLK:
			state->cmd = F_GETLK;
			break;
		case GF_LK_SETLK:
			state->cmd = F_SETLK;
			break;
		case GF_LK_SETLKW:
			state->cmd = F_SETLKW;
			break;
		}

		state->type = ntoh32 (req->type);

		state->path = req->path;

		state->ino  = ntoh64 (req->ino);
		gf_flock_to_flock (&req->flock, &state->flock);

		switch (state->type) {
		case GF_LK_F_RDLCK: state->flock.l_type = F_RDLCK; break;
		case GF_LK_F_WRLCK: state->flock.l_type = F_WRLCK; break;
		case GF_LK_F_UNLCK: state->flock.l_type = F_UNLCK; break;
		}

	}
	break;
	case GF_FOP_FINODELK:
	{
		gf_fop_finodelk_req_t *req = request;
		
		state->cmd = ntoh32 (req->cmd);
		switch (state->cmd) {
		case GF_LK_GETLK:
			state->cmd = F_GETLK;
			break;
		case GF_LK_SETLK:
			state->cmd = F_SETLK;
			break;
		case GF_LK_SETLKW:
			state->cmd = F_SETLKW;
			break;
		}

		state->type = ntoh32 (req->type);

		state->fd_no = ntoh64 (req->fd);
		state->fd = gf_fd_fdptr_get (connection_priv->fdtable, state->fd_no);
		
		state->ino  = ntoh64 (req->ino);
		gf_flock_to_flock (&req->flock, &state->flock);

		switch (state->type) {
		case GF_LK_F_RDLCK: state->flock.l_type = F_RDLCK; break;
		case GF_LK_F_WRLCK: state->flock.l_type = F_WRLCK; break;
		case GF_LK_F_UNLCK: state->flock.l_type = F_UNLCK; break;
		}

	}
	break;

	default:
		break;
	}
	
	return state;
}

/* server_loc_fill - derive a loc_t for a given inode number
 * 
 * NOTE: make sure that @loc is empty, because any pointers it holds with reference will
 *       be leaked after returning from here.
 */
static //inline __attribute__((always_inline))
void
server_loc_fill (loc_t *loc,
		 server_state_t *state,
		 const char *path)
{
	inode_t *inode = NULL;
	inode_t *parent = NULL;
	char *name = NULL;
	char *parent_path = NULL;

	if (path) {
		parent_path = strdup (path);
		parent_path = dirname (parent_path);

		loc->path = strdup (path);
		name = strrchr (path, '/');
		if (name)
			name++;
	}

	inode = loc->inode;
	if (inode == NULL) {
		inode = inode_from_path (state->itable, path);
	
		loc->inode = inode;
		if (inode)
			loc->ino = inode->ino;
	}
	
	parent = loc->parent;
	if (parent == NULL) {
		parent = inode_from_path (state->itable, parent_path);
		
		loc->parent = parent;
	}

	loc->name = name;
	
	if (parent_path)
		free (parent_path);
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
	trans = state->trans;

	hdr->callid = hton64 (frame->root->unique);
	hdr->type   = hton32 (type);
	hdr->op     = hton32 (op);

	transport_submit (trans, (char *)hdr, hdrlen, vector, count, refs);

	STACK_DESTROY (frame->root);

	if (state)
		free_state (state);

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
		gf_stat_from_stat (&rsp->stat, stbuf);

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
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_FCHMOD);

	if (!state->fd)	{
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %d", state->fd_no);

		server_fchmod_cbk (frame, NULL, frame->this,
				   -1, EINVAL, NULL);

		return -1;
	}

	STACK_WIND (frame,
		    server_fchmod_cbk,
		    bound_xl,
		    bound_xl->fops->fchmod,
		    state->fd,
		    state->mode);

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

	if (op_ret == 0) {
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
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_FCHOWN);

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_fchown_cbk (frame, NULL, frame->this,
				   -1, EINVAL, NULL);

		return -1;
	}

	STACK_WIND (frame,
		    server_fchown_cbk,
		    bound_xl,
		    bound_xl->fops->fchown,
		    state->fd,
		    state->uid,
		    state->gid);

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
		gf_flock_from_flock (&rsp->flock, lock);

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_LK,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}

int32_t
gf_add_locker (struct _lock_table *table,
	       loc_t *loc,
	       fd_t *fd,
	       pid_t pid)
{
	int32_t ret = -1;
	struct _locker *new = NULL;
	uint8_t dir = 0;

	new = calloc (1, sizeof (struct _locker));
	if (new == NULL) {
		gf_log ("server", GF_LOG_ERROR,
			"failed to allocate memory for \'struct _locker\'");
		goto out;
	} 
	INIT_LIST_HEAD (&new->lockers);

	if (fd == NULL) {
		loc_copy (&new->loc, loc);
		dir = S_ISDIR (new->loc.inode->st_mode);
	} else {
		new->fd = fd_ref (fd);
		dir = S_ISDIR (fd->inode->st_mode);
	}
	
	new->pid = pid;

	LOCK (&table->lock);
	{
		if (dir)
			list_add_tail (&new->lockers, &table->dir_lockers);
		else
			list_add_tail (&new->lockers, &table->file_lockers);
	}
	UNLOCK (&table->lock);
out:
	return ret;
}

int32_t
gf_del_locker (struct _lock_table *table,
	       loc_t *loc,
	       fd_t *fd,
	       pid_t pid)
{
	struct _locker *locker = NULL, *tmp = NULL;
	int32_t ret = 0;
	uint8_t dir = 0;
	struct list_head *head = NULL;
	struct list_head del;
	
	INIT_LIST_HEAD (&del);

	if (fd) {
		dir = S_ISDIR (fd->inode->st_mode);
	} else {
		dir = S_ISDIR (loc->inode->st_mode);
	}

	LOCK (&table->lock);
	{
		if (dir) {
			head = &table->dir_lockers;
		} else {
			head = &table->file_lockers;
		}

		list_for_each_entry_safe (locker, tmp, head, lockers) {
			if (locker->fd && 
			    (locker->fd == fd) && (locker->pid == pid)) {
				list_move_tail (&locker->lockers, &del);
			} else if (locker->loc.inode && 
				   (locker->loc.inode == loc->inode) &&
				   (locker->pid == pid)) {
				list_move_tail (&locker->lockers, &del);
			}
		}
	}
	UNLOCK (&table->lock);

	tmp = NULL;
	locker = NULL;

	list_for_each_entry_safe (locker, tmp, &del, lockers) {
		list_del_init (&locker->lockers);
		if (locker->fd)
			fd_unref (locker->fd);
		else
			loc_wipe (&locker->loc);
		
		free (locker);
	}

	return ret;
}

int32_t
server_inodelk_cbk (call_frame_t *frame, void *cookie,
		    xlator_t *this, int32_t op_ret, int32_t op_errno)
{
 	size_t hdrlen = 0;
 	gf_hdr_common_t *hdr = NULL;
 	gf_fop_inodelk_rsp_t *rsp = NULL;
	server_state_t *state = NULL;

 	hdrlen = gf_hdr_len (rsp, 0);
 	hdr    = gf_hdr_new (rsp, 0);
 	rsp    = gf_param (hdr);
  
 	hdr->rsp.op_ret   = hton32 (op_ret);
 	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));
	
	if (op_ret >= 0) {
		state = STATE (frame);
		if (state->flock.l_type == F_UNLCK)
			gf_del_locker (CONNECTION_PRIVATE(frame)->ltable, 
				       &state->loc, state->fd, frame->root->pid);
		else
			gf_add_locker (CONNECTION_PRIVATE(frame)->ltable, 
				       &state->loc, state->fd, frame->root->pid);
	}

 	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_INODELK,
 			       hdr, hdrlen, NULL, 0, NULL);
  
 	return 0;
}


int32_t
server_finodelk_cbk (call_frame_t *frame, void *cookie,
		     xlator_t *this, int32_t op_ret, int32_t op_errno)
{
 	size_t hdrlen = 0;
 	gf_hdr_common_t *hdr = NULL;
 	gf_fop_finodelk_rsp_t *rsp = NULL;
	server_state_t *state = NULL;

 	hdrlen = gf_hdr_len (rsp, 0);
 	hdr    = gf_hdr_new (rsp, 0);
 	rsp    = gf_param (hdr);
  
 	hdr->rsp.op_ret   = hton32 (op_ret);
 	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));
	
	if (op_ret >= 0) {
		state = STATE (frame);
		if (state->flock.l_type == F_UNLCK)
			gf_del_locker (CONNECTION_PRIVATE(frame)->ltable, 
				       &state->loc, state->fd, frame->root->pid);
		else
			gf_add_locker (CONNECTION_PRIVATE(frame)->ltable, 
				       &state->loc, state->fd, frame->root->pid);
	}

 	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FINODELK,
 			       hdr, hdrlen, NULL, 0, NULL);
  
 	return 0;
}
  
 
/*
 * server_gf_dir_lk_cbk - gf_dir_lk callback for server protocol
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
server_entrylk_cbk (call_frame_t *frame, void *cookie,
		    xlator_t *this, int32_t op_ret, int32_t op_errno)
{
 	size_t hdrlen = 0;
 	gf_hdr_common_t *hdr = NULL;
 	gf_fop_entrylk_rsp_t *rsp = NULL;
	server_state_t *state = NULL;

 	hdrlen = gf_hdr_len (rsp, 0);
 	hdr    = gf_hdr_new (rsp, 0);
 	rsp    = gf_param (hdr);
 
 	hdr->rsp.op_ret   = hton32 (op_ret);
 	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));
	
	if (op_ret >= 0) {
		state = STATE (frame);
		if (state->flock.l_type == F_UNLCK)
			gf_del_locker (CONNECTION_PRIVATE(frame)->ltable, 
				       &state->loc, state->fd, frame->root->pid);
		else
			gf_add_locker (CONNECTION_PRIVATE(frame)->ltable, 
				       &state->loc, state->fd, frame->root->pid);
	}

 	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_ENTRYLK,
 			       hdr, hdrlen, NULL, 0, NULL);
 
 	return 0;
}


int32_t
server_fentrylk_cbk (call_frame_t *frame, void *cookie,
		     xlator_t *this, int32_t op_ret, int32_t op_errno)
{
 	size_t hdrlen = 0;
 	gf_hdr_common_t *hdr = NULL;
 	gf_fop_fentrylk_rsp_t *rsp = NULL;
	server_state_t *state = NULL;

 	hdrlen = gf_hdr_len (rsp, 0);
 	hdr    = gf_hdr_new (rsp, 0);
 	rsp    = gf_param (hdr);
 
 	hdr->rsp.op_ret   = hton32 (op_ret);
 	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));
	
	if (op_ret >= 0) {
		state = STATE (frame);
		if (state->flock.l_type == F_UNLCK)
			gf_del_locker (CONNECTION_PRIVATE(frame)->ltable, 
				       &state->loc, state->fd, frame->root->pid);
		else
			gf_add_locker (CONNECTION_PRIVATE(frame)->ltable, 
				       &state->loc, state->fd, frame->root->pid);
	}

 	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FENTRYLK,
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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));
  
	server_loc_wipe (&(state->loc));

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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret == 0)
		gf_stat_from_stat (&rsp->stat, stbuf);
  
	server_loc_wipe (&(state->loc));

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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret == 0)
		gf_stat_from_stat (&rsp->stat, stbuf);
  
	server_loc_wipe (&(state->loc));

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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret == 0)
		gf_stat_from_stat (&rsp->stat, stbuf);
  
	server_loc_wipe (&(state->loc));

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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	if (op_ret == 0)
		inode_unlink (state->loc.inode, state->loc.parent, state->loc.name);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	server_loc_wipe (&(state->loc));

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
	gf_hdr_common_t *hdr = NULL;
	gf_fop_mkdir_rsp_t *rsp = NULL;
	size_t hdrlen = 0;
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret >= 0) {
		gf_stat_from_stat (&rsp->stat, stbuf);

		inode_link (inode, state->loc.parent, state->loc.name, stbuf);
		inode_lookup (inode);
	}

	server_loc_wipe (&(state->loc));

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
	gf_hdr_common_t *hdr = NULL;
	gf_fop_mknod_rsp_t *rsp = NULL;
	size_t hdrlen = 0;
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret >= 0) {
		gf_stat_from_stat (&rsp->stat, stbuf);

		inode_link (inode, state->loc.parent, state->loc.name, stbuf);
		inode_lookup (inode);
	}

	server_loc_wipe (&(state->loc));

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
	int32_t vec_count = 0;
	struct iovec vector[1];

	if (op_ret >= 0) {
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

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret == 0) {
		data_t *buf_data = get_new_data ();
		dict_t *reply_dict = get_new_dict ();
      
		reply_dict->is_locked = 1;
		buf_data->is_locked = 1;
		buf_data->data = buffer;
		buf_data->len = buf_len;
      
		dict_set (reply_dict, NULL, buf_data);
		frame->root->rsp_refs = dict_ref (reply_dict);
		vector[0].iov_base = buffer;
		vector[0].iov_len = buf_len;
		vec_count = 1;

		rsp->count = hton32 (count);
	} else {
		vector[0].iov_base = NULL;
		vector[0].iov_len = 0;
	}

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_GETDENTS,
			       hdr, hdrlen, vector, vec_count, frame->root->rsp_refs);

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
	size_t buf_size = 0;

	if (op_ret > 0)
		buf_size = gf_dirent_serialize (entries, NULL, 0);

	hdrlen = gf_hdr_len (rsp, buf_size);
	hdr    = gf_hdr_new (rsp, buf_size);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret > 0) {
		rsp->size = hton32 (buf_size);
		gf_dirent_serialize (entries, rsp->buf, buf_size);
	}

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_READDIR,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}


/*
 * server_releasedir_cbk - releasedir callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret: return value
 * @op_errno: errno
 *
 * not for external reference
 */
int32_t
server_releasedir_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
	gf_hdr_common_t *hdr = NULL;
	gf_cbk_releasedir_rsp_t *rsp = NULL;
	size_t hdrlen = 0;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	protocol_server_reply (frame, GF_OP_TYPE_CBK_REPLY, GF_CBK_RELEASEDIR,
			       hdr, hdrlen, NULL, 0, NULL);

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
	connection_private_t *priv = NULL;
	uint64_t fd_no = -1;
	server_state_t *state = NULL;
  
	state = STATE (frame);

	if (op_ret >= 0) {
		priv = CONNECTION_PRIVATE (frame);

		fd_bind (fd);

		fd_no = gf_fd_unused_get (priv->fdtable, fd);
	} else {
		/* NOTE: corresponding to fd_create()'s ref */
		if (state->fd)
			fd_unref (state->fd);
	}

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));
	rsp->fd           = hton64 (fd_no);
  
	server_loc_wipe (&(state->loc));

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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret >= 0) {
		gf_statfs_from_statfs (&rsp->statfs, buf);
	}

	server_loc_wipe (&(state->loc));

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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	server_loc_wipe (&(state->loc));

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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	if (op_ret >= 0) {
		dict_set (dict, "__@@protocol_client@@__key", str_to_data ("value"));
		len = dict_serialized_length (dict);
	}

	hdrlen = gf_hdr_len (rsp, len + 1);
	hdr    = gf_hdr_new (rsp, len + 1);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret >= 0) {
		rsp->dict_len = hton32 (len);
		dict_serialize (dict, rsp->dict);
	}

	server_loc_wipe (&(state->loc));

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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	server_loc_wipe (&(state->loc));

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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret == 0) {
		stbuf->st_ino = state->loc.ino;
		
		inode_rename (state->itable,
			      state->loc.parent, state->loc.name,
			      state->loc2.parent, state->loc2.name,
			      state->loc.inode, stbuf);
	    
		gf_stat_from_stat (&rsp->stat, stbuf);
	}
  
	server_loc_wipe (&(state->loc));
	server_loc_wipe (&(state->loc2));

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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	if (op_ret == 0)
		inode_unlink (state->loc.inode, state->loc.parent, state->loc.name);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	server_loc_wipe (&(state->loc));

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
	gf_hdr_common_t *hdr = NULL;
	gf_fop_symlink_rsp_t *rsp = NULL;
	size_t hdrlen = 0;
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret >= 0) {
		gf_stat_from_stat (&rsp->stat, stbuf);

		inode_link (inode, state->loc.parent, state->loc.name, stbuf);
		inode_lookup (inode);
	}

	server_loc_wipe (&(state->loc));

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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret == 0) {
		stbuf->st_ino = state->loc.ino;
		gf_stat_from_stat (&rsp->stat, stbuf);
		inode_link (inode, state->loc2.parent, state->loc2.name, stbuf);
	}
	server_loc_wipe (&(state->loc));
	server_loc_wipe (&(state->loc2));

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
	server_state_t *state = NULL;
  
	state = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret == 0)
		gf_stat_from_stat (&rsp->stat, stbuf);

	server_loc_wipe (&(state->loc));

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
		gf_stat_from_stat (&rsp->stat, stbuf);

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
		gf_stat_from_stat (&rsp->stat, stbuf);

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
 * server_release_cbk - rleease callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 * not for external reference
 */
int32_t
server_release_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	gf_hdr_common_t *hdr = NULL;
	gf_cbk_release_rsp_t *rsp = NULL;
	size_t hdrlen = 0;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	protocol_server_reply (frame, GF_OP_TYPE_CBK_REPLY, GF_CBK_RELEASE,
			       hdr, hdrlen, NULL, 0, NULL);

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
		gf_stat_from_stat (&rsp->stat, stbuf);

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
		gf_stat_from_stat (&rsp->stat, stbuf);

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
	connection_private_t *priv = NULL;
	int fd_no = -1;
	server_state_t *state = NULL;
  
	state = STATE (frame);

	if (op_ret >= 0) {
		priv = CONNECTION_PRIVATE (frame);

		fd_bind (fd);

		fd_no = gf_fd_unused_get (priv->fdtable, fd);
	} else {
		/* NOTE: corresponding to fd_create()'s ref */
		if (state->fd)
			fd_unref (state->fd);
	}

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));
	rsp->fd           = hton64 (fd_no);

	server_loc_wipe (&(state->loc));

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
	int32_t fd_no = -1;
	connection_private_t *priv = NULL;
	server_state_t *state = NULL;
  
	state = STATE (frame);

	if (op_ret >= 0) {
		priv = CONNECTION_PRIVATE (frame);

		inode_link (inode, state->loc.parent, state->loc.name, stbuf);
		inode_lookup (inode);
      
		fd_bind (fd);

		fd_no = gf_fd_unused_get (priv->fdtable, fd);

		if ((fd_no < 0) || (fd == 0)) {
			op_ret = fd_no;
			op_errno = errno;
		} 
	} 
	
	if (op_ret < 0) {
		/* NOTE: corresponding to fd_create()'s ref */
		if (state->fd)
			fd_unref (state->fd);
	}

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));
	rsp->fd           = hton64 (fd_no);

	if (op_ret >= 0)
		gf_stat_from_stat (&rsp->stat, stbuf);

	server_loc_wipe (&(state->loc));

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
	server_state_t *state = NULL;
  
	state  = STATE (frame);

	if (op_ret >= 0)
		linklen = strlen (buf) + 1;

	hdrlen = gf_hdr_len (rsp, linklen);
	hdr    = gf_hdr_new (rsp, linklen);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret >= 0)
		strcpy (rsp->path, buf);

	server_loc_wipe (&(state->loc));

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
	server_state_t *state = NULL;
  
	state  = STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret == 0)
		gf_stat_from_stat (&rsp->stat, stbuf);

	server_loc_wipe (&(state->loc));

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
	gf_cbk_forget_rsp_t *rsp = NULL;
	size_t hdrlen = 0;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	protocol_server_reply (frame, GF_OP_TYPE_CBK_REPLY, GF_CBK_FORGET,
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
	inode_t *root_inode = NULL;
	int dict_len = 0;
	gf_hdr_common_t *hdr = NULL;
	size_t hdrlen = 0;
	gf_fop_lookup_rsp_t *rsp = NULL;
	server_state_t *state = STATE (frame);

	if ((op_errno == ESTALE) && (op_ret == -1)) {
		/* Send lookup again with new ctx dictionary */
		loc_t loc = {0,};

		root_inode = BOUND_XL(frame)->itable->root;
		if (state->loc.inode != root_inode) {
			if (state->loc.inode)
				inode_unref (state->loc.inode);
			state->loc.inode = inode_new (BOUND_XL(frame)->itable);
		}
		loc.inode = state->loc.inode;
		loc.path = state->path;
		state->is_revalidate = 2;
		STACK_WIND (frame, server_lookup_cbk,
			    BOUND_XL(frame),
			    BOUND_XL(frame)->fops->lookup,
			    &loc,
			    state->need_xattr);
		return 0;
	}

	if (dict) {
		dict_set (dict, "__@@protocol_client@@__key", str_to_data ("value"));
		dict_len = dict_serialized_length (dict);
	}

	hdrlen = gf_hdr_len (rsp, dict_len);
	hdr    = gf_hdr_new (rsp, dict_len);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret == 0) {
		gf_stat_from_stat (&rsp->stat, stbuf);
		rsp->dict_len = hton32 (dict_len);
		if (dict)
			dict_serialize (dict, rsp->dict);
    
		root_inode = BOUND_XL(frame)->itable->root;
		if (inode == root_inode) {
			/* we just looked up root ("/") */
			stbuf->st_ino = 1;
			if (!inode->st_mode)
				inode->st_mode = stbuf->st_mode;
		}
    
		if (!inode->ino) {
			inode_link (inode, state->loc.parent, state->loc.name, stbuf);
			inode_lookup (inode);
		}
	}

	server_loc_wipe (&state->loc);
	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_LOOKUP,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}

/*
 * server_stub_resume - this is callback function used whenever an fop does
 *                   STACK_WIND to fops->lookup in order to lookup the inode
 *                   for a pathname. this case of doing fops->lookup arises
 *                   when fop searches in inode table for pathname and search
 *                   fails.
 *
 * @stub: call stub
 * @op_ret:
 * @op_errno:
 * @inode:
 * @parent:
 *
 * not for external reference
 */
int32_t
server_stub_resume (call_stub_t *stub,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    inode_t *parent)
{
	inode_t *server_inode = inode;
	
	if (stub) {
		switch (stub->fop)
		{
		case GF_FOP_RENAME:
			if (stub->args.rename.old.inode == NULL) {
				loc_t *newloc = NULL;
				/* now we are called by lookup of oldpath. */
				if (op_ret < 0) {
					gf_log (stub->frame->this->name, 
						GF_LOG_ERROR,
						"rename returning ENOENT: %d (%d)", 
						op_ret, op_errno);

					/* lookup of oldpath failed, UNWIND to 
					 * server_rename_cbk with ret=-1 and errno=ENOENT */
					server_rename_cbk (stub->frame,
							   NULL,
							   stub->frame->this,
							   -1,
							   ENOENT,
							   NULL);
					server_loc_wipe (&stub->args.rename.old);
					server_loc_wipe (&stub->args.rename.new);
					FREE (stub);
					return 0;
				}
				
				if (stub->args.rename.old.parent == NULL)
					stub->args.rename.old.parent = inode_ref (parent);

				/* store inode information of oldpath in our stub and search for
				 * newpath in inode table. */
				if (server_inode) {
					stub->args.rename.old.inode = inode_ref (server_inode);
					stub->args.rename.old.ino = server_inode->ino;
				}

				/* now lookup for newpath */
				newloc = &stub->args.rename.new;

				if (newloc->inode == NULL) {
					/* lookup for newpath */
					newloc->inode = inode_new (BOUND_XL(stub->frame)->itable);
					do_path_lookup (stub, newloc);
					break;
				} else {
					/* found newpath in inode cache */
					call_resume (stub);
					break;
				}
			} else {
				/* we are called by the lookup of newpath */
				if (stub->args.rename.new.parent == NULL)
					stub->args.rename.new.parent = inode_ref (parent);

				if (server_inode && (stub->args.rename.new.inode == NULL)) {
					/* as new.inode doesn't get forget, it
					 * needs to be unref'd here */
					stub->args.rename.new.inode = inode_ref (server_inode);
					stub->args.rename.new.ino = server_inode->ino;
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
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"open returning ENOENT: %d (%d)", 
					op_ret, op_errno);

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
			if (stub->args.open.loc.parent == NULL)
				stub->args.open.loc.parent = inode_ref (parent);

			if (server_inode && (stub->args.open.loc.inode == NULL)) {
				stub->args.open.loc.inode = inode_ref (server_inode);
				stub->args.open.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_LOOKUP:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_DEBUG,
					"lookup returning ENOENT: %d (%d)", 
					op_ret, op_errno);

				server_lookup_cbk (stub->frame,
						   NULL,
						   stub->frame->this,
						   -1,
						   ENOENT,
						   NULL,
						   NULL,
						   NULL);
				server_loc_wipe (&stub->args.lookup.loc);
				FREE (stub);
				return 0;
			}

			if (stub->args.lookup.loc.parent == NULL)
				stub->args.lookup.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.lookup.loc.inode == NULL)) {
				stub->args.lookup.loc.inode = inode_ref (server_inode);
				stub->args.lookup.loc.ino = server_inode->ino;
			}

			call_resume (stub);

			break;
		}

		case GF_FOP_STAT:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"stat returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_stat_cbk (stub->frame,
						 NULL,
						 stub->frame->this,
						 -1,
						 ENOENT,
						 NULL);
				server_loc_wipe (&stub->args.stat.loc);
				FREE (stub);
				return 0;
			}

			/* TODO: reply from here only, we already have stat structure */
			if (stub->args.stat.loc.parent == NULL)
				stub->args.stat.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.stat.loc.inode == NULL)) {
				stub->args.stat.loc.inode = inode_ref (server_inode);
				stub->args.stat.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_UNLINK:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"unlink returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_unlink_cbk (stub->frame,
						   NULL,
						   stub->frame->this,
						   -1,
						   ENOENT);
				server_loc_wipe (&stub->args.unlink.loc);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.unlink.loc.parent == NULL)
				stub->args.unlink.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.unlink.loc.inode == NULL)) {
				stub->args.unlink.loc.inode = inode_ref (server_inode);
				stub->args.unlink.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_SYMLINK:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"symlink returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_symlink_cbk (stub->frame,
						    NULL,
						    stub->frame->this,
						    -1,
						    ENOENT,
						    NULL,
						    NULL);
				server_loc_wipe (&stub->args.symlink.loc);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.symlink.loc.parent == NULL)
				stub->args.symlink.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.symlink.loc.inode == NULL)) {
				stub->args.symlink.loc.inode = inode_ref (server_inode);
				stub->args.symlink.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_RMDIR:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"rmdir returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_rmdir_cbk (stub->frame,
						  NULL,
						  stub->frame->this,
						  -1,
						  ENOENT);
				server_loc_wipe (&stub->args.rmdir.loc);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.rmdir.loc.parent == NULL)
				stub->args.rmdir.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.rmdir.loc.inode == NULL)) {
				stub->args.rmdir.loc.inode = inode_ref (server_inode);
				stub->args.rmdir.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_CHMOD:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"chmod returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_chmod_cbk (stub->frame,
						  NULL,
						  stub->frame->this,
						  -1,
						  ENOENT,
						  NULL);
				server_loc_wipe (&stub->args.chmod.loc);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.chmod.loc.parent == NULL)
				stub->args.chmod.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.chmod.loc.inode == NULL)) {
				stub->args.chmod.loc.inode = inode_ref (server_inode);
				stub->args.chmod.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_CHOWN:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"chown returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_chown_cbk (stub->frame,
						  NULL,
						  stub->frame->this,
						  -1,
						  ENOENT,
						  NULL);
				server_loc_wipe (&stub->args.chown.loc);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.chown.loc.parent == NULL)
				stub->args.chown.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.chown.loc.inode == NULL)) {
				stub->args.chown.loc.inode = inode_ref (server_inode);
				stub->args.chown.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_LINK:
		{
			if (stub->args.link.oldloc.inode == NULL) {
				if (op_ret < 0) {
					gf_log (stub->frame->this->name, 
						GF_LOG_ERROR,
						"link returning ENOENT: %d (%d)", 
						op_ret, op_errno);
					server_link_cbk (stub->frame,
							 NULL,
							 stub->frame->this,
							 -1,
							 ENOENT,
							 NULL,
							 NULL);
					server_loc_wipe (&stub->args.link.oldloc);
					server_loc_wipe (&stub->args.link.newloc);
					FREE (stub);
					return 0;
				}
				
				if (stub->args.link.oldloc.parent == NULL)
					stub->args.link.oldloc.parent = inode_ref (parent);

				if (server_inode && (stub->args.link.oldloc.inode == NULL)) {
					stub->args.link.oldloc.inode = inode_ref (server_inode);
					stub->args.link.oldloc.ino = server_inode->ino;
				}

				if (stub->args.link.newloc.parent == NULL) {
					do_path_lookup (stub, &(stub->args.link.newloc));
					break;
				}
			} else {
				/* we are called by the lookup of newpath */
				if ((op_ret < 0) && (parent == NULL)) {
					gf_log (stub->frame->this->name, 
						GF_LOG_ERROR,
						"link returning ENOENT: %d (%d)", 
						op_ret, op_errno);
					server_link_cbk (stub->frame,
							 NULL,
							 stub->frame->this,
							 -1,
							 ENOENT,
							 NULL,
							 NULL);
					server_loc_wipe (&stub->args.link.oldloc);
					server_loc_wipe (&stub->args.link.newloc);
					FREE (stub);
					break;
				}
					
				if (stub->args.link.newloc.parent == NULL)
					stub->args.link.newloc.parent = inode_ref (parent);

				if (server_inode && (stub->args.link.newloc.inode == NULL)) {
					/* as new.inode doesn't get forget, it
					 * needs to be unref'd here */
					stub->args.link.newloc.inode = inode_ref (server_inode);
					stub->args.link.newloc.ino = server_inode->ino;
				}
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_TRUNCATE:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"truncate returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_truncate_cbk (stub->frame,
						     NULL,
						     stub->frame->this,
						     -1,
						     ENOENT,
						     NULL);
				server_loc_wipe (&stub->args.truncate.loc);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.truncate.loc.parent == NULL)
				stub->args.truncate.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.truncate.loc.inode == NULL)) {
				stub->args.truncate.loc.inode = inode_ref (server_inode);
				stub->args.truncate.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_STATFS:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"statfs returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_statfs_cbk (stub->frame,
						   NULL,
						   stub->frame->this,
						   -1,
						   ENOENT,
						   NULL);
				server_loc_wipe (&stub->args.statfs.loc);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.statfs.loc.parent == NULL)
				stub->args.statfs.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.statfs.loc.inode == NULL)) {
				stub->args.statfs.loc.inode = inode_ref (server_inode);
				stub->args.statfs.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_SETXATTR:
		{
			dict_t *dict = stub->args.setxattr.dict;
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"setxattr returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_setxattr_cbk (stub->frame,
						     NULL,
						     stub->frame->this,
						     -1,
						     ENOENT);
				server_loc_wipe (&stub->args.setxattr.loc);
				dict_destroy (dict);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.setxattr.loc.parent == NULL)
				stub->args.setxattr.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.setxattr.loc.inode == NULL)) {
				stub->args.setxattr.loc.inode = inode_ref (server_inode);
				stub->args.setxattr.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_GETXATTR:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"getxattr returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_getxattr_cbk (stub->frame,
						     NULL,
						     stub->frame->this,
						     -1,
						     ENOENT,
						     NULL);
				server_loc_wipe (&stub->args.getxattr.loc);
				FREE (stub);
				return 0;
			}

			if (stub->args.getxattr.loc.parent == NULL)
				stub->args.getxattr.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.getxattr.loc.inode == NULL)) {
				stub->args.getxattr.loc.inode = inode_ref (server_inode);
				stub->args.getxattr.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_REMOVEXATTR:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"removexattr returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_removexattr_cbk (stub->frame,
							NULL,
							stub->frame->this,
							-1,
							ENOENT);
				server_loc_wipe (&stub->args.removexattr.loc);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.removexattr.loc.parent == NULL)
				stub->args.removexattr.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.removexattr.loc.inode == NULL)) {
				stub->args.removexattr.loc.inode = inode_ref (server_inode);
				stub->args.removexattr.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_OPENDIR:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"opendir returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_opendir_cbk (stub->frame,
						    NULL,
						    stub->frame->this,
						    -1,
						    ENOENT,
						    NULL);
				server_loc_wipe (&stub->args.opendir.loc);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.opendir.loc.parent == NULL)
				stub->args.opendir.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.opendir.loc.inode == NULL)) {
				stub->args.opendir.loc.inode = inode_ref (server_inode);
				stub->args.opendir.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_ACCESS:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"access returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_access_cbk (stub->frame,
						   NULL,
						   stub->frame->this,
						   -1,
						   ENOENT);
				server_loc_wipe (&stub->args.access.loc);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.access.loc.parent == NULL) 
				stub->args.access.loc.parent = inode_ref (parent);

			if (server_inode && (stub->args.access.loc.inode == NULL)) {
				stub->args.access.loc.inode = inode_ref (server_inode);
				stub->args.access.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}


		case GF_FOP_UTIMENS:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"utimens returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_utimens_cbk (stub->frame,
						    NULL,
						    stub->frame->this,
						    -1,
						    ENOENT,
						    NULL);
				server_loc_wipe (&stub->args.utimens.loc);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.utimens.loc.parent == NULL)
				stub->args.utimens.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.utimens.loc.inode == NULL)) {
				stub->args.utimens.loc.inode = inode_ref (server_inode);
				stub->args.utimens.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}

		case GF_FOP_READLINK:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"readlink returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_readlink_cbk (stub->frame,
						     NULL,
						     stub->frame->this,
						     -1,
						     ENOENT,
						     NULL);
				server_loc_wipe (&stub->args.readlink.loc);
				FREE (stub);
				return 0;
			}
			
			if (stub->args.readlink.loc.parent == NULL)
				stub->args.readlink.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.readlink.loc.inode == NULL)) {
				stub->args.readlink.loc.inode = inode_ref (server_inode);
				stub->args.readlink.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}
		case GF_FOP_MKDIR:
		{
			if ((op_ret < 0) && (parent == NULL)) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"mkdir returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_mkdir_cbk (stub->frame,
						  NULL,
						  stub->frame->this,
						  -1,
						  ENOENT,
						  NULL,
						  NULL);
				server_loc_wipe (&stub->args.mkdir.loc);
				FREE (stub);
				break;
			}
			
			if (stub->args.mkdir.loc.parent == NULL)
				stub->args.mkdir.loc.parent = inode_ref (parent);
			
			if (server_inode && (stub->args.mkdir.loc.inode == NULL)) {
				stub->args.mkdir.loc.inode = inode_ref (server_inode);
				stub->args.mkdir.loc.ino = server_inode->ino;
			}

			call_resume (stub);
			break;
		}

		case GF_FOP_CREATE:
		{
			if ((op_ret < 0) && (parent == NULL)) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"create returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_create_cbk (stub->frame,
						   NULL,
						   stub->frame->this,
						   -1,
						   ENOENT,
						   NULL,
						   NULL,
						   NULL);
				if (stub->args.create.fd)
					fd_unref (stub->args.create.fd);
				server_loc_wipe (&stub->args.create.loc);
				FREE (stub);
				break;
			}
			
			if (stub->args.create.loc.parent == NULL)
				stub->args.create.loc.parent = inode_ref (parent);

			if (server_inode && (stub->args.create.loc.inode == NULL)) {
				stub->args.create.loc.inode = inode_ref (server_inode);
				stub->args.create.loc.ino = server_inode->ino;
			}

			call_resume (stub);
			break;
		}

		case GF_FOP_MKNOD:
		{
			if ((op_ret < 0) && (parent == NULL)) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"mknod returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_mknod_cbk (stub->frame,
						  NULL,
						  stub->frame->this,
						  -1,
						  ENOENT,
						  NULL,
						  NULL);
				server_loc_wipe (&stub->args.mknod.loc);
				FREE (stub);
				break;
			}
			
			if (stub->args.mknod.loc.parent == NULL)
				stub->args.mknod.loc.parent = inode_ref (parent);

			if (server_inode && (stub->args.mknod.loc.inode == NULL)) {
				stub->args.mknod.loc.inode = inode_ref (server_inode);
				stub->args.mknod.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}
		case GF_FOP_ENTRYLK:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"entrylk returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_entrylk_cbk (stub->frame,
						    NULL,
						    stub->frame->this,
						    -1,
						    ENOENT);
				server_loc_wipe (&stub->args.entrylk.loc);
				FREE (stub);
				break;
			}
			
			if (stub->args.entrylk.loc.parent == NULL)
				stub->args.entrylk.loc.parent = inode_ref (parent);

			if (server_inode && (stub->args.entrylk.loc.inode == NULL)) {
				stub->args.entrylk.loc.inode = inode_ref (server_inode);
				stub->args.entrylk.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}
		case GF_FOP_INODELK:
		{
			if (op_ret < 0) {
				gf_log (stub->frame->this->name, 
					GF_LOG_ERROR,
					"inodelk returning ENOENT: %d (%d)", 
					op_ret, op_errno);
				server_inodelk_cbk (stub->frame,
						       NULL,
						       stub->frame->this,
						       -1,
						       ENOENT);
				server_loc_wipe (&stub->args.inodelk.loc);
				FREE (stub);
				break;
			}
			
			if (stub->args.inodelk.loc.parent == NULL)
				stub->args.inodelk.loc.parent = inode_ref (parent);

			if (server_inode && (stub->args.inodelk.loc.inode == NULL)) {
				stub->args.inodelk.loc.inode = inode_ref (server_inode);
				stub->args.inodelk.loc.ino = server_inode->ino;
			}
			call_resume (stub);
			break;
		}
		default:
			call_resume (stub);
		}
	}
	return 0;
}

static int
server_lookup_resume (call_frame_t *frame,
		      xlator_t *this,
		      loc_t *loc,
		      int32_t need_xattr)
{
	server_state_t *state = NULL;
	
	state = STATE (frame);
	
	if (state->loc.parent == NULL)
		state->loc.parent = inode_ref (loc->parent);

	if (state->loc.inode == NULL) {
		if (loc->inode == NULL)
			state->loc.inode = inode_new (state->itable);
		else
			state->loc.inode = inode_ref (loc->inode);
	} else {
		if (loc->inode && (state->loc.inode != loc->inode)) {
			if (state->loc.inode)
				inode_unref (state->loc.inode);
			state->loc.inode = inode_ref (loc->inode);
		}
	}

	STACK_WIND (frame,
		    server_lookup_cbk,
		    BOUND_XL (frame),
		    BOUND_XL (frame)->fops->lookup,
		    &(state->loc),
		    need_xattr);
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
int
server_lookup (call_frame_t *frame,
               xlator_t *bound_xl,
               gf_hdr_common_t *hdr, size_t hdrlen,
               char *buf, size_t buflen)
{
	gf_fop_lookup_req_t *req = NULL;
	server_state_t *state = NULL;
	call_stub_t *lookup_stub = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_LOOKUP);

	server_loc_fill (&state->loc, state, state->path);

 	if (state->loc.inode) {
		/* revalidate */
		state->is_revalidate = 1;
	} else {
		/* fresh lookup or inode was previously pruned out */
		state->is_revalidate = -1;
	}
	
	lookup_stub = fop_lookup_stub (frame, server_lookup_resume, 
				       &(state->loc), state->need_xattr);
	
	if (state->loc.parent == NULL)
		do_path_lookup (lookup_stub, &(state->loc));
	else
		call_resume (lookup_stub);

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
	gf_cbk_forget_req_t *req = NULL;
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
	call_stub_t *stat_stub = NULL;
	gf_fop_stat_req_t *req = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_STAT);
  
	server_loc_fill (&(state->loc), state, state->path);

	stat_stub = fop_stat_stub (frame,
				   server_stat_resume,
				   &(state->loc));

	if ((state->loc.parent == NULL) || (state->loc.inode == NULL)) {
		do_path_lookup (stat_stub, &(state->loc));
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
	call_stub_t *readlink_stub = NULL;
	gf_fop_readlink_req_t *req = NULL;
	server_state_t *state = NULL;

	req       = gf_param (hdr);
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_READLINK);

	server_loc_fill (&(state->loc), state, state->path);

	readlink_stub = fop_readlink_stub (frame,
					   server_readlink_resume,
					   &(state->loc),
					   state->size);

	if ((state->loc.parent == NULL) ||
	    (state->loc.inode == NULL)) {
		do_path_lookup (readlink_stub, &(state->loc));
	} else {
		call_resume (readlink_stub);
	}

	return 0;
}

int32_t
server_create_resume (call_frame_t *frame,
		      xlator_t *this,
		      loc_t *loc,
		      int32_t flags,
		      mode_t mode,
		      fd_t *fd)
{
	server_state_t *state = STATE (frame);
	
	if (state->loc.parent == NULL) 
		state->loc.parent = inode_ref (loc->parent);

	if (state->loc.inode == NULL) {
		if (loc->inode == NULL)
			state->loc.inode = inode_new (state->itable);
		else
			state->loc.inode = inode_ref (loc->inode);
	} else {
		if (loc->inode && (state->loc.inode != loc->inode)) {
			if (state->loc.inode)
				inode_unref (state->loc.inode);
			state->loc.inode = inode_ref (loc->inode);
		}
	}

	state->fd = fd_create (state->loc.inode, frame->root->pid);
	state->fd->flags = flags;
	state->fd = fd_ref (state->fd);

	STACK_WIND (frame,
		    server_create_cbk,
		    BOUND_XL (frame),
		    BOUND_XL (frame)->fops->create,
		    &(state->loc),
		    flags,
		    mode,
		    state->fd);

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
	server_state_t      *state = NULL;
	call_stub_t         *create_stub = NULL;

	req = gf_param (hdr);
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_CREATE);
  
	server_loc_fill (&(state->loc), state, state->path);
    
	create_stub = fop_create_stub (frame, server_create_resume, 
				       &(state->loc), state->flags, state->mode, state->fd);

	if (state->loc.parent == NULL) {
		do_path_lookup (create_stub, &state->loc);
	} else {
		call_resume (create_stub);
	}

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

	new_fd = fd_create (loc->inode, frame->root->pid);
	new_fd->flags = flags;

	state->fd = fd_ref (new_fd);

	STACK_WIND (frame,
		    server_open_cbk,
		    BOUND_XL (frame),
		    BOUND_XL (frame)->fops->open,
		    loc,
		    flags,
		    state->fd);

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
	call_stub_t *open_stub = NULL;
	gf_fop_open_req_t *req = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_OPEN);

	server_loc_fill (&(state->loc), state, state->path);

	open_stub = fop_open_stub (frame, 
				   server_open_resume, 
				   &(state->loc), state->flags, NULL);

	if ((state->loc.parent == NULL) || (state->loc.inode == NULL)) {
		do_path_lookup (open_stub, &state->loc);
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
	server_state_t *state = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_READ);

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_readv_cbk (frame, NULL, frame->this,
				  -1, EINVAL, NULL, 0, NULL);
		goto out;
	}

	STACK_WIND (frame,
		    server_readv_cbk,
		    bound_xl,
		    bound_xl->fops->readv,
		    state->fd, state->size, state->offset);
out:
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
	dict_t *refs = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_WRITE);

	if (state->fd == NULL)	{
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_writev_cbk (frame, NULL, frame->this,
				   -1, EINVAL, NULL);
		goto out;
	}

	iov.iov_base = buf;
	iov.iov_len = buflen;

	refs = get_new_dict ();
	dict_set (refs, NULL, data_from_dynptr (buf, buflen));
	frame->root->req_refs = dict_ref (refs);

	STACK_WIND (frame,
		    server_writev_cbk,
		    bound_xl,
		    bound_xl->fops->writev,
		    state->fd, &iov, 1, state->offset);

	dict_unref (refs);
out:
	return 0;
}



/*
 * server_release - release function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */

int32_t
server_release (call_frame_t *frame, xlator_t *bound_xl,
		gf_hdr_common_t *hdr, size_t hdrlen,
		char *buf, size_t buflen)
{
	gf_cbk_release_req_t *req = NULL;
	connection_private_t *priv = NULL;
	server_state_t *state = NULL;
	
	priv = CONNECTION_PRIVATE (frame);
	req = gf_param (hdr);
	state = STATE (frame);
	
	state->fd_no = ntoh64 (req->fd);
	state->fd    = gf_fd_fdptr_get (priv->fdtable, state->fd_no);
	
	if (state->fd == NULL)	{
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_release_cbk (frame, NULL, frame->this,
				    -1, EINVAL);
		goto out;
	}

	priv = CONNECTION_PRIVATE (frame);
	gf_fd_put (priv->fdtable, state->fd_no);
  
	STACK_WIND (frame,
		    server_release_cbk,
		    bound_xl,
		    bound_xl->fops->flush,
		    state->fd);
out:
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
	server_state_t *state = NULL;

	req = gf_param (hdr);
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_FSYNC);

	if (state->fd == NULL)	{
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_fsync_cbk (frame, NULL, frame->this,
				  -1, EINVAL);
		goto out;
	}

	STACK_WIND (frame,
		    server_fsync_cbk,
		    bound_xl,
		    bound_xl->fops->fsync,
		    state->fd, state->flags);
out:
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
	server_state_t *state = NULL;

	req = gf_param (hdr);
	state = STATE (frame);

	server_state_fill (frame, req, GF_FOP_FLUSH);

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_flush_cbk (frame, NULL, frame->this,
				  -1, EINVAL);
		goto out;
	}

	STACK_WIND (frame,
		    server_flush_cbk,
		    bound_xl,
		    bound_xl->fops->flush,
		    state->fd);
out:
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
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_FTRUNCATE);

	if (state->fd == NULL)	{
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_ftruncate_cbk (frame, NULL, frame->this,
				      -1, EINVAL, NULL);

		goto out;
	}

	STACK_WIND (frame,
		    server_ftruncate_cbk,
		    bound_xl,
		    bound_xl->fops->ftruncate,
		    state->fd,
		    state->offset);
out:
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
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_FSTAT);

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_fstat_cbk (frame, NULL, frame->this,
				  -1, EINVAL, NULL);

		goto out;
	}

	STACK_WIND (frame,
		    server_fstat_cbk,
		    bound_xl,
		    bound_xl->fops->fstat,
		    state->fd);
out:
	return 0;
}


int32_t
server_truncate_resume (call_frame_t *frame,
                        xlator_t *this,
                        loc_t *loc,
                        off_t offset)
{
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
	call_stub_t *truncate_stub = NULL;
	gf_fop_truncate_req_t *req = NULL;
	server_state_t *state = NULL;
  
	req = gf_param (hdr);
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_TRUNCATE);

	server_loc_fill (&(state->loc), state, state->path);

	truncate_stub = fop_truncate_stub (frame,
					   server_truncate_resume,
					   &(state->loc),
					   state->offset);
	if ((state->loc.parent == NULL) || 
	    (state->loc.inode == NULL)) {
		do_path_lookup (truncate_stub, &(state->loc));
	} else {
		call_resume (truncate_stub);
	}

	return 0;
}



int32_t
server_link_resume (call_frame_t *frame,
                    xlator_t *this,
                    loc_t *oldloc,
		    loc_t *newloc)
{
	server_state_t *state = NULL;
	
	state = STATE (frame);

	if (state->loc.parent == NULL) 
		state->loc.parent = inode_ref (oldloc->parent);

	if (state->loc.inode == NULL) {
		state->loc.inode = inode_ref (oldloc->inode);
	} else if (state->loc.inode != oldloc->inode) {
		if (state->loc.inode)
			inode_unref (state->loc.inode);
		state->loc.inode = inode_ref (oldloc->inode);			
	}
	
	if (state->loc2.parent == NULL)
		state->loc2.parent = inode_ref (newloc->parent);
	
	state->loc2.inode = inode_ref (state->loc.inode);

	STACK_WIND (frame,
		    server_link_cbk,
		    BOUND_XL (frame),
		    BOUND_XL (frame)->fops->link,
		    &(state->loc),
		    &(state->loc2));
	return 0;
}


int32_t
server_unlink_resume (call_frame_t *frame,
                      xlator_t *this,
                      loc_t *loc)
{
	server_state_t *state = NULL;
	
	state = STATE(frame);

	if (state->loc.parent == NULL)
		state->loc.parent = inode_ref (loc->parent);

	if (state->loc.inode == NULL)
		state->loc.inode = inode_ref (loc->inode);

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
	call_stub_t *unlink_stub = NULL;
	gf_fop_unlink_req_t *req = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_UNLINK);

	server_loc_fill (&(state->loc), state, state->path);

	unlink_stub = fop_unlink_stub (frame,
				       server_unlink_resume,
				       &(state->loc));

	if ((state->loc.parent == NULL) || 
	    (state->loc.inode == NULL)) {
		do_path_lookup (unlink_stub, &state->loc);
	} else {
		call_resume (unlink_stub);
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
	call_stub_t *setxattr_stub = NULL;
	gf_fop_setxattr_req_t *req = NULL;
	dict_t *dict = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_SETXATTR);

	server_loc_fill (&(state->loc), state, state->path);

	{
		/* Unserialize the dictionary */
		char *buf = memdup (req->dict, state->dict_len);
		dict = get_new_dict ();
		dict_unserialize (buf, state->dict_len, &dict);
		dict->extra_free = buf;
	}

	setxattr_stub = fop_setxattr_stub (frame,
					   server_setxattr_resume,
					   &(state->loc),
					   dict,
					   state->flags);

	if ((state->loc.parent == NULL) || 
	    (state->loc.inode == NULL)) {
		do_path_lookup (setxattr_stub, &(state->loc));
	} else {
		call_resume (setxattr_stub);
	}

	return 0;
}


int32_t
server_xattrop_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dict_t *dict)
{
	gf_hdr_common_t *hdr = NULL;
	gf_fop_xattrop_rsp_t *rsp = NULL;
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

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_XATTROP,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}

int32_t
server_xattrop (call_frame_t *frame,
		xlator_t *bound_xl,
		gf_hdr_common_t *hdr, size_t hdrlen,
		char *buf, size_t buflen)
{
	gf_fop_xattrop_req_t *req = NULL;
	dict_t *dict = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_XATTROP);

	if (state->dict_len) {
		/* Unserialize the dictionary */
		char *buf = memdup (req->dict, state->dict_len);
		dict = get_new_dict ();
		dict_unserialize (buf, state->dict_len, &dict);
		dict->extra_free = buf;
		dict_ref (dict);
	}
	STACK_WIND (frame,
		    server_xattrop_cbk,
		    bound_xl,
		    bound_xl->fops->xattrop,
		    state->fd,
		    state->path,
		    state->flags,
		    dict);
	if (dict)
		dict_unref (dict);
	return 0;
}


int32_t
server_getxattr_resume (call_frame_t *frame,
                        xlator_t *this,
                        loc_t *loc,
                        const char *name)
{
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
	gf_fop_getxattr_req_t *req = NULL;
	call_stub_t *getxattr_stub = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_GETXATTR);

	server_loc_fill (&(state->loc), state, state->path);

	getxattr_stub = fop_getxattr_stub (frame,
					   server_getxattr_resume,
					   &(state->loc),
					   state->name);

	if ((state->loc.parent == NULL) ||
	    (state->loc.inode == NULL)) {
		do_path_lookup (getxattr_stub, &(state->loc));
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
	gf_fop_removexattr_req_t *req = NULL;
	call_stub_t *removexattr_stub = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_REMOVEXATTR);

	server_loc_fill (&(state->loc), state, state->path);

	removexattr_stub = fop_removexattr_stub (frame,
						 server_removexattr_resume,
						 &(state->loc),
						 state->name);

	if ((state->loc.parent == NULL) || 
	    (state->loc.inode == NULL)) {
		do_path_lookup (removexattr_stub, &(state->loc));
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
	gf_fop_statfs_req_t *req = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
  
	server_state_fill (frame, req, GF_FOP_STATFS);

	server_loc_fill (&state->loc, state, state->path);

	STACK_WIND (frame,
		    server_statfs_cbk,
		    BOUND_XL (frame),
		    BOUND_XL (frame)->fops->statfs,
		    &(state->loc));

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
	
	new_fd = fd_create (loc->inode, frame->root->pid);
	state->fd = fd_ref (new_fd);

	STACK_WIND (frame,
		    server_opendir_cbk,
		    BOUND_XL (frame),
		    BOUND_XL (frame)->fops->opendir,
		    loc,
		    state->fd);
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
	call_stub_t *opendir_stub = NULL;
	gf_fop_opendir_req_t *req = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	state = server_state_fill (frame, req, GF_FOP_OPENDIR);

	server_loc_fill (&state->loc, state, state->path);

	opendir_stub = fop_opendir_stub (frame,
					 server_opendir_resume,
					 &(state->loc),
					 NULL);

	if ((state->loc.parent == NULL) ||
	    (state->loc.inode == NULL)) {
		do_path_lookup (opendir_stub, &(state->loc));
	} else {
		call_resume (opendir_stub);
	}

	return 0;
}


/*
 * server_releasedir - releasedir function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
int32_t
server_releasedir (call_frame_t *frame, xlator_t *bound_xl,
		   gf_hdr_common_t *hdr, size_t hdrlen,
		   char *buf, size_t buflen)
{
	gf_cbk_releasedir_req_t *req = NULL;
	connection_private_t *priv = NULL;
	server_state_t *state = NULL;

	priv = CONNECTION_PRIVATE (frame);
	req = gf_param (hdr);
	state = STATE (frame);

	state->fd_no = ntoh64 (req->fd);
	state->fd    = gf_fd_fdptr_get (priv->fdtable, state->fd_no);

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_releasedir_cbk (frame, NULL, frame->this,
				       -1, EINVAL);
		goto out;
	}

	priv = CONNECTION_PRIVATE (frame);
	gf_fd_put (priv->fdtable, state->fd_no);
  
	server_releasedir_cbk (frame, NULL, frame->this,
			       0, 0);
out:
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
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_GETDENTS);

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_getdents_cbk (frame, NULL, frame->this,
				     -1, EINVAL, NULL, 0);

		goto out;
	}

	STACK_WIND (frame,
		    server_getdents_cbk,
		    bound_xl,
		    bound_xl->fops->getdents,
		    state->fd,
		    state->size,
		    state->offset,
		    state->flags);
out:
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
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_READDIR);

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_readdir_cbk (frame, NULL, frame->this,
				    -1, EINVAL, NULL);

		goto out;
	}

	STACK_WIND (frame,
		    server_readdir_cbk,
		    bound_xl,
		    bound_xl->fops->readdir,
		    state->fd, state->size, state->offset);
out:
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
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_FSYNCDIR);

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_fsyncdir_cbk (frame, NULL, frame->this,
				     -1, EINVAL);
		goto out;
	}

	STACK_WIND (frame,
		    server_fsyncdir_cbk,
		    bound_xl,
		    bound_xl->fops->fsyncdir,
		    state->fd, state->flags);
out:
	return 0;
}


int32_t
server_mknod_resume (call_frame_t *frame,
		     xlator_t *this,
		     loc_t *loc,
		     mode_t mode,
		     dev_t dev)
{
	server_state_t *state = NULL;
	
	state = STATE (frame);
	
	if (state->loc.parent == NULL)
		state->loc.parent = inode_ref (loc->parent);

	if (loc->inode == NULL)
		state->loc.inode = inode_new (state->itable);
 	else if (state->loc.inode != loc->inode) {
		if (state->loc.inode)
			inode_unref (state->loc.inode);
		state->loc.inode = inode_ref (loc->inode);
	}

	STACK_WIND (frame,
		    server_mknod_cbk,
		    BOUND_XL (frame),
		    BOUND_XL (frame)->fops->mknod,
		    &(state->loc), mode, dev);

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
	gf_fop_mknod_req_t *req = NULL;
	server_state_t *state = NULL;
	call_stub_t *mknod_stub = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_MKNOD);
	
	server_loc_fill (&(state->loc), state, state->path);
  
	mknod_stub = fop_mknod_stub (frame, server_mknod_resume, 
				     &(state->loc), state->mode, state->dev);

	if ((state->loc.parent == NULL) || (state->loc.inode == NULL)) {
		do_path_lookup (mknod_stub, &(state->loc));
	} else {
		call_resume (mknod_stub);
	}

	return 0;
}

int32_t
server_mkdir_resume (call_frame_t *frame,
		     xlator_t *this,
		     loc_t *loc,
		     mode_t mode)

{
	server_state_t *state = NULL;
	
	state = STATE (frame);
	
	if (state->loc.parent == NULL)
		state->loc.parent = inode_ref (loc->parent);
	
	if (loc->inode == NULL)
		state->loc.inode = inode_new (state->itable);
	else if (state->loc.inode != loc->inode) {
		if (state->loc.inode)
			inode_unref (state->loc.inode);
		state->loc.inode = inode_ref (loc->inode);
	}
		
	STACK_WIND (frame,
		    server_mkdir_cbk,
		    BOUND_XL (frame),
		    BOUND_XL (frame)->fops->mkdir,
		    &(state->loc),
		    state->mode);
	
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
	gf_fop_mkdir_req_t *req = NULL;
	server_state_t *state = NULL;
	call_stub_t *mkdir_stub = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_MKDIR);
  
	server_loc_fill (&(state->loc), state, state->path);
  
	mkdir_stub = fop_mkdir_stub (frame, server_mkdir_resume, 
				     &(state->loc), state->mode);

	if (state->loc.parent == NULL) {
		do_path_lookup (mkdir_stub, &(state->loc));
	} else {
		call_resume (mkdir_stub);
	}

	return 0;
}


int32_t
server_rmdir_resume (call_frame_t *frame,
                     xlator_t *this,
                     loc_t *loc)
{
	server_state_t *state = NULL;
	
	state = STATE (frame);
	
	if (state->loc.parent == NULL)
		state->loc.parent = inode_ref (loc->parent);
	
	if (state->loc.inode == NULL)
		state->loc.inode = inode_ref (loc->inode);

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
	call_stub_t *rmdir_stub = NULL;
	gf_fop_rmdir_req_t *req = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_RMDIR);

	server_loc_fill (&(state->loc), state, state->path);

	rmdir_stub = fop_rmdir_stub (frame,
				     server_rmdir_resume,
				     &(state->loc));

	if ((state->loc.parent == NULL) ||
	    (state->loc.inode == NULL)) {
		do_path_lookup (rmdir_stub, &(state->loc));
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


int32_t
server_chown_resume (call_frame_t *frame,
                     xlator_t *this,
                     loc_t *loc,
                     uid_t uid,
                     gid_t gid)
{
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
	call_stub_t *chown_stub = NULL;
	gf_fop_chown_req_t *req = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_CHOWN);

	server_loc_fill (&(state->loc), state, state->path);

	chown_stub = fop_chown_stub (frame,
				     server_chown_resume,
				     &(state->loc),
				     state->uid,
				     state->gid);

	if ((state->loc.parent == NULL) ||
	    (state->loc.inode == NULL)) {
		do_path_lookup (chown_stub, &(state->loc));
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
	call_stub_t *chmod_stub = NULL;
	gf_fop_chmod_req_t *req = NULL;
	server_state_t *state = NULL;

	req       = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_CHMOD);

	server_loc_fill (&(state->loc), state, state->path);

	chmod_stub = fop_chmod_stub (frame,
				     server_chmod_resume,
				     &(state->loc),
				     state->mode);

	if ((state->loc.parent == NULL) ||
	    (state->loc.inode == NULL)) {
		do_path_lookup (chmod_stub, &(state->loc));
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
	call_stub_t *utimens_stub = NULL;
	gf_fop_utimens_req_t *req = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_UTIMENS);

	server_loc_fill (&(state->loc), state, state->path);

	utimens_stub = fop_utimens_stub (frame,
					 server_utimens_resume,
					 &(state->loc),
					 state->tv);

	if ((state->loc.parent == NULL) || 
	    (state->loc.inode == NULL)) {
		do_path_lookup (utimens_stub, &(state->loc));
	} else {
		call_resume (utimens_stub);
	}

	return 0;
}


 
int32_t
server_inodelk_resume (call_frame_t *frame,
		       xlator_t *this,
		       loc_t *loc, int32_t cmd,
		       struct flock *flock)
{
	server_state_t *state = NULL;
	
	state = STATE (frame);
	if (state->loc.inode == NULL) {
		state->loc.inode = inode_ref (loc->inode);
	}
	
	if (state->loc.parent == NULL) {
		state->loc.parent = inode_ref (loc->parent);
	}

 	STACK_WIND (frame,
 		    server_inodelk_cbk,
 		    BOUND_XL (frame),
 		    BOUND_XL (frame)->fops->inodelk,
 		    loc, cmd, flock);
 	return 0;
 
}
  
 
int32_t
server_inodelk (call_frame_t *frame,
		xlator_t *bound_xl,
		gf_hdr_common_t *hdr, size_t hdrlen,
		char *buf, size_t buflen)
{
 	call_stub_t *inodelk_stub = NULL;
 	gf_fop_inodelk_req_t *req = NULL;
 	server_state_t *state = NULL;
 
 	req       = gf_param (hdr);
   
 	state = STATE (frame);
 	server_state_fill (frame, req, GF_FOP_INODELK);

	server_loc_fill (&(state->loc), state, state->path);
 
 	inodelk_stub = fop_inodelk_stub (frame,
					 server_inodelk_resume,
					 &state->loc, state->cmd, &state->flock);
 
	if ((state->loc.parent == NULL) || 
	    (state->loc.inode == NULL)) {
		do_path_lookup (inodelk_stub, &(state->loc));
	} else {
		call_resume (inodelk_stub);
	}
 
 	return 0;
}


int32_t
server_finodelk (call_frame_t *frame,
		 xlator_t *bound_xl,
		 gf_hdr_common_t *hdr, size_t hdrlen,
		 char *buf, size_t buflen)
{
 	gf_fop_inodelk_req_t *req = NULL;
 	server_state_t *state = NULL;
 
 	req       = gf_param (hdr);
   
 	state = STATE (frame);
 	server_state_fill (frame, req, GF_FOP_FINODELK);

	if (!state->fd) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);
		
		server_finodelk_cbk (frame, NULL, frame->this,
				     -1, EINVAL);
		return -1;
  	} 

	STACK_WIND (frame, server_finodelk_cbk,
		    bound_xl, bound_xl->fops->finodelk,
		    state->fd, state->cmd, &state->flock);
 	return 0;
}
  
 
int32_t
server_entrylk_resume (call_frame_t *frame,
		       xlator_t *this,
		       loc_t *loc, const char *basename,
		       gf_dir_lk_cmd cmd, gf_dir_lk_type type)
{
	server_state_t *state = NULL;
	
	state = STATE (frame);
	
	if (state->loc.inode == NULL)
		state->loc.inode = inode_ref (loc->inode);
	
	if (state->loc.parent == NULL)
		state->loc.parent = inode_ref (loc->parent);

 	STACK_WIND (frame,
 		    server_entrylk_cbk,
 		    BOUND_XL (frame),
 		    BOUND_XL (frame)->fops->entrylk,
 		    loc, basename, cmd, type);
 	return 0;
 
}
 
/*
 * server_gf_dir_lk - gf_dir_lk function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
int32_t
server_entrylk (call_frame_t *frame,
		xlator_t *bound_xl,
		gf_hdr_common_t *hdr, size_t hdrlen,
		char *buf, size_t buflen)
{
 	call_stub_t *entrylk_stub = NULL;
 	gf_fop_entrylk_req_t *req = NULL;
 	server_state_t *state = NULL;
 
 	req       = gf_param (hdr);
   
 	state = STATE (frame);
 	server_state_fill (frame, req, GF_FOP_ENTRYLK);
 
 	server_loc_fill (&(state->loc), state, state->path);
 
  	entrylk_stub = fop_entrylk_stub (frame,
					 server_entrylk_resume,
					 &state->loc, state->basename, state->cmd, 
					 state->type);
 
 	if ((state->loc.parent == NULL) ||
	    (state->loc.inode == NULL)) {
 		do_path_lookup (entrylk_stub, &(state->loc));
 	} else {
 		call_resume (entrylk_stub);
 	}
 
 	return 0;
}


int32_t
server_fentrylk (call_frame_t *frame,
		 xlator_t *bound_xl,
		 gf_hdr_common_t *hdr, size_t hdrlen,
		 char *buf, size_t buflen)
{
 	gf_fop_fentrylk_req_t *req = NULL;
 	server_state_t *state = NULL;
 
 	req       = gf_param (hdr);
   
 	state = STATE (frame);
 	server_state_fill (frame, req, GF_FOP_FENTRYLK);

	if (!state->fd) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);
		
		server_fentrylk_cbk (frame, NULL, frame->this,
				     -1, EINVAL);
		return -1;
  	} 

	STACK_WIND (frame, server_fentrylk_cbk,
		    bound_xl, bound_xl->fops->fentrylk,
		    state->fd, state->basename, state->cmd, state->type);
 	return 0;
}


int32_t
server_access_resume (call_frame_t *frame,
                      xlator_t *this,
                      loc_t *loc,
                      int32_t mask)
{
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
	call_stub_t *access_stub = NULL;
	gf_fop_access_req_t *req = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_ACCESS);

	server_loc_fill (&(state->loc), state, state->path);

	access_stub = fop_access_stub (frame,
				       server_access_resume,
				       &(state->loc),
				       state->mask);

	if ((state->loc.parent == NULL) || 
	    (state->loc.inode == NULL)) {
		do_path_lookup (access_stub, &(state->loc));
	} else {
		call_resume (access_stub);
	}

	return 0;
}


int32_t
server_symlink_resume (call_frame_t *frame,
		       xlator_t *this,
		       const char *linkname,
		       loc_t *loc)
{
	server_state_t *state = STATE (frame);
	
	if (state->loc.parent == NULL) 
		state->loc.parent = inode_ref (loc->parent);

	if (loc->inode == NULL) {
		state->loc.inode = inode_new (BOUND_XL(frame)->itable);
	} else if (state->loc.inode != loc->inode) {
		if (state->loc.inode)
			inode_unref (state->loc.inode);
		state->loc.inode = inode_ref (loc->inode);
	}

	STACK_WIND (frame,
		    server_symlink_cbk,
		    BOUND_XL (frame),
		    BOUND_XL (frame)->fops->symlink,
		    linkname,
		    &(state->loc));

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
	server_state_t *state = NULL;
	gf_fop_symlink_req_t *req = NULL;
	call_stub_t *symlink_stub = NULL;
	
	req   = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_SYMLINK);
  
	server_loc_fill (&(state->loc), state, state->path); 
  
	symlink_stub = fop_symlink_stub (frame, server_symlink_resume, 
					 state->name, &(state->loc));

	if (state->loc.parent == NULL) {
		do_path_lookup (symlink_stub, &(state->loc));
	} else {
		call_resume (symlink_stub);
	}

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
             xlator_t *this,
             gf_hdr_common_t *hdr, size_t hdrlen,
             char *buf, size_t buflen)
{
	call_stub_t *link_stub = NULL;
	gf_fop_link_req_t *req = NULL;
	server_state_t *state = NULL;

	req   = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_LINK);

	server_loc_fill (&(state->loc), state, state->path);
	server_loc_fill (&(state->loc2), state, state->name);

	link_stub = fop_link_stub (frame, server_link_resume,
				   &(state->loc), &(state->loc2));

	if ((state->loc.parent == NULL) || 
	    (state->loc.inode == NULL)) {
		do_path_lookup (link_stub, &(state->loc));
	} else if (state->loc2.parent == NULL) {
		do_path_lookup (link_stub, &(state->loc2));
	} else {
		call_resume (link_stub);
	}

	return 0;
}


int32_t
server_rename_resume (call_frame_t *frame,
                      xlator_t *this,
                      loc_t *oldloc,
                      loc_t *newloc)
{
	server_state_t *state = NULL;
	
	state = STATE (frame);
	
	if (state->loc.parent == NULL) 
		state->loc.parent = inode_ref (oldloc->parent);

	if (state->loc.inode == NULL) {
		state->loc.inode = inode_ref (oldloc->inode);
	} 
	
	if (state->loc2.parent == NULL)
		state->loc2.parent = inode_ref (newloc->parent);
	
	STACK_WIND (frame,
		    server_rename_cbk,
		    BOUND_XL (frame),
		    BOUND_XL (frame)->fops->rename,
		    &(state->loc),
		    &(state->loc2));
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
	call_stub_t *rename_stub = NULL;
	gf_fop_rename_req_t *req = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);
  
	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_RENAME);

	server_loc_fill (&(state->loc), state, state->path);
	server_loc_fill (&(state->loc2), state, state->path2);

	rename_stub = fop_rename_stub (frame,
				       server_rename_resume,
				       &(state->loc),
				       &(state->loc2));

	if ((state->loc.parent == NULL) || 
	    (state->loc.inode == NULL)){
		/*    search of oldpath in inode cache _failed_.
		 *    we need to do a lookup for oldpath. we do a fops->lookup() for
		 * oldpath. call-back being server_stub_cbk(). server_stub_cbk() takes
		 * care of searching/lookup of newpath, if it already exists.
		 * server_stub_cbk() resumes to fops->rename(), after trying to lookup
		 * for newpath also.
		 *    if lookup of oldpath fails, server_stub_cbk() UNWINDs to
		 * server_rename_cbk() with ret=-1 and errno=ENOENT.
		 */
		do_path_lookup (rename_stub, &(state->loc));
	} else if ((state->loc2.parent == NULL)){
		/* inode for oldpath found in inode cache and search for newpath in inode
		 * cache_failed_.
		 * we need to lookup for newpath, with call-back being server_stub_cbk().
		 * since we already have found oldpath in inode cache, server_stub_cbk()
		 * continues with fops->rename(), irrespective of success or failure of
		 * lookup for newpath.
		 */
		do_path_lookup (rename_stub, &(state->loc2));
	} else {
		/* we have found inode for both oldpath and newpath in inode cache.
		 * we are continue with fops->rename() */
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
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_LK);

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_lk_cbk (frame, NULL, frame->this,
			       -1, EINVAL, NULL);

		goto out;
	}

	switch (state->cmd) {
	case GF_LK_GETLK:
		state->cmd = F_GETLK;
		break;
	case GF_LK_SETLK:
		state->cmd = F_SETLK;
		break;
	case GF_LK_SETLKW:
		state->cmd = F_SETLKW;
		break;
	}

	switch (state->type) {
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
		gf_log (bound_xl->name, GF_LOG_ERROR, "Unknown lock type: %d!", state->type);
		break;
	}

	gf_flock_to_flock (&req->flock, &lock);

	STACK_WIND (frame, server_lk_cbk,
		    bound_xl, bound_xl->fops->lk,
		    state->fd, state->cmd, &lock);

out:
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
	dir_entry_t *entry = NULL;
	server_state_t *state = NULL;

	req = gf_param (hdr);

	state = STATE (frame);
	server_state_fill (frame, req, GF_FOP_SETDENTS);

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"unresolved fd %"PRId64"", state->fd_no);

		server_setdents_cbk (frame, NULL, frame->this,
				     -1, EINVAL);

		goto out;
	}


	{
		dir_entry_t *trav = NULL, *prev = NULL;
		int32_t count, i, bread;
		char *ender = NULL, *buffer_ptr = NULL;
		char tmp_buf[512] = {0,};

		entry = calloc (1, sizeof (dir_entry_t));
		ERR_ABORT (entry);
		prev = entry;
		buffer_ptr = buf;

		for (i = 0; i < state->nr_count ; i++) {
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
		    state->fd,
		    state->flags,
		    entry,
		    state->nr_count);

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
out:
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
	char tmp_filename[GF_FILENAME_MAX] = {0,};
	char *filename = GLUSTERFSD_SPEC_PATH;
	int32_t file_len = 0;
	struct stat stbuf = {0,};
	gf_hdr_common_t *_hdr = NULL;
	gf_mop_getspec_rsp_t *rsp = NULL;
	size_t _hdrlen = 0;
	peer_info_t *peerinfo = NULL;

	peerinfo = &(TRANSPORT_OF (frame))->peerinfo;
	if (dict_get (frame->this->options, "client-volume-filename")) {
		filename = data_to_str (dict_get (frame->this->options,
						  "client-volume-filename"));
	}
	{
		sprintf (tmp_filename, "%s.%s", filename, peerinfo->identifier);
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
	gf_fop_checksum_rsp_t *rsp = NULL;
	size_t hdrlen = 0;

	hdrlen = gf_hdr_len (rsp, GF_FILENAME_MAX + 1 + GF_FILENAME_MAX + 1);
	hdr    = gf_hdr_new (rsp, GF_FILENAME_MAX + 1 + GF_FILENAME_MAX + 1);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret >= 0) {
		memcpy (rsp->fchecksum, fchecksum, GF_FILENAME_MAX);
		rsp->fchecksum[GF_FILENAME_MAX] =  '\0';
		memcpy (rsp->dchecksum + GF_FILENAME_MAX, dchecksum, GF_FILENAME_MAX);
		rsp->dchecksum[GF_FILENAME_MAX + GF_FILENAME_MAX] = '\0';
	}

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_CHECKSUM,
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
	gf_fop_checksum_req_t *req = NULL;

	req = gf_param (hdr);

	loc.path  = req->path;
	loc.ino   = ntoh64 (req->ino);
	loc.inode = NULL;
	flag      = ntoh32 (req->flag);

	STACK_WIND (frame,
		    server_checksum_cbk,
		    BOUND_XL (frame),
		    BOUND_XL (frame)->fops->checksum,
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

	ret = open (GLUSTERFSD_SPEC_PATH, O_WRONLY | O_CREAT | O_SYNC, 0644);
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
	int32_t              ret = -1;
	int32_t              remote_errno = 0;
	dict_t              *reply = NULL;
	connection_private_t *connection_priv = NULL;
	server_private_t    *server_priv = NULL;
	char                *name = NULL;
	char                *version = NULL;
	xlator_t            *xl = NULL;
	dict_t              *config_params = NULL;
	dict_t              *params = NULL;
	gf_hdr_common_t     *rsp_hdr = NULL;
	size_t               rsp_hdrlen = -1;
	gf_mop_setvolume_req_t *req = NULL;
	gf_mop_setvolume_rsp_t *rsp = NULL;
	size_t       dict_len = -1;
	peer_info_t *peerinfo = NULL;

	params = get_new_dict ();
	reply  = get_new_dict ();
	req    = gf_param (req_hdr);
	config_params = dict_copy (frame->this->options, NULL);

	dict_unserialize (req->buf, ntoh32 (req_hdr->size), &params);

	connection_priv = CONNECTION_PRIVATE (frame);

	server_priv = TRANSPORT_OF (frame)->xl->private;

	ret = dict_get_str (params, "version", &version);
	if (ret < 0) {
		remote_errno = EINVAL;
		dict_set_str (reply, "ERROR", 
			      "No version number specified");
		goto fail;
	}

	if (strcmp (version, PACKAGE_VERSION)) {
		char *msg = NULL;
		asprintf (&msg,
			  "Version mismatch: client(%s) Vs server (%s)",
			  version, PACKAGE_VERSION);
		remote_errno = EINVAL;
		ret = -1;
		dict_set_dynstr (reply, "ERROR", msg);
		goto fail;
	}


	ret = dict_get_str (params,
			     "remote-subvolume", &name);
	if (ret < 0) {
		remote_errno = EINVAL;
		dict_set_str (reply, "ERROR",
			      "No remote-subvolume option specified");
		goto fail;
	}

	xl = get_xlator_by_name (frame->this, name);
	if (xl == NULL) {
		char *msg = NULL;
		asprintf (&msg, "remote-subvolume \"%s\" is not found", name);
		dict_set_dynstr (reply, "ERROR", msg);
		ret = -1;
		remote_errno = ENOENT;
		goto fail;
	}

	peerinfo = &(TRANSPORT_OF (frame))->peerinfo;
	dict_set_static_ptr (params, "peer-info", peerinfo);

	if (server_priv->auth_modules == NULL) {
		gf_log (TRANSPORT_OF (frame)->xl->name,
			GF_LOG_ERROR,
			"Authentication module not initialized");
	}
	
	ret = gf_authenticate (params, config_params, server_priv->auth_modules);
	if (ret == AUTH_ACCEPT) {
		gf_log (TRANSPORT_OF (frame)->xl->name,  GF_LOG_DEBUG,
			"accepted client from %s",
			peerinfo->identifier);
		ret = 0;
		connection_priv->bound_xl = xl;
		dict_set_str (reply, "ERROR", "Success");
	} else {
		gf_log (TRANSPORT_OF (frame)->xl->name, GF_LOG_ERROR,
			"Cannot authenticate client from %s",
			peerinfo->identifier);
		ret = -1;
		remote_errno = EACCES;
		dict_set_str (reply, "ERROR", "Authentication failed");
		goto fail;
	}

	if (connection_priv->bound_xl == NULL) {
		dict_set_str (reply, "ERROR",
			      "Check volume spec file and handshake options");
		ret = -1;
		remote_errno = EACCES;
		goto fail;
	}

fail:
	if ((connection_priv->bound_xl != NULL) && 
	    (ret >= 0)               && 
	    (connection_priv->bound_xl->itable == NULL)) {
		/* create inode table for this bound_xl, if one doesn't already exist */
		int32_t lru_limit = 1024;
		xlator_t *xl = TRANSPORT_OF (frame)->xl;

		lru_limit = INODE_LRU_LIMIT (frame->this);

		gf_log (xl->name, GF_LOG_DEBUG,
			"creating inode table with lru_limit=%d, xlator=%s",
			lru_limit, connection_priv->bound_xl->name);

		connection_priv->bound_xl->itable = inode_table_new (lru_limit, 
								     connection_priv->bound_xl);
	}

	dict_set_int32 (reply, "RET", ret);
	dict_set_int32 (reply, "ERRNO", gf_errno_to_error (remote_errno));

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
			 "%"PRIx64",%"PRIx64",%"PRIx64
			 ",%"PRIx64",%"PRIx64",%"PRIx64
			 ",%"PRIx64",%"PRIx64"\n",
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
	gf_fop_flush_rsp_t *rsp = NULL;
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
 * get_frame_for_transport - get call frame for specified transport object
 *
 * @trans: transport object
 *
 */
static call_frame_t *
get_frame_for_transport (transport_t *trans)
{
	call_frame_t *frame = NULL;
	call_pool_t *pool = trans->xl->ctx->pool;
	connection_private_t *priv = trans->xl_private;
	server_state_t *state = NULL;;


	frame = create_frame (trans->xl, pool);

	state = calloc (1, sizeof (*state));

	if (priv->bound_xl)
		state->itable = priv->bound_xl->itable;

	state->bound_xl = priv->bound_xl;
	state->trans = transport_ref (trans);

	frame->root->trans = trans;
	frame->root->state = state;        /* which socket */
	frame->root->unique = 0;           /* which call */

	return frame;
}

/*
 * get_frame_for_call - create a frame into the capable of
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
	call_frame_t *frame = NULL;


	frame = get_frame_for_transport (trans);

	frame->op   = ntoh32 (hdr->op);
	frame->type = ntoh32 (hdr->type);

	frame->root->uid         = ntoh32 (hdr->req.uid);
	frame->root->unique      = ntoh64 (hdr->callid);      /* which call */
	frame->root->gid         = ntoh32 (hdr->req.gid);
	frame->root->pid         = ntoh32 (hdr->req.pid);

	return frame;
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
	[GF_FOP_STAT]         =  server_stat,
	[GF_FOP_READLINK]     =  server_readlink,
	[GF_FOP_MKNOD]        =  server_mknod,
	[GF_FOP_MKDIR]        =  server_mkdir,
	[GF_FOP_UNLINK]       =  server_unlink,
	[GF_FOP_RMDIR]        =  server_rmdir,
	[GF_FOP_SYMLINK]      =  server_symlink,
	[GF_FOP_RENAME]       =  server_rename,
	[GF_FOP_LINK]         =  server_link,
	[GF_FOP_CHMOD]        =  server_chmod,
	[GF_FOP_CHOWN]        =  server_chown,
	[GF_FOP_TRUNCATE]     =  server_truncate,
	[GF_FOP_OPEN]         =  server_open,
	[GF_FOP_READ]         =  server_readv,
	[GF_FOP_WRITE]        =  server_writev,
	[GF_FOP_STATFS]       =  server_statfs,
	[GF_FOP_FLUSH]        =  server_flush,
	[GF_FOP_FSYNC]        =  server_fsync,
	[GF_FOP_SETXATTR]     =  server_setxattr,
	[GF_FOP_GETXATTR]     =  server_getxattr,
	[GF_FOP_REMOVEXATTR]  =  server_removexattr,
	[GF_FOP_OPENDIR]      =  server_opendir,
	[GF_FOP_GETDENTS]     =  server_getdents,
	[GF_FOP_FSYNCDIR]     =  server_fsyncdir,
	[GF_FOP_ACCESS]       =  server_access,
	[GF_FOP_CREATE]       =  server_create,
	[GF_FOP_FTRUNCATE]    =  server_ftruncate,
	[GF_FOP_FSTAT]        =  server_fstat,
	[GF_FOP_LK]           =  server_lk,
	[GF_FOP_UTIMENS]      =  server_utimens,
	[GF_FOP_FCHMOD]       =  server_fchmod,
	[GF_FOP_FCHOWN]       =  server_fchown,
	[GF_FOP_LOOKUP]       =  server_lookup,
	[GF_FOP_SETDENTS]     =  server_setdents,
	[GF_FOP_RMELEM]       =  server_rmelem,
	[GF_FOP_READDIR]      =  server_readdir,
	[GF_FOP_INODELK]      =  server_inodelk,
	[GF_FOP_FINODELK]     =  server_finodelk,
	[GF_FOP_ENTRYLK]      =  server_entrylk,
	[GF_FOP_FENTRYLK]     =  server_fentrylk,
	[GF_FOP_CHECKSUM]     =  server_checksum,
	[GF_FOP_XATTROP]      =  server_xattrop,
};



static gf_op_t gf_mops[] = {
	[GF_MOP_SETVOLUME] = mop_setvolume,
	[GF_MOP_GETVOLUME] = mop_getvolume,
	[GF_MOP_STATS]     = mop_stats,
	[GF_MOP_SETSPEC]   = mop_setspec,
	[GF_MOP_GETSPEC]   = mop_getspec,
	[GF_MOP_LOCK]      = mop_lock,
	[GF_MOP_UNLOCK]    = mop_unlock,
	[GF_MOP_LISTLOCKS] = mop_listlocks,
	[GF_MOP_FSCK]      = mop_fsck,
};

static gf_op_t gf_cbks[] = {
	[GF_CBK_FORGET]     = server_forget,
	[GF_CBK_RELEASE]    = server_release,
	[GF_CBK_RELEASEDIR] = server_releasedir
};

int
protocol_server_interpret (xlator_t *this, transport_t *trans,
                           char *hdr_p, size_t hdrlen, char *buf, size_t buflen)
{
	int                  ret = -1;
	gf_hdr_common_t     *hdr = NULL;
	xlator_t            *bound_xl = NULL;
	connection_private_t   *priv = NULL;
	call_frame_t        *frame = NULL;
	int32_t              type = -1;
	int32_t              op = -1;
	peer_info_t         *peerinfo = NULL;

	hdr  = (gf_hdr_common_t *)hdr_p;
	type = ntoh32 (hdr->type);
	op   = ntoh32 (hdr->op);

	priv = trans->xl_private;
	bound_xl = priv->bound_xl;

	peerinfo = &trans->peerinfo;
	switch (type) {
	case GF_OP_TYPE_FOP_REQUEST:
		if (op < 0 || op > GF_FOP_MAXVALUE) {
			gf_log (this->name, GF_LOG_ERROR,
				"invalid fop %d from client %s", 
				op, peerinfo->identifier);
			break;
		}
		if (bound_xl == NULL) {
			gf_log (this->name, GF_LOG_ERROR,
				"Received fop %d before authentication.", op);
			break;
		}
		frame = get_frame_for_call (trans, hdr);
		ret = gf_fops[op] (frame, bound_xl, hdr, hdrlen, buf, buflen);
		break;

	case GF_OP_TYPE_MOP_REQUEST:
		if (op < 0 || op > GF_MOP_MAXVALUE) {
			gf_log (this->name, GF_LOG_ERROR,
				"invalid mop %d from client %s", 
				op, peerinfo->identifier);
			break;
		}
		frame = get_frame_for_call (trans, hdr);
		ret = gf_mops[op] (frame, bound_xl, hdr, hdrlen, buf, buflen);
		break;

	case GF_OP_TYPE_CBK_REQUEST:
		if (op < 0 || op > GF_CBK_MAXVALUE) {
			gf_log (this->name, GF_LOG_ERROR,
				"invalid cbk %d from client %s", 
				op, peerinfo->identifier);
			break;
		}
		frame = get_frame_for_call (trans, hdr);
		ret = gf_cbks[op] (frame, bound_xl, hdr, hdrlen, buf, buflen);
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
	server_state_t *state = NULL;
	
	state = STATE (frame);

	if (state)
		free_state (state);
	STACK_DESTROY (frame->root);
	return 0;
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
	connection_private_t *connection_priv = NULL;
	call_frame_t      *frame = NULL, *tmp_frame = NULL;
	peer_info_t       *peerinfo = NULL;
	xlator_t          *bound_xl = NULL;
	int32_t            ret = -1;
	struct list_head   file_lockers;
	struct list_head   dir_lockers;
	struct _lock_table *ltable = NULL;
	struct _locker     *locker = NULL, *tmp = NULL;
	struct flock        flock = {0,};
	
	connection_priv = trans->xl_private;
	if (connection_priv == NULL) {
		gf_log ("server",
			GF_LOG_CRITICAL,
			"connection private (null) for transport %p", trans);
		ret = -1;
		goto out;
	}

	bound_xl = (xlator_t *) (connection_priv->bound_xl);
	if (bound_xl) {
		/* trans will have ref_count = 1 after this call, but its ok since this function is
		   called in GF_EVENT_TRANSPORT_CLEANUP */
		frame = get_frame_for_transport (trans);
	  
		pthread_mutex_lock (&(connection_priv->lock));
		{
			if (connection_priv->ltable) {
				ltable = connection_priv->ltable;
				connection_priv->ltable = NULL;
			}
		}
		pthread_mutex_unlock (&connection_priv->lock);
		
		INIT_LIST_HEAD (&file_lockers);
		INIT_LIST_HEAD (&dir_lockers);
		
		LOCK (&ltable->lock);
		{
			list_splice_init (&ltable->file_lockers, &file_lockers);
			list_splice_init (&ltable->dir_lockers, &dir_lockers);
		}
		UNLOCK (&ltable->lock);
		free (ltable);

		flock.l_type  = F_UNLCK;
		flock.l_start = 0;
		flock.l_len   = 0;
		list_for_each_entry_safe (locker, tmp, &file_lockers, lockers) {
			tmp_frame = server_copy_frame (frame);
			tmp_frame->root->pid = locker->pid;

			if (locker->fd) {
				STACK_WIND (tmp_frame, server_nop_cbk,
					    bound_xl,
					    bound_xl->fops->finodelk,
					    locker->fd, F_SETLK, &flock);
				fd_unref (locker->fd);
			} else {
				STACK_WIND (tmp_frame, server_nop_cbk,
					    bound_xl,
					    bound_xl->fops->inodelk,
					    &(locker->loc), F_SETLK, &flock);
				loc_wipe (&locker->loc);
			}

			list_del_init (&locker->lockers);
			free (locker);
		}
		
		tmp = NULL;
		locker = NULL;
		list_for_each_entry_safe (locker, tmp, &dir_lockers, lockers) {
			tmp_frame = copy_frame (frame);
			tmp_frame->root->pid = locker->pid;

			if (locker->fd) {
				STACK_WIND (tmp_frame, server_nop_cbk,
					    bound_xl,
					    bound_xl->fops->fentrylk,
					    locker->fd, NULL, 
					    GF_DIR_LK_UNLOCK, GF_DIR_LK_WRLCK);
				fd_unref (locker->fd);
			} else {
				STACK_WIND (tmp_frame, server_nop_cbk,
					    bound_xl,
					    bound_xl->fops->entrylk,
					    &(locker->loc), NULL, 
					    GF_DIR_LK_UNLOCK, GF_DIR_LK_WRLCK);
				loc_wipe (&locker->loc);
			}

			list_del_init (&locker->lockers);
			free (locker);
		}

		pthread_mutex_lock (&(connection_priv->lock));
		{
			if (connection_priv->fdtable) {
				gf_fd_fdtable_destroy (connection_priv->fdtable);
				connection_priv->fdtable = NULL;
			}
		}
		pthread_mutex_unlock (&connection_priv->lock);

	}
	FREE (connection_priv);
	trans->xl_private = NULL;
	peerinfo = &trans->peerinfo;
	gf_log (trans->xl->name, GF_LOG_DEBUG,
		"cleaned up transport state for client %s",
		peerinfo->identifier);

out:
	return ret;
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
	int32_t ret = 0;
	int32_t error = 0;
	transport_t *trans = NULL;
	server_conf_t *conf = NULL;
	server_private_t *server_priv = NULL;

	if (!this->children) {
		gf_log (this->name, GF_LOG_ERROR,
			"protocol/server should have subvolume");
		return -1;
	}

	trans = transport_load (this->options, this);
	if (!trans) {
		gf_log (this->name, GF_LOG_ERROR, 
			"failed to load transport");
		return -1;
	}

	ret = transport_listen (trans);
	if (ret == -1)
	{
		gf_log (this->name, GF_LOG_ERROR, 
			"failed to bind/listen on socket");
		return -1;
	}

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

	if (dict_get (this->options, "inode-lru-limit")) {
		conf->inode_lru_limit = data_to_int32 ((dict_get (this->options,
								  "inode-lru-limit")));
	} else {
		conf->inode_lru_limit = 1024;
	}

	if (dict_get (this->options, "limits.transaction-size")) 
	{
		conf->max_block_size = data_to_int32 (dict_get (this->options,
								"limits.trasaction-size"));
	} 
	else 
	{
		gf_log (this->name, GF_LOG_DEBUG,
			"defaulting limits.transaction-size to %d", 
			DEFAULT_BLOCK_SIZE);
		conf->max_block_size = DEFAULT_BLOCK_SIZE;
	}

#ifndef GF_DARWIN_HOST_OS
	{
		struct rlimit lim;
    
		lim.rlim_cur = 1048576;
		lim.rlim_max = 1048576;

		if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
			gf_log (this->name, GF_LOG_WARNING, 
				"WARNING: Failed to set 'ulimit -n 1048576': %s",
				strerror(errno));
			lim.rlim_cur = 65536;
			lim.rlim_max = 65536;
      
			if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
				gf_log (this->name, GF_LOG_ERROR, 
					"Failed to set max open fd to 64k: %s", 
					strerror(errno));
			} else {
				gf_log (this->name, GF_LOG_ERROR, 
					"max open fd set to 64k");
			}
		}
	}
#endif

	trans->xl_private = conf;

	return 0;
}

static struct _lock_table *
gf_lock_table_new (void)
{
	struct _lock_table *new = NULL;

	new = calloc (1, sizeof (struct _lock_table));
	if (new == NULL) {
		gf_log ("server-protocol", GF_LOG_CRITICAL,
			"failed to allocate memory for new lock table");
		goto out;
	}
	INIT_LIST_HEAD (&new->dir_lockers);
	INIT_LIST_HEAD (&new->file_lockers);
	LOCK_INIT (&new->lock);
out:
	return new;
}

int
protocol_server_pollin (xlator_t *this, transport_t *trans)
{
	char *hdr = NULL;
	size_t hdrlen = 0;
	char *buf = NULL;
	size_t buflen = 0;
	connection_private_t *priv = NULL;
	server_conf_t *conf = NULL;
	int ret = -1;

	priv = trans->xl_private;
	conf = this->private;

	if (!priv) {
		priv = (void *) calloc (1, sizeof (*priv));
		ERR_ABORT (priv);

		trans->xl_private = priv;

		priv->fdtable = gf_fd_fdtable_alloc ();
		if (priv->fdtable == NULL) {
			gf_log (this->name, GF_LOG_ERROR,  "Cannot allocate fdtable");
			ret = -1;
			goto out;
		}
		
		priv->ltable = gf_lock_table_new ();
		if (priv->ltable == NULL) {
			gf_log (this->name, GF_LOG_ERROR, 
				"Cannot allocate lock table");
			ret = -1;
			goto out;
		}
		pthread_mutex_init (&priv->lock, NULL);
	}

	ret = transport_receive (trans, &hdr, &hdrlen, &buf, &buflen);

	if (ret == 0)
		ret = protocol_server_interpret (this, trans, hdr, hdrlen, buf, buflen);

	/* TODO: use mem-pool */
	FREE (hdr);
out:
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
	if (!server_priv)
		return;

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

	switch (event) {
	case GF_EVENT_POLLIN:
		ret = protocol_server_pollin (this, trans);
		break;
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

struct xlator_cbks cbks = {
};

struct xlator_options options[] = {
	/* Authentication module */
	{ "auth.ip.<volume-name>.allow", 
	  GF_OPTION_TYPE_ANY, 8, 0, 0 }, /* 1.3.x version support */
	{ "auth.addr.<volume-name>.[allow|reject]", 
	  GF_OPTION_TYPE_ANY, 10, 0, 0 },
	{ "auth.login.<volume-name>.allow", 
	  GF_OPTION_TYPE_STR, 11, 0, 0 }, 

	/* Transport */
	{ "ib-verbs-[port|mtu|device-name|work-request-send-size...]", 
	  GF_OPTION_TYPE_ANY, 9, 0, 0 }, 
	{ "listen-port", 
	  GF_OPTION_TYPE_INT, 0, 1025, 65534 }, 
	{ "transport-type", 
	  GF_OPTION_TYPE_STR, 0, 0, 0, 
	  "tcp|ib-verbs|ib-sdp|socket|unix|tcp/server|ib-verbs/server" }, 
	{ "address-family", 
	  GF_OPTION_TYPE_STR, 0, 0, 0, 
	  "inet|inet6|inet/inet6|inet6/inet|unix|ib-sdp" }, 
	{ "bind-address", 
	  GF_OPTION_TYPE_STR, 0, }, 
	{ "listen-path", 
	  GF_OPTION_TYPE_STR, 0, 0, 0 }, 

	/* Server protocol itself */
	{ "limits.transaction-size", 
	  GF_OPTION_TYPE_SIZET, 0, 128 * GF_UNIT_KB, 8 * GF_UNIT_MB }, 
	{ "client-volume-filename", 
	  GF_OPTION_TYPE_PATH, 0, }, 

	{ "inode-lru-limit", 
	  GF_OPTION_TYPE_INT, 0, 0, 1048576 },
	
	{ NULL, 0, 0, 0, 0 },
};
