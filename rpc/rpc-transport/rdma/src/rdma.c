/*
  Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
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

#include "dict.h"
#include "glusterfs.h"
#include "logging.h"
#include "rdma.h"
#include "name.h"
#include "byte-order.h"
#include "xlator.h"
#include <signal.h>

#define GF_RDMA_LOG_NAME "rpc-transport/rdma"

static int32_t
__gf_rdma_ioq_churn (gf_rdma_peer_t *peer);

gf_rdma_post_t *
gf_rdma_post_ref (gf_rdma_post_t *post);

int
gf_rdma_post_unref (gf_rdma_post_t *post);

int32_t
gf_resolve_ip6 (const char *hostname,
                uint16_t port,
                int family,
                void **dnscache,
                struct addrinfo **addr_info);

static uint16_t
gf_rdma_get_local_lid (struct ibv_context *context,
                       int32_t port)
{
        struct ibv_port_attr attr;

        if (ibv_query_port (context, port, &attr))
                return 0;

        return attr.lid;
}

static const char *
get_port_state_str(enum ibv_port_state pstate)
{
        switch (pstate) {
        case IBV_PORT_DOWN:          return "PORT_DOWN";
        case IBV_PORT_INIT:          return "PORT_INIT";
        case IBV_PORT_ARMED:         return "PORT_ARMED";
        case IBV_PORT_ACTIVE:        return "PORT_ACTIVE";
        case IBV_PORT_ACTIVE_DEFER:  return "PORT_ACTIVE_DEFER";
        default:                     return "invalid state";
        }
}

static int32_t
ib_check_active_port (struct ibv_context *ctx, uint8_t port)
{
        struct ibv_port_attr  port_attr = {0, };
        int32_t               ret       = 0;
        const char           *state_str = NULL;

        if (!ctx) {
                gf_log_callingfn (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                  "Error in supplied context");
                return -1;
        }

        ret = ibv_query_port (ctx, port, &port_attr);

        if (ret) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                        "Failed to query port %u properties", port);
                return -1;
        }

        state_str = get_port_state_str (port_attr.state);
        gf_log (GF_RDMA_LOG_NAME, GF_LOG_TRACE,
                "Infiniband PORT: (%u) STATE: (%s)",
                port, state_str);

        if (port_attr.state == IBV_PORT_ACTIVE)
                return 0;

        return -1;
}

static int32_t
ib_get_active_port (struct ibv_context *ib_ctx)
{
        struct ibv_device_attr ib_device_attr = {{0, }, };
        int32_t                ret            = -1;
        uint8_t                ib_port        = 0;

        if (!ib_ctx) {
                gf_log_callingfn (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                  "Error in supplied context");
                return -1;
        }
        if (ibv_query_device (ib_ctx, &ib_device_attr)) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                        "Failed to query device properties");
                return -1;
        }

        for (ib_port = 1; ib_port <= ib_device_attr.phys_port_cnt; ++ib_port) {
                ret = ib_check_active_port (ib_ctx, ib_port);
                if (ret == 0)
                        return ib_port;

                gf_log (GF_RDMA_LOG_NAME, GF_LOG_TRACE,
                        "Port:(%u) not active", ib_port);
                continue;
        }
        return ret;
}


static void
gf_rdma_put_post (gf_rdma_queue_t *queue, gf_rdma_post_t *post)
{
        post->ctx.is_request = 0;

        pthread_mutex_lock (&queue->lock);
        {
                if (post->prev) {
                        queue->active_count--;
                        post->prev->next = post->next;
                }

                if (post->next) {
                        post->next->prev = post->prev;
                }

                post->prev = &queue->passive_posts;
                post->next = post->prev->next;
                post->prev->next = post;
                post->next->prev = post;
                queue->passive_count++;
        }
        pthread_mutex_unlock (&queue->lock);
}


static gf_rdma_post_t *
gf_rdma_new_post (gf_rdma_device_t *device, int32_t len,
                  gf_rdma_post_type_t type)
{
        gf_rdma_post_t *post = NULL;
        int             ret  = -1;

        post = (gf_rdma_post_t *) GF_CALLOC (1, sizeof (*post),
                                             gf_common_mt_rdma_post_t);
        if (post == NULL) {
                goto out;
        }

        pthread_mutex_init (&post->lock, NULL);

        post->buf_size = len;

        post->buf = valloc (len);
        if (!post->buf) {
                gf_log_nomem (GF_RDMA_LOG_NAME, GF_LOG_ERROR, len);
                goto out;
        }

        post->mr = ibv_reg_mr (device->pd,
                               post->buf,
                               post->buf_size,
                               IBV_ACCESS_LOCAL_WRITE);
        if (!post->mr) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                        "memory registration failed");
                goto out;
        }

        post->device = device;
        post->type = type;

        ret = 0;
out:
        if (ret != 0) {
                if (post->buf != NULL) {
                        free (post->buf);
                }

                GF_FREE (post);
                post = NULL;
        }

        return post;
}


static gf_rdma_post_t *
gf_rdma_get_post (gf_rdma_queue_t *queue)
{
        gf_rdma_post_t *post = NULL;

        pthread_mutex_lock (&queue->lock);
        {
                post = queue->passive_posts.next;
                if (post == &queue->passive_posts)
                        post = NULL;

                if (post) {
                        if (post->prev)
                                post->prev->next = post->next;
                        if (post->next)
                                post->next->prev = post->prev;
                        post->prev = &queue->active_posts;
                        post->next = post->prev->next;
                        post->prev->next = post;
                        post->next->prev = post;
                        post->reused++;
                        queue->active_count++;
                }
        }
        pthread_mutex_unlock (&queue->lock);

        return post;
}

void
gf_rdma_destroy_post (gf_rdma_post_t *post)
{
        ibv_dereg_mr (post->mr);
        free (post->buf);
        GF_FREE (post);
}


static int32_t
__gf_rdma_quota_get (gf_rdma_peer_t *peer)
{
        int32_t            ret  = -1;
        gf_rdma_private_t *priv = NULL;

        priv = peer->trans->private;

        if (priv->connected && peer->quota > 0) {
                ret = peer->quota--;
        }

        return ret;
}

/*
  static int32_t
  gf_rdma_quota_get (gf_rdma_peer_t *peer)
  {
  int32_t ret = -1;
  gf_rdma_private_t *priv = peer->trans->private;

  pthread_mutex_lock (&priv->write_mutex);
  {
  ret = __gf_rdma_quota_get (peer);
  }
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
  }
*/

static void
__gf_rdma_ioq_entry_free (gf_rdma_ioq_t *entry)
{
        list_del_init (&entry->list);

        if (entry->iobref) {
                iobref_unref (entry->iobref);
                entry->iobref = NULL;
        }

        if (entry->msg.request.rsp_iobref) {
                iobref_unref (entry->msg.request.rsp_iobref);
                entry->msg.request.rsp_iobref = NULL;
        }
        /* TODO: use mem-pool */
        mem_put (entry);
}


static void
__gf_rdma_ioq_flush (gf_rdma_peer_t *peer)
{
        gf_rdma_ioq_t *entry = NULL, *dummy = NULL;

        list_for_each_entry_safe (entry, dummy, &peer->ioq, list) {
                __gf_rdma_ioq_entry_free (entry);
        }
}


static int32_t
__gf_rdma_disconnect (rpc_transport_t *this)
{
        gf_rdma_private_t *priv = NULL;
        int32_t            ret  = 0;

        priv = this->private;

        if (priv->connected || priv->tcp_connected) {
                fcntl (priv->sock, F_SETFL, O_NONBLOCK);
                if (shutdown (priv->sock, SHUT_RDWR) != 0) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                "shutdown () - error: %s",
                                strerror (errno));
                        ret = -errno;
                        priv->tcp_connected = 0;
                        priv->connected = 0;
                }
        }

        return ret;
}


static int32_t
gf_rdma_post_send (struct ibv_qp *qp, gf_rdma_post_t *post, int32_t len)
{
        struct ibv_sge list = {
                .addr = (unsigned long) post->buf,
                .length = len,
                .lkey = post->mr->lkey
        };

        struct ibv_send_wr wr = {
                .wr_id      = (unsigned long) post,
                .sg_list    = &list,
                .num_sge    = 1,
                .opcode     = IBV_WR_SEND,
                .send_flags = IBV_SEND_SIGNALED,
        }, *bad_wr;

        if (!qp)
                return EINVAL;

        return ibv_post_send (qp, &wr, &bad_wr);
}

int
__gf_rdma_encode_error(gf_rdma_peer_t *peer, gf_rdma_reply_info_t *reply_info,
                       struct iovec *rpchdr, uint32_t *ptr,
                       gf_rdma_errcode_t err)
{
        uint32_t       *startp  = NULL;
        struct rpc_msg *rpc_msg = NULL;

        startp = ptr;
        if (reply_info != NULL) {
                *ptr++ = hton32(reply_info->rm_xid);
        } else {
                rpc_msg = rpchdr[0].iov_base; /* assume rpchdr contains
                                               * only one vector.
                                               * (which is true)
                                               */
                *ptr++ = rpc_msg->rm_xid;
        }

        *ptr++ = hton32(GF_RDMA_VERSION);
        *ptr++ = hton32(peer->send_count);
        *ptr++ = hton32(GF_RDMA_ERROR);
        *ptr++ = hton32(err);
        if (err == ERR_VERS) {
                *ptr++ = hton32(GF_RDMA_VERSION);
                *ptr++ = hton32(GF_RDMA_VERSION);
        }

        return (int)((unsigned long)ptr - (unsigned long)startp);
}


int32_t
__gf_rdma_send_error (gf_rdma_peer_t *peer, gf_rdma_ioq_t *entry,
                      gf_rdma_post_t *post, gf_rdma_reply_info_t *reply_info,
                      gf_rdma_errcode_t err)
{
        int32_t  ret = -1, len = 0;

        len = __gf_rdma_encode_error (peer, reply_info, entry->rpchdr,
                                      (uint32_t *)post->buf, err);
        if (len == -1) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                        "encode error returned -1");
                goto out;
        }

        gf_rdma_post_ref (post);

        ret = gf_rdma_post_send (peer->qp, post, len);
        if (!ret) {
                ret = len;
        } else {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING,
                        "gf_rdma_post_send (to %s) failed with ret = %d (%s)",
                        peer->trans->peerinfo.identifier, ret,
                        (ret > 0) ? strerror (ret) : "");
                gf_rdma_post_unref (post);
                __gf_rdma_disconnect (peer->trans);
                ret = -1;
        }

out:
        return ret;
}


int32_t
__gf_rdma_create_read_chunks_from_vector (gf_rdma_peer_t *peer,
                                          gf_rdma_read_chunk_t **readch_ptr,
                                          int32_t *pos, struct iovec *vector,
                                          int count,
                                          gf_rdma_request_context_t *request_ctx)
{
        int                   i      = 0;
        gf_rdma_private_t    *priv   = NULL;
        gf_rdma_device_t     *device = NULL;
        struct ibv_mr        *mr     = NULL;
        gf_rdma_read_chunk_t *readch = NULL;
        int32_t               ret    = -1;

        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, peer, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, readch_ptr, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, *readch_ptr, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, request_ctx, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, vector, out);

        priv = peer->trans->private;
        device = priv->device;
        readch = *readch_ptr;

        for (i = 0; i < count; i++) {
                readch->rc_discrim = hton32 (1);
                readch->rc_position = hton32 (*pos);

                mr = ibv_reg_mr (device->pd, vector[i].iov_base,
                                 vector[i].iov_len,
                                 IBV_ACCESS_REMOTE_READ);
                if (!mr) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "memory registration failed");
                        goto out;
                }

                request_ctx->mr[request_ctx->mr_count++] = mr;

                readch->rc_target.rs_handle = hton32 (mr->rkey);
                readch->rc_target.rs_length
                        = hton32 (vector[i].iov_len);
                readch->rc_target.rs_offset
                        = hton64 ((uint64_t)(unsigned long)vector[i].iov_base);

                *pos = *pos + vector[i].iov_len;
                readch++;
        }

        *readch_ptr = readch;

        ret = 0;
out:
        return ret;
}


int32_t
__gf_rdma_create_read_chunks (gf_rdma_peer_t *peer, gf_rdma_ioq_t *entry,
                              gf_rdma_chunktype_t type, uint32_t **ptr,
                              gf_rdma_request_context_t *request_ctx)
{
        int32_t            ret      = -1;
        int                pos      = 0;

        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, peer, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, entry, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, ptr, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, *ptr, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, request_ctx, out);

        request_ctx->iobref = iobref_ref (entry->iobref);

        if (type == gf_rdma_areadch) {
                pos = 0;
                ret = __gf_rdma_create_read_chunks_from_vector (peer,
                                                                (gf_rdma_read_chunk_t **)ptr,
                                                                &pos,
                                                                entry->rpchdr,
                                                                entry->rpchdr_count,
                                                                request_ctx);
                if (ret == -1) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                "cannot create read chunks from vector, "
                                "entry->rpchdr");
                        goto out;
                }

                ret = __gf_rdma_create_read_chunks_from_vector (peer,
                                                                (gf_rdma_read_chunk_t **)ptr,
                                                                &pos,
                                                                entry->proghdr,
                                                                entry->proghdr_count,
                                                                request_ctx);
                if (ret == -1) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                "cannot create read chunks from vector, "
                                "entry->proghdr");
                }

                if (entry->prog_payload_count != 0) {
                        ret = __gf_rdma_create_read_chunks_from_vector (peer,
                                                                        (gf_rdma_read_chunk_t **)ptr,
                                                                        &pos,
                                                                        entry->prog_payload,
                                                                        entry->prog_payload_count,
                                                                        request_ctx);
                        if (ret == -1) {
                                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                        "cannot create read chunks from vector,"
                                        " entry->prog_payload");
                        }
                }
        } else {
                pos = iov_length (entry->rpchdr, entry->rpchdr_count);
                ret = __gf_rdma_create_read_chunks_from_vector (peer,
                                                                (gf_rdma_read_chunk_t **)ptr,
                                                                &pos,
                                                                entry->prog_payload,
                                                                entry->prog_payload_count,
                                                                request_ctx);
                if (ret == -1) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                "cannot create read chunks from vector, "
                                "entry->prog_payload");
                }
        }

        /* terminate read-chunk list*/
        **ptr = 0;
        *ptr = *ptr + 1;
out:
        return ret;
}


int32_t
__gf_rdma_create_write_chunks_from_vector (gf_rdma_peer_t *peer,
                                           gf_rdma_write_chunk_t **writech_ptr,
                                           struct iovec *vector, int count,
                                           gf_rdma_request_context_t *request_ctx)
{
        int                    i       = 0;
        gf_rdma_private_t     *priv    = NULL;
        gf_rdma_device_t      *device  = NULL;
        struct ibv_mr         *mr      = NULL;
        gf_rdma_write_chunk_t *writech = NULL;
        int32_t                ret     = -1;

        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, peer, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, writech_ptr, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, *writech_ptr, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, request_ctx, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, vector, out);

        writech = *writech_ptr;

        priv = peer->trans->private;
        device = priv->device;

        for (i = 0; i < count; i++) {
                mr = ibv_reg_mr (device->pd, vector[i].iov_base,
                                 vector[i].iov_len,
                                 IBV_ACCESS_REMOTE_WRITE
                                 | IBV_ACCESS_LOCAL_WRITE);
                if (!mr) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "memory registration failed");
                        goto out;
                }

                request_ctx->mr[request_ctx->mr_count++] = mr;

                writech->wc_target.rs_handle = hton32 (mr->rkey);
                writech->wc_target.rs_length = hton32 (vector[i].iov_len);
                writech->wc_target.rs_offset
                        = hton64 (((uint64_t)(unsigned long)vector[i].iov_base));

                writech++;
        }

        *writech_ptr = writech;

        ret = 0;
out:
        return ret;
}


int32_t
__gf_rdma_create_write_chunks (gf_rdma_peer_t *peer, gf_rdma_ioq_t *entry,
                               gf_rdma_chunktype_t chunk_type, uint32_t **ptr,
                               gf_rdma_request_context_t *request_ctx)
{
        int32_t                ret    = -1;
        gf_rdma_write_array_t *warray = NULL;

        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, peer, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, ptr, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, *ptr, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, request_ctx, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, entry, out);

        if ((chunk_type == gf_rdma_replych)
            && ((entry->msg.request.rsphdr_count != 1) ||
                (entry->msg.request.rsphdr_vec[0].iov_base == NULL))) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING,
                        (entry->msg.request.rsphdr_count == 1)
                        ? "chunktype specified as reply chunk but the vector "
                        "specifying the buffer to be used for holding reply"
                        " header is not correct" :
                        "chunktype specified as reply chunk, but more than one "
                        "buffer provided for holding reply");
                goto out;
        }

/*
  if ((chunk_type == gf_rdma_writech)
  && ((entry->msg.request.rsphdr_count == 0)
  || (entry->msg.request.rsphdr_vec[0].iov_base == NULL))) {
  gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
  "vector specifying buffer to hold the program's reply "
  "header should also be provided when buffers are "
  "provided for holding the program's payload in reply");
  goto out;
  }
*/

        if (chunk_type == gf_rdma_writech) {
                warray = (gf_rdma_write_array_t *)*ptr;
                warray->wc_discrim = hton32 (1);
                warray->wc_nchunks
                        = hton32 (entry->msg.request.rsp_payload_count);

                *ptr = (uint32_t *)&warray->wc_array[0];

                ret = __gf_rdma_create_write_chunks_from_vector (peer,
                                                                 (gf_rdma_write_chunk_t **)ptr,
                                                                 entry->msg.request.rsp_payload,
                                                                 entry->msg.request.rsp_payload_count,
                                                                 request_ctx);
                if (ret == -1) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                "cannot create write chunks from vector "
                                "entry->rpc_payload");
                        goto out;
                }

                /* terminate write chunklist */
                **ptr = 0;
                *ptr = *ptr + 1;

                /* no reply chunklist */
                **ptr = 0;
                *ptr = *ptr + 1;
        } else {
                /* no write chunklist */
                **ptr = 0;
                *ptr = *ptr + 1;

                warray = (gf_rdma_write_array_t *)*ptr;
                warray->wc_discrim = hton32 (1);
                warray->wc_nchunks = hton32 (entry->msg.request.rsphdr_count);

                *ptr = (uint32_t *)&warray->wc_array[0];

                ret = __gf_rdma_create_write_chunks_from_vector (peer,
                                                                 (gf_rdma_write_chunk_t **)ptr,
                                                                 entry->msg.request.rsphdr_vec,
                                                                 entry->msg.request.rsphdr_count,
                                                                 request_ctx);
                if (ret == -1) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                "cannot create write chunks from vector "
                                "entry->rpchdr");
                        goto out;
                }

                /* terminate reply chunklist */
                **ptr = 0;
                *ptr = *ptr + 1;
        }

out:
        return ret;
}


inline void
__gf_rdma_deregister_mr (struct ibv_mr **mr, int count)
{
        int i = 0;

        if (mr == NULL) {
                goto out;
        }

        for (i = 0; i < count; i++) {
                ibv_dereg_mr (mr[i]);
        }

out:
        return;
}


static int32_t
__gf_rdma_quota_put (gf_rdma_peer_t *peer)
{
        int32_t ret = 0;

        peer->quota++;
        ret = peer->quota;

        if (!list_empty (&peer->ioq)) {
                ret = __gf_rdma_ioq_churn (peer);
        }

        return ret;
}


static int32_t
gf_rdma_quota_put (gf_rdma_peer_t *peer)
{
        int32_t            ret  = 0;
        gf_rdma_private_t *priv = NULL;

        priv = peer->trans->private;
        pthread_mutex_lock (&priv->write_mutex);
        {
                ret = __gf_rdma_quota_put (peer);
        }
        pthread_mutex_unlock (&priv->write_mutex);

        return ret;
}


/* to be called with priv->mutex held */
void
__gf_rdma_request_context_destroy (gf_rdma_request_context_t *context)
{
        gf_rdma_peer_t    *peer = NULL;
        gf_rdma_private_t *priv = NULL;
        int32_t            ret  = 0;

        if (context == NULL) {
                goto out;
        }

        peer = context->peer;

        __gf_rdma_deregister_mr (context->mr, context->mr_count);

        priv = peer->trans->private;

        if (priv->connected) {
                ret = __gf_rdma_quota_put (peer);
                if (ret < 0) {
                        gf_log ("rdma", GF_LOG_DEBUG,
                                "failed to send "
                                "message");
                        mem_put (context);
                        __gf_rdma_disconnect (peer->trans);
                        goto out;
                }
        }

        if (context->iobref != NULL) {
                iobref_unref (context->iobref);
                context->iobref = NULL;
        }

        if (context->rsp_iobref != NULL) {
                iobref_unref (context->rsp_iobref);
                context->rsp_iobref = NULL;
        }

        mem_put (context);

out:
        return;
}


void
gf_rdma_post_context_destroy (gf_rdma_post_context_t *ctx)
{
        if (ctx == NULL) {
                goto out;
        }

        __gf_rdma_deregister_mr (ctx->mr, ctx->mr_count);

        if (ctx->iobref != NULL) {
                iobref_unref (ctx->iobref);
        }

        if (ctx->hdr_iobuf != NULL) {
                iobuf_unref (ctx->hdr_iobuf);
        }

        memset (ctx, 0, sizeof (*ctx));
out:
        return;
}


static int32_t
gf_rdma_post_recv (struct ibv_srq *srq,
                   gf_rdma_post_t *post)
{
        struct ibv_sge list = {
                .addr   = (unsigned long) post->buf,
                .length = post->buf_size,
                .lkey   = post->mr->lkey
        };

        struct ibv_recv_wr wr = {
                .wr_id  = (unsigned long) post,
                .sg_list = &list,
                .num_sge = 1,
        }, *bad_wr;

        gf_rdma_post_ref (post);

        return ibv_post_srq_recv (srq, &wr, &bad_wr);
}


int
gf_rdma_post_unref (gf_rdma_post_t *post)
{
        int refcount = -1;

        if (post == NULL) {
                goto out;
        }

        pthread_mutex_lock (&post->lock);
        {
                refcount = --post->refcount;
        }
        pthread_mutex_unlock (&post->lock);

        if (refcount == 0) {
                gf_rdma_post_context_destroy (&post->ctx);
                if (post->type == GF_RDMA_SEND_POST) {
                        gf_rdma_put_post (&post->device->sendq, post);
                } else {
                        gf_rdma_post_recv (post->device->srq, post);
                }
        }
out:
        return refcount;
}


int
gf_rdma_post_get_refcount (gf_rdma_post_t *post)
{
        int refcount = -1;

        if (post == NULL) {
                goto out;
        }

        pthread_mutex_lock (&post->lock);
        {
                refcount = post->refcount;
        }
        pthread_mutex_unlock (&post->lock);

out:
        return refcount;
}

gf_rdma_post_t *
gf_rdma_post_ref (gf_rdma_post_t *post)
{
        if (post == NULL) {
                goto out;
        }

        pthread_mutex_lock (&post->lock);
        {
                post->refcount++;
        }
        pthread_mutex_unlock (&post->lock);

out:
        return post;
}


int32_t
__gf_rdma_ioq_churn_request (gf_rdma_peer_t *peer, gf_rdma_ioq_t *entry,
                             gf_rdma_post_t *post)
{
        gf_rdma_chunktype_t        rtype               = gf_rdma_noch;
        gf_rdma_chunktype_t        wtype               = gf_rdma_noch;
        uint64_t                   send_size           = 0;
        gf_rdma_header_t          *hdr                 = NULL;
        struct rpc_msg            *rpc_msg             = NULL;
        uint32_t                  *chunkptr            = NULL;
        char                      *buf                 = NULL;
        int32_t                    ret                 = 0;
        gf_rdma_private_t         *priv                = NULL;
        gf_rdma_device_t          *device              = NULL;
        int                        chunk_count         = 0;
        gf_rdma_request_context_t *request_ctx         = NULL;
        uint32_t                   prog_payload_length = 0, len = 0;
        struct rpc_req            *rpc_req             = NULL;

        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, peer, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, entry, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, post, out);

        if ((entry->msg.request.rsphdr_count != 0)
            && (entry->msg.request.rsp_payload_count != 0)) {
                ret = -1;
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING,
                        "both write-chunklist and reply-chunk cannot be "
                        "present");
                goto out;
        }

        post->ctx.is_request = 1;
        priv = peer->trans->private;
        device = priv->device;

        hdr = (gf_rdma_header_t *)post->buf;

        send_size = iov_length (entry->rpchdr, entry->rpchdr_count)
                + iov_length (entry->proghdr, entry->proghdr_count)
                + GLUSTERFS_RDMA_MAX_HEADER_SIZE;

        if (entry->prog_payload_count != 0) {
                prog_payload_length
                        = iov_length (entry->prog_payload,
                                      entry->prog_payload_count);
        }

        if (send_size > GLUSTERFS_RDMA_INLINE_THRESHOLD) {
                rtype = gf_rdma_areadch;
        } else if ((send_size + prog_payload_length)
                   < GLUSTERFS_RDMA_INLINE_THRESHOLD) {
                rtype = gf_rdma_noch;
        } else if (entry->prog_payload_count != 0) {
                rtype = gf_rdma_readch;
        }

        if (entry->msg.request.rsphdr_count != 0) {
                wtype = gf_rdma_replych;
        } else if (entry->msg.request.rsp_payload_count != 0) {
                wtype = gf_rdma_writech;
        }

        if (rtype == gf_rdma_readch) {
                chunk_count += entry->prog_payload_count;
        } else if (rtype == gf_rdma_areadch) {
                chunk_count += entry->rpchdr_count;
                chunk_count += entry->proghdr_count;
        }

        if (wtype == gf_rdma_writech) {
                chunk_count += entry->msg.request.rsp_payload_count;
        } else if (wtype == gf_rdma_replych) {
                chunk_count += entry->msg.request.rsphdr_count;
        }

        if (chunk_count > GF_RDMA_MAX_SEGMENTS) {
                ret = -1;
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING,
                        "chunk count(%d) exceeding maximum allowed RDMA "
                        "segment count(%d)", chunk_count, GF_RDMA_MAX_SEGMENTS);
                goto out;
        }

        if ((wtype != gf_rdma_noch) || (rtype != gf_rdma_noch)) {
                request_ctx = mem_get (device->request_ctx_pool);
                if (request_ctx == NULL) {
                        ret = -1;
                        goto out;
                }

                memset (request_ctx, 0, sizeof (*request_ctx));

                request_ctx->pool = device->request_ctx_pool;
                request_ctx->peer = peer;

                entry->msg.request.rpc_req->conn_private = request_ctx;

                if (entry->msg.request.rsp_iobref != NULL) {
                        request_ctx->rsp_iobref
                                = iobref_ref (entry->msg.request.rsp_iobref);
                }
        }

        rpc_msg = (struct rpc_msg *) entry->rpchdr[0].iov_base;

        hdr->rm_xid    = rpc_msg->rm_xid; /* no need of hton32(rpc_msg->rm_xid),
                                           * since rpc_msg->rm_xid is already
                                           * hton32ed value of actual xid
                                           */
        hdr->rm_vers   = hton32 (GF_RDMA_VERSION);
        hdr->rm_credit = hton32 (peer->send_count);
        if (rtype == gf_rdma_areadch) {
                hdr->rm_type = hton32 (GF_RDMA_NOMSG);
        } else {
                hdr->rm_type   = hton32 (GF_RDMA_MSG);
        }

        chunkptr = &hdr->rm_body.rm_chunks[0];
        if (rtype != gf_rdma_noch) {
                ret = __gf_rdma_create_read_chunks (peer, entry, rtype, &chunkptr,
                                                    request_ctx);
                if (ret != 0) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                "creation of read chunks failed");
                        goto out;
                }
        } else {
                *chunkptr++ = 0; /* no read chunks */
        }

        if (wtype != gf_rdma_noch) {
                ret = __gf_rdma_create_write_chunks (peer, entry, wtype, &chunkptr,
                                                     request_ctx);
                if (ret != 0) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                "creation of write/reply chunk failed");
                        goto out;
                }
        } else {
                *chunkptr++ = 0; /* no write chunks */
                *chunkptr++ = 0; /* no reply chunk */
        }

        buf = (char *)chunkptr;

        if (rtype != gf_rdma_areadch) {
                iov_unload (buf, entry->rpchdr, entry->rpchdr_count);
                buf += iov_length (entry->rpchdr, entry->rpchdr_count);

                iov_unload (buf, entry->proghdr, entry->proghdr_count);
                buf += iov_length (entry->proghdr, entry->proghdr_count);

                if (rtype != gf_rdma_readch) {
                        iov_unload (buf, entry->prog_payload,
                                    entry->prog_payload_count);
                        buf += iov_length (entry->prog_payload,
                                           entry->prog_payload_count);
                }
        }

        len = buf - post->buf;

        gf_rdma_post_ref (post);

        ret = gf_rdma_post_send (peer->qp, post, len);
        if (!ret) {
                ret = len;
        } else {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING,
                        "gf_rdma_post_send (to %s) failed with ret = %d (%s)",
                        peer->trans->peerinfo.identifier, ret,
                        (ret > 0) ? strerror (ret) : "");
                gf_rdma_post_unref (post);
                __gf_rdma_disconnect (peer->trans);
                ret = -1;
        }

out:
        if (ret == -1) {
                rpc_req = entry->msg.request.rpc_req;

                if (request_ctx != NULL) {
                        __gf_rdma_request_context_destroy (rpc_req->conn_private);
                }

                rpc_req->conn_private = NULL;
        }

        return ret;
}


inline void
__gf_rdma_fill_reply_header (gf_rdma_header_t *header, struct iovec *rpchdr,
                             gf_rdma_reply_info_t *reply_info, int credits)
{
        struct rpc_msg *rpc_msg = NULL;

        if (reply_info != NULL) {
                header->rm_xid = hton32 (reply_info->rm_xid);
        } else {
                rpc_msg = rpchdr[0].iov_base; /* assume rpchdr contains
                                               * only one vector.
                                               * (which is true)
                                               */
                header->rm_xid = rpc_msg->rm_xid;
        }

        header->rm_type = hton32 (GF_RDMA_MSG);
        header->rm_vers = hton32 (GF_RDMA_VERSION);
        header->rm_credit = hton32 (credits);

        header->rm_body.rm_chunks[0] = 0; /* no read chunks */
        header->rm_body.rm_chunks[1] = 0; /* no write chunks */
        header->rm_body.rm_chunks[2] = 0; /* no reply chunks */

        return;
}


int32_t
__gf_rdma_send_reply_inline (gf_rdma_peer_t *peer, gf_rdma_ioq_t *entry,
                             gf_rdma_post_t *post, gf_rdma_reply_info_t *reply_info)
{
        gf_rdma_header_t  *header    = NULL;
        int32_t         send_size = 0, ret = 0;
        char           *buf       = NULL;

        send_size = iov_length (entry->rpchdr, entry->rpchdr_count)
                + iov_length (entry->proghdr, entry->proghdr_count)
                + iov_length (entry->prog_payload, entry->prog_payload_count)
                + sizeof (gf_rdma_header_t); /*
                                              * remember, no chunklists in the
                                              * reply
                                              */

        if (send_size > GLUSTERFS_RDMA_INLINE_THRESHOLD) {
                ret = __gf_rdma_send_error (peer, entry, post, reply_info,
                                            ERR_CHUNK);
                goto out;
        }

        header = (gf_rdma_header_t *)post->buf;

        __gf_rdma_fill_reply_header (header, entry->rpchdr, reply_info,
                                     peer->send_count);

        buf = (char *)&header->rm_body.rm_chunks[3];

        if (entry->rpchdr_count != 0) {
                iov_unload (buf, entry->rpchdr, entry->rpchdr_count);
                buf += iov_length (entry->rpchdr, entry->rpchdr_count);
        }

        if (entry->proghdr_count != 0) {
                iov_unload (buf, entry->proghdr, entry->proghdr_count);
                buf += iov_length (entry->proghdr, entry->proghdr_count);
        }

        if (entry->prog_payload_count != 0) {
                iov_unload (buf, entry->prog_payload,
                            entry->prog_payload_count);
                buf += iov_length (entry->prog_payload,
                                   entry->prog_payload_count);
        }

        gf_rdma_post_ref (post);

        ret = gf_rdma_post_send (peer->qp, post, (buf - post->buf));
        if (!ret) {
                ret = send_size;
        } else {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING,
                        "gf_rdma_post_send (to %s) failed with ret = %d (%s)",
                        peer->trans->peerinfo.identifier, ret,
                        (ret > 0) ? strerror (ret) : "");
                gf_rdma_post_unref (post);
                __gf_rdma_disconnect (peer->trans);
                ret = -1;
        }

out:
        return ret;
}


int32_t
__gf_rdma_reply_encode_write_chunks (gf_rdma_peer_t *peer,
                                     uint32_t payload_size,
                                     gf_rdma_post_t *post,
                                     gf_rdma_reply_info_t *reply_info,
                                     uint32_t **ptr)
{
        uint32_t               chunk_size   = 0;
        int32_t                ret          = -1;
        gf_rdma_write_array_t *target_array = NULL;
        int                    i            = 0;

        target_array = (gf_rdma_write_array_t *)*ptr;

        for (i = 0; i < reply_info->wc_array->wc_nchunks; i++) {
                chunk_size +=
                        reply_info->wc_array->wc_array[i].wc_target.rs_length;
        }

        if (chunk_size < payload_size) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                        "length of payload (%d) is exceeding the total "
                        "write chunk length (%d)", payload_size, chunk_size);
                goto out;
        }

        target_array->wc_discrim = hton32 (1);
        for (i = 0; (i < reply_info->wc_array->wc_nchunks)
                     && (payload_size != 0);
             i++) {
                target_array->wc_array[i].wc_target.rs_offset
                        = hton64 (reply_info->wc_array->wc_array[i].wc_target.rs_offset);

                target_array->wc_array[i].wc_target.rs_length
                        = hton32 (min (payload_size,
                                       reply_info->wc_array->wc_array[i].wc_target.rs_length));
        }

        target_array->wc_nchunks = hton32 (i);
        target_array->wc_array[i].wc_target.rs_handle = 0; /* terminate
                                                              chunklist */

        ret = 0;

        *ptr = &target_array->wc_array[i].wc_target.rs_length;
out:
        return ret;
}


inline int32_t
__gf_rdma_register_local_mr_for_rdma (gf_rdma_peer_t *peer,
                                      struct iovec *vector, int count,
                                      gf_rdma_post_context_t *ctx)
{
        int                i      = 0;
        int32_t            ret    = -1;
        gf_rdma_private_t *priv   = NULL;
        gf_rdma_device_t  *device = NULL;

        if ((ctx == NULL) || (vector == NULL)) {
                goto out;
        }

        priv = peer->trans->private;
        device = priv->device;

        for (i = 0; i < count; i++) {
                /* what if the memory is registered more than once?
                 * Assume that a single write buffer is passed to afr, which
                 * then passes it to its children. If more than one children
                 * happen to use rdma, then the buffer is registered more than
                 * once.
                 * Ib-verbs specification says that multiple registrations of
                 * same memory location is allowed. Refer to 10.6.3.8 of
                 * Infiniband Architecture Specification Volume 1
                 * (Release 1.2.1)
                 */
                ctx->mr[ctx->mr_count] = ibv_reg_mr (device->pd,
                                                     vector[i].iov_base,
                                                     vector[i].iov_len,
                                                     IBV_ACCESS_LOCAL_WRITE);
                if (ctx->mr[ctx->mr_count] == NULL) {
                        goto out;
                }

                ctx->mr_count++;
        }

        ret = 0;
out:
        return ret;
}

/* 1. assumes xfer_len of data is pointed by vector(s) starting from vec[*idx]
 * 2. modifies vec
 */
int32_t
__gf_rdma_write (gf_rdma_peer_t *peer, gf_rdma_post_t *post, struct iovec *vec,
                 uint32_t xfer_len, int *idx, gf_rdma_write_chunk_t *writech)
{
        int             size    = 0, num_sge = 0, i = 0;
        int32_t         ret     = -1;
        struct ibv_sge *sg_list = NULL;
        struct ibv_send_wr wr   = {
                .opcode         = IBV_WR_RDMA_WRITE,
                .send_flags     = IBV_SEND_SIGNALED,
        }, *bad_wr;

        if ((peer == NULL) || (writech == NULL) || (idx == NULL)
            || (post == NULL) || (vec == NULL) || (xfer_len == 0)) {
                goto out;
        }

        for (i = *idx; size < xfer_len; i++) {
                size += vec[i].iov_len;
        }

        num_sge = i - *idx;

        sg_list = GF_CALLOC (num_sge, sizeof (struct ibv_sge),
                             gf_common_mt_sge);
        if (sg_list == NULL) {
                ret = -1;
                goto out;
        }

        for ((i = *idx), (num_sge = 0); (xfer_len != 0); i++, num_sge++) {
                size = min (xfer_len, vec[i].iov_len);

                sg_list [num_sge].addr = (unsigned long)vec[i].iov_base;
                sg_list [num_sge].length = size;
                sg_list [num_sge].lkey = post->ctx.mr[i]->lkey;

                xfer_len -= size;
        }

        *idx = i;

        if (size < vec[i - 1].iov_len) {
                vec[i - 1].iov_base += size;
                vec[i - 1].iov_len -= size;
                *idx = i - 1;
        }

        wr.sg_list = sg_list;
        wr.num_sge = num_sge;
        wr.wr_id = (unsigned long) gf_rdma_post_ref (post);
        wr.wr.rdma.rkey = writech->wc_target.rs_handle;
        wr.wr.rdma.remote_addr = writech->wc_target.rs_offset;

        ret = ibv_post_send(peer->qp, &wr, &bad_wr);
        if (ret) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING, "rdma write to "
                        "client (%s) failed with ret = %d (%s)",
                        peer->trans->peerinfo.identifier, ret,
                        (ret > 0) ? strerror (ret) : "");
                ret = -1;
        }

        GF_FREE (sg_list);
out:
        return ret;
}


int32_t
__gf_rdma_do_gf_rdma_write (gf_rdma_peer_t *peer, gf_rdma_post_t *post,
                            struct iovec *vector, int count,
                            struct iobref *iobref,
                            gf_rdma_reply_info_t *reply_info)
{
        int      i            = 0, payload_idx = 0;
        uint32_t payload_size = 0, xfer_len = 0;
        int32_t  ret          = -1;

        if (count != 0) {
                payload_size = iov_length (vector, count);
        }

        if (payload_size == 0) {
                ret = 0;
                goto out;
        }

        ret = __gf_rdma_register_local_mr_for_rdma (peer, vector, count,
                                                    &post->ctx);
        if (ret == -1) {
                goto out;
        }

        post->ctx.iobref = iobref_ref (iobref);

        for (i = 0; (i < reply_info->wc_array->wc_nchunks)
                     && (payload_size != 0);
             i++) {
                xfer_len = min (payload_size,
                                reply_info->wc_array->wc_array[i].wc_target.rs_length);

                ret = __gf_rdma_write (peer, post, vector, xfer_len, &payload_idx,
                                       &reply_info->wc_array->wc_array[i]);
                if (ret == -1) {
                        goto out;
                }

                payload_size -= xfer_len;
        }

        ret = 0;
out:

        return ret;
}


int32_t
__gf_rdma_send_reply_type_nomsg (gf_rdma_peer_t *peer, gf_rdma_ioq_t *entry,
                                 gf_rdma_post_t *post,
                                 gf_rdma_reply_info_t *reply_info)
{
        gf_rdma_header_t      *header       = NULL;
        char               *buf          = NULL;
        uint32_t            payload_size = 0;
        int                 count        = 0, i = 0;
        int32_t             ret          = 0;
        struct iovec        vector[MAX_IOVEC];

        header = (gf_rdma_header_t *)post->buf;

        __gf_rdma_fill_reply_header (header, entry->rpchdr, reply_info,
                                     peer->send_count);

        header->rm_type = hton32 (GF_RDMA_NOMSG);

        payload_size = iov_length (entry->rpchdr, entry->rpchdr_count) +
                iov_length (entry->proghdr, entry->proghdr_count);

        /* encode reply chunklist */
        buf = (char *)&header->rm_body.rm_chunks[2];
        ret = __gf_rdma_reply_encode_write_chunks (peer, payload_size, post,
                                                   reply_info, (uint32_t **)&buf);
        if (ret == -1) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                        "encoding write chunks failed");
                ret = __gf_rdma_send_error (peer, entry, post, reply_info,
                                            ERR_CHUNK);
                goto out;
        }

        gf_rdma_post_ref (post);

        for (i = 0; i < entry->rpchdr_count; i++) {
                vector[count++] = entry->rpchdr[i];
        }

        for (i = 0; i < entry->proghdr_count; i++) {
                vector[count++] = entry->proghdr[i];
        }

        ret = __gf_rdma_do_gf_rdma_write (peer, post, vector, count,
                                          entry->iobref, reply_info);
        if (ret == -1) {
                gf_rdma_post_unref (post);
                goto out;
        }

        ret = gf_rdma_post_send (peer->qp, post, (buf - post->buf));
        if (ret) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING,
                        "gf_rdma_post_send to client (%s) failed with "
                        "ret = %d (%s)", peer->trans->peerinfo.identifier, ret,
                        (ret > 0) ? strerror (ret) : "");
                ret = -1;
                gf_rdma_post_unref (post);
        } else {
                ret = payload_size;
        }

out:
        return ret;
}


int32_t
__gf_rdma_send_reply_type_msg (gf_rdma_peer_t *peer, gf_rdma_ioq_t *entry,
                               gf_rdma_post_t *post, gf_rdma_reply_info_t *reply_info)
{
        gf_rdma_header_t      *header       = NULL;
        int32_t             send_size    = 0, ret = 0;
        char               *ptr          = NULL;
        uint32_t            payload_size = 0;

        send_size = iov_length (entry->rpchdr, entry->rpchdr_count)
                + iov_length (entry->proghdr, entry->proghdr_count)
                + GLUSTERFS_RDMA_MAX_HEADER_SIZE;

        if (send_size > GLUSTERFS_RDMA_INLINE_THRESHOLD) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING,
                        "client has provided only write chunks, but the "
                        "combined size of rpc and program header (%d) is "
                        "exceeding the size of msg that can be sent using "
                        "RDMA send (%d)", send_size,
                        GLUSTERFS_RDMA_INLINE_THRESHOLD);

                ret = __gf_rdma_send_error (peer, entry, post, reply_info,
                                            ERR_CHUNK);
                goto out;
        }

        header = (gf_rdma_header_t *)post->buf;

        __gf_rdma_fill_reply_header (header, entry->rpchdr, reply_info,
                                     peer->send_count);

        payload_size = iov_length (entry->prog_payload,
                                   entry->prog_payload_count);
        ptr = (char *)&header->rm_body.rm_chunks[1];

        ret = __gf_rdma_reply_encode_write_chunks (peer, payload_size, post,
                                                   reply_info,
                                                   (uint32_t **)&ptr);
        if (ret == -1) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                        "encoding write chunks failed");
                ret = __gf_rdma_send_error (peer, entry, post, reply_info,
                                            ERR_CHUNK);
                goto out;
        }

        *(uint32_t *)ptr = 0;          /* terminate reply chunklist */
        ptr += sizeof (uint32_t);

        gf_rdma_post_ref (post);

        ret = __gf_rdma_do_gf_rdma_write (peer, post, entry->prog_payload,
                                          entry->prog_payload_count,
                                          entry->iobref, reply_info);
        if (ret == -1) {
                gf_rdma_post_unref (post);
                goto out;
        }

        iov_unload (ptr, entry->rpchdr, entry->rpchdr_count);
        ptr += iov_length (entry->rpchdr, entry->rpchdr_count);

        iov_unload (ptr, entry->proghdr, entry->proghdr_count);
        ptr += iov_length (entry->proghdr, entry->proghdr_count);

        ret = gf_rdma_post_send (peer->qp, post, (ptr - post->buf));
        if (ret) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING,
                        "rdma send to client (%s) failed with ret = %d (%s)",
                        peer->trans->peerinfo.identifier, ret,
                        (ret > 0) ? strerror (ret) : "");
                gf_rdma_post_unref (post);
                ret = -1;
        } else {
                ret = send_size + payload_size;
        }

out:
        return ret;
}


void
gf_rdma_reply_info_destroy (gf_rdma_reply_info_t *reply_info)
{
        if (reply_info == NULL) {
                goto out;
        }

        if (reply_info->wc_array != NULL) {
                GF_FREE (reply_info->wc_array);
                reply_info->wc_array = NULL;
        }

        mem_put (reply_info);
out:
        return;
}


gf_rdma_reply_info_t *
gf_rdma_reply_info_alloc (gf_rdma_peer_t *peer)
{
        gf_rdma_reply_info_t *reply_info = NULL;
        gf_rdma_private_t    *priv       = NULL;

        priv = peer->trans->private;

        reply_info = mem_get (priv->device->reply_info_pool);
        if (reply_info == NULL) {
                goto out;
        }

        memset (reply_info, 0, sizeof (*reply_info));
        reply_info->pool = priv->device->reply_info_pool;

out:
        return reply_info;
}


int32_t
__gf_rdma_ioq_churn_reply (gf_rdma_peer_t *peer, gf_rdma_ioq_t *entry,
                           gf_rdma_post_t *post)
{
        gf_rdma_reply_info_t *reply_info = NULL;
        int32_t               ret        = -1;
        gf_rdma_chunktype_t   type       = gf_rdma_noch;

        if ((peer == NULL) || (entry == NULL) || (post == NULL)) {
                goto out;
        }

        reply_info = entry->msg.reply_info;
        if (reply_info != NULL) {
                type = reply_info->type;
        }

        switch (type) {
        case gf_rdma_noch:
                ret = __gf_rdma_send_reply_inline (peer, entry, post,
                                                   reply_info);
                break;

        case gf_rdma_replych:
                ret = __gf_rdma_send_reply_type_nomsg (peer, entry, post,
                                                       reply_info);
                break;

        case gf_rdma_writech:
                ret = __gf_rdma_send_reply_type_msg (peer, entry, post,
                                                     reply_info);
                break;

        default:
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING,
                        "invalid chunktype (%d) specified for sending reply",
                        type);
                break;
        }

        if (reply_info != NULL) {
                gf_rdma_reply_info_destroy (reply_info);
        }
out:
        return ret;
}


int32_t
__gf_rdma_ioq_churn_entry (gf_rdma_peer_t *peer, gf_rdma_ioq_t *entry)
{
        int32_t            ret     = 0, quota = 0;
        gf_rdma_private_t *priv    = NULL;
        gf_rdma_device_t  *device  = NULL;
        gf_rdma_options_t *options = NULL;
        gf_rdma_post_t    *post    = NULL;

        priv = peer->trans->private;
        options = &priv->options;
        device = priv->device;

        quota = __gf_rdma_quota_get (peer);
        if (quota > 0) {
                post = gf_rdma_get_post (&device->sendq);
                if (post == NULL) {
                        post = gf_rdma_new_post (device,
                                                 (options->send_size + 2048),
                                                 GF_RDMA_SEND_POST);
                }

                if (post == NULL) {
                        ret = -1;
                        goto out;
                }

                if (entry->is_request) {
                        ret = __gf_rdma_ioq_churn_request (peer, entry, post);
                } else {
                        ret = __gf_rdma_ioq_churn_reply (peer, entry, post);
                }

                if (ret != 0) {
                        __gf_rdma_ioq_entry_free (entry);
                }
        } else {
                ret = 0;
        }

out:
        return ret;
}


static int32_t
__gf_rdma_ioq_churn (gf_rdma_peer_t *peer)
{
        gf_rdma_ioq_t *entry = NULL;
        int32_t        ret   = 0;

        while (!list_empty (&peer->ioq))
        {
                /* pick next entry */
                entry = peer->ioq_next;

                ret = __gf_rdma_ioq_churn_entry (peer, entry);

                if (ret <= 0)
                        break;
        }

        /*
          list_for_each_entry_safe (entry, dummy, &peer->ioq, list) {
          ret = __gf_rdma_ioq_churn_entry (peer, entry);
          if (ret <= 0) {
          break;
          }
          }
        */

        return ret;
}


static int32_t
gf_rdma_writev (rpc_transport_t *this, gf_rdma_ioq_t *entry)
{
        int32_t            ret  = 0, need_append = 1;
        gf_rdma_private_t *priv = NULL;
        gf_rdma_peer_t    *peer = NULL;

        priv = this->private;
        pthread_mutex_lock (&priv->write_mutex);
        {
                if (!priv->connected) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "rdma is not connected to post a "
                                "send request");
                        ret = -1;
                        goto unlock;
                }

                peer = &priv->peer;
                if (list_empty (&peer->ioq)) {
                        ret = __gf_rdma_ioq_churn_entry (peer, entry);
                        if (ret != 0) {
                                need_append = 0;
                        }
                }

                if (need_append) {
                        list_add_tail (&entry->list, &peer->ioq);
                }
        }
unlock:
        pthread_mutex_unlock (&priv->write_mutex);
        return ret;
}


gf_rdma_ioq_t *
gf_rdma_ioq_new (rpc_transport_t *this, rpc_transport_data_t *data)
{
        gf_rdma_ioq_t       *entry = NULL;
        int                  count = 0, i = 0;
        rpc_transport_msg_t *msg   = NULL;
        gf_rdma_private_t   *priv  = NULL;

        if ((data == NULL) || (this == NULL)) {
                goto out;
        }

        priv = this->private;
        /* TODO: use mem-pool */
        entry = mem_get (priv->device->ioq_pool);
        if (entry == NULL) {
                goto out;
        }
        memset (entry, 0, sizeof (*entry));
        entry->pool = priv->device->ioq_pool;

        if (data->is_request) {
                msg = &data->data.req.msg;
                if (data->data.req.rsp.rsphdr_count != 0) {
                        for (i = 0; i < data->data.req.rsp.rsphdr_count; i++) {
                                entry->msg.request.rsphdr_vec[i]
                                        = data->data.req.rsp.rsphdr[i];
                        }

                        entry->msg.request.rsphdr_count =
                                data->data.req.rsp.rsphdr_count;
                }

                if (data->data.req.rsp.rsp_payload_count != 0) {
                        for (i = 0; i < data->data.req.rsp.rsp_payload_count;
                             i++) {
                                entry->msg.request.rsp_payload[i]
                                        = data->data.req.rsp.rsp_payload[i];
                        }

                        entry->msg.request.rsp_payload_count =
                                data->data.req.rsp.rsp_payload_count;
                }

                entry->msg.request.rpc_req = data->data.req.rpc_req;

                if (data->data.req.rsp.rsp_iobref != NULL) {
                        entry->msg.request.rsp_iobref
                                = iobref_ref (data->data.req.rsp.rsp_iobref);
                }
        } else {
                msg = &data->data.reply.msg;
                entry->msg.reply_info = data->data.reply.private;
        }

        entry->is_request = data->is_request;

        count = msg->rpchdrcount + msg->proghdrcount + msg->progpayloadcount;

        GF_ASSERT (count <= MAX_IOVEC);

        if (msg->rpchdr != NULL) {
                memcpy (&entry->rpchdr[0], msg->rpchdr,
                        sizeof (struct iovec) * msg->rpchdrcount);
                entry->rpchdr_count = msg->rpchdrcount;
        }

        if (msg->proghdr != NULL) {
                memcpy (&entry->proghdr[0], msg->proghdr,
                        sizeof (struct iovec) * msg->proghdrcount);
                entry->proghdr_count = msg->proghdrcount;
        }

        if (msg->progpayload != NULL) {
                memcpy (&entry->prog_payload[0], msg->progpayload,
                        sizeof (struct iovec) * msg->progpayloadcount);
                entry->prog_payload_count = msg->progpayloadcount;
        }

        if (msg->iobref != NULL) {
                entry->iobref = iobref_ref (msg->iobref);
        }

        INIT_LIST_HEAD (&entry->list);

out:
        return entry;
}


int32_t
gf_rdma_submit_request (rpc_transport_t *this, rpc_transport_req_t *req)
{
        int32_t               ret   = 0;
        gf_rdma_ioq_t        *entry = NULL;
        rpc_transport_data_t  data  = {0, };

        if (req == NULL) {
                goto out;
        }

        data.is_request = 1;
        data.data.req = *req;

        entry = gf_rdma_ioq_new (this, &data);
        if (entry == NULL) {
                goto out;
        }

        ret = gf_rdma_writev (this, entry);

        if (ret > 0) {
                ret = 0;
        } else if (ret < 0) {
                rpc_transport_disconnect (this);
        }

out:
        return ret;
}

int32_t
gf_rdma_submit_reply (rpc_transport_t *this, rpc_transport_reply_t *reply)
{
        int32_t               ret   = 0;
        gf_rdma_ioq_t        *entry = NULL;
        rpc_transport_data_t  data  = {0, };

        if (reply == NULL) {
                goto out;
        }

        data.data.reply = *reply;

        entry = gf_rdma_ioq_new (this, &data);
        if (entry == NULL) {
                goto out;
        }

        ret = gf_rdma_writev (this, entry);
        if (ret > 0) {
                ret = 0;
        } else if (ret < 0) {
                rpc_transport_disconnect (this);
        }

out:
        return ret;
}

#if 0
static int
gf_rdma_receive (rpc_transport_t *this, char **hdr_p, size_t *hdrlen_p,
                 struct iobuf **iobuf_p)
{
        gf_rdma_private_t *priv                   = this->private;
        /* TODO: return error if !priv->connected, check with locks */
        /* TODO: boundry checks for data_ptr/offset */
        char              *copy_from              = NULL;
        gf_rdma_header_t  *header                 = NULL;
        uint32_t           size1, size2, data_len = 0;
        char              *hdr                    = NULL;
        struct iobuf      *iobuf                  = NULL;
        int32_t            ret                    = 0;

        pthread_mutex_lock (&priv->recv_mutex);
        {
/*
  while (!priv->data_ptr)
  pthread_cond_wait (&priv->recv_cond, &priv->recv_mutex);
*/

                copy_from = priv->data_ptr + priv->data_offset;

                priv->data_ptr = NULL;
                data_len = priv->data_len;
                pthread_cond_broadcast (&priv->recv_cond);
        }
        pthread_mutex_unlock (&priv->recv_mutex);

        header = (gf_rdma_header_t *)copy_from;
        if (strcmp (header->colonO, ":O")) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                        "%s: corrupt header received", this->name);
                ret = -1;
                goto err;
        }

        size1 = ntoh32 (header->size1);
        size2 = ntoh32 (header->size2);

        if (data_len != (size1 + size2 + sizeof (*header))) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                        "%s: sizeof data read from transport is not equal "
                        "to the size specified in the header",
                        this->name);
                ret = -1;
                goto err;
        }

        copy_from += sizeof (*header);

        if (size1) {
                hdr = GF_CALLOC (1, size1, gf_common_mt_char);
                if (!hdr) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "unable to allocate header for peer %s",
                                this->peerinfo.identifier);
                        ret = -ENOMEM;
                        goto err;
                }
                memcpy (hdr, copy_from, size1);
                copy_from += size1;
                *hdr_p = hdr;
        }
        *hdrlen_p = size1;

        if (size2) {
                iobuf = iobuf_get (this->ctx->iobuf_pool);
                if (!iobuf) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "unable to allocate IO buffer for peer %s",
                                this->peerinfo.identifier);
                        ret = -ENOMEM;
                        goto err;
                }
                memcpy (iobuf->ptr, copy_from, size2);
                *iobuf_p = iobuf;
        }

err:
        return ret;
}
#endif


static void
gf_rdma_destroy_cq (rpc_transport_t *this)
{
        gf_rdma_private_t *priv   = NULL;
        gf_rdma_device_t  *device = NULL;

        priv = this->private;
        device = priv->device;

        if (device->recv_cq)
                ibv_destroy_cq (device->recv_cq);
        device->recv_cq = NULL;

        if (device->send_cq)
                ibv_destroy_cq (device->send_cq);
        device->send_cq = NULL;

        return;
}


static int32_t
gf_rdma_create_cq (rpc_transport_t *this)
{
        gf_rdma_private_t      *priv        = NULL;
        gf_rdma_options_t      *options     = NULL;
        gf_rdma_device_t       *device      = NULL;
        uint64_t                send_cqe    = 0;
        int32_t                 ret         = 0;
        struct ibv_device_attr  device_attr = {{0}, };

        priv = this->private;
        options = &priv->options;
        device = priv->device;

        device->recv_cq = ibv_create_cq (priv->device->context,
                                         options->recv_count * 2,
                                         device,
                                         device->recv_chan,
                                         0);
        if (!device->recv_cq) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                        "%s: creation of CQ for device %s failed",
                        this->name, device->device_name);
                ret = -1;
                goto out;
        } else if (ibv_req_notify_cq (device->recv_cq, 0)) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                        "%s: ibv_req_notify_cq on recv CQ of device %s failed",
                        this->name, device->device_name);
                ret = -1;
                goto out;
        }

        do {
                ret = ibv_query_device (priv->device->context, &device_attr);
                if (ret != 0) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "%s: ibv_query_device on %s returned %d (%s)",
                                this->name, priv->device->device_name, ret,
                                (ret > 0) ? strerror (ret) : "");
                        ret = -1;
                        goto out;
                }

                send_cqe = options->send_count * 128;
                send_cqe = (send_cqe > device_attr.max_cqe)
                        ? device_attr.max_cqe : send_cqe;

                /* TODO: make send_cq size dynamically adaptive */
                device->send_cq = ibv_create_cq (priv->device->context,
                                                 send_cqe, device,
                                                 device->send_chan, 0);
                if (!device->send_cq) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "%s: creation of send_cq for device %s failed",
                                this->name, device->device_name);
                        ret = -1;
                        goto out;
                }

                if (ibv_req_notify_cq (device->send_cq, 0)) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "%s: ibv_req_notify_cq on send_cq for device %s"
                                " failed", this->name, device->device_name);
                        ret = -1;
                        goto out;
                }
        } while (0);

out:
        if (ret != 0)
                gf_rdma_destroy_cq (this);

        return ret;
}


static int
gf_rdma_register_peer (gf_rdma_device_t *device, int32_t qp_num,
                       gf_rdma_peer_t *peer)
{
        struct _qpent   *ent   = NULL;
        gf_rdma_qpreg_t *qpreg = NULL;
        int32_t          hash  = 0;
        int              ret   = -1;

        qpreg = &device->qpreg;
        hash = qp_num % 42;

        pthread_mutex_lock (&qpreg->lock);
        {
                ent = qpreg->ents[hash].next;
                while ((ent != &qpreg->ents[hash]) && (ent->qp_num != qp_num)) {
                        ent = ent->next;
                }

                if (ent->qp_num == qp_num) {
                        ret = 0;
                        goto unlock;
                }

                ent = (struct _qpent *) GF_CALLOC (1, sizeof (*ent),
                                                   gf_common_mt_qpent);
                if (ent == NULL) {
                        goto unlock;
                }

                /* TODO: ref reg->peer */
                ent->peer = peer;
                ent->next = &qpreg->ents[hash];
                ent->prev = ent->next->prev;
                ent->next->prev = ent;
                ent->prev->next = ent;
                ent->qp_num = qp_num;
                qpreg->count++;
                ret = 0;
        }
unlock:
        pthread_mutex_unlock (&qpreg->lock);

        return ret;
}


static void
gf_rdma_unregister_peer (gf_rdma_device_t *device, int32_t qp_num)
{
        struct _qpent   *ent   = NULL;
        gf_rdma_qpreg_t *qpreg = NULL;
        int32_t          hash  = 0;

        qpreg = &device->qpreg;
        hash = qp_num % 42;

        pthread_mutex_lock (&qpreg->lock);
        {
                ent = qpreg->ents[hash].next;
                while ((ent != &qpreg->ents[hash]) && (ent->qp_num != qp_num))
                        ent = ent->next;
                if (ent->qp_num != qp_num) {
                        pthread_mutex_unlock (&qpreg->lock);
                        return;
                }
                ent->prev->next = ent->next;
                ent->next->prev = ent->prev;
                /* TODO: unref reg->peer */
                GF_FREE (ent);
                qpreg->count--;
        }
        pthread_mutex_unlock (&qpreg->lock);
}


static gf_rdma_peer_t *
__gf_rdma_lookup_peer (gf_rdma_device_t *device, int32_t qp_num)
{
        struct _qpent   *ent   = NULL;
        gf_rdma_peer_t  *peer  = NULL;
        gf_rdma_qpreg_t *qpreg = NULL;
        int32_t          hash  = 0;

        qpreg = &device->qpreg;
        hash = qp_num % 42;
        ent = qpreg->ents[hash].next;
        while ((ent != &qpreg->ents[hash]) && (ent->qp_num != qp_num))
                ent = ent->next;

        if (ent != &qpreg->ents[hash]) {
                peer = ent->peer;
        }

        return peer;
}

/*
  static gf_rdma_peer_t *
  gf_rdma_lookup_peer (gf_rdma_device_t *device,
  int32_t qp_num)
  {
  gf_rdma_qpreg_t *qpreg = NULL;
  gf_rdma_peer_t  *peer  = NULL;

  qpreg = &device->qpreg;
  pthread_mutex_lock (&qpreg->lock);
  {
  peer = __gf_rdma_lookup_peer (device, qp_num);
  }
  pthread_mutex_unlock (&qpreg->lock);

  return peer;
  }
*/


static void
__gf_rdma_destroy_qp (rpc_transport_t *this)
{
        gf_rdma_private_t *priv = NULL;

        priv = this->private;
        if (priv->peer.qp) {
                gf_rdma_unregister_peer (priv->device, priv->peer.qp->qp_num);
                ibv_destroy_qp (priv->peer.qp);
        }
        priv->peer.qp = NULL;

        return;
}


static int32_t
gf_rdma_create_qp (rpc_transport_t *this)
{
        gf_rdma_private_t *priv    = NULL;
        gf_rdma_options_t *options = NULL;
        gf_rdma_device_t  *device  = NULL;
        int32_t            ret     = 0;
        gf_rdma_peer_t    *peer    = NULL;

        priv = this->private;
        options = &priv->options;
        device = priv->device;

        peer = &priv->peer;

        struct ibv_qp_init_attr init_attr = {
                .send_cq        = device->send_cq,
                .recv_cq        = device->recv_cq,
                .srq            = device->srq,
                .cap            = {
                        .max_send_wr  = peer->send_count,
                        .max_recv_wr  = peer->recv_count,
                        .max_send_sge = 2,
                        .max_recv_sge = 1
                },
                .qp_type = IBV_QPT_RC
        };

        struct ibv_qp_attr attr = {
                .qp_state        = IBV_QPS_INIT,
                .pkey_index      = 0,
                .port_num        = options->port,
                .qp_access_flags
                = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE
        };

        peer->qp = ibv_create_qp (device->pd, &init_attr);
        if (!peer->qp) {
                gf_log (GF_RDMA_LOG_NAME,
                        GF_LOG_CRITICAL,
                        "%s: could not create QP",
                        this->name);
                ret = -1;
                goto out;
        } else if (ibv_modify_qp (peer->qp, &attr,
                                  IBV_QP_STATE              |
                                  IBV_QP_PKEY_INDEX         |
                                  IBV_QP_PORT               |
                                  IBV_QP_ACCESS_FLAGS)) {
                gf_log (GF_RDMA_LOG_NAME,
                        GF_LOG_ERROR,
                        "%s: failed to modify QP to INIT state",
                        this->name);
                ret = -1;
                goto out;
        }

        peer->local_lid = gf_rdma_get_local_lid (device->context,
                                                 options->port);
        peer->local_qpn = peer->qp->qp_num;
        peer->local_psn = lrand48 () & 0xffffff;

        ret = gf_rdma_register_peer (device, peer->qp->qp_num, peer);

out:
        if (ret == -1)
                __gf_rdma_destroy_qp (this);

        return ret;
}


static void
gf_rdma_destroy_posts (rpc_transport_t *this)
{

}


static int32_t
__gf_rdma_create_posts (rpc_transport_t *this, int32_t count, int32_t size,
                        gf_rdma_queue_t *q, gf_rdma_post_type_t type)
{
        int32_t            i      = 0;
        int32_t            ret    = 0;
        gf_rdma_private_t *priv   = NULL;
        gf_rdma_device_t  *device = NULL;

        priv = this->private;
        device = priv->device;

        for (i=0 ; i<count ; i++) {
                gf_rdma_post_t *post = NULL;

                post = gf_rdma_new_post (device, size + 2048, type);
                if (!post) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "%s: post creation failed",
                                this->name);
                        ret = -1;
                        break;
                }

                gf_rdma_put_post (q, post);
        }
        return ret;
}


static int32_t
gf_rdma_create_posts (rpc_transport_t *this)
{
        int32_t            i       = 0, ret = 0;
        gf_rdma_post_t    *post    = NULL;
        gf_rdma_private_t *priv    = NULL;
        gf_rdma_options_t *options = NULL;
        gf_rdma_device_t  *device  = NULL;

        priv = this->private;
        options = &priv->options;
        device = priv->device;

        ret =  __gf_rdma_create_posts (this, options->send_count,
                                       options->send_size,
                                       &device->sendq, GF_RDMA_SEND_POST);
        if (!ret)
                ret =  __gf_rdma_create_posts (this, options->recv_count,
                                               options->recv_size,
                                               &device->recvq,
                                               GF_RDMA_RECV_POST);

        if (!ret) {
                for (i=0 ; i<options->recv_count ; i++) {
                        post = gf_rdma_get_post (&device->recvq);
                        if (gf_rdma_post_recv (device->srq, post) != 0) {
                                ret = -1;
                                break;
                        }
                }
        }

        if (ret)
                gf_rdma_destroy_posts (this);

        return ret;
}


static int32_t
gf_rdma_connect_qp (rpc_transport_t *this)
{
        gf_rdma_private_t *priv = this->private;
        gf_rdma_options_t *options = &priv->options;
        struct ibv_qp_attr attr = {
                .qp_state               = IBV_QPS_RTR,
                .path_mtu               = options->mtu,
                .dest_qp_num            = priv->peer.remote_qpn,
                .rq_psn                 = priv->peer.remote_psn,
                .max_dest_rd_atomic     = 1,
                .min_rnr_timer          = 12,
                .qp_access_flags
                = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE,
                .ah_attr                = {
                        .is_global      = 0,
                        .dlid           = priv->peer.remote_lid,
                        .sl             = 0,
                        .src_path_bits  = 0,
                        .port_num       = options->port
                }
        };
        if (ibv_modify_qp (priv->peer.qp, &attr,
                           IBV_QP_STATE              |
                           IBV_QP_AV                 |
                           IBV_QP_PATH_MTU           |
                           IBV_QP_DEST_QPN           |
                           IBV_QP_RQ_PSN             |
                           IBV_QP_MAX_DEST_RD_ATOMIC |
                           IBV_QP_MIN_RNR_TIMER)) {
                gf_log (GF_RDMA_LOG_NAME,
                        GF_LOG_CRITICAL,
                        "Failed to modify QP to RTR\n");
                return -1;
        }

        /* TODO: make timeout and retry_cnt configurable from options */
        attr.qp_state       = IBV_QPS_RTS;
        attr.timeout        = 14;
        attr.retry_cnt      = 7;
        attr.rnr_retry      = 7;
        attr.sq_psn         = priv->peer.local_psn;
        attr.max_rd_atomic  = 1;
        if (ibv_modify_qp (priv->peer.qp, &attr,
                           IBV_QP_STATE              |
                           IBV_QP_TIMEOUT            |
                           IBV_QP_RETRY_CNT          |
                           IBV_QP_RNR_RETRY          |
                           IBV_QP_SQ_PSN             |
                           IBV_QP_MAX_QP_RD_ATOMIC)) {
                gf_log (GF_RDMA_LOG_NAME,
                        GF_LOG_CRITICAL,
                        "Failed to modify QP to RTS\n");
                return -1;
        }

        return 0;
}

static int32_t
__gf_rdma_teardown (rpc_transport_t *this)
{
        gf_rdma_private_t *priv = NULL;

        priv = this->private;
        __gf_rdma_destroy_qp (this);

        if (!list_empty (&priv->peer.ioq)) {
                __gf_rdma_ioq_flush (&priv->peer);
        }

        /* TODO: decrement cq size */
        return 0;
}

/*
 * return value:
 *   0 = success (completed)
 *  -1 = error
 * > 0 = incomplete
 */

static int
__tcp_rwv (rpc_transport_t *this, struct iovec *vector, int count,
           struct iovec **pending_vector, int *pending_count,
           int write)
{
        gf_rdma_private_t *priv     = NULL;
        int                sock     = -1;
        int                ret      = -1;
        struct iovec      *opvector = NULL;
        int                opcount  = 0;
        int                moved    = 0;

        priv = this->private;
        sock = priv->sock;
        opvector = vector;
        opcount = count;

        while (opcount)
        {
                if (write)
                {
                        ret = writev (sock, opvector, opcount);

                        if (ret == 0 || (ret == -1 && errno == EAGAIN))
                        {
                                /* done for now */
                                break;
                        }
                }
                else
                {
                        ret = readv (sock, opvector, opcount);

                        if (ret == -1 && errno == EAGAIN)
                        {
                                /* done for now */
                                break;
                        }
                }

                if (ret == 0)
                {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "EOF from peer %s", this->peerinfo.identifier);
                        opcount = -1;
                        errno = ENOTCONN;
                        break;
                }

                if (ret == -1)
                {
                        if (errno == EINTR)
                                continue;

                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s failed (%s)", write ? "writev" : "readv",
                                strerror (errno));
                        if (write && !priv->connected &&
                            (errno == ECONNREFUSED))
                                gf_log (this->name, GF_LOG_ERROR,
                                        "possible mismatch of 'rpc-transport-type'"
                                        " in protocol server and client. "
                                        "check volume file");
                        opcount = -1;
                        break;
                }

                moved = 0;

                while (moved < ret)
                {
                        if ((ret - moved) >= opvector[0].iov_len)
                        {
                                moved += opvector[0].iov_len;
                                opvector++;
                                opcount--;
                        }
                        else
                        {
                                opvector[0].iov_len -= (ret - moved);
                                opvector[0].iov_base += (ret - moved);
                                moved += (ret - moved);
                        }
                        while (opcount && !opvector[0].iov_len)
                        {
                                opvector++;
                                opcount--;
                        }
                }
        }

        if (pending_vector)
                *pending_vector = opvector;

        if (pending_count)
                *pending_count = opcount;

        return opcount;
}


static int
__tcp_readv (rpc_transport_t *this, struct iovec *vector, int count,
             struct iovec **pending_vector, int *pending_count)
{
        int ret = -1;

        ret = __tcp_rwv (this, vector, count,
                         pending_vector, pending_count, 0);

        return ret;
}


static int
__tcp_writev (rpc_transport_t *this, struct iovec *vector, int count,
              struct iovec **pending_vector, int *pending_count)
{
        int                ret  = -1;
        gf_rdma_private_t *priv = NULL;

        priv = this->private;

        ret = __tcp_rwv (this, vector, count, pending_vector,
                         pending_count, 1);

        if (ret > 0) {
                /* TODO: Avoid multiple calls when socket is already
                   registered for POLLOUT */
                priv->idx = event_select_on (this->ctx->event_pool,
                                             priv->sock, priv->idx, -1, 1);
        } else if (ret == 0) {
                priv->idx = event_select_on (this->ctx->event_pool,
                                             priv->sock,
                                             priv->idx, -1, 0);
        }

        return ret;
}


/*
 * allocates new memory to hold write-chunklist. New memory is needed since
 * write-chunklist will be used while sending reply and the post holding initial
 * write-chunklist sent from client will be put back to srq before a pollin
 * event is sent to upper layers.
 */
int32_t
gf_rdma_get_write_chunklist (char **ptr, gf_rdma_write_array_t **write_ary)
{
        gf_rdma_write_array_t *from = NULL, *to = NULL;
        int32_t                ret  = -1, size = 0, i = 0;

        from = (gf_rdma_write_array_t *) *ptr;
        if (from->wc_discrim == 0) {
                ret = 0;
                goto out;
        }

        from->wc_nchunks = ntoh32 (from->wc_nchunks);

        size = sizeof (*from)
                + (sizeof (gf_rdma_write_chunk_t) * from->wc_nchunks);

        to = GF_CALLOC (1, size, gf_common_mt_char);
        if (to == NULL) {
                ret = -1;
                goto out;
        }

        to->wc_discrim = ntoh32 (from->wc_discrim);
        to->wc_nchunks = from->wc_nchunks;

        for (i = 0; i < to->wc_nchunks; i++) {
                to->wc_array[i].wc_target.rs_handle
                        = ntoh32 (from->wc_array[i].wc_target.rs_handle);
                to->wc_array[i].wc_target.rs_length
                        = ntoh32 (from->wc_array[i].wc_target.rs_length);
                to->wc_array[i].wc_target.rs_offset
                        = ntoh64 (from->wc_array[i].wc_target.rs_offset);
        }

        *write_ary = to;
        ret = 0;
        *ptr = (char *)&from->wc_array[i].wc_target.rs_handle;
out:
        return ret;
}


/*
 * does not allocate new memory to hold read-chunklist. New memory is not
 * needed, since post is not put back to srq till we've completed all the
 * rdma-reads and hence readchunk-list can point to memory held by post.
 */
int32_t
gf_rdma_get_read_chunklist (char **ptr, gf_rdma_read_chunk_t **readch)
{
        int32_t               ret   = -1;
        gf_rdma_read_chunk_t *chunk = NULL;
        int                   i     = 0;

        chunk = (gf_rdma_read_chunk_t *)*ptr;
        if (chunk[0].rc_discrim == 0) {
                ret = 0;
                goto out;
        }

        for (i = 0; chunk[i].rc_discrim != 0; i++) {
                chunk[i].rc_discrim = ntoh32 (chunk[i].rc_discrim);
                chunk[i].rc_position = ntoh32 (chunk[i].rc_position);
                chunk[i].rc_target.rs_handle
                        = ntoh32 (chunk[i].rc_target.rs_handle);
                chunk[i].rc_target.rs_length
                        = ntoh32 (chunk[i].rc_target.rs_length);
                chunk[i].rc_target.rs_offset
                        = ntoh64 (chunk[i].rc_target.rs_offset);
        }

        *readch = &chunk[0];
        ret = 0;
        *ptr = (char *)&chunk[i].rc_discrim;
out:
        return ret;
}


inline int32_t
gf_rdma_decode_error_msg (gf_rdma_peer_t *peer, gf_rdma_post_t *post,
                          size_t bytes_in_post)
{
        gf_rdma_header_t *header = NULL;
        struct iobuf     *iobuf  = NULL;
        struct iobref    *iobref = NULL;
        int32_t           ret    = -1;

        header = (gf_rdma_header_t *)post->buf;
        header->rm_body.rm_error.rm_type
                = ntoh32 (header->rm_body.rm_error.rm_type);
        if (header->rm_body.rm_error.rm_type == ERR_VERS) {
                header->rm_body.rm_error.rm_version.gf_rdma_vers_low =
                        ntoh32 (header->rm_body.rm_error.rm_version.gf_rdma_vers_low);
                header->rm_body.rm_error.rm_version.gf_rdma_vers_high =
                        ntoh32 (header->rm_body.rm_error.rm_version.gf_rdma_vers_high);
        }

        iobuf = iobuf_get (peer->trans->ctx->iobuf_pool);
        if (iobuf == NULL) {
                ret = -1;
                goto out;
        }

        post->ctx.iobref = iobref = iobref_new ();
        if (iobref == NULL) {
                ret = -1;
                goto out;
        }

        iobref_add (iobref, iobuf);
        iobuf_unref (iobuf);
        /*
         * FIXME: construct an appropriate rpc-msg here, what is being sent
         * to rpc is not correct.
         */
        post->ctx.vector[0].iov_base = iobuf_ptr (iobuf);
        post->ctx.vector[0].iov_len = bytes_in_post;

        memcpy (post->ctx.vector[0].iov_base, (char *)post->buf,
                post->ctx.vector[0].iov_len);
        post->ctx.count = 1;

        iobuf = NULL;
        iobref = NULL;

out:
        if (ret == -1) {
                if (iobuf != NULL) {
                        iobuf_unref (iobuf);
                }

                if (iobref != NULL) {
                        iobref_unref (iobref);
                }
        }

        return 0;
}


int32_t
gf_rdma_decode_msg (gf_rdma_peer_t *peer, gf_rdma_post_t *post,
                    gf_rdma_read_chunk_t **readch, size_t bytes_in_post)
{
        int32_t                ret        = -1;
        gf_rdma_header_t      *header     = NULL;
        gf_rdma_reply_info_t  *reply_info = NULL;
        char                  *ptr        = NULL;
        gf_rdma_write_array_t *write_ary  = NULL;
        size_t                 header_len = 0;

        header = (gf_rdma_header_t *)post->buf;

        ptr = (char *)&header->rm_body.rm_chunks[0];

        ret = gf_rdma_get_read_chunklist (&ptr, readch);
        if (ret == -1) {
                goto out;
        }

        /* skip terminator of read-chunklist */
        ptr = ptr + sizeof (uint32_t);

        ret = gf_rdma_get_write_chunklist (&ptr, &write_ary);
        if (ret == -1) {
                goto out;
        }

        /* skip terminator of write-chunklist */
        ptr = ptr + sizeof (uint32_t);

        if (write_ary != NULL) {
                reply_info = gf_rdma_reply_info_alloc (peer);
                if (reply_info == NULL) {
                        ret = -1;
                        goto out;
                }

                reply_info->type = gf_rdma_writech;
                reply_info->wc_array = write_ary;
                reply_info->rm_xid = header->rm_xid;
        } else {
                ret = gf_rdma_get_write_chunklist (&ptr, &write_ary);
                if (ret == -1) {
                        goto out;
                }

                if (write_ary != NULL) {
                        reply_info = gf_rdma_reply_info_alloc (peer);
                        if (reply_info == NULL) {
                                ret = -1;
                                goto out;
                        }

                        reply_info->type = gf_rdma_replych;
                        reply_info->wc_array = write_ary;
                        reply_info->rm_xid = header->rm_xid;
                }
        }

        /* skip terminator of reply chunk */
        ptr = ptr + sizeof (uint32_t);
        if (header->rm_type != GF_RDMA_NOMSG) {
                post->ctx.hdr_iobuf = iobuf_get (peer->trans->ctx->iobuf_pool);
                if (post->ctx.hdr_iobuf == NULL) {
                        ret = -1;
                        goto out;
                }

                header_len = (long)ptr - (long)post->buf;
                post->ctx.vector[0].iov_base = iobuf_ptr (post->ctx.hdr_iobuf);
                post->ctx.vector[0].iov_len = bytes_in_post - header_len;
                memcpy (post->ctx.vector[0].iov_base, ptr,
                        post->ctx.vector[0].iov_len);
                post->ctx.count = 1;
        }

        post->ctx.reply_info = reply_info;
out:
        if (ret == -1) {
                if (*readch != NULL) {
                        GF_FREE (*readch);
                        *readch = NULL;
                }

                if (write_ary != NULL) {
                        GF_FREE (write_ary);
                }
        }

        return ret;
}


/* Assumes only one of either write-chunklist or a reply chunk is present */
int32_t
gf_rdma_decode_header (gf_rdma_peer_t *peer, gf_rdma_post_t *post,
                       gf_rdma_read_chunk_t **readch, size_t bytes_in_post)
{
        int32_t           ret    = -1;
        gf_rdma_header_t *header = NULL;

        header = (gf_rdma_header_t *)post->buf;

        header->rm_xid = ntoh32 (header->rm_xid);
        header->rm_vers = ntoh32 (header->rm_vers);
        header->rm_credit = ntoh32 (header->rm_credit);
        header->rm_type = ntoh32 (header->rm_type);

        switch (header->rm_type) {
        case GF_RDMA_MSG:
        case GF_RDMA_NOMSG:
                ret = gf_rdma_decode_msg (peer, post, readch, bytes_in_post);
                break;

        case GF_RDMA_MSGP:
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                        "rdma msg of msg-type GF_RDMA_MSGP should not have "
                        "been received");
                ret = -1;
                break;

        case GF_RDMA_DONE:
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                        "rdma msg of msg-type GF_RDMA_DONE should not have "
                        "been received");
                ret = -1;
                break;

        case GF_RDMA_ERROR:
                /* ret = gf_rdma_decode_error_msg (peer, post, bytes_in_post); */
                break;

        default:
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                        "unknown rdma msg-type (%d)", header->rm_type);
        }

        return ret;
}


int32_t
__gf_rdma_read (gf_rdma_peer_t *peer, gf_rdma_post_t *post, struct iovec *to,
                gf_rdma_read_chunk_t *readch)
{
        int32_t            ret  = -1;
        struct ibv_sge     list = {0, };
        struct ibv_send_wr wr   = {0, }, *bad_wr = NULL;

        ret = __gf_rdma_register_local_mr_for_rdma (peer, to, 1, &post->ctx);
        if (ret == -1) {
                goto out;
        }

        list.addr = (unsigned long) to->iov_base;
        list.length = to->iov_len;
        list.lkey = post->ctx.mr[post->ctx.mr_count - 1]->lkey;

        wr.wr_id      = (unsigned long) gf_rdma_post_ref (post);
        wr.sg_list    = &list;
        wr.num_sge    = 1;
        wr.opcode     = IBV_WR_RDMA_READ;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.wr.rdma.remote_addr = readch->rc_target.rs_offset;
        wr.wr.rdma.rkey = readch->rc_target.rs_handle;

        ret = ibv_post_send (peer->qp, &wr, &bad_wr);
        if (ret) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG, "rdma read from client "
                        "(%s) failed with ret = %d (%s)",
                        peer->trans->peerinfo.identifier,
                        ret, (ret > 0) ? strerror (ret) : "");
                ret = -1;
                gf_rdma_post_unref (post);
        }
out:
        return ret;
}


int32_t
gf_rdma_do_reads (gf_rdma_peer_t *peer, gf_rdma_post_t *post,
                  gf_rdma_read_chunk_t *readch)
{
        int32_t            ret   = -1, i = 0, count = 0;
        size_t             size  = 0;
        char              *ptr   = NULL;
        struct iobuf      *iobuf = NULL;
        gf_rdma_private_t *priv  = NULL;

        priv = peer->trans->private;

        for (i = 0; readch[i].rc_discrim != 0; i++) {
                size += readch[i].rc_target.rs_length;
        }

        if (i == 0) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                        "message type specified as rdma-read but there are no "
                        "rdma read-chunks present");
                goto out;
        }

        post->ctx.gf_rdma_reads = i;

        if (size > peer->trans->ctx->page_size) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                        "total size of rdma-read (%lu) is greater than "
                        "page-size (%lu). This is not supported till variable "
                        "sized iobufs are implemented", (unsigned long)size,
                        (unsigned long)peer->trans->ctx->page_size);
                goto out;
        }

        iobuf = iobuf_get (peer->trans->ctx->iobuf_pool);
        if (iobuf == NULL) {
                goto out;
        }

        if (post->ctx.iobref == NULL) {
                post->ctx.iobref = iobref_new ();
                if (post->ctx.iobref == NULL) {
                        iobuf_unref (iobuf);
                        goto out;
                }
        }

        iobref_add (post->ctx.iobref, iobuf);
        iobuf_unref (iobuf);

        ptr = iobuf_ptr (iobuf);
        iobuf = NULL;

        pthread_mutex_lock (&priv->write_mutex);
        {
                if (!priv->connected) {
                        goto unlock;
                }

                for (i = 0; readch[i].rc_discrim != 0; i++) {
                        count = post->ctx.count++;
                        post->ctx.vector[count].iov_base = ptr;
                        post->ctx.vector[count].iov_len
                                = readch[i].rc_target.rs_length;

                        ret = __gf_rdma_read (peer, post,
                                              &post->ctx.vector[count],
                                              &readch[i]);
                        if (ret == -1) {
                                goto unlock;
                        }

                        ptr += readch[i].rc_target.rs_length;
                }

                ret = 0;
        }
unlock:
        pthread_mutex_unlock (&priv->write_mutex);
out:

        if (ret == -1) {
                if (iobuf != NULL) {
                        iobuf_unref (iobuf);
                }
        }

        return ret;
}


int32_t
gf_rdma_pollin_notify (gf_rdma_peer_t *peer, gf_rdma_post_t *post)
{
        int32_t                    ret             = -1;
        enum msg_type              msg_type        = 0;
        struct rpc_req            *rpc_req         = NULL;
        gf_rdma_request_context_t *request_context = NULL;
        rpc_request_info_t         request_info    = {0, };
        gf_rdma_private_t         *priv            = NULL;
        uint32_t                  *ptr             = NULL;
        rpc_transport_pollin_t    *pollin          = NULL;

        if ((peer == NULL) || (post == NULL)) {
                goto out;
        }

        if (post->ctx.iobref == NULL) {
                post->ctx.iobref = iobref_new ();
                if (post->ctx.iobref == NULL) {
                        goto out;
                }

                /* handling the case where both hdr and payload of
                 * GF_FOP_READ_CBK were received in a single iobuf
                 * because of server sending entire msg as inline without
                 * doing rdma writes.
                 */
                if (post->ctx.hdr_iobuf)
                        iobref_add (post->ctx.iobref, post->ctx.hdr_iobuf);
        }

        pollin = rpc_transport_pollin_alloc (peer->trans,
                                             post->ctx.vector,
                                             post->ctx.count,
                                             post->ctx.hdr_iobuf,
                                             post->ctx.iobref,
                                             post->ctx.reply_info);
        if (pollin == NULL) {
                goto out;
        }

        ptr = (uint32_t *)pollin->vector[0].iov_base;

        request_info.xid = ntoh32 (*ptr);
        msg_type = ntoh32 (*(ptr + 1));

        if (msg_type == REPLY) {
                ret = rpc_transport_notify (peer->trans,
                                            RPC_TRANSPORT_MAP_XID_REQUEST,
                                            &request_info);
                if (ret == -1) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                "cannot get request information from rpc "
                                "layer");
                        goto out;
                }

                rpc_req = request_info.rpc_req;
                if (rpc_req == NULL) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                "rpc request structure not found");
                        ret = -1;
                        goto out;
                }

                request_context = rpc_req->conn_private;
                rpc_req->conn_private = NULL;

                priv = peer->trans->private;
                if (request_context != NULL) {
                        pthread_mutex_lock (&priv->write_mutex);
                        {
                                __gf_rdma_request_context_destroy (request_context);
                        }
                        pthread_mutex_unlock (&priv->write_mutex);
                } else {
                        gf_rdma_quota_put (peer);
                }

                pollin->is_reply = 1;
        }

        ret = rpc_transport_notify (peer->trans, RPC_TRANSPORT_MSG_RECEIVED,
                                    pollin);

out:
        if (pollin != NULL) {
                pollin->private = NULL;
                rpc_transport_pollin_destroy (pollin);
        }

        return ret;
}


int32_t
gf_rdma_recv_reply (gf_rdma_peer_t *peer, gf_rdma_post_t *post)
{
        int32_t                    ret          = -1;
        gf_rdma_header_t          *header       = NULL;
        gf_rdma_reply_info_t      *reply_info   = NULL;
        gf_rdma_write_array_t     *wc_array     = NULL;
        int                        i            = 0;
        uint32_t                  *ptr          = NULL;
        gf_rdma_request_context_t *ctx          = NULL;
        rpc_request_info_t         request_info = {0, };
        struct rpc_req            *rpc_req      = NULL;

        header = (gf_rdma_header_t *)post->buf;
        reply_info = post->ctx.reply_info;

        /* no write chunklist, just notify upper layers */
        if (reply_info == NULL) {
                ret = 0;
                goto out;
        }

        wc_array = reply_info->wc_array;

        if (header->rm_type == GF_RDMA_NOMSG) {
                post->ctx.vector[0].iov_base
                        = (void *)(long)wc_array->wc_array[0].wc_target.rs_offset;
                post->ctx.vector[0].iov_len
                        = wc_array->wc_array[0].wc_target.rs_length;

                post->ctx.count = 1;
        } else {
                for (i = 0; i < wc_array->wc_nchunks; i++) {
                        post->ctx.vector[i + 1].iov_base
                                = (void *)(long)wc_array->wc_array[i].wc_target.rs_offset;
                        post->ctx.vector[i + 1].iov_len
                                = wc_array->wc_array[i].wc_target.rs_length;
                }

                post->ctx.count += wc_array->wc_nchunks;
        }

        ptr = (uint32_t *)post->ctx.vector[0].iov_base;
        request_info.xid = ntoh32 (*ptr);

        ret = rpc_transport_notify (peer->trans,
                                    RPC_TRANSPORT_MAP_XID_REQUEST,
                                    &request_info);
        if (ret == -1) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                        "cannot get request information from rpc "
                        "layer");
                goto out;
        }

        rpc_req = request_info.rpc_req;
        if (rpc_req == NULL) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                        "rpc request structure not found");
                ret = -1;
                goto out;
        }

        ctx = rpc_req->conn_private;
        if ((post->ctx.iobref == NULL) && ctx->rsp_iobref) {
                post->ctx.iobref = iobref_ref (ctx->rsp_iobref);
        }

        ret = 0;

        gf_rdma_reply_info_destroy (reply_info);

out:
        if (ret == 0) {
                ret = gf_rdma_pollin_notify (peer, post);
        }

        return ret;
}


inline int32_t
gf_rdma_recv_request (gf_rdma_peer_t *peer, gf_rdma_post_t *post,
                      gf_rdma_read_chunk_t *readch)
{
        int32_t ret = -1;

        if (readch != NULL) {
                ret = gf_rdma_do_reads (peer, post, readch);
        } else {
                ret = gf_rdma_pollin_notify (peer, post);
                if (ret == -1) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                "pollin notification failed");
                }
        }

        return ret;
}

void
gf_rdma_process_recv (gf_rdma_peer_t *peer, struct ibv_wc *wc)
{
        gf_rdma_post_t       *post     = NULL;
        gf_rdma_read_chunk_t *readch   = NULL;
        int                   ret      = -1;
        uint32_t             *ptr      = NULL;
        enum msg_type         msg_type = 0;
        gf_rdma_header_t     *header   = NULL;

        post = (gf_rdma_post_t *) (long) wc->wr_id;
        if (post == NULL) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                        "no post found in successful work completion element");
                goto out;
        }

        ret = gf_rdma_decode_header (peer, post, &readch, wc->byte_len);
        if (ret == -1) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                        "decoding of header failed");
                goto out;
        }

        header = (gf_rdma_header_t *)post->buf;

        switch (header->rm_type) {
        case GF_RDMA_MSG:
                ptr = (uint32_t *)post->ctx.vector[0].iov_base;
                msg_type = ntoh32 (*(ptr + 1));
                break;

        case GF_RDMA_NOMSG:
                if (readch != NULL) {
                        msg_type = CALL;
                } else {
                        msg_type = REPLY;
                }
                break;

        case GF_RDMA_ERROR:
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                        "an error has happened while transmission of msg, "
                        "disconnecting the transport");
                rpc_transport_disconnect (peer->trans);
                goto out;

/*                ret = gf_rdma_pollin_notify (peer, post);
                  if (ret == -1) {
                  gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                  "pollin notification failed");
                  }
                  goto out;
*/

        default:
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                        "invalid rdma msg-type (%d)", header->rm_type);
                break;
        }

        if (msg_type == CALL) {
                ret = gf_rdma_recv_request (peer, post, readch);
        } else {
                ret = gf_rdma_recv_reply (peer, post);
        }

out:
        if (ret == -1) {
                rpc_transport_disconnect (peer->trans);
        }

        return;
}


static void *
gf_rdma_recv_completion_proc (void *data)
{
        struct ibv_comp_channel *chan      = NULL;
        gf_rdma_device_t        *device    = NULL;;
        gf_rdma_post_t          *post      = NULL;
        gf_rdma_peer_t          *peer      = NULL;
        struct ibv_cq           *event_cq  = NULL;
        struct ibv_wc            wc        = {0, };
        void                    *event_ctx = NULL;
        int32_t                  ret       = 0;

        chan = data;

        while (1) {
                ret = ibv_get_cq_event (chan, &event_cq, &event_ctx);
                if (ret) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "ibv_get_cq_event failed, terminating recv "
                                "thread %d (%d)", ret, errno);
                        continue;
                }

                device = event_ctx;

                ret = ibv_req_notify_cq (event_cq, 0);
                if (ret) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "ibv_req_notify_cq on %s failed, terminating "
                                "recv thread: %d (%d)",
                                device->device_name, ret, errno);
                        continue;
                }

                device = (gf_rdma_device_t *) event_ctx;

                while ((ret = ibv_poll_cq (event_cq, 1, &wc)) > 0) {
                        post = (gf_rdma_post_t *) (long) wc.wr_id;

                        pthread_mutex_lock (&device->qpreg.lock);
                        {
                                peer = __gf_rdma_lookup_peer (device,
                                                              wc.qp_num);

                                /*
                                 * keep a refcount on transport so that it
                                 * does not get freed because of some error
                                 * indicated by wc.status till we are done
                                 * with usage of peer and thereby that of trans.
                                 */
                                if (peer != NULL) {
                                        rpc_transport_ref (peer->trans);
                                }
                        }
                        pthread_mutex_unlock (&device->qpreg.lock);

                        if (wc.status != IBV_WC_SUCCESS) {
                                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                        "recv work request on `%s' returned "
                                        "error (%d)", device->device_name,
                                        wc.status);
                                if (peer) {
                                        rpc_transport_unref (peer->trans);
                                        rpc_transport_disconnect (peer->trans);
                                }

                                if (post) {
                                        gf_rdma_post_unref (post);
                                }
                                continue;
                        }

                        if (peer) {
                                gf_rdma_process_recv (peer, &wc);
                                rpc_transport_unref (peer->trans);
                        } else {
                                gf_log (GF_RDMA_LOG_NAME,
                                        GF_LOG_DEBUG,
                                        "could not lookup peer for qp_num: %d",
                                        wc.qp_num);
                        }

                        gf_rdma_post_unref (post);
                }

                if (ret < 0) {
                        gf_log (GF_RDMA_LOG_NAME,
                                GF_LOG_ERROR,
                                "ibv_poll_cq on `%s' returned error "
                                "(ret = %d, errno = %d)",
                                device->device_name, ret, errno);
                        continue;
                }
                ibv_ack_cq_events (event_cq, 1);
        }

        return NULL;
}


void
gf_rdma_handle_failed_send_completion (gf_rdma_peer_t *peer, struct ibv_wc *wc)
{
        gf_rdma_post_t    *post   = NULL;
        gf_rdma_device_t  *device = NULL;
        gf_rdma_private_t *priv   = NULL;

        if (peer != NULL) {
                priv = peer->trans->private;
                if (priv != NULL) {
                        device = priv->device;
                }
        }


        post = (gf_rdma_post_t *) (long) wc->wr_id;

        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                "send work request on `%s' returned error "
                "wc.status = %d, wc.vendor_err = %d, post->buf = %p, "
                "wc.byte_len = %d, post->reused = %d",
                (device != NULL) ? device->device_name : NULL, wc->status,
                wc->vendor_err, post->buf, wc->byte_len, post->reused);

        if (wc->status == IBV_WC_RETRY_EXC_ERR) {
                gf_log ("rdma", GF_LOG_ERROR, "connection between client and"
                        " server not working. check by running "
                        "'ibv_srq_pingpong'. also make sure subnet manager"
                        " is running (eg: 'opensm'), or check if rdma port is "
                        "valid (or active) by running 'ibv_devinfo'. contact "
                        "Gluster Support Team if the problem persists.");
        }

        if (peer) {
                rpc_transport_disconnect (peer->trans);
        }

        return;
}


void
gf_rdma_handle_successful_send_completion (gf_rdma_peer_t *peer,
                                           struct ibv_wc *wc)
{
        gf_rdma_post_t   *post   = NULL;
        int               reads  = 0, ret = 0;
        gf_rdma_header_t *header = NULL;

        if (wc->opcode != IBV_WC_RDMA_READ) {
                goto out;
        }

        post = (gf_rdma_post_t *)(long) wc->wr_id;

        pthread_mutex_lock (&post->lock);
        {
                reads = --post->ctx.gf_rdma_reads;
        }
        pthread_mutex_unlock (&post->lock);

        if (reads != 0) {
                /* if it is not the last rdma read, we've got nothing to do */
                goto out;
        }

        header = (gf_rdma_header_t *)post->buf;

        if (header->rm_type == GF_RDMA_NOMSG) {
                post->ctx.count = 1;
                post->ctx.vector[0].iov_len += post->ctx.vector[1].iov_len;
        }

        ret = gf_rdma_pollin_notify (peer, post);
        if ((ret == -1) && (peer != NULL)) {
                rpc_transport_disconnect (peer->trans);
        }

out:
        return;
}


static void *
gf_rdma_send_completion_proc (void *data)
{
        struct ibv_comp_channel *chan       = NULL;
        gf_rdma_post_t          *post       = NULL;
        gf_rdma_peer_t          *peer       = NULL;
        struct ibv_cq           *event_cq   = NULL;
        void                    *event_ctx  = NULL;
        gf_rdma_device_t        *device     = NULL;
        struct ibv_wc            wc         = {0, };
        char                     is_request = 0;
        int32_t                  ret        = 0, quota_ret = 0;

        chan = data;
        while (1) {
                ret = ibv_get_cq_event (chan, &event_cq, &event_ctx);
                if (ret) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "ibv_get_cq_event on failed, terminating "
                                "send thread: %d (%d)", ret, errno);
                        continue;
                }

                device = event_ctx;

                ret = ibv_req_notify_cq (event_cq, 0);
                if (ret) {
                        gf_log (GF_RDMA_LOG_NAME,  GF_LOG_ERROR,
                                "ibv_req_notify_cq on %s failed, terminating "
                                "send thread: %d (%d)",
                                device->device_name, ret, errno);
                        continue;
                }

                while ((ret = ibv_poll_cq (event_cq, 1, &wc)) > 0) {
                        post = (gf_rdma_post_t *) (long) wc.wr_id;

                        pthread_mutex_lock (&device->qpreg.lock);
                        {
                                peer = __gf_rdma_lookup_peer (device, wc.qp_num);

                                /*
                                 * keep a refcount on transport so that it
                                 * does not get freed because of some error
                                 * indicated by wc.status, till we are done
                                 * with usage of peer and thereby that of trans.
                                 */
                                if (peer != NULL) {
                                        rpc_transport_ref (peer->trans);
                                }
                        }
                        pthread_mutex_unlock (&device->qpreg.lock);

                        if (wc.status != IBV_WC_SUCCESS) {
                                gf_rdma_handle_failed_send_completion (peer, &wc);
                        } else {
                                gf_rdma_handle_successful_send_completion (peer,
                                                                           &wc);
                        }

                        if (post) {
                                is_request = post->ctx.is_request;

                                ret = gf_rdma_post_unref (post);
                                if ((ret == 0)
                                    && (wc.status == IBV_WC_SUCCESS)
                                    && !is_request
                                    && (post->type == GF_RDMA_SEND_POST)
                                    && (peer != NULL)) {
                                        /* An GF_RDMA_RECV_POST can end up in
                                         * gf_rdma_send_completion_proc for
                                         * rdma-reads, and we do not take
                                         * quota for getting an GF_RDMA_RECV_POST.
                                         */

                                        /*
                                         * if it is request, quota is returned
                                         * after reply has come.
                                         */
                                        quota_ret = gf_rdma_quota_put (peer);
                                        if (quota_ret < 0) {
                                                gf_log ("rdma", GF_LOG_DEBUG,
                                                        "failed to send "
                                                        "message");
                                        }
                                }
                        }

                        if (peer) {
                                rpc_transport_unref (peer->trans);
                        } else {
                                gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                                        "could not lookup peer for qp_num: %d",
                                        wc.qp_num);
                        }
                }

                if (ret < 0) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "ibv_poll_cq on `%s' returned error (ret = %d,"
                                " errno = %d)",
                                device->device_name, ret, errno);
                        continue;
                }

                ibv_ack_cq_events (event_cq, 1);
        }

        return NULL;
}


static void
gf_rdma_options_init (rpc_transport_t *this)
{
        gf_rdma_private_t *priv    = NULL;
        gf_rdma_options_t *options = NULL;
        int32_t            mtu     = 0;
        data_t            *temp    = NULL;

        /* TODO: validate arguments from options below */

        priv = this->private;
        options = &priv->options;
        options->send_size = GLUSTERFS_RDMA_INLINE_THRESHOLD;/*this->ctx->page_size * 4;  512 KB*/
        options->recv_size = GLUSTERFS_RDMA_INLINE_THRESHOLD;/*this->ctx->page_size * 4;  512 KB*/
        options->send_count = 4096;
        options->recv_count = 4096;

        temp = dict_get (this->options,
                         "transport.rdma.work-request-send-count");
        if (temp)
                options->send_count = data_to_int32 (temp);

        temp = dict_get (this->options,
                         "transport.rdma.work-request-recv-count");
        if (temp)
                options->recv_count = data_to_int32 (temp);

        options->port = 1;
        temp = dict_get (this->options,
                         "transport.rdma.port");
        if (temp)
                options->port = data_to_uint64 (temp);

        options->mtu = mtu = IBV_MTU_2048;
        temp = dict_get (this->options,
                         "transport.rdma.mtu");
        if (temp)
                mtu = data_to_int32 (temp);
        switch (mtu) {
        case 256: options->mtu = IBV_MTU_256;
                break;
        case 512: options->mtu = IBV_MTU_512;
                break;
        case 1024: options->mtu = IBV_MTU_1024;
                break;
        case 2048: options->mtu = IBV_MTU_2048;
                break;
        case 4096: options->mtu = IBV_MTU_4096;
                break;
        default:
                if (temp)
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING,
                                "%s: unrecognized MTU value '%s', defaulting "
                                "to '2048'", this->name,
                                data_to_str (temp));
                else
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_TRACE,
                                "%s: defaulting MTU to '2048'",
                                this->name);
                options->mtu = IBV_MTU_2048;
                break;
        }

        temp = dict_get (this->options,
                         "transport.rdma.device-name");
        if (temp)
                options->device_name = gf_strdup (temp->data);

        return;
}

static void
gf_rdma_queue_init (gf_rdma_queue_t *queue)
{
        pthread_mutex_init (&queue->lock, NULL);

        queue->active_posts.next = &queue->active_posts;
        queue->active_posts.prev = &queue->active_posts;
        queue->passive_posts.next = &queue->passive_posts;
        queue->passive_posts.prev = &queue->passive_posts;
}


static gf_rdma_device_t *
gf_rdma_get_device (rpc_transport_t *this, struct ibv_context *ibctx)
{
        glusterfs_ctx_t   *ctx         = NULL;
        gf_rdma_private_t *priv        = NULL;
        gf_rdma_options_t *options     = NULL;
        char              *device_name = NULL;
        uint32_t           port        = 0;
        uint8_t            active_port = 0;
        int32_t            ret         = 0;
        int32_t            i           = 0;
        gf_rdma_device_t  *trav        = NULL;

        priv        = this->private;
        options     = &priv->options;
        device_name = priv->options.device_name;
        ctx         = this->ctx;
        trav        = ctx->ib;
        port        = priv->options.port;

        while (trav) {
                if ((!strcmp (trav->device_name, device_name)) &&
                    (trav->port == port))
                        break;
                trav = trav->next;
        }

        if (!trav) {

                trav = GF_CALLOC (1, sizeof (*trav),
                                  gf_common_mt_rdma_device_t);
                if (trav == NULL) {
                        return NULL;
                }

                priv->device = trav;

                trav->context = ibctx;

                ret = ib_get_active_port (trav->context);

                if (ret < 0) {
                        if (!port) {
                                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                        "Failed to find any active ports and "
                                        "none specified in volume file,"
                                        " exiting");
                                GF_FREE (trav);
                                return NULL;
                        }
                }

                trav->request_ctx_pool = mem_pool_new (gf_rdma_request_context_t,
                                                       GF_RDMA_POOL_SIZE);
                if (trav->request_ctx_pool == NULL) {
                        return NULL;
                }

                trav->ioq_pool = mem_pool_new (gf_rdma_ioq_t, GF_RDMA_POOL_SIZE);
                if (trav->ioq_pool == NULL) {
                        mem_pool_destroy (trav->request_ctx_pool);
                        return NULL;
                }

                trav->reply_info_pool = mem_pool_new (gf_rdma_reply_info_t,
                                                      GF_RDMA_POOL_SIZE);
                if (trav->reply_info_pool == NULL) {
                        mem_pool_destroy (trav->request_ctx_pool);
                        mem_pool_destroy (trav->ioq_pool);
                        return NULL;
                }


                active_port = ret;

                if (port) {
                        ret = ib_check_active_port (trav->context, port);
                        if (ret < 0) {
                                gf_log (GF_RDMA_LOG_NAME, GF_LOG_WARNING,
                                        "On device %s: provided port:%u is "
                                        "found to be offline, continuing to "
                                        "use the same port", device_name, port);
                        }
                } else {
                        priv->options.port = active_port;
                        port = active_port;
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_TRACE,
                                "Port unspecified in volume file using active "
                                "port: %u", port);
                }

                trav->device_name = gf_strdup (device_name);
                trav->port = port;

                trav->next = ctx->ib;
                ctx->ib = trav;

                trav->send_chan = ibv_create_comp_channel (trav->context);
                if (!trav->send_chan) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "%s: could not create send completion channel",
                                device_name);
                        mem_pool_destroy (trav->ioq_pool);
                        mem_pool_destroy (trav->request_ctx_pool);
                        mem_pool_destroy (trav->reply_info_pool);
                        GF_FREE ((char *)trav->device_name);
                        GF_FREE (trav);
                        return NULL;
                }

                trav->recv_chan = ibv_create_comp_channel (trav->context);
                if (!trav->recv_chan) {
                        mem_pool_destroy (trav->ioq_pool);
                        mem_pool_destroy (trav->request_ctx_pool);
                        mem_pool_destroy (trav->reply_info_pool);
                        ibv_destroy_comp_channel (trav->send_chan);
                        GF_FREE ((char *)trav->device_name);
                        GF_FREE (trav);
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "could not create recv completion channel");
                        /* TODO: cleanup current mess */
                        return NULL;
                }

                if (gf_rdma_create_cq (this) < 0) {
                        mem_pool_destroy (trav->ioq_pool);
                        mem_pool_destroy (trav->request_ctx_pool);
                        mem_pool_destroy (trav->reply_info_pool);
                        ibv_destroy_comp_channel (trav->recv_chan);
                        ibv_destroy_comp_channel (trav->send_chan);
                        GF_FREE ((char *)trav->device_name);
                        GF_FREE (trav);
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "%s: could not create CQ",
                                this->name);
                        return NULL;
                }

                /* protection domain */
                trav->pd = ibv_alloc_pd (trav->context);

                if (!trav->pd) {
                        mem_pool_destroy (trav->ioq_pool);
                        mem_pool_destroy (trav->request_ctx_pool);
                        mem_pool_destroy (trav->reply_info_pool);
                        gf_rdma_destroy_cq (this);
                        ibv_destroy_comp_channel (trav->recv_chan);
                        ibv_destroy_comp_channel (trav->send_chan);
                        GF_FREE ((char *)trav->device_name);
                        GF_FREE (trav);
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "%s: could not allocate protection domain",
                                this->name);
                        return NULL;
                }

                struct ibv_srq_init_attr attr = {
                        .attr = {
                                .max_wr = options->recv_count,
                                .max_sge = 1
                        }
                };
                trav->srq = ibv_create_srq (trav->pd, &attr);

                if (!trav->srq) {
                        mem_pool_destroy (trav->ioq_pool);
                        mem_pool_destroy (trav->request_ctx_pool);
                        mem_pool_destroy (trav->reply_info_pool);
                        ibv_dealloc_pd (trav->pd);
                        gf_rdma_destroy_cq (this);
                        ibv_destroy_comp_channel (trav->recv_chan);
                        ibv_destroy_comp_channel (trav->send_chan);
                        GF_FREE ((char *)trav->device_name);
                        GF_FREE (trav);

                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "%s: could not create SRQ",
                                this->name);
                        return NULL;
                }

                /* queue init */
                gf_rdma_queue_init (&trav->sendq);
                gf_rdma_queue_init (&trav->recvq);

                if (gf_rdma_create_posts (this) < 0) {
                        mem_pool_destroy (trav->ioq_pool);
                        mem_pool_destroy (trav->request_ctx_pool);
                        mem_pool_destroy (trav->reply_info_pool);
                        ibv_dealloc_pd (trav->pd);
                        gf_rdma_destroy_cq (this);
                        ibv_destroy_comp_channel (trav->recv_chan);
                        ibv_destroy_comp_channel (trav->send_chan);
                        GF_FREE ((char *)trav->device_name);
                        GF_FREE (trav);

                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "%s: could not allocate posts",
                                this->name);
                        return NULL;
                }

                /* completion threads */
                ret = pthread_create (&trav->send_thread,
                                      NULL,
                                      gf_rdma_send_completion_proc,
                                      trav->send_chan);
                if (ret) {
                        gf_rdma_destroy_posts (this);
                        mem_pool_destroy (trav->ioq_pool);
                        mem_pool_destroy (trav->request_ctx_pool);
                        mem_pool_destroy (trav->reply_info_pool);
                        ibv_dealloc_pd (trav->pd);
                        gf_rdma_destroy_cq (this);
                        ibv_destroy_comp_channel (trav->recv_chan);
                        ibv_destroy_comp_channel (trav->send_chan);
                        GF_FREE ((char *)trav->device_name);
                        GF_FREE (trav);

                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "could not create send completion thread");
                        return NULL;
                }

                ret = pthread_create (&trav->recv_thread,
                                      NULL,
                                      gf_rdma_recv_completion_proc,
                                      trav->recv_chan);
                if (ret) {
                        gf_rdma_destroy_posts (this);
                        mem_pool_destroy (trav->ioq_pool);
                        mem_pool_destroy (trav->request_ctx_pool);
                        mem_pool_destroy (trav->reply_info_pool);
                        ibv_dealloc_pd (trav->pd);
                        gf_rdma_destroy_cq (this);
                        ibv_destroy_comp_channel (trav->recv_chan);
                        ibv_destroy_comp_channel (trav->send_chan);
                        GF_FREE ((char *)trav->device_name);
                        GF_FREE (trav);
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "could not create recv completion thread");
                        return NULL;
                }

                /* qpreg */
                pthread_mutex_init (&trav->qpreg.lock, NULL);
                for (i=0; i<42; i++) {
                        trav->qpreg.ents[i].next = &trav->qpreg.ents[i];
                        trav->qpreg.ents[i].prev = &trav->qpreg.ents[i];
                }
        }
        return trav;
}

static int32_t
gf_rdma_init (rpc_transport_t *this)
{
        gf_rdma_private_t   *priv    = NULL;
        gf_rdma_options_t   *options = NULL;
        struct ibv_device  **dev_list;
        struct ibv_context  *ib_ctx  = NULL;
        int32_t              ret     = 0;

        priv = this->private;
        options = &priv->options;

        ibv_fork_init ();
        gf_rdma_options_init (this);

        {
                dev_list = ibv_get_device_list (NULL);

                if (!dev_list) {
                        gf_log (GF_RDMA_LOG_NAME,
                                GF_LOG_CRITICAL,
                                "Failed to get IB devices");
                        ret = -1;
                        goto cleanup;
                }

                if (!*dev_list) {
                        gf_log (GF_RDMA_LOG_NAME,
                                GF_LOG_CRITICAL,
                                "No IB devices found");
                        ret = -1;
                        goto cleanup;
                }

                if (!options->device_name) {
                        if (*dev_list) {
                                options->device_name =
                                        gf_strdup (ibv_get_device_name (*dev_list));
                        } else {
                                gf_log (GF_RDMA_LOG_NAME, GF_LOG_CRITICAL,
                                        "IB device list is empty. Check for "
                                        "'ib_uverbs' module");
                                return -1;
                                goto cleanup;
                        }
                }

                while (*dev_list) {
                        if (!strcmp (ibv_get_device_name (*dev_list),
                                     options->device_name)) {
                                ib_ctx = ibv_open_device (*dev_list);

                                if (!ib_ctx) {
                                        gf_log (GF_RDMA_LOG_NAME,
                                                GF_LOG_ERROR,
                                                "Failed to get infiniband"
                                                "device context");
                                        ret = -1;
                                        goto cleanup;
                                }
                                break;
                        }
                        ++dev_list;
                }

                priv->device = gf_rdma_get_device (this, ib_ctx);

                if (!priv->device) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "could not create rdma device for %s",
                                options->device_name);
                        ret = -1;
                        goto cleanup;
                }
        }

        priv->peer.trans = this;
        INIT_LIST_HEAD (&priv->peer.ioq);

        pthread_mutex_init (&priv->read_mutex, NULL);
        pthread_mutex_init (&priv->write_mutex, NULL);
        pthread_mutex_init (&priv->recv_mutex, NULL);
        pthread_cond_init (&priv->recv_cond, NULL);

cleanup:
        if (-1 == ret) {
                if (ib_ctx)
                        ibv_close_device (ib_ctx);
        }

        if (dev_list)
                ibv_free_device_list (dev_list);

        return ret;
}


static int32_t
gf_rdma_disconnect (rpc_transport_t *this)
{
        gf_rdma_private_t *priv = NULL;
        int32_t            ret  = 0;

        priv = this->private;
        pthread_mutex_lock (&priv->write_mutex);
        {
                ret = __gf_rdma_disconnect (this);
        }
        pthread_mutex_unlock (&priv->write_mutex);

        return ret;
}


static int32_t
__tcp_connect_finish (int fd)
{
        int       ret    = -1;
        int       optval = 0;
        socklen_t optlen = sizeof (int);

        ret = getsockopt (fd, SOL_SOCKET, SO_ERROR,
                          (void *)&optval, &optlen);

        if (ret == 0 && optval)
        {
                errno = optval;
                ret = -1;
        }

        return ret;
}

static inline void
gf_rdma_fill_handshake_data (char *buf, struct gf_rdma_nbio *nbio,
                             gf_rdma_private_t *priv)
{
        sprintf (buf,
                 "QP1:RECV_BLKSIZE=%08x:SEND_BLKSIZE=%08x\n"
                 "QP1:LID=%04x:QPN=%06x:PSN=%06x\n",
                 priv->peer.recv_size,
                 priv->peer.send_size,
                 priv->peer.local_lid,
                 priv->peer.local_qpn,
                 priv->peer.local_psn);

        nbio->vector.iov_base = buf;
        nbio->vector.iov_len = strlen (buf) + 1;
        nbio->count = 1;
        return;
}

static inline void
gf_rdma_fill_handshake_ack (char *buf, struct gf_rdma_nbio *nbio)
{
        sprintf (buf, "DONE\n");
        nbio->vector.iov_base = buf;
        nbio->vector.iov_len = strlen (buf) + 1;
        nbio->count = 1;
        return;
}

static int
gf_rdma_handshake_pollin (rpc_transport_t *this)
{
        int                ret           = 0;
        gf_rdma_private_t *priv          = NULL;
        char              *buf           = NULL;
        int32_t            recv_buf_size = 0, send_buf_size;
        socklen_t          sock_len      = 0;

        priv = this->private;
	buf = priv->handshake.incoming.buf;

        if (priv->handshake.incoming.state == GF_RDMA_HANDSHAKE_COMPLETE) {
                return -1;
        }

        pthread_mutex_lock (&priv->write_mutex);
        {
                while (priv->handshake.incoming.state != GF_RDMA_HANDSHAKE_COMPLETE)
                {
                        switch (priv->handshake.incoming.state)
                        {
                        case GF_RDMA_HANDSHAKE_START:
                                buf = priv->handshake.incoming.buf = GF_CALLOC (1, 256, gf_common_mt_char);
                                gf_rdma_fill_handshake_data (buf, &priv->handshake.incoming, priv);
                                buf[0] = 0;
                                priv->handshake.incoming.state = GF_RDMA_HANDSHAKE_RECEIVING_DATA;
                                break;

                        case GF_RDMA_HANDSHAKE_RECEIVING_DATA:
                                ret = __tcp_readv (this,
                                                   &priv->handshake.incoming.vector,
                                                   priv->handshake.incoming.count,
                                                   &priv->handshake.incoming.pending_vector,
                                                   &priv->handshake.incoming.pending_count);
                                if (ret == -1) {
                                        goto unlock;
                                }

                                if (ret > 0) {
                                        gf_log (this->name, GF_LOG_TRACE,
                                                "partial header read on NB socket. continue later");
                                        goto unlock;
                                }

                                if (!ret) {
                                        priv->handshake.incoming.state = GF_RDMA_HANDSHAKE_RECEIVED_DATA;
                                }
                                break;

                        case GF_RDMA_HANDSHAKE_RECEIVED_DATA:
                                ret = sscanf (buf,
                                              "QP1:RECV_BLKSIZE=%08x:SEND_BLKSIZE=%08x\n"
                                              "QP1:LID=%04x:QPN=%06x:PSN=%06x\n",
                                              &recv_buf_size,
                                              &send_buf_size,
                                              &priv->peer.remote_lid,
                                              &priv->peer.remote_qpn,
                                              &priv->peer.remote_psn);

                                if ((ret != 5) && (strncmp (buf, "QP1:", 4))) {
                                        gf_log (GF_RDMA_LOG_NAME,
                                                GF_LOG_CRITICAL,
                                                "%s: remote-host(%s)'s "
                                                "transport type is different",
                                                this->name,
                                                this->peerinfo.identifier);
                                        ret = -1;
                                        goto unlock;
                                }

                                if (recv_buf_size < priv->peer.recv_size)
                                        priv->peer.recv_size = recv_buf_size;
                                if (send_buf_size < priv->peer.send_size)
                                        priv->peer.send_size = send_buf_size;

                                gf_log (GF_RDMA_LOG_NAME, GF_LOG_TRACE,
                                        "%s: transacted recv_size=%d "
                                        "send_size=%d",
                                        this->name, priv->peer.recv_size,
                                        priv->peer.send_size);

                                priv->peer.quota = priv->peer.send_count;

                                if (gf_rdma_connect_qp (this)) {
                                        gf_log (GF_RDMA_LOG_NAME,
                                                GF_LOG_ERROR,
                                                "%s: failed to connect with "
                                                "remote QP", this->name);
                                        ret = -1;
                                        goto unlock;
                                }
                                gf_rdma_fill_handshake_ack (buf, &priv->handshake.incoming);
                                buf[0] = 0;
                                priv->handshake.incoming.state = GF_RDMA_HANDSHAKE_RECEIVING_ACK;
                                break;

                        case GF_RDMA_HANDSHAKE_RECEIVING_ACK:
                                ret = __tcp_readv (this,
                                                   &priv->handshake.incoming.vector,
                                                   priv->handshake.incoming.count,
                                                   &priv->handshake.incoming.pending_vector,
                                                   &priv->handshake.incoming.pending_count);
                                if (ret == -1) {
                                        goto unlock;
                                }

                                if (ret > 0) {
                                        gf_log (this->name, GF_LOG_TRACE,
                                                "partial header read on NB "
                                                "socket. continue later");
                                        goto unlock;
                                }

                                if (!ret) {
                                        priv->handshake.incoming.state = GF_RDMA_HANDSHAKE_RECEIVED_ACK;
                                }
                                break;

                        case GF_RDMA_HANDSHAKE_RECEIVED_ACK:
                                if (strncmp (buf, "DONE", 4)) {
                                        gf_log (GF_RDMA_LOG_NAME,
                                                GF_LOG_DEBUG,
                                                "%s: handshake-3 did not "
                                                "return 'DONE' (%s)",
                                                this->name, buf);
                                        ret = -1;
                                        goto unlock;
                                }
                                ret = 0;
                                priv->connected = 1;
                                sock_len = sizeof (struct sockaddr_storage);
                                getpeername (priv->sock,
                                             (struct sockaddr *) &this->peerinfo.sockaddr,
                                             &sock_len);

                                GF_FREE (priv->handshake.incoming.buf);
                                priv->handshake.incoming.buf = NULL;
                                priv->handshake.incoming.state = GF_RDMA_HANDSHAKE_COMPLETE;
                        }
                }
        }
unlock:
        pthread_mutex_unlock (&priv->write_mutex);

        if (ret == -1) {
                rpc_transport_disconnect (this);
        } else {
                ret = 0;
        }


        if (!ret && priv->connected) {
                if (priv->is_server) {
                        ret = rpc_transport_notify (priv->listener,
                                                    RPC_TRANSPORT_ACCEPT,
                                                    this);
                } else {
                        ret = rpc_transport_notify (this, RPC_TRANSPORT_CONNECT,
                                                    this);
                }
        }

        return ret;
}

static int
gf_rdma_handshake_pollout (rpc_transport_t *this)
{
        gf_rdma_private_t *priv = NULL;
        char              *buf  = NULL;
        int32_t            ret  = 0;

        priv = this->private;
        buf = priv->handshake.outgoing.buf;

        if (priv->handshake.outgoing.state == GF_RDMA_HANDSHAKE_COMPLETE) {
                return 0;
        }

        pthread_mutex_unlock (&priv->write_mutex);
        {
                while (priv->handshake.outgoing.state
                       != GF_RDMA_HANDSHAKE_COMPLETE)
                {
                        switch (priv->handshake.outgoing.state)
                        {
                        case GF_RDMA_HANDSHAKE_START:
                                buf = priv->handshake.outgoing.buf
                                        = GF_CALLOC (1, 256, gf_common_mt_char);
                                gf_rdma_fill_handshake_data (buf,
                                                             &priv->handshake.outgoing, priv);
                                priv->handshake.outgoing.state
                                        = GF_RDMA_HANDSHAKE_SENDING_DATA;
                                break;

                        case GF_RDMA_HANDSHAKE_SENDING_DATA:
                                ret = __tcp_writev (this,
                                                    &priv->handshake.outgoing.vector,
                                                    priv->handshake.outgoing.count,
                                                    &priv->handshake.outgoing.pending_vector,
                                                    &priv->handshake.outgoing.pending_count);
                                if (ret == -1) {
                                        goto unlock;
                                }

                                if (ret > 0) {
                                        gf_log (this->name, GF_LOG_TRACE,
                                                "partial header read on NB "
                                                "socket. continue later");
                                        goto unlock;
                                }

                                if (!ret) {
                                        priv->handshake.outgoing.state
                                                = GF_RDMA_HANDSHAKE_SENT_DATA;
                                }
                                break;

                        case GF_RDMA_HANDSHAKE_SENT_DATA:
                                gf_rdma_fill_handshake_ack (buf,
                                                            &priv->handshake.outgoing);
                                priv->handshake.outgoing.state
                                        = GF_RDMA_HANDSHAKE_SENDING_ACK;
                                break;

                        case GF_RDMA_HANDSHAKE_SENDING_ACK:
                                ret = __tcp_writev (this,
                                                    &priv->handshake.outgoing.vector,
                                                    priv->handshake.outgoing.count,
                                                    &priv->handshake.outgoing.pending_vector,
                                                    &priv->handshake.outgoing.pending_count);

                                if (ret == -1) {
                                        goto unlock;
                                }

                                if (ret > 0) {
                                        gf_log (this->name, GF_LOG_TRACE,
                                                "partial header read on NB "
                                                "socket. continue later");
                                        goto unlock;
                                }

                                if (!ret) {
                                        GF_FREE (priv->handshake.outgoing.buf);
                                        priv->handshake.outgoing.buf = NULL;
                                        priv->handshake.outgoing.state
                                                = GF_RDMA_HANDSHAKE_COMPLETE;
                                }
                                break;
                        }
                }
        }
unlock:
        pthread_mutex_unlock (&priv->write_mutex);

        if (ret == -1) {
                rpc_transport_disconnect (this);
        } else {
                ret = 0;
        }

        return ret;
}

static int
gf_rdma_handshake_pollerr (rpc_transport_t *this)
{
        gf_rdma_private_t *priv = this->private;
        char need_unref = 0, connected = 0;

        gf_log (GF_RDMA_LOG_NAME, GF_LOG_DEBUG,
                "%s: peer disconnected, cleaning up",
                this->name);

        pthread_mutex_lock (&priv->write_mutex);
        {
                __gf_rdma_teardown (this);

                connected = priv->connected;
                if (priv->sock != -1) {
                        event_unregister (this->ctx->event_pool,
                                          priv->sock, priv->idx);
                        need_unref = 1;

                        if (close (priv->sock) != 0) {
                                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                        "close () - error: %s",
                                        strerror (errno));
                        }
                        priv->tcp_connected = priv->connected = 0;
                        priv->sock = -1;
                }

                if (priv->handshake.incoming.buf) {
                        GF_FREE (priv->handshake.incoming.buf);
                        priv->handshake.incoming.buf = NULL;
                }

                priv->handshake.incoming.state = GF_RDMA_HANDSHAKE_START;

                if (priv->handshake.outgoing.buf) {
                        GF_FREE (priv->handshake.outgoing.buf);
                        priv->handshake.outgoing.buf = NULL;
                }

                priv->handshake.outgoing.state = GF_RDMA_HANDSHAKE_START;
        }
        pthread_mutex_unlock (&priv->write_mutex);

        if (connected) {
                rpc_transport_notify (this, RPC_TRANSPORT_DISCONNECT, this);
        }

        if (need_unref)
                rpc_transport_unref (this);

        return 0;
}


static int
tcp_connect_finish (rpc_transport_t *this)
{
        gf_rdma_private_t *priv  = NULL;
        int                error = 0, ret = 0;

        priv = this->private;
        pthread_mutex_lock (&priv->write_mutex);
        {
                ret = __tcp_connect_finish (priv->sock);

                if (!ret) {
                        this->myinfo.sockaddr_len =
                                sizeof (this->myinfo.sockaddr);
                        ret = getsockname (priv->sock,
                                           (struct sockaddr *)&this->myinfo.sockaddr,
                                           &this->myinfo.sockaddr_len);
                        if (ret == -1)
                        {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "getsockname on new client-socket %d "
                                        "failed (%s)",
                                        priv->sock, strerror (errno));
                                close (priv->sock);
                                error = 1;
                                goto unlock;
                        }

                        gf_rdma_get_transport_identifiers (this);
                        priv->tcp_connected = 1;
                }

                if (ret == -1 && errno != EINPROGRESS) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "tcp connect to %s failed (%s)",
                                this->peerinfo.identifier, strerror (errno));
                        error = 1;
                }
        }
unlock:
        pthread_mutex_unlock (&priv->write_mutex);

        if (error) {
                rpc_transport_disconnect (this);
        }

        return ret;
}

static int
gf_rdma_event_handler (int fd, int idx, void *data,
                       int poll_in, int poll_out, int poll_err)
{
        rpc_transport_t   *this    = NULL;
        gf_rdma_private_t *priv    = NULL;
        gf_rdma_options_t *options = NULL;
        int                ret     = 0;

        this = data;
        priv = this->private;
        if (!priv->tcp_connected) {
                ret = tcp_connect_finish (this);
                if (priv->tcp_connected) {
                        options = &priv->options;

                        priv->peer.send_count = options->send_count;
                        priv->peer.recv_count = options->recv_count;
                        priv->peer.send_size = options->send_size;
                        priv->peer.recv_size = options->recv_size;

                        if ((ret = gf_rdma_create_qp (this)) < 0) {
                                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                        "%s: could not create QP",
                                        this->name);
                                rpc_transport_disconnect (this);
                        }
                }
        }

        if (!ret && poll_out && priv->tcp_connected) {
                ret = gf_rdma_handshake_pollout (this);
        }

        if (!ret && !poll_err && poll_in && priv->tcp_connected) {
                if (priv->handshake.incoming.state
                    == GF_RDMA_HANDSHAKE_COMPLETE) {
                        gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                                "%s: pollin received on tcp socket (peer: %s) "
                                "after handshake is complete",
                                this->name, this->peerinfo.identifier);
                        gf_rdma_handshake_pollerr (this);
                        return 0;
                }
                ret = gf_rdma_handshake_pollin (this);
        }

        if (ret < 0 || poll_err) {
                ret = gf_rdma_handshake_pollerr (this);
        }

        return 0;
}

static int
__tcp_nonblock (int fd)
{
        int flags = 0;
        int ret   = -1;

        flags = fcntl (fd, F_GETFL);

        if (flags != -1)
                ret = fcntl (fd, F_SETFL, flags | O_NONBLOCK);

        return ret;
}

static int32_t
gf_rdma_connect (struct rpc_transport *this, int port)
{
        gf_rdma_private_t   *priv         = NULL;
        int32_t              ret          = 0;
        gf_boolean_t         non_blocking = 1;
        union gf_sock_union  sock_union   = {{0, }, };
        socklen_t            sockaddr_len = 0;

        priv = this->private;

        ret = gf_rdma_client_get_remote_sockaddr (this,
                                                  &sock_union.sa,
                                                  &sockaddr_len, port);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "cannot get remote address to connect");
                return ret;
        }


        pthread_mutex_lock (&priv->write_mutex);
        {
                if (priv->sock != -1) {
                        ret = 0;
                        goto unlock;
                }

                priv->sock = socket (sock_union.sa.sa_family, SOCK_STREAM, 0);

                if (priv->sock == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "socket () - error: %s", strerror (errno));
                        ret = -errno;
                        goto unlock;
                }

                gf_log (this->name, GF_LOG_TRACE,
                        "socket fd = %d", priv->sock);

                memcpy (&this->peerinfo.sockaddr, &sock_union.storage,
                        sockaddr_len);
                this->peerinfo.sockaddr_len = sockaddr_len;

                if (port > 0)
                        sock_union.sin.sin_port = htons (port);

                ((struct sockaddr *) &this->myinfo.sockaddr)->sa_family =
                        ((struct sockaddr *)&this->peerinfo.sockaddr)->sa_family;

                if (non_blocking)
                {
                        ret = __tcp_nonblock (priv->sock);

                        if (ret == -1)
                        {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "could not set socket %d to non "
                                        "blocking mode (%s)",
                                        priv->sock, strerror (errno));
                                close (priv->sock);
                                priv->sock = -1;
                                goto unlock;
                        }
                }

                ret = gf_rdma_client_bind (this,
                                           (struct sockaddr *)&this->myinfo.sockaddr,
                                           &this->myinfo.sockaddr_len,
                                           priv->sock);
                if (ret == -1)
                {
                        gf_log (this->name, GF_LOG_WARNING,
                                "client bind failed: %s", strerror (errno));
                        close (priv->sock);
                        priv->sock = -1;
                        goto unlock;
                }

                ret = connect (priv->sock,
                               (struct sockaddr *)&this->peerinfo.sockaddr,
                               this->peerinfo.sockaddr_len);
                if (ret == -1 && errno != EINPROGRESS)
                {
                        gf_log (this->name, GF_LOG_ERROR,
                                "connection attempt failed (%s)",
                                strerror (errno));
                        close (priv->sock);
                        priv->sock = -1;
                        goto unlock;
                }

                priv->tcp_connected = priv->connected = 0;

                rpc_transport_ref (this);

                priv->handshake.incoming.state = GF_RDMA_HANDSHAKE_START;
                priv->handshake.outgoing.state = GF_RDMA_HANDSHAKE_START;

                priv->idx = event_register (this->ctx->event_pool,
                                            priv->sock, gf_rdma_event_handler,
                                            this, 1, 1);
        }
unlock:
        pthread_mutex_unlock (&priv->write_mutex);

        return ret;
}

static int
gf_rdma_server_event_handler (int fd, int idx, void *data,
                              int poll_in, int poll_out, int poll_err)
{
        int32_t            main_sock    = -1;
        rpc_transport_t   *this         = NULL, *trans = NULL;
        gf_rdma_private_t *priv         = NULL;
        gf_rdma_private_t *trans_priv   = NULL;
        gf_rdma_options_t *options      = NULL;

        if (!poll_in) {
                return 0;
        }

        trans = data;
        trans_priv = (gf_rdma_private_t *) trans->private;

        this = GF_CALLOC (1, sizeof (rpc_transport_t),
                          gf_common_mt_rpc_transport_t);
        if (this == NULL) {
                return -1;
        }

        this->listener = trans;

        priv = GF_CALLOC (1, sizeof (gf_rdma_private_t),
                          gf_common_mt_rdma_private_t);
        if (priv == NULL) {
                GF_FREE (priv);
                return -1;
        }
        this->private = priv;
        /* Copy all the rdma related values in priv, from trans_priv
           as other than QP, all the values remain same */
        priv->device = trans_priv->device;
        priv->options = trans_priv->options;
        priv->is_server = 1;
        priv->listener = trans;

        options = &priv->options;

        this->ops = trans->ops;
        this->init = trans->init;
        this->fini = trans->fini;
        this->ctx = trans->ctx;
        this->name = gf_strdup (trans->name);
        this->notify = trans->notify;
        this->mydata = trans->mydata;

        memcpy (&this->myinfo.sockaddr, &trans->myinfo.sockaddr,
                trans->myinfo.sockaddr_len);
        this->myinfo.sockaddr_len = trans->myinfo.sockaddr_len;

        main_sock = (trans_priv)->sock;
        this->peerinfo.sockaddr_len = sizeof (this->peerinfo.sockaddr);
        priv->sock = accept (main_sock,
                             (struct sockaddr *)&this->peerinfo.sockaddr,
                             &this->peerinfo.sockaddr_len);
        if (priv->sock == -1) {
                gf_log ("rdma/server", GF_LOG_ERROR,
                        "accept() failed: %s",
                        strerror (errno));
                GF_FREE (this->private);
                GF_FREE (this);
                return -1;
        }

        priv->peer.trans = this;
        rpc_transport_ref (this);

        gf_rdma_get_transport_identifiers (this);

        priv->tcp_connected = 1;
        priv->handshake.incoming.state = GF_RDMA_HANDSHAKE_START;
        priv->handshake.outgoing.state = GF_RDMA_HANDSHAKE_START;

        priv->peer.send_count = options->send_count;
        priv->peer.recv_count = options->recv_count;
        priv->peer.send_size = options->send_size;
        priv->peer.recv_size = options->recv_size;
        INIT_LIST_HEAD (&priv->peer.ioq);

        if (gf_rdma_create_qp (this) < 0) {
                gf_log (GF_RDMA_LOG_NAME, GF_LOG_ERROR,
                        "%s: could not create QP",
                        this->name);
                rpc_transport_disconnect (this);
                return -1;
        }

        priv->idx = event_register (this->ctx->event_pool, priv->sock,
                                    gf_rdma_event_handler, this, 1, 1);

        pthread_mutex_init (&priv->read_mutex, NULL);
        pthread_mutex_init (&priv->write_mutex, NULL);
        pthread_mutex_init (&priv->recv_mutex, NULL);
        /*  pthread_cond_init (&priv->recv_cond, NULL); */
        return 0;
}

static int32_t
gf_rdma_listen (rpc_transport_t *this)
{
        union gf_sock_union  sock_union   = {{0, }, };
        socklen_t            sockaddr_len = 0;
        gf_rdma_private_t   *priv         = NULL;
        int                  opt          = 1, ret = 0;
        char                 service[NI_MAXSERV], host[NI_MAXHOST];

        priv = this->private;
        memset (&sock_union, 0, sizeof (sock_union));
        ret = gf_rdma_server_get_local_sockaddr (this,
                                                 &sock_union.sa,
                                                 &sockaddr_len);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "cannot find network address of server to bind to");
                goto err;
        }

        priv->sock = socket (sock_union.sa.sa_family, SOCK_STREAM, 0);
        if (priv->sock == -1) {
                gf_log ("rdma/server", GF_LOG_CRITICAL,
                        "init: failed to create socket, error: %s",
                        strerror (errno));
                GF_FREE (this->private);
                ret = -1;
                goto err;
        }

        memcpy (&this->myinfo.sockaddr, &sock_union.storage, sockaddr_len);
        this->myinfo.sockaddr_len = sockaddr_len;

        ret = getnameinfo ((struct sockaddr *)&this->myinfo.sockaddr,
                           this->myinfo.sockaddr_len,
                           host, sizeof (host),
                           service, sizeof (service),
                           NI_NUMERICHOST);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "getnameinfo failed (%s)", gai_strerror (ret));
                goto err;
        }
        sprintf (this->myinfo.identifier, "%s:%s", host, service);

        setsockopt (priv->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));
        if (bind (priv->sock, &sock_union.sa, sockaddr_len) != 0) {
                ret = -1;
                gf_log ("rdma/server", GF_LOG_ERROR,
                        "init: failed to bind to socket for %s (%s)",
                        this->myinfo.identifier, strerror (errno));
                goto err;
        }

        if (listen (priv->sock, 10) != 0) {
                gf_log ("rdma/server", GF_LOG_ERROR,
                        "init: listen () failed on socket for %s (%s)",
                        this->myinfo.identifier, strerror (errno));
                ret = -1;
                goto err;
        }

        /* Register the main socket */
        priv->idx = event_register (this->ctx->event_pool, priv->sock,
                                    gf_rdma_server_event_handler,
                                    rpc_transport_ref (this), 1, 0);

err:
        return ret;
}

struct rpc_transport_ops tops = {
        .submit_request = gf_rdma_submit_request,
        .submit_reply   = gf_rdma_submit_reply,
        .connect        = gf_rdma_connect,
        .disconnect     = gf_rdma_disconnect,
        .listen         = gf_rdma_listen,
};

int32_t
init (rpc_transport_t *this)
{
        gf_rdma_private_t *priv = NULL;

        priv = GF_CALLOC (1, sizeof (*priv), gf_common_mt_rdma_private_t);
        if (!priv)
                return -1;

        this->private = priv;
        priv->sock = -1;

        if (gf_rdma_init (this)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to initialize IB Device");
                return -1;
        }

        return 0;
}

void
fini (struct rpc_transport *this)
{
        /* TODO: verify this function does graceful finish */
        gf_rdma_private_t *priv = NULL;

        priv = this->private;

        this->private = NULL;

        if (priv) {
                pthread_mutex_destroy (&priv->recv_mutex);
                pthread_mutex_destroy (&priv->write_mutex);
                pthread_mutex_destroy (&priv->read_mutex);

                /*  pthread_cond_destroy (&priv->recv_cond); */
                if (priv->sock != -1) {
                        event_unregister (this->ctx->event_pool,
                                          priv->sock, priv->idx);
                }

                gf_log (this->name, GF_LOG_TRACE,
                        "called fini on transport: %p", this);
                GF_FREE (priv);
        }
        return;
}

/* TODO: expand each option */
struct volume_options options[] = {
        { .key   = {"transport.rdma.port",
                    "rdma-port"},
          .type  = GF_OPTION_TYPE_INT,
          .min   = 1,
          .max   = 4,
          .description = "check the option by 'ibv_devinfo'"
        },
        { .key   = {"transport.rdma.mtu",
                    "rdma-mtu"},
          .type  = GF_OPTION_TYPE_INT,
        },
        { .key   = {"transport.rdma.device-name",
                    "rdma-device-name"},
          .type  = GF_OPTION_TYPE_ANY,
          .description = "check by 'ibv_devinfo'"
        },
        { .key   = {"transport.rdma.work-request-send-count",
                    "rdma-work-request-send-count"},
          .type  = GF_OPTION_TYPE_INT,
        },
        { .key   = {"transport.rdma.work-request-recv-count",
                    "rdma-work-request-recv-count"},
          .type  = GF_OPTION_TYPE_INT,
        },
        { .key   = {"remote-port",
                    "transport.remote-port",
                    "transport.rdma.remote-port"},
          .type  = GF_OPTION_TYPE_INT
        },
        { .key   = {"transport.rdma.listen-port", "listen-port"},
          .type  = GF_OPTION_TYPE_INT
        },
        { .key   = {"transport.rdma.connect-path", "connect-path"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"transport.rdma.bind-path", "bind-path"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"transport.rdma.listen-path", "listen-path"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"transport.address-family",
                    "address-family"},
          .value = {"inet", "inet6", "inet/inet6", "inet6/inet",
                    "unix", "inet-sdp" },
          .type  = GF_OPTION_TYPE_STR
        },
        { .key   = {"transport.socket.lowlat"},
          .type  = GF_OPTION_TYPE_BOOL
        },
        { .key = {NULL} }
};
