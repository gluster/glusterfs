/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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

#include "client.h"
#include "glusterfs-xdr.h"
#include "msg-xdr.h"
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
        int           ret        = 0;
        clnt_conf_t  *conf       = NULL;
        struct iovec  iov        = {0, };
        struct iobuf *iobuf      = NULL;
        int           count      = 0;
        char          new_iobref = 0;
        int           start_ping = 0;

        start_ping = 0;

        conf = this->private;

        iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (!iobuf) {
                goto out;
        };

        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref) {
                        goto out;
                }

                new_iobref = 1;
        }

        iobref_add (iobref, iobuf);

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
                               payload, payloadcnt, iobref, frame);

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
        if (new_iobref) {
                iobref_unref (iobref);
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

                ret = inode_ctx_put2 (inode, frame->this,
                                      stbuf.ia_ino, stbuf.ia_gen);
                if (ret < 0) {
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "SYMLINK %"PRId64"/%s (%s): failed to set "
                                "remote inode number to inode ctx",
                                local->loc.parent->ino, local->loc.name,
                                local->loc.path);
                }

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
        gfs3_mknod_rsp  rsp        = {0,};
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

                ret = inode_ctx_put2 (inode, frame->this,
                                      stbuf.ia_ino, stbuf.ia_gen);
                if (ret < 0) {
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "MKNOD %"PRId64"/%s (%s): failed to set "
                                "remote inode number to inode ctx",
                                local->loc.parent->ino, local->loc.name,
                                local->loc.path);
                }

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
        gfs3_mkdir_rsp  rsp        = {0,};
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

                ret = inode_ctx_put2 (inode, frame->this,
                                      stbuf.ia_ino, stbuf.ia_gen);
                if (ret < 0) {
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "MKDIR %"PRId64"/%s (%s): failed to set "
                                "remote inode number to inode ctx",
                                local->loc.parent->ino, local->loc.name,
                                local->loc.path);
                }

                gf_stat_to_iatt (&rsp.preparent, &preparent);
                gf_stat_to_iatt (&rsp.postparent, &postparent);
        }

out:
        frame->local = NULL;
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
        ino_t          ino   = 0;
        uint64_t       gen   = 0;
        int            ret   = 0;
        gfs3_open_rsp  rsp   = {0,};

        frame = myframe;
        local = frame->local;

        if (local->op) {
                local->op (req, iov, 1, myframe);
                return 0;
        }

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

                inode_ctx_get2 (fd->inode, frame->this, &ino, &gen);

                fdctx->remote_fd = rsp.fd;
                fdctx->inode     = inode_ref (fd->inode);
                fdctx->ino       = ino;
                fdctx->gen       = gen;
                fdctx->flags     = local->flags;
                fdctx->wbflags   = local->wbflags;

                INIT_LIST_HEAD (&fdctx->sfd_pos);

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
        int                op_errno = 0;
        gfs3_getxattr_rsp  rsp      = {0,};
        int                ret      = 0;

        frame = myframe;

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
        int                 op_errno = 0;

        frame = myframe;

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
        int               op_errno = 0;

        frame = myframe;

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

        op_ret = rsp.op_ret;
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
                             gf_error_to_errno (rsp.op_errno), dict);

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

        frame = myframe;

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

        op_ret = rsp.op_ret;
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
                             gf_error_to_errno (rsp.op_errno), dict);

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

                ret = inode_ctx_put2 (inode, frame->this,
                                      stbuf.ia_ino, stbuf.ia_gen);
                if (ret < 0) {
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "CREATE %"PRId64"/%s (%s): failed to set "
                                "remote inode number to inode ctx",
                                local->loc.parent->ino, local->loc.name,
                                local->loc.path);
                }

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
                fdctx->ino       = stbuf.ia_ino;
                fdctx->gen       = stbuf.ia_gen;
                fdctx->flags     = local->flags;

                INIT_LIST_HEAD (&fdctx->sfd_pos);

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
client3_1_checksum_cbk (struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
        call_frame_t *frame = NULL;
        gfs3_checksum_rsp rsp        = {0,};
        int              ret        = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_checksum_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:
        STACK_UNWIND_STRICT (checksum, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno),
                             (uint8_t *)rsp.fchecksum.fchecksum_val,
                             (uint8_t *)rsp.dchecksum.dchecksum_val);

        /* This is allocated by the libc while decoding RPC msg */
        /* Hence no 'GF_FREE', but just 'free' */
        if (rsp.fchecksum.fchecksum_val) {
                free (rsp.fchecksum.fchecksum_val);
        }
        if (rsp.dchecksum.dchecksum_val) {
                free (rsp.dchecksum.dchecksum_val);
        }
        return 0;
}

int
client3_1_lk_cbk (struct rpc_req *req, struct iovec *iov, int count,
                  void *myframe)
{
        call_frame_t  *frame = NULL;
        struct flock   lock = {0,};
        gfs3_lk_rsp    rsp        = {0,};
        int            ret        = 0;

        frame = myframe;

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
                gf_flock_to_flock (&rsp.flock, &lock);
        }

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
        gfs3_readdir_rsp        rsp = {0,};
        int32_t                 ret = 0;
        gf_dirent_t             entries;

        frame = myframe;

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
        gfs3_readdirp_rsp       rsp = {0,};
        int32_t                 ret = 0;
        gf_dirent_t             entries;

        frame = myframe;

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
                             &preoldparent, &postoldparent);

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
        ino_t                ino = 0;
        uint64_t             gen = 0;
        call_frame_t   *frame = NULL;
        fd_t             *fd = NULL;
        int ret = 0;
        gfs3_opendir_rsp  rsp = {0,};

        frame = myframe;
        local = frame->local;

        if (local->op) {
                local->op (req, iov, 1, myframe);
                return 0;
        }

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

                inode_ctx_get2 (fd->inode, frame->this, &ino, &gen);

                fdctx->remote_fd = rsp.fd;
                fdctx->inode     = inode_ref (fd->inode);
                fdctx->ino       = ino;
                fdctx->gen       = gen;

                fdctx->is_dir    = 1;

                INIT_LIST_HEAD (&fdctx->sfd_pos);

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
        int              op_errno   = 0;
        ino_t            oldino     = 0;
        uint64_t         oldgen     = 0;
        dict_t          *xattr      = NULL;
        inode_t         *inode      = NULL;
        char            *buf        = NULL;

        frame = myframe;
        local = frame->local;
        inode = local->loc.inode;
        frame->local = NULL;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_lookup_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        op_errno = gf_error_to_errno (rsp.op_errno);
        gf_stat_to_iatt (&rsp.postparent, &postparent);

        if (rsp.op_ret == 0) {
                rsp.op_ret = -1;
                gf_stat_to_iatt (&rsp.stat, &stbuf);

                ret = inode_ctx_get2 (inode, frame->this, &oldino, &oldgen);
                if (oldino != stbuf.ia_ino || oldgen != stbuf.ia_gen) {
                        if (oldino) {
                                gf_log (frame->this->name, GF_LOG_DEBUG,
                                        "LOOKUP %"PRId64"/%s (%s): "
                                        "inode number changed from "
                                        "{%"PRId64",%"PRId64"} to {%"PRId64",%"PRId64"}",
                                        local->loc.parent ?
                                        local->loc.parent->ino : (uint64_t) 0,
                                        local->loc.name,
                                        local->loc.path,
                                        oldgen, oldino, stbuf.ia_gen, stbuf.ia_ino);
                                op_errno = ESTALE;
                                goto out;
                        }

                        ret = inode_ctx_put2 (inode, frame->this,
                                              stbuf.ia_ino, stbuf.ia_gen);
                        if (ret < 0) {
                                gf_log (frame->this->name, GF_LOG_DEBUG,
                                        "LOOKUP %"PRId64"/%s (%s) : "
                                        "failed to set remote inode "
                                        "number to inode ctx",
                                        local->loc.parent ?
                                        local->loc.parent->ino : (uint64_t) 0,
                                        local->loc.name,
                                        local->loc.path);
                        }
                }

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

                rsp.op_ret = 0;
        }

out:
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
        call_frame_t  *frame  = NULL;
        struct iobref *iobref = NULL;
        struct iovec   vector = {0,};
        struct iatt    stat   = {0,};
        gfs3_read_rsp  rsp    = {0,};
        int            ret    = 0;

        frame = myframe;

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
                iobref = iobref_new ();
                gf_stat_to_iatt (&rsp.stat, &stat);
                vector.iov_len  = rsp.op_ret;

                if (rsp.op_ret > 0) {
                        vector.iov_base = req->rsp_procpayload->ptr;
                        iobref_add (iobref, req->rsp_procpayload);
                }
        }
out:
        STACK_UNWIND_STRICT (readv, frame, rsp.op_ret,
                             gf_error_to_errno (rsp.op_errno), &vector, 1,
                             &stat, iobref);

        if (iobref) {
                iobref_unref (iobref);
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
                gfs3_releasedir_req  req = {0,};
                req.fd = fdctx->remote_fd;
                req.gfs_id = GFS3_OP_RELEASEDIR;
                client_submit_request (this, &req, fr, &clnt3_1_fop_prog,
                                       GFS3_OP_RELEASEDIR, client3_1_releasedir_cbk,
                                       NULL, xdr_from_releasedir_req);
        } else {
                gfs3_release_req  req = {0,};
                req.fd = fdctx->remote_fd;
                req.gfs_id = GFS3_OP_RELEASE;
                client_submit_request (this, &req, fr, &clnt3_1_fop_prog,
                                       GFS3_OP_RELEASE, client3_1_release_cbk, NULL,
                                       xdr_from_release_req);
        }

out:
        fdctx->remote_fd = -1;
        inode_unref (fdctx->inode);
        GF_FREE (fdctx);

        return ret;
}

int
client3_1_reopen_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void           *myframe)
{
        int32_t        ret = -1;
        gfs3_open_rsp  rsp = {0,};
        clnt_local_t  *local = NULL;
        clnt_conf_t   *conf = NULL;
        clnt_fd_ctx_t *fdctx = NULL;
        call_frame_t  *frame = NULL;

        frame = myframe;

        local = frame->local;
        conf  = frame->this->private;
        fdctx = local->fdctx;

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

        gf_log (frame->this->name, GF_LOG_DEBUG,
                "reopen on %s returned %d (%"PRId64")",
                local->loc.path, rsp.op_ret, rsp.fd);

        if (-1 != rsp.op_ret) {
                pthread_mutex_lock (&conf->lock);
                {
                        fdctx->remote_fd = rsp.fd;

                        if (!fdctx->released) {
                                list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
                                fdctx = NULL;
                        }
                }
                pthread_mutex_unlock (&conf->lock);
        }

out:
        if (fdctx)
                client_fdctx_destroy (frame->this, fdctx);

        frame->local = NULL;
        STACK_DESTROY (frame->root);

        client_local_wipe (local);

        return 0;
}

int
client3_1_reopendir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                         void           *myframe)
{
        int32_t        ret   = -1;
        gfs3_open_rsp  rsp   = {0,};
        clnt_local_t  *local = NULL;
        clnt_conf_t   *conf  = NULL;
        clnt_fd_ctx_t *fdctx = NULL;
        call_frame_t  *frame = NULL;

        frame = myframe;
        if (!frame || !frame->this)
                goto out;

        local        = frame->local;
        frame->local = NULL;
        conf         = frame->this->private;
        fdctx        = local->fdctx;

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

        gf_log (frame->this->name, GF_LOG_DEBUG,
                "reopendir on %s returned %d (%"PRId64")",
                local->loc.path, rsp.op_ret, rsp.fd);

        if (fdctx) {
                pthread_mutex_lock (&conf->lock);
                {
                        fdctx->remote_fd = rsp.fd;

                        if (!fdctx->released) {
                                list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
                                fdctx = NULL;
                        }
                }
                pthread_mutex_unlock (&conf->lock);
        }

out:
        if (fdctx)
                client_fdctx_destroy (frame->this, fdctx);

        frame->local = NULL;
        STACK_DESTROY (frame->root);

        client_local_wipe (local);

        return 0;
}

int
protocol_client_reopendir (xlator_t *this, clnt_fd_ctx_t *fdctx)
{
        int               ret   = -1;
        gfs3_opendir_req  req   = {0,};
        clnt_local_t     *local = NULL;
        inode_t          *inode = NULL;
        char             *path  = NULL;
        call_frame_t     *frame = NULL;
        clnt_conf_t      *conf  = NULL;

        if (!this || !fdctx)
                goto out;

        inode = fdctx->inode;
        conf = this->private;

        ret = inode_path (inode, NULL, &path);
        if (ret < 0) {
                goto out;
        }

        local = GF_CALLOC (1, sizeof (*local), 0);
        if (!local) {
                goto out;
        }

        local->fdctx    = fdctx;
        local->op       = client3_1_reopendir_cbk;
        local->loc.path = path;
        path            = NULL;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                goto out;
        }

        req.ino   = fdctx->ino;
        req.gen   = fdctx->gen;
        req.path  = (char *)local->loc.path;
        req.gfs_id = GFS3_OP_OPENDIR;

        gf_log (frame->this->name, GF_LOG_DEBUG,
                "attempting reopen on %s", local->loc.path);

        frame->local = local; local = NULL;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_OPENDIR,
                               client3_1_opendir_cbk, NULL, xdr_from_opendir_req);

        return ret;

out:
        if (frame) {
                frame->local = NULL;
                STACK_DESTROY (frame->root);
        }

        if (local)
                client_local_wipe (local);

        if (path)
                GF_FREE (path);

        return 0;

}

int
protocol_client_reopen (xlator_t *this, clnt_fd_ctx_t *fdctx)
{
        int            ret   = -1;
        gfs3_open_req  req   = {0,};
        clnt_local_t  *local = NULL;
        inode_t       *inode = NULL;
        char          *path  = NULL;
        call_frame_t  *frame = NULL;
        clnt_conf_t   *conf  = NULL;

        if (!this || !fdctx)
                goto out;

        inode = fdctx->inode;
        conf  = this->private;

        ret = inode_path (inode, NULL, &path);
        if (ret < 0) {
                goto out;
        }

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                goto out;
        }

        local = GF_CALLOC (1, sizeof (*local), 0);
        if (!local) {
                goto out;
        }

        local->fdctx    = fdctx;
        local->op       = client3_1_reopen_cbk;
        local->loc.path = path;
        path            = NULL;
        frame->local    = local;

        req.ino      = fdctx->ino;
        req.gen      = fdctx->gen;
        req.flags    = gf_flags_from_flags (fdctx->flags);
        req.wbflags  = fdctx->wbflags;
        req.path     = (char *)local->loc.path;
        req.gfs_id = GFS3_OP_OPEN;

        gf_log (frame->this->name, GF_LOG_DEBUG,
                "attempting reopen on %s", local->loc.path);

        local = NULL;
        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_OPEN,
                               client3_1_open_cbk, NULL, xdr_from_open_req);

        return ret;

out:
        if (frame) {
                frame->local = NULL;
                STACK_DESTROY (frame->root);
        }

        if (local)
                client_local_wipe (local);

        if (path)
                GF_FREE (path);

        return 0;

}



int32_t
client3_1_releasedir (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        clnt_conf_t         *conf = NULL;
        clnt_fd_ctx_t       *fdctx = NULL;
        clnt_args_t         *args = NULL;
        gfs3_releasedir_req  req = {0,};
        int64_t              remote_fd = -1;

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
                req.gfs_id = GFS3_OP_RELEASEDIR;
                client_submit_request (this, &req, frame, conf->fops,
                                       GFS3_OP_RELEASEDIR, client3_1_releasedir_cbk,
                                       NULL, xdr_from_releasedir_req);
                inode_unref (fdctx->inode);
                GF_FREE (fdctx);
        }

        return 0;
unwind:
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
        gfs3_release_req  req       = {0,};

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
                req.gfs_id = GFS3_OP_RELEASE;
                client_submit_request (this, &req, frame, conf->fops,
                                       GFS3_OP_RELEASE, client3_1_release_cbk, NULL,
                                       xdr_from_release_req);
                inode_unref (fdctx->inode);
                GF_FREE (fdctx);
        }
        return 0;
unwind:
        return 0;
}


int32_t
client3_1_lookup (call_frame_t *frame, xlator_t *this,
                  void *data)
{
        clnt_conf_t     *conf     = NULL;
        clnt_local_t    *local    = NULL;
        clnt_args_t     *args     = NULL;
        gfs3_lookup_req  req      = {0,};
        int              ret      = 0;
        size_t           dict_len = 0;
        int              op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        conf = this->private;
        args = data;
        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }

        loc_copy (&local->loc, args->loc);
        frame->local = local;

        if (args->loc->ino != 1 && args->loc->parent) {
                ret = inode_ctx_get2 (args->loc->parent, this,
                                      &req.par, &req.gen);
                if (args->loc->parent->ino && ret < 0) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "LOOKUP %"PRId64"/%s (%s): failed to get "
                                "remote inode number for parent",
                                args->loc->parent->ino, args->loc->name,
                                args->loc->path);
                        goto unwind;
                }
                GF_VALIDATE_OR_GOTO (this->name, args->loc->name, unwind);
        } else {
                req.ino = 1;
        }

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

        req.path          = (char *)args->loc->path;
        req.bname         = (char *)args->loc->name;
        req.dict.dict_len = dict_len;
        req.gfs_id        = GFS3_OP_LOOKUP;

        client_submit_request (this, &req, frame, conf->fops,
                               GFS3_OP_LOOKUP, client3_1_lookup_cbk,
                               NULL, xdr_from_lookup_req);

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        return 0;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);

        if (local)
                client_local_wipe (local);

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        return 0;
}



int32_t
client3_1_stat (call_frame_t *frame, xlator_t *this,
                void *data)
{
        clnt_conf_t   *conf     = NULL;
        clnt_args_t   *args     = NULL;
        gfs3_stat_req  req      = {0,};
        int            ret      = 0;
        int            op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "STAT %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }
        req.path = (char *)args->loc->path;
        req.gfs_id = GFS3_OP_STAT;
        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_STAT,
                               client3_1_stat_cbk, NULL, xdr_from_stat_req);

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
        gfs3_truncate_req  req      = {0,};
        int                ret      = 0;
        int                op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "STAT %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }
        req.path = (char *)args->loc->path;
        req.offset = args->offset;
        req.gfs_id = GFS3_OP_TRUNCATE;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_TRUNCATE,
                               client3_1_truncate_cbk, NULL, xdr_from_truncate_req);

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
        gfs3_ftruncate_req  req      = {0,};
        int                 op_errno = EINVAL;

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
        req.gfs_id = GFS3_OP_FTRUNCATE;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_FTRUNCATE,
                               client3_1_ftruncate_cbk, NULL, xdr_from_ftruncate_req);

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
        gfs3_access_req  req      = {0,};
        int              ret      = 0;
        int              op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "STAT %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }
        req.path = (char *)args->loc->path;
        req.mask = args->mask;
        req.gfs_id = GFS3_OP_ACCESS;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_ACCESS,
                               client3_1_access_cbk, NULL, xdr_from_access_req);

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
        gfs3_readlink_req  req      = {0,};
        int                ret      = 0;
        int                op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "STAT %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }
        req.path = (char *)args->loc->path;
        req.size = args->size;
        req.gfs_id = GFS3_OP_READLINK;
        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_READLINK,
                               client3_1_readlink_cbk, NULL, xdr_from_readlink_req);

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
        gfs3_unlink_req  req      = {0,};
        int              ret      = 0;
        int              op_errno = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->parent, this, &req.par, &req.gen);
        if (args->loc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "STAT %"PRId64"/%s (%s): "
                        "failed to get remote inode number for parent",
                        args->loc->parent->ino, args->loc->name, args->loc->path);
                        goto unwind;
        }
        req.path  = (char *)args->loc->path;
        req.bname = (char *)args->loc->name;
        req.gfs_id = GFS3_OP_UNLINK;
        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_UNLINK,
                               client3_1_unlink_cbk, NULL, xdr_from_unlink_req);

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
        gfs3_rmdir_req  req      = {0,};
        int             ret      = 0;
        int             op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->parent, this, &req.par, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "STAT %"PRId64"/%s (%s): "
                        "failed to get remote inode number for parent",
                        args->loc->parent->ino, args->loc->name, args->loc->path);
                        goto unwind;
        }
        req.path  = (char *)args->loc->path;
        req.bname = (char *)args->loc->name;
        req.gfs_id = GFS3_OP_RMDIR;
        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_RMDIR,
                               client3_1_rmdir_cbk, NULL, xdr_from_rmdir_req);

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
        gfs3_symlink_req  req      = {0,};
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

        loc_copy (&local->loc, args->loc);
        frame->local = local;

        ret = inode_ctx_get2 (args->loc->parent, this, &req.par, &req.gen);
        if (args->loc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "SYMLINK %"PRId64"/%s (%s): failed to get remote inode"
                        " number parent",
                        args->loc->parent->ino, args->loc->name,
                        args->loc->path);
                        goto unwind;
        }

        req.path     = (char *)args->loc->path;
        req.linkname = (char *)args->linkname;
        req.bname    = (char *)args->loc->name;
        req.gfs_id = GFS3_OP_SYMLINK;
        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_SYMLINK,
                               client3_1_symlink_cbk, NULL, xdr_from_symlink_req);

        return 0;
unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (symlink, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        if (local)
                client_local_wipe (local);
        return 0;
}



int32_t
client3_1_rename (call_frame_t *frame, xlator_t *this,
                  void *data)
{
        clnt_conf_t     *conf     = NULL;
        clnt_args_t     *args     = NULL;
        gfs3_rename_req  req      = {0,};
        int              ret      = 0;
        int              op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->oldloc->parent, this,
                              &req.oldpar, &req.oldgen);
        if (args->oldloc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "RENAME %"PRId64"/%s (%s): failed to get remote inode "
                        "number for source parent", args->oldloc->parent->ino,
                        args->oldloc->name, args->oldloc->path);
                        goto unwind;
        }

        ret = inode_ctx_get2 (args->newloc->parent, this, &req.newpar,
                              &req.newgen);
        if (args->newloc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "CREATE %"PRId64"/%s (%s): failed to get remote inode "
                        "number for destination parent",
                        args->newloc->parent->ino, args->newloc->name,
                        args->newloc->path);
                        goto unwind;
        }

        req.oldpath = (char *)args->oldloc->path;
        req.oldbname =  (char *)args->oldloc->name;
        req.newpath = (char *)args->newloc->path;
        req.newbname = (char *)args->newloc->name;
        req.gfs_id = GFS3_OP_RENAME;
        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_RENAME,
                               client3_1_rename_cbk, NULL, xdr_from_rename_req);

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
        gfs3_link_req  req      = {0,};
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

        loc_copy (&local->loc, args->oldloc);
        frame->local = local;

        ret = inode_ctx_get2 (args->oldloc->inode, this,
                              &req.oldino, &req.oldgen);
        if (args->oldloc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "RENAME %"PRId64"/%s (%s): failed to get remote inode "
                        "number for source parent", args->oldloc->parent->ino,
                        args->oldloc->name, args->oldloc->path);
                        goto unwind;
        }

        ret = inode_ctx_get2 (args->newloc->parent, this, &req.newpar,
                              &req.newgen);
        if (args->newloc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "CREATE %"PRId64"/%s (%s): failed to get remote inode "
                        "number for destination parent",
                        args->newloc->parent->ino, args->newloc->name,
                        args->newloc->path);
                        goto unwind;
        }

        req.oldpath = (char *)args->oldloc->path;
        req.newpath = (char *)args->newloc->path;
        req.newbname = (char *)args->newloc->name;
        req.gfs_id = GFS3_OP_LINK;
        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_LINK,
                               client3_1_link_cbk, NULL, xdr_from_link_req);

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
        gfs3_mknod_req  req      = {0,};
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

        loc_copy (&local->loc, args->loc);
        frame->local = local;

        ret = inode_ctx_get2 (args->loc->parent, this, &req.par, &req.gen);
        if (args->loc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "SYMLINK %"PRId64"/%s (%s): failed to get remote inode"
                        " number parent",
                        args->loc->parent->ino, args->loc->name,
                        args->loc->path);
                        goto unwind;
        }

        req.path   = (char *)args->loc->path;
        req.bname  = (char *)args->loc->name;
        req.mode   = args->mode;
        req.dev    = args->rdev;
        req.gfs_id = GFS3_OP_MKNOD;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_MKNOD,
                               client3_1_mknod_cbk, NULL, xdr_from_mknod_req);

        return 0;
unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (mknod, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        if (local)
                client_local_wipe (local);
        return 0;
}



int32_t
client3_1_mkdir (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        clnt_local_t   *local    = NULL;
        clnt_conf_t    *conf     = NULL;
        clnt_args_t    *args     = NULL;
        gfs3_mkdir_req  req      = {0,};
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

        loc_copy (&local->loc, args->loc);
        frame->local = local;

        ret = inode_ctx_get2 (args->loc->parent, this, &req.par, &req.gen);
        if (args->loc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "SYMLINK %"PRId64"/%s (%s): failed to get remote inode"
                        " number parent",
                        args->loc->parent->ino, args->loc->name,
                        args->loc->path);
                        goto unwind;
        }

        req.path  = (char *)args->loc->path;
        req.bname = (char *)args->loc->name;
        req.mode  = args->mode;
        req.gfs_id = GFS3_OP_MKDIR;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_MKDIR,
                               client3_1_mkdir_cbk, NULL, xdr_from_mkdir_req);

        return 0;
unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        if (local)
                client_local_wipe (local);
        return 0;
}


int32_t
client3_1_create (call_frame_t *frame, xlator_t *this,
                  void *data)
{
        clnt_local_t    *local    = NULL;
        clnt_conf_t     *conf     = NULL;
        clnt_args_t     *args     = NULL;
        gfs3_create_req  req      = {0,};
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
        local->fd = fd_ref (args->fd);
        local->flags = args->flags;
        loc_copy (&local->loc, args->loc);
        frame->local = local;

        ret = inode_ctx_get2 (args->loc->parent, this, &req.par, &req.gen);
        if (args->loc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "SYMLINK %"PRId64"/%s (%s): failed to get remote inode"
                        " number parent",
                        args->loc->parent->ino, args->loc->name,
                        args->loc->path);
                        goto unwind;
        }

        req.path  = (char *)args->loc->path;
        req.bname = (char *)args->loc->name;
        req.mode  = args->mode;
        req.flags = gf_flags_from_flags (args->flags);
        req.gfs_id = GFS3_OP_CREATE;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_CREATE,
                               client3_1_create_cbk, NULL, xdr_from_create_req);

        return 0;
unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (create, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);
        if (local)
                client_local_wipe (local);
        return 0;
}



int32_t
client3_1_open (call_frame_t *frame, xlator_t *this,
                void *data)
{
        clnt_local_t  *local    = NULL;
        clnt_conf_t   *conf     = NULL;
        clnt_args_t   *args     = NULL;
        gfs3_open_req  req      = {0,};
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
        local->fd = fd_ref (args->fd);
        local->flags = args->flags;
        local->wbflags = args->wbflags;
        loc_copy (&local->loc, args->loc);
        frame->local = local;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "OPEN %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }
        req.flags = gf_flags_from_flags (args->flags);
        req.wbflags = args->wbflags;
        req.path = (char *)args->loc->path;
        req.gfs_id = GFS3_OP_OPEN;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_OPEN,
                               client3_1_open_cbk, NULL, xdr_from_open_req);

        return 0;
unwind:
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
        clnt_args_t   *args     = NULL;
        clnt_fd_ctx_t *fdctx    = NULL;
        clnt_conf_t   *conf     = NULL;
        int            op_errno = ESTALE;
        gfs3_read_req  req      = {0,};

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
        req.gfs_id = GFS3_OP_READ;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_READ,
                               client3_1_readv_cbk, NULL, xdr_from_readv_req);

        return 0;
unwind:
        STACK_UNWIND_STRICT (readv, frame, -1, op_errno, NULL, 0, NULL, NULL);
        return 0;
}


int32_t
client3_1_writev (call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_args_t    *args     = NULL;
        clnt_fd_ctx_t  *fdctx    = NULL;
        clnt_conf_t    *conf     = NULL;
        gfs3_write_req  req      = {0,};
        int             op_errno = ESTALE;

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
        req.gfs_id = GFS3_OP_WRITE;

        client_submit_vec_request (this, &req, frame, conf->fops, GFS3_OP_WRITE,
                                   client3_1_writev_cbk,
                                   args->vector, args->count,
                                   args->iobref, xdr_from_writev_req);

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
        gfs3_flush_req  req      = {0,};
        clnt_fd_ctx_t  *fdctx    = NULL;
        clnt_conf_t    *conf     = NULL;
        int             op_errno = ESTALE;

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
        req.gfs_id = GFS3_OP_FLUSH;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_FLUSH,
                               client3_1_flush_cbk, NULL, xdr_from_flush_req);

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
        gfs3_fsync_req  req      = {0,};
        clnt_fd_ctx_t  *fdctx    = NULL;
        clnt_conf_t    *conf     = NULL;
        int             op_errno = 0;

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
        req.gfs_id = GFS3_OP_FSYNC;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_FSYNC,
                               client3_1_fsync_cbk, NULL, xdr_from_fsync_req);

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
        gfs3_fstat_req  req      = {0,};
        clnt_fd_ctx_t  *fdctx    = NULL;
        clnt_conf_t    *conf     = NULL;
        int             op_errno = ESTALE;

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
        req.gfs_id = GFS3_OP_FSTAT;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_FSTAT,
                               client3_1_fstat_cbk, NULL, xdr_from_fstat_req);

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
        gfs3_opendir_req  req      = {0,};
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
        local->fd = fd_ref (args->fd);
        loc_copy (&local->loc, args->loc);
        frame->local = local;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "OPEN %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }
        req.path = (char *)args->loc->path;
        req.gfs_id = GFS3_OP_OPENDIR;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_OPENDIR,
                               client3_1_opendir_cbk, NULL, xdr_from_opendir_req);

        return 0;
unwind:
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
        gfs3_fsyncdir_req  req      = {0,};

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
        req.gfs_id = GFS3_OP_FSYNCDIR;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_FSYNCDIR,
                               client3_1_fsyncdir_cbk, NULL, xdr_from_fsyncdir_req);

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
        gfs3_statfs_req  req      = {0,};
        int              ret      = 0;
        int              op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        if (args->loc->inode) {
                ret = inode_ctx_get2 (args->loc->inode, this,
                                      &req.ino, &req.gen);
                if (args->loc->inode->ino && ret < 0) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "STATFS %"PRId64" (%s): "
                                "failed to get remote inode number",
                                args->loc->inode->ino, args->loc->path);
                        goto unwind;
                }
        }
        req.path = (char *)args->loc->path;
        req.gfs_id = GFS3_OP_STATFS;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_STATFS,
                               client3_1_statfs_cbk, NULL, xdr_from_statfs_req);

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
        gfs3_setxattr_req  req      = {0,};
        int                ret      = 0;
        size_t             dict_len = 0;
        int                op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "SETXATTR %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }
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
        req.gfs_id = GFS3_OP_SETXATTR;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_SETXATTR,
                               client3_1_setxattr_cbk, NULL, xdr_from_setxattr_req);

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
        gfs3_fsetxattr_req  req      = {0,};
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
        req.ino   = args->fd->inode->ino;
        req.gfs_id = GFS3_OP_FSETXATTR;

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

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_FSETXATTR,
                               client3_1_fsetxattr_cbk, NULL, xdr_from_fsetxattr_req);

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
        clnt_args_t        *args     = NULL;
        clnt_fd_ctx_t      *fdctx    = NULL;
        clnt_conf_t        *conf     = NULL;
        gfs3_fgetxattr_req  req      = {0,};
        int                 op_errno = ESTALE;

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

        req.namelen = 1; /* Use it as a flag */
        req.fd   = fdctx->remote_fd;
        req.name = (char *)args->name;
        if (!req.name) {
                req.name = "";
                req.namelen = 0;
        }
        req.gfs_id = GFS3_OP_FGETXATTR;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_FGETXATTR,
                               client3_1_fgetxattr_cbk, NULL, xdr_from_fgetxattr_req);

        return 0;
unwind:
        STACK_UNWIND_STRICT (fgetxattr, frame, -1, op_errno, NULL);
        return 0;
}



int32_t
client3_1_getxattr (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_conf_t       *conf     = NULL;
        clnt_args_t       *args     = NULL;
        gfs3_getxattr_req  req      = {0,};
        int                ret      = 0;
        int                op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "STAT %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }

        req.namelen = 1; /* Use it as a flag */
        req.path = (char *)args->loc->path;
        req.name = (char *)args->name;
        if (!req.name) {
                req.name = "";
                req.namelen = 0;
        }
        req.gfs_id = GFS3_OP_GETXATTR;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_GETXATTR,
                               client3_1_getxattr_cbk, NULL, xdr_from_getxattr_req);

        return 0;
unwind:
        STACK_UNWIND_STRICT (getxattr, frame, -1, op_errno, NULL);
        return 0;
}



int32_t
client3_1_xattrop (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_conf_t      *conf     = NULL;
        clnt_args_t      *args     = NULL;
        gfs3_xattrop_req  req      = {0,};
        int               ret      = 0;
        size_t            dict_len = 0;
        int               op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "SETXATTR %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }
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
        req.gfs_id = GFS3_OP_XATTROP;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_XATTROP,
                               client3_1_xattrop_cbk, NULL, xdr_from_xattrop_req);

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
unwind:
        STACK_UNWIND_STRICT (xattrop, frame, -1, op_errno, NULL);
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
}



int32_t
client3_1_fxattrop (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_args_t       *args     = NULL;
        clnt_fd_ctx_t     *fdctx    = NULL;
        clnt_conf_t       *conf     = NULL;
        gfs3_fxattrop_req  req      = {0,};
        int                op_errno = ESTALE;
        int                ret      = 0;
        size_t             dict_len = 0;

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
        req.ino   = args->fd->inode->ino;
        req.gfs_id = GFS3_OP_FXATTROP;

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

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_FXATTROP,
                               client3_1_fxattrop_cbk, NULL, xdr_from_fxattrop_req);
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (fxattrop, frame, -1, op_errno, NULL);
        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }
        return 0;
}



int32_t
client3_1_removexattr (call_frame_t *frame, xlator_t *this,
                       void *data)
{
        clnt_conf_t          *conf     = NULL;
        clnt_args_t          *args     = NULL;
        gfs3_removexattr_req  req      = {0,};
        int                   ret      = 0;
        int                   op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "REMOVEXATTR %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }
        req.path = (char *)args->loc->path;
        req.name = (char *)args->name;
        req.gfs_id = GFS3_OP_REMOVEXATTR;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_REMOVEXATTR,
                               client3_1_removexattr_cbk, NULL, xdr_from_removexattr_req);

        return 0;
unwind:
        STACK_UNWIND_STRICT (removexattr, frame, -1, op_errno);
        return 0;
}


int32_t
client3_1_lk (call_frame_t *frame, xlator_t *this,
              void *data)
{
        clnt_args_t   *args     = NULL;
        gfs3_lk_req    req      = {0,};
        int32_t        gf_cmd   = 0;
        int32_t        gf_type  = 0;
        clnt_fd_ctx_t *fdctx    = NULL;
        clnt_conf_t   *conf     = NULL;
        int            op_errno = ESTALE;

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

        req.fd    = fdctx->remote_fd;
        req.cmd   = gf_cmd;
        req.type  = gf_type;
        gf_flock_from_flock (&req.flock, args->flock);
        req.gfs_id = GFS3_OP_LK;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_LK,
                               client3_1_lk_cbk, NULL, xdr_from_lk_req);

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
        gfs3_inodelk_req  req     = {0,};
        int               ret     = 0;
        int32_t           gf_cmd  = 0;
        int32_t           gf_type = 0;
        int               op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "INODELK %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
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
        gf_flock_from_flock (&req.flock, args->flock);
        req.gfs_id = GFS3_OP_INODELK;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_INODELK,
                               client3_1_inodelk_cbk, NULL, xdr_from_inodelk_req);

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
        gfs3_finodelk_req  req      = {0,};
        int32_t            gf_cmd   = 0;
        int32_t            gf_type  = 0;
        clnt_fd_ctx_t     *fdctx    = NULL;
        clnt_conf_t       *conf     = NULL;
        int                op_errno = ESTALE;

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
        gf_flock_from_flock (&req.flock, args->flock);
        req.gfs_id = GFS3_OP_FINODELK;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_FINODELK,
                               client3_1_finodelk_cbk, NULL, xdr_from_finodelk_req);

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
        gfs3_entrylk_req  req      = {0,};
        int               ret      = 0;
        int               op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "STAT %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }
        req.path = (char *)args->loc->path;
        req.cmd = args->cmd_entrylk;
        req.type = args->type;
        req.volume = (char *)args->volume;
        req.name = "";
        if (args->basename) {
                req.name = (char *)args->basename;
                req.namelen = 1;
        }
        req.gfs_id = GFS3_OP_ENTRYLK;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_ENTRYLK,
                               client3_1_entrylk_cbk, NULL, xdr_from_entrylk_req);

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
        gfs3_fentrylk_req  req      = {0,};
        clnt_fd_ctx_t     *fdctx    = NULL;
        clnt_conf_t       *conf     = NULL;
        int                op_errno = ESTALE;

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
        req.gfs_id = GFS3_OP_FENTRYLK;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_FENTRYLK,
                               client3_1_fentrylk_cbk, NULL, xdr_from_fentrylk_req);

        return 0;
unwind:
        STACK_UNWIND_STRICT (fentrylk, frame, -1, op_errno);
        return 0;
}




int32_t
client3_1_checksum (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_conf_t       *conf     = NULL;
        clnt_args_t       *args     = NULL;
        gfs3_checksum_req  req      = {0,};
        int                ret      = 0;
        int                op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "STAT %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }
        req.path = (char *)args->loc->path;
        req.flag = args->flags;
        req.gfs_id = GFS3_OP_CHECKSUM;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_CHECKSUM,
                               client3_1_checksum_cbk, NULL, xdr_from_checksum_req);

        return 0;
unwind:
        STACK_UNWIND_STRICT (checksum, frame, -1, op_errno, NULL, NULL);
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
        req.gfs_id = GFS3_OP_RCHECKSUM;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_RCHECKSUM,
                               client3_1_rchecksum_cbk, NULL, xdr_from_rchecksum_req);

        return 0;
unwind:
        STACK_UNWIND_STRICT (rchecksum, frame, -1, op_errno, 0, NULL);
        return 0;
}



int32_t
client3_1_readdir (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_args_t      *args     = NULL;
        clnt_fd_ctx_t    *fdctx    = NULL;
        clnt_conf_t      *conf     = NULL;
        gfs3_readdir_req  req      = {0,};
        int               op_errno = ESTALE;

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

        req.size = args->size;
        req.offset = args->offset;
        req.fd = fdctx->remote_fd;
        req.gfs_id = GFS3_OP_READDIR;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_READDIR,
                               client3_1_readdir_cbk, NULL, xdr_from_readdir_req);

        return 0;
unwind:
        STACK_UNWIND_STRICT (readdir, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
client3_1_readdirp (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        clnt_args_t       *args     = NULL;
        gfs3_readdirp_req  req      = {0,};
        clnt_fd_ctx_t     *fdctx    = NULL;
        clnt_conf_t       *conf     = NULL;
        int                op_errno = ESTALE;

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

        req.size = args->size;
        req.offset = args->offset;
        req.fd = fdctx->remote_fd;
        req.gfs_id = GFS3_OP_READDIRP;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_READDIRP,
                               client3_1_readdirp_cbk, NULL, xdr_from_readdirp_req);

        return 0;
unwind:
        STACK_UNWIND_STRICT (readdirp, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
client3_1_setattr (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        clnt_conf_t      *conf     = NULL;
        clnt_args_t      *args     = NULL;
        gfs3_setattr_req  req      = {0,};
        int               ret      = 0;
        int               op_errno = ESTALE;

        if (!frame || !this || !data)
                goto unwind;

        args = data;

        ret = inode_ctx_get2 (args->loc->inode, this, &req.ino, &req.gen);
        if (args->loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "STAT %"PRId64" (%s): "
                        "failed to get remote inode number",
                        args->loc->inode->ino, args->loc->path);
                        goto unwind;
        }
        req.path = (char *)args->loc->path;
        req.valid = args->valid;
        gf_stat_from_iatt (&req.stbuf, args->stbuf);
        req.gfs_id = GFS3_OP_SETATTR;

        conf = this->private;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_SETATTR,
                               client3_1_setattr_cbk, NULL, xdr_from_setattr_req);

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
        req.gfs_id = GFS3_OP_FSETATTR;

        client_submit_request (this, &req, frame, conf->fops, GFS3_OP_FSETATTR,
                               client3_1_fsetattr_cbk, NULL, xdr_from_fsetattr_req);

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
        [GF_FOP_CHECKSUM]    = { "CHECKSUM",    client3_1_checksum },
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
        [GFS3_OP_CHECKSUM]    = "CHECKSUM",
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

rpc_clnt_prog_t clnt3_1_mgmt_prog = {
        .progname = "Gluster Mgmt 3.1",
        .prognum  = GLUSTER1_MGMT_PROGRAM,
        .progver  = GLUSTER1_MGMT_VERSION,
};
