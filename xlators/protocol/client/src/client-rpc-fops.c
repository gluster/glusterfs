/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "client.h"
#include "rpc-common-xdr.h"
#include "glusterfs3-xdr.h"
#include "glusterfs3.h"
#include "compat-errno.h"
#include "client-messages.h"
#include "defaults.h"
#include "client-common.h"
#include "compound-fop-utils.h"

int32_t client3_getspec (call_frame_t *frame, xlator_t *this, void *data);
rpc_clnt_prog_t clnt3_3_fop_prog;


int
client_submit_vec_request (xlator_t  *this, void *req, call_frame_t  *frame,
                           rpc_clnt_prog_t *prog, int procnum,
                           fop_cbk_fn_t cbkfn,
                           struct iovec  *payload, int payloadcnt,
                           struct iobref *iobref, xdrproc_t xdrproc)
{
        int             ret        = 0;
        clnt_conf_t    *conf       = NULL;
        struct iovec    iov        = {0, };
        struct iobuf   *iobuf      = NULL;
        int             count      = 0;
        struct iobref  *new_iobref = NULL;
        ssize_t         xdr_size   = 0;
        struct rpc_req  rpcreq     = {0, };

        conf = this->private;

        if (req && xdrproc) {
                xdr_size = xdr_sizeof (xdrproc, req);
                iobuf = iobuf_get2 (this->ctx->iobuf_pool, xdr_size);
                if (!iobuf) {
                        goto unwind;
                };

                new_iobref = iobref_new ();
                if (!new_iobref) {
                        goto unwind;
                }

                if (iobref != NULL) {
                        ret = iobref_merge (new_iobref, iobref);
                        if (ret != 0) {
                                gf_msg (this->name, GF_LOG_WARNING, ENOMEM,
                                        PC_MSG_NO_MEMORY, "cannot merge "
                                        "iobref passed from caller into "
                                        "new_iobref");
                        }
                }

                ret = iobref_add (new_iobref, iobuf);
                if (ret != 0) {
                        gf_msg (this->name, GF_LOG_WARNING, ENOMEM,
                                PC_MSG_NO_MEMORY, "cannot add iobuf into "
                                "iobref");
                        goto unwind;
                }

                iov.iov_base = iobuf->ptr;
                iov.iov_len  = iobuf_size (iobuf);

                /* Create the xdr payload */
                ret = xdr_serialize_generic (iov, req, xdrproc);
                if (ret == -1) {
                        gf_log_callingfn ("", GF_LOG_WARNING,
                                          "XDR function failed");
                        goto unwind;
                }

                iov.iov_len = ret;
                count = 1;
        }

        /* Send the msg */
        ret = rpc_clnt_submit (conf->rpc, prog, procnum, cbkfn, &iov, count,
                               payload, payloadcnt, new_iobref, frame, NULL, 0,
                               NULL, 0, NULL);
        if (ret < 0) {
                gf_msg_debug (this->name, 0, "rpc_clnt_submit failed");
        }

        if (new_iobref)
                iobref_unref (new_iobref);

        if (iobuf)
                iobuf_unref (iobuf);

        return ret;

unwind:
        rpcreq.rpc_status = -1;
        cbkfn (&rpcreq, NULL, 0, frame);

        if (new_iobref)
                iobref_unref (new_iobref);

        if (iobuf)
                iobuf_unref (iobuf);

        return ret;
}

/* CBK */

int
client3_3_symlink_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t     *frame      = NULL;
        gfs3_symlink_rsp  rsp        = {0,};
        struct iatt       stbuf      = {0,};
        struct iatt       preparent  = {0,};
        struct iatt       postparent = {0,};
        int               ret        = 0;
        clnt_local_t     *local      = NULL;
        inode_t          *inode      = NULL;
        xlator_t         *this       = NULL;
        dict_t           *xdata      = NULL;

        this = THIS;

        frame = myframe;

        local = frame->local;
        inode = local->loc.inode;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_symlink_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_symlink (this, &rsp, &stbuf, &preparent,
                             &postparent, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                if (GF_IGNORE_IF_GSYNCD_SAFE_ERROR(frame, rsp.op_errno)) {
                        /* no need to print the gfid, because it will be null,
                         * since symlink operation failed.
                         */
                        gf_msg (this->name, GF_LOG_WARNING,
                                gf_error_to_errno (rsp.op_errno),
                                PC_MSG_REMOTE_OP_FAILED,
                                "remote operation failed. Path: (%s to %s)",
                                local->loc.path, local->loc2.path);
                }
        }

        CLIENT_STACK_UNWIND (symlink, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), inode, &stbuf,
                             &preparent, &postparent, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}


int
client3_3_mknod_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        call_frame_t     *frame      = NULL;
        gfs3_mknod_rsp    rsp        = {0,};
        struct iatt       stbuf      = {0,};
        struct iatt       preparent  = {0,};
        struct iatt       postparent = {0,};
        int               ret        = 0;
        clnt_local_t     *local      = NULL;
        inode_t          *inode      = NULL;
        xlator_t         *this       = NULL;
        dict_t           *xdata      = NULL;

        this = THIS;

        frame = myframe;

        local = frame->local;

        inode = local->loc.inode;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_mknod_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_mknod (this, &rsp, &stbuf, &preparent, &postparent,
                           &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1 &&
            GF_IGNORE_IF_GSYNCD_SAFE_ERROR(frame, rsp.op_errno)) {
                gf_msg (this->name, fop_log_level (GF_FOP_MKNOD,
                        gf_error_to_errno (rsp.op_errno)),
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed. Path: %s",
                        local->loc.path);
        }

        CLIENT_STACK_UNWIND (mknod, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), inode,
                             &stbuf, &preparent, &postparent, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_mkdir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        call_frame_t     *frame      = NULL;
        gfs3_mkdir_rsp    rsp        = {0,};
        struct iatt       stbuf      = {0,};
        struct iatt       preparent  = {0,};
        struct iatt       postparent = {0,};
        int               ret        = 0;
        clnt_local_t     *local      = NULL;
        inode_t          *inode      = NULL;
        xlator_t         *this       = NULL;
        dict_t           *xdata      = NULL;

        this = THIS;

        frame = myframe;

        local = frame->local;
        inode = local->loc.inode;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_mkdir_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_mkdir (this, &rsp, &stbuf, &preparent, &postparent,
                           &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1 &&
            GF_IGNORE_IF_GSYNCD_SAFE_ERROR(frame, rsp.op_errno)) {
                gf_msg (this->name, fop_log_level (GF_FOP_MKDIR,
                        gf_error_to_errno (rsp.op_errno)),
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed. Path: %s",
                        local->loc.path);
        }

        CLIENT_STACK_UNWIND (mkdir, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), inode,
                             &stbuf, &preparent, &postparent, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
_copy_gfid_from_inode_holders (uuid_t gfid, loc_t *loc, fd_t *fd)
{
        int     ret = 0;

        if (fd && fd->inode && !gf_uuid_is_null (fd->inode->gfid)) {
                gf_uuid_copy (gfid, fd->inode->gfid);
                goto out;
        }

        if (!loc) {
                GF_ASSERT (0);
                ret = -1;
                goto out;
        }

        if (loc->inode && !gf_uuid_is_null (loc->inode->gfid)) {
                gf_uuid_copy (gfid, loc->inode->gfid);
        } else if (!gf_uuid_is_null (loc->gfid)) {
                gf_uuid_copy (gfid, loc->gfid);
        } else {
                GF_ASSERT (0);
                ret = -1;
        }
out:
        return ret;
}

int
client_add_fd_to_saved_fds (xlator_t *this, fd_t *fd, loc_t *loc, int32_t flags,
                            int64_t remote_fd, int is_dir)
{
        int             ret = 0;
        uuid_t          gfid = {0};
        clnt_conf_t     *conf       = NULL;
        clnt_fd_ctx_t   *fdctx      = NULL;

        conf  = this->private;
        ret = _copy_gfid_from_inode_holders (gfid, loc, fd);
        if (ret) {
                ret = -EINVAL;
                goto out;
        }

        fdctx = GF_CALLOC (1, sizeof (*fdctx),
                           gf_client_mt_clnt_fdctx_t);
        if (!fdctx) {
                ret = -ENOMEM;
                goto out;
        }

        gf_uuid_copy (fdctx->gfid, gfid);
        fdctx->is_dir        = is_dir;
        fdctx->remote_fd     = remote_fd;
        fdctx->flags         = flags;
        fdctx->lk_ctx        = fd_lk_ctx_ref (fd->lk_ctx);
        fdctx->lk_heal_state = GF_LK_HEAL_DONE;
        fdctx->reopen_done   = client_default_reopen_done;

        INIT_LIST_HEAD (&fdctx->sfd_pos);
        INIT_LIST_HEAD (&fdctx->lock_list);

        this_fd_set_ctx (fd, this, loc, fdctx);

        pthread_mutex_lock (&conf->lock);
        {
                list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
        }
        pthread_mutex_unlock (&conf->lock);
out:
        return ret;
}

int
client3_3_open_cbk (struct rpc_req *req, struct iovec *iov, int count,
                    void *myframe)
{
        clnt_local_t  *local = NULL;
        call_frame_t  *frame = NULL;
        fd_t          *fd    = NULL;
        int            ret   = 0;
        gfs3_open_rsp  rsp   = {0,};
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;
        local = frame->local;

        fd    = local->fd;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_open_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                ret = client_add_fd_to_saved_fds (frame->this, fd, &local->loc,
                                                  local->flags, rsp.fd, 0);
                if (ret) {
                        rsp.op_ret = -1;
                        rsp.op_errno = -ret;
                        goto out;
                }
        }

        ret = client_post_open (this, &rsp, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, fop_log_level (GF_FOP_OPEN,
                        gf_error_to_errno (rsp.op_errno)),
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed. Path: %s (%s)",
                        local->loc.path, loc_gfid_utoa (&local->loc));
        }

        CLIENT_STACK_UNWIND (open, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), fd, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}


int
client3_3_stat_cbk (struct rpc_req *req, struct iovec *iov, int count,
                    void *myframe)
{
        gfs3_stat_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  iatt = {0,};
        int ret = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_stat_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_stat (this, &rsp, &iatt, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                /* stale filehandles are possible during normal operations, no
                 * need to spam the logs with these */
                if (rsp.op_errno == ESTALE) {
                        gf_msg_debug (this->name, 0,
                                      "remote operation failed: %s",
                                      strerror (gf_error_to_errno
                                                (rsp.op_errno)));
                } else {
                        gf_msg (this->name, GF_LOG_WARNING,
                                gf_error_to_errno (rsp.op_errno),
                                PC_MSG_REMOTE_OP_FAILED,
                                "remote operation failed");
                }
        }

        CLIENT_STACK_UNWIND (stat, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &iatt, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_readlink_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        gfs3_readlink_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  iatt = {0,};
        int ret = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_readlink_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_readlink (this, &rsp, &iatt, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                if (gf_error_to_errno(rsp.op_errno) == ENOENT) {
                        gf_msg_debug (this->name, 0, "remote operation failed:"
                                      " %s", strerror
                                          (gf_error_to_errno (rsp.op_errno)));
                } else {
                        gf_msg (this->name, GF_LOG_WARNING,
                                gf_error_to_errno (rsp.op_errno),
                                PC_MSG_REMOTE_OP_FAILED, "remote operation "
                                "failed");
                }
        }

        CLIENT_STACK_UNWIND (readlink, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), rsp.path,
                             &iatt, xdata);

        /* This is allocated by the libc while decoding RPC msg */
        /* Hence no 'GF_FREE', but just 'free' */
        free (rsp.path);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_unlink_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        call_frame_t    *frame      = NULL;
        gfs3_unlink_rsp  rsp        = {0,};
        struct iatt      preparent  = {0,};
        struct iatt      postparent = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_unlink_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_unlink (this, &rsp, &preparent, &postparent,
                                  &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                if (gf_error_to_errno(rsp.op_errno) == ENOENT) {
                        gf_msg_debug (this->name, 0, "remote operation failed:"
                                      " %s", strerror
                                      (gf_error_to_errno (rsp.op_errno)));
                } else {
                        gf_msg (this->name, GF_LOG_WARNING,
                                gf_error_to_errno (rsp.op_errno),
                                PC_MSG_REMOTE_OP_FAILED, "remote operation "
                                "failed");
                }
        }

        CLIENT_STACK_UNWIND (unlink, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &preparent,
                             &postparent, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_rmdir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        gfs3_rmdir_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  preparent  = {0,};
        struct iatt  postparent = {0,};
        int ret = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_rmdir_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_rmdir (this, &rsp, &preparent, &postparent,
                                 &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                if (GF_IGNORE_IF_GSYNCD_SAFE_ERROR(frame, rsp.op_errno)) {
                        gf_msg (this->name, GF_LOG_WARNING,
                                gf_error_to_errno (rsp.op_errno),
                                PC_MSG_REMOTE_OP_FAILED,
                                "remote operation failed");
                }
        }
        CLIENT_STACK_UNWIND (rmdir, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &preparent,
                             &postparent, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}


int
client3_3_truncate_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        gfs3_truncate_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  prestat  = {0,};
        struct iatt  poststat = {0,};
        int ret = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_truncate_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_truncate (this, &rsp, &prestat, &poststat,
                                    &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (truncate, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}


int
client3_3_statfs_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        gfs3_statfs_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct statvfs  statfs = {0,};
        int ret = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_statfs_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_statfs (this, &rsp, &statfs, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (statfs, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &statfs, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}


int
client3_3_writev_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        gfs3_write_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  prestat  = {0,};
        struct iatt  poststat = {0,};
        int ret = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;
        clnt_local_t    *local = NULL;


        this = THIS;

        frame = myframe;
        local = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_write_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_writev (this, &rsp, &prestat, &poststat, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        } else if (rsp.op_ret >= 0) {
                if (local->attempt_reopen)
                        client_attempt_reopen (local->fd, this);
        }
        CLIENT_STACK_UNWIND (writev, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_flush_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        call_frame_t    *frame      = NULL;
        clnt_local_t  *local      = NULL;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;

        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;

        frame = myframe;
        this  = THIS;
        local = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_common_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (rsp.op_ret >= 0 && !fd_is_anonymous (local->fd)) {
                /* Delete all saved locks of the owner issuing flush */
                ret = delete_granted_locks_owner (local->fd, &local->owner);
                gf_msg_trace (this->name, 0,
                              "deleting locks of owner (%s) returned %d",
                              lkowner_utoa (&local->owner), ret);
        }

        ret = client_post_flush (this, &rsp, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, fop_log_level (GF_FOP_FLUSH,
                        gf_error_to_errno (rsp.op_errno)),
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (flush, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_fsync_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        gfs3_fsync_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  prestat  = {0,};
        struct iatt  poststat = {0,};
        int ret = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_fsync_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_fsync (this, &rsp, &prestat, &poststat,
                                 &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (fsync, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_setxattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t  *frame    = NULL;
        gf_common_rsp  rsp      = {0,};
        int            ret      = 0;
        xlator_t      *this     = NULL;
        dict_t        *xdata    = NULL;
        int            op_errno = EINVAL;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_common_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_setxattr (this, &rsp, &xdata);
        if (ret < 0)
                goto out;

out:
        op_errno = gf_error_to_errno (rsp.op_errno);
        if (rsp.op_ret == -1) {
                if (op_errno == ENOTSUP) {
                        gf_msg_debug (this->name, 0, "remote operation failed:"
                                      " %s", strerror (op_errno));
                } else {
                        gf_msg (this->name, GF_LOG_WARNING, op_errno,
                                PC_MSG_REMOTE_OP_FAILED, "remote operation "
                                "failed");
                }
        }

        CLIENT_STACK_UNWIND (setxattr, frame, rsp.op_ret, op_errno, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_getxattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t      *frame    = NULL;
        dict_t            *dict     = NULL;
        int                op_errno = EINVAL;
        gfs3_getxattr_rsp  rsp      = {0,};
        int                ret      = 0;
        clnt_local_t    *local    = NULL;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;
        local = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret = -1;
                op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_getxattr_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        op_errno = gf_error_to_errno (rsp.op_errno);
        ret = client_post_getxattr (this, &rsp, &dict, &xdata);
        if (ret) {
                op_errno = -ret;
                goto out;
        }

out:
        if (rsp.op_ret == -1) {
                if ((op_errno == ENOTSUP) || (op_errno == ENODATA) ||
                     (op_errno == ESTALE) || (op_errno == ENOENT)) {
                        gf_msg_debug (this->name, 0,
                                      "remote operation failed: %s. Path: %s "
                                      "(%s). Key: %s", strerror (op_errno),
                                      local->loc.path,
                                      loc_gfid_utoa (&local->loc),
                                      (local->name) ? local->name : "(null)");
                } else {
                        gf_msg (this->name, GF_LOG_WARNING, op_errno,
                                PC_MSG_REMOTE_OP_FAILED, "remote operation "
                                "failed. Path: %s (%s). Key: %s",
                                local->loc.path,
                                loc_gfid_utoa (&local->loc),
                                (local->name) ? local->name : "(null)");
                }
        }

        CLIENT_STACK_UNWIND (getxattr, frame, rsp.op_ret, op_errno, dict, xdata);

        /* don't use GF_FREE, this memory was allocated by libc */
        free (rsp.dict.dict_val);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        if (dict)
                dict_unref (dict);

        return 0;
}

int
client3_3_fgetxattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
        call_frame_t       *frame    = NULL;
        dict_t             *dict     = NULL;
        gfs3_fgetxattr_rsp  rsp      = {0,};
        int                 ret      = 0;
        int                 op_errno = EINVAL;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret = -1;
                op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_fgetxattr_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        op_errno = gf_error_to_errno (rsp.op_errno);
        ret = client_post_fgetxattr (this, &rsp, &dict, &xdata);
        if (ret) {
                op_errno = -ret;
                goto out;
        }
out:
        if (rsp.op_ret == -1) {
                if ((op_errno == ENOTSUP) || (op_errno == ERANGE) ||
                     (op_errno == ENODATA) || (op_errno == ENOENT)) {
                        gf_msg_debug (this->name, 0,
                                      "remote operation failed: %s",
                                      strerror (op_errno));
                } else {
                        gf_msg (this->name, GF_LOG_WARNING, op_errno,
                                PC_MSG_REMOTE_OP_FAILED, "remote operation "
                                "failed");
                }
        }

        CLIENT_STACK_UNWIND (fgetxattr, frame, rsp.op_ret, op_errno, dict, xdata);

        free (rsp.dict.dict_val);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        if (dict)
                dict_unref (dict);

        return 0;
}

int
client3_3_removexattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                           void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;
        xlator_t        *this       = NULL;
        dict_t          *xdata      = NULL;
        gf_loglevel_t    loglevel   = GF_LOG_NONE;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_common_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_removexattr (this, &rsp, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                if ((ENODATA == rsp.op_errno) || (ENOATTR == rsp.op_errno))
                        loglevel = GF_LOG_DEBUG;
                else
                        loglevel = GF_LOG_WARNING;

                gf_msg (this->name, loglevel,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }

        CLIENT_STACK_UNWIND (removexattr, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_fremovexattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                            void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_common_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_fremovexattr (this, &rsp, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (fremovexattr, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_fsyncdir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_common_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_fsyncdir (this, &rsp, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (fsyncdir, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_access_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_common_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_access (this, &rsp, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (access, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}


int
client3_3_ftruncate_cbk (struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
        gfs3_ftruncate_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  prestat  = {0,};
        struct iatt  poststat = {0,};
        int ret = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_ftruncate_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_ftruncate (this, &rsp, &prestat, &poststat,
                                     &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (ftruncate, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_fstat_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        gfs3_fstat_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  stat  = {0,};
        int ret = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_fstat_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_fstat (this, &rsp, &stat, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (fstat, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &stat,  xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}


int
client3_3_inodelk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_common_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_inodelk (this, &rsp, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, fop_log_level (GF_FOP_INODELK,
                        gf_error_to_errno (rsp.op_errno)),
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED, "remote operation failed");
        }
        CLIENT_STACK_UNWIND (inodelk, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_finodelk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t  *frame = NULL;
        gf_common_rsp rsp    = {0,};
        int           ret    = 0;
        xlator_t      *this  = NULL;
        dict_t        *xdata = NULL;
        clnt_local_t  *local = NULL;


        frame = myframe;
        this = frame->this;
        local = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_common_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_finodelk (this, &rsp, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, fop_log_level (GF_FOP_FINODELK,
                        gf_error_to_errno (rsp.op_errno)),
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED, "remote operation failed");
        } else if (rsp.op_ret == 0) {
                if (local->attempt_reopen)
                        client_attempt_reopen (local->fd, this);
        }
        CLIENT_STACK_UNWIND (finodelk, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_entrylk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_common_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_entrylk (this, &rsp, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, fop_log_level (GF_FOP_ENTRYLK,
                        gf_error_to_errno (rsp.op_errno)),
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED, "remote operation failed");
        }

        CLIENT_STACK_UNWIND (entrylk, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_fentrylk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_common_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_fentrylk (this, &rsp, &xdata);
        if (ret < 0)
                goto out;

out:
        if ((rsp.op_ret == -1) &&
            (EAGAIN != gf_error_to_errno (rsp.op_errno))) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }

        CLIENT_STACK_UNWIND (fentrylk, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_xattrop_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t     *frame    = NULL;
        dict_t           *dict     = NULL;
        gfs3_xattrop_rsp  rsp      = {0,};
        int               ret      = 0;
        int               op_errno = EINVAL;
        clnt_local_t   *local    = NULL;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;
        local = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret = -1;
                op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_xattrop_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        op_errno = rsp.op_errno;
        ret = client_post_xattrop (this, &rsp, &dict, &xdata);
        if (ret) {
                op_errno = -ret;
                goto out;
        }
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, fop_log_level (GF_FOP_XATTROP, op_errno),
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED, "remote operation failed. "
                        "Path: %s (%s)",
                        local->loc.path, loc_gfid_utoa (&local->loc));
        }

        CLIENT_STACK_UNWIND (xattrop, frame, rsp.op_ret,
                             gf_error_to_errno (op_errno), dict, xdata);

        free (rsp.dict.dict_val);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        if (dict)
                dict_unref (dict);

        return 0;
}

int
client3_3_fxattrop_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t      *frame    = NULL;
        dict_t            *dict     = NULL;
        dict_t            *xdata    = NULL;
        gfs3_fxattrop_rsp  rsp      = {0,};
        int                ret      = 0;
        int                op_errno = 0;
        clnt_local_t    *local    = NULL;
        xlator_t *this       = NULL;

        this = THIS;

        frame = myframe;
        local = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_fxattrop_rsp);
        if (ret < 0) {
                rsp.op_ret = -1;
                op_errno = EINVAL;
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                goto out;
        }
        op_errno = rsp.op_errno;
        ret = client_post_fxattrop (this, &rsp, &dict, &xdata);
        if (ret) {
                rsp.op_ret = -1;
                op_errno = -ret;
                goto out;
        }
out:

        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        } else if (rsp.op_ret == 0) {
                if (local->attempt_reopen)
                        client_attempt_reopen (local->fd, this);
        }
        CLIENT_STACK_UNWIND (fxattrop, frame, rsp.op_ret,
                             gf_error_to_errno (op_errno), dict, xdata);

        free (rsp.dict.dict_val);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        if (dict)
                dict_unref (dict);

        return 0;
}

int
client3_3_fsetxattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
        call_frame_t  *frame    = NULL;
        gf_common_rsp  rsp      = {0,};
        int            ret      = 0;
        xlator_t      *this     = NULL;
        dict_t        *xdata    = NULL;
        int            op_errno = EINVAL;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_common_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_setxattr (this, &rsp, &xdata);
        if (ret < 0)
                goto out;

out:
        op_errno = gf_error_to_errno (rsp.op_errno);
        if (rsp.op_ret == -1) {
                if (op_errno == ENOTSUP) {
                        gf_msg_debug (this->name, 0, "remote operation failed:"
                                      " %s", strerror (op_errno));
                } else {
                        gf_msg (this->name, GF_LOG_WARNING, rsp.op_errno,
                                PC_MSG_REMOTE_OP_FAILED, "remote operation "
                                "failed");
                }
        }

        CLIENT_STACK_UNWIND (fsetxattr, frame, rsp.op_ret, op_errno, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_fsetattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t    *frame      = NULL;
        gfs3_fsetattr_rsp rsp        = {0,};
        struct iatt      prestat    = {0,};
        struct iatt      poststat   = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_fsetattr_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_fsetattr (this, &rsp, &prestat, &poststat, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (fsetattr, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_fallocate_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t    *frame      = NULL;
        gfs3_fallocate_rsp rsp      = {0,};
        struct iatt      prestat    = {0,};
        struct iatt      poststat   = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_fallocate_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_fallocate (this, &rsp, &prestat, &poststat, &xdata);
        if (ret < 0)
                goto out;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, xdata, (rsp.xdata.xdata_val),
                                      (rsp.xdata.xdata_len), ret,
                                      rsp.op_errno, out);

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (fallocate, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_discard_cbk(struct rpc_req *req, struct iovec *iov, int count,
		      void *myframe)
{
        call_frame_t    *frame      = NULL;
        gfs3_discard_rsp rsp      = {0,};
        struct iatt      prestat    = {0,};
        struct iatt      poststat   = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic(*iov, &rsp, (xdrproc_t) xdr_gfs3_discard_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_discard (this, &rsp, &prestat, &poststat, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (discard, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_zerofill_cbk(struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        call_frame_t    *frame         = NULL;
        gfs3_zerofill_rsp rsp          = {0,};
        struct iatt      prestat       = {0,};
        struct iatt      poststat      = {0,};
        int              ret           = 0;
        xlator_t *this                 = NULL;
        dict_t  *xdata                 = NULL;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic(*iov, &rsp, (xdrproc_t) xdr_gfs3_zerofill_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_zerofill (this, &rsp, &prestat, &poststat, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (zerofill, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_ipc_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        call_frame_t    *frame         = NULL;
        gfs3_ipc_rsp     rsp          = {0,};
        int              ret           = 0;
        xlator_t *this                 = NULL;
        dict_t  *xdata                 = NULL;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic(*iov, &rsp, (xdrproc_t) xdr_gfs3_ipc_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_ipc (this, &rsp, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (ipc, frame,
                             rsp.op_ret, gf_error_to_errno (rsp.op_errno),
                             xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}


int
client3_3_seek_cbk (struct rpc_req *req, struct iovec *iov, int count,
                    void *myframe)
{
        call_frame_t          *frame     = NULL;
        struct gfs3_seek_rsp   rsp       = {0,};
        int                    ret       = 0;
        xlator_t              *this      = NULL;
        dict_t                *xdata     = NULL;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_generic(*iov, &rsp, (xdrproc_t) xdr_gfs3_seek_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_seek (this, &rsp, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (seek, frame,
                             rsp.op_ret, gf_error_to_errno (rsp.op_errno),
                             rsp.offset, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_setattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t    *frame      = NULL;
        gfs3_setattr_rsp rsp        = {0,};
        struct iatt      prestat    = {0,};
        struct iatt      poststat   = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_setattr_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_setattr (this, &rsp, &prestat, &poststat, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (setattr, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_create_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        call_frame_t    *frame      = NULL;
        fd_t            *fd         = NULL;
        inode_t         *inode      = NULL;
        struct iatt      stbuf      = {0, };
        struct iatt      preparent  = {0, };
        struct iatt      postparent = {0, };
        int32_t          ret        = -1;
        clnt_local_t    *local      = NULL;
        gfs3_create_rsp  rsp        = {0,};
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;

        this = THIS;

        frame = myframe;
        local = frame->local;
        fd    = local->fd;
        inode = local->loc.inode;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_create_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                ret = client_post_create (this, &rsp, &stbuf,
                                          &preparent, &postparent,
                                          local, &xdata);
                if (ret < 0)
                        goto out;
                ret = client_add_fd_to_saved_fds (frame->this, fd, &local->loc,
                                                  local->flags, rsp.fd, 0);
                if (ret) {
                        rsp.op_ret = -1;
                        rsp.op_errno = -ret;
                        goto out;
                }
        }


out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed. Path: %s",
                        local->loc.path);
        }

        CLIENT_STACK_UNWIND (create, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), fd, inode,
                             &stbuf, &preparent, &postparent, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_rchecksum_cbk (struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
        call_frame_t *frame = NULL;
        gfs3_rchecksum_rsp rsp        = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_rchecksum_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_rchecksum (this, &rsp, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (rchecksum, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno),
                             rsp.weak_checksum,
                             (uint8_t *)rsp.strong_checksum.strong_checksum_val,
                             xdata);

        if (rsp.strong_checksum.strong_checksum_val) {
                /* This is allocated by the libc while decoding RPC msg */
                /* Hence no 'GF_FREE', but just 'free' */
                free (rsp.strong_checksum.strong_checksum_val);
        }

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_lease_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        call_frame_t     *frame      = NULL;
        struct gf_lease   lease      = {0,};
        gfs3_lease_rsp    rsp        = {0,};
        int               ret        = 0;
        xlator_t         *this       = NULL;
        dict_t           *xdata      = NULL;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                gf_msg (this->name, GF_LOG_ERROR, ENOTCONN,
                        PC_MSG_REMOTE_OP_FAILED, "Lease fop failed");
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_lease_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_lease (this, &rsp, &lease, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }

        CLIENT_STACK_UNWIND (lease, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &lease, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_lk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                  void *myframe)
{
        call_frame_t    *frame      = NULL;
        struct gf_flock     lock       = {0,};
        gfs3_lk_rsp      rsp        = {0,};
        int              ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_lk_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (rsp.op_ret >= 0) {
                ret = client_post_lk (this, &rsp, &lock, &xdata);
                if (ret < 0)
                        goto out;
        }

        /* Save the lock to the client lock cache to be able
           to recover in the case of server reboot.*/
        /*
          temporarily
        if (local->cmd == F_SETLK || local->cmd == F_SETLKW) {
                ret = client_add_lock_for_recovery (local->fd, &lock,
                                                    local->owner, local->cmd);
                if (ret < 0) {
                        rsp.op_ret = -1;
                        rsp.op_errno = -ret;
                }
        }
        */

out:
        if ((rsp.op_ret == -1) &&
            (EAGAIN != gf_error_to_errno (rsp.op_errno))) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }

        CLIENT_STACK_UNWIND (lk, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &lock, xdata);

        free (rsp.xdata.xdata_val);

        free (rsp.flock.lk_owner.lk_owner_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_readdir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t     *frame    = NULL;
        gfs3_readdir_rsp  rsp      = {0,};
        int32_t           ret      = 0;
        clnt_local_t     *local    = NULL;
        gf_dirent_t       entries;
        xlator_t         *this     = NULL;
        dict_t           *xdata    = NULL;

        this = THIS;

        frame = myframe;
        local = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_readdir_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        INIT_LIST_HEAD (&entries.list);
        ret = client_post_readdir (this, &rsp, &entries, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed: remote_fd = %d",
                        local->cmd);
        }
        CLIENT_STACK_UNWIND (readdir, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &entries, xdata);

        if (rsp.op_ret != -1) {
                gf_dirent_free (&entries);
        }

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        clnt_readdir_rsp_cleanup (&rsp);

        return 0;
}

int
client3_3_readdirp_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t      *frame = NULL;
        gfs3_readdirp_rsp  rsp   = {0,};
        int32_t            ret   = 0;
        clnt_local_t      *local = NULL;
        gf_dirent_t        entries;
        xlator_t          *this  = NULL;
        dict_t            *xdata = NULL;

        this = THIS;

        frame = myframe;
        local = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_readdirp_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        INIT_LIST_HEAD (&entries.list);
        ret = client_post_readdirp (this, &rsp, local->fd, &entries, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (readdirp, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &entries, xdata);

        if (rsp.op_ret != -1) {
                gf_dirent_free (&entries);
        }
        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        clnt_readdirp_rsp_cleanup (&rsp);

        return 0;
}

int
client3_3_rename_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        call_frame_t     *frame      = NULL;
        gfs3_rename_rsp   rsp        = {0,};
        struct iatt       stbuf      = {0,};
        struct iatt       preoldparent  = {0,};
        struct iatt       postoldparent = {0,};
        struct iatt       prenewparent  = {0,};
        struct iatt       postnewparent = {0,};
        int               ret        = 0;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_rename_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_rename (this, &rsp, &stbuf, &preoldparent,
                            &postoldparent, &prenewparent,
                            &postnewparent, &xdata);
        if (ret < 0)
                goto out;

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (rename, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno),
                             &stbuf, &preoldparent, &postoldparent,
                             &prenewparent, &postnewparent, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_link_cbk (struct rpc_req *req, struct iovec *iov, int count,
                    void *myframe)
{
        call_frame_t     *frame      = NULL;
        gfs3_link_rsp     rsp        = {0,};
        struct iatt       stbuf      = {0,};
        struct iatt       preparent  = {0,};
        struct iatt       postparent = {0,};
        int               ret        = 0;
        clnt_local_t     *local      = NULL;
        inode_t          *inode      = NULL;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;


        this = THIS;

        frame = myframe;

        local = frame->local;
        inode = local->loc.inode;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_link_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_link (this, &rsp, &stbuf, &preparent,
                                &postparent, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                if (GF_IGNORE_IF_GSYNCD_SAFE_ERROR(frame, rsp.op_errno)) {
                        gf_msg (this->name, GF_LOG_WARNING,
                                gf_error_to_errno (rsp.op_errno),
                                PC_MSG_REMOTE_OP_FAILED,
                                "remote operation failed: (%s -> %s)",
                                local->loc.path, local->loc2.path);
                }
        }

        CLIENT_STACK_UNWIND (link, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), inode,
                             &stbuf, &preparent, &postparent, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_opendir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        clnt_local_t      *local = NULL;
        call_frame_t      *frame = NULL;
        fd_t              *fd = NULL;
        int ret = 0;
        gfs3_opendir_rsp  rsp = {0,};
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;

        this = THIS;

        frame = myframe;
        local = frame->local;

        fd    = local->fd;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_opendir_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                ret = client_add_fd_to_saved_fds (frame->this, fd, &local->loc,
                                                  0, rsp.fd, 1);
                if (ret) {
                        rsp.op_ret = -1;
                        rsp.op_errno = -ret;
                        goto out;
                }
        }

        ret = client_post_opendir (this, &rsp, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, fop_log_level (GF_FOP_OPENDIR,
                        gf_error_to_errno (rsp.op_errno)),
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED, "remote operation failed."
                        " Path: %s (%s)",
                        local->loc.path, loc_gfid_utoa (&local->loc));
        }
        CLIENT_STACK_UNWIND (opendir, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), fd, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}


int
client3_3_lookup_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        clnt_local_t    *local      = NULL;
        call_frame_t    *frame      = NULL;
        int              ret        = 0;
        gfs3_lookup_rsp  rsp        = {0,};
        struct iatt      stbuf      = {0,};
        struct iatt      postparent = {0,};
        int              op_errno   = EINVAL;
        dict_t          *xdata      = NULL;
        inode_t         *inode      = NULL;
        xlator_t        *this       = NULL;

        this = THIS;

        frame = myframe;
        local = frame->local;
        inode = local->loc.inode;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_lookup_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                op_errno = EINVAL;
                goto out;
        }

        /* Preserve the op_errno received from the server */
        op_errno = gf_error_to_errno (rsp.op_errno);

        ret = client_post_lookup (this, &rsp, &stbuf, &postparent, &xdata);
        if (ret < 0) {
                /* Don't change the op_errno if the fop failed on server */
                if (rsp.op_ret == 0)
                        op_errno = rsp.op_errno;
                rsp.op_ret = -1;
                goto out;
        }

        if (rsp.op_ret < 0)
                goto out;

        if ((!gf_uuid_is_null (inode->gfid))
            && (gf_uuid_compare (stbuf.ia_gfid, inode->gfid) != 0)) {
                gf_msg_debug (frame->this->name, 0,
                              "gfid changed for %s", local->loc.path);

                rsp.op_ret = -1;
                op_errno = ESTALE;
                if (xdata)
                        ret = dict_set_int32 (xdata, "gfid-changed", 1);

                goto out;
        }

        rsp.op_ret = 0;

out:
        /* Restore the correct op_errno to rsp.op_errno */
        rsp.op_errno = op_errno;
        if (rsp.op_ret == -1) {
                /* any error other than ENOENT */
                if (!(local->loc.name && rsp.op_errno == ENOENT) &&
		    !(rsp.op_errno == ESTALE))
                        gf_msg (this->name, GF_LOG_WARNING, rsp.op_errno,
                                PC_MSG_REMOTE_OP_FAILED, "remote operation "
                                "failed. Path: %s (%s)",
                                local->loc.path,
                                loc_gfid_utoa (&local->loc));
                else
                        gf_msg_trace (this->name, 0, "not found on remote "
                                      "node");

        }

        CLIENT_STACK_UNWIND (lookup, frame, rsp.op_ret, rsp.op_errno, inode,
                             &stbuf, xdata, &postparent);

        if (xdata)
                dict_unref (xdata);

        free (rsp.xdata.xdata_val);

        return 0;
}

int
client3_3_readv_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        call_frame_t   *frame  = NULL;
        struct iobref  *iobref = NULL;
        struct iovec    vector[MAX_IOVEC] = {{0}, };
        struct iatt     stat   = {0,};
        gfs3_read_rsp   rsp    = {0,};
        int             ret    = 0, rspcount = 0;
        clnt_local_t   *local  = NULL;
        xlator_t *this       = NULL;
        dict_t  *xdata       = NULL;

        this = THIS;

        memset (vector, 0, sizeof (vector));

        frame = myframe;
        local = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_read_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = client_post_readv (this, &rsp, &iobref, req->rsp_iobref,
                                 &stat, vector, &req->rsp[1],
                                 &rspcount, &xdata);
        if (ret < 0)
                goto out;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        } else if (rsp.op_ret >= 0) {
                if (local->attempt_reopen)
                        client_attempt_reopen (local->fd, this);
        }
        CLIENT_STACK_UNWIND (readv, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), vector, rspcount,
                             &stat, iobref, xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_release_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t   *frame = NULL;

        frame = myframe;
        STACK_DESTROY (frame->root);
        return 0;
}
int
client3_3_releasedir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                          void *myframe)
{
        call_frame_t   *frame = NULL;

        frame = myframe;
        STACK_DESTROY (frame->root);
        return 0;
}

static int
client3_3_getactivelk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                           void *myframe)
{
        call_frame_t            *frame = NULL;
        gfs3_getactivelk_rsp   rsp   = {0,};
        int32_t                 ret   = 0;
        lock_migration_info_t   locklist;
        xlator_t                *this  = NULL;
        dict_t                  *xdata = NULL;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_getactivelk_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        INIT_LIST_HEAD (&locklist.list);

        if (rsp.op_ret > 0) {
                clnt_unserialize_rsp_locklist (this, &rsp, &locklist);
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, xdata, (rsp.xdata.xdata_val),
                                      (rsp.xdata.xdata_len), ret,
                                      rsp.op_errno, out);

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }

        CLIENT_STACK_UNWIND (getactivelk, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &locklist,
                             xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        clnt_getactivelk_rsp_cleanup (&rsp);

        return 0;
}

static int
client3_3_setactivelk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                             void *myframe)
{
        call_frame_t            *frame = NULL;
        gfs3_getactivelk_rsp     rsp   = {0,};
        int32_t                  ret   = 0;
        xlator_t                *this  = NULL;
        dict_t                  *xdata = NULL;

        this = THIS;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_setactivelk_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, xdata, (rsp.xdata.xdata_val),
                                      (rsp.xdata.xdata_len), ret,
                                      rsp.op_errno, out);

out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }

        CLIENT_STACK_UNWIND (setactivelk, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), xdata);

        free (rsp.xdata.xdata_val);

        if (xdata)
                dict_unref (xdata);

        return 0;
}

int
client3_3_compound_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        gfs3_compound_rsp       rsp              = {0,};
        compound_args_cbk_t     *args_cbk        = NULL;
        call_frame_t            *frame           = NULL;
        xlator_t                *this            = NULL;
        dict_t                  *xdata           = NULL;
        clnt_local_t            *local           = NULL;
        int                     i                = 0;
        int                     length           = 0;
        int                     ret              = -1;

        this = THIS;

        frame = myframe;
        local = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_compound_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        length =  local->length;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, xdata, (rsp.xdata.xdata_val),
                                      (rsp.xdata.xdata_len), rsp.op_ret,
                                       rsp.op_errno, out);

        args_cbk = compound_args_cbk_alloc (length, xdata);
        if (!args_cbk) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOMEM;
                goto out;
        }

        /* TODO: see https://bugzilla.redhat.com/show_bug.cgi?id=1376328 */
        for (i = 0; i < args_cbk->fop_length; i++) {
                ret = client_process_response (frame, this, req, &rsp,
                                               args_cbk, i);
                if (ret) {
                        rsp.op_ret   = -1;
                        rsp.op_errno = -ret;
                        goto out;
                }

        }
        rsp.op_ret = 0;
out:
        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        gf_error_to_errno (rsp.op_errno),
                        PC_MSG_REMOTE_OP_FAILED,
                        "remote operation failed");
        }
        CLIENT_STACK_UNWIND (compound, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), args_cbk, xdata);

        free (rsp.xdata.xdata_val);
        client_compound_rsp_cleanup (&rsp, length);
        free (rsp.compound_rsp_array.compound_rsp_array_val);

        if (xdata)
                dict_unref (xdata);

        compound_args_cbk_cleanup (args_cbk);
        return 0;
}

int
client_fdctx_destroy (xlator_t *this, clnt_fd_ctx_t *fdctx)
{
        clnt_conf_t  *conf        = NULL;
        call_frame_t *fr          = NULL;
        int32_t       ret         = -1;
        char          parent_down = 0;
        fd_lk_ctx_t  *lk_ctx      = NULL;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fdctx, out);

        conf = (clnt_conf_t *) this->private;

        if (fdctx->remote_fd == -1) {
                gf_msg_debug (this->name, 0, "not a valid fd");
                goto out;
        }

        pthread_mutex_lock (&conf->lock);
        {
                parent_down   = conf->parent_down;
                lk_ctx        = fdctx->lk_ctx;
                fdctx->lk_ctx = NULL;
        }
        pthread_mutex_unlock (&conf->lock);

        if (lk_ctx)
                fd_lk_ctx_unref (lk_ctx);

        if (!parent_down)
                rpc_clnt_ref (conf->rpc);
        else
                goto out;

        fr = create_frame (this, this->ctx->pool);
        if (fr == NULL) {
                goto out;
        }

        ret = 0;

        if (fdctx->is_dir) {
                gfs3_releasedir_req  req = {{0,},};
                req.fd = fdctx->remote_fd;
                gf_msg_trace (this->name, 0, "sending releasedir on fd");
                client_submit_request (this, &req, fr, &clnt3_3_fop_prog,
                                       GFS3_OP_RELEASEDIR,
                                       client3_3_releasedir_cbk,
                                       NULL, NULL, 0, NULL, 0, NULL,
                                       (xdrproc_t)xdr_gfs3_releasedir_req);
        } else {
                gfs3_release_req  req = {{0,},};
                req.fd = fdctx->remote_fd;
                gf_msg_trace (this->name, 0, "sending release on fd");
                client_submit_request (this, &req, fr, &clnt3_3_fop_prog,
                                       GFS3_OP_RELEASE,
                                       client3_3_release_cbk, NULL,
                                       NULL, 0, NULL, 0, NULL,
                                       (xdrproc_t)xdr_gfs3_release_req);
        }

        rpc_clnt_unref (conf->rpc);
out:
        if (fdctx) {
                fdctx->remote_fd = -1;
                GF_FREE (fdctx);
        }

        return ret;
}

int32_t
client3_3_releasedir (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        clnt_conf_t         *conf        = NULL;
        clnt_fd_ctx_t       *fdctx       = NULL;
        clnt_args_t         *args        = NULL;
        int64_t              remote_fd   = -1;
        gf_boolean_t         destroy     = _gf_false;

        if (!this || !data)
                goto out;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_del_ctx (args->fd, this);
                if (fdctx != NULL) {
                        remote_fd = fdctx->remote_fd;

                        /* fdctx->remote_fd == -1 indicates a reopen attempt
                           in progress. Just mark ->released = 1 and let
                           reopen_cbk handle releasing
                        */

                        if (remote_fd == -1) {
                                fdctx->released = 1;
                        } else {
                                list_del_init (&fdctx->sfd_pos);
                                destroy = _gf_true;
                        }
                }
        }
        pthread_mutex_unlock (&conf->lock);

        if (destroy)
                client_fdctx_destroy (this, fdctx);

out:

        return 0;
}

int32_t
client3_3_release (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        int64_t           remote_fd     = -1;
        clnt_conf_t      *conf          = NULL;
        clnt_fd_ctx_t    *fdctx         = NULL;
        clnt_args_t      *args          = NULL;
        lk_heal_state_t   lk_heal_state = GF_LK_HEAL_DONE;
        gf_boolean_t      destroy       = _gf_false;

        if (!this || !data)
                goto out;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_del_ctx (args->fd, this);
                if (fdctx != NULL) {
                        remote_fd     = fdctx->remote_fd;
                        lk_heal_state = fdctx->lk_heal_state;

                        /* fdctx->remote_fd == -1 indicates a reopen attempt
                           in progress. Just mark ->released = 1 and let
                           reopen_cbk handle releasing
                        */
                        if (remote_fd == -1) {
                                fdctx->released = 1;
                        } else if (lk_heal_state == GF_LK_HEAL_DONE) {
                                list_del_init (&fdctx->sfd_pos);
                                destroy = _gf_true;
                        }
                }
        }
        pthread_mutex_unlock (&conf->lock);

        if (destroy)
                client_fdctx_destroy (this, fdctx);
out:
        return 0;
}


int32_t
client3_3_lookup (call_frame_t *frame, xlator_t *this,
                  void *data)
{
        clnt_conf_t     *conf              = NULL;
        clnt_local_t    *local             = NULL;
        clnt_args_t     *args              = NULL;
        gfs3_lookup_req  req               = {{0,},};
        int              ret               = 0;
        int              op_errno          = ESTALE;
        data_t          *content           = NULL;
        struct iovec     vector[MAX_IOVEC] = {{0}, };
        int              count             = 0;
        struct iobref   *rsp_iobref        = NULL;
        struct iobuf    *rsp_iobuf         = NULL;
        struct iovec    *rsphdr            = NULL;

        if (!frame || !this || !data)
                goto unwind;

        memset (vector, 0, sizeof (vector));

        conf = this->private;
        args = data;
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        if (!(args->loc && args->loc->inode))
                goto unwind;

        loc_copy (&local->loc, args->loc);
        loc_path (&local->loc, NULL);

        if (args->xdata) {
                content = dict_get (args->xdata, GF_CONTENT_KEY);
                if (content != NULL) {
                        rsp_iobref = iobref_new ();
                        if (rsp_iobref == NULL) {
                                goto unwind;
                        }

                        /* TODO: what is the size we should send ? */
                        /* This change very much depends on quick-read
                           changes */
                        rsp_iobuf = iobuf_get (this->ctx->iobuf_pool);
                        if (rsp_iobuf == NULL) {
                                goto unwind;
                        }

                        iobref_add (rsp_iobref, rsp_iobuf);
                        iobuf_unref (rsp_iobuf);
                        rsphdr = &vector[0];
                        rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
                        rsphdr->iov_len = iobuf_pagesize (rsp_iobuf);
                        count = 1;
                        local->iobref = rsp_iobref;
                        rsp_iobuf = NULL;
                        rsp_iobref = NULL;
                }

        }

        ret = client_pre_lookup (this, &req, args->loc, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_LOOKUP, client3_3_lookup_cbk,
                                     NULL, rsphdr, count,
                                     NULL, 0, local->iobref,
                                     (xdrproc_t)xdr_gfs3_lookup_req);

        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;

unwind:
        CLIENT_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL);

        GF_FREE (req.xdata.xdata_val);

        if (rsp_iobref)
                iobref_unref (rsp_iobref);

        if (rsp_iobuf)
                iobuf_unref (rsp_iobuf);

        return 0;
}

int32_t
client3_3_stat (call_frame_t *frame, xlator_t *this,
                void *data)
{
        clnt_conf_t   *conf     = NULL;
        clnt_args_t   *args     = NULL;
        gfs3_stat_req  req      = {{0,},};
        int            ret      = 0;
        int            op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_stat (this, &req, args->loc, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_STAT, client3_3_stat_cbk, NULL,
                                     NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_stat_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (stat, frame, -1, op_errno, NULL, NULL);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_truncate (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_conf_t       *conf     = NULL;
        clnt_args_t       *args     = NULL;
        gfs3_truncate_req  req      = {{0,},};
        int                ret      = 0;
        int                op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_truncate (this, &req, args->loc, args->offset,
                                   args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_TRUNCATE,
                                     client3_3_truncate_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_truncate_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_ftruncate (call_frame_t *frame, xlator_t *this,
                     void *data)
{
        clnt_args_t        *args     = NULL;
        clnt_conf_t        *conf     = NULL;
        gfs3_ftruncate_req  req      = {{0,},};
        int                 op_errno = EINVAL;
        int                 ret      = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        conf = this->private;

        ret = client_pre_ftruncate (this, &req, args->fd, args->offset,
                                     args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FTRUNCATE,
                                     client3_3_ftruncate_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_ftruncate_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (ftruncate, frame, -1, op_errno, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_access (call_frame_t *frame, xlator_t *this,
                  void *data)
{
        clnt_conf_t     *conf     = NULL;
        clnt_args_t     *args     = NULL;
        gfs3_access_req  req      = {{0,},};
        int              ret      = 0;
        int              op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        conf = this->private;


        ret = client_pre_access (this, &req, args->loc, args->mask,
                                 args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_ACCESS,
                                     client3_3_access_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_access_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (access, frame, -1, op_errno, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_readlink (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_conf_t       *conf              = NULL;
        clnt_args_t       *args              = NULL;
        gfs3_readlink_req  req               = {{0,},};
        int                ret               = 0;
        int                op_errno          = ESTALE;
        clnt_local_t      *local             = NULL;
        struct iobuf      *rsp_iobuf         = NULL;
        struct iobref     *rsp_iobref        = NULL;
        struct iovec      *rsphdr            = NULL;
        int                count             = 0;
        struct iovec       vector[MAX_IOVEC] = {{0}, };

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        conf = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }

        frame->local = local;

        ret = client_pre_readlink (this, &req, args->loc, args->size,
                                   args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        rsp_iobref = iobref_new ();
        if (rsp_iobref == NULL) {
                goto unwind;
        }

        rsp_iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (rsp_iobuf == NULL) {
                goto unwind;
        }

        iobref_add (rsp_iobref, rsp_iobuf);
        iobuf_unref (rsp_iobuf);
        rsphdr = &vector[0];
        rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
        rsphdr->iov_len = iobuf_pagesize (rsp_iobuf);
        count = 1;
        local->iobref = rsp_iobref;
        rsp_iobuf = NULL;
        rsp_iobref = NULL;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_READLINK,
                                     client3_3_readlink_cbk, NULL,
                                     rsphdr, count, NULL, 0,
                                     local->iobref,
                                     (xdrproc_t)xdr_gfs3_readlink_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        if (rsp_iobref != NULL) {
                iobref_unref (rsp_iobref);
        }

        CLIENT_STACK_UNWIND (readlink, frame, -1, op_errno, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_unlink (call_frame_t *frame, xlator_t *this,
                  void *data)
{
        clnt_conf_t     *conf     = NULL;
        clnt_args_t     *args     = NULL;
        gfs3_unlink_req  req      = {{0,},};
        int              ret      = 0;
        int              op_errno = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_unlink (this, &req, args->loc, args->flags,
                                 args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_UNLINK,
                                     client3_3_unlink_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_unlink_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_rmdir (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_conf_t    *conf     = NULL;
        clnt_args_t    *args     = NULL;
        gfs3_rmdir_req  req      = {{0,},};
        int             ret      = 0;
        int             op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_rmdir (this, &req, args->loc, args->flags,
                                args->xdata);

        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_RMDIR, client3_3_rmdir_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_rmdir_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }
        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (rmdir, frame, -1, op_errno, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_symlink (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_local_t     *local    = NULL;
        clnt_conf_t      *conf     = NULL;
        clnt_args_t      *args     = NULL;
        gfs3_symlink_req  req      = {{0,},};
        int               ret      = 0;
        int               op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }

        frame->local = local;

        if (!(args->loc && args->loc->parent))
                goto unwind;

        loc_copy (&local->loc, args->loc);
        loc_path (&local->loc, NULL);

        local->loc2.path = gf_strdup (args->linkname);

        ret = client_pre_symlink (this, &req, args->loc,
                                  args->linkname, args->umask, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_SYMLINK, client3_3_symlink_cbk,
                                     NULL,  NULL, 0, NULL,
                                     0, NULL, (xdrproc_t)xdr_gfs3_symlink_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:

        CLIENT_STACK_UNWIND (symlink, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_rename (call_frame_t *frame, xlator_t *this,
                  void *data)
{
        clnt_conf_t     *conf     = NULL;
        clnt_args_t     *args     = NULL;
        gfs3_rename_req  req      = {{0,},};
        int              ret      = 0;
        int              op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_rename (this, &req, args->oldloc, args->newloc,
                                 args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_RENAME, client3_3_rename_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_rename_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL, NULL);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_link (call_frame_t *frame, xlator_t *this,
                void *data)
{
        clnt_local_t  *local    = NULL;
        clnt_conf_t   *conf     = NULL;
        clnt_args_t   *args     = NULL;
        gfs3_link_req  req      = {{0,},};
        int            ret      = 0;
        int            op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }

        frame->local = local;

        ret = client_pre_link (this, &req, args->oldloc, args->newloc,
                               args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        loc_copy (&local->loc, args->oldloc);
        loc_path (&local->loc, NULL);
        loc_copy (&local->loc2, args->newloc);
        loc_path (&local->loc2, NULL);

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_LINK, client3_3_link_cbk, NULL,
                                     NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_link_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_mknod (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_local_t   *local    = NULL;
        clnt_conf_t    *conf     = NULL;
        clnt_args_t    *args     = NULL;
        gfs3_mknod_req  req      = {{0,},};
        int             ret      = 0;
        int             op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        loc_copy (&local->loc, args->loc);
        loc_path (&local->loc, NULL);

        ret = client_pre_mknod (this, &req, args->loc,
                                args->mode, args->rdev, args->umask,
                                args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_MKNOD, client3_3_mknod_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_mknod_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }
        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (mknod, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_mkdir (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_local_t   *local    = NULL;
        clnt_conf_t    *conf     = NULL;
        clnt_args_t    *args     = NULL;
        gfs3_mkdir_req  req      = {{0,},};
        int             ret      = 0;
        int             op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        if (!args->xdata || !dict_get (args->xdata, "gfid-req")) {
                op_errno = EPERM;
                gf_msg_callingfn (this->name, GF_LOG_WARNING, op_errno,
                                  PC_MSG_GFID_NULL, "mkdir: %s is received "
                                  "without gfid-req %p", args->loc->path,
                                  args->xdata);
                goto unwind;
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        if (!(args->loc && args->loc->parent))
                goto unwind;

        loc_copy (&local->loc, args->loc);
        loc_path (&local->loc, NULL);

        ret = client_pre_mkdir (this, &req, args->loc, args->mode,
                                 args->umask, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_MKDIR, client3_3_mkdir_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_mkdir_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }
        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_create (call_frame_t *frame, xlator_t *this,
                  void *data)
{
        clnt_local_t    *local    = NULL;
        clnt_conf_t     *conf     = NULL;
        clnt_args_t     *args     = NULL;
        gfs3_create_req  req      = {{0,},};
        int              ret      = 0;
        int              op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        local->fd = fd_ref (args->fd);
        local->flags = args->flags;

        loc_copy (&local->loc, args->loc);
        loc_path (&local->loc, NULL);

        ret = client_pre_create (this, &req, args->loc,
                                 args->fd, args->mode,
                                 args->flags, args->umask, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_CREATE, client3_3_create_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_create_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL, NULL);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_open (call_frame_t *frame, xlator_t *this,
                void *data)
{
        clnt_local_t  *local    = NULL;
        clnt_conf_t   *conf     = NULL;
        clnt_args_t   *args     = NULL;
        gfs3_open_req  req      = {{0,},};
        int            ret      = 0;
        int            op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        conf = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        local->flags = args->flags;

        local->fd = fd_ref (args->fd);
        loc_copy (&local->loc, args->loc);
        loc_path (&local->loc, NULL);

        ret = client_pre_open (this, &req, args->loc, args->fd, args->flags,
                               args->xdata);

        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_OPEN, client3_3_open_cbk, NULL,
                                     NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_open_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (open, frame, -1, op_errno, NULL, NULL);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_readv (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_args_t    *args       = NULL;
        clnt_conf_t    *conf       = NULL;
        clnt_local_t   *local      = NULL;
        int             op_errno   = ESTALE;
        gfs3_read_req   req        = {{0,},};
        int             ret        = 0;
        struct iovec    rsp_vec    = {0, };
        struct iobuf   *rsp_iobuf  = NULL;
        struct iobref  *rsp_iobref = NULL;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_readv (this, &req, args->fd, args->size,
                                args->offset, args->flags, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        ret = client_fd_fop_prepare_local (frame, args->fd, req.fd);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        local = frame->local;

        rsp_iobuf = iobuf_get2 (this->ctx->iobuf_pool, args->size);
        if (rsp_iobuf == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        rsp_iobref = iobref_new ();
        if (rsp_iobref == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        iobref_add (rsp_iobref, rsp_iobuf);
        iobuf_unref (rsp_iobuf);

        rsp_vec.iov_base = iobuf_ptr (rsp_iobuf);
        rsp_vec.iov_len = iobuf_pagesize (rsp_iobuf);

        rsp_iobuf = NULL;

        if (args->size > rsp_vec.iov_len) {
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM, PC_MSG_NO_MEMORY,
                        "read-size (%lu) is bigger than iobuf size (%lu)",
                        (unsigned long)args->size,
                        (unsigned long)rsp_vec.iov_len);
                op_errno = EINVAL;
                goto unwind;
        }

        local->iobref = rsp_iobref;
        rsp_iobref = NULL;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_READ, client3_3_readv_cbk, NULL,
                                     NULL, 0, &rsp_vec, 1,
                                     local->iobref,
                                     (xdrproc_t)xdr_gfs3_read_req);
        if (ret) {
                //unwind is done in the cbk
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        if (rsp_iobuf)
                iobuf_unref (rsp_iobuf);

        if (rsp_iobref)
                iobref_unref (rsp_iobref);

        CLIENT_STACK_UNWIND (readv, frame, -1, op_errno, NULL, 0, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_writev (call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_args_t    *args     = NULL;
        clnt_conf_t    *conf     = NULL;
        gfs3_write_req  req      = {{0,},};
        int             op_errno = ESTALE;
        int             ret      = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_writev (this, &req, args->fd, args->size,
                                 args->offset, args->flags, &args->xdata);

        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        ret = client_fd_fop_prepare_local (frame, args->fd, req.fd);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_vec_request (this, &req, frame, conf->fops,
                                         GFS3_OP_WRITE, client3_3_writev_cbk,
                                         args->vector, args->count,
                                         args->iobref,
                                         (xdrproc_t)xdr_gfs3_write_req);
        if (ret) {
                /*
                 * If the lower layers fail to submit a request, they'll also
                 * do the unwind for us (see rpc_clnt_submit), so don't unwind
                 * here in such cases.
                 */
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;

unwind:
        CLIENT_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_flush (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_args_t    *args     = NULL;
        gfs3_flush_req  req      = {{0,},};
        clnt_conf_t    *conf     = NULL;
        clnt_local_t   *local    = NULL;
        int             op_errno = ESTALE;
        int             ret      = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }

        frame->local = local;

        local->fd = fd_ref (args->fd);
        local->owner = frame->root->lk_owner;
        ret = client_pre_flush (this, &req, args->fd, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FLUSH, client3_3_flush_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_flush_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);


        return 0;

unwind:
        CLIENT_STACK_UNWIND (flush, frame, -1, op_errno, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_fsync (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_args_t    *args      = NULL;
        gfs3_fsync_req  req       = {{0,},};
        clnt_conf_t    *conf      = NULL;
        int             op_errno  = 0;
        int             ret       = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_fsync (this, &req, args->fd, args->flags,
                                 args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FSYNC, client3_3_fsync_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_fsync_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");

        }

        GF_FREE (req.xdata.xdata_val);

        return 0;

unwind:
        CLIENT_STACK_UNWIND (fsync, frame, -1, op_errno, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_fstat (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_args_t    *args     = NULL;
        gfs3_fstat_req  req      = {{0,},};
        clnt_conf_t    *conf     = NULL;
        int             op_errno = ESTALE;
        int           ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_fstat (this, &req, args->fd, args->xdata);

        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FSTAT, client3_3_fstat_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_fstat_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;

unwind:
        CLIENT_STACK_UNWIND (fstat, frame, -1, op_errno, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_opendir (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_local_t     *local    = NULL;
        clnt_conf_t      *conf     = NULL;
        clnt_args_t      *args     = NULL;
        gfs3_opendir_req  req      = {{0,},};
        int               ret      = 0;
        int               op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        local->fd = fd_ref (args->fd);
        loc_copy (&local->loc, args->loc);
        loc_path (&local->loc, NULL);

        ret = client_pre_opendir (this, &req, args->loc, args->fd,
                                  args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_OPENDIR, client3_3_opendir_cbk,
                                     NULL, NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_opendir_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;

unwind:
        CLIENT_STACK_UNWIND (opendir, frame, -1, op_errno, NULL, NULL);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_fsyncdir (call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_args_t       *args      = NULL;
        clnt_conf_t       *conf      = NULL;
        gfs3_fsyncdir_req  req       = {{0,},};
        int                ret       = 0;
        int32_t            op_errno  = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_fsyncdir (this, &req, args->fd, args->flags,
                                   args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FSYNCDIR, client3_3_fsyncdir_cbk,
                                     NULL, NULL, 0,
                                     NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_fsyncdir_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;

unwind:
        CLIENT_STACK_UNWIND (fsyncdir, frame, -1, op_errno, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_statfs (call_frame_t *frame, xlator_t *this,
                  void *data)
{
        clnt_conf_t     *conf     = NULL;
        clnt_args_t     *args     = NULL;
        gfs3_statfs_req  req      = {{0,},};
        int              ret      = 0;
        int              op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        conf = this->private;

        ret = client_pre_statfs (this, &req, args->loc, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_STATFS, client3_3_statfs_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_statfs_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;

unwind:
        CLIENT_STACK_UNWIND (statfs, frame, -1, op_errno, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_setxattr (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_conf_t       *conf     = NULL;
        clnt_args_t       *args     = NULL;
        gfs3_setxattr_req  req      = {{0,},};
        int                ret      = 0;
        int                op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_setxattr (this, &req, args->loc, args->xattr,
                                    args->flags, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_SETXATTR, client3_3_setxattr_cbk,
                                     NULL, NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_setxattr_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }
        GF_FREE (req.dict.dict_val);

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);
        GF_FREE (req.dict.dict_val);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_fsetxattr (call_frame_t *frame, xlator_t *this,
                     void *data)
{
        clnt_args_t        *args     = NULL;
        clnt_conf_t        *conf     = NULL;
        gfs3_fsetxattr_req  req      = {{0,},};
        int                 op_errno = ESTALE;
        int                 ret      = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_fsetxattr (this, &req, args->fd, args->flags,
                                     args->xattr, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FSETXATTR, client3_3_fsetxattr_cbk,
                                     NULL, NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_fsetxattr_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.dict.dict_val);

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (fsetxattr, frame, -1, op_errno, NULL);
        GF_FREE (req.dict.dict_val);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_fgetxattr (call_frame_t *frame, xlator_t *this,
                     void *data)
{
        clnt_args_t        *args       = NULL;
        clnt_conf_t        *conf       = NULL;
        gfs3_fgetxattr_req  req        = {{0,},};
        int                 op_errno   = ESTALE;
        int                 ret        = 0;
        int                 count      = 0;
        clnt_local_t       *local      = NULL;
        struct iobref      *rsp_iobref = NULL;
        struct iobuf       *rsp_iobuf  = NULL;
        struct iovec       *rsphdr     = NULL;
        struct iovec        vector[MAX_IOVEC] = {{0}, };

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        rsp_iobref = iobref_new ();
        if (rsp_iobref == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        /* TODO: what is the size we should send ? */
        rsp_iobuf = iobuf_get2 (this->ctx->iobuf_pool, 8 * GF_UNIT_KB);
        if (rsp_iobuf == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        iobref_add (rsp_iobref, rsp_iobuf);
        iobuf_unref (rsp_iobuf);

        rsphdr = &vector[0];
        rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
        rsphdr->iov_len = iobuf_pagesize (rsp_iobuf);;
        count = 1;
        local->iobref = rsp_iobref;
        rsp_iobuf = NULL;
        rsp_iobref = NULL;

        ret = client_pre_fgetxattr (this, &req, args->fd, args->name,
                                    args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FGETXATTR,
                                     client3_3_fgetxattr_cbk, NULL,
                                     rsphdr, count,
                                     NULL, 0, local->iobref,
                                     (xdrproc_t)xdr_gfs3_fgetxattr_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (fgetxattr, frame, -1, op_errno, NULL, NULL);

        if (rsp_iobuf)
                iobuf_unref (rsp_iobuf);

        if (rsp_iobref)
                iobref_unref (rsp_iobref);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_getxattr (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_conf_t       *conf       = NULL;
        clnt_args_t       *args       = NULL;
        gfs3_getxattr_req  req        = {{0,},};
        dict_t            *dict       = NULL;
        int                ret        = 0;
        int32_t            op_ret     = -1;
        int                op_errno   = ESTALE;
        int                count      = 0;
        clnt_local_t      *local      = NULL;
        struct iobref     *rsp_iobref = NULL;
        struct iobuf      *rsp_iobuf  = NULL;
        struct iovec      *rsphdr     = NULL;
        struct iovec       vector[MAX_IOVEC] = {{0}, };

        if (!frame || !this || !data) {
                op_errno = 0;
                goto unwind;
        }
        args = data;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }

        frame->local = local;

        loc_copy (&local->loc, args->loc);
        loc_path (&local->loc, NULL);

        if (args->name)
                local->name = gf_strdup (args->name);

        rsp_iobref = iobref_new ();
        if (rsp_iobref == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        /* TODO: what is the size we should send ? */
        rsp_iobuf = iobuf_get2 (this->ctx->iobuf_pool, 8 * GF_UNIT_KB);
        if (rsp_iobuf == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        iobref_add (rsp_iobref, rsp_iobuf);
        iobuf_unref (rsp_iobuf);

        rsphdr = &vector[0];
        rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
        rsphdr->iov_len = iobuf_pagesize (rsp_iobuf);
        count = 1;
        local->iobref = rsp_iobref;
        rsp_iobuf = NULL;
        rsp_iobref = NULL;

        conf = this->private;

        if (args && args->name) {
                if (is_client_dump_locks_cmd ((char *)args->name)) {
                        dict = dict_new ();
                        ret = client_dump_locks ((char *)args->name,
                                                 args->loc->inode,
                                                 dict);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                                        PC_MSG_INVALID_ENTRY, "Client dump "
                                        "locks failed");
                                op_errno = EINVAL;
                        }

                        GF_ASSERT (dict);
                        op_ret = 0;
                        op_errno = 0;
                        goto unwind;
                }
        }

        ret = client_pre_getxattr (this, &req, args->loc, args->name,
                                   args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_GETXATTR,
                                     client3_3_getxattr_cbk, NULL,
                                     rsphdr, count,
                                     NULL, 0, local->iobref,
                                     (xdrproc_t)xdr_gfs3_getxattr_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        if (rsp_iobuf)
                iobuf_unref (rsp_iobuf);

        if (rsp_iobref)
                iobref_unref (rsp_iobref);

        CLIENT_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict, NULL);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_xattrop (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_conf_t      *conf       = NULL;
        clnt_args_t      *args       = NULL;
        gfs3_xattrop_req  req        = {{0,},};
        int               ret        = 0;
        int               op_errno   = ESTALE;
        int               count      = 0;
        clnt_local_t   *local      = NULL;
        struct iobref    *rsp_iobref = NULL;
        struct iobuf     *rsp_iobuf  = NULL;
        struct iovec     *rsphdr     = NULL;
        struct iovec      vector[MAX_IOVEC] = {{0}, };

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        if (!(args->loc && args->loc->inode))
                goto unwind;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        rsp_iobref = iobref_new ();
        if (rsp_iobref == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        /* TODO: what is the size we should send ? */
        rsp_iobuf = iobuf_get2 (this->ctx->iobuf_pool, 8 * GF_UNIT_KB);
        if (rsp_iobuf == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        iobref_add (rsp_iobref, rsp_iobuf);
        iobuf_unref (rsp_iobuf);

        rsphdr = &vector[0];
        rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
        rsphdr->iov_len = iobuf_pagesize (rsp_iobuf);
        count = 1;
        local->iobref = rsp_iobref;
        rsp_iobuf = NULL;
        rsp_iobref = NULL;

        loc_copy (&local->loc, args->loc);
        loc_path (&local->loc, NULL);
        conf = this->private;

        ret = client_pre_xattrop (this, &req, args->loc, args->xattr,
                                  args->flags, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_XATTROP,
                                     client3_3_xattrop_cbk, NULL,
                                     rsphdr, count,
                                     NULL, 0, local->iobref,
                                     (xdrproc_t)xdr_gfs3_xattrop_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.dict.dict_val);

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (xattrop, frame, -1, op_errno, NULL, NULL);

        GF_FREE (req.dict.dict_val);

        if (rsp_iobuf)
                iobuf_unref (rsp_iobuf);

        if (rsp_iobref)
                iobref_unref (rsp_iobref);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_fxattrop (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_args_t       *args       = NULL;
        clnt_conf_t       *conf       = NULL;
        clnt_local_t      *local      = NULL;
        gfs3_fxattrop_req  req        = {{0,},};
        int                op_errno   = ESTALE;
        int                ret        = 0;
        int                count      = 0;
        struct iobref     *rsp_iobref = NULL;
        struct iobuf      *rsp_iobuf  = NULL;
        struct iovec      *rsphdr     = NULL;
        struct iovec       vector[MAX_IOVEC] = {{0}, };

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_fxattrop (this, &req, args->fd, args->xattr,
                                   args->flags, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_fd_fop_prepare_local (frame, args->fd, req.fd);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        local = frame->local;

        rsp_iobref = iobref_new ();
        if (rsp_iobref == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        /* TODO: what is the size we should send ? */
        rsp_iobuf = iobuf_get2 (this->ctx->iobuf_pool, 8 * GF_UNIT_KB);
        if (rsp_iobuf == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        iobref_add (rsp_iobref, rsp_iobuf);
        iobuf_unref (rsp_iobuf);
        rsphdr = &vector[0];
        rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
        rsphdr->iov_len = iobuf_pagesize (rsp_iobuf);
        count = 1;
        local->iobref = rsp_iobref;
        rsp_iobuf = NULL;
        rsp_iobref = NULL;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FXATTROP,
                                     client3_3_fxattrop_cbk, NULL,
                                     rsphdr, count,
                                     NULL, 0, local->iobref,
                                     (xdrproc_t)xdr_gfs3_fxattrop_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.dict.dict_val);

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (fxattrop, frame, -1, op_errno, NULL, NULL);

        GF_FREE (req.dict.dict_val);

        if (rsp_iobref)
                iobref_unref (rsp_iobref);

        if (rsp_iobuf)
                iobuf_unref (rsp_iobuf);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_removexattr (call_frame_t *frame, xlator_t *this,
                       void *data)
{
        clnt_conf_t          *conf     = NULL;
        clnt_args_t          *args     = NULL;
        gfs3_removexattr_req  req      = {{0,},};
        int                   ret      = 0;
        int                   op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_removexattr (this, &req, args->loc, args->name,
                                       args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_REMOVEXATTR,
                                     client3_3_removexattr_cbk, NULL,
                                     NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_removexattr_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (removexattr, frame, -1, op_errno, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_fremovexattr (call_frame_t *frame, xlator_t *this,
                        void *data)
{
        clnt_conf_t           *conf     = NULL;
        clnt_args_t           *args     = NULL;
        gfs3_fremovexattr_req  req      = {{0,},};
        int                    ret      = 0;
        int                    op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        conf = this->private;

        ret = client_pre_fremovexattr (this, &req, args->fd, args->name,
                                        args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FREMOVEXATTR,
                                     client3_3_fremovexattr_cbk, NULL,
                                     NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_fremovexattr_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (fremovexattr, frame, -1, op_errno, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_lease (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_args_t     *args       = NULL;
        gfs3_lease_req   req        = {{0,},};
        clnt_conf_t     *conf       = NULL;
        int              op_errno   = ESTALE;
        int              ret        = 0;

        GF_VALIDATE_OR_GOTO ("client", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);
        GF_VALIDATE_OR_GOTO (this->name, data, unwind);

        args = data;
        conf = this->private;

        ret = client_pre_lease (this, &req, args->loc, args->lease,
                                args->xdata);
        if (ret < 0) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops, GFS3_OP_LEASE,
                                     client3_3_lease_cbk, NULL,
                                     NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_lease_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (lease, frame, -1, op_errno, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_lk (call_frame_t *frame, xlator_t *this,
              void *data)
{
        clnt_args_t     *args       = NULL;
        gfs3_lk_req      req        = {{0,},};
        int32_t          gf_cmd     = 0;
        clnt_local_t    *local      = NULL;
        clnt_conf_t     *conf       = NULL;
        int              op_errno   = ESTALE;
        int              ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        ret = client_cmd_to_gf_cmd (args->cmd, &gf_cmd);
        if (ret) {
                op_errno = EINVAL;
                gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                        PC_MSG_INVALID_ENTRY, "Unknown cmd (%d)!", gf_cmd);
                goto unwind;
        }

        local->owner = frame->root->lk_owner;
        local->cmd   = args->cmd;
        local->fd    = fd_ref (args->fd);

        ret = client_pre_lk (this, &req, args->cmd, args->flock,
                             args->fd, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops, GFS3_OP_LK,
                                     client3_3_lk_cbk, NULL,
                                     NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_lk_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (lk, frame, -1, op_errno, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_inodelk (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_conf_t      *conf     = NULL;
        clnt_args_t      *args    = NULL;
        gfs3_inodelk_req  req     = {{0,},};
        int               ret     = 0;
        int               op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_inodelk (this, &req, args->loc, args->cmd,
                                   args->flock, args->volume, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_INODELK,
                                     client3_3_inodelk_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_inodelk_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (inodelk, frame, -1, op_errno, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_finodelk (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_args_t       *args     = NULL;
        gfs3_finodelk_req  req      = {{0,},};
        clnt_conf_t       *conf     = NULL;
        int                op_errno = ESTALE;
        int                ret      = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_finodelk (this, &req, args->fd,
                                    args->cmd, args->flock, args->volume,
                                    args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        ret = client_fd_fop_prepare_local (frame, args->fd, req.fd);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FINODELK,
                                     client3_3_finodelk_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_finodelk_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);
        return 0;
unwind:
        CLIENT_STACK_UNWIND (finodelk, frame, -1, op_errno, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_entrylk (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_conf_t      *conf     = NULL;
        clnt_args_t      *args     = NULL;
        gfs3_entrylk_req  req      = {{0,},};
        int               ret      = 0;
        int               op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        conf = this->private;

        ret = client_pre_entrylk (this, &req, args->loc, args->cmd_entrylk,
                                   args->type, args->volume, args->basename,
                                   args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_ENTRYLK,
                                     client3_3_entrylk_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_entrylk_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (entrylk, frame, -1, op_errno, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}



int32_t
client3_3_fentrylk (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_args_t       *args     = NULL;
        gfs3_fentrylk_req  req      = {{0,},};
        clnt_conf_t       *conf     = NULL;
        int                op_errno = ESTALE;
        int           ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_fentrylk (this, &req, args->fd, args->cmd_entrylk,
                                    args->type, args->volume, args->basename,
                                    args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FENTRYLK,
                                     client3_3_fentrylk_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_fentrylk_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (fentrylk, frame, -1, op_errno, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_rchecksum (call_frame_t *frame, xlator_t *this,
                     void *data)
{
        clnt_args_t        *args     = NULL;
        clnt_conf_t        *conf     = NULL;
        gfs3_rchecksum_req  req      = {0,};
        int                 op_errno = ESTALE;
        int                 ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_rchecksum (this, &req, args->fd, args->len,
                                     args->offset, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_RCHECKSUM,
                                     client3_3_rchecksum_cbk, NULL,
                                     NULL, 0, NULL,
                                     0, NULL,
                                     (xdrproc_t)xdr_gfs3_rchecksum_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (rchecksum, frame, -1, op_errno, 0, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_readdir (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_args_t      *args       = NULL;
        int64_t           remote_fd  = -1;
        clnt_conf_t      *conf       = NULL;
        gfs3_readdir_req  req        = {{0,},};
        gfs3_readdir_rsp  rsp        = {0, };
        clnt_local_t     *local      = NULL;
        int               op_errno   = ESTALE;
        int               ret        = 0;
        int               count      = 0;
        struct iobref    *rsp_iobref = NULL;
        struct iobuf     *rsp_iobuf  = NULL;
        struct iovec     *rsphdr     = NULL;
        struct iovec      vector[MAX_IOVEC] = {{0}, };
        int               readdir_rsp_size  = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        readdir_rsp_size = xdr_sizeof ((xdrproc_t) xdr_gfs3_readdir_rsp, &rsp)
                + args->size;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        local->cmd = remote_fd;

        if ((readdir_rsp_size + GLUSTERFS_RPC_REPLY_SIZE + GLUSTERFS_RDMA_MAX_HEADER_SIZE)
            > (GLUSTERFS_RDMA_INLINE_THRESHOLD)) {
                rsp_iobref = iobref_new ();
                if (rsp_iobref == NULL) {
                        goto unwind;
                }

                /* TODO: what is the size we should send ? */
                /* This iobuf will live for only receiving the response,
                   so not harmful */
                rsp_iobuf = iobuf_get (this->ctx->iobuf_pool);
                if (rsp_iobuf == NULL) {
                        goto unwind;
                }

                iobref_add (rsp_iobref, rsp_iobuf);
                iobuf_unref (rsp_iobuf);

                rsphdr = &vector[0];
                rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
                rsphdr->iov_len  = iobuf_pagesize (rsp_iobuf);
                count = 1;
                local->iobref = rsp_iobref;
                rsp_iobuf = NULL;
                rsp_iobref = NULL;
        }

        ret = client_pre_readdir (this, &req, args->fd, args->size,
                                  args->offset, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_READDIR,
                                     client3_3_readdir_cbk, NULL,
                                     rsphdr, count,
                                     NULL, 0, rsp_iobref,
                                     (xdrproc_t)xdr_gfs3_readdir_req);

        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;

unwind:
        if (rsp_iobref)
                iobref_unref (rsp_iobref);

        if (rsp_iobuf)
                iobuf_unref (rsp_iobuf);

        CLIENT_STACK_UNWIND (readdir, frame, -1, op_errno, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}


int32_t
client3_3_readdirp (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_args_t      *args              = NULL;
        gfs3_readdirp_req req               = {{0,},};
        gfs3_readdirp_rsp rsp               = {0,};
        clnt_conf_t      *conf              = NULL;
        int               op_errno          = ESTALE;
        int               ret               = 0;
        int               count             = 0;
        int               readdirp_rsp_size = 0;
        struct iobref    *rsp_iobref        = NULL;
        struct iobuf     *rsp_iobuf         = NULL;
        struct iovec     *rsphdr            = NULL;
        struct iovec      vector[MAX_IOVEC] = {{0}, };
        clnt_local_t     *local             = NULL;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        ret = client_pre_readdirp (this, &req, args->fd, args->size,
                                   args->offset, args->xdata);

        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        readdirp_rsp_size = xdr_sizeof ((xdrproc_t) xdr_gfs3_readdirp_rsp, &rsp)
                + args->size;

        if ((readdirp_rsp_size + GLUSTERFS_RPC_REPLY_SIZE
             + GLUSTERFS_RDMA_MAX_HEADER_SIZE)
            > (GLUSTERFS_RDMA_INLINE_THRESHOLD)) {
                rsp_iobref = iobref_new ();
                if (rsp_iobref == NULL) {
                        goto unwind;
                }

                /* TODO: what is the size we should send ? */
                /* This iobuf will live for only receiving the response,
                   so not harmful */
                rsp_iobuf = iobuf_get (this->ctx->iobuf_pool);
                if (rsp_iobuf == NULL) {
                        goto unwind;
                }

                iobref_add (rsp_iobref, rsp_iobuf);
                iobuf_unref (rsp_iobuf);

                rsphdr = &vector[0];
                rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
                rsphdr->iov_len  = iobuf_pagesize (rsp_iobuf);
                count = 1;
                local->iobref = rsp_iobref;
                rsp_iobuf = NULL;
                rsp_iobref = NULL;
        }

        local->fd = fd_ref (args->fd);

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_READDIRP,
                                     client3_3_readdirp_cbk, NULL,
                                     rsphdr, count, NULL,
                                     0, rsp_iobref,
                                     (xdrproc_t)xdr_gfs3_readdirp_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.dict.dict_val);

        return 0;
unwind:
        if (rsp_iobref)
                iobref_unref (rsp_iobref);

        if (rsp_iobuf)
                iobuf_unref (rsp_iobuf);

        GF_FREE (req.dict.dict_val);

        CLIENT_STACK_UNWIND (readdirp, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
client3_3_setattr (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_conf_t      *conf     = NULL;
        clnt_args_t      *args     = NULL;
        gfs3_setattr_req  req      = {{0,},};
        int               ret      = 0;
        int               op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_setattr (this, &req, args->loc, args->valid,
                                  args->stbuf, args->xdata);

        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_SETATTR,
                                     client3_3_setattr_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_setattr_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (setattr, frame, -1, op_errno, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_fsetattr (call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_args_t       *args     = NULL;
        clnt_conf_t       *conf     = NULL;
        gfs3_fsetattr_req  req      = {0,};
        int                op_errno = ESTALE;
        int                ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_fsetattr (this, &req, args->fd, args->valid,
                                   args->stbuf, args->xdata);

        if (ret) {
                op_errno = -ret;
                goto unwind;
        }
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FSETATTR,
                                     client3_3_fsetattr_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_fsetattr_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (fsetattr, frame, -1, op_errno, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_fallocate(call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_args_t       *args     = NULL;
        clnt_conf_t       *conf     = NULL;
	gfs3_fallocate_req req	    = {{0},};
        int                op_errno = ESTALE;
        int                ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_fallocate (this, &req, args->fd, args->flags,
                                     args->offset, args->size, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FALLOCATE,
                                     client3_3_fallocate_cbk, NULL,
                                     NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gfs3_fallocate_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (fallocate, frame, -1, op_errno, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_discard(call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_args_t       *args     = NULL;
        clnt_conf_t       *conf     = NULL;
	gfs3_discard_req   req	    = {{0},};
        int                op_errno = ESTALE;
        int                ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_discard (this, &req, args->fd, args->offset,
                                  args->size, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }


        ret = client_submit_request(this, &req, frame, conf->fops,
                                    GFS3_OP_DISCARD, client3_3_discard_cbk,
				    NULL, NULL, 0, NULL, 0, NULL,
                                    (xdrproc_t) xdr_gfs3_discard_req);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND(discard, frame, -1, op_errno, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_zerofill(call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_args_t       *args        = NULL;
        clnt_conf_t       *conf        = NULL;
        gfs3_zerofill_req   req        = {{0},};
        int                op_errno    = ESTALE;
        int                ret         = 0;

        GF_ASSERT (frame);

        if (!this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_zerofill (this, &req, args->fd, args->offset,
                                   args->size, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        ret = client_submit_request(this, &req, frame, conf->fops,
                                    GFS3_OP_ZEROFILL, client3_3_zerofill_cbk,
                                    NULL, NULL, 0, NULL, 0, NULL,
                                    (xdrproc_t) xdr_gfs3_zerofill_req);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND(zerofill, frame, -1, op_errno, NULL, NULL, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_ipc (call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_args_t       *args        = NULL;
        clnt_conf_t       *conf        = NULL;
        gfs3_ipc_req       req        = {0,};
        int                op_errno    = ESTALE;
        int                ret         = 0;

        GF_ASSERT (frame);

        if (!this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_ipc (this, &req, args->cmd, args->xdata);

        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        ret = client_submit_request(this, &req, frame, conf->fops,
                                    GFS3_OP_IPC, client3_3_ipc_cbk,
                                    NULL, NULL, 0, NULL, 0, NULL,
                                    (xdrproc_t) xdr_gfs3_ipc_req);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND(ipc, frame, -1, op_errno, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

int32_t
client3_3_seek (call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_args_t            *args        = NULL;
        clnt_conf_t            *conf        = NULL;
        struct gfs3_seek_req    req         = {{0,},};
        int                     op_errno    = ESTALE;
        int                     ret         = 0;

        GF_ASSERT (frame);

        if (!this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        ret = client_pre_seek (this, &req, args->fd,
                               args->offset, args->what, args->xdata);
        if (ret) {
                op_errno = -ret;
                goto unwind;
        }

        ret = client_submit_request(this, &req, frame, conf->fops,
                                    GFS3_OP_SEEK, client3_3_seek_cbk,
                                    NULL, NULL, 0, NULL, 0, NULL,
                                    (xdrproc_t) xdr_gfs3_seek_req);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND(ipc, frame, -1, op_errno, NULL);
        GF_FREE (req.xdata.xdata_val);

        return 0;
}

/* Brief explanation of gfs3_compound_req structure :
 * 1) It consists of version of compounding.
 * 2) A compound-fop enum, new enum for compound fops
 * 3) A 'compound_req_arrray' structure that has
 *      a) array len - based on the number of fops compounded
 *      b) compound_req_array_val - pointer to an array of compound_req's
 * 4) compound_req - structure that contains:
 *      a) fop enum of type glusterfs_fop_t
 *      b) union of structures of xdr requests of all fops.
 */

int32_t
client3_3_compound (call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_conf_t             *conf              = NULL;
        compound_args_t         *c_args            = data;
        gfs3_compound_req       req                = {0,};
        clnt_local_t            *local             = NULL;
        int                     op_errno           = ENOMEM;
        int                     ret                = 0;
        int                     i                  = 0;
        int                     rsp_count          = 0;
        struct iovec            rsp_vector[MAX_IOVEC] = {{0}, };
        struct iovec            req_vector[MAX_IOVEC] = {{0}, };
        struct iovec            vector[MAX_IOVEC] = {{0}, };
        struct iovec            *rsphdr             = NULL;
        struct iobref           *req_iobref         = NULL;
        struct iobref           *rsp_iobref         = NULL;
        struct iobref           *rsphdr_iobref      = NULL;
        struct iobuf            *rsphdr_iobuf       = NULL;
        int                     rsphdr_count        = 0;
        int                     req_count           = 0;
        dict_t                  *xdata              = c_args->xdata;

        GF_ASSERT (frame);

        if (!this || !data)
                goto unwind;

        memset (req_vector, 0, sizeof (req_vector));
        memset (rsp_vector, 0, sizeof (rsp_vector));

        conf = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        local->length = c_args->fop_length;
        local->compound_args = c_args;

        rsphdr_iobref = iobref_new ();
        if (rsphdr_iobref == NULL) {
                goto unwind;
        }

        /* TODO: what is the size we should send ? */
        rsphdr_iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (rsphdr_iobuf == NULL) {
                goto unwind;
        }

        iobref_add (rsphdr_iobref, rsphdr_iobuf);
        iobuf_unref (rsphdr_iobuf);
        rsphdr = &vector[0];
        rsphdr->iov_base = iobuf_ptr (rsphdr_iobuf);
        rsphdr->iov_len = iobuf_pagesize (rsphdr_iobuf);
        rsphdr_count = 1;
        rsphdr_iobuf = NULL;

        req.compound_fop_enum = c_args->fop_enum;
        req.compound_req_array.compound_req_array_len = c_args->fop_length;
        req.compound_version = 0;
        if (xdata) {
                GF_PROTOCOL_DICT_SERIALIZE (this, xdata,
                                            (&req.xdata.xdata_val),
                                            req.xdata.xdata_len,
                                            op_errno, unwind);
        }

        req.compound_req_array.compound_req_array_val = GF_CALLOC (local->length,
                                                        sizeof (compound_req),
                                                        gf_client_mt_compound_req_t);

        if (!req.compound_req_array.compound_req_array_val) {
                op_errno = ENOMEM;
                goto unwind;
        }

        for (i = 0; i < local->length; i++) {
                ret = client_handle_fop_requirements (this, frame,
                                                      &req, local,
                                                      &req_iobref, &rsp_iobref,
                                                      req_vector,
                                                      rsp_vector, &req_count,
                                                      &rsp_count,
                                                      &c_args->req_list[i],
                                                      c_args->enum_list[i],
                                                      i);
                if (ret) {
                        op_errno = ret;
                        goto unwind;
                }
        }

        local->iobref = rsp_iobref;
        rsp_iobref     = NULL;

        ret = client_submit_compound_request (this, &req, frame, conf->fops,
                                     GFS3_OP_COMPOUND, client3_3_compound_cbk,
                                     req_vector, req_count, req_iobref,
                                     rsphdr, rsphdr_count,
                                     rsp_vector, rsp_count,
                                     local->iobref,
                                     (xdrproc_t) xdr_gfs3_compound_req);

        GF_FREE (req.xdata.xdata_val);

        iobref_unref (rsphdr_iobref);

        compound_request_cleanup (&req);
        return 0;
unwind:
        CLIENT_STACK_UNWIND (compound, frame, -1, op_errno, NULL, NULL);

        if (rsp_iobref)
                iobref_unref (rsp_iobref);

        if (rsphdr_iobref)
                iobref_unref (rsphdr_iobref);

        GF_FREE (req.xdata.xdata_val);

        compound_request_cleanup (&req);
        return 0;
}

static int32_t
client3_3_getactivelk (call_frame_t *frame, xlator_t *this,
                        void *data)
{
        clnt_conf_t   *conf             = NULL;
        clnt_args_t   *args             = NULL;
        gfs3_getactivelk_req  req      = {{0,},};
        int            ret              = 0;
        int            op_errno         = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        if (!(args->loc && args->loc->inode))
                goto unwind;

        if (!gf_uuid_is_null (args->loc->inode->gfid))
                memcpy (req.gfid,  args->loc->inode->gfid, 16);
        else
                memcpy (req.gfid, args->loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                       !gf_uuid_is_null (*((uuid_t *)req.gfid)),
                                       unwind, op_errno, EINVAL);
        conf = this->private;

        GF_PROTOCOL_DICT_SERIALIZE (this, args->xdata, (&req.xdata.xdata_val),
                                    req.xdata.xdata_len, op_errno, unwind);

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_GETACTIVELK,
                                     client3_3_getactivelk_cbk, NULL,
                                     NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_getactivelk_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }

        GF_FREE (req.xdata.xdata_val);

        return 0;
unwind:
        CLIENT_STACK_UNWIND (getactivelk, frame, -1, op_errno, NULL, NULL);

        GF_FREE (req.xdata.xdata_val);

        return 0;
}

static int32_t
client3_3_setactivelk (call_frame_t *frame, xlator_t *this,
                        void *data)
{
        clnt_conf_t   *conf             = NULL;
        clnt_args_t   *args             = NULL;
        gfs3_setactivelk_req  req      = {{0,},};
        int            ret              = 0;
        int            op_errno         = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        if (!(args->loc && args->loc->inode && args->locklist))
                goto unwind;

        if (!gf_uuid_is_null (args->loc->inode->gfid))
                memcpy (req.gfid,  args->loc->inode->gfid, 16);
        else
                memcpy (req.gfid, args->loc->gfid, 16);

        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name,
                                       !gf_uuid_is_null (*((uuid_t *)req.gfid)),
                                       unwind, op_errno, EINVAL);
        conf = this->private;

        GF_PROTOCOL_DICT_SERIALIZE (this, args->xdata, (&req.xdata.xdata_val),
                                    req.xdata.xdata_len, op_errno, unwind);

        ret = serialize_req_locklist (args->locklist, &req);

        if (ret)
                goto unwind;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_SETACTIVELK, client3_3_setactivelk_cbk, NULL,
                                     NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_setactivelk_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_FOP_SEND_FAILED,
                        "failed to send the fop");
        }


        clnt_setactivelk_req_cleanup (&req);

        GF_FREE (req.xdata.xdata_val);

        return 0;

unwind:

        CLIENT_STACK_UNWIND (setactivelk, frame, -1, op_errno, NULL);

        GF_FREE (req.xdata.xdata_val);

        clnt_setactivelk_req_cleanup (&req);

        return 0;
}

/* Table Specific to FOPS */
rpc_clnt_procedure_t clnt3_3_fop_actors[GF_FOP_MAXVALUE] = {
        [GF_FOP_NULL]         = { "NULL",         NULL},
        [GF_FOP_STAT]         = { "STAT",         client3_3_stat },
        [GF_FOP_READLINK]     = { "READLINK",     client3_3_readlink },
        [GF_FOP_MKNOD]        = { "MKNOD",        client3_3_mknod },
        [GF_FOP_MKDIR]        = { "MKDIR",        client3_3_mkdir },
        [GF_FOP_UNLINK]       = { "UNLINK",       client3_3_unlink },
        [GF_FOP_RMDIR]        = { "RMDIR",        client3_3_rmdir },
        [GF_FOP_SYMLINK]      = { "SYMLINK",      client3_3_symlink },
        [GF_FOP_RENAME]       = { "RENAME",       client3_3_rename },
        [GF_FOP_LINK]         = { "LINK",         client3_3_link },
        [GF_FOP_TRUNCATE]     = { "TRUNCATE",     client3_3_truncate },
        [GF_FOP_OPEN]         = { "OPEN",         client3_3_open },
        [GF_FOP_READ]         = { "READ",         client3_3_readv },
        [GF_FOP_WRITE]        = { "WRITE",        client3_3_writev },
        [GF_FOP_STATFS]       = { "STATFS",       client3_3_statfs },
        [GF_FOP_FLUSH]        = { "FLUSH",        client3_3_flush },
        [GF_FOP_FSYNC]        = { "FSYNC",        client3_3_fsync },
        [GF_FOP_SETXATTR]     = { "SETXATTR",     client3_3_setxattr },
        [GF_FOP_GETXATTR]     = { "GETXATTR",     client3_3_getxattr },
        [GF_FOP_REMOVEXATTR]  = { "REMOVEXATTR",  client3_3_removexattr },
        [GF_FOP_OPENDIR]      = { "OPENDIR",      client3_3_opendir },
        [GF_FOP_FSYNCDIR]     = { "FSYNCDIR",     client3_3_fsyncdir },
        [GF_FOP_ACCESS]       = { "ACCESS",       client3_3_access },
        [GF_FOP_CREATE]       = { "CREATE",       client3_3_create },
        [GF_FOP_FTRUNCATE]    = { "FTRUNCATE",    client3_3_ftruncate },
        [GF_FOP_FSTAT]        = { "FSTAT",        client3_3_fstat },
        [GF_FOP_LK]           = { "LK",           client3_3_lk },
        [GF_FOP_LOOKUP]       = { "LOOKUP",       client3_3_lookup },
        [GF_FOP_READDIR]      = { "READDIR",      client3_3_readdir },
        [GF_FOP_INODELK]      = { "INODELK",      client3_3_inodelk },
        [GF_FOP_FINODELK]     = { "FINODELK",     client3_3_finodelk },
        [GF_FOP_ENTRYLK]      = { "ENTRYLK",      client3_3_entrylk },
        [GF_FOP_FENTRYLK]     = { "FENTRYLK",     client3_3_fentrylk },
        [GF_FOP_XATTROP]      = { "XATTROP",      client3_3_xattrop },
        [GF_FOP_FXATTROP]     = { "FXATTROP",     client3_3_fxattrop },
        [GF_FOP_FGETXATTR]    = { "FGETXATTR",    client3_3_fgetxattr },
        [GF_FOP_FSETXATTR]    = { "FSETXATTR",    client3_3_fsetxattr },
        [GF_FOP_RCHECKSUM]    = { "RCHECKSUM",    client3_3_rchecksum },
        [GF_FOP_SETATTR]      = { "SETATTR",      client3_3_setattr },
        [GF_FOP_FSETATTR]     = { "FSETATTR",     client3_3_fsetattr },
        [GF_FOP_READDIRP]     = { "READDIRP",     client3_3_readdirp },
	[GF_FOP_FALLOCATE]    = { "FALLOCATE",	  client3_3_fallocate },
	[GF_FOP_DISCARD]      = { "DISCARD",	  client3_3_discard },
        [GF_FOP_ZEROFILL]     = { "ZEROFILL",     client3_3_zerofill},
        [GF_FOP_RELEASE]      = { "RELEASE",      client3_3_release },
        [GF_FOP_RELEASEDIR]   = { "RELEASEDIR",   client3_3_releasedir },
        [GF_FOP_GETSPEC]      = { "GETSPEC",      client3_getspec },
        [GF_FOP_FREMOVEXATTR] = { "FREMOVEXATTR", client3_3_fremovexattr },
        [GF_FOP_IPC]          = { "IPC",          client3_3_ipc },
        [GF_FOP_SEEK]         = { "SEEK",         client3_3_seek },
        [GF_FOP_LEASE]        = { "LEASE",        client3_3_lease },
        [GF_FOP_GETACTIVELK]  = { "GETACTIVELK", client3_3_getactivelk},
        [GF_FOP_SETACTIVELK]  = { "SETACTIVELK", client3_3_setactivelk},
        [GF_FOP_COMPOUND]     = { "COMPOUND",     client3_3_compound },
};

/* Used From RPC-CLNT library to log proper name of procedure based on number */
char *clnt3_3_fop_names[GFS3_OP_MAXVALUE] = {
        [GFS3_OP_NULL]        = "NULL",
        [GFS3_OP_STAT]        = "STAT",
        [GFS3_OP_READLINK]    = "READLINK",
        [GFS3_OP_MKNOD]       = "MKNOD",
        [GFS3_OP_MKDIR]       = "MKDIR",
        [GFS3_OP_UNLINK]      = "UNLINK",
        [GFS3_OP_RMDIR]       = "RMDIR",
        [GFS3_OP_SYMLINK]     = "SYMLINK",
        [GFS3_OP_RENAME]      = "RENAME",
        [GFS3_OP_LINK]        = "LINK",
        [GFS3_OP_TRUNCATE]    = "TRUNCATE",
        [GFS3_OP_OPEN]        = "OPEN",
        [GFS3_OP_READ]        = "READ",
        [GFS3_OP_WRITE]       = "WRITE",
        [GFS3_OP_STATFS]      = "STATFS",
        [GFS3_OP_FLUSH]       = "FLUSH",
        [GFS3_OP_FSYNC]       = "FSYNC",
        [GFS3_OP_SETXATTR]    = "SETXATTR",
        [GFS3_OP_GETXATTR]    = "GETXATTR",
        [GFS3_OP_REMOVEXATTR] = "REMOVEXATTR",
        [GFS3_OP_OPENDIR]     = "OPENDIR",
        [GFS3_OP_FSYNCDIR]    = "FSYNCDIR",
        [GFS3_OP_ACCESS]      = "ACCESS",
        [GFS3_OP_CREATE]      = "CREATE",
        [GFS3_OP_FTRUNCATE]   = "FTRUNCATE",
        [GFS3_OP_FSTAT]       = "FSTAT",
        [GFS3_OP_LK]          = "LK",
        [GFS3_OP_LOOKUP]      = "LOOKUP",
        [GFS3_OP_READDIR]     = "READDIR",
        [GFS3_OP_INODELK]     = "INODELK",
        [GFS3_OP_FINODELK]    = "FINODELK",
        [GFS3_OP_ENTRYLK]     = "ENTRYLK",
        [GFS3_OP_FENTRYLK]    = "FENTRYLK",
        [GFS3_OP_XATTROP]     = "XATTROP",
        [GFS3_OP_FXATTROP]    = "FXATTROP",
        [GFS3_OP_FGETXATTR]   = "FGETXATTR",
        [GFS3_OP_FSETXATTR]   = "FSETXATTR",
        [GFS3_OP_RCHECKSUM]   = "RCHECKSUM",
        [GFS3_OP_SETATTR]     = "SETATTR",
        [GFS3_OP_FSETATTR]    = "FSETATTR",
        [GFS3_OP_READDIRP]    = "READDIRP",
        [GFS3_OP_RELEASE]     = "RELEASE",
        [GFS3_OP_RELEASEDIR]  = "RELEASEDIR",
        [GFS3_OP_FREMOVEXATTR] = "FREMOVEXATTR",
	[GFS3_OP_FALLOCATE]   = "FALLOCATE",
	[GFS3_OP_DISCARD]     = "DISCARD",
        [GFS3_OP_ZEROFILL]    = "ZEROFILL",
        [GFS3_OP_IPC]         = "IPC",
        [GFS3_OP_SEEK]        = "SEEK",
        [GFS3_OP_LEASE]       = "LEASE",
        [GFS3_OP_GETACTIVELK] = "GETACTIVELK",
        [GFS3_OP_SETACTIVELK] = "SETACTIVELK",
        [GFS3_OP_COMPOUND]    = "COMPOUND",
};

rpc_clnt_prog_t clnt3_3_fop_prog = {
        .progname  = "GlusterFS 3.3",
        .prognum   = GLUSTER_FOP_PROGRAM,
        .progver   = GLUSTER_FOP_VERSION,
        .numproc   = GLUSTER_FOP_PROCCNT,
        .proctable = clnt3_3_fop_actors,
        .procnames = clnt3_3_fop_names,
};
