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
#include <inttypes.h>


#include "glusterfs.h"
#include "client-protocol.h"
#include "compat.h"
#include "dict.h"
#include "protocol.h"
#include "transport.h"
#include "xlator.h"
#include "logging.h"
#include "timer.h"
#include "defaults.h"
#include "compat.h"
#include "compat-errno.h"
#include "statedump.h"

#include <sys/resource.h>
#include <inttypes.h>

/* for default_*_cbk functions */
#include "defaults.c"
#include "saved-frames.h"
#include "common-utils.h"

int protocol_client_cleanup (transport_t *trans);
int protocol_client_interpret (xlator_t *this, transport_t *trans,
                               char *hdr_p, size_t hdrlen,
                               struct iobuf *iobuf);
int
protocol_client_xfer (call_frame_t *frame, xlator_t *this, transport_t *trans,
                      int type, int op,
                      gf_hdr_common_t *hdr, size_t hdrlen,
                      struct iovec *vector, int count,
                      struct iobref *iobref);

int
protocol_client_post_handshake (call_frame_t *frame, xlator_t *this);

static gf_op_t gf_fops[];
static gf_op_t gf_mops[];
static gf_op_t gf_cbks[];


transport_t *
client_channel (xlator_t *this, int id)
{
        transport_t              *trans = NULL;
        client_conf_t            *conf = NULL;
        int                       i = 0;
        struct client_connection *conn = NULL;

        conf = this->private;

        trans = conf->transport[id];
        conn = trans->xl_private;

        if (conn->connected == 1)
                goto ret;

        for (i = 0; i < CHANNEL_MAX; i++) {
                trans = conf->transport[i];
                conn = trans->xl_private;
                if (conn->connected == 1)
                        break;
        }

ret:
        return trans;
}


client_fd_ctx_t *
this_fd_del_ctx (fd_t *file, xlator_t *this)
{
        int         dict_ret = -1;
        uint64_t    ctxaddr  = 0;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, file, out);

        dict_ret = fd_ctx_del (file, this, &ctxaddr);

        if (dict_ret < 0) {
                ctxaddr = 0;
        }

out:
        return (client_fd_ctx_t *)(unsigned long)ctxaddr;
}


client_fd_ctx_t *
this_fd_get_ctx (fd_t *file, xlator_t *this)
{
        int         dict_ret = -1;
        uint64_t    ctxaddr = 0;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, file, out);

        dict_ret = fd_ctx_get (file, this, &ctxaddr);

        if (dict_ret < 0) {
                ctxaddr = 0;
        }

out:
        return (client_fd_ctx_t *)(unsigned long)ctxaddr;
}


static void
this_fd_set_ctx (fd_t *file, xlator_t *this, loc_t *loc, client_fd_ctx_t *ctx)
{
        uint64_t oldaddr = 0;
        int32_t  ret = -1;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, file, out);

        ret = fd_ctx_get (file, this, &oldaddr);
        if (ret >= 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s (%"PRId64"): trying duplicate remote fd set. ",
                        loc->path, loc->inode->ino);
        }

        ret = fd_ctx_set (file, this, (uint64_t)(unsigned long)ctx);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s (%"PRId64"): failed to set remote fd",
                        loc->path, loc->inode->ino);
        }
out:
        return;
}


static int
client_local_wipe (client_local_t *local)
{
        if (local) {
                loc_wipe (&local->loc);

                if (local->fd)
                        fd_unref (local->fd);

                free (local);
        }

        return 0;
}

/*
 * lookup_frame - lookup call frame corresponding to a given callid
 * @trans: transport object
 * @callid: call id of the frame
 *
 * not for external reference
 */

static call_frame_t *
lookup_frame (transport_t *trans, int32_t op, int8_t type, int64_t callid)
{
        client_connection_t *conn = NULL;
        call_frame_t        *frame = NULL;

        conn = trans->xl_private;

        pthread_mutex_lock (&conn->lock);
        {
                frame = saved_frames_get (conn->saved_frames,
                                          op, type, callid);
        }
        pthread_mutex_unlock (&conn->lock);

        return frame;
}


static void
call_bail (void *data)
{
        client_connection_t  *conn = NULL;
        struct timeval        current;
        transport_t          *trans = NULL;
        struct list_head      list;
        struct saved_frame   *saved_frame = NULL;
        struct saved_frame   *trav = NULL;
        struct saved_frame   *tmp = NULL;
        call_frame_t         *frame = NULL;
        gf_hdr_common_t       hdr = {0, };
        char                **gf_op_list = NULL;
        gf_op_t              *gf_ops = NULL;
        struct tm             frame_sent_tm;
        char                  frame_sent[32] = {0,};
        struct timeval        timeout = {0,};
        gf_timer_cbk_t        timer_cbk = NULL;

        GF_VALIDATE_OR_GOTO ("client", data, out);
        trans = data;

        conn = trans->xl_private;

        gettimeofday (&current, NULL);
        INIT_LIST_HEAD (&list);

        pthread_mutex_lock (&conn->lock);
        {
                /* Chaining to get call-always functionality from
                   call-once timer */
                if (conn->timer) {
                        timer_cbk = conn->timer->cbk;

                        timeout.tv_sec = 10;
                        timeout.tv_usec = 0;

                        gf_timer_call_cancel (trans->xl->ctx, conn->timer);
                        conn->timer = gf_timer_call_after (trans->xl->ctx,
                                                           timeout,
                                                           timer_cbk,
                                                           trans);
                        if (conn->timer == NULL) {
                                gf_log (trans->xl->name, GF_LOG_DEBUG,
                                        "Cannot create bailout timer");
                        }
                }

                do {
                        saved_frame =
                                saved_frames_get_timedout (conn->saved_frames,
                                                           GF_OP_TYPE_MOP_REQUEST,
                                                           conn->frame_timeout,
                                                           &current);
                        if (saved_frame)
                                list_add (&saved_frame->list, &list);

                } while (saved_frame);

                do {
                        saved_frame =
                                saved_frames_get_timedout (conn->saved_frames,
                                                           GF_OP_TYPE_FOP_REQUEST,
                                                           conn->frame_timeout,
                                                           &current);
                        if (saved_frame)
                                list_add (&saved_frame->list, &list);
                } while (saved_frame);

                do {
                        saved_frame =
                                saved_frames_get_timedout (conn->saved_frames,
                                                           GF_OP_TYPE_CBK_REQUEST,
                                                           conn->frame_timeout,
                                                           &current);
                        if (saved_frame)
                                list_add (&saved_frame->list, &list);
                } while (saved_frame);
        }
        pthread_mutex_unlock (&conn->lock);

        hdr.rsp.op_ret   = hton32 (-1);
        hdr.rsp.op_errno = hton32 (ENOTCONN);

        list_for_each_entry_safe (trav, tmp, &list, list) {
                switch (trav->type)
                {
                case GF_OP_TYPE_FOP_REQUEST:
                        gf_ops = gf_fops;
                        gf_op_list = gf_fop_list;
                        break;
                case GF_OP_TYPE_MOP_REQUEST:
                        gf_ops = gf_mops;
                        gf_op_list = gf_mop_list;
                        break;
                case GF_OP_TYPE_CBK_REQUEST:
                        gf_ops = gf_cbks;
                        gf_op_list = gf_cbk_list;
                        break;
                }

                localtime_r (&trav->saved_at.tv_sec, &frame_sent_tm);
                strftime (frame_sent, 32, "%Y-%m-%d %H:%M:%S", &frame_sent_tm);

                gf_log (trans->xl->name, GF_LOG_ERROR,
                        "bailing out frame %s(%d) "
                        "frame sent = %s. frame-timeout = %d",
                        gf_op_list[trav->op], trav->op,
                        frame_sent, conn->frame_timeout);

                hdr.type = hton32 (trav->type);
                hdr.op   = hton32 (trav->op);

                frame = trav->frame;

                gf_ops[trav->op] (frame, &hdr, sizeof (hdr), NULL);

                list_del_init (&trav->list);
                FREE (trav);
        }
out:
        return;
}


void
save_frame (transport_t *trans, call_frame_t *frame,
            int32_t op, int8_t type, uint64_t callid)
{
        client_connection_t *conn = NULL;
        struct timeval       timeout = {0, };


        conn = trans->xl_private;

        saved_frames_put (conn->saved_frames, frame, op, type, callid);

        if (conn->timer == NULL) {
                timeout.tv_sec  = 10;
                timeout.tv_usec = 0;
                conn->timer = gf_timer_call_after (trans->xl->ctx, timeout,
                                                   call_bail, (void *) trans);
        }
}



void
client_ping_timer_expired (void *data)
{
        xlator_t            *this = NULL;
        transport_t         *trans = NULL;
        client_conf_t       *conf = NULL;
        client_connection_t *conn = NULL;
        int                  disconnect = 0;
        int                  transport_activity = 0;
        struct timeval       timeout = {0, };
        struct timeval       current = {0, };

        trans = data;
        this  = trans->xl;
        conf  = this->private;
        conn  = trans->xl_private;

        pthread_mutex_lock (&conn->lock);
        {
                if (conn->ping_timer)
                        gf_timer_call_cancel (trans->xl->ctx,
                                              conn->ping_timer);
                gettimeofday (&current, NULL);

                pthread_mutex_lock (&conf->mutex);
                {
                        if (((current.tv_sec - conf->last_received.tv_sec) <
                             conn->ping_timeout)
                            || ((current.tv_sec - conf->last_sent.tv_sec) <
                                conn->ping_timeout)) {
                                transport_activity = 1;
                        }
                }
                pthread_mutex_unlock (&conf->mutex);

                if (transport_activity) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "ping timer expired but transport activity "
                                "detected - not bailing transport");
                        conn->transport_activity = 0;
                        timeout.tv_sec = conn->ping_timeout;
                        timeout.tv_usec = 0;

                        conn->ping_timer =
                                gf_timer_call_after (trans->xl->ctx, timeout,
                                                     client_ping_timer_expired,
                                                     (void *) trans);
                        if (conn->ping_timer == NULL)
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "unable to setup timer");

                } else {
                        conn->ping_started = 0;
                        conn->ping_timer = NULL;
                        disconnect = 1;
                }
        }
        pthread_mutex_unlock (&conn->lock);
        if (disconnect) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Server %s has not responded in the last %d "
                        "seconds, disconnecting.",
                        conf->transport[0]->peerinfo.identifier,
                        conn->ping_timeout);

                transport_disconnect (conf->transport[0]);
                transport_disconnect (conf->transport[1]);
        }
}


void
client_start_ping (void *data)
{
        xlator_t            *this = NULL;
        transport_t         *trans = NULL;
        client_conf_t       *conf = NULL;
        client_connection_t *conn = NULL;
        int32_t              ret = -1;
        gf_hdr_common_t     *hdr = NULL;
        struct timeval       timeout = {0, };
        call_frame_t        *dummy_frame = NULL;
        size_t               hdrlen = -1;
        gf_mop_ping_req_t   *req = NULL;


        trans = data;
        this  = trans->xl;
        conf  = this->private;
        conn  = trans->xl_private;

        pthread_mutex_lock (&conn->lock);
        {
                if ((conn->saved_frames->count == 0) ||
                    !conn->connected) {
                        /* using goto looked ugly here,
                         * hence getting out this way */
                        if (conn->ping_timer)
                                gf_timer_call_cancel (trans->xl->ctx,
                                                      conn->ping_timer);
                        conn->ping_timer = NULL;
                        conn->ping_started = 0;
                        /* unlock */
                        pthread_mutex_unlock (&conn->lock);
                        return;
                }

                if (conn->saved_frames->count < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "saved_frames->count is %"PRId64,
                                conn->saved_frames->count);
                        conn->saved_frames->count = 0;
                }
                timeout.tv_sec = conn->ping_timeout;
                timeout.tv_usec = 0;

                if (conn->ping_timer)
                        gf_timer_call_cancel (trans->xl->ctx,
                                              conn->ping_timer);

                conn->ping_timer =
                        gf_timer_call_after (trans->xl->ctx, timeout,
                                             client_ping_timer_expired,
                                             (void *) trans);

                if (conn->ping_timer == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "unable to setup timer");
                } else {
                        conn->ping_started = 1;
                }
        }
        pthread_mutex_unlock (&conn->lock);

        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);

        dummy_frame = create_frame (this, this->ctx->pool);
        dummy_frame->local = trans;

        ret = protocol_client_xfer (dummy_frame, this, trans,
                                    GF_OP_TYPE_MOP_REQUEST, GF_MOP_PING,
                                    hdr, hdrlen, NULL, 0, NULL);
}


int
client_ping_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
        xlator_t            *this = NULL;
        transport_t         *trans = NULL;
        client_connection_t *conn = NULL;
        struct timeval       timeout = {0, };
        int                  op_ret = 0;

        trans  = frame->local; frame->local = NULL;
        this   = trans->xl;
        conn   = trans->xl_private;

        op_ret = ntoh32 (hdr->rsp.op_ret);

        if (op_ret == -1) {
                /* timer expired and transport bailed out */
                gf_log (this->name, GF_LOG_DEBUG, "timer must have expired");
                goto out;
        }

        pthread_mutex_lock (&conn->lock);
        {
                timeout.tv_sec  = conn->ping_timeout;
                timeout.tv_usec = 0;

                gf_timer_call_cancel (trans->xl->ctx,
                                      conn->ping_timer);

                conn->ping_timer =
                        gf_timer_call_after (trans->xl->ctx, timeout,
                                             client_start_ping, (void *)trans);
                if (conn->ping_timer == NULL)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "gf_timer_call_after() returned NULL");
        }
        pthread_mutex_unlock (&conn->lock);
out:
        STACK_DESTROY (frame->root);
        return 0;
}


int
protocol_client_xfer (call_frame_t *frame, xlator_t *this, transport_t *trans,
                      int type, int op,
                      gf_hdr_common_t *hdr, size_t hdrlen,
                      struct iovec *vector, int count,
                      struct iobref *iobref)
{
        client_conf_t        *conf = NULL;
        client_connection_t  *conn = NULL;
        uint64_t              callid = 0;
        int32_t               ret = -1;
        int                   start_ping = 0;
        gf_hdr_common_t       rsphdr = {0, };

        conf  = this->private;

        if (!trans) {
                /* default to bulk op since it is 'safer' */
                trans = conf->transport[CHANNEL_BULK];
        }
        conn  = trans->xl_private;

        pthread_mutex_lock (&conn->lock);
        {
                callid = ++conn->callid;

                hdr->callid = hton64 (callid);
                hdr->op     = hton32 (op);
                hdr->type   = hton32 (type);

                if (frame) {
                        hdr->req.uid = hton32 (frame->root->uid);
                        hdr->req.gid = hton32 (frame->root->gid);
                        hdr->req.pid = hton32 (frame->root->pid);
                }

                if (conn->connected == 0)
                        transport_connect (trans);

                ret = -1;

                if (conn->connected ||
                    ((type == GF_OP_TYPE_MOP_REQUEST) &&
                     (op == GF_MOP_SETVOLUME))) {
                        ret = transport_submit (trans, (char *)hdr, hdrlen,
                                                vector, count, iobref);
                }

                if ((ret >= 0) && frame) {
                        pthread_mutex_lock (&conf->mutex);
                        {
                                gettimeofday (&conf->last_sent, NULL);
                        }
                        pthread_mutex_unlock (&conf->mutex);
                        save_frame (trans, frame, op, type, callid);
                }

                if (!conn->ping_started && (ret >= 0)) {
                        start_ping = 1;
                }
        }
        pthread_mutex_unlock (&conn->lock);

        if (start_ping)
                client_start_ping ((void *) trans);

        if (frame && (ret < 0)) {
                rsphdr.op = op;
                rsphdr.rsp.op_ret   = hton32 (-1);
                rsphdr.rsp.op_errno = hton32 (ENOTCONN);

                if (type == GF_OP_TYPE_FOP_REQUEST) {
                        rsphdr.type = GF_OP_TYPE_FOP_REPLY;
                        gf_fops[op] (frame, &rsphdr, sizeof (rsphdr), NULL);
                } else if (type == GF_OP_TYPE_MOP_REQUEST) {
                        rsphdr.type = GF_OP_TYPE_MOP_REPLY;
                        gf_mops[op] (frame, &rsphdr, sizeof (rsphdr), NULL);
                } else {
                        rsphdr.type = GF_OP_TYPE_CBK_REPLY;
                        gf_cbks[op] (frame, &rsphdr, sizeof (rsphdr), NULL);
                }

                FREE (hdr);
        }

        return ret;
}



/**
 * client_create - create function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @path: complete path to file
 * @flags: create flags
 * @mode: create mode
 *
 * external reference through client_protocol_xlator->fops->create
 */

int
client_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
               mode_t mode, fd_t *fd)
{
        gf_hdr_common_t     *hdr = NULL;
        gf_fop_create_req_t *req = NULL;
        size_t               hdrlen = 0;
        size_t               pathlen = 0;
        size_t               baselen = 0;
        int32_t              ret = -1;
        ino_t                par = 0;
        uint64_t             gen = 0;
        client_local_t      *local = NULL;


        local = calloc (1, sizeof (*local));
        GF_VALIDATE_OR_GOTO (this->name, local, unwind);

        local->fd = fd_ref (fd);
        loc_copy (&local->loc, loc);

        frame->local = local;

        pathlen = STRLEN_0 (loc->path);
        baselen = STRLEN_0 (loc->name);

        ret = inode_ctx_get2 (loc->parent, this, &par, &gen);
        if (loc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "CREATE %"PRId64"/%s (%s): failed to get remote inode "
                        "number for parent inode",
                        loc->parent->ino, loc->name, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen + baselen);
        hdr    = gf_hdr_new (req, pathlen + baselen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->flags   = hton32 (gf_flags_from_flags (flags));
        req->mode    = hton32 (mode);
        req->par     = hton64 (par);
        req->gen     = hton64 (gen);
        strcpy (req->path, loc->path);
        strcpy (req->bname + pathlen, loc->name);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_CREATE,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, fd, NULL, NULL);
        return 0;

}

/**
 * client_open - open function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location of file
 * @flags: open flags
 * @mode: open modes
 *
 * external reference through client_protocol_xlator->fops->open
 */

int
client_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
             fd_t *fd, int32_t wbflags)
{
        int                 ret = -1;
        gf_hdr_common_t    *hdr = NULL;
        size_t              hdrlen = 0;
        gf_fop_open_req_t  *req = NULL;
        size_t              pathlen = 0;
        ino_t               ino = 0;
        uint64_t            gen = 0;
        client_local_t     *local = NULL;

        local = calloc (1, sizeof (*local));
        GF_VALIDATE_OR_GOTO (this->name, local, unwind);

        local->fd = fd_ref (fd);
        loc_copy (&local->loc, loc);

        frame->local = local;

        pathlen = STRLEN_0 (loc->path);

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "OPEN %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen);
        hdr    = gf_hdr_new (req, pathlen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino   = hton64 (ino);
        req->gen   = hton64 (gen);
        req->flags = hton32 (gf_flags_from_flags (flags));
        req->wbflags = hton32 (wbflags);
        strcpy (req->path, loc->path);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_OPEN,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, fd);
        return 0;

}


/**
 * client_stat - stat function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 *
 * external reference through client_protocol_xlator->fops->stat
 */

int
client_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        gf_hdr_common_t   *hdr = NULL;
        gf_fop_stat_req_t *req = NULL;
        size_t             hdrlen = -1;
        int32_t            ret = -1;
        size_t             pathlen = 0;
        ino_t              ino = 0;
        ino_t              gen = 0;

        pathlen = STRLEN_0 (loc->path);

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "STAT %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen);
        hdr    = gf_hdr_new (req, pathlen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino  = hton64 (ino);
        req->gen  = hton64 (gen);
        strcpy (req->path, loc->path);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_STAT,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;

}


/**
 * client_readlink - readlink function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @size:
 *
 * external reference through client_protocol_xlator->fops->readlink
 */
int
client_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size)
{
        gf_hdr_common_t       *hdr = NULL;
        gf_fop_readlink_req_t *req = NULL;
        size_t                 hdrlen = -1;
        int                    ret = -1;
        size_t                 pathlen = 0;
        ino_t                  ino = 0;
        uint64_t               gen = 0;

        pathlen = STRLEN_0 (loc->path);

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "READLINK %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen);
        hdr    = gf_hdr_new (req, pathlen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino  = hton64 (ino);
        req->gen  = hton64 (gen);
        req->size = hton32 (size);
        strcpy (req->path, loc->path);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_READLINK,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;

}


/**
 * client_mknod - mknod function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @path: pathname of node
 * @mode:
 * @dev:
 *
 * external reference through client_protocol_xlator->fops->mknod
 */
int
client_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
              dev_t dev)
{
        gf_hdr_common_t    *hdr = NULL;
        gf_fop_mknod_req_t *req = NULL;
        size_t              hdrlen = -1;
        int                 ret = -1;
        size_t              pathlen = 0;
        size_t              baselen = 0;
        ino_t               par = 0;
        uint64_t            gen = 0;
        client_local_t     *local = NULL;

        local = calloc (1, sizeof (*local));
        GF_VALIDATE_OR_GOTO (this->name, local, unwind);

        loc_copy (&local->loc, loc);

        frame->local = local;

        pathlen = STRLEN_0 (loc->path);
        baselen = STRLEN_0 (loc->name);
        ret = inode_ctx_get2 (loc->parent, this, &par, &gen);
        if (loc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "MKNOD %"PRId64"/%s (%s): failed to get remote inode "
                        "number for parent",
                        loc->parent->ino, loc->name, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen + baselen);
        hdr    = gf_hdr_new (req, pathlen + baselen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->par  = hton64 (par);
        req->gen  = hton64 (gen);
        req->mode = hton32 (mode);
        req->dev  = hton64 (dev);
        strcpy (req->path, loc->path);
        strcpy (req->bname + pathlen, loc->name);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_MKNOD,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, loc->inode, NULL);
        return 0;

}


/**
 * client_mkdir - mkdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @path: pathname of directory
 * @mode:
 *
 * external reference through client_protocol_xlator->fops->mkdir
 */
int
client_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode)
{
        gf_hdr_common_t    *hdr = NULL;
        gf_fop_mkdir_req_t *req = NULL;
        size_t              hdrlen = -1;
        int                 ret = -1;
        size_t              pathlen = 0;
        size_t              baselen = 0;
        ino_t               par = 0;
        uint64_t            gen = 0;
        client_local_t     *local = NULL;

        local = calloc (1, sizeof (*local));
        GF_VALIDATE_OR_GOTO (this->name, local, unwind);

        loc_copy (&local->loc, loc);

        frame->local = local;

        pathlen = STRLEN_0 (loc->path);
        baselen = STRLEN_0 (loc->name);
        ret = inode_ctx_get2 (loc->parent, this, &par, &gen);
        if (loc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "MKDIR %"PRId64"/%s (%s): failed to get remote inode "
                        "number for parent",
                        loc->parent->ino, loc->name, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen + baselen);
        hdr    = gf_hdr_new (req, pathlen + baselen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->par  = hton64 (par);
        req->gen  = hton64 (gen);
        req->mode = hton32 (mode);
        strcpy (req->path, loc->path);
        strcpy (req->bname + pathlen, loc->name);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_MKDIR,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, loc->inode, NULL);
        return 0;

}

/**
 * client_unlink - unlink function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location of file
 *
 * external reference through client_protocol_xlator->fops->unlink
 */

int
client_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        gf_hdr_common_t     *hdr = NULL;
        gf_fop_unlink_req_t *req = NULL;
        size_t               hdrlen = -1;
        int                  ret = -1;
        size_t               pathlen = 0;
        size_t               baselen = 0;
        ino_t                par = 0;
        uint64_t             gen = 0;

        pathlen = STRLEN_0 (loc->path);
        baselen = STRLEN_0 (loc->name);
        ret = inode_ctx_get2 (loc->parent, this, &par, &gen);
        if (loc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "UNLINK %"PRId64"/%s (%s): failed to get remote inode "
                        "number for parent",
                        loc->parent->ino, loc->name, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen + baselen);
        hdr    = gf_hdr_new (req, pathlen + baselen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->par  = hton64 (par);
        req->gen  = hton64 (gen);
        strcpy (req->path, loc->path);
        strcpy (req->bname + pathlen, loc->name);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_UNLINK,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL);
        return 0;

}

/**
 * client_rmdir - rmdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 *
 * external reference through client_protocol_xlator->fops->rmdir
 */

int
client_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        gf_hdr_common_t    *hdr = NULL;
        gf_fop_rmdir_req_t *req = NULL;
        size_t              hdrlen = -1;
        int                 ret = -1;
        size_t              pathlen = 0;
        size_t              baselen = 0;
        ino_t               par = 0;
        uint64_t            gen = 0;

        pathlen = STRLEN_0 (loc->path);
        baselen = STRLEN_0 (loc->name);
        ret = inode_ctx_get2 (loc->parent, this, &par, &gen);
        if (loc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "RMDIR %"PRId64"/%s (%s): failed to get remote inode "
                        "number for parent",
                        loc->parent->ino, loc->name, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen + baselen);
        hdr    = gf_hdr_new (req, pathlen + baselen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->par  = hton64 (par);
        req->gen  = hton64 (gen);
        strcpy (req->path, loc->path);
        strcpy (req->bname + pathlen, loc->name);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_RMDIR,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL);
        return 0;

}


/**
 * client_symlink - symlink function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @oldpath: pathname of target
 * @newpath: pathname of symlink
 *
 * external reference through client_protocol_xlator->fops->symlink
 */

int
client_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
                loc_t *loc)
{
        int                   ret = -1;
        gf_hdr_common_t      *hdr = NULL;
        gf_fop_symlink_req_t *req = NULL;
        size_t                hdrlen  = 0;
        size_t                pathlen = 0;
        size_t                newlen  = 0;
        size_t                baselen = 0;
        ino_t                 par = 0;
        uint64_t              gen = 0;
        client_local_t       *local = NULL;

        local = calloc (1, sizeof (*local));
        GF_VALIDATE_OR_GOTO (this->name, local, unwind);

        loc_copy (&local->loc, loc);

        frame->local = local;

        pathlen = STRLEN_0 (loc->path);
        baselen = STRLEN_0 (loc->name);
        newlen = STRLEN_0 (linkname);
        ret = inode_ctx_get2 (loc->parent, this, &par, &gen);
        if (loc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "SYMLINK %"PRId64"/%s (%s): failed to get remote inode"
                        " number parent",
                        loc->parent->ino, loc->name, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen + baselen + newlen);
        hdr    = gf_hdr_new (req, pathlen + baselen + newlen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->par =  hton64 (par);
        req->gen =  hton64 (gen);
        strcpy (req->path, loc->path);
        strcpy (req->bname + pathlen, loc->name);
        strcpy (req->linkname + pathlen + baselen, linkname);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_SYMLINK,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, loc->inode, NULL);
        return 0;

}

/**
 * client_rename - rename function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @oldloc: location of old pathname
 * @newloc: location of new pathname
 *
 * external reference through client_protocol_xlator->fops->rename
 */

int
client_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
               loc_t *newloc)
{
        int                  ret = -1;
        gf_hdr_common_t     *hdr = NULL;
        gf_fop_rename_req_t *req = NULL;
        size_t               hdrlen = 0;
        size_t               oldpathlen = 0;
        size_t               oldbaselen = 0;
        size_t               newpathlen = 0;
        size_t               newbaselen = 0;
        ino_t                oldpar = 0;
        uint64_t             oldgen = 0;
        ino_t                newpar = 0;
        uint64_t             newgen = 0;

        oldpathlen = STRLEN_0 (oldloc->path);
        oldbaselen = STRLEN_0 (oldloc->name);
        newpathlen = STRLEN_0 (newloc->path);
        newbaselen = STRLEN_0 (newloc->name);
        ret = inode_ctx_get2 (oldloc->parent, this, &oldpar, &oldgen);
        if (oldloc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "RENAME %"PRId64"/%s (%s): failed to get remote inode "
                        "number for source parent",
                        oldloc->parent->ino, oldloc->name, oldloc->path);
        }

        ret = inode_ctx_get2 (newloc->parent, this, &newpar, &newgen);
        if (newloc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "CREATE %"PRId64"/%s (%s): failed to get remote inode "
                        "number for destination parent",
                        newloc->parent->ino, newloc->name, newloc->path);
        }

        hdrlen = gf_hdr_len (req, (oldpathlen + oldbaselen +
                                   newpathlen + newbaselen));
        hdr    = gf_hdr_new (req, (oldpathlen + oldbaselen +
                                   newpathlen + newbaselen));

        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->oldpar = hton64 (oldpar);
        req->oldgen = hton64 (oldgen);
        req->newpar = hton64 (newpar);
        req->newgen = hton64 (newgen);

        strcpy (req->oldpath, oldloc->path);
        strcpy (req->oldbname + oldpathlen, oldloc->name);
        strcpy (req->newpath  + oldpathlen + oldbaselen, newloc->path);
        strcpy (req->newbname + oldpathlen + oldbaselen + newpathlen,
                newloc->name);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_RENAME,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;

}

/**
 * client_link - link function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @oldloc: location of old pathname
 * @newpath: new pathname
 *
 * external reference through client_protocol_xlator->fops->link
 */

int
client_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        int                ret = -1;
        gf_hdr_common_t   *hdr = NULL;
        gf_fop_link_req_t *req = NULL;
        size_t             hdrlen = 0;
        size_t             oldpathlen = 0;
        size_t             newpathlen = 0;
        size_t             newbaselen = 0;
        ino_t              oldino = 0;
        uint64_t           oldgen = 0;
        ino_t              newpar = 0;
        uint64_t           newgen = 0;
        client_local_t    *local = NULL;

        local = calloc (1, sizeof (*local));
        GF_VALIDATE_OR_GOTO (this->name, local, unwind);

        loc_copy (&local->loc, oldloc);

        frame->local = local;

        oldpathlen = STRLEN_0 (oldloc->path);
        newpathlen = STRLEN_0 (newloc->path);
        newbaselen = STRLEN_0 (newloc->name);

        ret = inode_ctx_get2 (oldloc->inode, this, &oldino, &oldgen);
        if (oldloc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "LINK %"PRId64"/%s (%s) ==> %"PRId64" (%s): "
                        "failed to get remote inode number for source inode",
                        newloc->parent->ino, newloc->name, newloc->path,
                        oldloc->ino, oldloc->path);
        }

        ret = inode_ctx_get2 (newloc->parent, this, &newpar, &newgen);
        if (newloc->parent->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "LINK %"PRId64"/%s (%s) ==> %"PRId64" (%s): "
                        "failed to get remote inode number destination parent",
                        newloc->parent->ino, newloc->name, newloc->path,
                        oldloc->ino, oldloc->path);
        }

        hdrlen = gf_hdr_len (req, oldpathlen + newpathlen + newbaselen);
        hdr    = gf_hdr_new (req, oldpathlen + newpathlen + newbaselen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        strcpy (req->oldpath, oldloc->path);
        strcpy (req->newpath  + oldpathlen, newloc->path);
        strcpy (req->newbname + oldpathlen + newpathlen, newloc->name);

        req->oldino = hton64 (oldino);
        req->oldgen = hton64 (oldgen);
        req->newpar = hton64 (newpar);
        req->newgen = hton64 (newgen);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_LINK,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, oldloc->inode, NULL);
        return 0;
}


/**
 * client_truncate - truncate function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @offset:
 *
 * external reference through client_protocol_xlator->fops->truncate
 */

int
client_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
        gf_hdr_common_t       *hdr = NULL;
        gf_fop_truncate_req_t *req = NULL;
        size_t                 hdrlen = -1;
        int                    ret = -1;
        size_t                 pathlen = 0;
        ino_t                  ino = 0;
        uint64_t               gen = 0;

        pathlen = STRLEN_0 (loc->path);
        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "TRUNCATE %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen);
        hdr    = gf_hdr_new (req, pathlen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino    = hton64 (ino);
        req->gen    = hton64 (gen);
        req->offset = hton64 (offset);
        strcpy (req->path, loc->path);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_TRUNCATE,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;

}


/**
 * client_readv - readv function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @size:
 * @offset:
 *
 * external reference through client_protocol_xlator->fops->readv
 */

int
client_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset)
{
        gf_hdr_common_t    *hdr = NULL;
        gf_fop_read_req_t  *req = NULL;
        size_t              hdrlen = 0;
        int64_t             remote_fd = -1;
        int                 ret = -1;
        client_fd_ctx_t    *fdctx = NULL;
        client_conf_t      *conf = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx, EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EINVAL, NULL, 0, NULL);
                return 0;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx, EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, NULL, 0, NULL);
                return 0;
        }

        remote_fd = fdctx->remote_fd;
        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->fd     = hton64 (remote_fd);
        req->size   = hton32 (size);
        req->offset = hton64 (offset);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_READ,
                                    hdr, hdrlen, NULL, 0, NULL);

        return 0;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, NULL, 0, NULL);
        return 0;

}

/**
 * client_writev - writev function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @vector:
 * @count:
 * @offset:
 *
 * external reference through client_protocol_xlator->fops->writev
 */

int
client_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
               struct iovec *vector, int32_t count, off_t offset,
               struct iobref *iobref)
{
        gf_hdr_common_t    *hdr = NULL;
        gf_fop_write_req_t *req = NULL;
        size_t              hdrlen = 0;
        int64_t             remote_fd = -1;
        int                 ret = -1;
        client_fd_ctx_t    *fdctx = NULL;
        client_conf_t      *conf = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, NULL);
                return 0;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, NULL);
                return 0;
        }

        remote_fd = fdctx->remote_fd;
        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->fd     = hton64 (remote_fd);
        req->size   = hton32 (iov_length (vector, count));
        req->offset = hton64 (offset);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_WRITE,
                                    hdr, hdrlen, vector, count, iobref);
        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;

}


/**
 * client_statfs - statfs function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 *
 * external reference through client_protocol_xlator->fops->statfs
 */

int
client_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        gf_hdr_common_t     *hdr = NULL;
        gf_fop_statfs_req_t *req = NULL;
        size_t               hdrlen = -1;
        int                  ret = -1;
        size_t               pathlen = 0;
        ino_t                ino = 0;
        ino_t                gen = 0;

        pathlen = STRLEN_0 (loc->path);

        if (loc->inode) {
                ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
                if (loc->inode->ino && ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "STATFS %"PRId64" (%s): "
                                "failed to get remote inode number",
                                loc->inode->ino, loc->path);
                }
        }

        hdrlen = gf_hdr_len (req, pathlen);
        hdr    = gf_hdr_new (req, pathlen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino = hton64 (ino);
        req->gen = hton64 (gen);
        strcpy (req->path, loc->path);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_STATFS,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;

}


/**
 * client_flush - flush function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->fops->flush
 */

int
client_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        gf_hdr_common_t     *hdr = NULL;
        gf_fop_flush_req_t  *req = NULL;
        size_t               hdrlen = 0;
        int64_t              remote_fd = -1;
        int                  ret = -1;
        client_fd_ctx_t     *fdctx = NULL;
        client_conf_t       *conf = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD);
                return 0;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD);
                return 0;
        }

        remote_fd = fdctx->remote_fd;
        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->fd = hton64 (remote_fd);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_FLUSH,
                                    hdr, hdrlen, NULL, 0, NULL);

        return 0;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL);
        return 0;

}

/**
 * client_fsync - fsync function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @flags:
 *
 * external reference through client_protocol_xlator->fops->fsync
 */

int
client_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
        gf_hdr_common_t    *hdr = NULL;
        gf_fop_fsync_req_t *req = NULL;
        size_t              hdrlen = 0;
        int64_t             remote_fd = -1;
        int32_t             ret = -1;
        client_fd_ctx_t    *fdctx = NULL;
        client_conf_t      *conf = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD);
                return 0;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD);
                return 0;
        }

        remote_fd = fdctx->remote_fd;
        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->fd   = hton64 (remote_fd);
        req->data = hton32 (flags);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_FSYNC,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL);
        return 0;

}

int
client_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
                gf_xattrop_flags_t flags, dict_t *dict)
{
        gf_hdr_common_t      *hdr = NULL;
        gf_fop_xattrop_req_t *req = NULL;
        size_t                hdrlen = 0;
        size_t                dict_len = 0;
        int32_t               ret = -1;
        size_t                pathlen = 0;
        ino_t                 ino = 0;
        uint64_t              gen = 0;
        char                 *buf = NULL;

        GF_VALIDATE_OR_GOTO ("client", this, unwind);

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        if (dict) {
                ret = dict_allocate_and_serialize (dict, &buf, &dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized length of dict(%p)",
                                dict);
                        goto unwind;
                }
        }

        pathlen = STRLEN_0 (loc->path);

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "XATTROP %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        hdrlen = gf_hdr_len (req, dict_len + pathlen);
        hdr    = gf_hdr_new (req, dict_len + pathlen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->flags = hton32 (flags);
        req->dict_len = hton32 (dict_len);
        if (dict) {
                memcpy (req->dict, buf, dict_len);
                FREE (buf);
        }

        req->ino = hton64 (ino);
        req->gen = hton64 (gen);
        strcpy (req->path + dict_len, loc->path);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_XATTROP,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;
}


int
client_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 gf_xattrop_flags_t flags, dict_t *dict)
{
        gf_hdr_common_t       *hdr = NULL;
        gf_fop_fxattrop_req_t *req = NULL;
        size_t                 hdrlen = 0;
        size_t                 dict_len = 0;
        int64_t                remote_fd = -1;
        int32_t                ret = -1;
        ino_t                  ino = 0;
        client_fd_ctx_t       *fdctx = NULL;
        client_conf_t         *conf  = NULL;

        conf = this->private;

        if (dict) {
                dict_len = dict_serialized_length (dict);
                if (dict_len < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized length of dict(%p)",
                                dict);
                        goto unwind;
                }
        }

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. "
                        "returning EBADFD",
                        fd->inode->ino);
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                goto unwind;
        }

        ino = fd->inode->ino;
        remote_fd = fdctx->remote_fd;

        hdrlen = gf_hdr_len (req, dict_len);
        hdr    = gf_hdr_new (req, dict_len);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->flags = hton32 (flags);
        req->dict_len = hton32 (dict_len);
        if (dict) {
                ret = dict_serialize (dict, req->dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to serialize dictionary(%p)",
                                dict);
                        goto unwind;
                }
        }
        req->fd = hton64 (remote_fd);
        req->ino = hton64 (ino);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_FXATTROP,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EBADFD, NULL);
        return 0;

}

/**
 * client_setxattr - setxattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @dict: dictionary which contains key:value to be set.
 * @flags:
 *
 * external reference through client_protocol_xlator->fops->setxattr
 */

int
client_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 dict_t *dict, int32_t flags)
{
        gf_hdr_common_t       *hdr = NULL;
        gf_fop_setxattr_req_t *req = NULL;
        size_t                 hdrlen = 0;
        size_t                 dict_len = 0;
        int                    ret = -1;
        size_t                 pathlen = 0;
        ino_t                  ino = 0;
        uint64_t               gen = 0;

        dict_len = dict_serialized_length (dict);
        if (dict_len < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get serialized length of dict(%p)",
                        dict);
                goto unwind;
        }

        pathlen = STRLEN_0 (loc->path);

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "SETXATTR %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        hdrlen = gf_hdr_len (req, dict_len + pathlen);
        hdr    = gf_hdr_new (req, dict_len + pathlen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino   = hton64 (ino);
        req->gen   = hton64 (gen);
        req->flags = hton32 (flags);
        req->dict_len = hton32 (dict_len);

        ret = dict_serialize (dict, req->dict);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to serialize dictionary(%p)",
                        dict);
                goto unwind;
        }

        strcpy (req->path + dict_len, loc->path);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_SETXATTR,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL);
        return 0;
}

/**
 * client_fsetxattr - fsetxattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: fd
 * @dict: dictionary which contains key:value to be set.
 * @flags:
 *
 * external reference through client_protocol_xlator->fops->fsetxattr
 */

int
client_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  dict_t *dict, int32_t flags)
{
        gf_hdr_common_t        *hdr = NULL;
        gf_fop_fsetxattr_req_t *req = NULL;
        size_t                  hdrlen = 0;
        size_t                  dict_len = 0;
        ino_t                   ino;
        int                     ret = -1;
        int64_t                 remote_fd = -1;
        client_fd_ctx_t        *fdctx = NULL;
        client_conf_t          *conf  = NULL;

        conf = this->private;

        dict_len = dict_serialized_length (dict);
        if (dict_len < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get serialized length of dict(%p)",
                        dict);
                goto unwind;
        }

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                goto unwind;
        }

        ino = fd->inode->ino;
        remote_fd = fdctx->remote_fd;

        hdrlen = gf_hdr_len (req, dict_len);
        hdr    = gf_hdr_new (req, dict_len);

        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino   = hton64 (ino);
        req->fd    = hton64 (remote_fd);
        req->flags = hton32 (flags);
        req->dict_len = hton32 (dict_len);

        ret = dict_serialize (dict, req->dict);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to serialize dictionary(%p)",
                        dict);
                goto unwind;
        }

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_FSETXATTR,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL);
        return 0;
}

/**
 * client_getxattr - getxattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 *
 * external reference through client_protocol_xlator->fops->getxattr
 */

int
client_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 const char *name)
{
        int                    ret = -1;
        gf_hdr_common_t       *hdr = NULL;
        gf_fop_getxattr_req_t *req = NULL;
        size_t                 hdrlen = 0;
        size_t                 pathlen = 0;
        size_t                 namelen = 0;
        ino_t                  ino = 0;
        uint64_t               gen = 0;

        pathlen = STRLEN_0 (loc->path);
        if (name)
                namelen = STRLEN_0 (name);

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "GETXATTR %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen + namelen);
        hdr    = gf_hdr_new (req, pathlen + namelen);
        GF_VALIDATE_OR_GOTO (frame->this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino   = hton64 (ino);
        req->gen   = hton64 (gen);
        req->namelen = hton32 (namelen);
        strcpy (req->path, loc->path);
        if (name)
                strcpy (req->name + pathlen, name);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_GETXATTR,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;
}


/**
 * client_fgetxattr - fgetxattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: fd
 *
 * external reference through client_protocol_xlator->fops->fgetxattr
 */

int
client_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  const char *name)
{
        int                     ret = -1;
        gf_hdr_common_t        *hdr = NULL;
        gf_fop_fgetxattr_req_t *req = NULL;
        size_t                  hdrlen = 0;
        int64_t                 remote_fd = -1;
        size_t                  namelen = 0;
        ino_t                   ino = 0;
        client_fd_ctx_t        *fdctx = NULL;
        client_conf_t          *conf  = NULL;

        if (name)
                namelen = STRLEN_0 (name);

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get remote fd. EBADFD",
                        fd->inode->ino);
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                goto unwind;
        }

        ino = fd->inode->ino;
        remote_fd = fdctx->remote_fd;

        hdrlen = gf_hdr_len (req, namelen);
        hdr    = gf_hdr_new (req, namelen);

        GF_VALIDATE_OR_GOTO (frame->this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino   = hton64 (ino);
        req->fd    = hton64 (remote_fd);
        req->namelen = hton32 (namelen);

        if (name)
                strcpy (req->name, name);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_FGETXATTR,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;
}


/**
 * client_removexattr - removexattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 * @name:
 *
 * external reference through client_protocol_xlator->fops->removexattr
 */

int
client_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    const char *name)
{
        int                       ret = -1;
        gf_hdr_common_t          *hdr = NULL;
        gf_fop_removexattr_req_t *req = NULL;
        size_t                    hdrlen = 0;
        size_t                    namelen = 0;
        size_t                    pathlen = 0;
        ino_t                     ino = 0;
        uint64_t                  gen = 0;

        pathlen = STRLEN_0 (loc->path);
        namelen = STRLEN_0 (name);

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "REMOVEXATTR %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen + namelen);
        hdr    = gf_hdr_new (req, pathlen + namelen);
        GF_VALIDATE_OR_GOTO (frame->this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino   = hton64 (ino);
        req->gen   = hton64 (gen);
        strcpy (req->path, loc->path);
        strcpy (req->name + pathlen, name);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_REMOVEXATTR,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL);
        return 0;
}

/**
 * client_opendir - opendir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 *
 * external reference through client_protocol_xlator->fops->opendir
 */

int
client_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc,
                fd_t *fd)
{
        gf_fop_opendir_req_t *req = NULL;
        gf_hdr_common_t      *hdr = NULL;
        size_t                hdrlen = 0;
        int                   ret = -1;
        ino_t                 ino = 0;
        uint64_t              gen = 0;
        size_t                pathlen = 0;
        client_local_t       *local = NULL;

        local = calloc (1, sizeof (*local));
        GF_VALIDATE_OR_GOTO (this->name, local, unwind);

        loc_copy (&local->loc, loc);
        local->fd = fd_ref (fd);

        frame->local = local;

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "OPENDIR %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        pathlen = STRLEN_0 (loc->path);

        hdrlen = gf_hdr_len (req, pathlen);
        hdr    = gf_hdr_new (req, pathlen);
        GF_VALIDATE_OR_GOTO (frame->this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino = hton64 (ino);
        req->gen = hton64 (gen);
        strcpy (req->path, loc->path);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_OPENDIR,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, fd);
        return 0;

}


/**
 * client_readdir - readdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 *
 * external reference through client_protocol_xlator->fops->readdir
 */

int
client_getdents (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 size_t size, off_t offset, int32_t flag)
{
        gf_hdr_common_t       *hdr = NULL;
        gf_fop_getdents_req_t *req = NULL;
        size_t                 hdrlen = 0;
        int64_t                remote_fd = -1;
        int                    ret = -1;
        client_fd_ctx_t       *fdctx = NULL;
        client_conf_t         *conf = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, NULL);
                return 0;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, NULL);
        }

        remote_fd = fdctx->remote_fd;
        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (frame->this->name, hdr, unwind);

        req    = gf_param (hdr);
        GF_VALIDATE_OR_GOTO (frame->this->name, hdr, unwind);

        req->fd     = hton64 (remote_fd);
        req->size   = hton32 (size);
        req->offset = hton64 (offset);
        req->flags  = hton32 (flag);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_GETDENTS,
                                    hdr, hdrlen, NULL, 0, NULL);

        return 0;
unwind:
        STACK_UNWIND (frame, -1, EINVAL, NULL, 0);
        return 0;
}


/**
 * client_readdirp - readdirp function for client protocol
 * @frame: call frame
 * @this: this translator structure
 *
 * external reference through client_protocol_xlator->fops->readdirp
 */

int
client_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t offset)
{
        gf_hdr_common_t         *hdr = NULL;
        gf_fop_readdirp_req_t   *req = NULL;
        size_t                  hdrlen = 0;
        int64_t                 remote_fd = -1;
        int                     ret = -1;
        client_fd_ctx_t         *fdctx = NULL;
        client_conf_t           *conf  = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", fd->inode->ino);
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", fd->inode->ino);
                goto unwind;
        }

        remote_fd = fdctx->remote_fd;
        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req->fd     = hton64 (remote_fd);
        req->size   = hton32 (size);
        req->offset = hton64 (offset);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_READDIRP,
                                    hdr, hdrlen, NULL, 0, NULL);

        return 0;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EBADFD, NULL);
        return 0;

}


/**
 * client_readdir - readdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 *
 * external reference through client_protocol_xlator->fops->readdir
 */

int
client_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t offset)
{
        gf_hdr_common_t       *hdr = NULL;
        gf_fop_readdir_req_t  *req = NULL;
        size_t                 hdrlen = 0;
        int64_t                remote_fd = -1;
        int                    ret = -1;
        client_fd_ctx_t       *fdctx = NULL;
        client_conf_t         *conf  = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", fd->inode->ino);
                goto unwind;
        }

        remote_fd = fdctx->remote_fd;
        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req->fd     = hton64 (remote_fd);
        req->size   = hton32 (size);
        req->offset = hton64 (offset);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_READDIR,
                                    hdr, hdrlen, NULL, 0, NULL);

        return 0;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EBADFD, NULL);
        return 0;

}

/**
 * client_fsyncdir - fsyncdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @flags:
 *
 * external reference through client_protocol_xlator->fops->fsyncdir
 */

int
client_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
        gf_hdr_common_t       *hdr = NULL;
        gf_fop_fsyncdir_req_t *req = NULL;
        size_t                 hdrlen = 0;
        int64_t                remote_fd = -1;
        int32_t                ret = -1;
        client_fd_ctx_t       *fdctx = NULL;
        client_conf_t         *conf  = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", fd->inode->ino);
                goto unwind;
        }

        remote_fd = fdctx->remote_fd;
        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->data = hton32 (flags);
        req->fd   = hton64 (remote_fd);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_FSYNCDIR,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        STACK_UNWIND (frame, -1, EBADFD);
        return 0;
}

/**
 * client_access - access function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 * @mode:
 *
 * external reference through client_protocol_xlator->fops->access
 */

int
client_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        gf_hdr_common_t     *hdr = NULL;
        gf_fop_access_req_t *req = NULL;
        size_t               hdrlen = -1;
        int                  ret = -1;
        ino_t                ino = 0;
        uint64_t             gen = 0;
        size_t               pathlen = 0;

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "ACCESS %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        pathlen = STRLEN_0 (loc->path);

        hdrlen = gf_hdr_len (req, pathlen);
        hdr    = gf_hdr_new (req, pathlen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino  = hton64 (ino);
        req->gen  = hton64 (gen);
        req->mask = hton32 (mask);
        strcpy (req->path, loc->path);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_ACCESS,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL);
        return 0;

}

/**
 * client_ftrucate - ftruncate function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @offset: offset to truncate to
 *
 * external reference through client_protocol_xlator->fops->ftruncate
 */

int
client_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  off_t offset)
{
        gf_hdr_common_t        *hdr = NULL;
        gf_fop_ftruncate_req_t *req = NULL;
        int64_t                 remote_fd = -1;
        size_t                  hdrlen = -1;
        int                     ret = -1;
        client_fd_ctx_t        *fdctx = NULL;
        client_conf_t          *conf  = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, NULL);
                return 0;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", fd->inode->ino);
                goto unwind;
        }

        remote_fd = fdctx->remote_fd;
        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->fd     = hton64 (remote_fd);
        req->offset = hton64 (offset);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_FTRUNCATE,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;

}

/**
 * client_fstat - fstat function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->fops->fstat
 */

int
client_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        gf_hdr_common_t    *hdr = NULL;
        gf_fop_fstat_req_t *req = NULL;
        int64_t             remote_fd = -1;
        size_t              hdrlen = -1;
        int                 ret = -1;
        client_fd_ctx_t    *fdctx = NULL;
        client_conf_t      *conf  = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, NULL);
                return 0;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", fd->inode->ino);
                goto unwind;
        }

        remote_fd = fdctx->remote_fd;
        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->fd = hton64 (remote_fd);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_FSTAT,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;

}

/**
 * client_lk - lk function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @cmd: lock command
 * @lock:
 *
 * external reference through client_protocol_xlator->fops->lk
 */

int
client_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
           struct flock *flock)
{
        int              ret = -1;
        gf_hdr_common_t *hdr = NULL;
        gf_fop_lk_req_t *req = NULL;
        size_t           hdrlen = 0;
        int64_t          remote_fd = -1;
        int32_t          gf_cmd = 0;
        int32_t          gf_type = 0;
        client_fd_ctx_t *fdctx = NULL;
        client_conf_t   *conf  = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, NULL);
                return 0;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE, "(%"PRId64"): failed to get"
                        " fd ctx. EBADFD", fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, NULL);
                return 0;
        }

        remote_fd = fdctx->remote_fd;
        if (cmd == F_GETLK || cmd == F_GETLK64)
                gf_cmd = GF_LK_GETLK;
        else if (cmd == F_SETLK || cmd == F_SETLK64)
                gf_cmd = GF_LK_SETLK;
        else if (cmd == F_SETLKW || cmd == F_SETLKW64)
                gf_cmd = GF_LK_SETLKW;
        else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Unknown cmd (%d)!", gf_cmd);
                goto unwind;
        }

        switch (flock->l_type) {
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

        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->fd   = hton64 (remote_fd);
        req->cmd  = hton32 (gf_cmd);
        req->type = hton32 (gf_type);
        gf_flock_from_flock (&req->flock, flock);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_LK,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;
}

/**
 * client_inodelk - inodelk function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @inode: inode structure
 * @cmd: lock command
 * @lock: flock struct
 *
 * external reference through client_protocol_xlator->fops->inodelk
 */

int
client_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
                loc_t *loc, int32_t cmd, struct flock *flock)
{
        int                   ret = -1;
        gf_hdr_common_t      *hdr = NULL;
        gf_fop_inodelk_req_t *req = NULL;
        size_t                hdrlen = 0;
        int32_t               gf_cmd = 0;
        int32_t               gf_type = 0;
        ino_t                 ino  = 0;
        uint64_t              gen  = 0;
        size_t                pathlen = 0;
        size_t                vollen  = 0;

        pathlen = STRLEN_0 (loc->path);
        vollen  = STRLEN_0 (volume);

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "INODELK %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        if (cmd == F_GETLK || cmd == F_GETLK64)
                gf_cmd = GF_LK_GETLK;
        else if (cmd == F_SETLK || cmd == F_SETLK64)
                gf_cmd = GF_LK_SETLK;
        else if (cmd == F_SETLKW || cmd == F_SETLKW64)
                gf_cmd = GF_LK_SETLKW;
        else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Unknown cmd (%d)!", gf_cmd);
                goto unwind;
        }

        switch (flock->l_type) {
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

        hdrlen = gf_hdr_len (req, pathlen + vollen);
        hdr    = gf_hdr_new (req, pathlen + vollen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        strcpy (req->path, loc->path);
        strcpy (req->path + pathlen, volume);

        req->ino  = hton64 (ino);
        req->gen  = hton64 (gen);

        req->cmd  = hton32 (gf_cmd);
        req->type = hton32 (gf_type);
        gf_flock_from_flock (&req->flock, flock);


        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST,
                                    GF_FOP_INODELK,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL);
        return 0;

}


/**
 * client_finodelk - finodelk function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @inode: inode structure
 * @cmd: lock command
 * @lock: flock struct
 *
 * external reference through client_protocol_xlator->fops->finodelk
 */

int
client_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
                 fd_t *fd, int32_t cmd, struct flock *flock)
{
        int                    ret = -1;
        gf_hdr_common_t       *hdr = NULL;
        gf_fop_finodelk_req_t *req = NULL;
        size_t                 hdrlen = 0;
        size_t                 vollen = 0;
        int32_t                gf_cmd = 0;
        int32_t                gf_type = 0;
        int64_t                remote_fd = -1;
        client_fd_ctx_t       *fdctx = NULL;
        client_conf_t         *conf  = NULL;

        vollen = STRLEN_0 (volume);

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD);
                return 0;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD);
                return 0;
        }

        remote_fd = fdctx->remote_fd;
        if (cmd == F_GETLK || cmd == F_GETLK64)
                gf_cmd = GF_LK_GETLK;
        else if (cmd == F_SETLK || cmd == F_SETLK64)
                gf_cmd = GF_LK_SETLK;
        else if (cmd == F_SETLKW || cmd == F_SETLKW64)
                gf_cmd = GF_LK_SETLKW;
        else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Unknown cmd (%d)!", gf_cmd);
                goto unwind;
        }

        switch (flock->l_type) {
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

        hdrlen = gf_hdr_len (req, vollen);
        hdr    = gf_hdr_new (req, vollen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        strcpy (req->volume, volume);

        req->fd = hton64 (remote_fd);

        req->cmd  = hton32 (gf_cmd);
        req->type = hton32 (gf_type);
        gf_flock_from_flock (&req->flock, flock);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST,
                                    GF_FOP_FINODELK,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL);
        return 0;
}


int
client_entrylk (call_frame_t *frame, xlator_t *this, const char *volume,
                loc_t *loc, const char *name, entrylk_cmd cmd,
                entrylk_type type)
{
        gf_hdr_common_t      *hdr = NULL;
        gf_fop_entrylk_req_t *req = NULL;
        size_t                pathlen = 0;
        size_t                vollen  = 0;
        size_t                hdrlen = -1;
        int                   ret = -1;
        ino_t                 ino = 0;
        uint64_t              gen = 0;
        size_t                namelen = 0;

        pathlen = STRLEN_0 (loc->path);
        vollen  = STRLEN_0 (volume);

        if (name)
                namelen = STRLEN_0 (name);

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "ENTRYLK %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen + vollen + namelen);
        hdr    = gf_hdr_new (req, pathlen + vollen + namelen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino  = hton64 (ino);
        req->gen  = hton64 (gen);
        req->namelen = hton64 (namelen);

        strcpy (req->path, loc->path);
        if (name)
                strcpy (req->name + pathlen, name);
        strcpy (req->volume + pathlen + namelen, volume);

        req->cmd  = hton32 (cmd);
        req->type = hton32 (type);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_ENTRYLK,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL);
        return 0;

}


int
client_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume,
                 fd_t *fd, const char *name, entrylk_cmd cmd,
                 entrylk_type type)
{
        gf_hdr_common_t        *hdr = NULL;
        gf_fop_fentrylk_req_t  *req = NULL;
        int64_t                 remote_fd = -1;
        size_t                  vollen  = 0;
        size_t                  namelen = 0;
        size_t                  hdrlen = -1;
        int                     ret = -1;
        client_fd_ctx_t        *fdctx = NULL;
        client_conf_t          *conf  = NULL;

        if (name)
                namelen = STRLEN_0 (name);

        conf = this->private;

        vollen = STRLEN_0 (volume);

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD);
                return 0;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD);
                return 0;
        }

        remote_fd = fdctx->remote_fd;
        hdrlen = gf_hdr_len (req, namelen + vollen);
        hdr    = gf_hdr_new (req, namelen + vollen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->fd = hton64 (remote_fd);
        req->namelen = hton64 (namelen);

        if (name)
                strcpy (req->name, name);

        strcpy (req->volume + namelen, volume);

        req->cmd  = hton32 (cmd);
        req->type = hton32 (type);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_FENTRYLK,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL);
        return 0;
}

/*
 * client_lookup - lookup function for client protocol
 * @frame: call frame
 * @this:
 * @loc: location
 *
 * not for external reference
 */

int
client_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
               dict_t *xattr_req)
{
        gf_hdr_common_t     *hdr = NULL;
        gf_fop_lookup_req_t *req = NULL;
        size_t               hdrlen = -1;
        int                  ret = -1;
        ino_t                ino = 0;
        ino_t                par = 0;
        uint64_t             gen = 0;
        size_t               dictlen = 0;
        size_t               pathlen = 0;
        size_t               baselen = 0;
        int32_t              op_ret = -1;
        int32_t              op_errno = EINVAL;
        client_local_t      *local = NULL;
        char                *buf = NULL;

        local = calloc (1, sizeof (*local));
        GF_VALIDATE_OR_GOTO (this->name, local, unwind);

        loc_copy (&local->loc, loc);

        frame->local = local;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->path, unwind);

        if (loc->ino != 1) {
                ret = inode_ctx_get2 (loc->parent, this, &par, &gen);
                if (loc->parent->ino && ret < 0) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "LOOKUP %"PRId64"/%s (%s): failed to get "
                                "remote inode number for parent",
                                loc->parent->ino, loc->name, loc->path);
                }
                GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);
                baselen = STRLEN_0 (loc->name);
        } else {
                ino = 1;
        }

        pathlen = STRLEN_0 (loc->path);

        if (xattr_req) {
                ret = dict_allocate_and_serialize (xattr_req, &buf, &dictlen);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized length of dict(%p)",
                                xattr_req);
                        goto unwind;
                }
        }

        hdrlen = gf_hdr_len (req, pathlen + baselen + dictlen);
        hdr    = gf_hdr_new (req, pathlen + baselen + dictlen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino   = hton64 (ino);
        req->gen   = hton64 (gen);
        req->par   = hton64 (par);
        strcpy (req->path, loc->path);
        if (baselen)
                strcpy (req->path + pathlen, loc->name);

        if (dictlen > 0) {
                memcpy (req->dict + pathlen + baselen, buf, dictlen);
                FREE (buf);
        }

        req->dictlen = hton32 (dictlen);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_LOOKUP,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;

unwind:
        STACK_UNWIND (frame, op_ret, op_errno, loc->inode, NULL, NULL);
        return ret;
}


int
client_setdents (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
                 dir_entry_t *entries, int32_t count)
{
        gf_hdr_common_t       *hdr = NULL;
        gf_fop_setdents_req_t *req = NULL;
        int64_t                remote_fd = 0;
        char                  *ptr = NULL;
        dir_entry_t           *trav = NULL;
        uint32_t               len = 0;
        int32_t                buf_len = 0;
        int32_t                ret = -1;
        int32_t                op_ret = -1;
        int32_t                op_errno = EINVAL;
        int32_t                vec_count = 0;
        size_t                 hdrlen = -1;
        struct iovec           vector[1];
        struct iobref         *iobref = NULL;
        struct iobuf          *iobuf = NULL;
        client_fd_ctx_t       *fdctx = NULL;
        client_conf_t         *conf  = NULL;

        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                op_errno = EBADFD;
                goto unwind;
        }

        remote_fd = fdctx->remote_fd;
        GF_VALIDATE_OR_GOTO (this->name, entries, unwind);
        GF_VALIDATE_OR_GOTO (this->name, count, unwind);

        trav = entries->next;
        while (trav) {
                len += strlen (trav->name);
                len += 1;
                len += strlen (trav->link);
                len += 1;
                len += 256; // max possible for statbuf;
                trav = trav->next;
        }
        iobuf = iobuf_get (this->ctx->iobuf_pool);
        GF_VALIDATE_OR_GOTO (this->name, iobuf, unwind);

        ptr = iobuf->ptr;

        trav = entries->next;
        while (trav) {
                int32_t this_len = 0;
                char *tmp_buf = NULL;
                struct stat *stbuf = &trav->buf;
                {
                        /* Convert the stat buf to string */
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

                        uint32_t atime_nsec = ST_ATIM_NSEC(stbuf);
                        uint32_t mtime_nsec = ST_MTIM_NSEC(stbuf);
                        uint32_t ctime_nsec = ST_CTIM_NSEC(stbuf);

                        ret = asprintf (&tmp_buf, GF_STAT_PRINT_FMT_STR,
                                        dev, ino, mode, nlink, uid, gid,
                                        rdev, size, blksize, blocks,
                                        atime, atime_nsec, mtime, mtime_nsec,
                                        ctime, ctime_nsec);
                        if (-1 == ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "asprintf failed while setting stat "
                                        "buf to string");
                                STACK_UNWIND (frame, -1, ENOMEM);
                                return 0;
                        }
                }
                this_len = sprintf (ptr, "%s/%s%s\n",
                                    trav->name, tmp_buf, trav->link);

                FREE (tmp_buf);
                trav = trav->next;
                ptr += this_len;
        }
        buf_len = strlen (iobuf->ptr);

        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->fd    = hton64 (remote_fd);
        req->flags = hton32 (flags);
        req->count = hton32 (count);

        iobref = iobref_new ();
        iobref_add (iobref, iobuf);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_SETDENTS,
                                    hdr, hdrlen, vector, vec_count, iobref);

        if (iobref)
                iobref_unref (iobref);

        if (iobuf)
                iobuf_unref (iobuf);

        return ret;
unwind:

        if (iobref)
                iobref_unref (iobref);

        if (iobuf)
                iobuf_unref (iobuf);

        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

int
client_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                struct stat *stbuf, int32_t valid)
{
        gf_hdr_common_t      *hdr = NULL;
        gf_fop_setattr_req_t *req = NULL;
        size_t                hdrlen = 0;
        size_t                pathlen = 0;
        ino_t                 ino = 0;
        uint64_t              gen = 0;
        int                   ret = -1;

        GF_VALIDATE_OR_GOTO ("client", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);

        pathlen = STRLEN_0 (loc->path);

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "SETATTR %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        hdrlen = gf_hdr_len (req, pathlen);
        hdr    = gf_hdr_new (req, pathlen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->ino  = hton64 (ino);
        req->gen  = hton64 (gen);
        strcpy (req->path, loc->path);

        gf_stat_from_stat (&req->stbuf, stbuf);
        req->valid = hton32 (valid);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_SETATTR,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;
}


int
client_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 struct stat *stbuf, int32_t valid)
{
        gf_hdr_common_t       *hdr = NULL;
        gf_fop_fsetattr_req_t *req = NULL;
        size_t                 hdrlen = 0;
        int                    ret = -1;
        client_fd_ctx_t       *fdctx = NULL;
        int64_t                remote_fd = -1;
        client_conf_t         *conf  = NULL;

        GF_VALIDATE_OR_GOTO ("client", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, NULL, NULL);
                return 0;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, NULL, NULL);
                return 0;
        }

        remote_fd = fdctx->remote_fd;
        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->fd = hton64 (remote_fd);

        gf_stat_from_stat (&req->stbuf, stbuf);
        req->valid = hton32 (valid);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_FSETATTR,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
        return 0;
}


int
client_fdctx_destroy (xlator_t *this, client_fd_ctx_t *fdctx)
{
        call_frame_t             *fr = NULL;
        int32_t                   ret = -1;
        gf_hdr_common_t          *hdr = NULL;
        size_t                    hdrlen = 0;
        gf_cbk_release_req_t     *req = NULL;
        gf_cbk_releasedir_req_t  *reqdir = NULL;
        int64_t                   remote_fd = -1;
        int                       op = 0;

        remote_fd = fdctx->remote_fd;

        if (remote_fd == -1)
                goto out;

        if (fdctx->is_dir) {
                hdrlen     = gf_hdr_len (reqdir, 0);
                hdr        = gf_hdr_new (reqdir, 0);
                op         = GF_CBK_RELEASEDIR;
                reqdir     = gf_param (hdr);
                reqdir->fd = hton64 (remote_fd);
        } else {
                hdrlen  = gf_hdr_len (req, 0);
                hdr     = gf_hdr_new (req, 0);
                op      = GF_CBK_RELEASE;
                req     = gf_param (hdr);
                req->fd = hton64 (remote_fd);
        }

        fr = create_frame (this, this->ctx->pool);

        ret = protocol_client_xfer (fr, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_CBK_REQUEST, op,
                                    hdr, hdrlen, NULL, 0, NULL);

out:
        inode_unref (fdctx->inode);
        FREE (fdctx);

        return ret;
}


/**
 * client_releasedir - releasedir function for client protocol
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->cbks->releasedir
 */

int
client_releasedir (xlator_t *this, fd_t *fd)
{
        int64_t                remote_fd = -1;
        client_conf_t         *conf = NULL;
        client_fd_ctx_t       *fdctx = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_del_ctx (fd, this);
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
        pthread_mutex_unlock (&conf->mutex);

        if (remote_fd != -1)
                client_fdctx_destroy (this, fdctx);

        return 0;
}


/**
 * client_release - release function for client protocol
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->cbks->release
 *
 */
int
client_release (xlator_t *this, fd_t *fd)
{
        int64_t                remote_fd = -1;
        client_conf_t         *conf = NULL;
        client_fd_ctx_t       *fdctx = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_del_ctx (fd, this);
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
        pthread_mutex_unlock (&conf->mutex);

        if (remote_fd != -1)
                client_fdctx_destroy (this, fdctx);

        return 0;
}

/*
 * MGMT_OPS
 */

/**
 * client_stats - stats function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @flags:
 *
 * external reference through client_protocol_xlator->mops->stats
 */

int
client_stats (call_frame_t *frame, xlator_t *this, int32_t flags)
{
        gf_hdr_common_t     *hdr = NULL;
        gf_mop_stats_req_t  *req = NULL;
        size_t               hdrlen = -1;
        int                  ret = -1;

        GF_VALIDATE_OR_GOTO ("client", this, unwind);

        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req    = gf_param (hdr);

        req->flags = hton32 (flags);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_MOP_REQUEST, GF_MOP_STATS,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;
}


/* Callbacks */

int
client_fxattrop_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        gf_fop_xattrop_rsp_t *rsp = NULL;
        int32_t               op_ret   = 0;
        int32_t               gf_errno = 0;
        int32_t               op_errno = 0;
        int32_t               dict_len = 0;
        dict_t               *dict = NULL;
        int32_t               ret = -1;
        char                 *dictbuf = NULL;

        rsp = gf_param (hdr);
        GF_VALIDATE_OR_GOTO (frame->this->name, rsp, fail);

        op_ret   = ntoh32 (hdr->rsp.op_ret);

        if (op_ret >= 0) {
                op_ret = -1;
                dict_len = ntoh32 (rsp->dict_len);

                if (dict_len > 0) {
                        dictbuf = memdup (rsp->dict, dict_len);
                        GF_VALIDATE_OR_GOTO (frame->this->name, dictbuf, fail);

                        dict = dict_new();
                        GF_VALIDATE_OR_GOTO (frame->this->name, dict, fail);

                        ret = dict_unserialize (dictbuf, dict_len, &dict);
                        if (ret < 0) {
                                gf_log (frame->this->name, GF_LOG_DEBUG,
                                        "failed to serialize dictionary(%p)",
                                        dict);
                                op_errno = -ret;
                                goto fail;
                        } else {
                                dict->extra_free = dictbuf;
                                dictbuf = NULL;
                        }
                }
                op_ret = 0;
        }
        gf_errno = ntoh32 (hdr->rsp.op_errno);
        op_errno = gf_error_to_errno (gf_errno);

fail:
        STACK_UNWIND (frame, op_ret, op_errno, dict);

        if (dictbuf)
                free (dictbuf);

        if (dict)
                dict_unref (dict);

        return 0;
}


int
client_xattrop_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                    struct iobuf *iobuf)
{
        gf_fop_xattrop_rsp_t *rsp = NULL;
        int32_t               op_ret   = -1;
        int32_t               gf_errno = EINVAL;
        int32_t               op_errno = 0;
        int32_t               dict_len = 0;
        dict_t               *dict = NULL;
        int32_t               ret = -1;
        char                 *dictbuf = NULL;

        rsp = gf_param (hdr);
        GF_VALIDATE_OR_GOTO (frame->this->name, rsp, fail);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        if (op_ret >= 0) {
                op_ret = -1;
                dict_len = ntoh32 (rsp->dict_len);

                if (dict_len > 0) {
                        dictbuf = memdup (rsp->dict, dict_len);
                        GF_VALIDATE_OR_GOTO (frame->this->name, dictbuf, fail);

                        dict = get_new_dict();
                        GF_VALIDATE_OR_GOTO (frame->this->name, dict, fail);
                        dict_ref (dict);

                        ret = dict_unserialize (dictbuf, dict_len, &dict);
                        if (ret < 0) {
                                gf_log (frame->this->name, GF_LOG_DEBUG,
                                        "failed to serialize dictionary(%p)",
                                        dict);
                                goto fail;
                        } else {
                                dict->extra_free = dictbuf;
                                dictbuf = NULL;
                        }
                }
                op_ret = 0;
        }
        gf_errno = ntoh32 (hdr->rsp.op_errno);
        op_errno = gf_error_to_errno (gf_errno);


fail:
        STACK_UNWIND (frame, op_ret, op_errno, dict);

        if (dictbuf)
                free (dictbuf);
        if (dict)
                dict_unref (dict);

        return 0;
}

/*
 * client_create_cbk - create callback function for client protocol
 * @frame: call frame
 * @args: arguments in dictionary
 *
 * not for external reference
 */

int
client_create_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                   struct iobuf *iobuf)
{
        gf_fop_create_rsp_t  *rsp = NULL;
        int32_t               op_ret = 0;
        int32_t               op_errno = 0;
        fd_t                 *fd = NULL;
        inode_t              *inode = NULL;
        struct stat           stbuf = {0, };
        struct stat           preparent = {0, };
        struct stat           postparent = {0, };
        int64_t               remote_fd = 0;
        int32_t               ret = -1;
        client_local_t       *local = NULL;
        client_conf_t        *conf = NULL;
        client_fd_ctx_t      *fdctx = NULL;
        ino_t                 ino = 0;
        uint64_t              gen = 0;

        local = frame->local; frame->local = NULL;
        conf  = frame->this->private;
        fd    = local->fd;
        inode = local->loc.inode;

        rsp = gf_param (hdr);

        op_ret    = ntoh32 (hdr->rsp.op_ret);
        op_errno  = ntoh32 (hdr->rsp.op_errno);

        if (op_ret >= 0) {
                remote_fd = ntoh64 (rsp->fd);
                gf_stat_to_stat (&rsp->stat, &stbuf);

                gf_stat_to_stat (&rsp->preparent, &preparent);
                gf_stat_to_stat (&rsp->postparent, &postparent);

                ino = stbuf.st_ino;
                gen = stbuf.st_dev;
        }

        if (op_ret >= 0) {
                ret = inode_ctx_put2 (local->loc.inode, frame->this, ino, gen);

                if (ret < 0) {
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "CREATE %"PRId64"/%s (%s): failed to set "
                                "remote inode number to inode ctx",
                                local->loc.parent->ino, local->loc.name,
                                local->loc.path);
                }

                fdctx = CALLOC (1, sizeof (*fdctx));
                if (!fdctx) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unwind_out;
                }

                fdctx->remote_fd = remote_fd;
                fdctx->inode     = inode_ref (fd->inode);
                fdctx->ino       = ino;
                fdctx->gen       = gen;

                INIT_LIST_HEAD (&fdctx->sfd_pos);

                this_fd_set_ctx (fd, frame->this, &local->loc, fdctx);

                pthread_mutex_lock (&conf->mutex);
                {
                        list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
                }
                pthread_mutex_unlock (&conf->mutex);
        }
unwind_out:
        STACK_UNWIND (frame, op_ret, op_errno, fd, inode, &stbuf,
                      &preparent, &postparent);

        client_local_wipe (local);

        return 0;
}


/*
 * client_open_cbk - open callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int
client_open_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
        int32_t              op_ret = -1;
        int32_t              op_errno = ENOTCONN;
        fd_t                *fd = NULL;
        int64_t              remote_fd = 0;
        gf_fop_open_rsp_t   *rsp = NULL;
        client_local_t      *local = NULL;
        client_conf_t       *conf = NULL;
        client_fd_ctx_t     *fdctx = NULL;
        ino_t                ino = 0;
        uint64_t             gen = 0;


        local = frame->local;

        if (local->op) {
                local->op (frame, hdr, hdrlen, iobuf);
                return 0;
        }

        frame->local = NULL;
        conf  = frame->this->private;
        fd    = local->fd;

        rsp = gf_param (hdr);

        op_ret    = ntoh32 (hdr->rsp.op_ret);
        op_errno  = ntoh32 (hdr->rsp.op_errno);

        if (op_ret >= 0) {
                remote_fd = ntoh64 (rsp->fd);
        }

        if (op_ret >= 0) {
                fdctx = CALLOC (1, sizeof (*fdctx));
                if (!fdctx) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unwind_out;
                }

                inode_ctx_get2 (fd->inode, frame->this, &ino, &gen);

                fdctx->remote_fd = remote_fd;
                fdctx->inode     = inode_ref (fd->inode);
                fdctx->ino       = ino;
                fdctx->gen       = gen;

                INIT_LIST_HEAD (&fdctx->sfd_pos);

                this_fd_set_ctx (fd, frame->this, &local->loc, fdctx);

                pthread_mutex_lock (&conf->mutex);
                {
                        list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
                }
                pthread_mutex_unlock (&conf->mutex);
        }
unwind_out:
        STACK_UNWIND (frame, op_ret, op_errno, fd);

        client_local_wipe (local);

        return 0;
}

/*
 * client_stat_cbk - stat callback for client protocol
 * @frame: call frame
 * @args: arguments dictionary
 *
 * not for external reference
 */

int
client_stat_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
        struct stat        stbuf = {0, };
        gf_fop_stat_rsp_t *rsp = NULL;
        int32_t            op_ret = 0;
        int32_t            op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret == 0) {
                gf_stat_to_stat (&rsp->stat, &stbuf);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

        return 0;
}


/*
 * client_mknod_cbk - mknod callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_mknod_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
        gf_fop_mknod_rsp_t *rsp = NULL;
        int32_t             op_ret = 0;
        int32_t             op_errno = 0;
        struct stat         stbuf = {0, };
        inode_t            *inode = NULL;
        client_local_t     *local = NULL;
        int                 ret = 0;
        struct stat         preparent = {0,};
        struct stat         postparent = {0,};

        local = frame->local;
        frame->local = NULL;
        inode = local->loc.inode;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret >= 0) {
                gf_stat_to_stat (&rsp->stat, &stbuf);

                ret = inode_ctx_put2 (local->loc.inode, frame->this,
                                      stbuf.st_ino, stbuf.st_dev);
                if (ret < 0) {
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "MKNOD %"PRId64"/%s (%s): failed to set remote"
                                " inode number to inode ctx",
                                local->loc.parent->ino, local->loc.name,
                                local->loc.path);
                }

                gf_stat_to_stat (&rsp->preparent, &preparent);
                gf_stat_to_stat (&rsp->postparent, &postparent);
        }

        STACK_UNWIND (frame, op_ret, op_errno, inode, &stbuf,
                      &preparent, &postparent);

        client_local_wipe (local);

        return 0;
}

/*
 * client_symlink_cbk - symlink callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_symlink_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                    struct iobuf *iobuf)
{
        gf_fop_symlink_rsp_t *rsp = NULL;
        int32_t               op_ret = 0;
        int32_t               op_errno = 0;
        struct stat           stbuf = {0, };
        struct stat           preparent = {0,};
        struct stat           postparent = {0,};
        inode_t              *inode = NULL;
        client_local_t       *local = NULL;
        int                   ret = 0;

        local = frame->local;
        frame->local = NULL;
        inode = local->loc.inode;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret >= 0) {
                gf_stat_to_stat (&rsp->stat, &stbuf);

                ret = inode_ctx_put2 (inode, frame->this,
                                      stbuf.st_ino, stbuf.st_dev);
                if (ret < 0) {
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "SYMLINK %"PRId64"/%s (%s): failed to set "
                                "remote inode number to inode ctx",
                                local->loc.parent->ino, local->loc.name,
                                local->loc.path);
                }
                gf_stat_to_stat (&rsp->preparent, &preparent);
                gf_stat_to_stat (&rsp->postparent, &postparent);
        }

        STACK_UNWIND (frame, op_ret, op_errno, inode, &stbuf,
                      &preparent, &postparent);

        client_local_wipe (local);

        return 0;
}

/*
 * client_link_cbk - link callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_link_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                 struct iobuf *iobuf)
{
        gf_fop_link_rsp_t *rsp = NULL;
        int32_t            op_ret = 0;
        int32_t            op_errno = 0;
        struct stat        stbuf = {0, };
        inode_t           *inode = NULL;
        client_local_t    *local = NULL;
        struct stat        preparent = {0,};
        struct stat        postparent = {0,};

        local = frame->local;
        frame->local = NULL;
        inode = local->loc.inode;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret >= 0) {
                gf_stat_to_stat (&rsp->stat, &stbuf);

                gf_stat_to_stat (&rsp->preparent, &preparent);
                gf_stat_to_stat (&rsp->postparent, &postparent);
        }

        STACK_UNWIND (frame, op_ret, op_errno, inode, &stbuf,
                      &preparent, &postparent);

        client_local_wipe (local);

        return 0;
}

/*
 * client_truncate_cbk - truncate callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_truncate_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        gf_fop_truncate_rsp_t *rsp = NULL;
        int32_t                op_ret = 0;
        int32_t                op_errno = 0;
        struct stat         prestat = {0, };
        struct stat         poststat = {0, };

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret == 0) {
                gf_stat_to_stat (&rsp->prestat, &prestat);
                gf_stat_to_stat (&rsp->poststat, &poststat);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &prestat, &poststat);

        return 0;
}

/* client_fstat_cbk - fstat callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_fstat_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
        struct stat         stbuf = {0, };
        gf_fop_fstat_rsp_t *rsp = NULL;
        int32_t             op_ret = 0;
        int32_t             op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret == 0) {
                gf_stat_to_stat (&rsp->stat, &stbuf);

        }

        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

        return 0;
}

/*
 * client_ftruncate_cbk - ftruncate callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int
client_ftruncate_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                      struct iobuf *iobuf)
{
        gf_fop_ftruncate_rsp_t *rsp = NULL;
        int32_t                 op_ret = 0;
        int32_t                 op_errno = 0;
        struct stat             prestat = {0, };
        struct stat             poststat = {0, };

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret == 0) {
                gf_stat_to_stat (&rsp->prestat, &prestat);
                gf_stat_to_stat (&rsp->poststat, &poststat);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &prestat, &poststat);

        return 0;
}


/* client_readv_cbk - readv callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external referece
 */

int
client_readv_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
        gf_fop_read_rsp_t  *rsp = NULL;
        int32_t             op_ret = 0;
        int32_t             op_errno = 0;
        struct iovec        vector = {0, };
        struct stat         stbuf = {0, };
        struct iobref      *iobref = NULL;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret != -1) {
                iobref = iobref_new ();
                gf_stat_to_stat (&rsp->stat, &stbuf);
                vector.iov_len  = op_ret;

                if (op_ret > 0) {
                        vector.iov_base = iobuf->ptr;
                        iobref_add (iobref, iobuf);
                }
        }

        STACK_UNWIND (frame, op_ret, op_errno, &vector, 1, &stbuf, iobref);

        if (iobref)
                iobref_unref (iobref);

        if (iobuf)
                iobuf_unref (iobuf);

        return 0;
}

/*
 * client_write_cbk - write callback for client protocol
 * @frame: cal frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_write_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
        gf_fop_write_rsp_t *rsp = NULL;
        int32_t             op_ret = 0;
        int32_t             op_errno = 0;
        struct stat         prestat = {0, };
        struct stat         poststat = {0, };

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret >= 0) {
                gf_stat_to_stat (&rsp->prestat, &prestat);
                gf_stat_to_stat (&rsp->poststat, &poststat);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &prestat, &poststat);

        return 0;
}


int
client_readdirp_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        gf_fop_readdirp_rsp_t   *rsp = NULL;
        int32_t                 op_ret = 0;
        int32_t                 op_errno = 0;
        uint32_t                buf_size = 0;
        gf_dirent_t             entries;

        rsp = gf_param (hdr);

        op_ret    = ntoh32 (hdr->rsp.op_ret);
        op_errno  = ntoh32 (hdr->rsp.op_errno);

        INIT_LIST_HEAD (&entries.list);
        if (op_ret > 0) {
                buf_size = ntoh32 (rsp->size);
                gf_dirent_unserialize (&entries, rsp->buf, buf_size);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &entries);

        gf_dirent_free (&entries);

        return 0;
}


int
client_readdir_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                    struct iobuf *iobuf)
{
        gf_fop_readdir_rsp_t *rsp = NULL;
        int32_t               op_ret = 0;
        int32_t               op_errno = 0;
        uint32_t              buf_size = 0;
        gf_dirent_t           entries;

        rsp = gf_param (hdr);

        op_ret    = ntoh32 (hdr->rsp.op_ret);
        op_errno  = ntoh32 (hdr->rsp.op_errno);

        INIT_LIST_HEAD (&entries.list);
        if (op_ret > 0) {
                buf_size = ntoh32 (rsp->size);
                gf_dirent_unserialize (&entries, rsp->buf, buf_size);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &entries);

        gf_dirent_free (&entries);

        return 0;
}

/*
 * client_fsync_cbk - fsync callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_fsync_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
        struct stat         prestat = {0, };
        struct stat         poststat = {0,};
        gf_fop_fsync_rsp_t *rsp = NULL;
        int32_t             op_ret = 0;
        int32_t             op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret == 0) {
                gf_stat_to_stat (&rsp->prestat, &prestat);
                gf_stat_to_stat (&rsp->poststat, &poststat);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &prestat, &poststat);

        return 0;
}

/*
 * client_unlink_cbk - unlink callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_unlink_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                   struct iobuf *iobuf)
{
        gf_fop_unlink_rsp_t *rsp = NULL;
        int32_t              op_ret = 0;
        int32_t              op_errno = 0;
        struct stat          preparent = {0,};
        struct stat          postparent = {0,};

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret == 0) {
                gf_stat_to_stat (&rsp->preparent, &preparent);
                gf_stat_to_stat (&rsp->postparent, &postparent);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &preparent, &postparent);

        return 0;
}

/*
 * client_rename_cbk - rename callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_rename_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                   struct iobuf *iobuf)
{
        struct stat          stbuf = {0, };
        gf_fop_rename_rsp_t *rsp = NULL;
        int32_t              op_ret = 0;
        int32_t              op_errno = 0;
        struct stat          preoldparent = {0, };
        struct stat          postoldparent = {0, };
        struct stat          prenewparent = {0, };
        struct stat          postnewparent = {0, };

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret == 0) {
                gf_stat_to_stat (&rsp->stat, &stbuf);
                gf_stat_to_stat (&rsp->preoldparent, &preoldparent);
                gf_stat_to_stat (&rsp->postoldparent, &postoldparent);
                gf_stat_to_stat (&rsp->prenewparent, &prenewparent);
                gf_stat_to_stat (&rsp->postnewparent, &postnewparent);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &stbuf, &preoldparent,
                      &postoldparent, &prenewparent, &postnewparent);

        return 0;
}


/*
 * client_readlink_cbk - readlink callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int
client_readlink_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        gf_fop_readlink_rsp_t *rsp = NULL;
        int32_t                op_ret = 0;
        int32_t                op_errno = 0;
        char                  *link = NULL;
        struct stat            stbuf = {0,};

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret > 0) {
                link = rsp->path;
                gf_stat_to_stat (&rsp->buf, &stbuf);
        }

        STACK_UNWIND (frame, op_ret, op_errno, link, &stbuf);
        return 0;
}

/*
 * client_mkdir_cbk - mkdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_mkdir_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
        gf_fop_mkdir_rsp_t *rsp = NULL;
        int32_t             op_ret = 0;
        int32_t             op_errno = 0;
        struct stat         stbuf = {0, };
        inode_t            *inode = NULL;
        client_local_t     *local = NULL;
        int                 ret = 0;
        struct stat         preparent = {0,};
        struct stat         postparent = {0,};

        local = frame->local;
        inode = local->loc.inode;
        frame->local = NULL;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret >= 0) {
                gf_stat_to_stat (&rsp->stat, &stbuf);

                ret = inode_ctx_put2 (inode, frame->this, stbuf.st_ino,
                                      stbuf.st_dev);
                if (ret < 0) {
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "MKDIR %"PRId64"/%s (%s): failed to set "
                                "remote inode number to inode ctx",
                                local->loc.parent->ino, local->loc.name,
                                local->loc.path);
                }

                gf_stat_to_stat (&rsp->preparent, &preparent);
                gf_stat_to_stat (&rsp->postparent, &postparent);
        }

        STACK_UNWIND (frame, op_ret, op_errno, inode, &stbuf,
                      &preparent, &postparent);

        client_local_wipe (local);

        return 0;
}

/*
 * client_flush_cbk - flush callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_flush_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
        int32_t op_ret = 0;
        int32_t op_errno = 0;

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}

/*
 * client_opendir_cbk - opendir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_opendir_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                    struct iobuf *iobuf)
{
        int32_t               op_ret   = -1;
        int32_t               op_errno = ENOTCONN;
        fd_t                 *fd       = NULL;
        int64_t               remote_fd = 0;
        gf_fop_opendir_rsp_t *rsp       = NULL;
        client_local_t       *local = NULL;
        client_conf_t        *conf = NULL;
        client_fd_ctx_t      *fdctx = NULL;
        ino_t                 ino = 0;
        uint64_t              gen = 0;


        local = frame->local;

        if (local->op) {
                local->op (frame, hdr, hdrlen, iobuf);
                return 0;
        }

        frame->local = NULL;
        conf  = frame->this->private;
        fd    = local->fd;

        rsp = gf_param (hdr);

        op_ret    = ntoh32 (hdr->rsp.op_ret);
        op_errno  = ntoh32 (hdr->rsp.op_errno);

        if (op_ret >= 0) {
                remote_fd = ntoh64 (rsp->fd);
        }

        if (op_ret >= 0) {
                fdctx = CALLOC (1, sizeof (*fdctx));
                if (!fdctx) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unwind_out;
                }

                inode_ctx_get2 (fd->inode, frame->this, &ino, &gen);

                fdctx->remote_fd = remote_fd;
                fdctx->inode     = inode_ref (fd->inode);
                fdctx->ino       = ino;
                fdctx->gen       = gen;

                fdctx->is_dir    = 1;

                INIT_LIST_HEAD (&fdctx->sfd_pos);

                this_fd_set_ctx (fd, frame->this, &local->loc, fdctx);

                pthread_mutex_lock (&conf->mutex);
                {
                        list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
                }
                pthread_mutex_unlock (&conf->mutex);
        }
unwind_out:
        STACK_UNWIND (frame, op_ret, op_errno, fd);

        client_local_wipe (local);

        return 0;
}

/*
 * client_rmdir_cbk - rmdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_rmdir_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
        gf_fop_rmdir_rsp_t *rsp = NULL;
        int32_t             op_ret = 0;
        int32_t             op_errno = 0;
        struct stat         preparent = {0,};
        struct stat         postparent = {0,};

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret == 0) {
                gf_stat_to_stat (&rsp->preparent, &preparent);
                gf_stat_to_stat (&rsp->postparent, &postparent);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &preparent, &postparent);

        return 0;
}

/*
 * client_access_cbk - access callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_access_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                   struct iobuf *iobuf)
{
        gf_fop_access_rsp_t *rsp = NULL;
        int32_t              op_ret = 0;
        int32_t              op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}

/*
 * client_lookup_cbk - lookup callback for client protocol
 *
 * @frame: call frame
 * @args: arguments dictionary
 *
 * not for external reference
 */

int
client_lookup_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                   struct iobuf *iobuf)
{
        struct stat          stbuf = {0, };
        struct stat          postparent = {0, };
        inode_t             *inode = NULL;
        dict_t              *xattr = NULL;
        gf_fop_lookup_rsp_t *rsp = NULL;
        int32_t              op_ret = 0;
        int32_t              op_errno = 0;
        size_t               dict_len = 0;
        char                *dictbuf = NULL;
        int32_t              ret = -1;
        int32_t              gf_errno = 0;
        client_local_t      *local = NULL;
        ino_t                oldino = 0;
        uint64_t             oldgen = 0;

        local = frame->local;
        inode = local->loc.inode;
        frame->local = NULL;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);

        gf_stat_to_stat (&rsp->postparent, &postparent);

        if (op_ret == 0) {
                op_ret = -1;
                gf_stat_to_stat (&rsp->stat, &stbuf);

                ret = inode_ctx_get2 (inode, frame->this, &oldino, &oldgen);
                if (oldino != stbuf.st_ino || oldgen != stbuf.st_dev) {
                        if (oldino) {
                                gf_log (frame->this->name, GF_LOG_DEBUG,
                                        "LOOKUP %"PRId64"/%s (%s): "
                                        "inode number changed from "
                                        "%"PRId64" to %"PRId64,
                                        local->loc.parent->ino,
                                        local->loc.name,
                                        local->loc.path,
                                        oldino, stbuf.st_ino);
                                goto fail;
                        }

                        ret = inode_ctx_put2 (inode, frame->this,
                                              stbuf.st_ino, stbuf.st_dev);
                        if (ret < 0) {
                                gf_log (frame->this->name, GF_LOG_DEBUG,
                                        "LOOKUP %"PRId64"/%s (%s) : "
                                        "failed to set remote inode "
                                        "number to inode ctx",
                                        local->loc.parent->ino,
                                        local->loc.name,
                                        local->loc.path);
                        }
                }

                dict_len = ntoh32 (rsp->dict_len);

                if (dict_len > 0) {
                        dictbuf = memdup (rsp->dict, dict_len);
                        GF_VALIDATE_OR_GOTO (frame->this->name, dictbuf, fail);

                        xattr = dict_new();
                        GF_VALIDATE_OR_GOTO (frame->this->name, xattr, fail);

                        ret = dict_unserialize (dictbuf, dict_len, &xattr);
                        if (ret < 0) {
                                gf_log (frame->this->name, GF_LOG_DEBUG,
                                        "%s (%"PRId64"): failed to "
                                        "unserialize dictionary",
                                        local->loc.path, inode->ino);
                                goto fail;
                        } else {
                                xattr->extra_free = dictbuf;
                                dictbuf = NULL;
                        }
                }
                op_ret = 0;
        }
        gf_errno = ntoh32 (hdr->rsp.op_errno);
        op_errno = gf_error_to_errno (gf_errno);

fail:
        STACK_UNWIND (frame, op_ret, op_errno, inode, &stbuf, xattr,
                      &postparent);

        client_local_wipe (local);

        if (dictbuf)
                free (dictbuf);

        if (xattr)
                dict_unref (xattr);

        return 0;
}

static int32_t
client_setattr_cbk (call_frame_t *frame,gf_hdr_common_t *hdr, size_t hdrlen,
                    struct iobuf *iobuf)
{
        struct stat           statpre = {0, };
        struct stat           statpost = {0, };
        gf_fop_setattr_rsp_t *rsp = NULL;
        int32_t               op_ret = 0;
        int32_t               op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret == 0) {
                gf_stat_to_stat (&rsp->statpre, &statpre);
                gf_stat_to_stat (&rsp->statpost, &statpost);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &statpre, &statpost);

        return 0;
}

static int32_t
client_fsetattr_cbk (call_frame_t *frame,gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        struct stat           statpre = {0, };
        struct stat           statpost = {0, };
        gf_fop_setattr_rsp_t *rsp = NULL;
        int32_t               op_ret = 0;
        int32_t               op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret == 0) {
                gf_stat_to_stat (&rsp->statpre, &statpre);
                gf_stat_to_stat (&rsp->statpost, &statpost);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &statpre, &statpost);

        return 0;
}

static dir_entry_t *
gf_bin_to_direntry (char *buf, size_t count)
{
        int           idx = 0;
        int           bread = 0;
        size_t        rcount = 0;
        char         *ender = NULL;
        char         *buffer = NULL;
        char          tmp_buf[512] = {0,};
        dir_entry_t  *trav = NULL;
        dir_entry_t  *prev = NULL;
        dir_entry_t  *thead = NULL;
        dir_entry_t  *head = NULL;

        thead = CALLOC (1, sizeof (dir_entry_t));
        GF_VALIDATE_OR_GOTO ("client-protocol", thead, fail);

        buffer = buf;
        prev = thead;

        for (idx = 0; idx < count ; idx++) {
                bread = 0;
                trav = CALLOC (1, sizeof (dir_entry_t));
                GF_VALIDATE_OR_GOTO ("client-protocol", trav, fail);

                ender = strchr (buffer, '/');
                if (!ender)
                        break;
                rcount = ender - buffer;
                trav->name = CALLOC (1, rcount + 2);
                GF_VALIDATE_OR_GOTO ("client-protocol", trav->name, fail);

                strncpy (trav->name, buffer, rcount);
                bread = rcount + 1;
                buffer += bread;

                ender = strchr (buffer, '\n');
                if (!ender)
                        break;
                rcount = ender - buffer;
                strncpy (tmp_buf, buffer, rcount);
                bread = rcount + 1;
                buffer += bread;

                gf_string_to_stat (tmp_buf, &trav->buf);

                ender = strchr (buffer, '\n');
                if (!ender)
                        break;
                rcount = ender - buffer;
                *ender = '\0';
                if (S_ISLNK (trav->buf.st_mode))
                        trav->link = strdup (buffer);
                else
                        trav->link = "";

                bread = rcount + 1;
                buffer += bread;

                prev->next = trav;
                prev = trav;
        }

        head = thead;
fail:
        return head;
}


int
gf_free_direntry (dir_entry_t *head)
{
        dir_entry_t *prev = NULL;
        dir_entry_t *trav = NULL;

        prev = head;
        GF_VALIDATE_OR_GOTO ("client-protocol", prev, fail);

        trav = head->next;
        while (trav) {
                prev->next = trav->next;
                FREE (trav->name);
                if (S_ISLNK (trav->buf.st_mode))
                        FREE (trav->link);
                FREE (trav);
                trav = prev->next;
        }
        FREE (head);
fail:
        return 0;
}

/*
 * client_getdents_cbk - readdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_getdents_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        gf_fop_getdents_rsp_t *rsp = NULL;
        int32_t                op_ret = 0;
        int32_t                op_errno = 0;
        int32_t                gf_errno = 0;
        int32_t                nr_count = 0;
        dir_entry_t           *entry = NULL;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        gf_errno = ntoh32 (hdr->rsp.op_errno);
        op_errno = gf_error_to_errno (gf_errno);

        if (op_ret >= 0) {
                nr_count = ntoh32 (rsp->count);
                entry = gf_bin_to_direntry(iobuf->ptr, nr_count);
                if (entry == NULL) {
                        op_ret = -1;
                        op_errno = EINVAL;
                }
        }

        STACK_UNWIND (frame, op_ret, op_errno, entry, nr_count);

        if (iobuf)
                iobuf_unref (iobuf);
        if (entry)
                gf_free_direntry(entry);

        return 0;
}

/*
 * client_statfs_cbk - statfs callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_statfs_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                   struct iobuf *iobuf)
{
        struct statvfs       stbuf = {0, };
        gf_fop_statfs_rsp_t *rsp = NULL;
        int32_t              op_ret = 0;
        int32_t              op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret == 0) {
                gf_statfs_to_statfs (&rsp->statfs, &stbuf);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

        return 0;
}

/*
 * client_fsyncdir_cbk - fsyncdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_fsyncdir_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        int32_t op_ret = 0;
        int32_t op_errno = 0;

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}

/*
 * client_setxattr_cbk - setxattr callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_setxattr_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        gf_fop_setxattr_rsp_t *rsp = NULL;
        int32_t                op_ret = 0;
        int32_t                op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}

/*
 * client_getxattr_cbk - getxattr callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_getxattr_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        gf_fop_getxattr_rsp_t *rsp = NULL;
        int32_t                op_ret   = 0;
        int32_t                gf_errno = 0;
        int32_t                op_errno = 0;
        int32_t                dict_len = 0;
        dict_t                *dict = NULL;
        int32_t                ret = -1;
        char                  *dictbuf = NULL;
        client_local_t        *local = NULL;

        local = frame->local;
        frame->local = NULL;

        rsp = gf_param (hdr);
        GF_VALIDATE_OR_GOTO (frame->this->name, rsp, fail);

        op_ret   = ntoh32 (hdr->rsp.op_ret);

        if (op_ret >= 0) {
                op_ret = -1;
                dict_len = ntoh32 (rsp->dict_len);

                if (dict_len > 0) {
                        dictbuf = memdup (rsp->dict, dict_len);
                        GF_VALIDATE_OR_GOTO (frame->this->name, dictbuf, fail);

                        dict = dict_new();
                        GF_VALIDATE_OR_GOTO (frame->this->name, dict, fail);

                        ret = dict_unserialize (dictbuf, dict_len, &dict);
                        if (ret < 0) {
                                gf_log (frame->this->name, GF_LOG_DEBUG,
                                        "%s (%"PRId64"): failed to "
                                        "unserialize xattr dictionary",
                                        local->loc.path,
                                        local->loc.inode->ino);
                                goto fail;
                        } else {
                                dict->extra_free = dictbuf;
                                dictbuf = NULL;
                        }
                }
                op_ret = 0;
        }
        gf_errno = ntoh32 (hdr->rsp.op_errno);
        op_errno = gf_error_to_errno (gf_errno);
fail:
        STACK_UNWIND (frame, op_ret, op_errno, dict);

        client_local_wipe (local);

        if (dictbuf)
                free (dictbuf);

        if (dict)
                dict_unref (dict);

        return 0;
}

/*
 * client_removexattr_cbk - removexattr callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_removexattr_cbk (call_frame_t *frame, gf_hdr_common_t *hdr,
                        size_t hdrlen, struct iobuf *iobuf)
{
        int32_t op_ret = 0;
        int32_t op_errno = 0;

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}

/*
 * client_lk_cbk - lk callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_lk_common_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                      struct iobuf *iobuf)
{
        struct flock     lock = {0,};
        gf_fop_lk_rsp_t *rsp = NULL;
        int32_t          op_ret = 0;
        int32_t          op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret >= 0) {
                gf_flock_to_flock (&rsp->flock, &lock);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &lock);
        return 0;
}

/*
 * client_gf_file_lk_cbk - gf_file_lk callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_inodelk_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                    struct iobuf *iobuf)
{
        gf_fop_inodelk_rsp_t *rsp = NULL;
        int32_t               op_ret = 0;
        int32_t               op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}


int
client_finodelk_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        gf_fop_finodelk_rsp_t *rsp = NULL;
        int32_t                op_ret = 0;
        int32_t                op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

/*
 * client_entrylk_cbk - entrylk callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_entrylk_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                    struct iobuf *iobuf)
{
        gf_fop_entrylk_rsp_t *rsp = NULL;
        int32_t               op_ret = 0;
        int32_t               op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

int
client_fentrylk_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        gf_fop_fentrylk_rsp_t *rsp = NULL;
        int32_t                op_ret = 0;
        int32_t                op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

/**
 * client_writedir_cbk -
 *
 * @frame:
 * @args:
 *
 * not for external reference
 */

int
client_setdents_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        int32_t op_ret = 0;
        int32_t op_errno = 0;

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}

/*
 * client_stats_cbk - stats callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_stats_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                  struct iobuf *iobuf)
{
        struct xlator_stats  stats = {0,};
        gf_mop_stats_rsp_t  *rsp = NULL;
        char                *buffer = NULL;
        int32_t              op_ret = 0;
        int32_t              op_errno = 0;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if (op_ret >= 0)
        {
                buffer = rsp->buf;

                sscanf (buffer, "%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64
                        ",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64"\n",
                        &stats.nr_files, &stats.disk_usage, &stats.free_disk,
                        &stats.total_disk_size, &stats.read_usage,
                        &stats.write_usage, &stats.disk_speed,
                        &stats.nr_clients);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &stats);
        return 0;
}

/*
 * client_getspec - getspec function for client protocol
 * @frame: call frame
 * @this: client protocol xlator structure
 * @flag:
 *
 * external reference through client_protocol_xlator->fops->getspec
 */

int
client_getspec (call_frame_t *frame, xlator_t *this, const char *key,
                int32_t flag)
{
        gf_hdr_common_t      *hdr = NULL;
        gf_mop_getspec_req_t *req = NULL;
        size_t                hdrlen = -1;
        int                   keylen = 0;
        int                   ret = -1;

        if (key)
                keylen = STRLEN_0 (key);

        hdrlen = gf_hdr_len (req, keylen);
        hdr    = gf_hdr_new (req, keylen);
        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req        = gf_param (hdr);
        req->flags = hton32 (flag);
        req->keylen = hton32 (keylen);
        if (keylen)
                strcpy (req->key, key);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_MOP_REQUEST, GF_MOP_GETSPEC,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
unwind:
        if (hdr)
                free (hdr);
        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;
}

/*
 * client_getspec_cbk - getspec callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_getspec_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                    struct iobuf *iobuf)
{
        gf_mop_getspec_rsp_t  *rsp = NULL;
        char                  *spec_data = NULL;
        int32_t                op_ret = 0;
        int32_t                op_errno = 0;
        int32_t                gf_errno = 0;

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        gf_errno = ntoh32 (hdr->rsp.op_errno);
        op_errno = gf_error_to_errno (gf_errno);
        rsp = gf_param (hdr);

        if (op_ret >= 0) {
                spec_data = rsp->spec;
        }

        STACK_UNWIND (frame, op_ret, op_errno, spec_data);
        return 0;
}


int
client_log (call_frame_t *frame, xlator_t *this, const char *msg)
{
        gf_hdr_common_t *     hdr = NULL;
        gf_mop_log_req_t *    req = NULL;
        size_t                hdrlen = -1;
        int                   msglen = 0;
        int                   ret = -1;

        if (msg)
                msglen = STRLEN_0 (msg);

        hdrlen = gf_hdr_len (req, msglen);
        hdr    = gf_hdr_new (req, msglen);

        GF_VALIDATE_OR_GOTO (this->name, hdr, unwind);

        req         = gf_param (hdr);
        req->msglen = hton32 (msglen);

        if (msglen)
                strcpy (req->msg, msg);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_MOP_REQUEST, GF_MOP_LOG,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;

unwind:
        if (hdr)
                free (hdr);

        STACK_UNWIND (frame, -1, EINVAL, NULL);
        return 0;
}


int
client_log_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen)
{
        gf_mop_log_rsp_t *     rsp      = NULL;

        int32_t                op_ret   = 0;
        int32_t                op_errno = 0;
        int32_t                gf_errno = 0;

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        gf_errno = ntoh32 (hdr->rsp.op_errno);
        op_errno = gf_error_to_errno (gf_errno);

        rsp = gf_param (hdr);

        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}


int
client_checksum (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flag)
{
        gf_hdr_common_t       *hdr = NULL;
        gf_fop_checksum_req_t *req = NULL;
        size_t                 hdrlen = -1;
        int                    ret = -1;
        ino_t                  ino = 0;
        uint64_t               gen = 0;

        hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
        hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
        req    = gf_param (hdr);

        ret = inode_ctx_get2 (loc->inode, this, &ino, &gen);
        if (loc->inode->ino && ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "CHECKSUM %"PRId64" (%s): "
                        "failed to get remote inode number",
                        loc->inode->ino, loc->path);
        }

        req->ino  = hton64 (ino);
        req->gen  = hton64 (gen);
        req->flag = hton32 (flag);
        strcpy (req->path, loc->path);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_CHECKSUM,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
}


int
client_checksum_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                     struct iobuf *iobuf)
{
        gf_fop_checksum_rsp_t *rsp = NULL;
        int32_t                op_ret = 0;
        int32_t                op_errno = 0;
        int32_t                gf_errno = 0;
        unsigned char         *fchecksum = NULL;
        unsigned char         *dchecksum = NULL;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        gf_errno = ntoh32 (hdr->rsp.op_errno);
        op_errno = gf_error_to_errno (gf_errno);

        if (op_ret >= 0) {
                fchecksum = rsp->fchecksum;
                dchecksum = rsp->dchecksum + NAME_MAX;
        }

        STACK_UNWIND (frame, op_ret, op_errno, fchecksum, dchecksum);
        return 0;
}


int
client_rchecksum (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                  int32_t len)
{
        gf_hdr_common_t        *hdr = NULL;
        gf_fop_rchecksum_req_t *req = NULL;
        size_t                  hdrlen = -1;
        int                     ret = -1;

        int64_t                remote_fd = -1;
        client_fd_ctx_t       *fdctx     = NULL;
        client_conf_t         *conf = NULL;

        hdrlen = gf_hdr_len (req, 0);
        hdr    = gf_hdr_new (req, 0);
        req    = gf_param (hdr);

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx = this_fd_get_ctx (fd, this);
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx == NULL) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, 0, NULL);
                return 0;
        }

        if (fdctx->remote_fd == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "(%"PRId64"): failed to get fd ctx. EBADFD",
                        fd->inode->ino);
                STACK_UNWIND (frame, -1, EBADFD, 0, NULL);
                return 0;
        }

        remote_fd = fdctx->remote_fd;

        req->fd     = hton64 (remote_fd);
        req->offset = hton64 (offset);
        req->len    = hton32 (len);

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_BULK),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_RCHECKSUM,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;
}


int
client_rchecksum_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                      struct iobuf *iobuf)
{
        gf_fop_rchecksum_rsp_t *rsp = NULL;

        int32_t                op_ret = 0;
        int32_t                op_errno = 0;
        int32_t                gf_errno = 0;
        uint32_t               weak_checksum = 0;
        unsigned char         *strong_checksum = NULL;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        gf_errno = ntoh32 (hdr->rsp.op_errno);
        op_errno = gf_error_to_errno (gf_errno);

        if (op_ret >= 0) {
                weak_checksum   = rsp->weak_checksum;
                strong_checksum = rsp->strong_checksum;
        }

        STACK_UNWIND (frame, op_ret, op_errno, weak_checksum, strong_checksum);

        return 0;
}


/*
 * client_setspec_cbk - setspec callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_setspec_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                    struct iobuf *iobuf)
{
        int32_t op_ret = 0;
        int32_t op_errno = 0;

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}



int
protocol_client_reopendir_cbk (call_frame_t *frame, gf_hdr_common_t *hdr,
                               size_t hdrlen, struct iobuf *iobuf)
{
        int32_t              op_ret = -1;
        int32_t              op_errno = ENOTCONN;
        int64_t              remote_fd = -1;
        gf_fop_open_rsp_t   *rsp = NULL;
        client_local_t      *local = NULL;
        client_conf_t       *conf = NULL;
        client_fd_ctx_t     *fdctx = NULL;


        local = frame->local; frame->local = NULL;
        conf  = frame->this->private;
        fdctx = local->fdctx;

        rsp = gf_param (hdr);

        op_ret    = ntoh32 (hdr->rsp.op_ret);
        op_errno  = ntoh32 (hdr->rsp.op_errno);

        if (op_ret >= 0)
                remote_fd = ntoh64 (rsp->fd);

        gf_log (frame->this->name, GF_LOG_DEBUG,
                "reopendir on %s returned %d (%"PRId64")",
                local->loc.path, op_ret, remote_fd);

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx->remote_fd = remote_fd;

                if (!fdctx->released) {
                        list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
                        fdctx = NULL;
                }
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx)
                client_fdctx_destroy (frame->this, fdctx);

        STACK_DESTROY (frame->root);

        client_local_wipe (local);

        return 0;
}



int
protocol_client_reopendir (xlator_t *this, client_fd_ctx_t *fdctx)
{
        int                    ret = -1;
        gf_hdr_common_t       *hdr = NULL;
        size_t                 hdrlen = 0;
        gf_fop_opendir_req_t  *req = NULL;
        size_t                 pathlen = 0;
        client_local_t        *local = NULL;
        inode_t               *inode = NULL;
        char                  *path = NULL;
        call_frame_t          *frame = NULL;

        inode = fdctx->inode;

        ret = inode_path (inode, NULL, &path);
        if (ret == -1) {
                goto out;
        }

        local = calloc (1, sizeof (*local));
        if (!local) {
                goto out;
        }

        local->fdctx = fdctx;
        local->op = protocol_client_reopendir_cbk;
        local->loc.path = path; path = NULL;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                goto out;
        }

        pathlen = STRLEN_0 (local->loc.path);

        hdrlen = gf_hdr_len (req, pathlen);
        hdr    = gf_hdr_new (req, pathlen);

        req    = gf_param (hdr);

        req->ino   = hton64 (fdctx->ino);
        req->gen   = hton64 (fdctx->gen);

        strcpy (req->path, local->loc.path);

        gf_log (frame->this->name, GF_LOG_DEBUG,
                "attempting reopendir on %s", local->loc.path);

        frame->local = local; local = NULL;

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_OPENDIR,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;

out:
        if (frame)
                STACK_DESTROY (frame->root);

        if (local)
                client_local_wipe (local);

        if (path)
                FREE (path);

        return 0;
}


int
protocol_client_reopen_cbk (call_frame_t *frame, gf_hdr_common_t *hdr,
                            size_t hdrlen, struct iobuf *iobuf)
{
        int32_t              op_ret = -1;
        int32_t              op_errno = ENOTCONN;
        int64_t              remote_fd = -1;
        gf_fop_open_rsp_t   *rsp = NULL;
        client_local_t      *local = NULL;
        client_conf_t       *conf = NULL;
        client_fd_ctx_t     *fdctx = NULL;


        local = frame->local; frame->local = NULL;
        conf  = frame->this->private;
        fdctx = local->fdctx;

        rsp = gf_param (hdr);

        op_ret    = ntoh32 (hdr->rsp.op_ret);
        op_errno  = ntoh32 (hdr->rsp.op_errno);

        if (op_ret >= 0)
                remote_fd = ntoh64 (rsp->fd);

        gf_log (frame->this->name, GF_LOG_DEBUG,
                "reopen on %s returned %d (%"PRId64")",
                local->loc.path, op_ret, remote_fd);

        pthread_mutex_lock (&conf->mutex);
        {
                fdctx->remote_fd = remote_fd;

                if (!fdctx->released) {
                        list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
                        fdctx = NULL;
                }
        }
        pthread_mutex_unlock (&conf->mutex);

        if (fdctx)
                client_fdctx_destroy (frame->this, fdctx);

        STACK_DESTROY (frame->root);

        client_local_wipe (local);

        return 0;
}


int
protocol_client_reopen (xlator_t *this, client_fd_ctx_t *fdctx)
{
        int                 ret = -1;
        gf_hdr_common_t    *hdr = NULL;
        size_t              hdrlen = 0;
        gf_fop_open_req_t  *req = NULL;
        size_t              pathlen = 0;
        client_local_t     *local = NULL;
        inode_t            *inode = NULL;
        char               *path = NULL;
        call_frame_t       *frame = NULL;

        inode = fdctx->inode;

        ret = inode_path (inode, NULL, &path);
        if (ret == -1) {
                goto out;
        }

        local = calloc (1, sizeof (*local));
        if (!local) {
                goto out;
        }

        local->fdctx = fdctx;
        local->op = protocol_client_reopen_cbk;
        local->loc.path = path; path = NULL;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                goto out;
        }

        pathlen = STRLEN_0 (local->loc.path);

        hdrlen = gf_hdr_len (req, pathlen);
        hdr    = gf_hdr_new (req, pathlen);

        req    = gf_param (hdr);

        req->ino   = hton64 (fdctx->ino);
        req->gen   = hton64 (fdctx->gen);
        req->flags = hton32 (gf_flags_from_flags (fdctx->flags));
        req->wbflags = hton32 (fdctx->wbflags);
        strcpy (req->path, local->loc.path);

        gf_log (frame->this->name, GF_LOG_DEBUG,
                "attempting reopen on %s", local->loc.path);

        frame->local = local; local = NULL;

        ret = protocol_client_xfer (frame, this,
                                    CLIENT_CHANNEL (this, CHANNEL_LOWLAT),
                                    GF_OP_TYPE_FOP_REQUEST, GF_FOP_OPEN,
                                    hdr, hdrlen, NULL, 0, NULL);

        return ret;

out:
        if (frame)
                STACK_DESTROY (frame->root);

        if (local)
                client_local_wipe (local);

        if (path)
                FREE (path);

        return 0;

}


int
protocol_client_post_handshake (call_frame_t *frame, xlator_t *this)
{
        client_conf_t            *conf = NULL;
        client_fd_ctx_t          *tmp = NULL;
        client_fd_ctx_t          *fdctx = NULL;
        xlator_list_t            *parent = NULL;
        struct list_head          reopen_head;

        conf = this->private;
        INIT_LIST_HEAD (&reopen_head);

        pthread_mutex_lock (&conf->mutex);
        {
                list_for_each_entry_safe (fdctx, tmp, &conf->saved_fds,
                                          sfd_pos) {
                        if (fdctx->remote_fd != -1)
                                continue;

                        list_del (&fdctx->sfd_pos);
                        list_add_tail (&fdctx->sfd_pos, &reopen_head);
                }
        }
        pthread_mutex_unlock (&conf->mutex);

        list_for_each_entry_safe (fdctx, tmp, &reopen_head, sfd_pos) {
                list_del_init (&fdctx->sfd_pos);

                if (fdctx->is_dir)
                        protocol_client_reopendir (this, fdctx);
                else
                        protocol_client_reopen (this, fdctx);
        }

        parent = this->parents;

        while (parent) {
                xlator_notify (parent->xlator, GF_EVENT_CHILD_UP,
                               this);
                parent = parent->next;
        }

        return 0;
}

/*
 * client_setvolume_cbk - setvolume callback for client protocol
 * @frame:  call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int
client_setvolume_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                      struct iobuf *iobuf)
{
        client_conf_t          *conf = NULL;
        gf_mop_setvolume_rsp_t *rsp = NULL;
        client_connection_t    *conn = NULL;
        glusterfs_ctx_t        *ctx = NULL;
        xlator_t               *this = NULL;
        xlator_list_t          *parent = NULL;
        transport_t            *trans = NULL;
        dict_t                 *reply = NULL;
        char                   *remote_subvol = NULL;
        char                   *remote_error = NULL;
        char                   *process_uuid = NULL;
        int32_t                 ret = -1;
        int32_t                 op_ret   = -1;
        int32_t                 op_errno = EINVAL;
        int32_t                 dict_len = 0;
        transport_t            *peer_trans = NULL;
        uint64_t                peer_trans_int = 0;

        trans = frame->local; frame->local = NULL;
        this  = frame->this;
        conn  = trans->xl_private;
        conf  = this->private;

        rsp = gf_param (hdr);

        op_ret   = ntoh32 (hdr->rsp.op_ret);
        op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

        if ((op_ret < 0) && (op_errno == ENOTCONN)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "setvolume failed (%s)",
                        strerror (op_errno));
                goto out;
        }

        reply = dict_new ();
        GF_VALIDATE_OR_GOTO (this->name, reply, out);

        dict_len = ntoh32 (rsp->dict_len);
        ret = dict_unserialize (rsp->buf, dict_len, &reply);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_DEBUG,
                        "failed to unserialize buffer(%p) to dictionary",
                        rsp->buf);
                goto out;
        }

        ret = dict_get_str (reply, "ERROR", &remote_error);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get ERROR string from reply dictionary");
        }

        ret = dict_get_str (reply, "process-uuid", &process_uuid);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get 'process-uuid' from reply dictionary");
        }

        if (op_ret < 0) {
                gf_log (trans->xl->name, GF_LOG_DEBUG,
                        "SETVOLUME on remote-host failed: %s",
                        remote_error ? remote_error : strerror (op_errno));
                errno = op_errno;
                if (op_errno == ESTALE) {
                        parent = trans->xl->parents;
                        while (parent) {
                                xlator_notify (parent->xlator,
                                               GF_EVENT_VOLFILE_MODIFIED,
                                               trans->xl);
                                parent = parent->next;
                        }
                }

        } else {
                ret = dict_get_str (this->options, "remote-subvolume",
                                    &remote_subvol);
                if (!remote_subvol)
                        goto out;

                ctx = this->ctx;

                if (process_uuid && !strcmp (ctx->process_uuid,process_uuid)) {
                        ret = dict_get_uint64 (reply, "transport-ptr",
                                               &peer_trans_int);

                        peer_trans = (void *) (long) (peer_trans_int);

                        gf_log (this->name, GF_LOG_WARNING,
                                "attaching to the local volume '%s'",
                                remote_subvol);

                        transport_setpeer (trans, peer_trans);

                }

                gf_log (trans->xl->name, GF_LOG_NORMAL,
                        "Connected to %s, attached "
                        "to remote volume '%s'.",
                        trans->peerinfo.identifier, remote_subvol);

                pthread_mutex_lock (&(conn->lock));
                {
                        conn->connected = 1;
                }
                pthread_mutex_unlock (&(conn->lock));

                protocol_client_post_handshake (frame, frame->this);
        }

        conf->connecting = 0;
out:

        if (-1 == op_ret) {
                /* Let the connection/re-connection happen in
                 * background, for now, don't hang here,
                 * tell the parents that i am all ok..
                 */
                parent = trans->xl->parents;
                while (parent) {
                        xlator_notify (parent->xlator,
                                       GF_EVENT_CHILD_CONNECTING, trans->xl);
                        parent = parent->next;
                }
                conf->connecting= 1;
        }

        STACK_DESTROY (frame->root);

        if (reply)
                dict_unref (reply);

        return op_ret;
}

/*
 * client_enosys_cbk -
 * @frame: call frame
 *
 * not for external reference
 */

int
client_enosys_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                   struct iobuf *iobuf)
{
        STACK_DESTROY (frame->root);
        return 0;
}


void
client_protocol_reconnect (void *trans_ptr)
{
        transport_t         *trans = NULL;
        client_connection_t *conn = NULL;
        struct timeval       tv = {0, 0};
        int32_t              ret = 0;

        trans = trans_ptr;
        conn  = trans->xl_private;
        pthread_mutex_lock (&conn->lock);
        {
                if (conn->reconnect)
                        gf_timer_call_cancel (trans->xl->ctx,
                                              conn->reconnect);
                conn->reconnect = 0;

                if (conn->connected == 0) {
                        tv.tv_sec = 10;

                        gf_log (trans->xl->name, GF_LOG_TRACE,
                                "attempting reconnect");
                        ret = transport_connect (trans);

                        conn->reconnect =
                                gf_timer_call_after (trans->xl->ctx, tv,
                                                     client_protocol_reconnect,
                                                     trans);
                } else {
                        gf_log (trans->xl->name, GF_LOG_TRACE,
                                "breaking reconnect chain");
                }
        }
        pthread_mutex_unlock (&conn->lock);

        if (ret == -1 && errno != EINPROGRESS) {
                default_notify (trans->xl, GF_EVENT_CHILD_DOWN, NULL);
        }
}

int
protocol_client_mark_fd_bad (xlator_t *this)
{
        client_conf_t            *conf = NULL;
        client_fd_ctx_t          *tmp = NULL;
        client_fd_ctx_t          *fdctx = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                list_for_each_entry_safe (fdctx, tmp, &conf->saved_fds,
                                          sfd_pos) {
                        fdctx->remote_fd = -1;
                }
        }
        pthread_mutex_unlock (&conf->mutex);

        return 0;
}

/*
 * client_protocol_cleanup - cleanup function
 * @trans: transport object
 *
 */

int
protocol_client_cleanup (transport_t *trans)
{
        client_connection_t    *conn = NULL;
        struct saved_frames    *saved_frames = NULL;

        conn = trans->xl_private;

        gf_log (trans->xl->name, GF_LOG_TRACE,
                "cleaning up state in transport object %p", trans);

        pthread_mutex_lock (&conn->lock);
        {
                saved_frames = conn->saved_frames;
                conn->saved_frames = saved_frames_new ();

                /* bailout logic cleanup */
                if (conn->timer) {
                        gf_timer_call_cancel (trans->xl->ctx, conn->timer);
                        conn->timer = NULL;
                }

                if (conn->reconnect == NULL) {
                        /* :O This part is empty.. any thing missing? */
                }
        }
        pthread_mutex_unlock (&conn->lock);

        saved_frames_destroy (trans->xl, saved_frames,
                              gf_fops, gf_mops, gf_cbks);

        return 0;
}


/* cbk callbacks */
int
client_releasedir_cbk (call_frame_t *frame, gf_hdr_common_t *hdr,
                       size_t hdrlen, struct iobuf *iobuf)
{
        STACK_DESTROY (frame->root);
        return 0;
}


int
client_release_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                    struct iobuf *iobuf)
{
        STACK_DESTROY (frame->root);
        return 0;
}


int
client_forget_cbk (call_frame_t *frame, gf_hdr_common_t *hdr, size_t hdrlen,
                   struct iobuf *iobuf)
{
        gf_log ("", GF_LOG_CRITICAL, "fop not implemented");
        return 0;
}


static gf_op_t gf_fops[] = {
        [GF_FOP_STAT]           =  client_stat_cbk,
        [GF_FOP_READLINK]       =  client_readlink_cbk,
        [GF_FOP_MKNOD]          =  client_mknod_cbk,
        [GF_FOP_MKDIR]          =  client_mkdir_cbk,
        [GF_FOP_UNLINK]         =  client_unlink_cbk,
        [GF_FOP_RMDIR]          =  client_rmdir_cbk,
        [GF_FOP_SYMLINK]        =  client_symlink_cbk,
        [GF_FOP_RENAME]         =  client_rename_cbk,
        [GF_FOP_LINK]           =  client_link_cbk,
        [GF_FOP_TRUNCATE]       =  client_truncate_cbk,
        [GF_FOP_OPEN]           =  client_open_cbk,
        [GF_FOP_READ]           =  client_readv_cbk,
        [GF_FOP_WRITE]          =  client_write_cbk,
        [GF_FOP_STATFS]         =  client_statfs_cbk,
        [GF_FOP_FLUSH]          =  client_flush_cbk,
        [GF_FOP_FSYNC]          =  client_fsync_cbk,
        [GF_FOP_SETXATTR]       =  client_setxattr_cbk,
        [GF_FOP_GETXATTR]       =  client_getxattr_cbk,
        [GF_FOP_REMOVEXATTR]    =  client_removexattr_cbk,
        [GF_FOP_OPENDIR]        =  client_opendir_cbk,
        [GF_FOP_GETDENTS]       =  client_getdents_cbk,
        [GF_FOP_FSYNCDIR]       =  client_fsyncdir_cbk,
        [GF_FOP_ACCESS]         =  client_access_cbk,
        [GF_FOP_CREATE]         =  client_create_cbk,
        [GF_FOP_FTRUNCATE]      =  client_ftruncate_cbk,
        [GF_FOP_FSTAT]          =  client_fstat_cbk,
        [GF_FOP_LK]             =  client_lk_common_cbk,
        [GF_FOP_LOOKUP]         =  client_lookup_cbk,
        [GF_FOP_SETDENTS]       =  client_setdents_cbk,
        [GF_FOP_READDIR]        =  client_readdir_cbk,
        [GF_FOP_READDIRP]       =  client_readdirp_cbk,
        [GF_FOP_INODELK]        =  client_inodelk_cbk,
        [GF_FOP_FINODELK]       =  client_finodelk_cbk,
        [GF_FOP_ENTRYLK]        =  client_entrylk_cbk,
        [GF_FOP_FENTRYLK]       =  client_fentrylk_cbk,
        [GF_FOP_CHECKSUM]       =  client_checksum_cbk,
        [GF_FOP_RCHECKSUM]      =  client_rchecksum_cbk,
        [GF_FOP_XATTROP]        =  client_xattrop_cbk,
        [GF_FOP_FXATTROP]       =  client_fxattrop_cbk,
        [GF_FOP_SETATTR]        =  client_setattr_cbk,
        [GF_FOP_FSETATTR]        =  client_fsetattr_cbk,
};

static gf_op_t gf_mops[] = {
        [GF_MOP_SETVOLUME]        =  client_setvolume_cbk,
        [GF_MOP_GETVOLUME]        =  client_enosys_cbk,
        [GF_MOP_STATS]            =  client_stats_cbk,
        [GF_MOP_SETSPEC]          =  client_setspec_cbk,
        [GF_MOP_GETSPEC]          =  client_getspec_cbk,
        [GF_MOP_PING]             =  client_ping_cbk,
};

static gf_op_t gf_cbks[] = {
        [GF_CBK_FORGET]           = client_forget_cbk,
        [GF_CBK_RELEASE]          = client_release_cbk,
        [GF_CBK_RELEASEDIR]       = client_releasedir_cbk
};

/*
 * client_protocol_interpret - protocol interpreter
 * @trans: transport object
 * @blk: data block
 *
 */
int
protocol_client_interpret (xlator_t *this, transport_t *trans,
                           char *hdr_p, size_t hdrlen, struct iobuf *iobuf)
{
        int                  ret = -1;
        call_frame_t        *frame = NULL;
        gf_hdr_common_t     *hdr = NULL;
        uint64_t             callid = 0;
        int                  type = -1;
        int                  op = -1;
        client_connection_t *conn = NULL;

        conn  = trans->xl_private;

        hdr  = (gf_hdr_common_t *)hdr_p;

        type   = ntoh32 (hdr->type);
        op     = ntoh32 (hdr->op);
        callid = ntoh64 (hdr->callid);

        frame  = lookup_frame (trans, op, type, callid);
        if (frame == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "no frame for callid=%"PRId64" type=%d op=%d",
                        callid, type, op);
                return 0;
        }

        switch (type) {
        case GF_OP_TYPE_FOP_REPLY:
                if ((op > GF_FOP_MAXVALUE) ||
                    (op < 0)) {
                        gf_log (trans->xl->name, GF_LOG_WARNING,
                                "invalid fop '%d'", op);
                } else {
                        ret = gf_fops[op] (frame, hdr, hdrlen, iobuf);
                }
                break;
        case GF_OP_TYPE_MOP_REPLY:
                if ((op > GF_MOP_MAXVALUE) ||
                    (op < 0)) {
                        gf_log (trans->xl->name, GF_LOG_WARNING,
                                "invalid fop '%d'", op);
                } else {
                        ret = gf_mops[op] (frame, hdr, hdrlen, iobuf);
                }
                break;
        case GF_OP_TYPE_CBK_REPLY:
                if ((op > GF_CBK_MAXVALUE) ||
                    (op < 0)) {
                        gf_log (trans->xl->name, GF_LOG_WARNING,
                                "invalid cbk '%d'", op);
                } else {
                        ret = gf_cbks[op] (frame, hdr, hdrlen, iobuf);
                }
                break;
        default:
                gf_log (trans->xl->name, GF_LOG_DEBUG,
                        "invalid packet type: %d", type);
                break;
        }

        return ret;
}

/*
 * init - initiliazation function. called during loading of client protocol
 * @this:
 *
 */

int
init (xlator_t *this)
{
        transport_t               *trans = NULL;
        client_conf_t             *conf = NULL;
        client_connection_t       *conn = NULL;
        int32_t                    frame_timeout = 0;
        int32_t                    ping_timeout = 0;
        data_t                    *remote_subvolume = NULL;
        int32_t                    ret = -1;
        int                        i = 0;

        if (this->children) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: client protocol translator cannot have any "
                        "subvolumes");
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Volume is dangling. ");
        }

        remote_subvolume = dict_get (this->options, "remote-subvolume");
        if (remote_subvolume == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Option 'remote-subvolume' is not specified.");
                goto out;
        }

        ret = dict_get_int32 (this->options, "frame-timeout",
                              &frame_timeout);
        if (ret >= 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "setting frame-timeout to %d", frame_timeout);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "defaulting frame-timeout to 30mins");
                frame_timeout = 1800;
        }

        ret = dict_get_int32 (this->options, "ping-timeout",
                              &ping_timeout);
        if (ret >= 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "setting ping-timeout to %d", ping_timeout);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "defaulting ping-timeout to 10");
                ping_timeout = 10;
        }

        conf = CALLOC (1, sizeof (client_conf_t));

        pthread_mutex_init (&conf->mutex, NULL);
        INIT_LIST_HEAD (&conf->saved_fds);

        this->private = conf;

        for (i = 0; i < CHANNEL_MAX; i++) {
                if (CHANNEL_LOWLAT == i) {
                        dict_set (this->options, "transport.socket.lowlat",
                                  data_from_dynstr (strdup ("true")));
                }
                trans = transport_load (this->options, this);
                if (trans == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Failed to load transport");
                        ret = -1;
                        goto out;
                }

                conn = CALLOC (1, sizeof (*conn));

                conn->saved_frames = saved_frames_new ();

                conn->callid = 1;

                conn->frame_timeout = frame_timeout;
                conn->ping_timeout = ping_timeout;

                pthread_mutex_init (&conn->lock, NULL);

                trans->xl_private = conn;
                conf->transport[i] = transport_ref (trans);
        }

#ifndef GF_DARWIN_HOST_OS
        {
                struct rlimit lim;

                lim.rlim_cur = 1048576;
                lim.rlim_max = 1048576;

                ret = setrlimit (RLIMIT_NOFILE, &lim);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "WARNING: Failed to set 'ulimit -n 1M': %s",
                                strerror(errno));
                        lim.rlim_cur = 65536;
                        lim.rlim_max = 65536;

                        ret = setrlimit (RLIMIT_NOFILE, &lim);
                        if (ret == -1) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Failed to set max open fd to 64k: %s",
                                        strerror(errno));
                        } else {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "max open fd set to 64k");
                        }

                }
        }
#endif
        ret = 0;
out:
        return ret;
}

/*
 * fini - finish function called during unloading of client protocol
 * @this:
 *
 */
void
fini (xlator_t *this)
{
        /* TODO: Check if its enough.. how to call transport's fini () */
        client_conf_t *conf = NULL;

        conf = this->private;
        this->private = NULL;

        if (conf) {
                FREE (conf);
        }
        return;
}


int
protocol_client_handshake (xlator_t *this, transport_t *trans)
{
        gf_hdr_common_t        *hdr = NULL;
        gf_mop_setvolume_req_t *req = NULL;
        dict_t                 *options = NULL;
        int32_t                 ret = -1;
        int                     hdrlen = 0;
        int                     dict_len = 0;
        call_frame_t           *fr = NULL;
        char                   *process_uuid_xl;

        options = this->options;
        ret = dict_set_str (options, "protocol-version", GF_PROTOCOL_VERSION);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to set protocol version(%s) in handshake msg",
                        GF_PROTOCOL_VERSION);
        }

        ret = asprintf (&process_uuid_xl, "%s-%s", this->ctx->process_uuid,
                        this->name);
        if (-1 == ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "asprintf failed while setting process_uuid");
                goto fail;
        }
        ret = dict_set_dynstr (options, "process-uuid",
                               process_uuid_xl);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to set process-uuid(%s) in handshake msg",
                        process_uuid_xl);
        }

        if (this->ctx->cmd_args.volfile_server) {
                if (this->ctx->cmd_args.volfile_id)
                        ret = dict_set_str (options, "volfile-key",
                                            this->ctx->cmd_args.volfile_id);
                ret = dict_set_uint32 (options, "volfile-checksum",
                                       this->ctx->volfile_checksum);
        }

        dict_len = dict_serialized_length (options);
        if (dict_len < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get serialized length of dict(%p)",
                        options);
                ret = dict_len;
                goto fail;
        }

        hdrlen = gf_hdr_len (req, dict_len);
        hdr    = gf_hdr_new (req, dict_len);
        GF_VALIDATE_OR_GOTO (this->name, hdr, fail);

        req    = gf_param (hdr);

        ret = dict_serialize (options, req->buf);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to serialize dictionary(%p)",
                        options);
                goto fail;
        }

        req->dict_len = hton32 (dict_len);
        fr  = create_frame (this, this->ctx->pool);
        GF_VALIDATE_OR_GOTO (this->name, fr, fail);

        fr->local = trans;
        ret = protocol_client_xfer (fr, this, trans,
                                    GF_OP_TYPE_MOP_REQUEST, GF_MOP_SETVOLUME,
                                    hdr, hdrlen, NULL, 0, NULL);
        return ret;
fail:
        if (hdr)
                free (hdr);
        return ret;
}


int
protocol_client_pollout (xlator_t *this, transport_t *trans)
{
        client_conf_t *conf = NULL;

        conf = trans->xl->private;

        pthread_mutex_lock (&conf->mutex);
        {
                gettimeofday (&conf->last_sent, NULL);
        }
        pthread_mutex_unlock (&conf->mutex);

        return 0;
}


int
protocol_client_pollin (xlator_t *this, transport_t *trans)
{
        client_conf_t *conf = NULL;
        int            ret = -1;
        struct iobuf  *iobuf = NULL;
        char          *hdr = NULL;
        size_t         hdrlen = 0;

        conf = trans->xl->private;

        pthread_mutex_lock (&conf->mutex);
        {
                gettimeofday (&conf->last_received, NULL);
        }
        pthread_mutex_unlock (&conf->mutex);

        ret = transport_receive (trans, &hdr, &hdrlen, &iobuf);

        if (ret == 0)
        {
                ret = protocol_client_interpret (this, trans, hdr, hdrlen,
                                                 iobuf);
        }

        /* TODO: use mem-pool */
        FREE (hdr);

        return ret;
}

int
client_priv_dump (xlator_t *this)
{
        client_conf_t   *conf = NULL;
        int             ret   = -1;
        client_fd_ctx_t *tmp = NULL;
        int             i = 0;
        char            key[GF_DUMP_MAX_BUF_LEN];
        char            key_prefix[GF_DUMP_MAX_BUF_LEN];

        if (!this)
                return -1;

        conf = this->private;
        if (!conf) {
                gf_log (this->name, GF_LOG_WARNING,
                        "conf null in xlator");
                return -1;
        }

        ret = pthread_mutex_trylock(&conf->mutex);
        if (ret) {
                gf_log("", GF_LOG_WARNING, "Unable to lock client %s"
                       " errno: %d", this->name, errno);
                return -1;
        }

        gf_proc_dump_build_key(key_prefix, "xlator.protocol.client",
                               "%s.priv", this->name);

        gf_proc_dump_add_section(key_prefix);

        list_for_each_entry(tmp, &conf->saved_fds, sfd_pos) {
                gf_proc_dump_build_key(key, key_prefix,
                                       "fd.%d.remote_fd", ++i);
                gf_proc_dump_write(key, "%d", tmp->remote_fd);
        }

        gf_proc_dump_build_key(key, key_prefix, "connecting");
        gf_proc_dump_write(key, "%d", conf->connecting);
        gf_proc_dump_build_key(key, key_prefix, "last_sent");
        gf_proc_dump_write(key, "%s", ctime(&conf->last_sent.tv_sec));
        gf_proc_dump_build_key(key, key_prefix, "last_received");
        gf_proc_dump_write(key, "%s", ctime(&conf->last_received.tv_sec));

        pthread_mutex_unlock(&conf->mutex);

        return 0;

}

int32_t
client_inodectx_dump (xlator_t *this, inode_t *inode)
{
        ino_t   par = 0;
        int     ret = -1;
        char    key[GF_DUMP_MAX_BUF_LEN];

        if (!inode)
                return -1;

        if (!this)
                return -1;

        ret = inode_ctx_get (inode, this, &par);

        if (ret != 0)
                return ret;

        gf_proc_dump_build_key(key, "xlator.protocol.client",
                               "%s.inode.%ld.par",
                               this->name,inode->ino);
        gf_proc_dump_write(key, "%ld", par);

        return 0;
}

/*
 * client_protocol_notify - notify function for client protocol
 * @this:
 * @trans: transport object
 * @event
 *
 */

int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        int                  i          = 0;
        int                  ret        = -1;
        int                  child_down = 1;
        int                  was_not_down = 0;
        transport_t         *trans      = NULL;
        client_connection_t *conn       = NULL;
        client_conf_t       *conf       = NULL;
        xlator_list_t       *parent = NULL;

        conf = this->private;
        trans = data;

        switch (event) {
        case GF_EVENT_POLLOUT:
        {
                ret = protocol_client_pollout (this, trans);

                break;
        }
        case GF_EVENT_POLLIN:
        {
                ret = protocol_client_pollin (this, trans);

                break;
        }
        /* no break for ret check to happen below */
        case GF_EVENT_POLLERR:
        {
                ret = -1;
                protocol_client_cleanup (trans);

                if (conf->connecting == 0) {
                        /* Let the connection/re-connection happen in
                         * background, for now, don't hang here,
                         * tell the parents that i am all ok..
                         */
                        parent = trans->xl->parents;
                        while (parent) {
                                parent->xlator->notify (parent->xlator,
                                                        GF_EVENT_CHILD_CONNECTING,
                                                        trans->xl);
                                parent = parent->next;
                        }
                        conf->connecting = 1;
                }

                was_not_down = 0;
                for (i = 0; i < CHANNEL_MAX; i++) {
                        conn = conf->transport[i]->xl_private;
                        if (conn->connected == 1)
                                was_not_down = 1;
                }

                conn = trans->xl_private;
                if (conn->connected) {
                        conn->connected = 0;
                        if (conn->reconnect == 0)
                                client_protocol_reconnect (trans);
                }

                child_down = 1;
                for (i = 0; i < CHANNEL_MAX; i++) {
                        trans = conf->transport[i];
                        conn = trans->xl_private;
                        if (conn->connected == 1)
                                child_down = 0;
                }

                if (child_down && was_not_down) {
                        gf_log (this->name, GF_LOG_INFO, "disconnected");

                        protocol_client_mark_fd_bad (this);

                        parent = this->parents;
                        while (parent) {
                                xlator_notify (parent->xlator,
                                               GF_EVENT_CHILD_DOWN, this);
                                parent = parent->next;
                        }
                }
        }
        break;

        case GF_EVENT_PARENT_UP:
        {
                client_conf_t *conf = NULL;
                int            i = 0;
                transport_t   *trans = NULL;

                conf = this->private;
                for (i = 0; i < CHANNEL_MAX; i++) {
                        trans = conf->transport[i];
                        if (!trans) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "transport init failed");
                                return -1;
                        }

                        conn = trans->xl_private;

                        gf_log (this->name, GF_LOG_DEBUG,
                                "got GF_EVENT_PARENT_UP, attempting connect "
                                "on transport");

                        client_protocol_reconnect (trans);
                }
        }
        break;

        case GF_EVENT_CHILD_UP:
        {
                char *handshake = NULL;

                ret = dict_get_str (this->options, "disable-handshake",
                                    &handshake);
                gf_log (this->name, GF_LOG_DEBUG,
                        "got GF_EVENT_CHILD_UP");
                if ((ret < 0) ||
                    (strcasecmp (handshake, "on"))) {
                        ret = protocol_client_handshake (this, trans);
                } else {
                        conn = trans->xl_private;
                        conn->connected = 1;
                        ret = default_notify (this, event, trans);
                }

                if (ret)
                        transport_disconnect (trans);

        }
        break;

        default:
                gf_log (this->name, GF_LOG_DEBUG,
                        "got %d, calling default_notify ()", event);

                default_notify (this, event, data);
                break;
        }

        return ret;
}


struct xlator_fops fops = {
        .stat        = client_stat,
        .readlink    = client_readlink,
        .mknod       = client_mknod,
        .mkdir       = client_mkdir,
        .unlink      = client_unlink,
        .rmdir       = client_rmdir,
        .symlink     = client_symlink,
        .rename      = client_rename,
        .link        = client_link,
        .truncate    = client_truncate,
        .open        = client_open,
        .readv       = client_readv,
        .writev      = client_writev,
        .statfs      = client_statfs,
        .flush       = client_flush,
        .fsync       = client_fsync,
        .setxattr    = client_setxattr,
        .getxattr    = client_getxattr,
        .fsetxattr   = client_fsetxattr,
        .fgetxattr   = client_fgetxattr,
        .removexattr = client_removexattr,
        .opendir     = client_opendir,
        .readdir     = client_readdir,
        .readdirp    = client_readdirp,
        .fsyncdir    = client_fsyncdir,
        .access      = client_access,
        .ftruncate   = client_ftruncate,
        .fstat       = client_fstat,
        .create      = client_create,
        .lk          = client_lk,
        .inodelk     = client_inodelk,
        .finodelk    = client_finodelk,
        .entrylk     = client_entrylk,
        .fentrylk    = client_fentrylk,
        .lookup      = client_lookup,
        .setdents    = client_setdents,
        .getdents    = client_getdents,
        .checksum    = client_checksum,
        .rchecksum   = client_rchecksum,
        .xattrop     = client_xattrop,
        .fxattrop    = client_fxattrop,
        .setattr     = client_setattr,
        .fsetattr    = client_fsetattr,
};

struct xlator_mops mops = {
        .stats     = client_stats,
        .getspec   = client_getspec,
        .log       = client_log,
};

struct xlator_cbks cbks = {
        .release    = client_release,
        .releasedir = client_releasedir
};


struct xlator_dumpops dumpops = {
        .priv      =  client_priv_dump,
        .inodectx  =  client_inodectx_dump,
};

struct volume_options options[] = {
        { .key   = {"username"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"password"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"transport-type"},
          .value = {"tcp", "socket", "ib-verbs", "unix", "ib-sdp",
                    "tcp/client", "ib-verbs/client"},
          .type  = GF_OPTION_TYPE_STR
        },
        { .key   = {"remote-host"},
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS
        },
        { .key   = {"remote-subvolume"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"frame-timeout"},
          .type  = GF_OPTION_TYPE_TIME,
          .min   = 5,
          .max   = 1013,
        },
        { .key   = {"ping-timeout"},
          .type  = GF_OPTION_TYPE_TIME,
          .min   = 5,
          .max   = 1013,
        },
        { .key   = {NULL} },
};
