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
#include "xlator.h"
#include "defaults.h"
#include "glusterfs.h"
#include "statedump.h"
#include "compat-errno.h"

#include "glusterfs3.h"
#include "portmap.h"

extern rpc_clnt_prog_t clnt3_1_fop_prog;
extern rpc_clnt_prog_t clnt_pmap_prog;

int client_ping_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe);

/* Handshake */

void
rpc_client_ping_timer_expired (void *data)
{
        rpc_transport_t         *trans              = NULL;
        rpc_clnt_connection_t   *conn               = NULL;
        int                      disconnect         = 0;
        int                      transport_activity = 0;
        struct timeval           timeout            = {0, };
        struct timeval           current            = {0, };
        struct rpc_clnt         *clnt               = NULL;
        xlator_t                *this               = NULL;
        clnt_conf_t             *conf               = NULL;

        this = data;

        if (!this || !this->private) {
                goto out;
        }

        conf = this->private;

        clnt = conf->rpc;
        if (!clnt)
                goto out;

        conn = &clnt->conn;
        trans = conn->trans;

        if (!trans)
                goto out;

        pthread_mutex_lock (&conn->lock);
        {
                if (conn->ping_timer)
                        gf_timer_call_cancel (this->ctx,
                                              conn->ping_timer);
                gettimeofday (&current, NULL);

                if (((current.tv_sec - conn->last_received.tv_sec) <
                     conf->opt.ping_timeout)
                    || ((current.tv_sec - conn->last_sent.tv_sec) <
                        conf->opt.ping_timeout)) {
                        transport_activity = 1;
                }

                if (transport_activity) {
                        gf_log (trans->name, GF_LOG_TRACE,
                                "ping timer expired but transport activity "
                                "detected - not bailing transport");
                        timeout.tv_sec = conf->opt.ping_timeout;
                        timeout.tv_usec = 0;

                        conn->ping_timer =
                                gf_timer_call_after (this->ctx, timeout,
                                                     rpc_client_ping_timer_expired,
                                                     (void *) this);
                        if (conn->ping_timer == NULL)
                                gf_log (trans->name, GF_LOG_DEBUG,
                                        "unable to setup timer");

                } else {
                        conn->ping_started = 0;
                        conn->ping_timer = NULL;
                        disconnect = 1;
                }
        }
        pthread_mutex_unlock (&conn->lock);

        if (disconnect) {
                gf_log (trans->name, GF_LOG_ERROR,
                        "Server %s has not responded in the last %d "
                        "seconds, disconnecting.",
                        conn->trans->peerinfo.identifier,
                        conf->opt.ping_timeout);

                rpc_transport_disconnect (conn->trans);
        }

out:
        return;
}

void
client_start_ping (void *data)
{
        xlator_t                *this        = NULL;
        clnt_conf_t             *conf        = NULL;
        rpc_clnt_connection_t   *conn        = NULL;
        int32_t                  ret         = -1;
        struct timeval           timeout     = {0, };
        call_frame_t            *frame       = NULL;
        int                      frame_count = 0;

        this = data;
        if (!this || !this->private)
                goto fail;

        conf  = this->private;
        if (!conf->rpc)
                goto fail;

        conn = &conf->rpc->conn;

        if (conf->opt.ping_timeout == 0)
                return;

        pthread_mutex_lock (&conn->lock);
        {
                if (conn->ping_timer)
                        gf_timer_call_cancel (this->ctx, conn->ping_timer);

                conn->ping_timer = NULL;
                conn->ping_started = 0;

                if (conn->saved_frames)
                        /* treat the case where conn->saved_frames is NULL
                           as no pending frames */
                        frame_count = conn->saved_frames->count;

                if ((frame_count == 0) || !conn->connected) {
                        /* using goto looked ugly here,
                         * hence getting out this way */
                        /* unlock */
                        pthread_mutex_unlock (&conn->lock);
                        return;
                }

                if (frame_count < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "saved_frames->count is %"PRId64,
                                conn->saved_frames->count);
                        conn->saved_frames->count = 0;
                }

                timeout.tv_sec = conf->opt.ping_timeout;
                timeout.tv_usec = 0;

                conn->ping_timer =
                        gf_timer_call_after (this->ctx, timeout,
                                             rpc_client_ping_timer_expired,
                                             (void *) this);

                if (conn->ping_timer == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "unable to setup timer");
                } else {
                        conn->ping_started = 1;
                }
        }
        pthread_mutex_unlock (&conn->lock);

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto fail;

        ret = client_submit_request (this, NULL, frame, conf->handshake,
                                     GF_HNDSK_PING, client_ping_cbk, NULL, NULL,
                                     NULL, 0, NULL, 0, NULL);
        if (ret)
                goto fail;

        return;
fail:

        if (frame) {
                STACK_DESTROY (frame->root);
        }

        return;
}


int
client_ping_cbk (struct rpc_req *req, struct iovec *iov, int count,
                 void *myframe)
{
        xlator_t              *this    = NULL;
        rpc_clnt_connection_t *conn    = NULL;
        struct timeval         timeout = {0, };
        call_frame_t          *frame   = NULL;
        clnt_conf_t           *conf    = NULL;

        if (!myframe)
                goto out;

        frame = myframe;
        this = frame->this;
        if (!this || !this->private)
                goto out;

        conf = this->private;
        conn = &conf->rpc->conn;

        if (req->rpc_status == -1) {
		 if (conn->ping_timer != NULL) {
			 gf_log (this->name, GF_LOG_DEBUG, "socket or ib"
				 " related error");
			 gf_timer_call_cancel (this->ctx, conn->ping_timer);
			 conn->ping_timer = NULL;
		 } else {
			 /* timer expired and transport bailed out */
			 gf_log (this->name, GF_LOG_DEBUG, "timer must have "
			 "expired");
		 }
                goto out;
        }

        pthread_mutex_lock (&conn->lock);
        {
                timeout.tv_sec  = conf->opt.ping_timeout;
                timeout.tv_usec = 0;

                gf_timer_call_cancel (this->ctx,
                                      conn->ping_timer);

                conn->ping_timer =
                        gf_timer_call_after (this->ctx, timeout,
                                             client_start_ping, (void *)this);

                if (conn->ping_timer == NULL)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "gf_timer_call_after() returned NULL");
        }
        pthread_mutex_unlock (&conn->lock);
out:
        if (frame)
                STACK_DESTROY (frame->root);
        return 0;
}


int
client3_getspec_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        gf_getspec_rsp           rsp   = {0,};
        call_frame_t            *frame = NULL;
        int                      ret   = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_getspec_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 == rsp.op_ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "failed to get the 'volume file' from server");
                goto out;
        }

out:
        STACK_UNWIND_STRICT (getspec, frame, rsp.op_ret, rsp.op_errno, rsp.spec);

        /* Don't use 'GF_FREE', this is allocated by libc */
        if (rsp.spec)
                free (rsp.spec);

        return 0;
}

int32_t client3_getspec (call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_conf_t    *conf     = NULL;
        clnt_args_t    *args     = NULL;
        gf_getspec_req  req      = {0,};
        int             op_errno = ESTALE;
        int             ret      = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;
        req.flags = args->flags;
        req.key   = (char *)args->name;

        ret = client_submit_request (this, &req, frame, conf->handshake,
                                     GF_HNDSK_GETSPEC, client3_getspec_cbk,
                                     NULL, xdr_from_getspec_req, NULL, 0,
                                     NULL, 0, NULL);

        if (ret)
                goto unwind;

        return 0;
unwind:
        STACK_UNWIND_STRICT (getspec, frame, -1, op_errno, NULL);
        return 0;

}

int
client_notify_parents_child_up (xlator_t *this)
{
        xlator_list_t          *parent = NULL;

        /* As fuse is not 'parent' of any translator now, triggering its
           CHILD_UP event is hacky in case client has only client protocol */
        if (!this->parents && this->ctx && this->ctx->master) {
                /* send notify to 'ctx->master' if it exists */
                xlator_notify (this->ctx->master, GF_EVENT_CHILD_UP,
                               this->graph);
        } else {

                parent = this->parents;
                while (parent) {
                        xlator_notify (parent->xlator, GF_EVENT_CHILD_UP,
                                       this);
                        parent = parent->next;
                }
        }

        return 0;
}

int
client3_1_reopen_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void           *myframe)
{
        int32_t        ret                   = -1;
        gfs3_open_rsp  rsp                   = {0,};
        int            attempt_lock_recovery = _gf_false;
        uint64_t       fd_count              = 0;
        clnt_local_t  *local                 = NULL;
        clnt_conf_t   *conf                  = NULL;
        clnt_fd_ctx_t *fdctx                 = NULL;
        call_frame_t  *frame                 = NULL;

        frame = myframe;
        local = frame->local;
        conf  = frame->this->private;

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

        if (rsp.op_ret < 0) {
                gf_log (frame->this->name, GF_LOG_NORMAL,
                        "reopen on %s failed (%s)",
                        local->loc.path, strerror (rsp.op_errno));
        } else {
                gf_log (frame->this->name, GF_LOG_NORMAL,
                        "reopen on %s succeeded (remote-fd = %"PRId64")",
                        local->loc.path, rsp.fd);
        }

        if (rsp.op_ret == -1) {
                ret = -1;
                goto out;
        }

        fdctx = local->fdctx;

        if (!fdctx) {
                ret = -1;
                goto out;
        }

        pthread_mutex_lock (&conf->lock);
        {
                fdctx->remote_fd = rsp.fd;
                if (!fdctx->released) {
                        list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
                        if (!list_empty (&fdctx->lock_list))
                                attempt_lock_recovery = _gf_true;
                        fdctx = NULL;
                }
        }
        pthread_mutex_unlock (&conf->lock);

        attempt_lock_recovery = _gf_false; /* temporarily */

        if (attempt_lock_recovery) {
                ret = client_attempt_lock_recovery (frame->this, local->fdctx);
                if (ret < 0) {
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "No locks on fd to recover");
                } else {
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "Need to attempt lock recovery on %lld open fds",
                                (unsigned long long) fd_count);
                }
        } else {
                fd_count = decrement_reopen_fd_count (frame->this, conf);
        }


out:
        if (fdctx)
                client_fdctx_destroy (frame->this, fdctx);

        if ((ret < 0) && frame && frame->this && conf)
                decrement_reopen_fd_count (frame->this, conf);

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

        if (rsp.op_ret < 0) {
                gf_log (frame->this->name, GF_LOG_NORMAL,
                        "reopendir on %s failed (%s)",
                        local->loc.path, strerror (rsp.op_errno));
        } else {
                gf_log (frame->this->name, GF_LOG_NORMAL,
                        "reopendir on %s succeeded (fd = %"PRId64")",
                        local->loc.path, rsp.fd);
        }

	if (-1 != rsp.op_ret) {
                fdctx = local->fdctx;
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
        }

        decrement_reopen_fd_count (frame->this, conf);
        ret = 0;

out:
        if (fdctx)
                client_fdctx_destroy (frame->this, fdctx);

        if ((ret < 0) && frame && frame->this && conf)
                decrement_reopen_fd_count (frame->this, conf);

        if (frame) {
                frame->local = NULL;
                STACK_DESTROY (frame->root);
        }

        client_local_wipe (local);

        return 0;
}

int
protocol_client_reopendir (xlator_t *this, clnt_fd_ctx_t *fdctx)
{
        int               ret   = -1;
        gfs3_opendir_req  req   = {{0,},};
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

        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                ret = -1;
                goto out;
        }

        local->fdctx    = fdctx;
        local->loc.path = path;
        path            = NULL;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        memcpy (req.gfid,  inode->gfid, 16);
        req.path  = (char *)local->loc.path;

        gf_log (frame->this->name, GF_LOG_DEBUG,
                "attempting reopen on %s", local->loc.path);

        frame->local = local; local = NULL;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_OPENDIR,
                                     client3_1_reopendir_cbk, NULL,
                                     xdr_from_opendir_req, NULL, 0, NULL, 0,
                                     NULL);
        if (ret)
                goto out;

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
        if ((ret < 0) && this && conf) {
                decrement_reopen_fd_count (this, conf);
        }

        return 0;

}

int
protocol_client_reopen (xlator_t *this, clnt_fd_ctx_t *fdctx)
{
        int            ret   = -1;
        gfs3_open_req  req   = {{0,},};
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
                ret = -1;
                goto out;
        }

        local = GF_CALLOC (1, sizeof (*local), gf_client_mt_clnt_local_t);
        if (!local) {
                ret = -1;
                goto out;
        }

        local->fdctx    = fdctx;
        local->loc.path = path;
        path            = NULL;
        frame->local    = local;

        memcpy (req.gfid,  inode->gfid, 16);
        req.flags    = gf_flags_from_flags (fdctx->flags);
        req.wbflags  = fdctx->wbflags;
        req.path     = (char *)local->loc.path;

        gf_log (frame->this->name, GF_LOG_DEBUG,
                "attempting reopen on %s", local->loc.path);

        local = NULL;
        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_OPEN, client3_1_reopen_cbk, NULL,
                                     xdr_from_open_req, NULL, 0, NULL, 0, NULL);
        if (ret)
                goto out;

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

        if ((ret < 0) && this && conf) {
                decrement_reopen_fd_count (this, conf);
        }

        return 0;

}


int
client_post_handshake (call_frame_t *frame, xlator_t *this)
{
        clnt_conf_t            *conf = NULL;
        clnt_fd_ctx_t          *tmp = NULL;
        clnt_fd_ctx_t          *fdctx = NULL;
        struct list_head        reopen_head;

        int count = 0;

        if (!this || !this->private)
                goto out;

        conf = this->private;
        INIT_LIST_HEAD (&reopen_head);

        pthread_mutex_lock (&conf->lock);
        {
                list_for_each_entry_safe (fdctx, tmp, &conf->saved_fds,
                                          sfd_pos) {
                        if (fdctx->remote_fd != -1)
                                continue;

                        list_del_init (&fdctx->sfd_pos);
                        list_add_tail (&fdctx->sfd_pos, &reopen_head);
                        count++;
                }
        }
        pthread_mutex_unlock (&conf->lock);

        /* Delay notifying CHILD_UP to parents
           until all locks are recovered */
        if (count > 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "%d fds open - Delaying child_up until they are re-opened",
                        count);
                client_save_number_fds (conf, count);

                list_for_each_entry_safe (fdctx, tmp, &reopen_head, sfd_pos) {
                        list_del_init (&fdctx->sfd_pos);

                        if (fdctx->is_dir)
                                protocol_client_reopendir (this, fdctx);
                        else
                                protocol_client_reopen (this, fdctx);
                }
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "No open fds - notifying all parents child up");
                client_notify_parents_child_up (this);

        }
out:
        return 0;
}

int
client_setvolume_cbk (struct rpc_req *req, struct iovec *iov, int count, void *myframe)
{
        call_frame_t         *frame         = NULL;
        clnt_conf_t          *conf          = NULL;
        xlator_t             *this          = NULL;
        dict_t               *reply         = NULL;
        xlator_list_t        *parent        = NULL;
        char                 *process_uuid  = NULL;
        char                 *remote_error  = NULL;
        char                 *remote_subvol = NULL;
        gf_setvolume_rsp      rsp           = {0,};
        int                   ret           = 0;
        int32_t               op_ret        = 0;
        int32_t               op_errno        = 0;

        frame = myframe;
        this  = frame->this;
        conf  = this->private;

        if (-1 == req->rpc_status) {
                op_ret = -1;
                goto out;
        }

        ret = xdr_to_setvolume_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                op_ret = -1;
                goto out;
        }
        op_ret   = rsp.op_ret;
        op_errno = gf_error_to_errno (rsp.op_errno);
        if (-1 == rsp.op_ret) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        "failed to set the volume");
        }

        reply = dict_new ();
        if (!reply)
                goto out;

        if (rsp.dict.dict_len) {
                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len, &reply);
                if (ret < 0) {
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "failed to unserialize buffer to dict");
                        goto out;
                }
        }

        ret = dict_get_str (reply, "ERROR", &remote_error);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get ERROR string from reply dict");
        }

        ret = dict_get_str (reply, "process-uuid", &process_uuid);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get 'process-uuid' from reply dict");
        }

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "SETVOLUME on remote-host failed: %s",
                        remote_error ? remote_error : strerror (op_errno));
                errno = op_errno;
                if (op_errno == ESTALE) {
                        parent = this->parents;
                        while (parent) {
                                xlator_notify (parent->xlator,
                                               GF_EVENT_VOLFILE_MODIFIED,
                                               this);
                                parent = parent->next;
                        }
                }
                goto out;
        }
        ret = dict_get_str (this->options, "remote-subvolume",
                            &remote_subvol);
        if (ret || !remote_subvol)
                goto out;

        /* TODO: currently setpeer path is broken */
        /*
        if (process_uuid && req->conn &&
            !strcmp (this->ctx->process_uuid, process_uuid)) {
                rpc_transport_t      *peer_trans    = NULL;
                uint64_t              peertrans_int = 0;

                ret = dict_get_uint64 (reply, "transport-ptr",
                                       &peertrans_int);
                if (ret)
                        goto out;

                gf_log (this->name, GF_LOG_WARNING,
                        "attaching to the local volume '%s'",
                        remote_subvol);

                peer_trans = (void *) (long) (peertrans_int);

                rpc_transport_setpeer (req->conn->trans, peer_trans);
        }
        */

        gf_log (this->name, GF_LOG_NORMAL,
                "Connected to %s, attached to remote volume '%s'.",
                conf->rpc->conn.trans->peerinfo.identifier,
                remote_subvol);

        rpc_clnt_set_connected (&conf->rpc->conn);

        op_ret = 0;
        conf->connecting = 0;
        conf->connected = 1;

        /* TODO: more to test */
        client_post_handshake (frame, frame->this);

out:

        if (-1 == op_ret) {
                /* Let the connection/re-connection happen in
                 * background, for now, don't hang here,
                 * tell the parents that i am all ok..
                 */
                parent = this->parents;
                while (parent) {
                        xlator_notify (parent->xlator,
                                       GF_EVENT_CHILD_CONNECTING, this);
                        parent = parent->next;
                }

                conf->connecting= 1;
        }

        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);

        STACK_DESTROY (frame->root);

        if (reply)
                dict_unref (reply);

        return 0;
}

int
client_setvolume (xlator_t *this, struct rpc_clnt *rpc)
{
        int               ret             = 0;
        gf_setvolume_req  req             = {{0,},};
        call_frame_t     *fr              = NULL;
        char             *process_uuid_xl = NULL;
        clnt_conf_t      *conf            = NULL;
        dict_t           *options         = NULL;

        struct rpc_clnt_config config = {0, };


        options = this->options;
        conf    = this->private;

        if (conf->fops) {
                ret = dict_set_int32 (options, "fops-version",
                                      conf->fops->prognum);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to set version-fops(%d) in handshake msg",
                                conf->fops->prognum);
                        goto fail;
                }
        }

        if (conf->mgmt) {
                ret = dict_set_int32 (options, "mgmt-version", conf->mgmt->prognum);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to set version-mgmt(%d) in handshake msg",
                                conf->mgmt->prognum);
                        goto fail;
                }
        }

        ret = gf_asprintf (&process_uuid_xl, "%s-%s", this->ctx->process_uuid,
                           this->name);
        if (-1 == ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "asprintf failed while setting process_uuid");
                goto fail;
        }
        ret = dict_set_dynstr (options, "process-uuid", process_uuid_xl);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to set process-uuid(%s) in handshake msg",
                        process_uuid_xl);
                goto fail;
        }

        if (this->ctx->cmd_args.volfile_server) {
                if (this->ctx->cmd_args.volfile_id) {
                        ret = dict_set_str (options, "volfile-key",
                                            this->ctx->cmd_args.volfile_id);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "failed to set 'volfile-key'");
                }
                ret = dict_set_uint32 (options, "volfile-checksum",
                                       this->graph->volfile_checksum);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to set 'volfile-checksum'");
        }

        req.dict.dict_len = dict_serialized_length (options);
        if (req.dict.dict_len < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get serialized length of dict");
                ret = -1;
                goto fail;
        }
        req.dict.dict_val = GF_CALLOC (1, req.dict.dict_len,
                                       gf_client_mt_clnt_req_buf_t);
        ret = dict_serialize (options, req.dict.dict_val);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to serialize dictionary");
                goto fail;
        }

        fr  = create_frame (this, this->ctx->pool);
        if (!fr)
                goto fail;

        ret = client_submit_request (this, &req, fr, conf->handshake,
                                     GF_HNDSK_SETVOLUME, client_setvolume_cbk,
                                     NULL, xdr_from_setvolume_req, NULL, 0,
                                     NULL, 0, NULL);

fail:
        if (ret) {
                config.remote_port = -1;
                rpc_clnt_reconfig (conf->rpc, &config);
        }

        if (req.dict.dict_val)
                GF_FREE (req.dict.dict_val);

        return ret;
}

int
select_server_supported_programs (xlator_t *this, gf_prog_detail *prog)
{
        gf_prog_detail *trav     = NULL;
        clnt_conf_t    *conf     = NULL;
        int             ret      = -1;

        if (!this || !prog)
                goto out;

        conf = this->private;
        trav = prog;

        while (trav) {
                /* Select 'programs' */
                if ((clnt3_1_fop_prog.prognum == trav->prognum) &&
                    (clnt3_1_fop_prog.progver == trav->progver)) {
                        conf->fops = &clnt3_1_fop_prog;
                        gf_log (this->name, GF_LOG_INFO,
                                "Using Program %s, Num (%"PRId64"), "
                                "Version (%"PRId64")",
                                trav->progname, trav->prognum, trav->progver);
                        ret = 0;
                }
                if (ret) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "%s (%"PRId64") not supported", trav->progname,
                                trav->progver);
                }
                trav = trav->next;
        }

out:
        return ret;
}


int
server_has_portmap (xlator_t *this, gf_prog_detail *prog)
{
        gf_prog_detail *trav     = NULL;
        int             ret      = -1;

        if (!this || !prog)
                goto out;

        trav = prog;

        while (trav) {
                if ((trav->prognum == GLUSTER_PMAP_PROGRAM) &&
                    (trav->progver == GLUSTER_PMAP_VERSION)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "detected portmapper on server");
                        ret = 0;
                        break;
                }
                trav = trav->next;
        }

out:
        return ret;
}


int
client_query_portmap_cbk (struct rpc_req *req, struct iovec *iov, int count, void *myframe)
{
        struct pmap_port_by_brick_rsp     rsp   = {0,};
        call_frame_t                     *frame = NULL;
        clnt_conf_t                      *conf  = NULL;
        int                               ret   = -1;
        struct rpc_clnt_config            config = {0, };
        xlator_t                         *this   = NULL;

        frame = myframe;
        if (!frame || !frame->this || !frame->this->private)
                goto out;

        this  = frame->this;
        conf  = frame->this->private;

        if (-1 == req->rpc_status) {
                gf_log ("", 1, "some error, retry again later");
                goto out;
        }

        ret = xdr_to_pmap_port_by_brick_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        if (-1 == rsp.op_ret) {
                ret = -1;
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "failed to get the port number for remote subvolume");
                goto out;
        }

        config.remote_port = rsp.port;
        rpc_clnt_reconfig (conf->rpc, &config);
        conf->skip_notify = 1;

out:
        if (frame)
                STACK_DESTROY (frame->root);

        if (conf) {

                rpc_transport_disconnect (conf->rpc->conn.trans);

                rpc_clnt_reconnect (conf->rpc->conn.trans);
        }

        return ret;
}


int
client_query_portmap (xlator_t *this, struct rpc_clnt *rpc)
{
        int                      ret             = -1;
        pmap_port_by_brick_req   req             = {0,};
        call_frame_t            *fr              = NULL;
        clnt_conf_t             *conf            = NULL;
        dict_t                  *options         = NULL;
        char                    *remote_subvol   = NULL;

        options = this->options;
        conf    = this->private;

        ret = dict_get_str (options, "remote-subvolume", &remote_subvol);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "remote-subvolume not set in volfile");
                goto fail;
        }

        req.brick = remote_subvol;

        fr  = create_frame (this, this->ctx->pool);
        if (!fr) {
                ret = -1;
                goto fail;
        }

        ret = client_submit_request (this, &req, fr, &clnt_pmap_prog,
                                     GF_PMAP_PORTBYBRICK,
                                     client_query_portmap_cbk,
                                     NULL, xdr_from_pmap_port_by_brick_req,
                                     NULL, 0, NULL, 0, NULL);

fail:
        return ret;
}


int
client_dump_version_cbk (struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
        gf_dump_rsp     rsp   = {0,};
        gf_prog_detail *trav  = NULL;
        gf_prog_detail *next  = NULL;
        call_frame_t   *frame = NULL;
        clnt_conf_t    *conf  = NULL;
        int             ret   = 0;

        frame = myframe;
        conf  = frame->this->private;

        if (-1 == req->rpc_status) {
                gf_log ("", 1, "some error, retry again later");
                goto out;
        }

        ret = xdr_to_dump_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }
        if (-1 == rsp.op_ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "failed to get the 'versions' from server");
                goto out;
        }

        if (server_has_portmap (frame->this, rsp.prog) == 0) {
                ret = client_query_portmap (frame->this, conf->rpc);
                goto out;
        }

        /* Check for the proper version string */
        /* Reply in "Name:Program-Number:Program-Version,..." format */
        ret = select_server_supported_programs (frame->this, rsp.prog);
        if (ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Server versions are not present in this "
                        "release");
                goto out;
        }

        client_setvolume (frame->this, conf->rpc);

out:
        /* don't use GF_FREE, buffer was allocated by libc */
        if (rsp.prog) {
                trav = rsp.prog;
                while (trav) {
                        next = trav->next;
                        free (trav->progname);
                        free (trav);
                        trav = next;
                }
        }

        STACK_DESTROY (frame->root);

        if (ret != 0)
                rpc_transport_disconnect (conf->rpc->conn.trans);

        return ret;
}

int
client_handshake (xlator_t *this, struct rpc_clnt *rpc)
{
        call_frame_t *frame = NULL;
        clnt_conf_t  *conf  = NULL;
        gf_dump_req   req   = {0,};
        int           ret   = 0;

        conf = this->private;
        if (!conf->handshake)
                goto out;

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        req.gfs_id = 0xbabe;
        ret = client_submit_request (this, &req, frame, conf->dump,
                                     GF_DUMP_DUMP, client_dump_version_cbk,
                                     NULL, xdr_from_dump_req, NULL, 0, NULL, 0,
                                     NULL);

out:
        return ret;
}

char *clnt_handshake_procs[GF_HNDSK_MAXVALUE] = {
        [GF_HNDSK_NULL]         = "NULL",
        [GF_HNDSK_SETVOLUME]    = "SETVOLUME",
        [GF_HNDSK_GETSPEC]      = "GETSPEC",
        [GF_HNDSK_PING]         = "PING",
};

rpc_clnt_prog_t clnt_handshake_prog = {
        .progname  = "GlusterFS Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .procnames = clnt_handshake_procs,
};

char *clnt_dump_proc[GF_DUMP_MAXVALUE] = {
        [GF_DUMP_NULL] = "NULL",
        [GF_DUMP_DUMP] = "DUMP",
};

rpc_clnt_prog_t clnt_dump_prog = {
        .progname  = "GF-DUMP",
        .prognum   = GLUSTER_DUMP_PROGRAM,
        .progver   = GLUSTER_DUMP_VERSION,
        .procnames = clnt_dump_proc,
};

char *clnt_pmap_procs[GF_PMAP_MAXVALUE] = {
        [GF_PMAP_PORTBYBRICK] = "PORTBYBRICK",
};

rpc_clnt_prog_t clnt_pmap_prog = {
        .progname   = "PORTMAP",
        .prognum    = GLUSTER_PMAP_PROGRAM,
        .progver    = GLUSTER_PMAP_VERSION,
        .procnames  = clnt_pmap_procs,
};
