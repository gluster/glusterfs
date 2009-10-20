/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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
#include "server-protocol.h"
#include "server-helpers.h"
#include "call-stub.h"
#include "defaults.h"
#include "list.h"
#include "dict.h"
#include "compat.h"
#include "compat-errno.h"
#include "statedump.h"
#include "md5.h"


static void
protocol_server_reply (call_frame_t *frame, int type, int op,
                       gf_hdr_common_t *hdr, size_t hdrlen,
                       struct iovec *vector, int count,
                       struct iobref *iobref)
{
	server_state_t *state = NULL;
	xlator_t       *bound_xl = NULL;
	transport_t    *trans = NULL;
        int             ret = 0;

	bound_xl = BOUND_XL (frame);
	state    = CALL_STATE (frame);
	trans    = state->trans;

	hdr->callid = hton64 (frame->root->unique);
	hdr->type   = hton32 (type);
	hdr->op     = hton32 (op);

	ret = transport_submit (trans, (char *)hdr, hdrlen, vector, 
                                count, iobref);
        if (ret < 0) {
                gf_log ("protocol/server", GF_LOG_ERROR,
                        "frame %"PRId64": failed to submit. op= %d, type= %d",
                        frame->root->unique, op, type);
        }

	STACK_DESTROY (frame->root);

	if (state)
		free_state (state);

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
int
server_setdents_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
	gf_hdr_common_t        *hdr = NULL;
	gf_fop_setdents_rsp_t  *rsp = NULL;
	size_t                  hdrlen = 0;
	int32_t                 gf_errno = 0;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret   = hton32 (op_ret);
	gf_errno = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

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
int
server_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct flock *lock)
{
	gf_hdr_common_t     *hdr = NULL;
	gf_fop_lk_rsp_t     *rsp = NULL;
	size_t               hdrlen = 0;
	int32_t              gf_errno = 0;
	server_state_t      *state = NULL;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret == 0) {
		gf_flock_from_flock (&rsp->flock, lock);
	} else if (op_errno != ENOSYS) {
		state = CALL_STATE(frame);

		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": LK %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no,
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
	}

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_LK,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}


int
server_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
	server_connection_t  *conn = NULL;
 	gf_hdr_common_t      *hdr = NULL;
 	gf_fop_inodelk_rsp_t *rsp = NULL;
	server_state_t       *state = NULL;
 	size_t                hdrlen = 0;
	int32_t               gf_errno = 0;
	
	conn = SERVER_CONNECTION(frame);

	state = CALL_STATE(frame);

 	hdrlen = gf_hdr_len (rsp, 0);
 	hdr    = gf_hdr_new (rsp, 0);
 	rsp    = gf_param (hdr);

 	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno = gf_errno_to_error (op_errno);
 	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret >= 0) {
		if (state->flock.l_type == F_UNLCK)
			gf_del_locker (conn->ltable, state->volume,
				       &state->loc, NULL, frame->root->pid);
		else
			gf_add_locker (conn->ltable, state->volume,
				       &state->loc, NULL, frame->root->pid);
	} else if (op_errno != ENOSYS) {
		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": INODELK %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path, 
			state->loc.inode ? state->loc.inode->ino : 0, op_ret,
			strerror (op_errno));
	}

 	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_INODELK,
 			       hdr, hdrlen, NULL, 0, NULL);

 	return 0;
}


int
server_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
	server_connection_t   *conn = NULL;
 	gf_hdr_common_t       *hdr = NULL;
 	gf_fop_finodelk_rsp_t *rsp = NULL;
	server_state_t        *state = NULL;
 	size_t                 hdrlen = 0;
	int32_t                gf_errno = 0;
	
	conn = SERVER_CONNECTION(frame);

 	hdrlen = gf_hdr_len (rsp, 0);
 	hdr    = gf_hdr_new (rsp, 0);
 	rsp    = gf_param (hdr);

 	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno = gf_errno_to_error (op_errno);
 	hdr->rsp.op_errno = hton32 (gf_errno);
	
	state = CALL_STATE(frame);

	if (op_ret >= 0) {
		if (state->flock.l_type == F_UNLCK)
			gf_del_locker (conn->ltable, state->volume,
				       NULL, state->fd, 
                                       frame->root->pid);
		else
			gf_add_locker (conn->ltable, state->volume,
				       NULL, state->fd, 
                                       frame->root->pid);
	} else if (op_errno != ENOSYS) {
		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": FINODELK %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no, 
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
	}

 	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FINODELK,
 			       hdr, hdrlen, NULL, 0, NULL);

 	return 0;
}


/*
 * server_entrylk_cbk - 
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @lock:
 *
 * not for external reference
 */
int
server_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
	server_connection_t  *conn = NULL;
 	gf_hdr_common_t      *hdr = NULL;
 	gf_fop_entrylk_rsp_t *rsp = NULL;
	server_state_t       *state = NULL;
 	size_t                hdrlen = 0;
	int32_t               gf_errno = 0;

	conn = SERVER_CONNECTION(frame);

	state = CALL_STATE(frame);

 	hdrlen = gf_hdr_len (rsp, 0);
 	hdr    = gf_hdr_new (rsp, 0);
 	rsp    = gf_param (hdr);

 	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno = gf_errno_to_error (op_errno);
 	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret >= 0) {
		if (state->cmd == ENTRYLK_UNLOCK)
			gf_del_locker (conn->ltable, state->volume,
				       &state->loc, NULL, frame->root->pid);
		else
			gf_add_locker (conn->ltable, state->volume,
				       &state->loc, NULL, frame->root->pid);
	} else if (op_errno != ENOSYS) {
		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": INODELK %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path, 
			state->loc.inode ? state->loc.inode->ino : 0, op_ret,
			strerror (op_errno));
	}

 	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_ENTRYLK,
 			       hdr, hdrlen, NULL, 0, NULL);

 	return 0;
}


int
server_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
	server_connection_t   *conn = NULL;
 	gf_hdr_common_t       *hdr = NULL;
 	gf_fop_fentrylk_rsp_t *rsp = NULL;
	server_state_t        *state = NULL;
 	size_t                 hdrlen = 0;
	int32_t                gf_errno = 0;

	conn = SERVER_CONNECTION(frame);

 	hdrlen = gf_hdr_len (rsp, 0);
 	hdr    = gf_hdr_new (rsp, 0);
 	rsp    = gf_param (hdr);

 	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
 	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret >= 0) {
		state = CALL_STATE(frame);
		if (state->cmd == ENTRYLK_UNLOCK)
			gf_del_locker (conn->ltable, state->volume,
				       NULL, state->fd, frame->root->pid);
		else
			gf_add_locker (conn->ltable, state->volume,
				       NULL, state->fd, frame->root->pid);
	} else if (op_errno != ENOSYS) {
		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": FENTRYLK %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no, 
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
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
int
server_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno)
{
	gf_hdr_common_t     *hdr = NULL;
	gf_fop_access_rsp_t *rsp = NULL;
	server_state_t      *state = NULL;
	size_t               hdrlen = 0;
	int32_t              gf_errno = 0;

	state = CALL_STATE(frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_ACCESS,
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
int
server_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct stat *preparent,
                  struct stat *postparent)
{
	gf_hdr_common_t    *hdr = NULL;
	gf_fop_rmdir_rsp_t *rsp = NULL;
	server_state_t     *state = NULL;
	int32_t             gf_errno = 0;
	size_t              hdrlen = 0;
        inode_t            *parent = NULL;

	state = CALL_STATE(frame);

	if (op_ret == 0) {
		inode_unlink (state->loc.inode, state->loc.parent,
			      state->loc.name);
                parent = inode_parent (state->loc.inode, 0, NULL);
                if (parent)
                        inode_unref (parent);
                else
                        inode_forget (state->loc.inode, 0);
	} else {
		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": RMDIR %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path,
			state->loc.inode ? state->loc.inode->ino : 0,
			op_ret, strerror (op_errno));
	}

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

        if (op_ret == 0) {
                gf_stat_from_stat (&rsp->preparent, preparent);
                gf_stat_from_stat (&rsp->postparent, postparent);
        }

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_RMDIR,
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
int
server_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct stat *stbuf, struct stat *preparent,
                  struct stat *postparent)
{
	gf_hdr_common_t    *hdr = NULL;
	gf_fop_mkdir_rsp_t *rsp = NULL;
	server_state_t     *state = NULL;
	size_t              hdrlen = 0;
	int32_t             gf_errno = 0;
        inode_t            *link_inode = NULL;

	state = CALL_STATE(frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret >= 0) {
		gf_stat_from_stat (&rsp->stat, stbuf);
		gf_stat_from_stat (&rsp->preparent, preparent);
		gf_stat_from_stat (&rsp->postparent, postparent);

		link_inode = inode_link (inode, state->loc.parent,
                                         state->loc.name, stbuf);
		inode_lookup (link_inode);
                inode_unref (link_inode);
	} else {
		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": MKDIR %s  ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path,
			op_ret, strerror (op_errno));
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
int
server_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  inode_t *inode, struct stat *stbuf, struct stat *preparent,
                  struct stat *postparent)
{
	gf_hdr_common_t    *hdr = NULL;
	gf_fop_mknod_rsp_t *rsp = NULL;
	server_state_t     *state = NULL;
	int32_t             gf_errno = 0;
	size_t              hdrlen = 0;
        inode_t            *link_inode = NULL;

	state = CALL_STATE(frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret >= 0) {
		gf_stat_from_stat (&rsp->stat, stbuf);
                gf_stat_from_stat (&rsp->preparent, preparent);
                gf_stat_from_stat (&rsp->postparent, postparent);

		link_inode = inode_link (inode, state->loc.parent,
                                         state->loc.name, stbuf);
		inode_lookup (link_inode);
                inode_unref (link_inode);
	} else {
		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": MKNOD %s ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path,
			op_ret, strerror (op_errno));
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
int
server_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
	gf_hdr_common_t       *hdr = NULL;
	gf_fop_fsyncdir_rsp_t *rsp = NULL;
	size_t                 hdrlen = 0;
	int32_t                gf_errno = 0;
	server_state_t        *state = NULL;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);
	
	if (op_ret < 0) {
		state = CALL_STATE(frame);
		
		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": FSYNCDIR %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no, 
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
	}

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

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
int
server_getdents_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dir_entry_t *entries,
                     int32_t count)
{
	gf_hdr_common_t       *hdr = NULL;
	gf_fop_getdents_rsp_t *rsp = NULL;
	size_t                 hdrlen = 0;
	int32_t                vec_count = 0;
	int32_t                gf_errno = 0;
        struct iobref         *iobref = NULL;
        struct iobuf          *iobuf = NULL;
	size_t                 buflen = 0;
	struct iovec           vector[1];
	server_state_t        *state = NULL;

	state = CALL_STATE(frame);

	if (op_ret >= 0) {
                iobuf = iobuf_get (this->ctx->iobuf_pool);
                if (!iobuf) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

		buflen = gf_direntry_to_bin (entries, iobuf->ptr);
		if (buflen < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"fd - %"PRId64" (%"PRId64"): failed to convert "
				"entries list to string buffer",
				state->resolve.fd_no, state->fd->inode->ino);
			op_ret = -1;
			op_errno = EINVAL;
			goto out;
		}

		iobref = iobref_new ();
		if (iobref == NULL) {
			gf_log (this->name, GF_LOG_ERROR,
				"fd - %"PRId64" (%"PRId64"): failed to get iobref",
				state->resolve.fd_no, state->fd->inode->ino);
			op_ret = -1;
			op_errno = ENOMEM;
			goto out;
		}
		
                iobref_add (iobref, iobuf);

		vector[0].iov_base = iobuf->ptr;
		vector[0].iov_len = buflen;
		vec_count = 1;
	} else {
		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": GETDENTS %"PRId64" (%"PRId64"): %"PRId32" (%s)",
			frame->root->unique,
			state->resolve.fd_no, 
			state->fd ? state->fd->inode->ino : 0, 
			op_ret, strerror (op_errno));
		vector[0].iov_base = NULL;
		vector[0].iov_len = 0;
	}

out:
	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	rsp->count = hton32 (count);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_GETDENTS,
			       hdr, hdrlen, vector, vec_count, iobref);

	if (iobref)
		iobref_unref (iobref);
        if (iobuf)
                iobuf_unref (iobuf);

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
int
server_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
	gf_hdr_common_t      *hdr = NULL;
	gf_fop_readdir_rsp_t *rsp = NULL;
	size_t                hdrlen = 0;
	size_t                buf_size = 0;
	int32_t               gf_errno = 0;
	server_state_t       *state = NULL;

	if (op_ret > 0)
		buf_size = gf_dirent_serialize (entries, NULL, 0);

	hdrlen = gf_hdr_len (rsp, buf_size);
	hdr    = gf_hdr_new (rsp, buf_size);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret > 0) {
		rsp->size = hton32 (buf_size);
		gf_dirent_serialize (entries, rsp->buf, buf_size);
	} else {
		state = CALL_STATE(frame);

		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": READDIR %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no, 
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
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
int
server_releasedir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno)
{
	gf_hdr_common_t         *hdr = NULL;
	gf_cbk_releasedir_rsp_t *rsp = NULL;
	size_t                   hdrlen = 0;
	int32_t                  gf_errno = 0;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

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
int
server_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd)
{
	server_connection_t   *conn = NULL;
	gf_hdr_common_t       *hdr = NULL;
	gf_fop_opendir_rsp_t  *rsp = NULL;
	server_state_t        *state = NULL;
	size_t                 hdrlen = 0;
	int32_t                gf_errno = 0;
        uint64_t               fd_no = 0;

	conn = SERVER_CONNECTION (frame);

	state = CALL_STATE (frame);

	if (op_ret >= 0) {
		fd_bind (fd);

		fd_no = gf_fd_unused_get (conn->fdtable, fd);
                fd_ref (fd); // on behalf of the client
	} else {
		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": OPENDIR %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path,
			state->loc.inode ? state->loc.inode->ino : 0,
			op_ret, strerror (op_errno));
	}

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);
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
int
server_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct statvfs *buf)
{
	gf_hdr_common_t     *hdr = NULL;
	gf_fop_statfs_rsp_t *rsp = NULL;
	server_state_t      *state = NULL;
	size_t               hdrlen = 0;
	int32_t              gf_errno = 0;

	state = CALL_STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret >= 0) {
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
int
server_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno)
{
	gf_hdr_common_t          *hdr = NULL;
	gf_fop_removexattr_rsp_t *rsp = NULL;
	server_state_t           *state = NULL;
	size_t                    hdrlen = 0;
	int32_t                   gf_errno = 0;

	state = CALL_STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

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
int
server_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict)
{
	gf_hdr_common_t       *hdr = NULL;
	gf_fop_getxattr_rsp_t *rsp = NULL;
	server_state_t        *state = NULL;
	size_t                 hdrlen = 0;
	int32_t                len = 0;
	int32_t                gf_errno = 0;
	int32_t                ret = -1;

	state = CALL_STATE (frame);

	if (op_ret >= 0) {
		len = dict_serialized_length (dict);
		if (len < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"%s (%"PRId64"): failed to get serialized length of "
				"reply dict",
				state->loc.path, state->resolve.ino);
			op_ret   = -1;
			op_errno = EINVAL;
			len = 0;
		}
	}

	hdrlen = gf_hdr_len (rsp, len + 1);
	hdr    = gf_hdr_new (rsp, len + 1);
	rsp    = gf_param (hdr);

	if (op_ret >= 0) {
		ret = dict_serialize (dict, rsp->dict);
		if (len < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"%s (%"PRId64"): failed to serialize reply dict",
				state->loc.path, state->resolve.ino);
			op_ret = -1;
			op_errno = -ret;
		}
	}
	rsp->dict_len = hton32 (len);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_GETXATTR,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}


int
server_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict)
{
	gf_hdr_common_t        *hdr  = NULL;
	gf_fop_fgetxattr_rsp_t *rsp = NULL;
	server_state_t         *state = NULL;
	size_t                  hdrlen = 0;
	int32_t                 len = 0;
	int32_t                 gf_errno = 0;
	int32_t                 ret = -1;

	state = CALL_STATE (frame);

	if (op_ret >= 0) {
		len = dict_serialized_length (dict);
		if (len < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"%s (%"PRId64"): failed to get serialized length of "
				"reply dict",
				state->loc.path, state->resolve.ino);
			op_ret   = -1;
			op_errno = EINVAL;
			len = 0;
		}
	}

	hdrlen = gf_hdr_len (rsp, len + 1);
	hdr    = gf_hdr_new (rsp, len + 1);
	rsp    = gf_param (hdr);

	if (op_ret >= 0) {
		ret = dict_serialize (dict, rsp->dict);
		if (len < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"%s (%"PRId64"): failed to serialize reply dict",
				state->loc.path, state->resolve.ino);
			op_ret = -1;
			op_errno = -ret;
		}
	}
	rsp->dict_len = hton32 (len);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FGETXATTR,
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
int
server_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
	gf_hdr_common_t        *hdr = NULL;
	gf_fop_setxattr_rsp_t  *rsp = NULL;
	server_state_t         *state = NULL;
	size_t                  hdrlen = 0;
	int32_t                 gf_errno = 0;

	state = CALL_STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_SETXATTR,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}


int
server_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno)
{
	gf_hdr_common_t        *hdr = NULL;
	gf_fop_fsetxattr_rsp_t *rsp = NULL;
	server_state_t         *state = NULL;
	size_t                  hdrlen = 0;
	int32_t                 gf_errno = 0;

	state = CALL_STATE(frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FSETXATTR,
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
int
server_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct stat *stbuf,
                   struct stat *preoldparent, struct stat *postoldparent,
                   struct stat *prenewparent, struct stat *postnewparent)
{
	gf_hdr_common_t     *hdr = NULL;
	gf_fop_rename_rsp_t *rsp = NULL;
	server_state_t      *state = NULL;
	size_t               hdrlen = 0;
	int32_t              gf_errno = 0;

	state = CALL_STATE(frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret == 0) {
		stbuf->st_ino  = state->loc.inode->ino;
		stbuf->st_mode = state->loc.inode->st_mode;

		gf_log (state->bound_xl->name, GF_LOG_TRACE,
			"%"PRId64": RENAME_CBK (%"PRId64") %"PRId64"/%s "
			"==> %"PRId64"/%s",
			frame->root->unique, state->loc.inode->ino,
			state->loc.parent->ino,	state->loc.name,
			state->loc2.parent->ino, state->loc2.name);

		inode_rename (state->itable,
			      state->loc.parent, state->loc.name,
			      state->loc2.parent, state->loc2.name,
			      state->loc.inode, stbuf);
		gf_stat_from_stat (&rsp->stat, stbuf);

		gf_stat_from_stat (&rsp->preoldparent, preoldparent);
		gf_stat_from_stat (&rsp->postoldparent, postoldparent);

		gf_stat_from_stat (&rsp->prenewparent, prenewparent);
		gf_stat_from_stat (&rsp->postnewparent, postnewparent);
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
int
server_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct stat *preparent,
                   struct stat *postparent)
{
	gf_hdr_common_t      *hdr = NULL;
	gf_fop_unlink_rsp_t  *rsp = NULL;
	server_state_t       *state = NULL;
	size_t                hdrlen = 0;
	int32_t               gf_errno = 0;
        inode_t              *parent = NULL;

	state = CALL_STATE(frame);

	if (op_ret == 0) {
		gf_log (state->bound_xl->name, GF_LOG_TRACE,
			"%"PRId64": UNLINK_CBK %"PRId64"/%s (%"PRId64")",
			frame->root->unique, state->loc.parent->ino,
			state->loc.name, state->loc.inode->ino);

		inode_unlink (state->loc.inode, state->loc.parent,
			      state->loc.name);

                parent = inode_parent (state->loc.inode, 0, NULL);
                if (parent)
                        inode_unref (parent);
                else
                        inode_forget (state->loc.inode, 0);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": UNLINK %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path,
			state->loc.inode ? state->loc.inode->ino : 0,
			op_ret, strerror (op_errno));
	}

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

        if (op_ret == 0) {
                gf_stat_from_stat (&rsp->preparent, preparent);
                gf_stat_from_stat (&rsp->postparent, postparent);
        }

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
int
server_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct stat *stbuf, struct stat *preparent,
                    struct stat *postparent)
{
	gf_hdr_common_t      *hdr = NULL;
	gf_fop_symlink_rsp_t *rsp = NULL;
	server_state_t       *state = NULL;
	size_t                hdrlen = 0;
	int32_t               gf_errno = 0;
        inode_t              *link_inode = NULL;

	state = CALL_STATE(frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret >= 0) {
		gf_stat_from_stat (&rsp->stat, stbuf);
                gf_stat_from_stat (&rsp->preparent, preparent);
                gf_stat_from_stat (&rsp->postparent, postparent);

		link_inode = inode_link (inode, state->loc.parent,
                                         state->loc.name, stbuf);
		inode_lookup (link_inode);
                inode_unref (link_inode);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": SYMLINK %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path,
			state->loc.inode ? state->loc.inode->ino : 0,
			op_ret, strerror (op_errno));
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
int
server_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct stat *stbuf, struct stat *preparent,
                 struct stat *postparent)
{
	gf_hdr_common_t   *hdr = NULL;
	gf_fop_link_rsp_t *rsp = NULL;
	server_state_t    *state = NULL;
	int32_t            gf_errno = 0;
	size_t             hdrlen = 0;
        inode_t           *link_inode = NULL;

	state = CALL_STATE(frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret == 0) {
		stbuf->st_ino = state->loc.inode->ino;

		gf_stat_from_stat (&rsp->stat, stbuf);
		gf_stat_from_stat (&rsp->preparent, preparent);
		gf_stat_from_stat (&rsp->postparent, postparent);

		gf_log (state->bound_xl->name, GF_LOG_TRACE,
			"%"PRId64": LINK (%"PRId64") %"PRId64"/%s ==> %"PRId64"/%s",
			frame->root->unique, inode->ino,
                        state->loc2.parent->ino,
			state->loc2.name, state->loc.parent->ino,
                        state->loc.name);

		link_inode = inode_link (inode, state->loc2.parent,
                                         state->loc2.name, stbuf);
                inode_unref (link_inode);
	} else {
		gf_log (state->bound_xl->name, GF_LOG_DEBUG,
			"%"PRId64": LINK (%"PRId64") %"PRId64"/%s ==> %"PRId64"/%s "
			" ==> %"PRId32" (%s)",
			frame->root->unique, inode->ino,
                        state->loc2.parent->ino,
			state->loc2.name, state->loc.parent->ino,
                        state->loc.name,
			op_ret, strerror (op_errno));
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
int
server_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                     struct stat *postbuf)
{
	gf_hdr_common_t       *hdr = NULL;
	gf_fop_truncate_rsp_t *rsp = NULL;
	server_state_t        *state = NULL;
	size_t                 hdrlen = 0;
	int32_t                gf_errno = 0;

	state = CALL_STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret == 0) {
		gf_stat_from_stat (&rsp->prestat, prebuf);
		gf_stat_from_stat (&rsp->poststat, postbuf);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": TRUNCATE %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path,
			state->loc.inode ? state->loc.inode->ino : 0,
			op_ret, strerror (op_errno));
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
int
server_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct stat *stbuf)
{
	gf_hdr_common_t    *hdr = NULL;
	gf_fop_fstat_rsp_t *rsp = NULL;
	size_t              hdrlen = 0;
	int32_t             gf_errno = 0;
	server_state_t     *state = NULL;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret == 0) {
		gf_stat_from_stat (&rsp->stat, stbuf);
	} else {
		state = CALL_STATE(frame);
		
		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": FSTAT %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no, 
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
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
int
server_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                      struct stat *postbuf)
{
	gf_hdr_common_t        *hdr = NULL;
	gf_fop_ftruncate_rsp_t *rsp = NULL;
	size_t                  hdrlen = 0;
	int32_t                 gf_errno = 0;
	server_state_t         *state = NULL;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret == 0) {
		gf_stat_from_stat (&rsp->prestat, prebuf);
		gf_stat_from_stat (&rsp->poststat, postbuf);
	} else {
		state = CALL_STATE (frame);

		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": FTRUNCATE %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no, 
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
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
int
server_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
	gf_hdr_common_t    *hdr = NULL;
	gf_fop_flush_rsp_t *rsp = NULL;
	size_t              hdrlen = 0;
	int32_t             gf_errno = 0;
	server_state_t     *state = NULL;

	if (op_ret < 0) {
		state = CALL_STATE(frame);

		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": FLUSH %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no, 
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
	}

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);
	
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
int
server_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                  struct stat *postbuf)
{
	gf_hdr_common_t    *hdr = NULL;
	gf_fop_fsync_rsp_t *rsp = NULL;
	size_t              hdrlen = 0;
	int32_t             gf_errno = 0;
	server_state_t     *state = NULL;

	if (op_ret < 0) {
		state = CALL_STATE(frame);

		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": FSYNC %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no, 
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
	}

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

        if (op_ret >= 0) {
                gf_stat_from_stat (&(rsp->prestat), prebuf);
                gf_stat_from_stat (&(rsp->poststat), postbuf);
        }

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
int
server_release_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno)
{
	gf_hdr_common_t      *hdr = NULL;
	gf_cbk_release_rsp_t *rsp = NULL;
	size_t                hdrlen = 0;
	int32_t               gf_errno = 0;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

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

int
server_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                   struct stat *postbuf)
{
	gf_hdr_common_t    *hdr = NULL;
	gf_fop_write_rsp_t *rsp = NULL;
	size_t              hdrlen = 0;
	int32_t             gf_errno = 0;
	server_state_t     *state = NULL;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret >= 0) {
		gf_stat_from_stat (&rsp->prestat, prebuf);
		gf_stat_from_stat (&rsp->poststat, postbuf);
	} else {
		state = CALL_STATE(frame);

		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": WRITEV %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no, 
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
	}

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_WRITE,
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
int
server_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iovec *vector, int32_t count,
                  struct stat *stbuf, struct iobref *iobref)
{
	gf_hdr_common_t   *hdr = NULL;
	gf_fop_read_rsp_t *rsp = NULL;
	size_t             hdrlen = 0;
	int32_t            gf_errno = 0;
	server_state_t    *state = NULL;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret >= 0) {
		gf_stat_from_stat (&rsp->stat, stbuf);
	} else {
		state = CALL_STATE(frame);

		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": READV %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no, 
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
	}

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_READ,
			       hdr, hdrlen, vector, count, iobref);

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
int
server_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd)
{
	server_connection_t  *conn = NULL;
	gf_hdr_common_t      *hdr = NULL;
	gf_fop_open_rsp_t    *rsp = NULL;
	server_state_t       *state = NULL;
	size_t                hdrlen = 0;
	int32_t               gf_errno = 0;
        uint64_t              fd_no = 0;

	conn = SERVER_CONNECTION (frame);

	state = CALL_STATE (frame);

	if (op_ret >= 0) {
		fd_bind (fd);

		fd_no = gf_fd_unused_get (conn->fdtable, fd);
                fd_ref (fd);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": OPEN %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path, 
			state->loc.inode ? state->loc.inode->ino : 0,
			op_ret, strerror (op_errno));
	}

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);
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
int
server_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   fd_t *fd, inode_t *inode, struct stat *stbuf,
                   struct stat *preparent, struct stat *postparent)
{
	server_connection_t *conn = NULL;
	gf_hdr_common_t     *hdr = NULL;
	gf_fop_create_rsp_t *rsp = NULL;
	server_state_t      *state = NULL;
	size_t               hdrlen = 0;
	int32_t              gf_errno = 0;
        uint64_t             fd_no = 0;
        inode_t             *link_inode = NULL;

	conn = SERVER_CONNECTION (frame);

	state = CALL_STATE (frame);

	if (op_ret >= 0) {
		gf_log (state->bound_xl->name, GF_LOG_TRACE,
			"%"PRId64": CREATE %"PRId64"/%s (%"PRId64")",
			frame->root->unique, state->loc.parent->ino,
			state->loc.name, stbuf->st_ino);

		link_inode = inode_link (inode, state->loc.parent,
                                         state->loc.name, stbuf);
		inode_lookup (link_inode);
                inode_unref (link_inode);

		fd_bind (fd);

		fd_no = gf_fd_unused_get (conn->fdtable, fd);
                fd_ref (fd);

		if ((fd_no < 0) || (fd == 0)) {
			op_ret = fd_no;
			op_errno = errno;
		}
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": CREATE %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path,
			state->loc.inode ? state->loc.inode->ino : 0,
			op_ret, strerror (op_errno));
	}

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);
	rsp->fd           = hton64 (fd_no);

	if (op_ret >= 0) {
		gf_stat_from_stat (&rsp->stat, stbuf);
		gf_stat_from_stat (&rsp->preparent, preparent);
		gf_stat_from_stat (&rsp->postparent, postparent);
        }

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
int
server_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, const char *buf,
                     struct stat *sbuf)
{
	gf_hdr_common_t       *hdr = NULL;
	gf_fop_readlink_rsp_t *rsp = NULL;
	server_state_t        *state = NULL;
	size_t                 hdrlen = 0;
	size_t                 linklen = 0;
	int32_t                gf_errno = 0;

	state  = CALL_STATE(frame);

	if (op_ret >= 0) {
		linklen = strlen (buf) + 1;
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": READLINK %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path, 
			state->loc.inode ? state->loc.inode->ino : 0,
			op_ret, strerror (op_errno));
	}

	hdrlen = gf_hdr_len (rsp, linklen);
	hdr    = gf_hdr_new (rsp, linklen);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret >= 0) {
                gf_stat_from_stat (&(rsp->buf), sbuf);
		strcpy (rsp->path, buf);
        }

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
int
server_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct stat *stbuf)
{
	gf_hdr_common_t   *hdr = NULL;
	gf_fop_stat_rsp_t *rsp = NULL;
	server_state_t    *state = NULL;
	size_t             hdrlen = 0;
	int32_t            gf_errno = 0;

	state  = CALL_STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret == 0) {
		gf_stat_from_stat (&rsp->stat, stbuf);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": STAT %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path, 
			state->loc.inode ? state->loc.inode->ino : 0,
			op_ret, strerror (op_errno));
	}

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_STAT,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}

/*
 * server_setattr_cbk - setattr callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 * not for external reference
 */

int
server_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct stat *statpre, struct stat *statpost)
{
	gf_hdr_common_t       *hdr = NULL;
	gf_fop_setattr_rsp_t  *rsp = NULL;
	server_state_t        *state = NULL;
	size_t                 hdrlen = 0;
	int32_t                gf_errno = 0;

	state  = CALL_STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret == 0) {
		gf_stat_from_stat (&rsp->statpre, statpre);
		gf_stat_from_stat (&rsp->statpost, statpost);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": SETATTR %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path,
			state->loc.inode ? state->loc.inode->ino : 0,
			op_ret, strerror (op_errno));
	}

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_SETATTR,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}

/*
 * server_setattr_cbk - setattr callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @stbuf:
 *
 * not for external reference
 */
int
server_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct stat *statpre, struct stat *statpost)
{
	gf_hdr_common_t       *hdr = NULL;
	gf_fop_fsetattr_rsp_t *rsp = NULL;
	server_state_t        *state = NULL;
	size_t                 hdrlen = 0;
	int32_t                gf_errno = 0;

	state  = CALL_STATE (frame);

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno_to_error (op_errno));

	if (op_ret == 0) {
		gf_stat_from_stat (&rsp->statpre, statpre);
		gf_stat_from_stat (&rsp->statpost, statpost);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": FSETATTR %"PRId64" (%"PRId64") ==> "
                        "%"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no,
			state->fd ? state->fd->inode->ino : 0,
			op_ret, strerror (op_errno));
	}

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FSETATTR,
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
int
server_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   inode_t *inode, struct stat *stbuf, dict_t *dict,
                   struct stat *postparent)
{
	gf_hdr_common_t     *hdr = NULL;
	gf_fop_lookup_rsp_t *rsp = NULL;
	server_state_t      *state = NULL;
	inode_t             *root_inode = NULL;
	int32_t              dict_len = 0;
	size_t               hdrlen = 0;
	int32_t              gf_errno = 0;
	int32_t              ret = -1;
        inode_t             *link_inode = NULL;

	state = CALL_STATE(frame);

	if (dict) {
		dict_len = dict_serialized_length (dict);
		if (dict_len < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"%s (%"PRId64"): failed to get serialized "
				"length of reply dict",
				state->loc.path, state->loc.inode->ino);
			op_ret   = -1;
			op_errno = EINVAL;
			dict_len = 0;
		}
	}

	hdrlen = gf_hdr_len (rsp, dict_len);
	hdr    = gf_hdr_new (rsp, dict_len);
	rsp    = gf_param (hdr);

	if ((op_ret >= 0) && dict) {
		ret = dict_serialize (dict, rsp->dict);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"%s (%"PRId64"): failed to serialize reply dict",
				state->loc.path, state->loc.inode->ino);
			op_ret = -1;
			op_errno = -ret;
			dict_len = 0;
		}
	}
	rsp->dict_len = hton32 (dict_len);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

        if (postparent)
                gf_stat_from_stat (&rsp->postparent, postparent);

	if (op_ret == 0) {
		root_inode = BOUND_XL(frame)->itable->root;
		if (inode == root_inode) {
			/* we just looked up root ("/") */
			stbuf->st_ino = 1;
			if (inode->st_mode == 0)
				inode->st_mode = stbuf->st_mode;
		}

		gf_stat_from_stat (&rsp->stat, stbuf);

		if (inode->ino == 0) {
			link_inode = inode_link (inode, state->loc.parent,
                                                 state->loc.name, stbuf);
			inode_lookup (link_inode);
                        inode_unref (link_inode);
		}
	} else {
		gf_log (this->name,
                        (op_errno == ENOENT ? GF_LOG_TRACE : GF_LOG_DEBUG),
			"%"PRId64": LOOKUP %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path,
			state->loc.inode ? state->loc.inode->ino : 0,
			op_ret, strerror (op_errno));
	}

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_LOOKUP,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}


int
server_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, dict_t *dict)
{
	gf_hdr_common_t       *hdr = NULL;
	gf_fop_xattrop_rsp_t  *rsp = NULL;
	server_state_t        *state = NULL;
	size_t                 hdrlen = 0;
	int32_t                len = 0;
	int32_t                gf_errno = 0;
	int32_t                ret = -1;

	state = CALL_STATE (frame);
	
	if (op_ret < 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": XATTROP %s (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->loc.path, 
			state->loc.inode ? state->loc.inode->ino : 0,
			op_ret, strerror (op_errno));
	}

	if ((op_ret >= 0) && dict) {
		len = dict_serialized_length (dict);
		if (len < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"%s (%"PRId64"): failed to get serialized length"
				" for reply dict", 
				state->loc.path, state->loc.inode->ino);
			op_ret = -1;
			op_errno = EINVAL;
			len = 0;
		}
	} 

	hdrlen = gf_hdr_len (rsp, len + 1);
	hdr    = gf_hdr_new (rsp, len + 1);
	rsp    = gf_param (hdr);

	if ((op_ret >= 0) && dict) {
		ret = dict_serialize (dict, rsp->dict);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"%s (%"PRId64"): failed to serialize reply dict", 
				state->loc.path, state->loc.inode->ino);
			op_ret = -1;
			op_errno = -ret;
			len = 0;
		}
	}
	rsp->dict_len = hton32 (len);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);
	
	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_XATTROP,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}


int
server_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno, dict_t *dict)
{
	gf_hdr_common_t      *hdr = NULL;
	gf_fop_xattrop_rsp_t *rsp = NULL;
	size_t                hdrlen = 0;
	int32_t               len = 0;
	int32_t               gf_errno = 0;
	int32_t               ret = -1;
	server_state_t       *state = NULL;
	
	state = CALL_STATE(frame);
	
	if (op_ret < 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"%"PRId64": FXATTROP %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no, 
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
	}

	if ((op_ret >= 0) && dict) {
		len = dict_serialized_length (dict);
		if (len < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"fd - %"PRId64" (%"PRId64"): failed to get "
				"serialized length for reply dict", 
				state->resolve.fd_no, state->fd->inode->ino);
			op_ret = -1;
			op_errno = EINVAL;
			len = 0;				
		}
	}

	hdrlen = gf_hdr_len (rsp, len + 1);
	hdr    = gf_hdr_new (rsp, len + 1);
	rsp    = gf_param (hdr);

	if ((op_ret >= 0) && dict) {
		ret = dict_serialize (dict, rsp->dict);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"fd - %"PRId64" (%"PRId64"): failed to "
				"serialize reply dict", 
				state->resolve.fd_no, state->fd->inode->ino);
			op_ret = -1;
			op_errno = -ret;
			len = 0;
		}
	}
	rsp->dict_len = hton32 (len);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);


	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_FXATTROP,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}


int
server_lookup_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        if (!state->loc.inode)
                state->loc.inode = inode_new (state->itable);

        STACK_WIND (frame, server_lookup_cbk,
                    bound_xl, bound_xl->fops->lookup,
                    &state->loc, state->dict);

        return 0;
err:
        server_lookup_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL, NULL, NULL);

        return 0;
}


int
server_lookup (call_frame_t *frame, xlator_t *bound_xl,
               gf_hdr_common_t *hdr, size_t hdrlen,
               struct iobuf *iobuf)
{
	gf_fop_lookup_req_t *req = NULL;
	server_state_t      *state = NULL;
	int32_t              ret = -1;
	size_t               pathlen = 0;
        size_t               baselen = 0;
	size_t               dictlen = 0;
	dict_t              *xattr_req = NULL;
	char                *req_dictbuf = NULL;

	req = gf_param (hdr);

	state = CALL_STATE (frame);

        pathlen = STRLEN_0 (req->path);
        dictlen = ntoh32 (req->dictlen);

        /* NOTE: lookup() uses req->ino only to identify if a lookup()
         *       is requested for 'root' or not
         */
        state->resolve.ino    = ntoh64 (req->ino);
        if (state->resolve.ino != 1)
                state->resolve.ino = 0;

        state->resolve.type   = RESOLVE_DONTCARE;
        state->resolve.par    = ntoh64 (req->par);
        state->resolve.gen    = ntoh64 (req->gen);
        state->resolve.path   = strdup (req->path);

        if (IS_NOT_ROOT (pathlen)) {
                state->resolve.bname = strdup (req->bname + pathlen);
                baselen = STRLEN_0 (state->resolve.bname);
        }

        if (dictlen) {
                /* Unserialize the dictionary */
                req_dictbuf = memdup (req->dict + pathlen + baselen, dictlen);

                xattr_req = dict_new ();

                ret = dict_unserialize (req_dictbuf, dictlen, &xattr_req);
                if (ret < 0) {
                        gf_log (bound_xl->name, GF_LOG_ERROR,
                                "%"PRId64": %s (%"PRId64"): failed to "
                                "unserialize req-buffer to dictionary",
                                frame->root->unique, state->resolve.path,
                                state->resolve.ino);
                        FREE (req_dictbuf);
                        goto err;
                }

                xattr_req->extra_free = req_dictbuf;
                state->dict = xattr_req;
	}

        resolve_and_resume (frame, server_lookup_resume);

	return 0;
err:
	if (xattr_req)
		dict_unref (xattr_req);

	server_lookup_cbk (frame, NULL, frame->this, -1, EINVAL, NULL, NULL,
                           NULL, NULL);
	return 0;
}


/*
 * server_forget - forget function for server protocol
 *
 * not for external reference
 */
int
server_forget (call_frame_t *frame, xlator_t *bound_xl,
               gf_hdr_common_t *hdr, size_t hdrlen,
               struct iobuf *iobuf)
{
        gf_log ("forget", GF_LOG_CRITICAL, "function not implemented");
	return 0;
}


int
server_stat_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_stat_cbk,
                    bound_xl, bound_xl->fops->stat, &state->loc);
	return 0;
err:
        server_stat_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                         state->resolve.op_errno, NULL);
        return 0;
}


int
server_stat (call_frame_t *frame, xlator_t *bound_xl,
             gf_hdr_common_t *hdr, size_t hdrlen,
             struct iobuf *iobuf)
{
	gf_fop_stat_req_t *req = NULL;
	server_state_t    *state = NULL;

	req = gf_param (hdr);
	state = CALL_STATE (frame);
        {
                state->resolve.type  = RESOLVE_MUST;
                state->resolve.ino   = ntoh64 (req->ino);
                state->resolve.gen   = ntoh64 (req->gen);
                state->resolve.path  = strdup (req->path);
        }

        resolve_and_resume (frame, server_stat_resume);

	return 0;
}


int
server_setattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_setattr_cbk,
                    bound_xl, bound_xl->fops->setattr,
		    &state->loc, &state->stbuf, state->valid);
	return 0;
err:
	server_setattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL, NULL);

        return 0;
}


int
server_setattr (call_frame_t *frame, xlator_t *bound_xl,
                gf_hdr_common_t *hdr, size_t hdrlen,
                struct iobuf *iobuf)
{
	gf_fop_setattr_req_t *req = NULL;
	server_state_t       *state = NULL;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type  = RESOLVE_MUST;
	state->resolve.ino   = ntoh64 (req->ino);
        state->resolve.gen   = ntoh64 (req->gen);
	state->resolve.path  = strdup (req->path);

        gf_stat_to_stat (&req->stbuf, &state->stbuf);
        state->valid = ntoh32 (req->valid);

        resolve_and_resume (frame, server_setattr_resume);

	return 0;
}


int
server_fsetattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_fsetattr_cbk,
                    bound_xl, bound_xl->fops->fsetattr,
		    state->fd, &state->stbuf, state->valid);
	return 0;
err:
	server_fsetattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL);

        return 0;
}


int
server_fsetattr (call_frame_t *frame, xlator_t *bound_xl,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
	gf_fop_fsetattr_req_t  *req = NULL;
	server_state_t         *state = NULL;


	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = ntoh64 (req->fd);

        gf_stat_to_stat (&req->stbuf, &state->stbuf);
        state->valid = ntoh32 (req->valid);

        resolve_and_resume (frame, server_fsetattr_resume);

	return 0;
}


int
server_readlink_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;
	
	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_readlink_cbk,
                    bound_xl, bound_xl->fops->readlink,
                    &state->loc, state->size);
	return 0;
err:
        server_readlink_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL);
        return 0;
}


int
server_readlink (call_frame_t *frame, xlator_t *bound_xl,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
	gf_fop_readlink_req_t *req = NULL;
	server_state_t        *state = NULL;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type = RESOLVE_MUST;
	state->resolve.ino  = ntoh64 (req->ino);
	state->resolve.gen  = ntoh64 (req->ino);
	state->resolve.path = strdup (req->path);

	state->size  = ntoh32 (req->size);

        resolve_and_resume (frame, server_readlink_resume);

	return 0;
}


int
server_create_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	state->loc.inode = inode_new (state->itable);

	state->fd = fd_create (state->loc.inode, frame->root->pid);
	state->fd->flags = state->flags;

	STACK_WIND (frame, server_create_cbk,
		    bound_xl, bound_xl->fops->create,
		    &(state->loc), state->flags, state->mode, state->fd);

	return 0;
err:
	server_create_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL, NULL,
                           NULL, NULL);
	return 0;
}


int
server_create (call_frame_t *frame, xlator_t *bound_xl,
               gf_hdr_common_t *hdr, size_t hdrlen,
               struct iobuf *iobuf)
{
	gf_fop_create_req_t *req = NULL;
	server_state_t      *state = NULL;
        int                  pathlen = 0;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        pathlen = STRLEN_0 (req->path);

        state->resolve.type   = RESOLVE_NOT;
        state->resolve.par    = ntoh64 (req->par);
        state->resolve.gen    = ntoh64 (req->gen);
        state->resolve.path   = strdup (req->path);
        state->resolve.bname  = strdup (req->bname + pathlen);
        state->mode           = ntoh32 (req->mode);
        state->flags          = gf_flags_to_flags (ntoh32 (req->flags));

        resolve_and_resume (frame, server_create_resume);

	return 0;
}


int
server_open_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t  *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	state->fd = fd_create (state->loc.inode, frame->root->pid);
        state->fd->flags = state->flags;

	STACK_WIND (frame, server_open_cbk,
                    bound_xl, bound_xl->fops->open,
		    &state->loc, state->flags, state->fd, 0);

	return 0;
err:
	server_open_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                         state->resolve.op_errno, NULL);
	return 0;
}


int
server_open (call_frame_t *frame, xlator_t *bound_xl,
             gf_hdr_common_t *hdr, size_t hdrlen,
             struct iobuf *iobuf)
{
	gf_fop_open_req_t  *req = NULL;
	server_state_t     *state = NULL;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.ino   = ntoh64 (req->ino);
        state->resolve.gen   = ntoh64 (req->gen);
        state->resolve.path  = strdup (req->path);

        state->flags = gf_flags_to_flags (ntoh32 (req->flags));

        resolve_and_resume (frame, server_open_resume);

	return 0;
}


int
server_readv_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_readv_cbk,
                    bound_xl, bound_xl->fops->readv,
                    state->fd, state->size, state->offset);

        return 0;
err:
        server_readv_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, 0, NULL, NULL);
        return 0;
}


int
server_readv (call_frame_t *frame, xlator_t *bound_xl,
              gf_hdr_common_t *hdr, size_t hdrlen,
              struct iobuf *iobuf)
{
	gf_fop_read_req_t   *req = NULL;
	server_state_t      *state = NULL;
	
	req = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = ntoh64 (req->fd);
        state->size           = ntoh32 (req->size);
        state->offset         = ntoh64 (req->offset);

        resolve_and_resume (frame, server_readv_resume);

	return 0;
}


int
server_writev_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t   *state = NULL;
        struct iovec      iov = {0, };

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	iov.iov_len  = state->size;

        if (state->iobuf) {
                iov.iov_base = state->iobuf->ptr;
        }

	STACK_WIND (frame, server_writev_cbk,
                    bound_xl, bound_xl->fops->writev,
		    state->fd, &iov, 1, state->offset, state->iobref);

        return 0;
err:
	server_writev_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL);
        return 0;
}


int
server_writev (call_frame_t *frame, xlator_t *bound_xl,
               gf_hdr_common_t *hdr, size_t hdrlen,
               struct iobuf *iobuf)
{
	gf_fop_write_req_t  *req = NULL;
	server_state_t      *state = NULL;
        struct iobref       *iobref = NULL;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.fd_no = ntoh64 (req->fd);
        state->offset        = ntoh64 (req->offset);
        state->size          = ntoh32 (req->size);

        if (iobuf) {
                iobref = iobref_new ();
                iobref_add (iobref, state->iobuf);

                state->iobuf = iobuf;
                state->iobref = iobref;
        }

        resolve_and_resume (frame, server_writev_resume);

	return 0;
}


int
server_release (call_frame_t *frame, xlator_t *bound_xl,
		gf_hdr_common_t *hdr, size_t hdrlen,
                struct iobuf *iobuf)
{
	gf_cbk_release_req_t  *req = NULL;
	server_state_t        *state = NULL;
	server_connection_t   *conn = NULL;

	conn  = SERVER_CONNECTION (frame);
	state = CALL_STATE (frame);
	req   = gf_param (hdr);

	state->resolve.fd_no = ntoh64 (req->fd);

	gf_fd_put (conn->fdtable, state->resolve.fd_no);

	server_release_cbk (frame, NULL, frame->this, 0, 0);

	return 0;
}


int
server_fsync_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_fsync_cbk,
                    bound_xl, bound_xl->fops->fsync,
		    state->fd, state->flags);
	return 0;
err:
	server_fsync_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL);

	return 0;
}


int
server_fsync (call_frame_t *frame, xlator_t *bound_xl,
              gf_hdr_common_t *hdr, size_t hdrlen,
              struct iobuf *iobuf)
{
	gf_fop_fsync_req_t  *req = NULL;
	server_state_t      *state = NULL;
	
	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.fd_no = ntoh64 (req->fd);
        state->flags         = ntoh32 (req->data);

        resolve_and_resume (frame, server_fsync_resume);

        return 0;
}



int
server_flush_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_flush_cbk,
                    bound_xl, bound_xl->fops->flush, state->fd);
	return 0;
err:
	server_flush_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno);

	return 0;
}


int
server_flush (call_frame_t *frame, xlator_t *bound_xl,
              gf_hdr_common_t *hdr, size_t hdrlen,
              struct iobuf *iobuf)
{
	gf_fop_fsync_req_t  *req = NULL;
	server_state_t      *state = NULL;
	
	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.fd_no = ntoh64 (req->fd);

        resolve_and_resume (frame, server_flush_resume);

        return 0;
}



int
server_ftruncate_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_ftruncate_cbk,
                    bound_xl, bound_xl->fops->ftruncate,
                    state->fd, state->offset);
	return 0;
err:
	server_ftruncate_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                              state->resolve.op_errno, NULL, NULL);

	return 0;
}


int
server_ftruncate (call_frame_t *frame, xlator_t *bound_xl,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
	gf_fop_ftruncate_req_t  *req = NULL;
	server_state_t          *state = NULL;

	req = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = ntoh64 (req->fd);
        state->offset         = ntoh64 (req->offset);

	resolve_and_resume (frame, server_ftruncate_resume);

	return 0;
}


int
server_fstat_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t     *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_fstat_cbk,
		    bound_xl, bound_xl->fops->fstat,
		    state->fd);
        return 0;
err:
        server_fstat_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL);
        return 0;
}


int
server_fstat (call_frame_t *frame, xlator_t *bound_xl,
              gf_hdr_common_t *hdr, size_t hdrlen,
              struct iobuf *iobuf)
{
	gf_fop_fstat_req_t  *req = NULL;
	server_state_t      *state = NULL;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type    = RESOLVE_MUST;
        state->resolve.fd_no   = ntoh64 (req->fd);

        resolve_and_resume (frame, server_fstat_resume);

	return 0;
}


int
server_truncate_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;
	
	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_truncate_cbk,
                    bound_xl, bound_xl->fops->truncate,
		    &state->loc, state->offset);
        return 0;
err:
        server_truncate_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL);
	return 0;
}



int
server_truncate (call_frame_t *frame, xlator_t *bound_xl,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
	gf_fop_truncate_req_t *req = NULL;
	server_state_t        *state = NULL;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.path  = strdup (req->path);
        state->resolve.ino   = ntoh64 (req->ino);
        state->resolve.gen   = ntoh64 (req->gen);
        state->offset        = ntoh64 (req->offset);

        resolve_and_resume (frame, server_truncate_resume);

	return 0;
}


int
server_unlink_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_unlink_cbk,
                    bound_xl, bound_xl->fops->unlink,
		    &state->loc);
	return 0;
err:
        server_unlink_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL);
        return 0;
}


int
server_unlink (call_frame_t *frame, xlator_t *bound_xl,
               gf_hdr_common_t *hdr, size_t hdrlen,
               struct iobuf *iobuf)
{
	gf_fop_unlink_req_t *req = NULL;
	server_state_t      *state = NULL;
        int                  pathlen = 0;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);

	pathlen = STRLEN_0 (req->path);

        state->resolve.type   = RESOLVE_MUST;
	state->resolve.par    = ntoh64 (req->par);
	state->resolve.gen    = ntoh64 (req->gen);
	state->resolve.path   = strdup (req->path);
        state->resolve.bname  = strdup (req->bname + pathlen);

        resolve_and_resume (frame, server_unlink_resume);

	return 0;
}


int
server_setxattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_setxattr_cbk,
                    bound_xl, bound_xl->fops->setxattr,
		    &state->loc, state->dict, state->flags);
	return 0;
err:
        server_setxattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno);

        return 0;
}


int
server_setxattr (call_frame_t *frame, xlator_t *bound_xl,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
	gf_fop_setxattr_req_t *req = NULL;
	server_state_t        *state = NULL;
	dict_t                *dict = NULL;
	int32_t                ret = -1;
	size_t                 dict_len = 0;
	char                  *req_dictbuf = NULL;

	req = gf_param (hdr);
	state = CALL_STATE (frame);

        dict_len = ntoh32 (req->dict_len);

        state->resolve.type     = RESOLVE_MUST;
        state->resolve.path     = strdup (req->path + dict_len);
        state->resolve.ino      = ntoh64 (req->ino);
        state->resolve.gen      = ntoh64 (req->gen);
        state->flags            = ntoh32 (req->flags);

        if (dict_len) {
                req_dictbuf = memdup (req->dict, dict_len);

                dict = dict_new ();

                ret = dict_unserialize (req_dictbuf, dict_len, &dict);
                if (ret < 0) {
                        gf_log (bound_xl->name, GF_LOG_ERROR,
                                "%"PRId64": %s (%"PRId64"): failed to "
                                "unserialize request buffer to dictionary",
                                frame->root->unique, state->loc.path,
                                state->resolve.ino);
                        FREE (req_dictbuf);
                        goto err;
                }

                dict->extra_free = req_dictbuf;
                state->dict = dict;
        }

        resolve_and_resume (frame, server_setxattr_resume);

	return 0;
err:
	if (dict)
		dict_unref (dict);

	server_setxattr_cbk (frame, NULL, frame->this, -1, EINVAL);

	return 0;

}


int
server_fsetxattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_setxattr_cbk,
                    bound_xl, bound_xl->fops->fsetxattr,
		    state->fd, state->dict, state->flags);
	return 0;
err:
        server_fsetxattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                              state->resolve.op_errno);

        return 0;
}


int
server_fsetxattr (call_frame_t *frame, xlator_t *bound_xl,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
	gf_fop_fsetxattr_req_t *req = NULL;
	server_state_t         *state = NULL;
	dict_t                 *dict = NULL;
	int32_t                 ret = -1;
	size_t                  dict_len = 0;
	char                   *req_dictbuf = NULL;

	req = gf_param (hdr);
	state = CALL_STATE (frame);

        dict_len = ntoh32 (req->dict_len);

        state->resolve.type      = RESOLVE_MUST;
        state->resolve.fd_no     = ntoh64 (req->fd);
        state->flags             = ntoh32 (req->flags);

        if (dict_len) {
                req_dictbuf = memdup (req->dict, dict_len);

                dict = dict_new ();

                ret = dict_unserialize (req_dictbuf, dict_len, &dict);
                if (ret < 0) {
                        gf_log (bound_xl->name, GF_LOG_ERROR,
                                "%"PRId64": %s (%"PRId64"): failed to "
                                "unserialize request buffer to dictionary",
                                frame->root->unique, state->loc.path,
                                state->resolve.ino);
                        FREE (req_dictbuf);
                        goto err;
                }

                dict->extra_free = req_dictbuf;
                state->dict = dict;
        }

        resolve_and_resume (frame, server_fsetxattr_resume);

	return 0;
err:
	if (dict)
		dict_unref (dict);

	server_setxattr_cbk (frame, NULL, frame->this, -1, EINVAL);

	return 0;
}


int
server_fxattrop_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_fxattrop_cbk,
                    bound_xl, bound_xl->fops->fxattrop,
		    state->fd, state->flags, state->dict);
	return 0;
err:
        server_fxattrop_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL);
        return 0;
}


int
server_fxattrop (call_frame_t *frame, xlator_t *bound_xl,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
	gf_fop_fxattrop_req_t *req = NULL;
	dict_t                *dict = NULL;
	server_state_t        *state = NULL;
	size_t                 dict_len = 0;
	char                  *req_dictbuf = NULL;
	int32_t                ret = -1;

	req   = gf_param (hdr);
	state = CALL_STATE(frame);

        dict_len = ntoh32 (req->dict_len);

        state->resolve.type    = RESOLVE_MUST;
        state->resolve.fd_no   = ntoh64 (req->fd);

        state->resolve.ino     = ntoh64 (req->ino);
        state->resolve.gen     = ntoh64 (req->gen);
        state->flags           = ntoh32 (req->flags);

	if (dict_len) {
		/* Unserialize the dictionary */
		req_dictbuf = memdup (req->dict, dict_len);

		dict = dict_new ();

		ret = dict_unserialize (req_dictbuf, dict_len, &dict);
		if (ret < 0) {
			gf_log (bound_xl->name, GF_LOG_ERROR,
				"fd - %"PRId64" (%"PRId64"): failed to unserialize "
				"request buffer to dictionary",
				state->resolve.fd_no, state->fd->inode->ino);
			free (req_dictbuf);
			goto fail;
		}
                dict->extra_free = req_dictbuf;
                state->dict = dict;
	}

        resolve_and_resume (frame, server_fxattrop_resume);

	return 0;

fail:
	if (dict)
		dict_unref (dict);

	server_fxattrop_cbk (frame, NULL, frame->this, -1, EINVAL, NULL);
	return 0;
}


int
server_xattrop_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;
	
	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_xattrop_cbk,
                    bound_xl, bound_xl->fops->xattrop,
		    &state->loc, state->flags, state->dict);
	return 0;
err:
        server_xattrop_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL);
        return 0;
}


int
server_xattrop (call_frame_t *frame, xlator_t *bound_xl,
		gf_hdr_common_t *hdr, size_t hdrlen,
                struct iobuf *iobuf)
{
	gf_fop_xattrop_req_t  *req = NULL;
	dict_t                *dict = NULL;
	server_state_t        *state = NULL;
	size_t                 dict_len = 0;
	char                  *req_dictbuf = NULL;
	int32_t                ret = -1;

	req   = gf_param (hdr);
	state = CALL_STATE(frame);

        dict_len = ntoh32 (req->dict_len);

        state->resolve.type    = RESOLVE_MUST;
        state->resolve.path    = strdup (req->path + dict_len);
        state->resolve.ino     = ntoh64 (req->ino);
        state->resolve.gen     = ntoh64 (req->gen);
        state->flags           = ntoh32 (req->flags);

	if (dict_len) {
		/* Unserialize the dictionary */
		req_dictbuf = memdup (req->dict, dict_len);

		dict = dict_new ();

		ret = dict_unserialize (req_dictbuf, dict_len, &dict);
		if (ret < 0) {
			gf_log (bound_xl->name, GF_LOG_ERROR,
				"fd - %"PRId64" (%"PRId64"): failed to unserialize "
				"request buffer to dictionary",
				state->resolve.fd_no, state->fd->inode->ino);
			free (req_dictbuf);
			goto fail;
		}
                dict->extra_free = req_dictbuf;
                state->dict = dict_ref (dict);
	}

        resolve_and_resume (frame, server_xattrop_resume);

	return 0;

fail:
	if (dict)
		dict_unref (dict);

	server_xattrop_cbk (frame, NULL, frame->this, -1, EINVAL, NULL);
	return 0;
}


int
server_getxattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_getxattr_cbk,
                    bound_xl, bound_xl->fops->getxattr,
		    &state->loc, state->name);
	return 0;
err:
        server_getxattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL);
        return 0;
}


int
server_getxattr (call_frame_t *frame, xlator_t *bound_xl,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
	gf_fop_getxattr_req_t  *req = NULL;
	server_state_t         *state = NULL;
	size_t                  namelen = 0;
	size_t                  pathlen = 0;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        pathlen = STRLEN_0 (req->path);

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.path  = strdup (req->path);
        state->resolve.ino   = ntoh64 (req->ino);
        state->resolve.gen   = ntoh64 (req->gen);

        namelen = ntoh32 (req->namelen);
        if (namelen)
                state->name = strdup (req->name + pathlen);

        resolve_and_resume (frame, server_getxattr_resume);

	return 0;
}


int
server_fgetxattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_fgetxattr_cbk,
                    bound_xl, bound_xl->fops->fgetxattr,
		    state->fd, state->name);
	return 0;
err:
        server_fgetxattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                              state->resolve.op_errno, NULL);
        return 0;
}


int
server_fgetxattr (call_frame_t *frame, xlator_t *bound_xl,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
	gf_fop_fgetxattr_req_t *req = NULL;
	server_state_t         *state = NULL;
	size_t                  namelen = 0;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.fd_no = ntoh64 (req->fd);

        namelen = ntoh32 (req->namelen);
        if (namelen)
                state->name = strdup (req->name);

        resolve_and_resume (frame, server_fgetxattr_resume);

	return 0;
}


int
server_removexattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_removexattr_cbk,
		    bound_xl, bound_xl->fops->removexattr,
		    &state->loc, state->name);
	return 0;
err:
        server_removexattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                                state->resolve.op_errno);
        return 0;
}


int
server_removexattr (call_frame_t *frame, xlator_t *bound_xl,
                    gf_hdr_common_t *hdr, size_t hdrlen,
                    struct iobuf *iobuf)
{
	gf_fop_removexattr_req_t  *req = NULL;
	server_state_t            *state = NULL;
	size_t                     pathlen = 0;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);
        pathlen = STRLEN_0 (req->path);

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.path   = strdup (req->path);
        state->resolve.ino    = ntoh64 (req->ino);
        state->resolve.gen    = ntoh64 (req->gen);
        state->name           = strdup (req->name + pathlen);

        resolve_and_resume (frame, server_removexattr_resume);

	return 0;
}


int
server_statfs_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t      *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret !=0)
                goto err;

        STACK_WIND (frame, server_statfs_cbk,
                    bound_xl, bound_xl->fops->statfs,
                    &state->loc);
        return 0;

err:
        server_statfs_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL);
        return 0;
}


int
server_statfs (call_frame_t *frame, xlator_t *bound_xl,
               gf_hdr_common_t *hdr, size_t hdrlen,
               struct iobuf *iobuf)
{
	gf_fop_statfs_req_t *req = NULL;
	server_state_t      *state = NULL;

	req = gf_param (hdr);

	state = CALL_STATE (frame);

        state->resolve.type   = RESOLVE_MUST;
	state->resolve.ino    = ntoh64 (req->ino);
        if (!state->resolve.ino)
                state->resolve.ino = 1;
	state->resolve.gen    = ntoh64 (req->gen);
	state->resolve.path   = strdup (req->path);

        resolve_and_resume (frame, server_statfs_resume);

	return 0;
}


int
server_opendir_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t  *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	state->fd = fd_create (state->loc.inode, frame->root->pid);

	STACK_WIND (frame, server_opendir_cbk,
                    bound_xl, bound_xl->fops->opendir,
                    &state->loc, state->fd);
	return 0;
err:
        server_opendir_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL);
        return 0;
}


int
server_opendir (call_frame_t *frame, xlator_t *bound_xl,
                gf_hdr_common_t *hdr, size_t hdrlen,
                struct iobuf *iobuf)
{
	gf_fop_opendir_req_t  *req = NULL;
	server_state_t        *state = NULL;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.path   = strdup (req->path);
        state->resolve.ino    = ntoh64 (req->ino);
        state->resolve.gen    = ntoh64 (req->gen);

        resolve_and_resume (frame, server_opendir_resume);

	return 0;
}


int
server_releasedir (call_frame_t *frame, xlator_t *bound_xl,
		   gf_hdr_common_t *hdr, size_t hdrlen,
                   struct iobuf *iobuf)
{
	gf_cbk_releasedir_req_t *req = NULL;
	server_connection_t     *conn = NULL;
        uint64_t                 fd_no = 0;
	
	conn = SERVER_CONNECTION (frame);

	req = gf_param (hdr);

	fd_no = ntoh64 (req->fd);

	gf_fd_put (conn->fdtable, fd_no);

	server_releasedir_cbk (frame, NULL, frame->this, 0, 0);

	return 0;
}


int
server_getdents (call_frame_t *frame, xlator_t *bound_xl,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
	gf_fop_getdents_req_t  *req = NULL;
	server_state_t         *state = NULL;
	server_connection_t    *conn = NULL;
	
	conn = SERVER_CONNECTION (frame);

	req   = gf_param (hdr);
	state = CALL_STATE(frame);
	{
		state->resolve.fd_no = ntoh64 (req->fd);
		if (state->resolve.fd_no >= 0)
			state->fd = gf_fd_fdptr_get (conn->fdtable, 
						     state->resolve.fd_no);

		state->size = ntoh32 (req->size);
		state->offset = ntoh64 (req->offset);
		state->flags = ntoh32 (req->flags);
	}


	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"fd - %"PRId64": unresolved fd", 
			state->resolve.fd_no);

		server_getdents_cbk (frame, NULL, frame->this, -1, EBADF,
                                     NULL, 0);
		goto out;
	}

	gf_log (bound_xl->name, GF_LOG_TRACE,
		"%"PRId64": GETDENTS \'fd=%"PRId64" (%"PRId64"); "
		"offset=%"PRId64"; size=%"PRId64, 
		frame->root->unique, state->resolve.fd_no,
                state->fd->inode->ino,
		state->offset, (int64_t)state->size);

	STACK_WIND (frame, server_getdents_cbk,
		    bound_xl,
		    bound_xl->fops->getdents,
		    state->fd, state->size, state->offset, state->flags);
out:
	return 0;
}

/*
 * server_readdirp_cbk - getdents callback for server protocol
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 * not for external reference
 */
int
server_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
	gf_hdr_common_t         *hdr = NULL;
	gf_fop_readdirp_rsp_t   *rsp = NULL;
	size_t                  hdrlen = 0;
	size_t                  buf_size = 0;
	int32_t                 gf_errno = 0;
	server_state_t          *state = NULL;

	if (op_ret > 0)
		buf_size = gf_dirent_serialize (entries, NULL, 0);

	hdrlen = gf_hdr_len (rsp, buf_size);
	hdr    = gf_hdr_new (rsp, buf_size);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret > 0) {
		rsp->size = hton32 (buf_size);
		gf_dirent_serialize (entries, rsp->buf, buf_size);
	} else {
		state = CALL_STATE(frame);

		gf_log (this->name, GF_LOG_TRACE,
			"%"PRId64": READDIRP %"PRId64" (%"PRId64") ==>"
                        "%"PRId32" (%s)",
			frame->root->unique, state->resolve.fd_no,
			state->fd ? state->fd->inode->ino : 0, op_ret,
			strerror (op_errno));
	}

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_READDIRP,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}



/*
 * server_readdirp - readdirp function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 * not for external reference
 */
int
server_readdirp (call_frame_t *frame, xlator_t *bound_xl, gf_hdr_common_t *hdr,
                 size_t hdrlen, struct iobuf *iobuf)
{
	gf_fop_readdirp_req_t *req = NULL;
	server_state_t *state = NULL;
	server_connection_t *conn = NULL;

	conn = SERVER_CONNECTION(frame);

	req   = gf_param (hdr);
	state = CALL_STATE(frame);
	{
		state->resolve.fd_no = ntoh64 (req->fd);
		if (state->resolve.fd_no >= 0)
			state->fd = gf_fd_fdptr_get (conn->fdtable,
						     state->resolve.fd_no);

		state->size   = ntoh32 (req->size);
		state->offset = ntoh64 (req->offset);
	}


	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"fd - %"PRId64": unresolved fd",
			state->resolve.fd_no);

		server_readdirp_cbk (frame, NULL, frame->this, -1, EBADF, NULL);
		goto out;
	}

	gf_log (bound_xl->name, GF_LOG_TRACE,
		"%"PRId64": READDIRP \'fd=%"PRId64" (%"PRId64"); "
		"offset=%"PRId64"; size=%"PRId64,
		frame->root->unique, state->resolve.fd_no,
                state->fd->inode->ino,
		state->offset, (int64_t)state->size);

	STACK_WIND (frame, server_readdirp_cbk, bound_xl,
                    bound_xl->fops->readdirp, state->fd, state->size,
                    state->offset);
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
int
server_readdir (call_frame_t *frame, xlator_t *bound_xl,
                gf_hdr_common_t *hdr, size_t hdrlen,
                struct iobuf *iobuf)
{
	gf_fop_readdir_req_t *req = NULL;
	server_state_t *state = NULL;
	server_connection_t *conn = NULL;
	
	conn = SERVER_CONNECTION(frame);

	req   = gf_param (hdr);
	state = CALL_STATE(frame);
	{
		state->resolve.fd_no = ntoh64 (req->fd);
		if (state->resolve.fd_no >= 0)
			state->fd = gf_fd_fdptr_get (conn->fdtable, 
						     state->resolve.fd_no);

		state->size   = ntoh32 (req->size);
		state->offset = ntoh64 (req->offset);
	}


	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"fd - %"PRId64": unresolved fd", 
			state->resolve.fd_no);

		server_readdir_cbk (frame, NULL, frame->this, -1, EBADF, NULL);
		goto out;
	}

	gf_log (bound_xl->name, GF_LOG_TRACE,
		"%"PRId64": READDIR \'fd=%"PRId64" (%"PRId64"); "
		"offset=%"PRId64"; size=%"PRId64,
		frame->root->unique, state->resolve.fd_no,
                state->fd->inode->ino, 
		state->offset, (int64_t)state->size);

	STACK_WIND (frame, server_readdir_cbk,
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
int
server_fsyncdir (call_frame_t *frame, xlator_t *bound_xl,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
	gf_fop_fsyncdir_req_t *req = NULL;
	server_state_t        *state = NULL;
	server_connection_t   *conn = NULL;
	
	conn = SERVER_CONNECTION (frame);

	req   = gf_param (hdr);
	state = CALL_STATE(frame);
	{
		state->resolve.fd_no = ntoh64 (req->fd);
		if (state->resolve.fd_no >= 0)
			state->fd = gf_fd_fdptr_get (conn->fdtable, 
						     state->resolve.fd_no);

		state->flags = ntoh32 (req->data);
	}

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"fd - %"PRId64": unresolved fd", 
			state->resolve.fd_no);

		server_fsyncdir_cbk (frame, NULL, frame->this, -1, EBADF);
		goto out;
	}

	gf_log (bound_xl->name, GF_LOG_TRACE,
		"%"PRId64": FSYNCDIR \'fd=%"PRId64" (%"PRId64")\'", 
		frame->root->unique, state->resolve.fd_no,
                state->fd->inode->ino);

	STACK_WIND (frame, server_fsyncdir_cbk,
		    bound_xl,
		    bound_xl->fops->fsyncdir,
		    state->fd, state->flags);
out:
	return 0;
}


int
server_mknod_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	state->loc.inode = inode_new (state->itable);

	STACK_WIND (frame, server_mknod_cbk,
                    bound_xl, bound_xl->fops->mknod,
		    &(state->loc), state->mode, state->dev);

	return 0;
err:
        server_mknod_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL, NULL, NULL);
        return 0;
}



int
server_mknod (call_frame_t *frame, xlator_t *bound_xl,
              gf_hdr_common_t *hdr, size_t hdrlen,
              struct iobuf *iobuf)
{
	gf_fop_mknod_req_t *req = NULL;
	server_state_t     *state = NULL;
	size_t              pathlen = 0;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);
        pathlen = STRLEN_0 (req->path);

        state->resolve.type    = RESOLVE_NOT;
        state->resolve.par     = ntoh64 (req->par);
        state->resolve.gen     = ntoh64 (req->gen);
        state->resolve.path    = strdup (req->path);
        state->resolve.bname   = strdup (req->bname + pathlen);

        state->mode = ntoh32 (req->mode);
        state->dev  = ntoh64 (req->dev);

        resolve_and_resume (frame, server_mknod_resume);

	return 0;
}


int
server_mkdir_resume (call_frame_t *frame, xlator_t *bound_xl)

{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	state->loc.inode = inode_new (state->itable);

	STACK_WIND (frame, server_mkdir_cbk,
                    bound_xl, bound_xl->fops->mkdir,
		    &(state->loc), state->mode);

	return 0;
err:
        server_mkdir_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


int
server_mkdir (call_frame_t *frame, xlator_t *bound_xl,
              gf_hdr_common_t *hdr, size_t hdrlen,
              struct iobuf *iobuf)
{
	gf_fop_mkdir_req_t *req = NULL;
	server_state_t     *state = NULL;
	size_t              pathlen = 0;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);
        pathlen = STRLEN_0 (req->path);

        state->resolve.type    = RESOLVE_NOT;
        state->resolve.par     = ntoh64 (req->par);
        state->resolve.gen     = ntoh64 (req->gen);
        state->resolve.path    = strdup (req->path);
        state->resolve.bname   = strdup (req->bname + pathlen);

        state->mode = ntoh32 (req->mode);

        resolve_and_resume (frame, server_mkdir_resume);

	return 0;
}


int
server_rmdir_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_rmdir_cbk,
                    bound_xl, bound_xl->fops->rmdir, &state->loc);
	return 0;
err:
        server_rmdir_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL);
        return 0;
}

int
server_rmdir (call_frame_t *frame, xlator_t *bound_xl,
              gf_hdr_common_t *hdr, size_t hdrlen,
              struct iobuf *iobuf)
{
	gf_fop_rmdir_req_t *req = NULL;
	server_state_t     *state = NULL;
        int                 pathlen = 0;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);
        pathlen = STRLEN_0 (req->path);

        state->resolve.type    = RESOLVE_MUST;
        state->resolve.par     = ntoh64 (req->par);
        state->resolve.gen     = ntoh64 (req->gen);
        state->resolve.path    = strdup (req->path);
        state->resolve.bname   = strdup (req->bname + pathlen);

        resolve_and_resume (frame, server_rmdir_resume);

	return 0;
}


int
server_inodelk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

 	STACK_WIND (frame, server_inodelk_cbk,
                    bound_xl, bound_xl->fops->inodelk,
 		    state->volume, &state->loc, state->cmd, &state->flock);
 	return 0;
err:
        server_inodelk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno);
        return 0;
}


int
server_inodelk (call_frame_t *frame, xlator_t *bound_xl,
		gf_hdr_common_t *hdr, size_t hdrlen,
                struct iobuf *iobuf)
{
 	gf_fop_inodelk_req_t *req = NULL;
 	server_state_t       *state = NULL;
	size_t                pathlen = 0;
        size_t                vollen  = 0;
        int                   cmd = 0;

 	req   = gf_param (hdr);
 	state = CALL_STATE (frame);
        pathlen = STRLEN_0 (req->path);
        vollen  = STRLEN_0 (req->volume + pathlen);

        state->resolve.type    = RESOLVE_EXACT;
        state->resolve.ino     = ntoh64 (req->ino);
        state->resolve.gen     = ntoh64 (req->gen);
        state->resolve.path    = strdup (req->path);

        cmd = ntoh32 (req->cmd);
        switch (cmd) {
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
        state->volume = strdup (req->volume + pathlen);

        gf_flock_to_flock (&req->flock, &state->flock);

        switch (state->type) {
        case GF_LK_F_RDLCK: 
                state->flock.l_type = F_RDLCK; 
                break;
        case GF_LK_F_WRLCK: 
                state->flock.l_type = F_WRLCK; 
                break;
        case GF_LK_F_UNLCK: 
                state->flock.l_type = F_UNLCK; 
                break;
	}

        resolve_and_resume (frame, server_inodelk_resume);

 	return 0;
}


int
server_finodelk (call_frame_t *frame, xlator_t *bound_xl,
		 gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
 	gf_fop_finodelk_req_t  *req = NULL;
 	server_state_t         *state = NULL;
	server_connection_t    *conn = NULL;

	conn = SERVER_CONNECTION(frame);

 	req   = gf_param (hdr);
 	state = CALL_STATE(frame);
	{
                state->volume = strdup (req->volume);

		state->resolve.fd_no = ntoh64 (req->fd);
		if (state->resolve.fd_no >= 0)
			state->fd = gf_fd_fdptr_get (conn->fdtable, 
						     state->resolve.fd_no);

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

		gf_flock_to_flock (&req->flock, &state->flock);

		switch (state->type) {
		case GF_LK_F_RDLCK: 
			state->flock.l_type = F_RDLCK; 
			break;
		case GF_LK_F_WRLCK: 
			state->flock.l_type = F_WRLCK; 
			break;
		case GF_LK_F_UNLCK: 
			state->flock.l_type = F_UNLCK; 
			break;
		}

	}

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"fd - %"PRId64": unresolved fd", 
			state->resolve.fd_no);
		
		server_finodelk_cbk (frame, NULL, frame->this,
				     -1, EBADF);
		return -1;
  	} 

	gf_log (BOUND_XL(frame)->name, GF_LOG_TRACE,
		"%"PRId64": FINODELK \'fd=%"PRId64" (%"PRId64")\'", 
		frame->root->unique, state->resolve.fd_no,
                state->fd->inode->ino);

	STACK_WIND (frame, server_finodelk_cbk,
		    BOUND_XL(frame), 
		    BOUND_XL(frame)->fops->finodelk,
		    state->volume, state->fd, state->cmd, &state->flock);
 	return 0;
}
  
 
int
server_entrylk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

 	STACK_WIND (frame, server_entrylk_cbk,
                    bound_xl, bound_xl->fops->entrylk,
 		    state->volume, &state->loc, state->name,
                    state->cmd, state->type);
 	return 0;
err:
        server_entrylk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno);
        return 0;
}


int
server_entrylk (call_frame_t *frame, xlator_t *bound_xl,
		gf_hdr_common_t *hdr, size_t hdrlen,
                struct iobuf *iobuf)
{
 	gf_fop_entrylk_req_t *req = NULL;
 	server_state_t       *state = NULL;
	size_t                pathlen = 0;
	size_t                namelen = 0;
        size_t                vollen  = 0;

 	req   = gf_param (hdr);
 	state = CALL_STATE (frame);
        pathlen = STRLEN_0 (req->path);
        namelen = ntoh64 (req->namelen);
        vollen = STRLEN_0(req->volume + pathlen + namelen);

        state->resolve.type   = RESOLVE_EXACT;
        state->resolve.path   = strdup (req->path);
        state->resolve.ino    = ntoh64 (req->ino);
        state->resolve.gen    = ntoh64 (req->gen);

        if (namelen)
                state->name   = strdup (req->name + pathlen);
        state->volume         = strdup (req->volume + pathlen + namelen);

        state->cmd            = ntoh32 (req->cmd);
        state->type           = ntoh32 (req->type);

        resolve_and_resume (frame, server_entrylk_resume);

 	return 0;
}


int
server_fentrylk (call_frame_t *frame, xlator_t *bound_xl,
		 gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
 	gf_fop_fentrylk_req_t *req = NULL;
 	server_state_t        *state = NULL;
	size_t                 namelen = 0;
        size_t                 vollen  = 0;
	server_connection_t   *conn = NULL;
	
	conn = SERVER_CONNECTION (frame);

 	req   = gf_param (hdr);
 	state = CALL_STATE(frame);
	{
		state->resolve.fd_no = ntoh64 (req->fd);
		if (state->resolve.fd_no >= 0)
			state->fd = gf_fd_fdptr_get (conn->fdtable, 
						     state->resolve.fd_no);

		state->cmd  = ntoh32 (req->cmd);
		state->type = ntoh32 (req->type);
		namelen = ntoh64 (req->namelen);
		
		if (namelen)
			state->name = req->name;
                
                vollen = STRLEN_0(req->volume + namelen);
                state->volume = strdup (req->volume + namelen);
	}

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"fd - %"PRId64": unresolved fd", 
			state->resolve.fd_no);
		
		server_fentrylk_cbk (frame, NULL, frame->this, -1, EBADF);
		return -1;
  	} 

	gf_log (BOUND_XL(frame)->name, GF_LOG_TRACE,
		"%"PRId64": FENTRYLK \'fd=%"PRId64" (%"PRId64")\'", 
		frame->root->unique, state->resolve.fd_no,
                state->fd->inode->ino);

	STACK_WIND (frame, server_fentrylk_cbk,
		    BOUND_XL(frame), 
		    BOUND_XL(frame)->fops->fentrylk,
		    state->volume, state->fd, state->name, 
                    state->cmd, state->type);
 	return 0;
}


int
server_access_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;
	
	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

	STACK_WIND (frame, server_access_cbk,
                    bound_xl, bound_xl->fops->access,
		    &state->loc, state->mask);
	return 0;
err:
        server_access_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno);
        return 0;
}


int
server_access (call_frame_t *frame, xlator_t *bound_xl,
               gf_hdr_common_t *hdr, size_t hdrlen,
               struct iobuf *iobuf)
{
	gf_fop_access_req_t *req = NULL;
	server_state_t      *state = NULL;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.ino   = hton64 (req->ino);
        state->resolve.gen   = hton64 (req->gen);
	state->resolve.path  = strdup (req->path);

	state->mask  = ntoh32 (req->mask);

        resolve_and_resume (frame, server_access_resume);

	return 0;
}


int
server_symlink_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->loc.inode = inode_new (state->itable);

	STACK_WIND (frame, server_symlink_cbk,
                    bound_xl, bound_xl->fops->symlink,
		    state->name, &state->loc);

	return 0;
err:
        server_symlink_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL, NULL, NULL, NULL);
        return 0;
}



int
server_symlink (call_frame_t *frame, xlator_t *bound_xl,
                gf_hdr_common_t *hdr, size_t hdrlen,
                struct iobuf *iobuf)
{
	server_state_t       *state = NULL;
	gf_fop_symlink_req_t *req = NULL;
	size_t                pathlen = 0;
	size_t                baselen = 0;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);
        pathlen = STRLEN_0 (req->path);
        baselen = STRLEN_0 (req->bname + pathlen);

        state->resolve.type   = RESOLVE_NOT;
        state->resolve.par    = ntoh64 (req->par);
        state->resolve.gen    = ntoh64 (req->gen);
        state->resolve.path   = strdup (req->path);
        state->resolve.bname  = strdup (req->bname + pathlen);
        state->name           = strdup (req->linkname + pathlen + baselen);

        resolve_and_resume (frame, server_symlink_resume);

	return 0;
}


int
server_link_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;
        int             op_ret = 0;
        int             op_errno = 0;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0) {
                op_ret   = state->resolve.op_ret;
                op_errno = state->resolve.op_errno;
                goto err;
        }

        if (state->resolve2.op_ret != 0) {
                op_ret   = state->resolve2.op_ret;
                op_errno = state->resolve2.op_errno;
                goto err;
        }

	state->loc2.inode = inode_ref (state->loc.inode);

	STACK_WIND (frame, server_link_cbk,
                    bound_xl, bound_xl->fops->link,
		    &state->loc, &state->loc2);
	return 0;
err:
        server_link_cbk (frame, NULL, frame->this, op_ret, op_errno,
                         NULL, NULL, NULL, NULL);
        return 0;
}


int
server_link (call_frame_t *frame, xlator_t *this,
             gf_hdr_common_t *hdr, size_t hdrlen,
             struct iobuf *iobuf)
{
	gf_fop_link_req_t *req = NULL;
	server_state_t    *state = NULL;
	size_t             oldpathlen = 0;
	size_t             newpathlen = 0;
	size_t             newbaselen = 0;

	req   = gf_param (hdr);
	state = CALL_STATE (frame);
        oldpathlen = STRLEN_0 (req->oldpath);
        newpathlen = STRLEN_0 (req->newpath  + oldpathlen);
        newbaselen = STRLEN_0 (req->newbname + oldpathlen + newpathlen);

        state->resolve.type    = RESOLVE_MUST;
        state->resolve.path    = strdup (req->oldpath);
        state->resolve.ino     = ntoh64 (req->oldino);
        state->resolve.gen     = ntoh64 (req->oldgen);

        state->resolve2.type   = RESOLVE_NOT;
        state->resolve2.path   = strdup (req->newpath + oldpathlen);
        state->resolve2.bname  = strdup (req->newbname + oldpathlen + newpathlen);
        state->resolve2.par    = ntoh64 (req->newpar);
        state->resolve2.gen    = ntoh64 (req->newgen);

        resolve_and_resume (frame, server_link_resume);

	return 0;
}


int
server_rename_resume (call_frame_t *frame, xlator_t *bound_xl)
{
	server_state_t *state = NULL;
        int             op_ret = 0;
        int             op_errno = 0;

	state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0) {
                op_ret   = state->resolve.op_ret;
                op_errno = state->resolve.op_errno;
                goto err;
        }

        if (state->resolve2.op_ret != 0) {
                op_ret   = state->resolve2.op_ret;
                op_errno = state->resolve2.op_errno;
                goto err;
        }

	STACK_WIND (frame, server_rename_cbk,
                    bound_xl, bound_xl->fops->rename,
		    &state->loc, &state->loc2);
	return 0;
err:
        server_rename_cbk (frame, NULL, frame->this, op_ret, op_errno,
                           NULL, NULL, NULL, NULL, NULL);
        return 0;
}


int
server_rename (call_frame_t *frame, xlator_t *bound_xl,
               gf_hdr_common_t *hdr, size_t hdrlen,
               struct iobuf *iobuf)
{
	gf_fop_rename_req_t *req = NULL;
	server_state_t      *state = NULL;
	size_t               oldpathlen = 0;
	size_t               oldbaselen = 0;
	size_t               newpathlen = 0;
	size_t               newbaselen = 0;

	req = gf_param (hdr);

	state = CALL_STATE (frame);
        oldpathlen = STRLEN_0 (req->oldpath);
        oldbaselen = STRLEN_0 (req->oldbname + oldpathlen);
        newpathlen = STRLEN_0 (req->newpath  + oldpathlen + oldbaselen);
        newbaselen = STRLEN_0 (req->newbname + oldpathlen +
                               oldbaselen + newpathlen);

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.path   = strdup (req->oldpath);
        state->resolve.bname  = strdup (req->oldbname + oldpathlen);
        state->resolve.par    = ntoh64 (req->oldpar);
        state->resolve.gen    = ntoh64 (req->oldgen);

        state->resolve2.type  = RESOLVE_MAY;
        state->resolve2.path  = strdup (req->newpath  + oldpathlen + oldbaselen);
        state->resolve2.bname = strdup (req->newbname + oldpathlen + oldbaselen +
                                        newpathlen);
        state->resolve2.par   = ntoh64 (req->newpar);
        state->resolve2.gen   = ntoh64 (req->newgen);

        resolve_and_resume (frame, server_rename_resume);

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

int
server_lk (call_frame_t *frame, xlator_t *bound_xl,
           gf_hdr_common_t *hdr, size_t hdrlen,
           struct iobuf *iobuf)
{
	struct flock         lock = {0, };
	gf_fop_lk_req_t     *req = NULL;
	server_state_t      *state = NULL;
	server_connection_t *conn = NULL;
	
	conn = SERVER_CONNECTION (frame);

	req   = gf_param (hdr);
	state = CALL_STATE (frame);
	{
		state->resolve.fd_no = ntoh64 (req->fd);
		if (state->resolve.fd_no >= 0)
			state->fd = gf_fd_fdptr_get (conn->fdtable, 
						     state->resolve.fd_no);

		state->cmd =  ntoh32 (req->cmd);
		state->type = ntoh32 (req->type);
	}


	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"fd - %"PRId64": unresolved fd", 
			state->resolve.fd_no);

		server_lk_cbk (frame, NULL, frame->this, -1, EBADF, NULL);
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
		gf_log (bound_xl->name, GF_LOG_ERROR,
			"fd - %"PRId64" (%"PRId64"): Unknown lock type: %"PRId32"!", 
			state->resolve.fd_no, state->fd->inode->ino, state->type);
		break;
	}

	gf_flock_to_flock (&req->flock, &lock);

	gf_log (BOUND_XL(frame)->name, GF_LOG_TRACE,
		"%"PRId64": LK \'fd=%"PRId64" (%"PRId64")\'",
		frame->root->unique, state->resolve.fd_no,
                state->fd->inode->ino);

	STACK_WIND (frame, server_lk_cbk,
		    BOUND_XL(frame), 
		    BOUND_XL(frame)->fops->lk,
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
int
server_setdents (call_frame_t *frame, xlator_t *bound_xl,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
	server_connection_t         *conn = NULL;
	gf_fop_setdents_req_t       *req   = NULL;
	server_state_t              *state = NULL;
	dir_entry_t                 *entry = NULL;
	dir_entry_t                 *trav = NULL;
	dir_entry_t                 *prev = NULL;
	int32_t                      count = 0;
	int32_t                      i = 0;
	int32_t                      bread = 0;
	char                        *ender = NULL;
	char                        *buffer_ptr = NULL;
	char                         tmp_buf[512] = {0,};
	
	conn = SERVER_CONNECTION(frame);

	req   = gf_param (hdr);
	state = CALL_STATE(frame);

	state->resolve.fd_no = ntoh64 (req->fd);
	if (state->resolve.fd_no >= 0)
		state->fd = gf_fd_fdptr_get (conn->fdtable, 
					     state->resolve.fd_no);
	
	state->nr_count = ntoh32 (req->count);

	if (state->fd == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"fd - %"PRId64": unresolved fd", 
			state->resolve.fd_no);

		server_setdents_cbk (frame, NULL, frame->this, -1, EBADF);

		goto out;
	}
	
	if (iobuf == NULL) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"fd - %"PRId64" (%"PRId64"): received a null buffer, "
			"returning EINVAL",
			state->resolve.fd_no, state->fd->inode->ino);

		server_setdents_cbk (frame, NULL, frame->this, -1, ENOMEM);

		goto out;
	}

	entry = CALLOC (1, sizeof (dir_entry_t));
	ERR_ABORT (entry);
	prev = entry;
	buffer_ptr = iobuf->ptr;

	for (i = 0; i < state->nr_count ; i++) {
		bread = 0;
		trav = CALLOC (1, sizeof (dir_entry_t));
		ERR_ABORT (trav);
		
		ender = strchr (buffer_ptr, '/');
		if (!ender)
			break;
		count = ender - buffer_ptr;
		trav->name = CALLOC (1, count + 2);
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
				&dev, &ino, &mode, &nlink, &uid, &gid, &rdev,
				&size, &blksize, &blocks, &atime, &atime_nsec,
				&mtime, &mtime_nsec, &ctime, &ctime_nsec);
			
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
			
			ST_ATIM_NSEC_SET(&trav->buf, atime_nsec);
			ST_MTIM_NSEC_SET(&trav->buf, mtime_nsec);
			ST_CTIM_NSEC_SET(&trav->buf, ctime_nsec);

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

	gf_log (bound_xl->name, GF_LOG_TRACE,
		"%"PRId64": SETDENTS \'fd=%"PRId64" (%"PRId64"); count=%"PRId64,
		frame->root->unique, state->resolve.fd_no, state->fd->inode->ino,
		(int64_t)state->nr_count);
	
	STACK_WIND (frame, server_setdents_cbk,
		    BOUND_XL(frame),
		    BOUND_XL(frame)->fops->setdents,
		    state->fd, state->flags, entry, state->nr_count);
	
	
	/* Free the variables allocated in this fop here */
	trav = entry->next;
	prev = entry;
	while (trav) {
		prev->next = trav->next;
		FREE (trav->name);
		if (S_ISLNK (trav->buf.st_mode))
			FREE (trav->link);
		FREE (trav);
		trav = prev->next;
	}
	FREE (entry);

out:
        if (iobuf)
                iobuf_unref (iobuf);
	return 0;
}



/* xxx_MOPS */
int
_volfile_update_checksum (xlator_t *this, char *key, uint32_t checksum)
{
        server_conf_t       *conf         = NULL;
        struct _volfile_ctx *temp_volfile = NULL;

        conf         = this->private;
        temp_volfile = conf->volfile;

        while (temp_volfile) {
                if ((NULL == key) && (NULL == temp_volfile->key))
                        break;
                if ((NULL == key) || (NULL == temp_volfile->key)) {
                        temp_volfile = temp_volfile->next;
                        continue;
                }
                if (strcmp (temp_volfile->key, key) == 0)
                        break;
                temp_volfile = temp_volfile->next;
        }

        if (!temp_volfile) {
                temp_volfile = CALLOC (1, sizeof (struct _volfile_ctx));

                temp_volfile->next  = conf->volfile;
                temp_volfile->key   = (key)? strdup (key): NULL;
                temp_volfile->checksum = checksum;
                
                conf->volfile = temp_volfile;
                goto out;
        }

        if (temp_volfile->checksum != checksum) {
                gf_log (this->name, GF_LOG_CRITICAL, 
                        "the volume file got modified between earlier access "
                        "and now, this may lead to inconsistency between "
                        "clients, advised to remount client");
                temp_volfile->checksum  = checksum;
        }

 out:
        return 0;
}


size_t 
build_volfile_path (xlator_t *this, const char *key, char *path, 
                    size_t path_len)
{
        int   ret = -1;
        int   free_filename = 0;
        char *filename = NULL;
	char  data_key[256] = {0,};

	/* Inform users that this option is changed now */
	ret = dict_get_str (this->options, "client-volume-filename", 
			    &filename);
	if (ret == 0) {
		gf_log (this->name, GF_LOG_WARNING,
			"option 'client-volume-filename' is changed to "
			"'volume-filename.<key>' which now takes 'key' as an "
			"option to choose/fetch different files from server. "
			"Refer documentation or contact developers for more "
			"info. Currently defaulting to given file '%s'", 
			filename);
	}
	
	if (key && !filename) {
		sprintf (data_key, "volume-filename.%s", key);
		ret = dict_get_str (this->options, data_key, &filename);
		if (ret < 0) {
                        /* Make sure that key doesn't contain 
                         * "../" in path 
                         */
                        if (!strstr (key, "../")) {
                                ret = asprintf (&filename, "%s/%s.vol", 
                                                CONFDIR, key);
                                if (-1 == ret) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "asprintf failed to get "
                                                "volume file path");
                                } else {
                                        free_filename = 1;
                                }
                        } else {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "%s: invalid key", key);
                        }
		} 
	}
        
	if (!filename) {
		ret = dict_get_str (this->options, 
                                    "volume-filename.default", &filename);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_DEBUG,
				"no default volume filename given, "
                                "defaulting to %s", DEFAULT_VOLUME_FILE_PATH);

                        filename = DEFAULT_VOLUME_FILE_PATH;
                }
	}

        ret = -1;
        if ((filename) && (path_len > strlen (filename))) {
                strcpy (path, filename);
                ret = strlen (filename);
        }

        if (free_filename)
                free (filename);

        return ret;
}

int 
_validate_volfile_checksum (xlator_t *this, char *key,
                            uint32_t checksum)
{        
	char                 filename[ZR_PATH_MAX] = {0,};
        server_conf_t       *conf         = NULL;
        struct _volfile_ctx *temp_volfile = NULL;
        int                  ret          = 0;
        uint32_t             local_checksum = 0;

        conf         = this->private;
        temp_volfile = conf->volfile;
        
        if (!checksum) 
                goto out;
        
        if (!temp_volfile) {
                ret = build_volfile_path (this, key, filename, 
                                          sizeof (filename));
                if (ret <= 0)
                        goto out;
                ret = open (filename, O_RDONLY);
                if (-1 == ret) {
                        ret = 0;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to open volume file (%s) : %s",
                                filename, strerror (errno));
                        goto out;
                }
                get_checksum_for_file (ret, &local_checksum);
                _volfile_update_checksum (this, key, local_checksum);
                close (ret);
        }

        temp_volfile = conf->volfile;
        while (temp_volfile) {
                if ((NULL == key) && (NULL == temp_volfile->key))
                        break;
                if ((NULL == key) || (NULL == temp_volfile->key)) {
                        temp_volfile = temp_volfile->next;
                        continue;
                }
                if (strcmp (temp_volfile->key, key) == 0)
                        break;
                temp_volfile = temp_volfile->next;
        }

        if (!temp_volfile)
                goto out;

        if ((temp_volfile->checksum) && 
            (checksum != temp_volfile->checksum)) 
                ret = -1;

out:
        return ret;
}

/* Management Calls */
/*
 * mop_getspec - getspec function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params:
 *
 */
int
mop_getspec (call_frame_t *frame, xlator_t *bound_xl,
             gf_hdr_common_t *hdr, size_t hdrlen,
             struct iobuf *iobuf)
{
	gf_hdr_common_t      *_hdr = NULL;
	gf_mop_getspec_rsp_t *rsp = NULL;
	int32_t               ret = -1;
	int32_t               op_errno = ENOENT;
	int32_t               gf_errno = 0;
	int32_t               spec_fd = -1;
	size_t                file_len = 0;
	size_t                _hdrlen = 0;
	char                  filename[ZR_PATH_MAX] = {0,};
	struct stat           stbuf = {0,};
	gf_mop_getspec_req_t *req = NULL;
        uint32_t              checksum = 0;
	uint32_t              flags  = 0;
	uint32_t              keylen = 0;
	char                 *key = NULL;
	server_conf_t        *conf = NULL;
        
	req   = gf_param (hdr);
	flags = ntoh32 (req->flags);
	keylen = ntoh32 (req->keylen);
	if (keylen) {
		key = req->key;
	}

        conf = frame->this->private;

        ret = build_volfile_path (frame->this, key, filename, 
                                  sizeof (filename));
        if (ret > 0) {
                /* to allocate the proper buffer to hold the file data */
		ret = stat (filename, &stbuf);
		if (ret < 0){
			gf_log (frame->this->name, GF_LOG_ERROR,
				"Unable to stat %s (%s)", 
				filename, strerror (errno));
			goto fail;
		}
                
                spec_fd = open (filename, O_RDONLY);
                if (spec_fd < 0) {
                        gf_log (frame->this->name, GF_LOG_ERROR,
                                "Unable to open %s (%s)", 
                                filename, strerror (errno));
                        goto fail;
                }
                ret = 0;
                file_len = stbuf.st_size;
                if (conf->verify_volfile_checksum) {
                        get_checksum_for_file (spec_fd, &checksum);
                        _volfile_update_checksum (frame->this, key, checksum);
                }
	} else {
                errno = ENOENT;
        }

fail:
	op_errno = errno;

	_hdrlen = gf_hdr_len (rsp, file_len + 1);
	_hdr    = gf_hdr_new (rsp, file_len + 1);
	rsp     = gf_param (_hdr);

	_hdr->rsp.op_ret = hton32 (ret);
	gf_errno         = gf_errno_to_error (op_errno);
	_hdr->rsp.op_errno = hton32 (gf_errno);

	if (file_len) {
		ret = read (spec_fd, rsp->spec, file_len);
		close (spec_fd);
	}
	protocol_server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_GETSPEC,
			       _hdr, _hdrlen, NULL, 0, NULL);

	return 0;
}


int
server_checksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     uint8_t *fchecksum, uint8_t *dchecksum)
{
	gf_hdr_common_t       *hdr = NULL;
	gf_fop_checksum_rsp_t *rsp = NULL;
	size_t                 hdrlen = 0;
	int32_t                gf_errno = 0;

	hdrlen = gf_hdr_len (rsp, NAME_MAX + 1 + NAME_MAX + 1);
	hdr    = gf_hdr_new (rsp, NAME_MAX + 1 + NAME_MAX + 1);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret >= 0) {
		memcpy (rsp->fchecksum, fchecksum, NAME_MAX);
		rsp->fchecksum[NAME_MAX] =  '\0';
		memcpy (rsp->dchecksum + NAME_MAX, 
			dchecksum, NAME_MAX);
		rsp->dchecksum[NAME_MAX + NAME_MAX] = '\0';
	} 

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_CHECKSUM,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}


int
server_checksum (call_frame_t *frame, xlator_t *bound_xl,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
	loc_t                  loc = {0,};
	int32_t                flag = 0;
	gf_fop_checksum_req_t *req = NULL;

	req = gf_param (hdr);

	loc.path  = req->path;
	loc.ino   = ntoh64 (req->ino);
	loc.inode = NULL;
	flag      = ntoh32 (req->flag);

	gf_log (bound_xl->name, GF_LOG_TRACE,
		"%"PRId64": CHECKSUM \'%s (%"PRId64")\'", 
		frame->root->unique, loc.path, loc.ino);

	STACK_WIND (frame, server_checksum_cbk,
		    BOUND_XL(frame),
		    BOUND_XL(frame)->fops->checksum,
		    &loc, flag);
	return 0;
}


int
server_rchecksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      uint32_t weak_checksum, uint8_t *strong_checksum)
{
	gf_hdr_common_t       *hdr = NULL;
	gf_fop_rchecksum_rsp_t *rsp = NULL;
	size_t                 hdrlen = 0;
	int32_t                gf_errno = 0;

	hdrlen = gf_hdr_len (rsp, MD5_DIGEST_LEN + 1);
	hdr    = gf_hdr_new (rsp, MD5_DIGEST_LEN + 1);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

	if (op_ret >= 0) {
		rsp->weak_checksum = weak_checksum;

		memcpy (rsp->strong_checksum,
			strong_checksum, MD5_DIGEST_LEN);

		rsp->strong_checksum[MD5_DIGEST_LEN] = '\0';
	}

	protocol_server_reply (frame, GF_OP_TYPE_FOP_REPLY, GF_FOP_RCHECKSUM,
			       hdr, hdrlen, NULL, 0, NULL);

	return 0;
}


int
server_rchecksum (call_frame_t *frame, xlator_t *bound_xl,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
	gf_fop_rchecksum_req_t  *req = NULL;
	server_state_t          *state = NULL;
	server_connection_t     *conn = NULL;

	conn = SERVER_CONNECTION(frame);

	req = gf_param (hdr);

	state = CALL_STATE(frame);
	{
		state->resolve.fd_no = ntoh64 (req->fd);
		if (state->resolve.fd_no >= 0)
			state->fd = gf_fd_fdptr_get (conn->fdtable,
						     state->resolve.fd_no);

		state->offset = ntoh64 (req->offset);
                state->size   = ntoh32 (req->len);
	}

	GF_VALIDATE_OR_GOTO(bound_xl->name, state->fd, fail);

	gf_log (bound_xl->name, GF_LOG_TRACE,
		"%"PRId64": RCHECKSUM \'fd=%"PRId64" (%"PRId64"); "
		"offset=%"PRId64"\'",
		frame->root->unique, state->resolve.fd_no,
                state->fd->inode->ino,
		state->offset);

	STACK_WIND (frame, server_rchecksum_cbk,
		    bound_xl,
		    bound_xl->fops->rchecksum,
		    state->fd, state->offset, state->size);
	return 0;
fail:
	server_rchecksum_cbk (frame, NULL, frame->this, -1, EINVAL, 0, NULL);

	return 0;
}


/*
 * mop_unlock - unlock management function for server protocol
 * @frame: call frame
 * @bound_xl:
 * @params: parameter dictionary
 *
 */
int
mop_getvolume (call_frame_t *frame, xlator_t *bound_xl,
               gf_hdr_common_t *hdr, size_t hdrlen,
               struct iobuf *iobuf)
{
	return 0;
}

struct __get_xl_struct {
	const char *name;
	xlator_t *reply;
};

void __check_and_set (xlator_t *each, void *data)
{
	if (!strcmp (each->name,
		     ((struct __get_xl_struct *) data)->name))
		((struct __get_xl_struct *) data)->reply = each;
}

static xlator_t *
get_xlator_by_name (xlator_t *some_xl, const char *name)
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
int
mop_setvolume (call_frame_t *frame, xlator_t *bound_xl,
               gf_hdr_common_t *req_hdr, size_t req_hdrlen,
               struct iobuf *iobuf)
{
	server_connection_t         *conn = NULL;
	server_conf_t               *conf = NULL;
	gf_hdr_common_t             *rsp_hdr = NULL;
	gf_mop_setvolume_req_t      *req = NULL;
	gf_mop_setvolume_rsp_t      *rsp = NULL;
	peer_info_t                 *peerinfo = NULL;
	int32_t                      ret = -1;
	int32_t                      op_ret = -1;
	int32_t                      op_errno = EINVAL;
	int32_t                      gf_errno = 0;
	dict_t                      *reply = NULL;
	dict_t                      *config_params = NULL;
	dict_t                      *params = NULL;
	char                        *name = NULL;
	char                        *version = NULL;
	char                        *process_uuid = NULL;
	xlator_t                    *xl = NULL;
	transport_t                 *trans = NULL;
	size_t                       rsp_hdrlen = -1;
	size_t                       dict_len = -1;
	size_t                       req_dictlen = -1;
        char                        *msg = NULL;
        char                        *volfile_key = NULL;
        uint32_t                     checksum = 0;
        int32_t                      lru_limit = 1024;

	params = dict_new ();
	reply  = dict_new ();

	req    = gf_param (req_hdr);
	req_dictlen = ntoh32 (req->dict_len);
	ret = dict_unserialize (req->buf, req_dictlen, &params);

	config_params = dict_copy_with_ref (frame->this->options, NULL);
	trans         = TRANSPORT_FROM_FRAME(frame);
	conf          = SERVER_CONF(frame);

	if (ret < 0) {
		ret = dict_set_str (reply, "ERROR",
				    "Internal error: failed to unserialize "
				    "request dictionary");
		if (ret < 0)
			gf_log (bound_xl->name, GF_LOG_DEBUG,
				"failed to set error msg \"%s\"",
				"Internal error: failed to unserialize "
				"request dictionary");

		op_ret = -1;
		op_errno = EINVAL;
		goto fail;
	}

	ret = dict_get_str (params, "process-uuid", &process_uuid);
	if (ret < 0) {
		ret = dict_set_str (reply, "ERROR",
				    "UUID not specified");
		if (ret < 0)
			gf_log (bound_xl->name, GF_LOG_DEBUG,
				"failed to set error msg");

		op_ret = -1;
		op_errno = EINVAL;
		goto fail;
	}
	

	conn = server_connection_get (frame->this, process_uuid);
	if (trans->xl_private != conn)
		trans->xl_private = conn;

	ret = dict_get_str (params, "protocol-version", &version);
	if (ret < 0) {
		ret = dict_set_str (reply, "ERROR",
				    "No version number specified");
		if (ret < 0)
			gf_log (trans->xl->name, GF_LOG_DEBUG,
				"failed to set error msg");

		op_ret = -1;
		op_errno = EINVAL;
		goto fail;
	}
	
	ret = strcmp (version, GF_PROTOCOL_VERSION);
	if (ret != 0) {
		ret = asprintf (&msg, "protocol version mismatch: client(%s) "
                                "- server(%s)", version, GF_PROTOCOL_VERSION);
                if (-1 == ret) {
                        gf_log (trans->xl->name, GF_LOG_ERROR,
                                "asprintf failed while setting up error msg");
                        goto fail;
                }
		ret = dict_set_dynstr (reply, "ERROR", msg);
		if (ret < 0)
			gf_log (trans->xl->name, GF_LOG_DEBUG,
				"failed to set error msg");

		op_ret = -1;
		op_errno = EINVAL;
		goto fail;
	}

	ret = dict_get_str (params,
			    "remote-subvolume", &name);
	if (ret < 0) {
		ret = dict_set_str (reply, "ERROR",
				    "No remote-subvolume option specified");
		if (ret < 0)
			gf_log (trans->xl->name, GF_LOG_DEBUG,
				"failed to set error msg");

		op_ret = -1;
		op_errno = EINVAL;
		goto fail;
	}

	xl = get_xlator_by_name (frame->this, name);
	if (xl == NULL) {
		ret = asprintf (&msg, "remote-subvolume \"%s\" is not found", 
                                name);
                if (-1 == ret) {
                        gf_log (trans->xl->name, GF_LOG_ERROR,
                                "asprintf failed while setting error msg");
                        goto fail;
                }
		ret = dict_set_dynstr (reply, "ERROR", msg);
		if (ret < 0)
			gf_log (trans->xl->name, GF_LOG_DEBUG,
				"failed to set error msg");

		op_ret = -1;
		op_errno = ENOENT;
		goto fail;
	}

        if (conf->verify_volfile_checksum) {
                ret = dict_get_uint32 (params, "volfile-checksum", &checksum);
                if (ret == 0) {
                        ret = dict_get_str (params, "volfile-key", 
                                            &volfile_key);
                        
                        ret = _validate_volfile_checksum (trans->xl, 
                                                          volfile_key, 
                                                          checksum);
                        if (-1 == ret) {
                                ret = dict_set_str (reply, "ERROR",
                                                    "volume-file checksum "
                                                    "varies from earlier "
                                                    "access");
                                if (ret < 0)
                                        gf_log (trans->xl->name, GF_LOG_DEBUG,
                                                "failed to set error msg");
                                
                                op_ret   = -1;
                                op_errno = ESTALE;
                                goto fail;
                        }
                }
        }


	peerinfo = &trans->peerinfo;
	ret = dict_set_static_ptr (params, "peer-info", peerinfo);
	if (ret < 0)
		gf_log (trans->xl->name, GF_LOG_DEBUG,
			"failed to set peer-info");

	if (conf->auth_modules == NULL) {
		gf_log (trans->xl->name, GF_LOG_ERROR,
			"Authentication module not initialized");
	}

	ret = gf_authenticate (params, config_params, 
			       conf->auth_modules);
	if (ret == AUTH_ACCEPT) {
		gf_log (trans->xl->name, GF_LOG_INFO,
			"accepted client from %s",
			peerinfo->identifier);
		op_ret = 0;
		conn->bound_xl = xl;
		ret = dict_set_str (reply, "ERROR", "Success");
		if (ret < 0)
			gf_log (trans->xl->name, GF_LOG_DEBUG,
				"failed to set error msg");
	} else {
		gf_log (trans->xl->name, GF_LOG_ERROR,
			"Cannot authenticate client from %s",
			peerinfo->identifier);
		op_ret = -1;
		op_errno = EACCES;
		ret = dict_set_str (reply, "ERROR", "Authentication failed");
		if (ret < 0)
			gf_log (bound_xl->name, GF_LOG_DEBUG,
				"failed to set error msg");

		goto fail;
	}

	if (conn->bound_xl == NULL) {
		ret = dict_set_str (reply, "ERROR",
				    "Check volfile and handshake "
				    "options in protocol/client");
		if (ret < 0)
			gf_log (trans->xl->name, GF_LOG_DEBUG, 
				"failed to set error msg");

		op_ret = -1;
		op_errno = EACCES;
		goto fail;
	}

	if ((conn->bound_xl != NULL) &&
	    (ret >= 0)                   &&
	    (conn->bound_xl->itable == NULL)) {
		/* create inode table for this bound_xl, if one doesn't 
		   already exist */
		lru_limit = INODE_LRU_LIMIT (frame->this);

		gf_log (trans->xl->name, GF_LOG_TRACE,
			"creating inode table with lru_limit=%"PRId32", "
			"xlator=%s", lru_limit, conn->bound_xl->name);

		conn->bound_xl->itable = 
			inode_table_new (1048576,
					 conn->bound_xl);
	}

	ret = dict_set_str (reply, "process-uuid", 
			    xl->ctx->process_uuid);

	ret = dict_set_uint64 (reply, "transport-ptr",
                               ((uint64_t) (long) trans));

fail:
	dict_len = dict_serialized_length (reply);
	if (dict_len < 0) {
		gf_log (xl->name, GF_LOG_DEBUG,
			"failed to get serialized length of reply dict");
		op_ret   = -1;
		op_errno = EINVAL;
		dict_len = 0;
	}

	rsp_hdr    = gf_hdr_new (rsp, dict_len);
	rsp_hdrlen = gf_hdr_len (rsp, dict_len);
	rsp = gf_param (rsp_hdr);

	if (dict_len) {
		ret = dict_serialize (reply, rsp->buf);
		if (ret < 0) {
			gf_log (xl->name, GF_LOG_DEBUG,
				"failed to serialize reply dict");
			op_ret = -1;
			op_errno = -ret;
		}
	}
	rsp->dict_len = hton32 (dict_len);

	rsp_hdr->rsp.op_ret = hton32 (op_ret);
	gf_errno = gf_errno_to_error (op_errno);
	rsp_hdr->rsp.op_errno = hton32 (gf_errno);

	protocol_server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_SETVOLUME,
			       rsp_hdr, rsp_hdrlen, NULL, 0, NULL);

	dict_unref (params);
	dict_unref (reply);
	dict_unref (config_params);

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

int
server_mop_stats_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t ret, int32_t op_errno,
                      struct xlator_stats *stats)
{
	/* TODO: get this information from somewhere else, not extern */
	gf_hdr_common_t    *hdr = NULL;
	gf_mop_stats_rsp_t *rsp = NULL;
	char                buffer[256] = {0,};
	int64_t             glusterfsd_stats_nr_clients = 0;
	size_t              hdrlen = 0;
	size_t              buf_len = 0;
	int32_t             gf_errno = 0;

	if (ret >= 0) {
		sprintf (buffer,
			 "%"PRIx64",%"PRIx64",%"PRIx64
			 ",%"PRIx64",%"PRIx64",%"PRIx64
			 ",%"PRIx64",%"PRIx64"\n",
			 stats->nr_files, stats->disk_usage, stats->free_disk,
			 stats->total_disk_size, stats->read_usage,
			 stats->write_usage, stats->disk_speed,
			 glusterfsd_stats_nr_clients);

		buf_len = strlen (buffer);
	}

	hdrlen = gf_hdr_len (rsp, buf_len + 1);
	hdr    = gf_hdr_new (rsp, buf_len + 1);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (ret);
	gf_errno        = gf_errno_to_error (op_errno);
	hdr->rsp.op_errno = hton32 (gf_errno);

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
int
mop_stats (call_frame_t *frame, xlator_t *bound_xl,
           gf_hdr_common_t *hdr, size_t hdrlen,
           struct iobuf *iobuf)
{
	int32_t             flag = 0;
	gf_mop_stats_req_t *req = NULL;

	req = gf_param (hdr);

	flag = ntoh32 (req->flags);

	STACK_WIND (frame, server_mop_stats_cbk,
		    bound_xl,
		    bound_xl->mops->stats,
		    flag);

	return 0;
}


int
mop_ping (call_frame_t *frame, xlator_t *bound_xl,
          gf_hdr_common_t *hdr, size_t hdrlen,
          struct iobuf *iobuf)
{
	gf_hdr_common_t     *rsp_hdr = NULL;
	gf_mop_ping_rsp_t   *rsp = NULL;
	size_t               rsp_hdrlen = 0;

	rsp_hdrlen = gf_hdr_len (rsp, 0);
	rsp_hdr    = gf_hdr_new (rsp, 0);

	hdr->rsp.op_ret = 0;

	protocol_server_reply (frame, GF_OP_TYPE_MOP_REPLY, GF_MOP_PING,
			       rsp_hdr, rsp_hdrlen, NULL, 0, NULL);

	return 0;
}


int
mop_log (call_frame_t *frame, xlator_t *bound_xl,
         gf_hdr_common_t *hdr, size_t hdrlen,
         struct iobuf *iobuf)
{
        gf_mop_log_req_t *    req = NULL;
        char *                msg = NULL;
        uint32_t           msglen = 0;

        transport_t *       trans = NULL;

        trans = TRANSPORT_FROM_FRAME (frame);

        req    = gf_param (hdr);
        msglen = ntoh32 (req->msglen);

        if (msglen)
                msg = req->msg;

        gf_log_from_client (msg, trans->peerinfo.identifier);

        return 0;
}


/*
 * unknown_op_cbk - This function is called when a opcode for unknown 
 *                  type is called. Helps to keep the backward/forward
 *                  compatiblity
 * @frame: call frame
 * @type:
 * @opcode:
 *
 */

int
unknown_op_cbk (call_frame_t *frame, int32_t type, int32_t opcode)
{
	gf_hdr_common_t    *hdr = NULL;
	gf_fop_flush_rsp_t *rsp = NULL;
	size_t              hdrlen = 0;
	int32_t             gf_errno = 0;

	hdrlen = gf_hdr_len (rsp, 0);
	hdr    = gf_hdr_new (rsp, 0);
	rsp    = gf_param (hdr);

	hdr->rsp.op_ret = hton32 (-1);
	gf_errno        = gf_errno_to_error (ENOSYS);
	hdr->rsp.op_errno = hton32 (gf_errno);

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
	call_frame_t         *frame = NULL;
	call_pool_t          *pool = NULL;
	server_connection_t  *conn = NULL;
	server_state_t       *state = NULL;;
	
	GF_VALIDATE_OR_GOTO("server", trans, out);

	if (trans->xl && trans->xl->ctx)
		pool = trans->xl->ctx->pool;
	GF_VALIDATE_OR_GOTO("server", pool, out);

	frame = create_frame (trans->xl, pool);
	GF_VALIDATE_OR_GOTO("server", frame, out);

	state = CALLOC (1, sizeof (*state));
	GF_VALIDATE_OR_GOTO("server", state, out);

	conn = trans->xl_private;
	if (conn) {
		if (conn->bound_xl)
			state->itable = conn->bound_xl->itable;
		state->bound_xl = conn->bound_xl;
	}

	state->trans = transport_ref (trans);
        state->resolve.fd_no = -1;
        state->resolve2.fd_no = -1;

	frame->root->trans = conn;
	frame->root->state = state;        /* which socket */
	frame->root->unique = 0;           /* which call */

out:
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

	frame->root->op   = ntoh32 (hdr->op);
	frame->root->type = ntoh32 (hdr->type);

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
                            struct iobuf *iobuf);


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
	[GF_FOP_TRUNCATE]     =  server_truncate,
	[GF_FOP_OPEN]         =  server_open,
	[GF_FOP_READ]         =  server_readv,
	[GF_FOP_WRITE]        =  server_writev,
	[GF_FOP_STATFS]       =  server_statfs,
	[GF_FOP_FLUSH]        =  server_flush,
	[GF_FOP_FSYNC]        =  server_fsync,
	[GF_FOP_SETXATTR]     =  server_setxattr,
	[GF_FOP_GETXATTR]     =  server_getxattr,
        [GF_FOP_FGETXATTR]    =  server_fgetxattr,
        [GF_FOP_FSETXATTR]    =  server_fsetxattr,
	[GF_FOP_REMOVEXATTR]  =  server_removexattr,
	[GF_FOP_OPENDIR]      =  server_opendir,
	[GF_FOP_GETDENTS]     =  server_getdents,
	[GF_FOP_FSYNCDIR]     =  server_fsyncdir,
	[GF_FOP_ACCESS]       =  server_access,
	[GF_FOP_CREATE]       =  server_create,
	[GF_FOP_FTRUNCATE]    =  server_ftruncate,
	[GF_FOP_FSTAT]        =  server_fstat,
	[GF_FOP_LK]           =  server_lk,
	[GF_FOP_LOOKUP]       =  server_lookup,
	[GF_FOP_SETDENTS]     =  server_setdents,
	[GF_FOP_READDIR]      =  server_readdir,
	[GF_FOP_READDIRP]     =  server_readdirp,
	[GF_FOP_INODELK]      =  server_inodelk,
	[GF_FOP_FINODELK]     =  server_finodelk,
	[GF_FOP_ENTRYLK]      =  server_entrylk,
	[GF_FOP_FENTRYLK]     =  server_fentrylk,
	[GF_FOP_CHECKSUM]     =  server_checksum,
        [GF_FOP_RCHECKSUM]    =  server_rchecksum,
	[GF_FOP_XATTROP]      =  server_xattrop,
	[GF_FOP_FXATTROP]     =  server_fxattrop,
        [GF_FOP_SETATTR]      =  server_setattr,
        [GF_FOP_FSETATTR]     =  server_fsetattr,
};



static gf_op_t gf_mops[] = {
	[GF_MOP_SETVOLUME] = mop_setvolume,
	[GF_MOP_GETVOLUME] = mop_getvolume,
	[GF_MOP_STATS]     = mop_stats,
	[GF_MOP_GETSPEC]   = mop_getspec,
	[GF_MOP_PING]      = mop_ping,
        [GF_MOP_LOG]       = mop_log,
};

static gf_op_t gf_cbks[] = {
	[GF_CBK_FORGET]     = server_forget,
	[GF_CBK_RELEASE]    = server_release,
	[GF_CBK_RELEASEDIR] = server_releasedir
};

int
protocol_server_interpret (xlator_t *this, transport_t *trans,
                           char *hdr_p, size_t hdrlen, struct iobuf *iobuf)
{
	server_connection_t         *conn = NULL;
	gf_hdr_common_t             *hdr = NULL;
	xlator_t                    *bound_xl = NULL;
	call_frame_t                *frame = NULL;
	peer_info_t                 *peerinfo = NULL;
	int32_t                      type = -1;
	int32_t                      op = -1;
	int32_t                      ret = -1;

	hdr  = (gf_hdr_common_t *)hdr_p;
	type = ntoh32 (hdr->type);
	op   = ntoh32 (hdr->op);

	conn = trans->xl_private;
	if (conn)
		bound_xl = conn->bound_xl;

	peerinfo = &trans->peerinfo;
	switch (type) {
	case GF_OP_TYPE_FOP_REQUEST:
		if ((op < 0) || (op >= GF_FOP_MAXVALUE)) {
			gf_log (this->name, GF_LOG_ERROR,
				"invalid fop %"PRId32" from client %s",
				op, peerinfo->identifier);
			break;
		}
		if (bound_xl == NULL) {
			gf_log (this->name, GF_LOG_ERROR,
				"Received fop %"PRId32" before "
				"authentication.", op);
			break;
		}
		frame = get_frame_for_call (trans, hdr);
		ret = gf_fops[op] (frame, bound_xl, hdr, hdrlen, iobuf);
		break;

	case GF_OP_TYPE_MOP_REQUEST:
		if ((op < 0) || (op >= GF_MOP_MAXVALUE)) {
			gf_log (this->name, GF_LOG_ERROR,
				"invalid mop %"PRId32" from client %s",
				op, peerinfo->identifier);
			break;
		}
		frame = get_frame_for_call (trans, hdr);
		ret = gf_mops[op] (frame, bound_xl, hdr, hdrlen, iobuf);
		break;

	case GF_OP_TYPE_CBK_REQUEST:
		if ((op < 0) || (op >= GF_CBK_MAXVALUE)) {
			gf_log (this->name, GF_LOG_ERROR,
				"invalid cbk %"PRId32" from client %s",
				op, peerinfo->identifier);
			break;
		}
		if (bound_xl == NULL) {
			gf_log (this->name, GF_LOG_ERROR,
				"Received cbk %d before authentication.", op);
			break;
		}

		frame = get_frame_for_call (trans, hdr);
		ret = gf_cbks[op] (frame, bound_xl, hdr, hdrlen, iobuf);
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
int
server_nop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno)
{
	server_state_t *state = NULL;
	
	state = CALL_STATE(frame);

	if (state)
		free_state (state);
	STACK_DESTROY (frame->root);
	return 0;
}

/*
 * server_fd - fdtable dump function for server protocol
 * @this:
 *
 */
int
server_fd (xlator_t *this)
{
         server_conf_t        *conf = NULL;
 	 server_connection_t  *trav = NULL;
         char                 key[GF_DUMP_MAX_BUF_LEN];
         int                  i = 1;
         int                  ret = -1;
 	
         if (!this)
                 return -1;
 	
         conf = this->private;
         if (!conf) {
 		gf_log (this->name, GF_LOG_WARNING,
 			"conf null in xlator");
                return -1;
         }
 
         gf_proc_dump_add_section("xlator.protocol.server.conn");
         
         ret = pthread_mutex_trylock (&conf->mutex);
         if (ret) { 
                gf_log("", GF_LOG_WARNING, "Unable to dump fdtable"
                " errno: %d", errno);
                return -1;
        }
 	
         list_for_each_entry (trav, &conf->conns, list) {
                 if (trav->id) {
                         gf_proc_dump_build_key(key, 
                                          "xlator.protocol.server.conn", 
                                          "%d.id", i);
                         gf_proc_dump_write(key, "%s", trav->id);
                 }
                         
                 gf_proc_dump_build_key(key,"xlator.protocol.server.conn",
                                        "%d.ref",i) 
                 gf_proc_dump_write(key, "%d", trav->ref);
                 if (trav->bound_xl) {
                         gf_proc_dump_build_key(key, 
                                          "xlator.protocol.server.conn", 
                                          "%d.bound_xl", i);
                         gf_proc_dump_write(key, "%s", trav->bound_xl->name);
                 }
                         
                 gf_proc_dump_build_key(key, 
                                        "xlator.protocol.server.conn", 
                                         "%d.id", i);
                 fdtable_dump(trav->fdtable,key);
                 i++;
         }
 	pthread_mutex_unlock (&conf->mutex);
 
 
 	return 0;
 }

int
server_priv (xlator_t *this) 
{
        return 0;
}

int
server_inode (xlator_t *this)
{
	 server_conf_t        *conf = NULL;
	 server_connection_t  *trav = NULL;
         char                 key[GF_DUMP_MAX_BUF_LEN];
         int                  i = 1;
         int                  ret = -1;

         if (!this)
                 return -1;

         conf = this->private;
         if (!conf) {
		gf_log (this->name, GF_LOG_WARNING,
			"conf null in xlator");
                return -1;
         }

         ret = pthread_mutex_trylock (&conf->mutex);
         if (ret) {
                gf_log("", GF_LOG_WARNING, "Unable to dump itable"
                " errno: %d", errno);
                return -1;
        }

        list_for_each_entry (trav, &conf->conns, list) {
                 if (trav->bound_xl && trav->bound_xl->itable) {
                         gf_proc_dump_build_key(key,
                                          "xlator.protocol.server.conn",
                                          "%d.bound_xl.%s",
                                          i, trav->bound_xl->name);
                         inode_table_dump(trav->bound_xl->itable,key);
                         i++;
                 }
        }
	pthread_mutex_unlock (&conf->mutex);


	return 0;
}


static void
get_auth_types (dict_t *this, char *key, data_t *value, void *data)
{
	dict_t   *auth_dict = NULL;
	char     *saveptr = NULL;
        char     *tmp = NULL;
	char     *key_cpy = NULL;
	int32_t   ret = -1;

        auth_dict = data;
	key_cpy = strdup (key);
	GF_VALIDATE_OR_GOTO("server", key_cpy, out);

	tmp = strtok_r (key_cpy, ".", &saveptr);
	ret = strcmp (tmp, "auth");
	if (ret == 0) {
		tmp = strtok_r (NULL, ".", &saveptr);
		if (strcmp (tmp, "ip") == 0) {
			/* TODO: backward compatibility, remove when 
			   newer versions are available */
			tmp = "addr";
			gf_log ("server", GF_LOG_WARNING, 
				"assuming 'auth.ip' to be 'auth.addr'");
		}
		ret = dict_set_dynptr (auth_dict, tmp, NULL, 0);
		if (ret < 0) {
			gf_log ("server", GF_LOG_DEBUG,
				"failed to dict_set_dynptr");
		} 
	}

	FREE (key_cpy);
out:
	return;
}


int
validate_auth_options (xlator_t *this, dict_t *dict)
{
	int            ret = -1;
	int            error = 0;
	xlator_list_t *trav = NULL;
	data_pair_t   *pair = NULL;
	char          *saveptr = NULL;
        char          *tmp = NULL;
	char          *key_cpy = NULL;

	trav = this->children;
	while (trav) {
		error = -1;
		for (pair = dict->members_list; pair; pair = pair->next) {
			key_cpy = strdup (pair->key);
			tmp = strtok_r (key_cpy, ".", &saveptr);
			ret = strcmp (tmp, "auth");
			if (ret == 0) {
				/* for module type */
				tmp = strtok_r (NULL, ".", &saveptr); 
				/* for volume name */
				tmp = strtok_r (NULL, ".", &saveptr); 
			}

			if (strcmp (tmp, trav->xlator->name) == 0) {
				error = 0;
				free (key_cpy);
				break;
			}
			free (key_cpy);
		}
		if (-1 == error) {
			gf_log (this->name, GF_LOG_ERROR, 
				"volume '%s' defined as subvolume, but no "
				"authentication defined for the same",
				trav->xlator->name);
			break;
		}
		trav = trav->next;
	}

	return error;
}


/*
 * init - called during server protocol initialization
 *
 * @this:
 *
 */
int
init (xlator_t *this)
{
	int32_t        ret = -1;
	transport_t   *trans = NULL;
	server_conf_t *conf = NULL;
        data_t        *data = NULL;

	if (this->children == NULL) {
		gf_log (this->name, GF_LOG_ERROR,
			"protocol/server should have subvolume");
		goto out;
	}

	trans = transport_load (this->options, this);
	if (trans == NULL) {
		gf_log (this->name, GF_LOG_ERROR,
			"failed to load transport");
		goto out;
	}

	ret = transport_listen (trans);
	if (ret == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"failed to bind/listen on socket");
		goto out;
	}

	conf = CALLOC (1, sizeof (server_conf_t));
	GF_VALIDATE_OR_GOTO(this->name, conf, out);

	INIT_LIST_HEAD (&conf->conns);
	pthread_mutex_init (&conf->mutex, NULL);

	conf->trans = trans;

	conf->auth_modules = dict_new ();
	GF_VALIDATE_OR_GOTO(this->name, conf->auth_modules, out);

	dict_foreach (this->options, get_auth_types, 
		      conf->auth_modules);
	ret = validate_auth_options (this, this->options);
	if (ret == -1) {
		/* logging already done in validate_auth_options function. */
		goto out;
	}
	
	ret = gf_auth_init (this, conf->auth_modules);
	if (ret) {
		dict_unref (conf->auth_modules);
		goto out;
	}

	this->private = conf;

	ret = dict_get_int32 (this->options, "inode-lru-limit", 
			      &conf->inode_lru_limit);
	if (ret < 0) {
		conf->inode_lru_limit = 1024;
	}

	ret = dict_get_int32 (this->options, "limits.transaction-size", 
			      &conf->max_block_size);
	if (ret < 0) {
		gf_log (this->name, GF_LOG_TRACE,
			"defaulting limits.transaction-size to %d",
			DEFAULT_BLOCK_SIZE);
		conf->max_block_size = DEFAULT_BLOCK_SIZE;
	}
        
        conf->verify_volfile_checksum = 1;
	data = dict_get (this->options, "verify-volfile-checksum");
	if (data) {
                ret = gf_string2boolean(data->data, 
                                        &conf->verify_volfile_checksum);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "wrong value for verify-volfile-checksum");
                        conf->verify_volfile_checksum = 1;
                }
	}

#ifndef GF_DARWIN_HOST_OS
	{
		struct rlimit lim;

		lim.rlim_cur = 1048576;
		lim.rlim_max = 1048576;

		if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
			gf_log (this->name, GF_LOG_WARNING,
				"WARNING: Failed to set 'ulimit -n 1M': %s",
				strerror(errno));
			lim.rlim_cur = 65536;
			lim.rlim_max = 65536;

			if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
				gf_log (this->name, GF_LOG_WARNING,
					"Failed to set max open fd to 64k: %s",
					strerror(errno));
			} else {
				gf_log (this->name, GF_LOG_TRACE,
					"max open fd set to 64k");
			}
		}
	}
#endif
	this->ctx->top = this;

	ret = 0;
out:
	return ret;
}



int
protocol_server_pollin (xlator_t *this, transport_t *trans)
{
	char                *hdr = NULL;
	size_t               hdrlen = 0;
	int                  ret = -1;
        struct iobuf        *iobuf = NULL;


	ret = transport_receive (trans, &hdr, &hdrlen, &iobuf);

	if (ret == 0)
		ret = protocol_server_interpret (this, trans, hdr, 
						 hdrlen, iobuf);

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
	server_conf_t *conf = this->private;

	GF_VALIDATE_OR_GOTO(this->name, conf, out);

	if (conf->auth_modules) {
		dict_unref (conf->auth_modules);
	}

	FREE (conf);
	this->private = NULL;
out:
	return;
}

/*
 * server_protocol_notify - notify function for server protocol
 * @this:
 * @trans:
 * @event:
 *
 */
int
notify (xlator_t *this, int32_t event, void *data, ...)
{
	int          ret = 0;
	transport_t *trans = data;
        peer_info_t *peerinfo = NULL;
        peer_info_t *myinfo = NULL;

        if (trans != NULL) {
                peerinfo = &(trans->peerinfo);
                myinfo = &(trans->myinfo);
        }

	switch (event) {
	case GF_EVENT_POLLIN:
		ret = protocol_server_pollin (this, trans);
		break;
	case GF_EVENT_POLLERR:
	{
		gf_log (trans->xl->name, GF_LOG_INFO, "%s disconnected",
			peerinfo->identifier);

		ret = -1;
		transport_disconnect (trans);
                if (trans->xl_private == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "POLLERR received on (%s) even before "
                                "handshake with (%s) is successful",
                                myinfo->identifier, peerinfo->identifier);
                } else {
                        /*
                         * FIXME: shouldn't we check for return value?
                         * what should be done if cleanup fails?
                         */
                        server_connection_cleanup (this, trans->xl_private);
                }
	}
	break;

	case GF_EVENT_TRANSPORT_CLEANUP:
	{
		if (trans->xl_private) {
			server_connection_put (this, trans->xl_private);
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "transport (%s) cleaned up even before "
                                "handshake with (%s) is successful",
                                myinfo->identifier, peerinfo->identifier);
                }
	}
	break;

	default:
		default_notify (this, event, data);
		break;
	}

	return ret;
}


struct xlator_mops mops = {
};

struct xlator_fops fops = {
};

struct xlator_cbks cbks = {
};

struct xlator_dumpops dumpops = {
        .inode = server_inode,
        .priv  = server_priv,
        .fd    = server_fd,
};


struct volume_options options[] = {
 	{ .key   = {"transport-type"}, 
	  .value = {"tcp", "socket", "ib-verbs", "unix", "ib-sdp", 
		    "tcp/server", "ib-verbs/server"},
	  .type  = GF_OPTION_TYPE_STR 
	},
	{ .key   = {"volume-filename.*"}, 
	  .type  = GF_OPTION_TYPE_PATH, 
	},
	{ .key   = {"inode-lru-limit"},  
	  .type  = GF_OPTION_TYPE_INT,
	  .min   = 0, 
	  .max   = (1 * GF_UNIT_MB)
	},
	{ .key   = {"client-volume-filename"}, 
	  .type  = GF_OPTION_TYPE_PATH
	}, 
	{ .key   = {"verify-volfile-checksum"}, 
	  .type  = GF_OPTION_TYPE_BOOL
	}, 
	{ .key   = {NULL} },
};
