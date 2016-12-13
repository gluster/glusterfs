/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "dict.h"
#include "glusterfs.h"
#include "iobuf.h"
#include "logging.h"
#include "rdma.h"
#include "name.h"
#include "byte-order.h"
#include "xlator.h"
#include "xdr-rpc.h"
#include "rpc-lib-messages.h"
#include "rpc-trans-rdma-messages.h"
#include <signal.h>

#define GF_RDMA_LOG_NAME "rpc-transport/rdma"

static int32_t
__gf_rdma_ioq_churn (gf_rdma_peer_t *peer);

gf_rdma_post_t *
gf_rdma_post_ref (gf_rdma_post_t *post);

int
gf_rdma_post_unref (gf_rdma_post_t *post);

static void *
gf_rdma_send_completion_proc (void *data);

static void *
gf_rdma_recv_completion_proc (void *data);

void *
gf_rdma_async_event_thread (void *context);

static int32_t
gf_rdma_create_qp (rpc_transport_t *this);

static int32_t
__gf_rdma_teardown (rpc_transport_t *this);

static int32_t
gf_rdma_teardown (rpc_transport_t *this);

static int32_t
gf_rdma_disconnect (rpc_transport_t *this, gf_boolean_t wait);

static void
gf_rdma_cm_handle_disconnect (rpc_transport_t *this);

static int
gf_rdma_cm_handle_connect_init (struct rdma_cm_event *event);

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
gf_rdma_new_post (rpc_transport_t *this, gf_rdma_device_t *device, int32_t len,
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
                gf_msg_nomem (GF_RDMA_LOG_NAME, GF_LOG_ERROR, len);
                goto out;
        }

        post->mr = ibv_reg_mr (device->pd,
                               post->buf,
                               post->buf_size,
                               IBV_ACCESS_LOCAL_WRITE);
        if (!post->mr) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        RDMA_MSG_MR_ALOC_FAILED,
                        "memory registration failed");
                goto out;
        }

        post->device = device;
        post->type = type;

        ret = 0;
out:
        if (ret != 0) {
                free (post->buf);

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

        priv = this->private;

        if (priv->connected) {
                rdma_disconnect (priv->peer.cm_id);
        }

        return 0;
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


static void
__gf_rdma_destroy_queue (gf_rdma_post_t *post)
{
        gf_rdma_post_t *tmp = NULL;

        while (post->next != post) {
                tmp = post->next;

                post->next = post->next->next;
                post->next->prev = post;

                gf_rdma_destroy_post (tmp);
        }
}


static void
gf_rdma_destroy_queue (gf_rdma_queue_t *queue)
{
        if (queue == NULL) {
                goto out;
        }

        pthread_mutex_lock (&queue->lock);
        {
                if (queue->passive_count > 0) {
                        __gf_rdma_destroy_queue (&queue->passive_posts);
                        queue->passive_count = 0;
                }

                if (queue->active_count > 0) {
                        __gf_rdma_destroy_queue (&queue->active_posts);
                        queue->active_count = 0;
                }
        }
        pthread_mutex_unlock (&queue->lock);

out:
        return;
}


static void
gf_rdma_destroy_posts (rpc_transport_t *this)
{
        gf_rdma_device_t  *device = NULL;
        gf_rdma_private_t *priv   = NULL;

        if (this == NULL) {
                goto out;
        }

        priv = this->private;
        device = priv->device;

        gf_rdma_destroy_queue (&device->sendq);
        gf_rdma_destroy_queue (&device->recvq);

out:
        return;
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

        for (i = 0 ; i < count ; i++) {
                gf_rdma_post_t *post = NULL;

                post = gf_rdma_new_post (this, device, size + 2048, type);
                if (!post) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_POST_CREATION_FAILED,
                                "post creation failed");
                        ret = -1;
                        break;
                }

                gf_rdma_put_post (q, post);
        }
        return ret;
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

static void
gf_rdma_deregister_iobuf_pool (gf_rdma_device_t *device)
{

        gf_rdma_arena_mr   *arena_mr  = NULL;
        gf_rdma_arena_mr   *tmp       = NULL;

        while (device) {
                if (!list_empty(&device->all_mr)) {
                        list_for_each_entry_safe (arena_mr, tmp,
                                                  &device->all_mr, list) {
                                if (ibv_dereg_mr(arena_mr->mr)) {
                                        gf_msg ("rdma", GF_LOG_WARNING, 0,
                                                RDMA_MSG_DEREGISTER_ARENA_FAILED,
                                                "deallocation of memory region "
                                                "failed");
                                        return;
                                }
                                list_del(&arena_mr->list);
                                GF_FREE(arena_mr);
                        }
                }
                device = device->next;
        }
}
int
gf_rdma_deregister_arena (struct list_head **mr_list,
                          struct iobuf_arena *iobuf_arena)
{
        gf_rdma_arena_mr *tmp     = NULL;
        gf_rdma_arena_mr *dummy   = NULL;
        int               count   = 0, i = 0;

        count = iobuf_arena->iobuf_pool->rdma_device_count;
        for (i = 0; i < count; i++) {
                list_for_each_entry_safe (tmp, dummy, mr_list[i], list) {
                        if (tmp->iobuf_arena == iobuf_arena) {
                                if (ibv_dereg_mr(tmp->mr)) {
                                        gf_msg ("rdma", GF_LOG_WARNING, 0,
                                               RDMA_MSG_DEREGISTER_ARENA_FAILED,
                                                "deallocation of memory region "
                                                "failed");
                                        return -1;
                                }
                                list_del(&tmp->list);
                                GF_FREE(tmp);
                                break;
                        }
                }
        }

        return 0;
}


int
gf_rdma_register_arena (void **arg1, void *arg2)
{
        struct ibv_mr       *mr          = NULL;
        gf_rdma_arena_mr    *new         = NULL;
        struct iobuf_pool   *iobuf_pool  = NULL;
        gf_rdma_device_t    **device     = (gf_rdma_device_t **)arg1;
        struct iobuf_arena  *iobuf_arena = arg2;
        int                  count       = 0, i = 0;

        iobuf_pool = iobuf_arena->iobuf_pool;
        count = iobuf_pool->rdma_device_count;
        for (i = 0; i < count; i++) {
                new = GF_CALLOC(1, sizeof(gf_rdma_arena_mr),
                                gf_common_mt_rdma_arena_mr);
                if (new == NULL) {
                        gf_msg ("rdma", GF_LOG_INFO, ENOMEM,
                                RDMA_MSG_MR_ALOC_FAILED, "Out of "
                                "memory: registering pre allocated buffer "
                                "with rdma device failed.");
                      return -1;
                }
                INIT_LIST_HEAD (&new->list);
                new->iobuf_arena = iobuf_arena;

                mr = ibv_reg_mr(device[i]->pd, iobuf_arena->mem_base,
                                         iobuf_arena->arena_size,
                                         IBV_ACCESS_REMOTE_READ |
                                         IBV_ACCESS_LOCAL_WRITE |
                                         IBV_ACCESS_REMOTE_WRITE
                                         );
                if (!mr)
                        gf_msg ("rdma", GF_LOG_WARNING, 0,
                                RDMA_MSG_MR_ALOC_FAILED, "allocation of mr "
                                "failed");

                new->mr = mr;
                list_add (&new->list, &device[i]->all_mr);
                new = NULL;
        }

        return 0;

}

static void
gf_rdma_register_iobuf_pool (gf_rdma_device_t *device,
                        struct iobuf_pool *iobuf_pool)
{
        struct iobuf_arena  *tmp        = NULL;
        struct iobuf_arena  *dummy      = NULL;
        struct ibv_mr       *mr         = NULL;
        gf_rdma_arena_mr    *new        = NULL;

        if (!list_empty(&iobuf_pool->all_arenas)) {

                list_for_each_entry_safe (tmp, dummy, &iobuf_pool->all_arenas,
                                          all_list) {
                        new = GF_CALLOC(1, sizeof(gf_rdma_arena_mr),
                                        gf_common_mt_rdma_arena_mr);
                        if (new == NULL) {
                                gf_msg ("rdma", GF_LOG_INFO, ENOMEM,
                                        RDMA_MSG_MR_ALOC_FAILED, "Out of "
                                        "memory: registering pre allocated "
                                        "buffer with rdma device failed.");
                              return;
                        }
                        INIT_LIST_HEAD (&new->list);
                        new->iobuf_arena = tmp;

                        mr = ibv_reg_mr(device->pd, tmp->mem_base,
                                        tmp->arena_size,
                                        IBV_ACCESS_REMOTE_READ |
                                        IBV_ACCESS_LOCAL_WRITE |
                                        IBV_ACCESS_REMOTE_WRITE);
                        if (!mr) {
                                gf_msg ("rdma", GF_LOG_WARNING, 0,
                                        RDMA_MSG_MR_ALOC_FAILED, "failed"
                                        " to pre register buffers with rdma "
                                        "devices.");

                        }
                        new->mr = mr;
                        list_add (&new->list, &device->all_mr);

                        new = NULL;
                }
        }

       return;
}

static void
gf_rdma_register_iobuf_pool_with_device (gf_rdma_device_t *device,
                                         struct iobuf_pool *iobuf_pool)
{
        while (device) {
                gf_rdma_register_iobuf_pool (device, iobuf_pool);
                device = device->next;
        }
}

static struct ibv_mr*
gf_rdma_get_pre_registred_mr(rpc_transport_t *this, void *ptr, int size)
{
        gf_rdma_arena_mr   *tmp        = NULL;
        gf_rdma_arena_mr   *dummy      = NULL;
        gf_rdma_private_t  *priv       = NULL;
        gf_rdma_device_t   *device     = NULL;

        priv = this->private;
        device = priv->device;

        if (!list_empty(&device->all_mr)) {
                list_for_each_entry_safe (tmp, dummy, &device->all_mr, list) {
                        if (tmp->iobuf_arena->mem_base <= ptr &&
                            ptr < tmp->iobuf_arena->mem_base +
                            tmp->iobuf_arena->arena_size)
                                return tmp->mr;
                        }
        }

        return NULL;
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
                for (i = 0 ; i < options->recv_count ; i++) {
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        RDMA_MSG_CQ_CREATION_FAILED, "creation of CQ for "
                        "device %s failed", device->device_name);
                ret = -1;
                goto out;
        } else if (ibv_req_notify_cq (device->recv_cq, 0)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        RDMA_MSG_REQ_NOTIFY_CQ_REVQ_FAILED, "ibv_req_notify_"
                        "cq on recv CQ of device %s failed",
                        device->device_name);
                ret = -1;
                goto out;
        }

        do {
                ret = ibv_query_device (priv->device->context, &device_attr);
                if (ret != 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_QUERY_DEVICE_FAILED, "ibv_query_"
                                "device on %s returned %d (%s)",
                                priv->device->device_name, ret,
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_CQ_CREATION_FAILED,
                                "creation of send_cq "
                                "for device %s failed", device->device_name);
                        ret = -1;
                        goto out;
                }

                if (ibv_req_notify_cq (device->send_cq, 0)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_REQ_NOTIFY_CQ_SENDQ_FAILED,
                                "ibv_req_notify_cq on send_cq for device %s"
                                " failed",  device->device_name);
                        ret = -1;
                        goto out;
                }
        } while (0);

out:
        if (ret != 0)
                gf_rdma_destroy_cq (this);

        return ret;
}


static gf_rdma_device_t *
gf_rdma_get_device (rpc_transport_t *this, struct ibv_context *ibctx,
                    char *device_name)
{
        glusterfs_ctx_t   *ctx      = NULL;
        gf_rdma_private_t *priv     = NULL;
        gf_rdma_options_t *options  = NULL;
        int32_t            ret      = 0;
        int32_t            i        = 0;
        gf_rdma_device_t  *trav     = NULL, *device = NULL;
        gf_rdma_ctx_t     *rdma_ctx = NULL;
        struct iobuf_pool *iobuf_pool = NULL;

        priv        = this->private;
        options     = &priv->options;
        ctx         = this->ctx;
        rdma_ctx    = ctx->ib;
        iobuf_pool = ctx->iobuf_pool;

        trav = rdma_ctx->device;

        while (trav) {
                if (!strcmp (trav->device_name, device_name))
                        break;
                trav = trav->next;
        }

        if (!trav) {
                trav = GF_CALLOC (1, sizeof (*trav),
                                  gf_common_mt_rdma_device_t);
                if (trav == NULL) {
                        goto out;
                }
                priv->device = trav;
                trav->context = ibctx;

                trav->next = rdma_ctx->device;
                rdma_ctx->device = trav;

                iobuf_pool->device[iobuf_pool->rdma_device_count] = trav;
                iobuf_pool->mr_list[iobuf_pool->rdma_device_count++] = &trav->all_mr;
                trav->request_ctx_pool
                        = mem_pool_new (gf_rdma_request_context_t,
                                        GF_RDMA_POOL_SIZE);
                if (trav->request_ctx_pool == NULL) {
                        goto out;
                }

                trav->ioq_pool
                        = mem_pool_new (gf_rdma_ioq_t, GF_RDMA_POOL_SIZE);
                if (trav->ioq_pool == NULL) {
                        goto out;
                }

                trav->reply_info_pool = mem_pool_new (gf_rdma_reply_info_t,
                                                      GF_RDMA_POOL_SIZE);
                if (trav->reply_info_pool == NULL) {
                        goto out;
                }

                trav->device_name = gf_strdup (device_name);

                trav->send_chan = ibv_create_comp_channel (trav->context);
                if (!trav->send_chan) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_SEND_COMP_CHAN_FAILED, "could not "
                                "create send completion channel for "
                                "device (%s)", device_name);
                        goto out;
                }

                trav->recv_chan = ibv_create_comp_channel (trav->context);
                if (!trav->recv_chan) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_RECV_COMP_CHAN_FAILED, "could not "
                                "create recv completion channel for "
                                "device (%s)", device_name);

                        /* TODO: cleanup current mess */
                        goto out;
                }

                if (gf_rdma_create_cq (this) < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_CQ_CREATION_FAILED,
                                "could not create CQ for device (%s)",
                                device_name);
                        goto out;
                }

                /* protection domain */
                trav->pd = ibv_alloc_pd (trav->context);

                if (!trav->pd) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_ALOC_PROT_DOM_FAILED, "could not "
                                "allocate protection domain for device (%s)",
                                device_name);
                        goto out;
                }

                struct ibv_srq_init_attr attr = {
                        .attr = {
                                .max_wr = options->recv_count,
                                .max_sge = 1,
                                .srq_limit = 10
                        }
                };
                trav->srq = ibv_create_srq (trav->pd, &attr);

                if (!trav->srq) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_CRE_SRQ_FAILED, "could not create SRQ"
                                " for device (%s)",
                                device_name);
                        goto out;
                }

                /* queue init */
                gf_rdma_queue_init (&trav->sendq);
                gf_rdma_queue_init (&trav->recvq);

                INIT_LIST_HEAD (&trav->all_mr);
                gf_rdma_register_iobuf_pool(trav, iobuf_pool);

                if (gf_rdma_create_posts (this) < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_ALOC_POST_FAILED, "could not allocate"
                                "posts for device (%s)", device_name);
                        goto out;
                }

                /* completion threads */
                ret = gf_thread_create (&trav->send_thread, NULL,
                                        gf_rdma_send_completion_proc,
                                        trav->send_chan);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_SEND_COMP_THREAD_FAILED,
                                "could not create send completion thread for "
                                "device (%s)", device_name);
                        goto out;
                }

                ret = gf_thread_create (&trav->recv_thread, NULL,
                                         gf_rdma_recv_completion_proc,
                                         trav->recv_chan);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_RECV_COMP_THREAD_FAILED,
                                "could not create recv completion thread "
                                "for device (%s)", device_name);
                        return NULL;
                }

                ret = gf_thread_create (&trav->async_event_thread, NULL,
                                         gf_rdma_async_event_thread,
                                         ibctx);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                RDMA_MSG_ASYNC_EVENT_THEAD_FAILED,
                                "could not create async_event_thread");
                        return NULL;
                }

                /* qpreg */
                pthread_mutex_init (&trav->qpreg.lock, NULL);
                for (i = 0; i < 42; i++) {
                        trav->qpreg.ents[i].next = &trav->qpreg.ents[i];
                        trav->qpreg.ents[i].prev = &trav->qpreg.ents[i];
                }
        }

        device = trav;
        trav = NULL;
out:

        if (trav != NULL) {
                rdma_ctx->device = trav->next;
                gf_rdma_destroy_posts (this);
                mem_pool_destroy (trav->ioq_pool);
                mem_pool_destroy (trav->request_ctx_pool);
                mem_pool_destroy (trav->reply_info_pool);
                if (trav->pd != NULL) {
                        ibv_dealloc_pd (trav->pd);
                }
                gf_rdma_destroy_cq (this);
                ibv_destroy_comp_channel (trav->recv_chan);
                ibv_destroy_comp_channel (trav->send_chan);
                GF_FREE ((char *)trav->device_name);
                GF_FREE (trav);
        }

        return device;
}


static rpc_transport_t *
gf_rdma_transport_new (rpc_transport_t *listener, struct rdma_cm_id *cm_id)
{
        gf_rdma_private_t *listener_priv = NULL, *priv = NULL;
        rpc_transport_t   *this          = NULL, *new = NULL;
        gf_rdma_options_t *options       = NULL;
        char              *device_name   = NULL;

        listener_priv = listener->private;

        this = GF_CALLOC (1, sizeof (rpc_transport_t),
                          gf_common_mt_rpc_transport_t);
        if (this == NULL) {
                goto out;
        }

        this->listener = listener;

        priv = GF_CALLOC (1, sizeof (gf_rdma_private_t),
                          gf_common_mt_rdma_private_t);
        if (priv == NULL) {
                goto out;
        }

        this->private = priv;
        priv->options = listener_priv->options;

        priv->listener = listener;
        priv->entity = GF_RDMA_SERVER;

        options = &priv->options;

        this->ops = listener->ops;
        this->init = listener->init;
        this->fini = listener->fini;
        this->ctx = listener->ctx;
        this->name = gf_strdup (listener->name);
        this->notify = listener->notify;
        this->mydata = listener->mydata;
        this->xl = listener->xl;

        this->myinfo.sockaddr_len = sizeof (cm_id->route.addr.src_addr);
        memcpy (&this->myinfo.sockaddr, &cm_id->route.addr.src_addr,
                this->myinfo.sockaddr_len);

        this->peerinfo.sockaddr_len = sizeof (cm_id->route.addr.dst_addr);
        memcpy (&this->peerinfo.sockaddr, &cm_id->route.addr.dst_addr,
                this->peerinfo.sockaddr_len);

        priv->peer.trans = this;
        gf_rdma_get_transport_identifiers (this);

        device_name = (char *)ibv_get_device_name (cm_id->verbs->device);
        if (device_name == NULL) {
                gf_msg (listener->name, GF_LOG_WARNING, 0,
                        RDMA_MSG_GET_DEVICE_NAME_FAILED, "cannot get device "
                        "name (peer:%s me:%s)", this->peerinfo.identifier,
                        this->myinfo.identifier);
                goto out;
        }

        priv->device = gf_rdma_get_device (this, cm_id->verbs,
                                           device_name);
        if (priv->device == NULL) {
                gf_msg (listener->name, GF_LOG_WARNING, 0,
                        RDMA_MSG_GET_IB_DEVICE_FAILED, "cannot get infiniband"
                        " device %s (peer:%s me:%s)", device_name,
                        this->peerinfo.identifier, this->myinfo.identifier);
                goto out;
        }

        priv->peer.send_count = options->send_count;
        priv->peer.recv_count = options->recv_count;
        priv->peer.send_size = options->send_size;
        priv->peer.recv_size = options->recv_size;
        priv->peer.cm_id = cm_id;
        INIT_LIST_HEAD (&priv->peer.ioq);

        pthread_mutex_init (&priv->write_mutex, NULL);
        pthread_mutex_init (&priv->recv_mutex, NULL);

        cm_id->context = this;

        new = rpc_transport_ref (this);
        this = NULL;
out:
        if (this != NULL) {
                if (this->private != NULL) {
                        GF_FREE (this->private);
                }

                if (this->name != NULL) {
                        GF_FREE (this->name);
                }

                GF_FREE (this);
        }

        return new;
}


static int
gf_rdma_cm_handle_connect_request (struct rdma_cm_event *event)
{
        int                     ret         = -1;
        rpc_transport_t        *this        = NULL, *listener = NULL;
        struct rdma_cm_id      *child_cm_id = NULL, *listener_cm_id = NULL;
        struct rdma_conn_param  conn_param  = {0, };
        gf_rdma_private_t      *priv        = NULL;
        gf_rdma_options_t      *options     = NULL;

        child_cm_id = event->id;
        listener_cm_id = event->listen_id;

        listener = listener_cm_id->context;
        priv = listener->private;
        options = &priv->options;

        this = gf_rdma_transport_new (listener, child_cm_id);
        if (this == NULL) {
                gf_msg (listener->name, GF_LOG_WARNING, 0,
                        RDMA_MSG_CREAT_INC_TRANS_FAILED, "could not create "
                        "a transport for incoming connection"
                        " (me.name:%s me.identifier:%s)", listener->name,
                        listener->myinfo.identifier);
                rdma_destroy_id (child_cm_id);
                goto out;
        }

        gf_msg_trace (listener->name, 0, "got a connect request (me:%s peer:"
                      "%s)", listener->myinfo.identifier,
                      this->peerinfo.identifier);

        ret = gf_rdma_create_qp (this);
        if (ret < 0) {
                gf_msg (listener->name, GF_LOG_WARNING, 0,
                        RDMA_MSG_CREAT_QP_FAILED, "could not create QP "
                        "(peer:%s me:%s)", this->peerinfo.identifier,
                        this->myinfo.identifier);
                gf_rdma_cm_handle_disconnect (this);
                goto out;
        }

        conn_param.responder_resources = 1;
        conn_param.initiator_depth = 1;
        conn_param.retry_count = options->attr_retry_cnt;
        conn_param.rnr_retry_count = options->attr_rnr_retry;

        ret = rdma_accept(child_cm_id, &conn_param);
        if (ret < 0) {
                gf_msg (listener->name, GF_LOG_WARNING, errno,
                        RDMA_MSG_ACCEPT_FAILED, "rdma_accept failed peer:%s "
                        "me:%s", this->peerinfo.identifier,
                        this->myinfo.identifier);
                gf_rdma_cm_handle_disconnect (this);
                goto out;
        }
        gf_rdma_cm_handle_connect_init (event);
        ret = 0;

out:
        return ret;
}


static int
gf_rdma_cm_handle_route_resolved (struct rdma_cm_event *event)
{
        struct rdma_conn_param  conn_param = {0, };
        int                     ret        = 0;
        rpc_transport_t        *this       = NULL;
        gf_rdma_private_t      *priv       = NULL;
        gf_rdma_peer_t         *peer       = NULL;
        gf_rdma_options_t      *options    = NULL;

        if (event == NULL) {
                goto out;
        }

        this = event->id->context;

        priv = this->private;
        peer = &priv->peer;
        options = &priv->options;

        ret = gf_rdma_create_qp (this);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        RDMA_MSG_CREAT_QP_FAILED, "could not create QP "
                        "(peer:%s me:%s)", this->peerinfo.identifier,
                        this->myinfo.identifier);
                gf_rdma_cm_handle_disconnect (this);
                goto out;
        }

        memset(&conn_param, 0, sizeof conn_param);
        conn_param.responder_resources = 1;
        conn_param.initiator_depth = 1;
        conn_param.retry_count = options->attr_retry_cnt;
        conn_param.rnr_retry_count = options->attr_rnr_retry;

        ret = rdma_connect(peer->cm_id, &conn_param);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        RDMA_MSG_CONNECT_FAILED,
                        "rdma_connect failed");
                gf_rdma_cm_handle_disconnect (this);
                goto out;
        }

        gf_msg_trace (this->name, 0, "route resolved (me:%s peer:%s)",
                      this->myinfo.identifier, this->peerinfo.identifier);

        ret = 0;
out:
        return ret;
}


static int
gf_rdma_cm_handle_addr_resolved (struct rdma_cm_event *event)
{
        rpc_transport_t   *this = NULL;
        gf_rdma_peer_t    *peer = NULL;
        gf_rdma_private_t *priv = NULL;
        int                ret  = 0;

        this = event->id->context;

        priv = this->private;
        peer = &priv->peer;

        GF_ASSERT (peer->cm_id == event->id);

        this->myinfo.sockaddr_len = sizeof (peer->cm_id->route.addr.src_addr);
        memcpy (&this->myinfo.sockaddr, &peer->cm_id->route.addr.src_addr,
                this->myinfo.sockaddr_len);

        this->peerinfo.sockaddr_len = sizeof (peer->cm_id->route.addr.dst_addr);
        memcpy (&this->peerinfo.sockaddr, &peer->cm_id->route.addr.dst_addr,
                this->peerinfo.sockaddr_len);

        gf_rdma_get_transport_identifiers (this);

        ret = rdma_resolve_route(peer->cm_id, 2000);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        RDMA_MSG_ROUTE_RESOLVE_FAILED, "rdma_resolve_route "
                        "failed (me:%s peer:%s)",
                        this->myinfo.identifier, this->peerinfo.identifier);
                gf_rdma_cm_handle_disconnect (this);
        }

        gf_msg_trace (this->name, 0, "Address resolved (me:%s peer:%s)",
                      this->myinfo.identifier, this->peerinfo.identifier);

        return ret;
}


static void
gf_rdma_cm_handle_disconnect (rpc_transport_t *this)
{
        gf_rdma_private_t *priv       = NULL;
        char               need_unref = 0;

        priv = this->private;
        gf_msg_debug (this->name, 0, "peer disconnected, cleaning up");

        pthread_mutex_lock (&priv->write_mutex);
        {
                if (priv->peer.cm_id != NULL) {
                        need_unref = 1;
                        priv->connected = 0;
                }

                __gf_rdma_teardown (this);
        }
        pthread_mutex_unlock (&priv->write_mutex);

        rpc_transport_notify (this, RPC_TRANSPORT_DISCONNECT, this);

        if (need_unref)
                rpc_transport_unref (this);

}


static int
gf_rdma_cm_handle_connect_init (struct rdma_cm_event *event)
{
        rpc_transport_t   *this  = NULL;
        gf_rdma_private_t *priv  = NULL;
        struct rdma_cm_id *cm_id = NULL;
        int                ret   = 0;

        cm_id = event->id;
        this = cm_id->context;
        priv = this->private;

        if (priv->connected == 1) {
                gf_msg_trace (this->name, 0, "received event "
                              "RDMA_CM_EVENT_ESTABLISHED (me:%s peer:%s)",
                              this->myinfo.identifier,
                              this->peerinfo.identifier);
                return ret;
        }

        priv->connected = 1;

        pthread_mutex_lock (&priv->write_mutex);
        {
                priv->peer.quota = 1;
                priv->peer.quota_set = 0;
        }
        pthread_mutex_unlock (&priv->write_mutex);

        if (priv->entity == GF_RDMA_CLIENT) {
                gf_msg_trace (this->name, 0, "received event "
                              "RDMA_CM_EVENT_ESTABLISHED (me:%s peer:%s)",
                              this->myinfo.identifier,
                              this->peerinfo.identifier);
                ret = rpc_transport_notify (this, RPC_TRANSPORT_CONNECT, this);

        } else if (priv->entity == GF_RDMA_SERVER) {
                ret = rpc_transport_notify (priv->listener,
                                            RPC_TRANSPORT_ACCEPT, this);
        }

        if (ret < 0) {
                gf_rdma_disconnect (this, _gf_false);
        }

        return ret;
}


static int
gf_rdma_cm_handle_event_error (rpc_transport_t *this)
{
        gf_rdma_private_t *priv  = NULL;

        priv = this->private;

        if (priv->entity != GF_RDMA_SERVER_LISTENER) {
                gf_rdma_cm_handle_disconnect (this);
        }

        return 0;
}


static int
gf_rdma_cm_handle_device_removal (struct rdma_cm_event *event)
{
        return 0;
}


static void *
gf_rdma_cm_event_handler (void *data)
{
        struct rdma_cm_event      *event         = NULL;
        int                        ret           = 0;
        rpc_transport_t           *this          = NULL;
        struct rdma_event_channel *event_channel = NULL;

        event_channel = data;

        while (1) {
                ret = rdma_get_cm_event (event_channel, &event);
                if (ret != 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, errno,
                                RDMA_MSG_CM_EVENT_FAILED,
                                "rdma_cm_get_event failed");
                        break;
                }

                switch (event->event) {
                case RDMA_CM_EVENT_ADDR_RESOLVED:
                        gf_rdma_cm_handle_addr_resolved (event);
                        break;

                case RDMA_CM_EVENT_ROUTE_RESOLVED:
                        gf_rdma_cm_handle_route_resolved (event);
                        break;

                case RDMA_CM_EVENT_CONNECT_REQUEST:
                        gf_rdma_cm_handle_connect_request (event);
                        break;

                case RDMA_CM_EVENT_ESTABLISHED:
                        gf_rdma_cm_handle_connect_init (event);
                        break;

                case RDMA_CM_EVENT_ADDR_ERROR:
                case RDMA_CM_EVENT_ROUTE_ERROR:
                case RDMA_CM_EVENT_CONNECT_ERROR:
                case RDMA_CM_EVENT_UNREACHABLE:
                case RDMA_CM_EVENT_REJECTED:
                        this = event->id->context;

                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                RDMA_MSG_CM_EVENT_FAILED, "cma event %s, "
                                "error %d (me:%s peer:%s)\n",
                                rdma_event_str(event->event), event->status,
                                this->myinfo.identifier,
                                this->peerinfo.identifier);

                        rdma_ack_cm_event (event);
                        event = NULL;

                        gf_rdma_cm_handle_event_error (this);
                        continue;

                case RDMA_CM_EVENT_DISCONNECTED:
                        this = event->id->context;

                        gf_msg_debug (this->name, 0, "received disconnect "
                                      "(me:%s peer:%s)\n",
                                      this->myinfo.identifier,
                                      this->peerinfo.identifier);

                        rdma_ack_cm_event (event);
                        event = NULL;

                        gf_rdma_cm_handle_disconnect (this);
                        continue;

                case RDMA_CM_EVENT_DEVICE_REMOVAL:
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_CM_EVENT_FAILED, "device "
                                "removed");
                        gf_rdma_cm_handle_device_removal (event);
                        break;

                default:
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_CM_EVENT_FAILED,
                                "unhandled event: %s, ignoring",
                                rdma_event_str(event->event));
                        break;
                }

                rdma_ack_cm_event (event);
        }

        return NULL;
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
                       struct iovec *rpchdr, gf_rdma_header_t *hdr,
                       gf_rdma_errcode_t err)
{
        struct rpc_msg *rpc_msg = NULL;

        if (reply_info != NULL) {
                hdr->rm_xid = hton32(reply_info->rm_xid);
        } else {
                rpc_msg = rpchdr[0].iov_base; /* assume rpchdr contains
                                               * only one vector.
                                               * (which is true)
                                               */
                hdr->rm_xid = rpc_msg->rm_xid;
        }

        hdr->rm_vers = hton32(GF_RDMA_VERSION);
        hdr->rm_credit = hton32(peer->send_count);
        hdr->rm_type = hton32(GF_RDMA_ERROR);
        hdr->rm_body.rm_error.rm_type = hton32(err);
        if (err == ERR_VERS) {
                hdr->rm_body.rm_error.rm_version.gf_rdma_vers_low
                        = hton32(GF_RDMA_VERSION);
                hdr->rm_body.rm_error.rm_version.gf_rdma_vers_high
                        = hton32(GF_RDMA_VERSION);
        }

        return sizeof (*hdr);
}


int32_t
__gf_rdma_send_error (gf_rdma_peer_t *peer, gf_rdma_ioq_t *entry,
                      gf_rdma_post_t *post, gf_rdma_reply_info_t *reply_info,
                      gf_rdma_errcode_t err)
{
        int32_t  ret = -1, len = 0;

        len = __gf_rdma_encode_error (peer, reply_info, entry->rpchdr,
                                      (gf_rdma_header_t *)post->buf, err);
        if (len == -1) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_ERROR, 0,
                        RDMA_MSG_ENCODE_ERROR, "encode error returned -1");
                goto out;
        }

        gf_rdma_post_ref (post);

        ret = gf_rdma_post_send (peer->qp, post, len);
        if (!ret) {
                ret = len;
        } else {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_POST_SEND_FAILED,
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

                mr = gf_rdma_get_pre_registred_mr(peer->trans,
                                (void *)vector[i].iov_base, vector[i].iov_len);
                if (!mr) {
                mr = ibv_reg_mr (device->pd, vector[i].iov_base,
                                 vector[i].iov_len,
                                 IBV_ACCESS_REMOTE_READ);
                }
                if (!mr) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, errno,
                                RDMA_MSG_MR_ALOC_FAILED,
                                "memory registration failed (peer:%s)",
                                peer->trans->peerinfo.identifier);
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
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_READ_CHUNK_VECTOR_FAILED,
                                "cannot create read chunks from vector "
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
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_READ_CHUNK_VECTOR_FAILED,
                                "cannot create read chunks from vector "
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
                                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                        RDMA_MSG_READ_CHUNK_VECTOR_FAILED,
                                        "cannot create read chunks from vector"
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
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_READ_CHUNK_VECTOR_FAILED,
                                "cannot create read chunks from vector "
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

                mr = gf_rdma_get_pre_registred_mr(peer->trans,
                                (void *)vector[i].iov_base, vector[i].iov_len);
                if (!mr) {
                mr = ibv_reg_mr (device->pd, vector[i].iov_base,
                                 vector[i].iov_len,
                                 IBV_ACCESS_REMOTE_WRITE
                                 | IBV_ACCESS_LOCAL_WRITE);
                }

                if (!mr) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, errno,
                                RDMA_MSG_MR_ALOC_FAILED, "memory "
                                "registration failed (peer:%s)",
                                peer->trans->peerinfo.identifier);
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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_BUFFER_ERROR,
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
  gf_msg_debug (GF_RDMA_LOG_NAME, 0,
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
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_WRITE_CHUNK_VECTOR_FAILED,
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
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_WRITE_CHUNK_VECTOR_FAILED,
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


static void
__gf_rdma_deregister_mr (gf_rdma_device_t *device,
                         struct ibv_mr **mr, int count)
{
        gf_rdma_arena_mr    *tmp   = NULL;
        gf_rdma_arena_mr    *dummy = NULL;
        int                  i     = 0;
        int                  found = 0;

               if (mr == NULL) {
                goto out;
        }

        for (i = 0; i < count; i++) {
                 found = 0;
                 if (!list_empty(&device->all_mr)) {
                 list_for_each_entry_safe (tmp, dummy, &device->all_mr, list) {
                        if (tmp->mr == mr[i]) {
                                found = 1;
                                break;
                        }
                 }
                 }
                if (!found)
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
        gf_rdma_peer_t    *peer   = NULL;
        gf_rdma_private_t *priv   = NULL;
        gf_rdma_device_t  *device = NULL;
        int32_t            ret    = 0;

        if (context == NULL) {
                goto out;
        }

        peer = context->peer;

        priv = peer->trans->private;
        device = priv->device;
        __gf_rdma_deregister_mr (device, context->mr, context->mr_count);


        if (priv->connected) {
                ret = __gf_rdma_quota_put (peer);
                if (ret < 0) {
                        gf_msg_debug ("rdma", 0, "failed to send message");
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
gf_rdma_post_context_destroy (gf_rdma_device_t *device,
                              gf_rdma_post_context_t *ctx)
{
        if (ctx == NULL) {
                goto out;
        }

        __gf_rdma_deregister_mr (device, ctx->mr, ctx->mr_count);

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
                gf_rdma_post_context_destroy (post->device, &post->ctx);
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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_WRITE_REPLY_CHUNCK_CONFLICT,
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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_CHUNK_COUNT_GREAT_MAX_SEGMENTS,
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
                ret = __gf_rdma_create_read_chunks (peer, entry, rtype,
                                                    &chunkptr,
                                                    request_ctx);
                if (ret != 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_CREATE_READ_CHUNK_FAILED,
                                "creation of read chunks failed");
                        goto out;
                }
        } else {
                *chunkptr++ = 0; /* no read chunks */
        }

        if (wtype != gf_rdma_noch) {
                ret = __gf_rdma_create_write_chunks (peer, entry, wtype,
                                                     &chunkptr,
                                                     request_ctx);
                if (ret != 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_CREATE_WRITE_REPLAY_FAILED,
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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_POST_SEND_FAILED,
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


static void
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
                             gf_rdma_post_t *post,
                             gf_rdma_reply_info_t *reply_info)
{
        gf_rdma_header_t *header    = NULL;
        int32_t           send_size = 0, ret = 0;
        char             *buf       = NULL;

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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_SEND_SIZE_GREAT_INLINE_THRESHOLD,
                        "msg size (%d) is greater than maximum size "
                        "of msg that can be sent inlined (%d)",
                        send_size, GLUSTERFS_RDMA_INLINE_THRESHOLD);
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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_POST_SEND_FAILED, "posting send (to %s) "
                        "failed with ret = %d (%s)",
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
                gf_msg_debug (GF_RDMA_LOG_NAME, 0, "length of payload (%d) is "
                              "exceeding the total write chunk length (%d)",
                              payload_size, chunk_size);
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


static int32_t
__gf_rdma_register_local_mr_for_rdma (gf_rdma_peer_t *peer,
                                      struct iovec *vector, int count,
                                      gf_rdma_post_context_t *ctx)
{
        int                i      = 0;
        int32_t            ret    = -1;
        gf_rdma_private_t *priv   = NULL;
        gf_rdma_device_t  *device = NULL;

        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, ctx, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, vector, out);

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
                ctx->mr[ctx->mr_count] = gf_rdma_get_pre_registred_mr(
                                peer->trans, (void *)vector[i].iov_base,
                                vector[i].iov_len);

                if (!ctx->mr[ctx->mr_count]) {
                ctx->mr[ctx->mr_count] = ibv_reg_mr (device->pd,
                                                     vector[i].iov_base,
                                                     vector[i].iov_len,
                                                     IBV_ACCESS_LOCAL_WRITE);
                }
                if (ctx->mr[ctx->mr_count] == NULL) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, errno,
                                RDMA_MSG_MR_ALOC_FAILED,
                                "registering memory for IBV_ACCESS_LOCAL_WRITE"
                                " failed");
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

                sg_list[num_sge].addr = (unsigned long)vec[i].iov_base;
                sg_list[num_sge].length = size;
                sg_list[num_sge].lkey = post->ctx.mr[i]->lkey;

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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_WRITE_CLIENT_ERROR, "rdma write to "
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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_MR_ALOC_FAILED,
                        "registering memory region for rdma failed");
                goto out;
        }

        post->ctx.iobref = iobref_ref (iobref);

        for (i = 0; (i < reply_info->wc_array->wc_nchunks)
                     && (payload_size != 0);
             i++) {
                xfer_len = min (payload_size,
                                reply_info->wc_array->wc_array[i].wc_target.rs_length);

                ret = __gf_rdma_write (peer, post, vector, xfer_len,
                                       &payload_idx,
                                       &reply_info->wc_array->wc_array[i]);
                if (ret == -1) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_WRITE_CLIENT_ERROR, "rdma write to "
                                "client (%s) failed",
                                peer->trans->peerinfo.identifier);
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
        gf_rdma_header_t *header       = NULL;
        char             *buf          = NULL;
        uint32_t          payload_size = 0;
        int               count        = 0, i = 0;
        int32_t           ret          = 0;
        struct iovec      vector[MAX_IOVEC];

        header = (gf_rdma_header_t *)post->buf;

        __gf_rdma_fill_reply_header (header, entry->rpchdr, reply_info,
                                     peer->send_count);

        header->rm_type = hton32 (GF_RDMA_NOMSG);

        payload_size = iov_length (entry->rpchdr, entry->rpchdr_count) +
                iov_length (entry->proghdr, entry->proghdr_count);

        /* encode reply chunklist */
        buf = (char *)&header->rm_body.rm_chunks[2];
        ret = __gf_rdma_reply_encode_write_chunks (peer, payload_size, post,
                                                   reply_info,
                                                   (uint32_t **)&buf);
        if (ret == -1) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_ENCODE_ERROR, "encoding write chunks failed");
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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_WRITE_PEER_FAILED, "rdma write to peer "
                        "(%s) failed", peer->trans->peerinfo.identifier);
                gf_rdma_post_unref (post);
                goto out;
        }

        ret = gf_rdma_post_send (peer->qp, post, (buf - post->buf));
        if (ret) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_POST_SEND_FAILED, "posting a send request "
                        "to client (%s) failed with ret = %d (%s)",
                        peer->trans->peerinfo.identifier, ret,
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
                               gf_rdma_post_t *post,
                               gf_rdma_reply_info_t *reply_info)
{
        gf_rdma_header_t *header       = NULL;
        int32_t           send_size    = 0, ret = 0;
        char             *ptr          = NULL;
        uint32_t          payload_size = 0;

        send_size = iov_length (entry->rpchdr, entry->rpchdr_count)
                + iov_length (entry->proghdr, entry->proghdr_count)
                + GLUSTERFS_RDMA_MAX_HEADER_SIZE;

        if (send_size > GLUSTERFS_RDMA_INLINE_THRESHOLD) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_SEND_SIZE_GREAT_INLINE_THRESHOLD,
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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_ENCODE_ERROR, "encoding write chunks failed");
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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_WRITE_PEER_FAILED, "rdma write to peer "
                        "(%s) failed", peer->trans->peerinfo.identifier);
                gf_rdma_post_unref (post);
                goto out;
        }

        iov_unload (ptr, entry->rpchdr, entry->rpchdr_count);
        ptr += iov_length (entry->rpchdr, entry->rpchdr_count);

        iov_unload (ptr, entry->proghdr, entry->proghdr_count);
        ptr += iov_length (entry->proghdr, entry->proghdr_count);

        ret = gf_rdma_post_send (peer->qp, post, (ptr - post->buf));
        if (ret) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_SEND_CLIENT_ERROR,
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

        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, peer, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, entry, out);
        GF_VALIDATE_OR_GOTO (GF_RDMA_LOG_NAME, post, out);

        reply_info = entry->msg.reply_info;
        if (reply_info != NULL) {
                type = reply_info->type;
        }

        switch (type) {
        case gf_rdma_noch:
                ret = __gf_rdma_send_reply_inline (peer, entry, post,
                                                   reply_info);
                if (ret < 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_SEND_REPLY_FAILED,
                                "failed to send reply to peer (%s) as an "
                                "inlined rdma msg",
                                peer->trans->peerinfo.identifier);
                }
                break;

        case gf_rdma_replych:
                ret = __gf_rdma_send_reply_type_nomsg (peer, entry, post,
                                                       reply_info);
                if (ret < 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_SEND_REPLY_FAILED,
                                "failed to send reply to peer (%s) as "
                                "RDMA_NOMSG", peer->trans->peerinfo.identifier);
                }
                break;

        case gf_rdma_writech:
                ret = __gf_rdma_send_reply_type_msg (peer, entry, post,
                                                     reply_info);
                if (ret < 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_SEND_REPLY_FAILED,
                                "failed to send reply with write chunks "
                                "to peer (%s)",
                                peer->trans->peerinfo.identifier);
                }
                break;

        default:
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_INVALID_CHUNK_TYPE,
                        "invalid chunktype (%d) specified for sending reply "
                        " (peer:%s)", type, peer->trans->peerinfo.identifier);
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
                        post = gf_rdma_new_post (peer->trans, device,
                                                 (options->send_size + 2048),
                                                 GF_RDMA_SEND_POST);
                }

                if (post == NULL) {
                        ret = -1;
                        gf_msg_callingfn (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                          RDMA_MSG_POST_SEND_FAILED,
                                          "not able to get a post to send msg");
                        goto out;
                }

                if (entry->is_request) {
                        ret = __gf_rdma_ioq_churn_request (peer, entry, post);
                        if (ret < 0) {
                                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                        RDMA_MSG_PROC_IOQ_ENTRY_FAILED,
                                        "failed to process request ioq entry "
                                        "to peer(%s)",
                                        peer->trans->peerinfo.identifier);
                        }
                } else {
                        ret = __gf_rdma_ioq_churn_reply (peer, entry, post);
                        if (ret < 0) {
                                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                        RDMA_MSG_PROC_IOQ_ENTRY_FAILED,
                                        "failed to process reply ioq entry "
                                        "to peer (%s)",
                                        peer->trans->peerinfo.identifier);
                        }
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

        while (!list_empty (&peer->ioq)) {
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
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                RDMA_MSG_PEER_DISCONNECTED,
                                "rdma is not connected to peer (%s)",
                                this->peerinfo.identifier);
                        ret = -1;
                        goto unlock;
                }

                peer = &priv->peer;
                if (list_empty (&peer->ioq)) {
                        ret = __gf_rdma_ioq_churn_entry (peer, entry);
                        if (ret != 0) {
                                need_append = 0;

                                if (ret < 0) {
                                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                                RDMA_MSG_PROC_IOQ_ENTRY_FAILED,
                                                "processing ioq entry destined"
                                                " to (%s) failed",
                                                this->peerinfo.identifier);
                                }
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
        gf_rdma_private_t    *priv  = NULL;
        gf_rdma_peer_t       *peer  = NULL;

        if (req == NULL) {
                goto out;
        }

        priv = this->private;
        if (priv == NULL) {
                ret = -1;
                goto out;
        }

        peer = &priv->peer;
        data.is_request = 1;
        data.data.req = *req;
/*
 * when fist message is received on a transport, quota variable will
 * initiaize  and quota_set will set to one. In gluster code client
 * process with respect to transport is the one who sends the first
 * message. Before settng quota_set variable if a submit request is
 * came on server, then the message should not send.
 */

        if (priv->entity == GF_RDMA_SERVER && peer->quota_set == 0) {
                ret = 0;
                goto out;
        }

        entry = gf_rdma_ioq_new (this, &data);
        if (entry == NULL) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        RDMA_MSG_NEW_IOQ_ENTRY_FAILED,
                        "getting a new ioq entry failed (peer:%s)",
                        this->peerinfo.identifier);
                goto out;
        }

        ret = gf_rdma_writev (this, entry);

        if (ret > 0) {
                ret = 0;
        } else if (ret < 0) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_WRITE_PEER_FAILED,
                        "sending request to peer (%s) failed",
                        this->peerinfo.identifier);
                rpc_transport_disconnect (this, _gf_false);
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
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        RDMA_MSG_NEW_IOQ_ENTRY_FAILED,
                        "getting a new ioq entry failed (peer:%s)",
                        this->peerinfo.identifier);
                goto out;
        }

        ret = gf_rdma_writev (this, entry);
        if (ret > 0) {
                ret = 0;
        } else if (ret < 0) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_WRITE_PEER_FAILED,
                        "sending request to peer (%s) failed",
                        this->peerinfo.identifier);
                rpc_transport_disconnect (this, _gf_false);
        }

out:
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


static void
__gf_rdma_destroy_qp (rpc_transport_t *this)
{
        gf_rdma_private_t *priv = NULL;

        priv = this->private;
        if (priv->peer.qp) {
                gf_rdma_unregister_peer (priv->device, priv->peer.qp->qp_num);
                rdma_destroy_qp (priv->peer.cm_id);
        }
        priv->peer.qp = NULL;

        return;
}


static int32_t
gf_rdma_create_qp (rpc_transport_t *this)
{
        gf_rdma_private_t *priv        = NULL;
        gf_rdma_device_t  *device      = NULL;
        int32_t            ret         = 0;
        gf_rdma_peer_t    *peer        = NULL;
        char              *device_name = NULL;

        priv = this->private;

        peer = &priv->peer;

        device_name = (char *)ibv_get_device_name (peer->cm_id->verbs->device);
        if (device_name == NULL) {
                ret = -1;
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        RDMA_MSG_GET_DEVICE_NAME_FAILED, "cannot get "
                        "device_name");
                goto out;
        }

        device = gf_rdma_get_device (this, peer->cm_id->verbs,
                                     device_name);
        if (device == NULL) {
                ret = -1;
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        RDMA_MSG_GET_DEVICE_FAILED, "cannot get device for "
                        "device %s", device_name);
                goto out;
        }

        if (priv->device == NULL) {
                priv->device = device;
        }

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

        ret = rdma_create_qp(peer->cm_id, device->pd, &init_attr);
        if (ret != 0) {
                gf_msg (peer->trans->name, GF_LOG_CRITICAL, errno,
                        RDMA_MSG_CREAT_QP_FAILED, "%s: could not create QP",
                        this->name);
                ret = -1;
                goto out;
        }

        peer->qp = peer->cm_id->qp;

        ret = gf_rdma_register_peer (device, peer->qp->qp_num, peer);

out:
        if (ret == -1)
                __gf_rdma_destroy_qp (this);

        return ret;
}


static int32_t
__gf_rdma_teardown (rpc_transport_t *this)
{
        gf_rdma_private_t *priv = NULL;
        gf_rdma_peer_t    *peer = NULL;

        priv = this->private;
        peer = &priv->peer;

        if (peer->cm_id->qp != NULL) {
                __gf_rdma_destroy_qp (this);
        }

        if (!list_empty (&priv->peer.ioq)) {
                __gf_rdma_ioq_flush (peer);
        }

        if (peer->cm_id != NULL) {
                rdma_destroy_id (peer->cm_id);
                peer->cm_id = NULL;
        }

        /* TODO: decrement cq size */
        return 0;
}


static int32_t
gf_rdma_teardown (rpc_transport_t *this)
{
        int32_t ret = 0;
        gf_rdma_private_t *priv = NULL;

        if (this == NULL) {
                goto out;
        }

        priv = this->private;

        pthread_mutex_lock (&priv->write_mutex);
        {
                ret = __gf_rdma_teardown (this);
        }
        pthread_mutex_unlock (&priv->write_mutex);

out:
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


static int32_t
gf_rdma_decode_error_msg (gf_rdma_peer_t *peer, gf_rdma_post_t *post,
                          size_t bytes_in_post)
{
        gf_rdma_header_t *header  = NULL;
        struct iobuf     *iobuf   = NULL;
        struct iobref    *iobref  = NULL;
        int32_t           ret     = -1;
        struct rpc_msg    rpc_msg = {0, };

        header = (gf_rdma_header_t *)post->buf;
        header->rm_body.rm_error.rm_type
                = ntoh32 (header->rm_body.rm_error.rm_type);
        if (header->rm_body.rm_error.rm_type == ERR_VERS) {
                header->rm_body.rm_error.rm_version.gf_rdma_vers_low =
                        ntoh32 (header->rm_body.rm_error.rm_version.gf_rdma_vers_low);
                header->rm_body.rm_error.rm_version.gf_rdma_vers_high =
                        ntoh32 (header->rm_body.rm_error.rm_version.gf_rdma_vers_high);
        }

        rpc_msg.rm_xid = header->rm_xid;
        rpc_msg.rm_direction = REPLY;
        rpc_msg.rm_reply.rp_stat = MSG_DENIED;

        iobuf = iobuf_get2 (peer->trans->ctx->iobuf_pool, bytes_in_post);
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

        ret = rpc_reply_to_xdr (&rpc_msg, iobuf_ptr (iobuf),
                                iobuf_pagesize (iobuf), &post->ctx.vector[0]);
        if (ret == -1) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_RPC_REPLY_CREATE_FAILED, "Failed to create "
                        "RPC reply");
                goto out;
        }

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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_GET_READ_CHUNK_FAILED, "cannot get read "
                        "chunklist from msg");
                goto out;
        }

        /* skip terminator of read-chunklist */
        ptr = ptr + sizeof (uint32_t);

        ret = gf_rdma_get_write_chunklist (&ptr, &write_ary);
        if (ret == -1) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_GET_WRITE_CHUNK_FAILED, "cannot get write "
                        "chunklist from msg");
                goto out;
        }

        /* skip terminator of write-chunklist */
        ptr = ptr + sizeof (uint32_t);

        if (write_ary != NULL) {
                reply_info = gf_rdma_reply_info_alloc (peer);
                if (reply_info == NULL) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_REPLY_INFO_ALLOC_FAILED,
                                "reply_info_alloc failed");
                        ret = -1;
                        goto out;
                }

                reply_info->type = gf_rdma_writech;
                reply_info->wc_array = write_ary;
                reply_info->rm_xid = header->rm_xid;
        } else {
                ret = gf_rdma_get_write_chunklist (&ptr, &write_ary);
                if (ret == -1) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_CHUNKLIST_ERROR, "cannot get reply "
                                "chunklist from msg");
                        goto out;
                }

                if (write_ary != NULL) {
                        reply_info = gf_rdma_reply_info_alloc (peer);
                        if (reply_info == NULL) {
                                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                        RDMA_MSG_REPLY_INFO_ALLOC_FAILED,
                                        "reply_info_alloc_failed");
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
                header_len = (long)ptr - (long)post->buf;
                post->ctx.vector[0].iov_len = (bytes_in_post - header_len);

                post->ctx.hdr_iobuf = iobuf_get2 (peer->trans->ctx->iobuf_pool,
                                                  (bytes_in_post - header_len));
                if (post->ctx.hdr_iobuf == NULL) {
                        ret = -1;
                        goto out;
                }

                post->ctx.vector[0].iov_base = iobuf_ptr (post->ctx.hdr_iobuf);
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

                GF_FREE (write_ary);
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
                if (ret < 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_ENCODE_ERROR, "cannot decode msg of "
                                "type (%d)", header->rm_type);
                }

                break;

        case GF_RDMA_MSGP:
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_INVALID_ENTRY, "rdma msg of msg-type "
                        "GF_RDMA_MSGP should not have been received");
                ret = -1;
                break;

        case GF_RDMA_DONE:
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_INVALID_ENTRY, "rdma msg of msg-type "
                        "GF_RDMA_DONE should not have been received");
                ret = -1;
                break;

        case GF_RDMA_ERROR:
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_RDMA_ERROR_RECEIVED, "received a msg of type"
                        " RDMA_ERROR");
                ret = gf_rdma_decode_error_msg (peer, post, bytes_in_post);
                break;

        default:
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_INVALID_ENTRY, "unknown rdma msg-type (%d)",
                        header->rm_type);
        }

        return ret;
}


int32_t
gf_rdma_do_reads (gf_rdma_peer_t *peer, gf_rdma_post_t *post,
                  gf_rdma_read_chunk_t *readch)
{
        int32_t             ret       = -1, i = 0, count = 0;
        size_t              size      = 0;
        char               *ptr       = NULL;
        struct iobuf       *iobuf     = NULL;
        gf_rdma_private_t  *priv      = NULL;
        struct ibv_sge     *list      = NULL;
        struct ibv_send_wr *wr        = NULL, *bad_wr = NULL;
        int                 total_ref = 0;
        priv = peer->trans->private;

        for (i = 0; readch[i].rc_discrim != 0; i++) {
                size += readch[i].rc_target.rs_length;
        }

        if (i == 0) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_INVALID_CHUNK_TYPE, "message type specified "
                        "as rdma-read but there are no rdma read-chunks "
                        "present");
                goto out;
        }

        post->ctx.gf_rdma_reads = i;
        i = 0;
        iobuf = iobuf_get2 (peer->trans->ctx->iobuf_pool, size);
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
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_PEER_DISCONNECTED, "transport not "
                                "connected to peer (%s), not doing rdma reads",
                                peer->trans->peerinfo.identifier);
                        goto unlock;
                }

                list = GF_CALLOC (post->ctx.gf_rdma_reads,
                                sizeof (struct ibv_sge), gf_common_mt_sge);

                if (list == NULL) {
                       errno =  ENOMEM;
                       ret = -1;
                       goto unlock;
                }
                wr   = GF_CALLOC (post->ctx.gf_rdma_reads,
                                sizeof (struct ibv_send_wr), gf_common_mt_wr);
                if (wr == NULL) {
                       errno =  ENOMEM;
                       ret = -1;
                       goto unlock;
                }
                for (i = 0; readch[i].rc_discrim != 0; i++) {
                        count = post->ctx.count++;
                        post->ctx.vector[count].iov_base = ptr;
                        post->ctx.vector[count].iov_len
                                = readch[i].rc_target.rs_length;

                        ret = __gf_rdma_register_local_mr_for_rdma (peer,
                                &post->ctx.vector[count], 1, &post->ctx);
                        if (ret == -1) {
                                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                        RDMA_MSG_MR_ALOC_FAILED,
                                        "registering local memory"
                                       " for rdma read failed");
                                goto unlock;
                        }

                        list[i].addr = (unsigned long)
                                       post->ctx.vector[count].iov_base;
                        list[i].length = post->ctx.vector[count].iov_len;
                        list[i].lkey =
                                post->ctx.mr[post->ctx.mr_count - 1]->lkey;

                        wr[i].wr_id      =
                                (unsigned long) gf_rdma_post_ref (post);
                        wr[i].sg_list    = &list[i];
                        wr[i].next       = &wr[i+1];
                        wr[i].num_sge    = 1;
                        wr[i].opcode     = IBV_WR_RDMA_READ;
                        wr[i].send_flags = IBV_SEND_SIGNALED;
                        wr[i].wr.rdma.remote_addr =
                                readch[i].rc_target.rs_offset;
                        wr[i].wr.rdma.rkey = readch[i].rc_target.rs_handle;

                        ptr += readch[i].rc_target.rs_length;
                        total_ref++;
                }
                wr[i-1].next = NULL;
                ret = ibv_post_send (peer->qp, wr, &bad_wr);
                if (ret) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_READ_CLIENT_ERROR, "rdma read from "
                                "client (%s) failed with ret = %d (%s)",
                                peer->trans->peerinfo.identifier,
                                ret, (ret > 0) ? strerror (ret) : "");

                        if (!bad_wr) {
                                ret = -1;
                                goto unlock;
                        }

                        for (i = 0; i < post->ctx.gf_rdma_reads; i++) {
                                if (&wr[i] != bad_wr)
                                        total_ref--;
                                else
                                        break;
                        }

                        ret = -1;
                }

        }
unlock:
        pthread_mutex_unlock (&priv->write_mutex);
out:
        if (list)
                GF_FREE (list);
        if (wr)
                GF_FREE (wr);

        if (ret == -1) {
                while (total_ref-- > 0)
                        gf_rdma_post_unref (post);

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
                        gf_msg_debug (GF_RDMA_LOG_NAME, 0, "cannot get request"
                                      "information from rpc layer");
                        goto out;
                }

                rpc_req = request_info.rpc_req;
                if (rpc_req == NULL) {
                        gf_msg_debug (GF_RDMA_LOG_NAME, 0, "rpc request "
                                      "structure not found");
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
        if (ret < 0) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        TRANS_MSG_TRANSPORT_ERROR, "transport_notify failed");
        }

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
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        TRANS_MSG_TRANSPORT_ERROR, "cannot get request "
                        "information (peer:%s) from rpc layer",
                        peer->trans->peerinfo.identifier);
                goto out;
        }

        rpc_req = request_info.rpc_req;
        if (rpc_req == NULL) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_RPC_ST_ERROR, "rpc request structure not "
                        "found");
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
                if (ret < 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_POLL_IN_NOTIFY_FAILED,
                                "pollin notify failed");
                }
        }

        return ret;
}


static int32_t
gf_rdma_recv_request (gf_rdma_peer_t *peer, gf_rdma_post_t *post,
                      gf_rdma_read_chunk_t *readch)
{
        int32_t ret = -1;

        if (readch != NULL) {
                ret = gf_rdma_do_reads (peer, post, readch);
                if (ret < 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_PEER_READ_FAILED,
                                "rdma read from peer (%s) failed",
                                peer->trans->peerinfo.identifier);
                }
        } else {
                ret = gf_rdma_pollin_notify (peer, post);
                if (ret == -1) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_POLL_IN_NOTIFY_FAILED,
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
        gf_rdma_private_t    *priv     = NULL;

        post = (gf_rdma_post_t *) (long) wc->wr_id;
        if (post == NULL) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_POST_MISSING, "no post found in successful "
                        "work completion element");
                goto out;
        }

        ret = gf_rdma_decode_header (peer, post, &readch, wc->byte_len);
        if (ret == -1) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_HEADER_DECODE_FAILED, "decoding of header "
                        "failed");
                goto out;
        }

        header = (gf_rdma_header_t *)post->buf;

        priv = peer->trans->private;

        pthread_mutex_lock (&priv->write_mutex);
        {
                if (!priv->peer.quota_set) {
                        priv->peer.quota_set = 1;

                        /* Initially peer.quota is set to 1 as per RFC 5666. We
                         * have to account for the quota used while sending
                         * first msg (which may or may not be returned to pool
                         * at this point) while deriving peer.quota from
                         * header->rm_credit. Hence the arithmatic below,
                         * instead of directly setting it to header->rm_credit.
                         */
                        priv->peer.quota = header->rm_credit
                                - (1 - priv->peer.quota);
                }
        }
        pthread_mutex_unlock (&priv->write_mutex);

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
                if (header->rm_body.rm_error.rm_type == ERR_CHUNK) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_RDMA_ERROR_RECEIVED,
                                "peer (%s), couldn't encode or decode the msg "
                                "properly or write chunks were not provided "
                                "for replies that were bigger than "
                                "RDMA_INLINE_THRESHOLD (%d)",
                                peer->trans->peerinfo.identifier,
                                GLUSTERFS_RDMA_INLINE_THRESHOLD);
                        ret = gf_rdma_pollin_notify (peer, post);
                        if (ret == -1) {
                                gf_msg_debug (GF_RDMA_LOG_NAME, 0, "pollin "
                                              "notification failed");
                        }
                        goto out;
                } else {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_ERROR, 0,
                                TRANS_MSG_TRANSPORT_ERROR, "an error has "
                                "happened while transmission of msg, "
                                "disconnecting the transport");
                        ret = -1;
                        goto out;
                }

        default:
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                        RDMA_MSG_INVALID_ENTRY, "invalid rdma msg-type (%d)",
                        header->rm_type);
                goto out;
        }

        if (msg_type == CALL) {
                ret = gf_rdma_recv_request (peer, post, readch);
                if (ret < 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_PEER_REQ_FAILED, "receiving a request"
                                " from peer (%s) failed",
                                peer->trans->peerinfo.identifier);
                }
        } else {
                ret = gf_rdma_recv_reply (peer, post);
                if (ret < 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_PEER_REP_FAILED, "receiving a reply "
                                "from peer (%s) failed",
                                peer->trans->peerinfo.identifier);
                }
        }

out:
        if (ret == -1) {
                rpc_transport_disconnect (peer->trans, _gf_false);
        }

        return;
}

void *
gf_rdma_async_event_thread (void *context)
{
        struct ibv_async_event event;
        int ret;

        while (1) {
                do {
                        ret = ibv_get_async_event((struct ibv_context *)context,
                                                  &event);

                        if (ret && errno != EINTR) {
                                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, errno,
                                        RDMA_MSG_EVENT_ERROR, "Error getting "
                                        "event");
                        }
                } while (ret && errno == EINTR);

                switch (event.event_type) {
                case IBV_EVENT_SRQ_LIMIT_REACHED:
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_EVENT_SRQ_LIMIT_REACHED, "received "
                                "srq_limit reached");
                        break;

                default:
                        gf_msg_debug (GF_RDMA_LOG_NAME, 0, "event (%d) "
                                      "received", event.event_type);
                        break;
                }

                ibv_ack_async_event(&event);
        }

        return 0;
}


static void *
gf_rdma_recv_completion_proc (void *data)
{
        struct ibv_comp_channel *chan      = NULL;
        gf_rdma_device_t        *device    = NULL;;
        gf_rdma_post_t          *post      = NULL;
        gf_rdma_peer_t          *peer      = NULL;
        struct ibv_cq           *event_cq  = NULL;
        struct ibv_wc            wc[10]    = {{0},};
        void                    *event_ctx = NULL;
        int32_t                  ret       = 0;
        int32_t                  num_wr    = 0, index = 0;
        uint8_t                  failed    = 0;

        chan = data;

        while (1) {
                failed = 0;
                ret = ibv_get_cq_event (chan, &event_cq, &event_ctx);
                if (ret) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_ERROR, errno,
                                RDMA_MSG_IBV_GET_CQ_FAILED,
                                "ibv_get_cq_event failed, terminating recv "
                                "thread %d (%d)", ret, errno);
                        continue;
                }

                device = event_ctx;

                ret = ibv_req_notify_cq (event_cq, 0);
                if (ret) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_ERROR, errno,
                                RDMA_MSG_IBV_REQ_NOTIFY_CQ_FAILED,
                                "ibv_req_notify_cq on %s failed, terminating "
                                "recv thread: %d (%d)",
                                device->device_name, ret, errno);
                        continue;
                }

                device = (gf_rdma_device_t *) event_ctx;

                while (!failed &&
                       (num_wr = ibv_poll_cq (event_cq, 10, wc)) > 0) {

                        for (index = 0; index < num_wr && !failed; index++) {
                                post = (gf_rdma_post_t *) (long)
                                        wc[index].wr_id;

                                pthread_mutex_lock (&device->qpreg.lock);
                                {
                                        peer = __gf_rdma_lookup_peer (device,
                                                           wc[index].qp_num);

                                /*
                                 * keep a refcount on transport so that it
                                 * does not get freed because of some error
                                 * indicated by wc.status till we are done
                                 * with usage of peer and thereby that of
                                 * trans.
                                 */
                                if (peer != NULL) {
                                        rpc_transport_ref (peer->trans);
                                }
                                }
                                pthread_mutex_unlock (&device->qpreg.lock);

                                if (wc[index].status != IBV_WC_SUCCESS) {
                                        gf_msg (GF_RDMA_LOG_NAME,
                                        GF_LOG_ERROR, 0,
                                        RDMA_MSG_RECV_ERROR, "recv work "
                                        "request on `%s' returned error (%d)",
                                        device->device_name,
                                        wc[index].status);
                                        failed = 1;
                                if (peer) {
                                        ibv_ack_cq_events (event_cq, num_wr);
                                        rpc_transport_unref (peer->trans);
                                        rpc_transport_disconnect (peer->trans,
                                                                  _gf_false);
                                }

                                if (post) {
                                        gf_rdma_post_unref (post);
                                }
                                continue;
                                }

                                if (peer) {
                                        gf_rdma_process_recv (peer,
                                                        &wc[index]);
                                        rpc_transport_unref (peer->trans);
                                } else {
                                        gf_msg_debug (GF_RDMA_LOG_NAME, 0,
                                                      "could not lookup peer "
                                                      "for qp_num: %d",
                                                      wc[index].qp_num);
                                }

                                gf_rdma_post_unref (post);
                        }
                }

                if (ret < 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_ERROR, errno,
                                RDMA_MSG_IBV_POLL_CQ_ERROR,
                                "ibv_poll_cq on `%s' returned error "
                                "(ret = %d, errno = %d)",
                                device->device_name, ret, errno);
                        continue;
                }
                if (!failed)
                        ibv_ack_cq_events (event_cq, num_wr);
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

        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                RDMA_MSG_RDMA_HANDLE_FAILED,
                "send work request on `%s' returned error "
                "wc.status = %d, wc.vendor_err = %d, post->buf = %p, "
                "wc.byte_len = %d, post->reused = %d",
                (device != NULL) ? device->device_name : NULL, wc->status,
                wc->vendor_err, post->buf, wc->byte_len, post->reused);

        if (wc->status == IBV_WC_RETRY_EXC_ERR) {
                gf_msg ("rdma", GF_LOG_ERROR, 0, TRANS_MSG_TIMEOUT_EXCEEDED,
                        "connection between client and server not working. "
                        "check by running 'ibv_srq_pingpong'. also make sure "
                        "subnet manager is running (eg: 'opensm'), or check "
                        "if rdma port is valid (or active) by running "
                        "'ibv_devinfo'. contact Gluster Support Team if the "
                        "problem persists.");
        }

        if (peer) {
                rpc_transport_disconnect (peer->trans, _gf_false);
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
        /*
         * if reads performed as vectored, then all the buffers are actually
         * contiguous memory, so that we can use it as single vector, instead
         * of multiple.
         */
        while (post->ctx.count > 2) {
                post->ctx.vector[1].iov_len +=
                        post->ctx.vector[post->ctx.count-1].iov_len;
                post->ctx.count--;
        }

        ret = gf_rdma_pollin_notify (peer, post);
        if ((ret == -1) && (peer != NULL)) {
                rpc_transport_disconnect (peer->trans, _gf_false);
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
        struct ibv_wc            wc[10]     = {{0},};
        char                     is_request = 0;
        int32_t                  ret        = 0, quota_ret = 0, num_wr = 0;
        int32_t                  index      = 0, failed = 0;
        chan = data;
        while (1) {
                failed = 0;
                ret = ibv_get_cq_event (chan, &event_cq, &event_ctx);
                if (ret) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_ERROR, errno,
                                RDMA_MSG_IBV_GET_CQ_FAILED,
                                "ibv_get_cq_event on failed, terminating "
                                "send thread: %d (%d)", ret, errno);
                        continue;
                }

                device = event_ctx;

                ret = ibv_req_notify_cq (event_cq, 0);
                if (ret) {
                        gf_msg (GF_RDMA_LOG_NAME,  GF_LOG_ERROR, errno,
                                RDMA_MSG_IBV_REQ_NOTIFY_CQ_FAILED,
                                "ibv_req_notify_cq on %s failed, terminating "
                                "send thread: %d (%d)",
                                device->device_name, ret, errno);
                        continue;
                }

                while (!failed &&
                       (num_wr = ibv_poll_cq (event_cq, 10, wc)) > 0) {
                        for (index = 0; index < num_wr && !failed; index++) {
                                post = (gf_rdma_post_t *) (long)
                                        wc[index].wr_id;

                                pthread_mutex_lock (&device->qpreg.lock);
                                {
                                        peer = __gf_rdma_lookup_peer (device,
                                                        wc[index].qp_num);

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

                                if (wc[index].status != IBV_WC_SUCCESS) {
                                        ibv_ack_cq_events (event_cq, num_wr);
                                        failed = 1;
                                        gf_rdma_handle_failed_send_completion
                                                (peer, &wc[index]);
                                } else {
                                      gf_rdma_handle_successful_send_completion
                                                (peer, &wc[index]);
                                }

                                if (post) {
                                        is_request = post->ctx.is_request;

                                        ret = gf_rdma_post_unref (post);
                                        if ((ret == 0)
                                        && (wc[index].status == IBV_WC_SUCCESS)
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
                                                quota_ret = gf_rdma_quota_put
                                                        (peer);
                                                if (quota_ret < 0) {
                                                        gf_msg_debug ("rdma",
                                                        0, "failed to send "
                                                        "message");
                                                }
                                        }
                                }

                                if (peer) {
                                        rpc_transport_unref (peer->trans);
                                } else {
                                        gf_msg_debug (GF_RDMA_LOG_NAME, 0,
                                        "could not lookup peer for qp_num: %d",
                                        wc[index].qp_num);

                                }
                        }
                }

                if (ret < 0) {
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_ERROR, errno,
                        RDMA_MSG_IBV_POLL_CQ_ERROR,
                        "ibv_poll_cq on `%s' returned error (ret = %d,"
                        " errno = %d)",
                        device->device_name, ret, errno);
                        continue;
               }
               if (!failed)
                       ibv_ack_cq_events (event_cq, num_wr);
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
        options->attr_timeout = GF_RDMA_TIMEOUT;
        options->attr_retry_cnt = GF_RDMA_RETRY_CNT;
        options->attr_rnr_retry = GF_RDMA_RNR_RETRY;

        temp = dict_get (this->options,
                         "transport.rdma.work-request-send-count");
        if (temp)
                options->send_count = data_to_int32 (temp);

        temp = dict_get (this->options,
                         "transport.rdma.work-request-recv-count");
        if (temp)
                options->recv_count = data_to_int32 (temp);

        temp = dict_get (this->options, "transport.rdma.attr-timeout");

        if (temp)
                options->attr_timeout = data_to_uint8 (temp);

        temp = dict_get (this->options, "transport.rdma.attr-retry-cnt");

        if (temp)
                options->attr_retry_cnt = data_to_uint8 (temp);

        temp = dict_get (this->options, "transport.rdma.attr-rnr-retry");

        if (temp)
                options->attr_rnr_retry = data_to_uint8 (temp);

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
                        gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, 0,
                                RDMA_MSG_UNRECG_MTU_VALUE, "%s: unrecognized "
                                "MTU value '%s', defaulting to '2048'",
                                this->name, data_to_str (temp));
                else
                        gf_msg_trace (GF_RDMA_LOG_NAME, 0, "%s: defaulting "
                                      "MTU to '2048'", this->name);
                options->mtu = IBV_MTU_2048;
                break;
        }

        temp = dict_get (this->options,
                         "transport.rdma.device-name");
        if (temp)
                options->device_name = gf_strdup (temp->data);

        return;
}


gf_rdma_ctx_t *
__gf_rdma_ctx_create (void)
{
        gf_rdma_ctx_t *rdma_ctx = NULL;
        int            ret      = -1;

        rdma_ctx = GF_CALLOC (1, sizeof (*rdma_ctx), gf_common_mt_char);
        if (rdma_ctx == NULL) {
                goto out;
        }
        pthread_mutex_init (&rdma_ctx->lock, NULL);
        rdma_ctx->rdma_cm_event_channel = rdma_create_event_channel ();
        if (rdma_ctx->rdma_cm_event_channel == NULL) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, errno,
                        RDMA_MSG_CM_EVENT_FAILED, "rdma_cm event channel "
                        "creation failed");
                goto out;
        }

        ret = gf_thread_create (&rdma_ctx->rdma_cm_thread, NULL,
                                gf_rdma_cm_event_handler,
                                rdma_ctx->rdma_cm_event_channel);
        if (ret != 0) {
                gf_msg (GF_RDMA_LOG_NAME, GF_LOG_WARNING, ret,
                        RDMA_MSG_CM_EVENT_FAILED, "creation of thread to "
                        "handle rdma-cm events failed");
                goto out;
        }

out:
        if (ret < 0) {
                if (rdma_ctx->rdma_cm_event_channel != NULL) {
                        rdma_destroy_event_channel (rdma_ctx->rdma_cm_event_channel);
                }

                GF_FREE (rdma_ctx);
                rdma_ctx = NULL;
        }

        return rdma_ctx;
}

static int32_t
gf_rdma_init (rpc_transport_t *this)
{
        gf_rdma_private_t   *priv    = NULL;
        int32_t              ret     = 0;
        glusterfs_ctx_t     *ctx     = NULL;
        gf_rdma_options_t   *options = NULL;

        ctx = this->ctx;

        priv = this->private;

        ibv_fork_init ();
        gf_rdma_options_init (this);

        options = &priv->options;
        priv->peer.send_count = options->send_count;
        priv->peer.recv_count = options->recv_count;
        priv->peer.send_size = options->send_size;
        priv->peer.recv_size = options->recv_size;

        priv->peer.trans = this;
        INIT_LIST_HEAD (&priv->peer.ioq);

        pthread_mutex_init (&priv->write_mutex, NULL);
        pthread_mutex_init (&priv->recv_mutex, NULL);
        pthread_cond_init (&priv->recv_cond, NULL);

        LOCK (&ctx->lock);
        {
                if (ctx->ib == NULL) {
                        ctx->ib = __gf_rdma_ctx_create ();
                        if (ctx->ib == NULL) {
                                ret = -1;
                        }
                }
        }
        UNLOCK (&ctx->lock);

        return ret;
}


static int32_t
gf_rdma_disconnect (rpc_transport_t *this, gf_boolean_t wait)
{
        gf_rdma_private_t *priv = NULL;
        int32_t            ret  = 0;

        priv = this->private;
        gf_msg_callingfn (this->name, GF_LOG_DEBUG, 0,
                          RDMA_MSG_PEER_DISCONNECTED,
                          "disconnect called (peer:%s)",
                          this->peerinfo.identifier);

        pthread_mutex_lock (&priv->write_mutex);
        {
                ret = __gf_rdma_disconnect (this);
        }
        pthread_mutex_unlock (&priv->write_mutex);

        return ret;
}


static int32_t
gf_rdma_connect (struct rpc_transport *this, int port)
{
        gf_rdma_private_t   *priv         = NULL;
        int32_t              ret          = 0;
        union gf_sock_union  sock_union   = {{0, }, };
        socklen_t            sockaddr_len = 0;
        gf_rdma_peer_t      *peer         = NULL;
        gf_rdma_ctx_t       *rdma_ctx     = NULL;
        gf_boolean_t         connected    = _gf_false;

        priv = this->private;

        peer = &priv->peer;

        rpc_transport_ref (this);

        ret = gf_rdma_client_get_remote_sockaddr (this,
                                                  &sock_union.sa,
                                                  &sockaddr_len, port);
        if (ret != 0) {
                gf_msg_debug (this->name, 0, "cannot get remote address to "
                              "connect");
                goto out;
        }

        rdma_ctx = this->ctx->ib;

        pthread_mutex_lock (&priv->write_mutex);
        {
                if (peer->cm_id != NULL) {
                        ret = -1;
                        errno = EINPROGRESS;
                        connected = _gf_true;
                        goto unlock;
                }

                priv->entity = GF_RDMA_CLIENT;

                ret = rdma_create_id (rdma_ctx->rdma_cm_event_channel,
                                      &peer->cm_id, this, RDMA_PS_TCP);
                if (ret != 0) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                RDMA_MSG_CM_EVENT_FAILED, "creation of "
                                "rdma_cm_id failed");
                        ret = -errno;
                        goto unlock;
                }

                memcpy (&this->peerinfo.sockaddr, &sock_union.storage,
                        sockaddr_len);
                this->peerinfo.sockaddr_len = sockaddr_len;

                if (port > 0)
                        sock_union.sin.sin_port = htons (port);

                ((struct sockaddr *) &this->myinfo.sockaddr)->sa_family =
                        ((struct sockaddr *)&this->peerinfo.sockaddr)->sa_family;

                ret = gf_rdma_client_bind (this,
                                           (struct sockaddr *)&this->myinfo.sockaddr,
                                           &this->myinfo.sockaddr_len,
                                           peer->cm_id);
                if (ret != 0) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                RDMA_MSG_CLIENT_BIND_FAILED,
                                "client bind failed");
                        goto unlock;
                }

                ret = rdma_resolve_addr (peer->cm_id, NULL, &sock_union.sa,
                                         2000);
                if (ret != 0) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                RDMA_MSG_RDMA_RESOLVE_ADDR_FAILED,
                                "rdma_resolve_addr failed");
                        goto unlock;
                }

                priv->connected = 0;
        }
unlock:
        pthread_mutex_unlock (&priv->write_mutex);

out:
        if (ret != 0) {
                if (!connected) {
                        gf_rdma_teardown (this);
                }

                rpc_transport_unref (this);
        }

        return ret;
}


static int32_t
gf_rdma_listen (rpc_transport_t *this)
{
        union gf_sock_union  sock_union   = {{0, }, };
        socklen_t            sockaddr_len = 0;
        gf_rdma_private_t   *priv         = NULL;
        gf_rdma_peer_t      *peer         = NULL;
        int                  ret          = 0;
        gf_rdma_ctx_t       *rdma_ctx     = NULL;
        char                 service[NI_MAXSERV], host[NI_MAXHOST];
        int                  optval = 2;

        priv = this->private;
        peer = &priv->peer;

        priv->entity = GF_RDMA_SERVER_LISTENER;

        rdma_ctx = this->ctx->ib;

        ret = gf_rdma_server_get_local_sockaddr (this, &sock_union.sa,
                                                 &sockaddr_len);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        RDMA_MSG_NW_ADDR_UNKNOWN,
                        "cannot find network address of server to bind to");
                goto err;
        }

        ret = rdma_create_id (rdma_ctx->rdma_cm_event_channel,
                              &peer->cm_id, this, RDMA_PS_TCP);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        RDMA_MSG_CM_EVENT_FAILED, "creation of rdma_cm_id "
                        "failed");
                goto err;
        }

        memcpy (&this->myinfo.sockaddr, &sock_union.storage,
                sockaddr_len);
        this->myinfo.sockaddr_len = sockaddr_len;

        ret = getnameinfo ((struct sockaddr *)&this->myinfo.sockaddr,
                           this->myinfo.sockaddr_len, host, sizeof (host),
                           service, sizeof (service),
                           NI_NUMERICHOST);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, ret,
                        TRANS_MSG_GET_NAME_INFO_FAILED,
                        "getnameinfo failed");
                goto err;
        }

        sprintf (this->myinfo.identifier, "%s:%s", host, service);

        ret = rdma_set_option(peer->cm_id, RDMA_OPTION_ID,
                              RDMA_OPTION_ID_REUSEADDR,
                              (void *)&optval, sizeof(optval));
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        RDMA_MSG_OPTION_SET_FAILED, "rdma option set failed");
                goto err;
        }

        ret = rdma_bind_addr (peer->cm_id, &sock_union.sa);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        RDMA_MSG_RDMA_BIND_ADDR_FAILED,
                        "rdma_bind_addr failed");
                goto err;
        }

        ret = rdma_listen (peer->cm_id, 10);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        RDMA_MSG_LISTEN_FAILED,
                        "rdma_listen failed");
                goto err;
        }

        rpc_transport_ref (this);

        ret = 0;
err:
        if (ret < 0) {
                if (peer->cm_id != NULL) {
                        rdma_destroy_id (peer->cm_id);
                        peer->cm_id = NULL;
                }
        }

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
        gf_rdma_ctx_t *rdma_ctx = NULL;
        struct iobuf_pool *iobuf_pool = NULL;

        priv = GF_CALLOC (1, sizeof (*priv), gf_common_mt_rdma_private_t);
        if (!priv)
                return -1;

        this->private = priv;

        if (gf_rdma_init (this)) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        RDMA_MSG_INIT_IB_DEVICE_FAILED,
                        "Failed to initialize IB Device");
                this->private = NULL;
                GF_FREE (priv);
                return -1;
        }
        rdma_ctx = this->ctx->ib;
        pthread_mutex_lock (&rdma_ctx->lock);
        {
                if (rdma_ctx != NULL) {
                        if (this->dl_handle && (++(rdma_ctx->dlcount)) == 1) {
                        iobuf_pool = this->ctx->iobuf_pool;
                        iobuf_pool->rdma_registration = gf_rdma_register_arena;
                        iobuf_pool->rdma_deregistration =
                                                      gf_rdma_deregister_arena;
                        gf_rdma_register_iobuf_pool_with_device
                                                (rdma_ctx->device, iobuf_pool);
                        }
                }
        }
        pthread_mutex_unlock (&rdma_ctx->lock);

        return 0;
}

void
fini (struct rpc_transport *this)
{
        /* TODO: verify this function does graceful finish */
        gf_rdma_private_t *priv = NULL;
        struct iobuf_pool *iobuf_pool = NULL;
        gf_rdma_ctx_t *rdma_ctx = NULL;

        priv = this->private;

        this->private = NULL;

        if (priv) {
                pthread_mutex_destroy (&priv->recv_mutex);
                pthread_mutex_destroy (&priv->write_mutex);

                gf_msg_trace (this->name, 0,
                              "called fini on transport: %p", this);
                GF_FREE (priv);
        }

        rdma_ctx = this->ctx->ib;
        if (!rdma_ctx)
                return;

        pthread_mutex_lock (&rdma_ctx->lock);
        {
                if (this->dl_handle && (--(rdma_ctx->dlcount)) == 0) {
                        iobuf_pool = this->ctx->iobuf_pool;
                        gf_rdma_deregister_iobuf_pool (rdma_ctx->device);
                        iobuf_pool->rdma_registration = NULL;
                        iobuf_pool->rdma_deregistration = NULL;
                }
        }
        pthread_mutex_unlock (&rdma_ctx->lock);

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
        { .key   = {"transport.rdma.attr-timeout",
                    "rdma-attr-timeout"},
          .type  = GF_OPTION_TYPE_INT
        },
        { .key   = {"transport.rdma.attr-retry-cnt",
                    "rdma-attr-retry-cnt"},
          .type  = GF_OPTION_TYPE_INT
        },
        { .key   = {"transport.rdma.attr-rnr-retry",
                    "rdma-attr-rnr-retry"},
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
