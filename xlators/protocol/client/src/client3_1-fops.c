/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "client.h"
#include "glusterfs3-xdr.h"
#include "glusterfs3.h"
#include "compat-errno.h"

int32_t client3_getspec (call_frame_t *frame, xlator_t *this, void *data);
void client_start_ping (void *data);
rpc_clnt_prog_t clnt3_1_fop_prog;

int
client_submit_vec_request (xlator_t  *this, void *req, call_frame_t  *frame,
                           rpc_clnt_prog_t *prog, int procnum, fop_cbk_fn_t cbk,
                           struct iovec  *payload, int payloadcnt,
                           struct iobref *iobref, gfs_serialize_t sfunc)
{
        int            ret        = 0;
        clnt_conf_t   *conf       = NULL;
        struct iovec   iov        = {0, };
        struct iobuf  *iobuf      = NULL;
        int            count      = 0;
        int            start_ping = 0;
        struct iobref *new_iobref = NULL;

        start_ping = 0;

        conf = this->private;

        iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (!iobuf) {
                goto out;
        };

        new_iobref = iobref_new ();
        if (!new_iobref) {
                goto out;
        }

        if (iobref != NULL) {
                ret = iobref_merge (new_iobref, iobref);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "cannot merge iobref passed from caller into "
                                "new_iobref");
                        goto out;
                }
        }

        ret = iobref_add (new_iobref, iobuf);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot add iobuf into iobref");
                goto out;
        }

        iov.iov_base = iobuf->ptr;
        iov.iov_len  = 128 * GF_UNIT_KB;

        /* Create the xdr payload */
        if (req && sfunc) {
                ret = sfunc (iov, req);
                if (ret == -1) {
                        goto out;
                }

                iov.iov_len = ret;
                count = 1;
        }
        /* Send the msg */
        ret = rpc_clnt_submit (conf->rpc, prog, procnum, cbk, &iov, count,
                               payload, payloadcnt, new_iobref, frame, NULL, 0,
                               NULL, 0, NULL);

        if (ret == 0) {
                pthread_mutex_lock (&conf->rpc->conn.lock);
                {
                        if (!conf->rpc->conn.ping_started) {
                                start_ping = 1;
                        }
                }
                pthread_mutex_unlock (&conf->rpc->conn.lock);
        }

        if (start_ping)
                client_start_ping ((void *) this);

out:
        if (new_iobref != NULL) {
                iobref_unref (new_iobref);
        }

        iobuf_unref (iobuf);

        return 0;
}

/* CBK */

int
client3_1_symlink_cbk (struct rpc_req *req, struct iovec *iov, int count,
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

        frame = myframe;

        local = frame->local;
        frame->local = NULL;
        inode = local->loc.inode;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_symlink_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.stat, &stbuf);

                gf_stat_to_iatt (&rsp.preparent, &preparent);
                gf_stat_to_iatt (&rsp.postparent, &postparent);
        }

out:
        frame->local = NULL;
        STACK_UNWIND_STRICT (symlink, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), inode, &stbuf,
                             &preparent, &postparent);

        if (local)
                client_local_wipe (local);

        return 0;
}


int
client3_1_mknod_cbk (struct rpc_req *req, struct iovec *iov, int count,
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

        frame = myframe;

        local = frame->local;
        frame->local = NULL;
        inode = local->loc.inode;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_mknod_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.stat, &stbuf);

                gf_stat_to_iatt (&rsp.preparent, &preparent);
                gf_stat_to_iatt (&rsp.postparent, &postparent);
        }

out:
        frame->local = NULL;
        STACK_UNWIND_STRICT (mknod, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), inode,
                             &stbuf, &preparent, &postparent);

        if (local)
                client_local_wipe (local);

        return 0;
}

int
client3_1_mkdir_cbk (struct rpc_req *req, struct iovec *iov, int count,
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

        frame = myframe;

        local = frame->local;
        frame->local = NULL;
        inode = local->loc.inode;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_mkdir_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.stat, &stbuf);

                gf_stat_to_iatt (&rsp.preparent, &preparent);
                gf_stat_to_iatt (&rsp.postparent, &postparent);
        }

out:
        STACK_UNWIND_STRICT (mkdir, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), inode,
                             &stbuf, &preparent, &postparent);

        if (local)
                client_local_wipe (local);

        return 0;
}

int
client3_1_open_cbk (struct rpc_req *req, struct iovec *iov, int count,
                    void *myframe)
{
        clnt_local_t  *local = NULL;
        clnt_conf_t   *conf  = NULL;
        clnt_fd_ctx_t *fdctx = NULL;
        call_frame_t  *frame = NULL;
        fd_t          *fd    = NULL;
        int            ret   = 0;
        gfs3_open_rsp  rsp   = {0,};

        frame = myframe;
        local = frame->local;

        frame->local = NULL;
        conf  = frame->this->private;
        fd    = local->fd;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_open_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                fdctx = GF_CALLOC (1, sizeof (*fdctx),
                                   gf_client_mt_clnt_fdctx_t);
                if (!fdctx) {
                        rsp.op_ret = -1;
                        rsp.op_errno = ENOMEM;
                        goto out;
                }

                fdctx->remote_fd = rsp.fd;
                fdctx->inode     = inode_ref (fd->inode);
                fdctx->flags     = local->flags;
                fdctx->wbflags   = local->wbflags;

                INIT_LIST_HEAD (&fdctx->sfd_pos);
                INIT_LIST_HEAD (&fdctx->lock_list);

                this_fd_set_ctx (fd, frame->this, &local->loc, fdctx);

                pthread_mutex_lock (&conf->lock);
                {
                        list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
                }
                pthread_mutex_unlock (&conf->lock);
        }

out:
        frame->local = NULL;
        STACK_UNWIND_STRICT (open, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), fd);

        client_local_wipe (local);

        return 0;
}


int
client3_1_stat_cbk (struct rpc_req *req, struct iovec *iov, int count,
                    void *myframe)
{
        gfs3_stat_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  iatt = {0,};
        int ret = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_stat_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.stat, &iatt);
        }

out:
        STACK_UNWIND_STRICT (stat, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &iatt);

        return 0;
}

int
client3_1_readlink_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        gfs3_readlink_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  iatt = {0,};
        int ret = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_readlink_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.buf, &iatt);
        }

out:
        STACK_UNWIND_STRICT (readlink, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), rsp.path, &iatt);

        /* This is allocated by the libc while decoding RPC msg */
        /* Hence no 'GF_FREE', but just 'free' */
        if (rsp.path)
                free (rsp.path);

        return 0;
}

int
client3_1_unlink_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        call_frame_t    *frame      = NULL;
        gfs3_unlink_rsp  rsp        = {0,};
        struct iatt      preparent  = {0,};
        struct iatt      postparent = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_unlink_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.preparent, &preparent);
                gf_stat_to_iatt (&rsp.postparent, &postparent);
        }

out:
        STACK_UNWIND_STRICT (unlink, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &preparent,
                             &postparent);

        return 0;
}

int
client3_1_rmdir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        gfs3_rmdir_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  preparent  = {0,};
        struct iatt  postparent = {0,};
        int ret = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_rmdir_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.preparent, &preparent);
                gf_stat_to_iatt (&rsp.postparent, &postparent);
        }

out:
        STACK_UNWIND_STRICT (rmdir, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &preparent,
                             &postparent);

        return 0;
}


int
client3_1_truncate_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        gfs3_truncate_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  prestat  = {0,};
        struct iatt  poststat = {0,};
        int ret = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_truncate_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.prestat, &prestat);
                gf_stat_to_iatt (&rsp.poststat, &poststat);
        }

out:
        STACK_UNWIND_STRICT (truncate, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat);

        return 0;
}


int
client3_1_statfs_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        gfs3_statfs_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct statvfs  statfs = {0,};
        int ret = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_statfs_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_statfs_to_statfs (&rsp.statfs, &statfs);
        }

out:
        STACK_UNWIND_STRICT (statfs, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &statfs);

        return 0;
}


int
client3_1_writev_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        gfs3_write_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  prestat  = {0,};
        struct iatt  poststat = {0,};
        int ret = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_truncate_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.prestat, &prestat);
                gf_stat_to_iatt (&rsp.poststat, &poststat);
        }

out:
        STACK_UNWIND_STRICT (writev, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat);

        return 0;
}

int
client3_1_flush_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        call_frame_t    *frame      = NULL;
        clnt_local_t  *local      = NULL;
        xlator_t        *this       = NULL;
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
        ret = xdr_to_common_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (rsp.op_ret >= 0) {
                /* Delete all saved locks of the owner issuing flush */
                gf_log (this->name, GF_LOG_DEBUG,
                        "Attempting to delete locks of owner=%llu",
                        (long long unsigned) local->owner);
                delete_granted_locks_owner (local->fd, local->owner);
        }

        frame->local = NULL;
        if (local)
                client_local_wipe (local);

out:
        STACK_UNWIND_STRICT (flush, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno));

        return 0;
}

int
client3_1_fsync_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        gfs3_fsync_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  prestat  = {0,};
        struct iatt  poststat = {0,};
        int ret = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_truncate_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.prestat, &prestat);
                gf_stat_to_iatt (&rsp.poststat, &poststat);
        }

out:
        STACK_UNWIND_STRICT (fsync, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat);

        return 0;
}

int
client3_1_setxattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_common_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:
        STACK_UNWIND_STRICT (setxattr, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno));

        return 0;
}

int
client3_1_getxattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t      *frame    = NULL;
        dict_t            *dict     = NULL;
        char              *buf      = NULL;
        int                dict_len = 0;
        int                op_ret   = 0;
        int                op_errno = EINVAL;
        gfs3_getxattr_rsp  rsp      = {0,};
        int                ret      = 0;
        clnt_local_t    *local    = NULL;

        frame = myframe;
        local = frame->local;
        frame->local = NULL;

        if (-1 == req->rpc_status) {
                op_ret   = -1;
                op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_getxattr_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                op_ret   = -1;
                op_errno = EINVAL;
                goto out;
        }

        op_errno = gf_error_to_errno (rsp.op_errno);
        op_ret = rsp.op_ret;
        if (-1 != op_ret) {
                op_ret = -1;
                dict_len = rsp.dict.dict_len;

                if (dict_len > 0) {
                        dict = dict_new();
                        buf = memdup (rsp.dict.dict_val, rsp.dict.dict_len);

                        GF_VALIDATE_OR_GOTO (frame->this->name, dict, out);
                        GF_VALIDATE_OR_GOTO (frame->this->name, buf, out);

                        ret = dict_unserialize (buf, dict_len, &dict);
                        if (ret < 0) {
                                gf_log (frame->this->name, GF_LOG_DEBUG,
                                        "failed to unserialize xattr dict");
                                op_errno = EINVAL;
                                goto out;
                        }
                        dict->extra_free = buf;
                        buf = NULL;
                }
                op_ret = 0;
        }

out:
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict);

        if (rsp.dict.dict_val) {
                /* don't use GF_FREE, this memory was allocated by libc
                 */
                free (rsp.dict.dict_val);
                rsp.dict.dict_val = NULL;
        }

        if (buf)
                GF_FREE (buf);

        if (dict)
                dict_unref (dict);

        client_local_wipe (local);

        return 0;
}

int
client3_1_fgetxattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
        call_frame_t       *frame    = NULL;
        char               *buf      = NULL;
        dict_t             *dict     = NULL;
        gfs3_fgetxattr_rsp  rsp      = {0,};
        int                 ret      = 0;
        int                 dict_len = 0;
        int                 op_ret   = 0;
        int                 op_errno = EINVAL;
        clnt_local_t     *local    = NULL;

        frame = myframe;
        local = frame->local;
        frame->local = NULL;

        if (-1 == req->rpc_status) {
                op_ret   = -1;
                op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_fgetxattr_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                op_ret   = -1;
                op_errno = EINVAL;
                goto out;
        }

        op_errno = gf_error_to_errno (rsp.op_errno);
        op_ret = rsp.op_ret;
        if (-1 != op_ret) {
                op_ret = -1;
                dict_len = rsp.dict.dict_len;

                if (dict_len > 0) {
                        dict = dict_new();
                        GF_VALIDATE_OR_GOTO (frame->this->name, dict, out);
                        buf = memdup (rsp.dict.dict_val, rsp.dict.dict_len);
                        GF_VALIDATE_OR_GOTO (frame->this->name, buf, out);

                        ret = dict_unserialize (buf, dict_len, &dict);
                        if (ret < 0) {
                                gf_log (frame->this->name, GF_LOG_DEBUG,
                                        "failed to unserialize xattr dict");
                                op_errno = EINVAL;
                                goto out;
                        }
                        dict->extra_free = buf;
                        buf = NULL;
                }
                op_ret = 0;
        }
out:
        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict);
        if (rsp.dict.dict_val) {
                /* don't use GF_FREE, this memory was allocated by libc
                 */
                free (rsp.dict.dict_val);
                rsp.dict.dict_val = NULL;
        }

        if (buf)
                GF_FREE (buf);

        if (dict)
                dict_unref (dict);

        if (local)
                client_local_wipe (local);

        return 0;
}

int
client3_1_removexattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                           void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_common_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:
        STACK_UNWIND_STRICT (removexattr, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno));

        return 0;
}

int
client3_1_fsyncdir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_common_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:
        STACK_UNWIND_STRICT (fsyncdir, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno));

        return 0;
}

int
client3_1_access_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_common_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:
        STACK_UNWIND_STRICT (access, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno));

        return 0;
}


int
client3_1_ftruncate_cbk (struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
        gfs3_ftruncate_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  prestat  = {0,};
        struct iatt  poststat = {0,};
        int ret = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_ftruncate_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.prestat, &prestat);
                gf_stat_to_iatt (&rsp.poststat, &poststat);
        }

out:
        STACK_UNWIND_STRICT (ftruncate, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat);

        return 0;
}

int
client3_1_fstat_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        gfs3_fstat_rsp rsp = {0,};
        call_frame_t   *frame = NULL;
        struct iatt  stat  = {0,};
        int ret = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_fstat_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.stat, &stat);
        }

out:
        STACK_UNWIND_STRICT (fstat, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &stat);

        return 0;
}


int
client3_1_inodelk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_common_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:
        STACK_UNWIND_STRICT (inodelk, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno));

        return 0;
}

int
client3_1_finodelk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_common_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:
        STACK_UNWIND_STRICT (finodelk, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno));

        return 0;
}

int
client3_1_entrylk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_common_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:

        STACK_UNWIND_STRICT (entrylk, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno));

        return 0;
}

int
client3_1_fentrylk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_common_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:
        STACK_UNWIND_STRICT (fentrylk, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno));

        return 0;
}

int
client3_1_xattrop_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t     *frame    = NULL;
        dict_t           *dict     = NULL;
        char             *buf      = NULL;
        gfs3_xattrop_rsp  rsp      = {0,};
        int               ret      = 0;
        int               op_ret   = 0;
        int               dict_len = 0;
        int               op_errno = EINVAL;
        clnt_local_t   *local    = NULL;

        frame = myframe;
        local = frame->local;
        frame->local = NULL;

        if (-1 == req->rpc_status) {
                op_ret   = -1;
                op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_xattrop_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                op_ret   = -1;
                op_errno = EINVAL;
                goto out;
        }

        op_errno = rsp.op_errno;
        op_ret   = rsp.op_ret;
        if (-1 != op_ret) {
                op_ret = -1;
                dict_len = rsp.dict.dict_len;

                if (dict_len > 0) {
                        dict = dict_new();
                        GF_VALIDATE_OR_GOTO (frame->this->name, dict, out);

                        buf = memdup (rsp.dict.dict_val, rsp.dict.dict_len);
                        GF_VALIDATE_OR_GOTO (frame->this->name, buf, out);
                        op_ret = dict_unserialize (buf, dict_len, &dict);
                        if (op_ret < 0) {
                                gf_log (frame->this->name, GF_LOG_DEBUG,
                                        "failed to unserialize xattr dict");
                                op_errno = EINVAL;
                                goto out;
                        }
                        dict->extra_free = buf;
                        buf = NULL;
                }
                op_ret = 0;
        }

out:

        STACK_UNWIND_STRICT (xattrop, frame, op_ret,
                             gf_error_to_errno (op_errno), dict);

        if (rsp.dict.dict_val) {
                /* don't use GF_FREE, this memory was allocated by libc
                 */
                free (rsp.dict.dict_val);
                rsp.dict.dict_val = NULL;
        }

        if (buf)
                GF_FREE (buf);

        if (dict)
                dict_unref (dict);

        client_local_wipe (local);

        return 0;
}

int
client3_1_fxattrop_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t      *frame    = NULL;
        dict_t            *dict     = NULL;
        char              *buf      = NULL;
        gfs3_fxattrop_rsp  rsp      = {0,};
        int                ret      = 0;
        int                op_ret   = 0;
        int                dict_len = 0;
        int                op_errno = 0;
        clnt_local_t    *local    = NULL;

        frame = myframe;
        local = frame->local;
        frame->local = NULL;

        if (-1 == req->rpc_status) {
                op_ret   = -1;
                op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_fxattrop_rsp (*iov, &rsp);
        if (ret < 0) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }
        op_errno = rsp.op_errno;
        op_ret   = rsp.op_ret;
        if (-1 != op_ret) {
                op_ret = -1;
                dict_len = rsp.dict.dict_len;

                if (dict_len > 0) {
                        dict = dict_new();
                        GF_VALIDATE_OR_GOTO (frame->this->name, dict, out);

                        buf = memdup (rsp.dict.dict_val, rsp.dict.dict_len);
                        GF_VALIDATE_OR_GOTO (frame->this->name, buf, out);
                        op_ret = dict_unserialize (buf, dict_len, &dict);
                        if (op_ret < 0) {
                                gf_log (frame->this->name, GF_LOG_DEBUG,
                                        "failed to unserialize xattr dict");
                                op_errno = EINVAL;
                                goto out;
                        }
                        dict->extra_free = buf;
                        buf = NULL;
                }
                op_ret = 0;
        }

out:

        STACK_UNWIND_STRICT (fxattrop, frame, op_ret,
                             gf_error_to_errno (op_errno), dict);

        if (rsp.dict.dict_val) {
                /* don't use GF_FREE, this memory was allocated by libc
                 */
                free (rsp.dict.dict_val);
                rsp.dict.dict_val = NULL;
        }

        if (buf)
                GF_FREE (buf);

        if (dict)
                dict_unref (dict);

        client_local_wipe (local);
        return 0;
}

int
client3_1_fsetxattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
        call_frame_t    *frame      = NULL;
        gf_common_rsp    rsp        = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_common_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:
        STACK_UNWIND_STRICT (fsetxattr, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno));

        return 0;
}

int
client3_1_fsetattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t    *frame      = NULL;
        gfs3_fsetattr_rsp rsp        = {0,};
        struct iatt      prestat    = {0,};
        struct iatt      poststat   = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }
        ret = xdr_to_fsetattr_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.statpre, &prestat);
                gf_stat_to_iatt (&rsp.statpost, &poststat);
        }

out:
        STACK_UNWIND_STRICT (fsetattr, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat);

        return 0;
}


int
client3_1_setattr_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t    *frame      = NULL;
        gfs3_setattr_rsp rsp        = {0,};
        struct iatt      prestat    = {0,};
        struct iatt      poststat   = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_setattr_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.statpre, &prestat);
                gf_stat_to_iatt (&rsp.statpost, &poststat);
        }

out:
        STACK_UNWIND_STRICT (setattr, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &prestat,
                             &poststat);

        return 0;
}

int
client3_1_create_cbk (struct rpc_req *req, struct iovec *iov, int count,
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
        clnt_conf_t     *conf       = NULL;
        clnt_fd_ctx_t   *fdctx      = NULL;
        gfs3_create_rsp  rsp        = {0,};

        frame = myframe;
        local = frame->local; frame->local = NULL;
        conf  = frame->this->private;
        fd    = local->fd;
        inode = local->loc.inode;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_create_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.stat, &stbuf);

                gf_stat_to_iatt (&rsp.preparent, &preparent);
                gf_stat_to_iatt (&rsp.postparent, &postparent);

                fdctx = GF_CALLOC (1, sizeof (*fdctx),
                                   gf_client_mt_clnt_fdctx_t);
                if (!fdctx) {
                        rsp.op_ret = -1;
                        rsp.op_errno = ENOMEM;
                        goto out;
                }

                fdctx->remote_fd = rsp.fd;
                fdctx->inode     = inode_ref (inode);
                fdctx->flags     = local->flags;

                INIT_LIST_HEAD (&fdctx->sfd_pos);
                INIT_LIST_HEAD (&fdctx->lock_list);

                this_fd_set_ctx (fd, frame->this, &local->loc, fdctx);

                pthread_mutex_lock (&conf->lock);
                {
                        list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
                }
                pthread_mutex_unlock (&conf->lock);
        }

out:
        frame->local = NULL;
        STACK_UNWIND_STRICT (create, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), fd, inode,
                             &stbuf, &preparent, &postparent);

        client_local_wipe (local);
        return 0;
}


int
client3_1_rchecksum_cbk (struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
        call_frame_t *frame = NULL;
        gfs3_rchecksum_rsp rsp        = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_rchecksum_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:
        STACK_UNWIND_STRICT (rchecksum, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno),
                             rsp.weak_checksum,
                             (uint8_t *)rsp.strong_checksum.strong_checksum_val);

        if (rsp.strong_checksum.strong_checksum_val) {
                /* This is allocated by the libc while decoding RPC msg */
                /* Hence no 'GF_FREE', but just 'free' */
                free (rsp.strong_checksum.strong_checksum_val);
        }

        return 0;
}

int
client3_1_lk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                  void *myframe)
{
        call_frame_t    *frame      = NULL;
        clnt_local_t  *local      = NULL;
        struct gf_flock     lock       = {0,};
        gfs3_lk_rsp      rsp        = {0,};
        int              ret        = 0;

        frame = myframe;
        local = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_lk_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (rsp.op_ret >= 0) {
                gf_proto_flock_to_flock (&rsp.flock, &lock);
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

        frame->local = NULL;
        client_local_wipe (local);

out:
        STACK_UNWIND_STRICT (lk, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &lock);

        return 0;
}

int
client3_1_readdir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t           *frame = NULL;
        gfs3_readdir_rsp        rsp   = {0,};
        int32_t                 ret   = 0;
        clnt_local_t         *local = NULL;
        gf_dirent_t             entries;

        frame = myframe;
        local = frame->local;
        frame->local = NULL;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_readdir_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        INIT_LIST_HEAD (&entries.list);
        if (rsp.op_ret > 0) {
                unserialize_rsp_dirent (&rsp, &entries);
        }

out:
        STACK_UNWIND_STRICT (readdir, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &entries);

        client_local_wipe (local);

        if (rsp.op_ret != -1) {
                gf_dirent_free (&entries);
        }

        clnt_readdir_rsp_cleanup (&rsp);

        return 0;
}


int
client3_1_readdirp_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t           *frame = NULL;
        gfs3_readdirp_rsp       rsp   = {0,};
        int32_t                 ret   = 0;
        clnt_local_t         *local = NULL;
        gf_dirent_t             entries;

        frame = myframe;
        local = frame->local;
        frame->local = NULL;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_readdirp_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        INIT_LIST_HEAD (&entries.list);
        if (rsp.op_ret > 0) {
                unserialize_rsp_direntp (&rsp, &entries);
        }

out:
        STACK_UNWIND_STRICT (readdirp, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &entries);

        client_local_wipe (local);

        if (rsp.op_ret != -1) {
                gf_dirent_free (&entries);
        }

        clnt_readdirp_rsp_cleanup (&rsp);

        return 0;
}


int
client3_1_rename_cbk (struct rpc_req *req, struct iovec *iov, int count,
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

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_rename_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.stat, &stbuf);

                gf_stat_to_iatt (&rsp.preoldparent, &preoldparent);
                gf_stat_to_iatt (&rsp.postoldparent, &postoldparent);

                gf_stat_to_iatt (&rsp.prenewparent, &prenewparent);
                gf_stat_to_iatt (&rsp.postnewparent, &postnewparent);
        }

out:
        STACK_UNWIND_STRICT (rename, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno),
                             &stbuf, &preoldparent, &postoldparent,
                             &prenewparent, &postnewparent);

        return 0;
}

int
client3_1_link_cbk (struct rpc_req *req, struct iovec *iov, int count,
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

        frame = myframe;

        local = frame->local;
        frame->local = NULL;
        inode = local->loc.inode;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_link_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                gf_stat_to_iatt (&rsp.stat, &stbuf);

                gf_stat_to_iatt (&rsp.preparent, &preparent);
                gf_stat_to_iatt (&rsp.postparent, &postparent);
        }

out:
        frame->local = NULL;
        STACK_UNWIND_STRICT (link, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), inode,
                             &stbuf, &preparent, &postparent);

        client_local_wipe (local);
        return 0;
}


int
client3_1_opendir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        clnt_local_t      *local = NULL;
        clnt_conf_t       *conf = NULL;
        clnt_fd_ctx_t     *fdctx = NULL;
        call_frame_t   *frame = NULL;
        fd_t             *fd = NULL;
        int ret = 0;
        gfs3_opendir_rsp  rsp = {0,};

        frame = myframe;
        local = frame->local;

        frame->local = NULL;
        conf  = frame->this->private;
        fd    = local->fd;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_opendir_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 != rsp.op_ret) {
                fdctx = GF_CALLOC (1, sizeof (*fdctx),
                                   gf_client_mt_clnt_fdctx_t);
                if (!fdctx) {
                        rsp.op_ret = -1;
                        rsp.op_errno = ENOMEM;
                        goto out;
                }

                fdctx->remote_fd = rsp.fd;
                fdctx->inode     = inode_ref (fd->inode);
                fdctx->is_dir    = 1;

                INIT_LIST_HEAD (&fdctx->sfd_pos);
                INIT_LIST_HEAD (&fdctx->lock_list);

                this_fd_set_ctx (fd, frame->this, &local->loc, fdctx);

                pthread_mutex_lock (&conf->lock);
                {
                        list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
                }
                pthread_mutex_unlock (&conf->lock);
        }

out:
        frame->local = NULL;
        STACK_UNWIND_STRICT (opendir, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), fd);

        client_local_wipe (local);

        return 0;
}


int
client3_1_lookup_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        clnt_local_t    *local      = NULL;
        call_frame_t    *frame      = NULL;
        int              ret        = 0;
        gfs3_lookup_rsp  rsp        = {0,};
        struct iatt      stbuf      = {0,};
        struct iatt      postparent = {0,};
        int              op_errno   = EINVAL;
        dict_t          *xattr      = NULL;
        inode_t         *inode      = NULL;
        char            *buf        = NULL;

        frame = myframe;
        local = frame->local;
        inode = local->loc.inode;
        frame->local = NULL;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_lookup_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                op_errno = EINVAL;
                goto out;
        }

        op_errno = gf_error_to_errno (rsp.op_errno);
        gf_stat_to_iatt (&rsp.postparent, &postparent);

        if (rsp.op_ret == -1)
                goto out;

        rsp.op_ret = -1;
        gf_stat_to_iatt (&rsp.stat, &stbuf);

        if (rsp.dict.dict_len > 0) {
                xattr = dict_new();
                GF_VALIDATE_OR_GOTO (frame->this->name, xattr, out);

                buf = memdup (rsp.dict.dict_val, rsp.dict.dict_len);
                GF_VALIDATE_OR_GOTO (frame->this->name, buf, out);

                ret = dict_unserialize (buf, rsp.dict.dict_len, &xattr);
                if (ret < 0) {
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "%s (%"PRId64"): failed to "
                                "unserialize dictionary",
                                local->loc.path, inode->ino);
                        op_errno = EINVAL;
                        goto out;
                }

                xattr->extra_free = buf;
                buf = NULL;
        }

        if ((!uuid_is_null (inode->gfid))
            && (uuid_compare (stbuf.ia_gfid, inode->gfid) != 0)) {
                gf_log (frame->this->name, GF_LOG_DEBUG,
                        "gfid changed for %s", local->loc.path);
                rsp.op_ret = -1;
                rsp.op_errno = ESTALE;
                goto out;
        }

        rsp.op_ret = 0;

out:
        rsp.op_errno = op_errno;
        frame->local = NULL;
        STACK_UNWIND_STRICT (lookup, frame, rsp.op_ret, rsp.op_errno, inode,
                             &stbuf, xattr, &postparent);

        client_local_wipe (local);

        if (xattr)
                dict_unref (xattr);

        if (rsp.dict.dict_val) {
                /* don't use GF_FREE, this memory was allocated by libc
                 */
                free (rsp.dict.dict_val);
                rsp.dict.dict_val = NULL;
        }

        if (buf) {
                GF_FREE (buf);
        }

        return 0;
}

int
client3_1_readv_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        call_frame_t   *frame  = NULL;
        struct iobref  *iobref = NULL;
        struct iovec    vector[MAX_IOVEC] = {{0}, };
        struct iatt     stat   = {0,};
        gfs3_read_rsp   rsp    = {0,};
        int             ret    = 0, rspcount = 0;
        clnt_local_t   *local  = NULL;

        memset (vector, 0, sizeof (vector));

        frame = myframe;
        local = frame->local;
        frame->local = NULL;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_readv_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (rsp.op_ret != -1) {
                iobref = req->rsp_iobref;
                gf_stat_to_iatt (&rsp.stat, &stat);

                vector[0].iov_len = rsp.op_ret;
                if (rsp.op_ret > 0)
                        vector[0].iov_base = req->rsp[1].iov_base;
                rspcount = 1;
        }
out:
        STACK_UNWIND_STRICT (readv, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), vector, rspcount,
                             &stat, iobref);

        if (local) {
                client_local_wipe (local);
        }

        return 0;
}

int
client3_1_release_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        call_frame_t   *frame = NULL;

        frame = myframe;
        STACK_DESTROY (frame->root);
        return 0;
}
int
client3_1_releasedir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                          void *myframe)
{
        call_frame_t   *frame = NULL;

        frame = myframe;
        STACK_DESTROY (frame->root);
        return 0;
}

int
client_fdctx_destroy (xlator_t *this, clnt_fd_ctx_t *fdctx)
{
        call_frame_t *fr = NULL;
        int32_t       ret = -1;

        if (!fdctx)
                goto out;

        if (fdctx->remote_fd == -1)
                goto out;

        fr = create_frame (this, this->ctx->pool);

        if (fdctx->is_dir) {
                gfs3_releasedir_req  req = {{0,},};
                req.fd = fdctx->remote_fd;
                ret = client_submit_request (this, &req, fr, &clnt3_1_fop_prog,
                                             GFS3_OP_RELEASEDIR,
                                             client3_1_releasedir_cbk,
                                             NULL, xdr_from_releasedir_req,
                                             NULL, 0, NULL, 0, NULL);
        } else {
                gfs3_release_req  req = {{0,},};
                req.fd = fdctx->remote_fd;
                ret = client_submit_request (this, &req, fr, &clnt3_1_fop_prog,
                                             GFS3_OP_RELEASE,
                                             client3_1_release_cbk, NULL,
                                             xdr_from_release_req, NULL, 0,
                                             NULL, 0, NULL);
        }

out:
        if (!ret && fdctx) {
                fdctx->remote_fd = -1;
                inode_unref (fdctx->inode);
                GF_FREE (fdctx);
        }

        if (ret && fr)
                STACK_DESTROY (fr->root);

        return ret;
}

int32_t
client3_1_releasedir (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        clnt_conf_t         *conf = NULL;
        clnt_fd_ctx_t       *fdctx = NULL;
        clnt_args_t         *args = NULL;
        gfs3_releasedir_req  req = {{0,},};
        int64_t              remote_fd = -1;
        int                  ret = 0;

        if (!frame || !this || !data)
                goto unwind;

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

                        if (remote_fd != -1)
                                list_del_init (&fdctx->sfd_pos);

                        fdctx->released = 1;
                }
        }
        pthread_mutex_unlock (&conf->lock);

        if (remote_fd != -1) {
                req.fd = remote_fd;
                ret = client_submit_request (this, &req, frame, conf->fops,
                                             GFS3_OP_RELEASEDIR,
                                             client3_1_releasedir_cbk,
                                             NULL, xdr_from_releasedir_req,
                                             NULL, 0, NULL, 0, NULL);
                inode_unref (fdctx->inode);
                GF_FREE (fdctx);
        }

unwind:
        if (ret)
                STACK_DESTROY (frame->root);

        return 0;
}

int32_t
client3_1_release (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        int64_t           remote_fd = -1;
        clnt_conf_t      *conf      = NULL;
        clnt_fd_ctx_t    *fdctx     = NULL;
        clnt_args_t      *args      = NULL;
        gfs3_release_req  req       = {{0,},};
        int               ret       = 0;

        if (!frame || !this || !data)
                goto unwind;

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

                        if (remote_fd != -1)
                                list_del_init (&fdctx->sfd_pos);

                        fdctx->released = 1;
                }
        }
        pthread_mutex_unlock (&conf->lock);

        if (remote_fd != -1) {
                req.fd = remote_fd;

                delete_granted_locks_fd (fdctx);

                ret = client_submit_request (this, &req, frame, conf->fops,
                                             GFS3_OP_RELEASE,
                                             client3_1_release_cbk, NULL,
                                             xdr_from_release_req, NULL, 0,
                                             NULL, 0, NULL);
                inode_unref (fdctx->inode);
                GF_FREE (fdctx);
        }
unwind:
        if (ret)
                STACK_DESTROY (frame->root);

        return 0;
}


int32_t
client3_1_lookup (call_frame_t *frame, xlator_t *this,
                  void *data)
{
        clnt_conf_t     *conf              = NULL;
        clnt_local_t    *local             = NULL;
        clnt_args_t     *args              = NULL;
        gfs3_lookup_req  req               = {{0,},};
        int              ret               = 0;
        size_t           dict_len          = 0;
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
        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }

        if (!(args->loc && args->loc->inode))
                goto unwind;

        loc_copy (&local->loc, args->loc);
        frame->local = local;

        if (args->loc->parent)
                memcpy (req.pargfid, args->loc->parent->gfid, 16);
        else
                memcpy (req.gfid, args->loc->inode->gfid, 16);

        if (args->dict) {
                content = dict_get (args->dict, GF_CONTENT_KEY);
                if (content != NULL) {
                        rsp_iobref = iobref_new ();
                        if (rsp_iobref == NULL) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "out of memory");
                                goto unwind;
                        }

                        rsp_iobuf = iobuf_get (this->ctx->iobuf_pool);
                        if (rsp_iobuf == NULL) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "out of memory");
                                goto unwind;
                        }

                        iobref_add (rsp_iobref, rsp_iobuf);
                        iobuf_unref (rsp_iobuf);
                        rsphdr = &vector[0];
                        rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
                        rsphdr->iov_len
                                = rsp_iobuf->iobuf_arena->iobuf_pool->page_size;
                        count = 1;
                        rsp_iobuf = NULL;
                        local->iobref = rsp_iobref;
                        rsp_iobref = NULL;
                }

                ret = dict_allocate_and_serialize (args->dict,
                                                   &req.dict.dict_val,
                                                   &dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized length of dict");
                        op_errno = EINVAL;
                        goto unwind;
                }
        }

        req.path          = (char *)args->loc->path;
        req.bname         = (char *)args->loc->name;
        req.dict.dict_len = dict_len;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_LOOKUP, client3_1_lookup_cbk,
                                     NULL, xdr_from_lookup_req, rsphdr, count,
                                     NULL, 0, local->iobref);

        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        if (rsp_iobref != NULL) {
                iobref_unref (rsp_iobref);
        }

        return 0;

unwind:
        if (frame)
                frame->local = NULL;

        STACK_UNWIND_STRICT (lookup, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL);

        if (local)
                client_local_wipe (local);

        if (req.dict.dict_val)
                GF_FREE (req.dict.dict_val);

        if (rsp_iobref != NULL) {
                iobref_unref (rsp_iobref);
        }

        if (rsp_iobuf != NULL) {
                iobuf_unref (rsp_iobuf);
        }

        return 0;
}



int32_t
client3_1_stat (call_frame_t *frame, xlator_t *this,
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
        if (!(args->loc && args->loc->inode))
                goto unwind;

        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        req.path = (char *)args->loc->path;
        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_STAT, client3_1_stat_cbk, NULL,
                                     xdr_from_stat_req, NULL, 0, NULL, 0, NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (stat, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
client3_1_truncate (call_frame_t *frame, xlator_t *this,
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

        if (!(args->loc && args->loc->inode))
                goto unwind;

        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        req.path = (char *)args->loc->path;
        req.offset = args->offset;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_TRUNCATE,
                                     client3_1_truncate_cbk, NULL,
                                     xdr_from_truncate_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        STACK_UNWIND_STRICT (truncate, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
client3_1_ftruncate (call_frame_t *frame, xlator_t *this,
                     void *data)
{
        clnt_args_t        *args     = NULL;
        clnt_fd_ctx_t      *fdctx    = NULL;
        clnt_conf_t        *conf     = NULL;
        gfs3_ftruncate_req  req      = {{0,},};
        int                 op_errno = EINVAL;
        int                 ret      = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        req.offset = args->offset;
        req.fd     = fdctx->remote_fd;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FTRUNCATE,
                                     client3_1_ftruncate_cbk, NULL,
                                     xdr_from_ftruncate_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        STACK_UNWIND_STRICT (ftruncate, frame, -1, op_errno, NULL, NULL);
        return 0;
}



int32_t
client3_1_access (call_frame_t *frame, xlator_t *this,
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

        if (!(args->loc && args->loc->inode))
                goto unwind;

        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        req.path = (char *)args->loc->path;
        req.mask = args->mask;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_ACCESS,
                                     client3_1_access_cbk, NULL,
                                     xdr_from_access_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (access, frame, -1, op_errno);
        return 0;
}

int32_t
client3_1_readlink (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_conf_t       *conf     = NULL;
        clnt_args_t       *args     = NULL;
        gfs3_readlink_req  req      = {{0,},};
        int                ret      = 0;
        int                op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        if (!(args->loc && args->loc->inode))
                goto unwind;

        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        req.path = (char *)args->loc->path;
        req.size = args->size;
        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_READLINK,
                                     client3_1_readlink_cbk, NULL,
                                     xdr_from_readlink_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (readlink, frame, -1, op_errno, NULL, NULL);
        return 0;
}




int32_t
client3_1_unlink (call_frame_t *frame, xlator_t *this,
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

        if (!(args->loc && args->loc->parent))
                goto unwind;

        memcpy (req.pargfid,  args->loc->parent->gfid, 16);
        req.path  = (char *)args->loc->path;
        req.bname = (char *)args->loc->name;
        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_UNLINK,
                                     client3_1_unlink_cbk, NULL,
                                     xdr_from_unlink_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        STACK_UNWIND_STRICT (unlink, frame, -1, op_errno, NULL, NULL);
        return 0;
}



int32_t
client3_1_rmdir (call_frame_t *frame, xlator_t *this,
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

        if (!(args->loc && args->loc->parent))
                goto unwind;

        memcpy (req.pargfid,  args->loc->parent->gfid, 16);
        req.path  = (char *)args->loc->path;
        req.bname = (char *)args->loc->name;
        req.flags = args->flags;
        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_RMDIR, client3_1_rmdir_cbk, NULL,
                                     xdr_from_rmdir_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        STACK_UNWIND_STRICT (rmdir, frame, -1, op_errno, NULL, NULL);
        return 0;
}



int32_t
client3_1_symlink (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_local_t     *local    = NULL;
        clnt_conf_t      *conf     = NULL;
        clnt_args_t      *args     = NULL;
        gfs3_symlink_req  req      = {{0,},};
        size_t            dict_len = 0;
        int               ret      = 0;
        int               op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }

        if (!(args->loc && args->loc->parent))
                goto unwind;

        loc_copy (&local->loc, args->loc);
        frame->local = local;

        memcpy (req.pargfid,  args->loc->parent->gfid, 16);
        req.path     = (char *)args->loc->path;
        req.linkname = (char *)args->linkname;
        req.bname    = (char *)args->loc->name;
        if (args->dict) {
                ret = dict_allocate_and_serialize (args->dict,
                                                   &req.dict.dict_val,
                                                   &dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized length of dict");
                        op_errno = EINVAL;
                        goto unwind;
                }
        }
        req.dict.dict_len = dict_len;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_SYMLINK, client3_1_symlink_cbk,
                                     NULL, xdr_from_symlink_req, NULL, 0, NULL,
                                     0, NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
unwind:
        if (frame)
                frame->local = NULL;

        STACK_UNWIND_STRICT (symlink, frame, -1, op_errno, NULL, NULL, NULL, NULL);

        if (local)
                client_local_wipe (local);
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
}



int32_t
client3_1_rename (call_frame_t *frame, xlator_t *this,
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

        if (!(args->oldloc && args->newloc && args->oldloc->parent &&
              args->newloc->parent))
                goto unwind;

        memcpy (req.oldgfid,  args->oldloc->parent->gfid, 16);
        memcpy (req.newgfid, args->newloc->parent->gfid, 16);

        req.oldpath = (char *)args->oldloc->path;
        req.oldbname =  (char *)args->oldloc->name;
        req.newpath = (char *)args->newloc->path;
        req.newbname = (char *)args->newloc->name;
        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_RENAME, client3_1_rename_cbk, NULL,
                                     xdr_from_rename_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        STACK_UNWIND_STRICT (rename, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);
        return 0;
}



int32_t
client3_1_link (call_frame_t *frame, xlator_t *this,
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

        if (!(args->oldloc && args->oldloc->inode && args->newloc &&
              args->newloc->parent))
                goto unwind;

        memcpy (req.oldgfid,  args->oldloc->inode->gfid, 16);
        memcpy (req.newgfid, args->newloc->parent->gfid, 16);

        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }

        loc_copy (&local->loc, args->oldloc);
        frame->local = local;

        req.oldpath = (char *)args->oldloc->path;
        req.newpath = (char *)args->newloc->path;
        req.newbname = (char *)args->newloc->name;
        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_LINK, client3_1_link_cbk, NULL,
                                     xdr_from_link_req, NULL, 0, NULL, 0, NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        STACK_UNWIND_STRICT (link, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}



int32_t
client3_1_mknod (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_local_t   *local    = NULL;
        clnt_conf_t    *conf     = NULL;
        clnt_args_t    *args     = NULL;
        gfs3_mknod_req  req      = {{0,},};
        size_t          dict_len = 0;
        int             ret      = 0;
        int             op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }

        if (!(args->loc && args->loc->parent))
                goto unwind;

        loc_copy (&local->loc, args->loc);
        frame->local = local;

        memcpy (req.pargfid,  args->loc->parent->gfid, 16);
        req.path   = (char *)args->loc->path;
        req.bname  = (char *)args->loc->name;
        req.mode   = args->mode;
        req.dev    = args->rdev;
        if (args->dict) {
                ret = dict_allocate_and_serialize (args->dict,
                                                   &req.dict.dict_val,
                                                   &dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized length of dict");
                        op_errno = EINVAL;
                        goto unwind;
                }
        }
        req.dict.dict_len = dict_len;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_MKNOD, client3_1_mknod_cbk, NULL,
                                     xdr_from_mknod_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
unwind:
        if (frame)
                frame->local = NULL;

        STACK_UNWIND_STRICT (mknod, frame, -1, op_errno, NULL, NULL, NULL, NULL);

        if (local)
                client_local_wipe (local);
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
}



int32_t
client3_1_mkdir (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_local_t   *local    = NULL;
        clnt_conf_t    *conf     = NULL;
        clnt_args_t    *args     = NULL;
        gfs3_mkdir_req  req      = {{0,},};
        size_t          dict_len = 0;
        int             ret      = 0;
        int             op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }

        if (!(args->loc && args->loc->parent))
                goto unwind;

        loc_copy (&local->loc, args->loc);
        frame->local = local;

        memcpy (req.pargfid,  args->loc->parent->gfid, 16);
        req.path  = (char *)args->loc->path;
        req.bname = (char *)args->loc->name;
        req.mode  = args->mode;
        if (args->dict) {
                ret = dict_allocate_and_serialize (args->dict,
                                                   &req.dict.dict_val,
                                                   &dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized length of dict");
                        op_errno = EINVAL;
                        goto unwind;
                }
        }
        req.dict.dict_len = dict_len;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_MKDIR, client3_1_mkdir_cbk, NULL,
                                     xdr_from_mkdir_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
unwind:
        if (frame)
                frame->local = NULL;

        STACK_UNWIND_STRICT (mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL);

        if (local)
                client_local_wipe (local);
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
}


int32_t
client3_1_create (call_frame_t *frame, xlator_t *this,
                  void *data)
{
        clnt_local_t    *local    = NULL;
        clnt_conf_t     *conf     = NULL;
        clnt_args_t     *args     = NULL;
        gfs3_create_req  req      = {{0,},};
        size_t           dict_len = 0;
        int              ret      = 0;
        int              op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        if (!(args->loc && args->loc->parent))
                goto unwind;

        local->fd = fd_ref (args->fd);
        local->flags = args->flags;

        loc_copy (&local->loc, args->loc);
        frame->local = local;

        memcpy (req.pargfid,  args->loc->parent->gfid, 16);
        req.path  = (char *)args->loc->path;
        req.bname = (char *)args->loc->name;
        req.mode  = args->mode;
        req.flags = gf_flags_from_flags (args->flags);
        if (args->dict) {
                ret = dict_allocate_and_serialize (args->dict,
                                                   &req.dict.dict_val,
                                                   &dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized length of dict");
                        op_errno = EINVAL;
                        goto unwind;
                }
        }
        req.dict.dict_len = dict_len;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_CREATE, client3_1_create_cbk, NULL,
                                     xdr_from_create_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
unwind:
        if (frame)
                frame->local = NULL;

        STACK_UNWIND_STRICT (create, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL);
        if (local)
                client_local_wipe (local);
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
}



int32_t
client3_1_open (call_frame_t *frame, xlator_t *this,
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

        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        if (!(args->loc && args->loc->inode))
                goto unwind;

        local->fd = fd_ref (args->fd);
        local->flags = args->flags;
        local->wbflags = args->wbflags;
        loc_copy (&local->loc, args->loc);
        frame->local = local;

        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        req.flags = gf_flags_from_flags (args->flags);
        req.wbflags = args->wbflags;
        req.path = (char *)args->loc->path;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_OPEN, client3_1_open_cbk, NULL,
                                     xdr_from_open_req, NULL, 0, NULL, 0, NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        if (frame)
                frame->local = NULL;

        STACK_UNWIND_STRICT (open, frame, -1, op_errno, NULL);

        if (local)
                client_local_wipe (local);
        return 0;
}



int32_t
client3_1_readv (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_args_t    *args       = NULL;
        clnt_fd_ctx_t  *fdctx      = NULL;
        clnt_conf_t    *conf       = NULL;
        int             op_errno   = ESTALE;
        gfs3_read_req   req        = {{0,},};
        int             ret        = 0;
        struct iovec    rsp_vec    = {0, };
        struct iobuf   *rsp_iobuf  = NULL;
        struct iobref  *rsp_iobref = NULL;
        clnt_local_t   *local      = NULL;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        req.size   = args->size;
        req.offset = args->offset;
        req.fd     = fdctx->remote_fd;

        rsp_iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (rsp_iobuf == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                op_errno = ENOMEM;
                goto unwind;
        }

        rsp_iobref = iobref_new ();
        if (rsp_iobref == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                op_errno = ENOMEM;
                goto unwind;
        }

        iobref_add (rsp_iobref, rsp_iobuf);
        iobuf_unref (rsp_iobuf);
        rsp_vec.iov_base = iobuf_ptr (rsp_iobuf);
        rsp_vec.iov_len = rsp_iobuf->iobuf_arena->iobuf_pool->page_size;

        rsp_iobuf = NULL;

        if (args->size > rsp_vec.iov_len) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "read-size (%lu) is bigger than iobuf size (%lu)",
                        (unsigned long)args->size,
                        (unsigned long)rsp_vec.iov_len);
                op_errno = EINVAL;
                goto unwind;
        }

        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (local == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                op_errno = ENOMEM;
                goto unwind;
        }

        local->iobref = rsp_iobref;
        rsp_iobref = NULL;
        frame->local = local;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_READ, client3_1_readv_cbk, NULL,
                                     xdr_from_readv_req, NULL, 0, &rsp_vec, 1,
                                     local->iobref);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        if (rsp_iobuf) {
                iobuf_unref (rsp_iobuf);
        }

        if (rsp_iobref) {
                iobref_unref (rsp_iobref);
        }

        STACK_UNWIND_STRICT (readv, frame, -1, op_errno, NULL, 0, NULL, NULL);
        return 0;
}


int32_t
client3_1_writev (call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_args_t    *args     = NULL;
        clnt_fd_ctx_t  *fdctx    = NULL;
        clnt_conf_t    *conf     = NULL;
        gfs3_write_req  req      = {{0,},};
        int             op_errno = ESTALE;
        int             ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        req.size   = args->size;
        req.offset = args->offset;
        req.fd     = fdctx->remote_fd;

        ret = client_submit_vec_request (this, &req, frame, conf->fops, GFS3_OP_WRITE,
                                         client3_1_writev_cbk,
                                         args->vector, args->count,
                                         args->iobref, xdr_from_writev_req);
        if (ret)
                goto unwind;

        return 0;
unwind:
        STACK_UNWIND_STRICT (writev, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
client3_1_flush (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_args_t    *args     = NULL;
        gfs3_flush_req  req      = {{0,},};
        clnt_fd_ctx_t  *fdctx    = NULL;
        clnt_conf_t    *conf     = NULL;
        clnt_local_t *local    = NULL;
        int             op_errno = ESTALE;
        int             ret      = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        conf = this->private;

        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of Memory");
                STACK_UNWIND (frame, -1, ENOMEM);
                return 0;

        }

        local->fd = fd_ref (args->fd);
        local->owner = frame->root->lk_owner;
        frame->local = local;

        req.fd = fdctx->remote_fd;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FLUSH, client3_1_flush_cbk, NULL,
                                     xdr_from_flush_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        STACK_UNWIND_STRICT (flush, frame, -1, op_errno);
        return 0;
}



int32_t
client3_1_fsync (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_args_t    *args     = NULL;
        gfs3_fsync_req  req      = {{0,},};
        clnt_fd_ctx_t  *fdctx    = NULL;
        clnt_conf_t    *conf     = NULL;
        int             op_errno = 0;
        int           ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        req.fd   = fdctx->remote_fd;
        req.data = args->flags;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FSYNC, client3_1_fsync_cbk, NULL,
                                     xdr_from_fsync_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        STACK_UNWIND_STRICT (fsync, frame, -1, op_errno, NULL, NULL);
        return 0;
}



int32_t
client3_1_fstat (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_args_t    *args     = NULL;
        gfs3_fstat_req  req      = {{0,},};
        clnt_fd_ctx_t  *fdctx    = NULL;
        clnt_conf_t    *conf     = NULL;
        int             op_errno = ESTALE;
        int           ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        req.fd = fdctx->remote_fd;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FSTAT, client3_1_fstat_cbk, NULL,
                                     xdr_from_fstat_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        STACK_UNWIND_STRICT (fstat, frame, -1, op_errno, NULL);
        return 0;
}



int32_t
client3_1_opendir (call_frame_t *frame, xlator_t *this,
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

        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }
        if (!(args->loc && args->loc->inode))
                goto unwind;

        local->fd = fd_ref (args->fd);
        loc_copy (&local->loc, args->loc);
        frame->local = local;

        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        req.path = (char *)args->loc->path;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_OPENDIR, client3_1_opendir_cbk,
                                     NULL, xdr_from_opendir_req,
                                     NULL, 0, NULL, 0, NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        if (frame)
                frame->local = NULL;
        STACK_UNWIND_STRICT (opendir, frame, -1, op_errno, NULL);
        if (local)
                client_local_wipe (local);
        return 0;
}



int32_t
client3_1_fsyncdir (call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_args_t       *args     = NULL;
        clnt_fd_ctx_t     *fdctx    = NULL;
        clnt_conf_t       *conf     = NULL;
        int                op_errno = ESTALE;
        gfs3_fsyncdir_req  req      = {{0,},};
        int                ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        req.fd   = fdctx->remote_fd;
        req.data = args->flags;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FSYNCDIR, client3_1_fsyncdir_cbk,
                                     NULL, xdr_from_fsyncdir_req, NULL, 0,
                                     NULL, 0, NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        STACK_UNWIND_STRICT (fsyncdir, frame, -1, op_errno);
        return 0;
}



int32_t
client3_1_statfs (call_frame_t *frame, xlator_t *this,
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

        if (!args->loc)
                goto unwind;

        if (args->loc->inode)
	        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        else
		req.gfid[15] = 1;

        req.path = (char *)args->loc->path;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_STATFS, client3_1_statfs_cbk, NULL,
                                     xdr_from_statfs_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        return 0;
unwind:
        STACK_UNWIND_STRICT (statfs, frame, -1, op_errno, NULL);
        return 0;
}



int32_t
client3_1_setxattr (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_conf_t       *conf     = NULL;
        clnt_args_t       *args     = NULL;
        gfs3_setxattr_req  req      = {{0,},};
        int                ret      = 0;
        size_t             dict_len = 0;
        int                op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        if (!(args->loc && args->loc->inode))
                goto unwind;

        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        if (args->dict) {
                ret = dict_allocate_and_serialize (args->dict,
                                                   &req.dict.dict_val,
                                                   &dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized dict");
                        op_errno = EINVAL;
                        goto unwind;
                }
                req.dict.dict_len = dict_len;
        }
        req.flags = args->flags;
        req.path  = (char *)args->loc->path;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_SETXATTR, client3_1_setxattr_cbk,
                                     NULL, xdr_from_setxattr_req, NULL, 0,
                                     NULL, 0, NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (setxattr, frame, -1, op_errno);
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
}



int32_t
client3_1_fsetxattr (call_frame_t *frame, xlator_t *this,
                     void *data)
{
        clnt_args_t        *args     = NULL;
        clnt_fd_ctx_t      *fdctx    = NULL;
        clnt_conf_t        *conf     = NULL;
        gfs3_fsetxattr_req  req      = {{0,},};
        int                 op_errno = ESTALE;
        int                 ret      = 0;
        size_t              dict_len = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        req.fd    = fdctx->remote_fd;
        req.flags = args->flags;
        memcpy (req.gfid,  args->fd->inode->gfid, 16);

        if (args->dict) {
                ret = dict_allocate_and_serialize (args->dict,
                                                   &req.dict.dict_val,
                                                   &dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized dict");
                        goto unwind;
                }
                req.dict.dict_len = dict_len;
        }

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FSETXATTR, client3_1_fsetxattr_cbk,
                                     NULL, xdr_from_fsetxattr_req, NULL, 0,
                                     NULL, 0, NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (fsetxattr, frame, -1, op_errno);
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
}




int32_t
client3_1_fgetxattr (call_frame_t *frame, xlator_t *this,
                     void *data)
{
        clnt_args_t        *args       = NULL;
        clnt_fd_ctx_t      *fdctx      = NULL;
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

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        local = GF_CALLOC (1, sizeof (*local),
                           gf_client_mt_clnt_local_t);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        rsp_iobref = iobref_new ();
        if (rsp_iobref == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                op_errno = ENOMEM;
                goto unwind;
        }

        rsp_iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (rsp_iobuf == NULL) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                goto unwind;
        }

        iobref_add (rsp_iobref, rsp_iobuf);
        iobuf_unref (rsp_iobuf);
        rsphdr = &vector[0];
        rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
        rsphdr->iov_len = rsp_iobuf->iobuf_arena->iobuf_pool->page_size;
        count = 1;
        rsp_iobuf = NULL;
        local->iobref = rsp_iobref;
        rsp_iobref = NULL;

        req.namelen = 1; /* Use it as a flag */
        req.fd   = fdctx->remote_fd;
        req.name = (char *)args->name;
        if (!req.name) {
                req.name = "";
                req.namelen = 0;
        }

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FGETXATTR,
                                     client3_1_fgetxattr_cbk, NULL,
                                     xdr_from_fgetxattr_req, rsphdr, count,
                                     NULL, 0, local->iobref);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        local = frame->local;
        frame->local = NULL;

        STACK_UNWIND_STRICT (fgetxattr, frame, -1, op_errno, NULL);

        client_local_wipe (local);

        if (rsp_iobuf) {
                iobuf_unref (rsp_iobuf);
        }

        if (rsp_iobref) {
                iobref_unref (rsp_iobref);
        }

        return 0;
}



int32_t
client3_1_getxattr (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_conf_t       *conf       = NULL;
        clnt_args_t       *args       = NULL;
        gfs3_getxattr_req  req        = {{0,},};
        dict_t            *dict       = NULL;
        int                ret        = 0;
        int32_t            op_ret     = 0;
        int                op_errno   = ESTALE;
        int                count      = 0;
        clnt_local_t      *local      = NULL;
        struct iobref     *rsp_iobref = NULL;
        struct iobuf      *rsp_iobuf  = NULL;
        struct iovec      *rsphdr     = NULL;
        struct iovec       vector[MAX_IOVEC] = {{0}, };

        if (!frame || !this || !data) {
                op_ret   = -1;
                op_errno = 0;
                goto unwind;
        }
        args = data;

        if (!(args->loc && args->loc->inode)) {
                op_ret   = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        local = GF_CALLOC (1, sizeof (*local),
                           gf_client_mt_clnt_local_t);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        rsp_iobref = iobref_new ();
        if (rsp_iobref == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        rsp_iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (rsp_iobuf == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                goto unwind;
        }

        iobref_add (rsp_iobref, rsp_iobuf);
        iobuf_unref (rsp_iobuf);
        rsphdr = &vector[0];
        rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
        rsphdr->iov_len = rsp_iobuf->iobuf_arena->iobuf_pool->page_size;
        count = 1;
        rsp_iobuf = NULL;
        local->iobref = rsp_iobref;
        rsp_iobref = NULL;

        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        req.namelen = 1; /* Use it as a flag */
        req.path = (char *)args->loc->path;
        req.name = (char *)args->name;
        if (!req.name) {
                req.name = "";
                req.namelen = 0;
        }

        conf = this->private;

        if (args && args->name) {
                if (is_client_dump_locks_cmd ((char *)args->name)) {
                        dict = dict_new ();
                        ret = client_dump_locks ((char *)args->name,
                                                 args->loc->inode,
                                                 dict);
                        if (ret) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Client dump locks failed");
                                op_ret = -1;
                                op_errno = EINVAL;
                        }

                        GF_ASSERT (dict);
                        op_ret = 0;
                        op_errno = 0;
                        goto unwind;
                }
        }

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_GETXATTR,
                                     client3_1_getxattr_cbk, NULL,
                                     xdr_from_getxattr_req, rsphdr, count,
                                     NULL, 0, local->iobref);
        if (ret) {
                op_ret   = -1;
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        local = frame->local;
        frame->local = NULL;
        client_local_wipe (local);

        if (rsp_iobuf) {
                iobuf_unref (rsp_iobuf);
        }

        if (rsp_iobref) {
                iobref_unref (rsp_iobref);
        }

        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict);

        return 0;
}



int32_t
client3_1_xattrop (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_conf_t      *conf       = NULL;
        clnt_args_t      *args       = NULL;
        gfs3_xattrop_req  req        = {{0,},};
        int               ret        = 0;
        size_t            dict_len   = 0;
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

        local = GF_CALLOC (1, sizeof (*local),
                           gf_client_mt_clnt_local_t);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        rsp_iobref = iobref_new ();
        if (rsp_iobref == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                op_errno = ENOMEM;
                goto unwind;
        }

        rsp_iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (rsp_iobuf == NULL) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                goto unwind;
        }

        iobref_add (rsp_iobref, rsp_iobuf);
        iobuf_unref (rsp_iobuf);
        rsphdr = &vector[0];
        rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
        rsphdr->iov_len = rsp_iobuf->iobuf_arena->iobuf_pool->page_size;
        count = 1;
        rsp_iobuf = NULL;
        local->iobref = rsp_iobref;
        rsp_iobref = NULL;

        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        if (args->dict) {
                ret = dict_allocate_and_serialize (args->dict,
                                                   &req.dict.dict_val,
                                                   &dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized dict");
                        op_errno = EINVAL;
                        goto unwind;
                }
                req.dict.dict_len = dict_len;
        }
        req.flags = args->flags;
        req.path  = (char *)args->loc->path;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_XATTROP,
                                     client3_1_xattrop_cbk, NULL,
                                     xdr_from_xattrop_req, rsphdr, count,
                                     NULL, 0, local->iobref);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
unwind:
        local = frame->local;
        frame->local = NULL;

        STACK_UNWIND_STRICT (xattrop, frame, -1, op_errno, NULL);

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        client_local_wipe (local);

        if (rsp_iobuf) {
                iobuf_unref (rsp_iobuf);
        }

        if (rsp_iobref) {
                iobref_unref (rsp_iobref);
        }

        return 0;
}



int32_t
client3_1_fxattrop (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_args_t       *args       = NULL;
        clnt_fd_ctx_t     *fdctx      = NULL;
        clnt_conf_t       *conf       = NULL;
        gfs3_fxattrop_req  req        = {{0,},};
        int                op_errno   = ESTALE;
        int                ret        = 0;
        size_t             dict_len   = 0;
        int                count      = 0;
        clnt_local_t    *local      = NULL;
        struct iobref     *rsp_iobref = NULL;
        struct iobuf      *rsp_iobuf  = NULL;
        struct iovec      *rsphdr     = NULL;
        struct iovec       vector[MAX_IOVEC] = {{0}, };

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        req.fd     = fdctx->remote_fd;
        req.flags  = args->flags;
        memcpy (req.gfid,  args->fd->inode->gfid, 16);

        local = GF_CALLOC (1, sizeof (*local),
                           gf_client_mt_clnt_local_t);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                op_errno = ENOMEM;
                goto unwind;
        }
        frame->local = local;

        rsp_iobref = iobref_new ();
        if (rsp_iobref == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                op_errno = ENOMEM;
                goto unwind;
        }

        rsp_iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (rsp_iobuf == NULL) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                goto unwind;
        }

        iobref_add (rsp_iobref, rsp_iobuf);
        iobuf_unref (rsp_iobuf);
        rsphdr = &vector[0];
        rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
        rsphdr->iov_len = rsp_iobuf->iobuf_arena->iobuf_pool->page_size;
        count = 1;
        rsp_iobuf = NULL;
        local->iobref = rsp_iobref;
        rsp_iobref = NULL;

        if (args->dict) {
                ret = dict_allocate_and_serialize (args->dict,
                                                   &req.dict.dict_val,
                                                   &dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized dict");
                        goto unwind;
                }
                req.dict.dict_len = dict_len;
        }

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FXATTROP,
                                     client3_1_fxattrop_cbk, NULL,
                                     xdr_from_fxattrop_req, rsphdr, count,
                                     NULL, 0, local->iobref);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        return 0;
unwind:
        local = frame->local;
        frame->local = NULL;

        STACK_UNWIND_STRICT (fxattrop, frame, -1, op_errno, NULL);

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        client_local_wipe (local);

        if (rsp_iobref) {
                iobref_unref (rsp_iobref);
        }

        if (rsp_iobuf) {
                iobuf_unref (rsp_iobuf);
        }

        return 0;
}



int32_t
client3_1_removexattr (call_frame_t *frame, xlator_t *this,
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

        if (!(args->loc && args->loc->inode))
                goto unwind;

        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        req.path = (char *)args->loc->path;
        req.name = (char *)args->name;

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_REMOVEXATTR,
                                     client3_1_removexattr_cbk, NULL,
                                     xdr_from_removexattr_req, NULL, 0, NULL,
                                     0, NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (removexattr, frame, -1, op_errno);
        return 0;
}


int32_t
client3_1_lk (call_frame_t *frame, xlator_t *this,
              void *data)
{
        clnt_args_t     *args       = NULL;
        gfs3_lk_req      req        = {{0,},};
        int32_t          gf_cmd     = 0;
        int32_t          gf_type    = 0;
        clnt_fd_ctx_t   *fdctx      = NULL;
        clnt_local_t    *local      = NULL;
        clnt_conf_t     *conf       = NULL;
        int              op_errno   = ESTALE;
        int              ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;
        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                op_errno = ENOMEM;
                goto unwind;
        }

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        ret = client_cmd_to_gf_cmd (args->cmd, &gf_cmd);
        if (ret) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_DEBUG,
                        "Unknown cmd (%d)!", gf_cmd);
        }

        switch (args->flock->l_type) {
        case F_RDLCK:
                gf_type = GF_LK_F_RDLCK;
                break;
        case F_WRLCK:
                gf_type = GF_LK_F_WRLCK;
                break;
        case F_UNLCK:
                gf_type = GF_LK_F_UNLCK;
                break;
        }

        local->owner = frame->root->lk_owner;
        local->cmd   = args->cmd;
        local->fd    = fd_ref (args->fd);
        frame->local = local;

        req.fd    = fdctx->remote_fd;
        req.cmd   = gf_cmd;
        req.type  = gf_type;
        gf_proto_flock_from_flock (&req.flock, args->flock);

        ret = client_submit_request (this, &req, frame, conf->fops, GFS3_OP_LK,
                                     client3_1_lk_cbk, NULL, xdr_from_lk_req,
                                     NULL, 0, NULL, 0, NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (lk, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
client3_1_inodelk (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_conf_t      *conf     = NULL;
        clnt_args_t      *args    = NULL;
        gfs3_inodelk_req  req     = {{0,},};
        int               ret     = 0;
        int32_t           gf_cmd  = 0;
        int32_t           gf_type = 0;
        int               op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        if (!(args->loc && args->loc->inode))
                goto unwind;

        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        if (args->cmd == F_GETLK || args->cmd == F_GETLK64)
                gf_cmd = GF_LK_GETLK;
        else if (args->cmd == F_SETLK || args->cmd == F_SETLK64)
                gf_cmd = GF_LK_SETLK;
        else if (args->cmd == F_SETLKW || args->cmd == F_SETLKW64)
                gf_cmd = GF_LK_SETLKW;
        else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Unknown cmd (%d)!", gf_cmd);
                op_errno = EINVAL;
                goto unwind;
        }

        switch (args->flock->l_type) {
        case F_RDLCK:
                gf_type = GF_LK_F_RDLCK;
                break;
        case F_WRLCK:
                gf_type = GF_LK_F_WRLCK;
                break;
        case F_UNLCK:
                gf_type = GF_LK_F_UNLCK;
                break;
        }

        req.path   = (char *)args->loc->path;
        req.volume = (char *)args->volume;
        req.cmd    = gf_cmd;
        req.type   = gf_type;
        gf_proto_flock_from_flock (&req.flock, args->flock);

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_INODELK,
                                     client3_1_inodelk_cbk, NULL,
                                     xdr_from_inodelk_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (inodelk, frame, -1, op_errno);
        return 0;
}



int32_t
client3_1_finodelk (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_args_t       *args     = NULL;
        gfs3_finodelk_req  req      = {{0,},};
        int32_t            gf_cmd   = 0;
        int32_t            gf_type  = 0;
        clnt_fd_ctx_t     *fdctx    = NULL;
        clnt_conf_t       *conf     = NULL;
        int                op_errno = ESTALE;
        int           ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (args->cmd == F_GETLK || args->cmd == F_GETLK64)
                gf_cmd = GF_LK_GETLK;
        else if (args->cmd == F_SETLK || args->cmd == F_SETLK64)
                gf_cmd = GF_LK_SETLK;
        else if (args->cmd == F_SETLKW || args->cmd == F_SETLKW64)
                gf_cmd = GF_LK_SETLKW;
        else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Unknown cmd (%d)!", gf_cmd);
                goto unwind;
        }

        switch (args->flock->l_type) {
        case F_RDLCK:
                gf_type = GF_LK_F_RDLCK;
                break;
        case F_WRLCK:
                gf_type = GF_LK_F_WRLCK;
                break;
        case F_UNLCK:
                gf_type = GF_LK_F_UNLCK;
                break;
        }

        req.volume = (char *)args->volume;
        req.fd    = fdctx->remote_fd;
        req.cmd   = gf_cmd;
        req.type  = gf_type;
        gf_proto_flock_from_flock (&req.flock, args->flock);

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FINODELK,
                                     client3_1_finodelk_cbk, NULL,
                                     xdr_from_finodelk_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (finodelk, frame, -1, op_errno);
        return 0;
}


int32_t
client3_1_entrylk (call_frame_t *frame, xlator_t *this,
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

        if (!(args->loc && args->loc->inode))
                goto unwind;

        memcpy (req.gfid,  args->loc->inode->gfid, 16);
        req.path = (char *)args->loc->path;
        req.cmd = args->cmd_entrylk;
        req.type = args->type;
        req.volume = (char *)args->volume;
        req.name = "";
        if (args->basename) {
                req.name = (char *)args->basename;
                req.namelen = 1;
        }

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_ENTRYLK,
                                     client3_1_entrylk_cbk, NULL,
                                     xdr_from_entrylk_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (entrylk, frame, -1, op_errno);
        return 0;
}



int32_t
client3_1_fentrylk (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_args_t       *args     = NULL;
        gfs3_fentrylk_req  req      = {{0,},};
        clnt_fd_ctx_t     *fdctx    = NULL;
        clnt_conf_t       *conf     = NULL;
        int                op_errno = ESTALE;
        int           ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        req.fd  = fdctx->remote_fd;
        req.cmd = args->cmd_entrylk;
        req.type = args->type;
        req.volume = (char *)args->volume;
        req.name = "";
        if (args->basename) {
                req.name = (char *)args->basename;
                req.namelen = 1;
        }

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FENTRYLK,
                                     client3_1_fentrylk_cbk, NULL,
                                     xdr_from_fentrylk_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (fentrylk, frame, -1, op_errno);
        return 0;
}


int32_t
client3_1_rchecksum (call_frame_t *frame, xlator_t *this,
                     void *data)
{
        clnt_args_t        *args     = NULL;
        clnt_fd_ctx_t      *fdctx    = NULL;
        clnt_conf_t        *conf     = NULL;
        gfs3_rchecksum_req  req      = {0,};
        int                 op_errno = ESTALE;
        int                 ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        req.len    = args->len;
        req.offset = args->offset;
        req.fd     = fdctx->remote_fd;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_RCHECKSUM,
                                     client3_1_rchecksum_cbk, NULL,
                                     xdr_from_rchecksum_req, NULL, 0, NULL,
                                     0, NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (rchecksum, frame, -1, op_errno, 0, NULL);
        return 0;
}



int32_t
client3_1_readdir (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_args_t      *args       = NULL;
        clnt_fd_ctx_t    *fdctx      = NULL;
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

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        readdir_rsp_size = xdr_sizeof ((xdrproc_t) xdr_gfs3_readdir_rsp, &rsp)
                + args->size;

        if ((readdir_rsp_size + GLUSTERFS_RPC_REPLY_SIZE + GLUSTERFS_RDMA_MAX_HEADER_SIZE)
            > (GLUSTERFS_RDMA_INLINE_THRESHOLD)) {
                local = GF_CALLOC (1, sizeof (*local),
                                   gf_client_mt_clnt_local_t);
                if (!local) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Out of memory");
                        op_errno = ENOMEM;
                        goto unwind;
                }
                frame->local = local;

                rsp_iobref = iobref_new ();
                if (rsp_iobref == NULL) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "out of memory");
                        goto unwind;
                }

                rsp_iobuf = iobuf_get (this->ctx->iobuf_pool);
                if (rsp_iobuf == NULL) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "out of memory");
                        goto unwind;
                }

                iobref_add (rsp_iobref, rsp_iobuf);
                iobuf_unref (rsp_iobuf);
                rsphdr = &vector[0];
                rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
                rsphdr->iov_len
                        = rsp_iobuf->iobuf_arena->iobuf_pool->page_size;
                count = 1;
                rsp_iobuf = NULL;
                local->iobref = rsp_iobref;
        }

        req.size = args->size;
        req.offset = args->offset;
        req.fd = fdctx->remote_fd;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_READDIR,
                                     client3_1_readdir_cbk, NULL,
                                     xdr_from_readdir_req, rsphdr, count,
                                     NULL, 0, rsp_iobref);
        rsp_iobref = NULL;

        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;

unwind:
        local = frame->local;
        frame->local = NULL;
        client_local_wipe (local);

        if (rsp_iobref != NULL) {
                iobref_unref (rsp_iobref);
        }

        if (rsp_iobuf != NULL) {
                iobuf_unref (rsp_iobuf);
        }

        STACK_UNWIND_STRICT (readdir, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
client3_1_readdirp (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_args_t      *args              = NULL;
        gfs3_readdirp_req req               = {{0,},};
        gfs3_readdirp_rsp rsp               = {0,};
        clnt_fd_ctx_t    *fdctx             = NULL;
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

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        readdirp_rsp_size = xdr_sizeof ((xdrproc_t) xdr_gfs3_readdirp_rsp, &rsp)
                + args->size;

        if ((readdirp_rsp_size + GLUSTERFS_RPC_REPLY_SIZE
             + GLUSTERFS_RDMA_MAX_HEADER_SIZE)
            > (GLUSTERFS_RDMA_INLINE_THRESHOLD)) {
                local = GF_CALLOC (1, sizeof (*local),
                                   gf_client_mt_clnt_local_t);
                if (!local) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Out of memory");
                        op_errno = ENOMEM;
                        goto unwind;
                }
                frame->local = local;

                rsp_iobref = iobref_new ();
                if (rsp_iobref == NULL) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "out of memory");
                        goto unwind;
                }

                rsp_iobuf = iobuf_get (this->ctx->iobuf_pool);
                if (rsp_iobuf == NULL) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "out of memory");
                        goto unwind;
                }

                iobref_add (rsp_iobref, rsp_iobuf);
                iobuf_unref (rsp_iobuf);
                rsphdr = &vector[0];
                rsphdr->iov_base = iobuf_ptr (rsp_iobuf);
                rsphdr->iov_len
                        = rsp_iobuf->iobuf_arena->iobuf_pool->page_size;
                count = 1;
                rsp_iobuf = NULL;
                local->iobref = rsp_iobref;
                rsp_iobref = NULL;
        }

        req.size = args->size;
        req.offset = args->offset;
        req.fd = fdctx->remote_fd;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_READDIRP,
                                     client3_1_readdirp_cbk, NULL,
                                     xdr_from_readdirp_req, rsphdr, count, NULL,
                                     0, rsp_iobref);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        local = frame->local;
        frame->local = NULL;
        client_local_wipe (local);

        if (rsp_iobref) {
                iobref_unref (rsp_iobref);
        }

        if (rsp_iobuf) {
                iobuf_unref (rsp_iobuf);
        }

        STACK_UNWIND_STRICT (readdirp, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
client3_1_setattr (call_frame_t *frame, xlator_t *this,
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

        if (!(args->loc && args->loc->inode))
                goto unwind;

        memcpy (req.gfid, args->loc->inode->gfid, 16);
        req.path = (char *)args->loc->path;
        req.valid = args->valid;
        gf_stat_from_iatt (&req.stbuf, args->stbuf);

        conf = this->private;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_SETATTR,
                                     client3_1_setattr_cbk, NULL,
                                     xdr_from_setattr_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (setattr, frame, -1, op_errno, NULL, NULL);
        return 0;
}

int32_t
client3_1_fsetattr (call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_args_t       *args     = NULL;
        clnt_fd_ctx_t     *fdctx    = NULL;
        clnt_conf_t       *conf     = NULL;
        gfs3_fsetattr_req  req      = {0,};
        int                op_errno = ESTALE;
        int                ret        = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (args->fd, this);
        }
        pthread_mutex_unlock (&conf->lock);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", args->fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        req.fd = fdctx->remote_fd;
        req.valid = args->valid;
        gf_stat_from_iatt (&req.stbuf, args->stbuf);

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_FSETATTR,
                                     client3_1_fsetattr_cbk, NULL,
                                     xdr_from_fsetattr_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret) {
                op_errno = ENOTCONN;
                goto unwind;
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (fsetattr, frame, -1, op_errno, NULL, NULL);
        return 0;
}



/* Table Specific to FOPS */


rpc_clnt_procedure_t clnt3_1_fop_actors[GF_FOP_MAXVALUE] = {
        [GF_FOP_NULL]        = { "NULL",        NULL},
        [GF_FOP_STAT]        = { "STAT",        client3_1_stat },
        [GF_FOP_READLINK]    = { "READLINK",    client3_1_readlink },
        [GF_FOP_MKNOD]       = { "MKNOD",       client3_1_mknod },
        [GF_FOP_MKDIR]       = { "MKDIR",       client3_1_mkdir },
        [GF_FOP_UNLINK]      = { "UNLINK",      client3_1_unlink },
        [GF_FOP_RMDIR]       = { "RMDIR",       client3_1_rmdir },
        [GF_FOP_SYMLINK]     = { "SYMLINK",     client3_1_symlink },
        [GF_FOP_RENAME]      = { "RENAME",      client3_1_rename },
        [GF_FOP_LINK]        = { "LINK",        client3_1_link },
        [GF_FOP_TRUNCATE]    = { "TRUNCATE",    client3_1_truncate },
        [GF_FOP_OPEN]        = { "OPEN",        client3_1_open },
        [GF_FOP_READ]        = { "READ",        client3_1_readv },
        [GF_FOP_WRITE]       = { "WRITE",       client3_1_writev },
        [GF_FOP_STATFS]      = { "STATFS",      client3_1_statfs },
        [GF_FOP_FLUSH]       = { "FLUSH",       client3_1_flush },
        [GF_FOP_FSYNC]       = { "FSYNC",       client3_1_fsync },
        [GF_FOP_SETXATTR]    = { "SETXATTR",    client3_1_setxattr },
        [GF_FOP_GETXATTR]    = { "GETXATTR",    client3_1_getxattr },
        [GF_FOP_REMOVEXATTR] = { "REMOVEXATTR", client3_1_removexattr },
        [GF_FOP_OPENDIR]     = { "OPENDIR",     client3_1_opendir },
        [GF_FOP_FSYNCDIR]    = { "FSYNCDIR",    client3_1_fsyncdir },
        [GF_FOP_ACCESS]      = { "ACCESS",      client3_1_access },
        [GF_FOP_CREATE]      = { "CREATE",      client3_1_create },
        [GF_FOP_FTRUNCATE]   = { "FTRUNCATE",   client3_1_ftruncate },
        [GF_FOP_FSTAT]       = { "FSTAT",       client3_1_fstat },
        [GF_FOP_LK]          = { "LK",          client3_1_lk },
        [GF_FOP_LOOKUP]      = { "LOOKUP",      client3_1_lookup },
        [GF_FOP_READDIR]     = { "READDIR",     client3_1_readdir },
        [GF_FOP_INODELK]     = { "INODELK",     client3_1_inodelk },
        [GF_FOP_FINODELK]    = { "FINODELK",    client3_1_finodelk },
        [GF_FOP_ENTRYLK]     = { "ENTRYLK",     client3_1_entrylk },
        [GF_FOP_FENTRYLK]    = { "FENTRYLK",    client3_1_fentrylk },
        [GF_FOP_XATTROP]     = { "XATTROP",     client3_1_xattrop },
        [GF_FOP_FXATTROP]    = { "FXATTROP",    client3_1_fxattrop },
        [GF_FOP_FGETXATTR]   = { "FGETXATTR",   client3_1_fgetxattr },
        [GF_FOP_FSETXATTR]   = { "FSETXATTR",   client3_1_fsetxattr },
        [GF_FOP_RCHECKSUM]   = { "RCHECKSUM",   client3_1_rchecksum },
        [GF_FOP_SETATTR]     = { "SETATTR",     client3_1_setattr },
        [GF_FOP_FSETATTR]    = { "FSETATTR",    client3_1_fsetattr },
        [GF_FOP_READDIRP]    = { "READDIRP",    client3_1_readdirp },
        [GF_FOP_RELEASE]     = { "RELEASE",     client3_1_release },
        [GF_FOP_RELEASEDIR]  = { "RELEASEDIR",  client3_1_releasedir },
        [GF_FOP_GETSPEC]     = { "GETSPEC",     client3_getspec },
};

/* Used From RPC-CLNT library to log proper name of procedure based on number */
char *clnt3_1_fop_names[GFS3_OP_MAXVALUE] = {
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
};

rpc_clnt_prog_t clnt3_1_fop_prog = {
        .progname  = "GlusterFS 3.1",
        .prognum   = GLUSTER3_1_FOP_PROGRAM,
        .progver   = GLUSTER3_1_FOP_VERSION,
        .numproc   = GLUSTER3_1_FOP_PROCCNT,
        .proctable = clnt3_1_fop_actors,
        .procnames = clnt3_1_fop_names,
};
