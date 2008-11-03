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

#include "dict.h"
#include "glusterfs.h"
#include "transport.h"
#include "protocol.h"
#include "logging.h"
#include "xlator.h"
#include "name.h"
#include "ib-verbs.h"
#include <signal.h>

int32_t
gf_resolve_ip6 (const char *hostname, 
		uint16_t port, 
		int family, 
		void **dnscache, 
		struct addrinfo **addr_info);

static uint16_t 
ib_verbs_get_local_lid (struct ibv_context *context,
			int32_t port)
{
  struct ibv_port_attr attr;

  if (ibv_query_port (context, port, &attr))
    return 0;

  return attr.lid;
}


static void
ib_verbs_put_post (ib_verbs_queue_t *queue,
		   ib_verbs_post_t *post)
{
  pthread_mutex_lock (&queue->lock);
  if (post->prev) {
    queue->active_count--;
    post->prev->next = post->next;
  }
  if (post->next)
    post->next->prev = post->prev;
  post->prev = &queue->passive_posts;
  post->next = post->prev->next;
  post->prev->next = post;
  post->next->prev = post;
  queue->passive_count++;
  pthread_mutex_unlock (&queue->lock);
}


static ib_verbs_post_t *
ib_verbs_new_post (ib_verbs_device_t *device, int32_t len)
{
  ib_verbs_post_t *post;

  post = (ib_verbs_post_t *) calloc (1, sizeof (*post));
  if (!post)
    return NULL;

  post->buf_size = len;

  post->buf = valloc (len);
  if (!post->buf) {
    free (post);
    return NULL;
  }

  post->mr = ibv_reg_mr (device->pd,
			 post->buf,
			 post->buf_size,
			 IBV_ACCESS_LOCAL_WRITE);
  if (!post->mr) {
    free (post->buf);
    free (post);
    return NULL;
  }

  return post;
}


static ib_verbs_post_t *
ib_verbs_get_post (ib_verbs_queue_t *queue)
{
  ib_verbs_post_t *post;

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

static void
ib_verbs_destroy_post (ib_verbs_post_t *post)
{
  ibv_dereg_mr (post->mr);
  free (post->buf);
  free (post);
}


static int32_t
__ib_verbs_quota_get (ib_verbs_peer_t *peer)
{
  int32_t ret = -1;
  ib_verbs_private_t *priv = peer->trans->private;

  if (priv->connected && peer->quota > 0) {
    ret = peer->quota--;
  }

  return ret;
}

/*
static int32_t
ib_verbs_quota_get (ib_verbs_peer_t *peer)
{
  int32_t ret = -1;
  ib_verbs_private_t *priv = peer->trans->private;

  pthread_mutex_lock (&priv->write_mutex);
  {
    ret = __ib_verbs_quota_get (peer);
  }
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}
*/

static void 
__ib_verbs_ioq_entry_free (ib_verbs_ioq_t *entry)
{
  list_del_init (&entry->list);
  if (entry->refs)
    dict_unref (entry->refs);

  /* TODO: use mem-pool */
  free (entry->buf);

  /* TODO: use mem-pool */
  free (entry);
}


static void
__ib_verbs_ioq_flush (ib_verbs_peer_t *peer)
{
  ib_verbs_ioq_t *entry = NULL, *dummy = NULL;

  list_for_each_entry_safe (entry, dummy, &peer->ioq, list) {
    __ib_verbs_ioq_entry_free (entry);
  }
}


static int32_t
__ib_verbs_disconnect (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  int32_t ret = 0;

  if (priv->connected || priv->tcp_connected) {
    fcntl (priv->sock, F_SETFL, O_NONBLOCK);
    if (shutdown (priv->sock, SHUT_RDWR) != 0) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "shutdown () - error: %s",
		strerror (errno));
      ret = -errno;
      priv->tcp_connected = 0;
    }
  }
  
  return ret;
}


static int32_t
ib_verbs_post_send (struct ibv_qp *qp,
		    ib_verbs_post_t *post,
		    int32_t len)
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
    return -1;

  return ibv_post_send (qp, &wr, &bad_wr);
}


static int32_t
__ib_verbs_ioq_churn_entry (ib_verbs_peer_t *peer, ib_verbs_ioq_t *entry)
{
  int32_t ret = 0, quota = 0;
  ib_verbs_private_t *priv = peer->trans->private;
  ib_verbs_device_t *device = priv->device;
  ib_verbs_options_t *options = &priv->options;
  int32_t len = 0;

  quota = __ib_verbs_quota_get (peer);
  if (quota > 0) {
    ib_verbs_post_t *post = NULL;

    post = ib_verbs_get_post (&device->sendq);
    if (!post) 
      post = ib_verbs_new_post (device, (options->send_size + 2048));

    len = iov_length ((const struct iovec *)&entry->vector, entry->count);
    iov_unload (post->buf, (const struct iovec *)&entry->vector, entry->count);

    ret = ib_verbs_post_send (peer->qp, post, len);
    if (!ret) {
      __ib_verbs_ioq_entry_free (entry);
      ret = len;
    } else {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "ibv_post_send failed with ret = %d", ret);
      ib_verbs_put_post (&device->sendq, post);
      __ib_verbs_disconnect (peer->trans);
      ret = -1;
    } 
  }
  return ret;
}


static int32_t
__ib_verbs_ioq_churn (ib_verbs_peer_t *peer)
{
  ib_verbs_ioq_t *entry = NULL;
  int32_t ret = 0;

  while (!list_empty (&peer->ioq))
    {
      /* pick next entry */
      entry = peer->ioq_next;

      ret = __ib_verbs_ioq_churn_entry (peer, entry);

      if (ret <= 0)
	break;
    }

  /*
  list_for_each_entry_safe (entry, dummy, &peer->ioq, list) {
    ret = __ib_verbs_ioq_churn_entry (peer, entry);
    if (ret <= 0) {
      break;
    }
  }
  */

  return ret;
}

static int32_t
__ib_verbs_quota_put (ib_verbs_peer_t *peer)
{
  int32_t ret;

  peer->quota++;
  ret = peer->quota;

  if (!list_empty (&peer->ioq)) {
    __ib_verbs_ioq_churn (peer);
  }

  return ret;
}


static int32_t
ib_verbs_quota_put (ib_verbs_peer_t *peer)
{
  int32_t ret;
  ib_verbs_private_t *priv = peer->trans->private;

  pthread_mutex_lock (&priv->write_mutex);
  {
    ret = __ib_verbs_quota_put (peer);
  }
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}


static int32_t
ib_verbs_post_recv (struct ibv_srq *srq,
		    ib_verbs_post_t *post)
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

  return ibv_post_srq_recv (srq, &wr, &bad_wr);
}


static int32_t
ib_verbs_writev (transport_t *this,
		 ib_verbs_ioq_t *entry)
{
  int32_t ret = 0, need_append = 1;
  ib_verbs_private_t *priv = this->private;
  ib_verbs_peer_t  *peer = NULL;

  pthread_mutex_lock (&priv->write_mutex);
  {
    if (!priv->connected) {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "ib-verbs is not connected to post a send request");
      ret = -1;
      goto unlock;
    }

    peer = &priv->peer;
    if (list_empty (&peer->ioq)) {
      ret = __ib_verbs_ioq_churn_entry (peer, entry);
      if (ret > 0) {
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


static ib_verbs_ioq_t *
ib_verbs_ioq_new (char *buf, int len, struct iovec *vector, 
		  int count, dict_t *refs)
{
  ib_verbs_ioq_t *entry = NULL;

  /* TODO: use mem-pool */
  entry = calloc (1, sizeof (*entry));

  assert (count <= (MAX_IOVEC-2));

  entry->header.colonO[0] = ':';
  entry->header.colonO[1] = 'O';
  entry->header.colonO[2] = '\0';
  entry->header.version   = 42;
  entry->header.size1     = hton32 (len);
  entry->header.size2     = hton32 (iov_length (vector, count));

  entry->vector[0].iov_base = &entry->header;
  entry->vector[0].iov_len  = sizeof (entry->header);
  entry->count++;

  entry->vector[1].iov_base = buf;
  entry->vector[1].iov_len  = len;
  entry->count++;

  if (vector && count)
    {
      memcpy (&entry->vector[2], vector, sizeof (*vector) * count);
      entry->count += count;
    }

  if (refs)
    entry->refs = dict_ref (refs);

  entry->buf = buf;

  INIT_LIST_HEAD (&entry->list);

  return entry;
}


static int32_t
ib_verbs_submit (transport_t *this, char *buf, int32_t len,
		 struct iovec *vector, int count, dict_t *refs)
{
  int32_t ret = 0;
  ib_verbs_ioq_t *entry = NULL;
  
  entry = ib_verbs_ioq_new (buf, len, vector, count, refs);
  ret = ib_verbs_writev (this, entry);

  if (ret > 0) {
    ret = 0;
  }

  return ret;
}

static int
ib_verbs_receive (transport_t *this, char **hdr_p, size_t *hdrlen_p,
		  char **buf_p, size_t *buflen_p)
{
  ib_verbs_private_t *priv = this->private;
  /* TODO: return error if !priv->connected, check with locks */
  /* TODO: boundry checks for data_ptr/offset */
  char *copy_from = NULL;
  ib_verbs_header_t *header = NULL;
  uint32_t size1, size2;
  char *hdr = NULL, *buf = NULL;

  pthread_mutex_lock (&priv->recv_mutex);
  {
    while (!priv->data_ptr)
      pthread_cond_wait (&priv->recv_cond, &priv->recv_mutex);

    copy_from = priv->data_ptr + priv->data_offset;

    priv->data_ptr = NULL;
    pthread_cond_broadcast (&priv->recv_cond);
  }
  pthread_mutex_unlock (&priv->recv_mutex);

  header = (ib_verbs_header_t *)copy_from;
  size1 = ntoh32 (header->size1);
  size2 = ntoh32 (header->size2);

  copy_from += sizeof (*header);

  if (size1) {
    hdr = calloc (1, size1);
    memcpy (hdr, copy_from, size1);
    copy_from += size1;
    *hdr_p = hdr;
  }
  *hdrlen_p = size1;

  if (size2) {
    buf = calloc (1, size2);
    memcpy (buf, copy_from, size2);
    *buf_p = buf;
  }
  *buflen_p = size2;

  return 0;
}


static void
ib_verbs_destroy_cq (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  ib_verbs_device_t *device = priv->device;
  
  if (device->recv_cq)
    ibv_destroy_cq (device->recv_cq);
  device->recv_cq = NULL;
  
  if (device->send_cq)
    ibv_destroy_cq (device->send_cq);
  device->send_cq = NULL;

  return;
}


static int32_t
ib_verbs_create_cq (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  ib_verbs_options_t *options = &priv->options;
  ib_verbs_device_t *device = priv->device;
  int32_t ret = 0;

  device->recv_cq = ibv_create_cq (priv->device->context,
				   options->recv_count * 2,
				   device,
				   device->recv_chan,
				   0);
  if (!device->recv_cq) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_ERROR,
	    "%s: creation of CQ failed",
	    this->xl->name);
    ret = -1;
  } else if (ibv_req_notify_cq (device->recv_cq, 0)) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_ERROR,
	    "%s: ibv_req_notify_cq on CQ failed",
	    this->xl->name);
    ret = -1;
  }
    
  do {
    /* TODO: make send_cq size dynamically adaptive */
    device->send_cq = ibv_create_cq (priv->device->context,
				     options->send_count * 1024,
				     device,
				     device->send_chan,
				     0);
    if (!device->send_cq) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "%s: creation of send_cq failed",
	      this->xl->name);
      ret = -1;
      break;
    }

    if (ibv_req_notify_cq (device->send_cq, 0)) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "%s: ibv_req_notify_cq on send_cq failed",
	      this->xl->name);
      ret = -1;
      break;
    }
  } while (0);

  if (ret != 0)
    ib_verbs_destroy_cq (this);

  return ret;
}


static void
ib_verbs_register_peer (ib_verbs_device_t *device,
			int32_t qp_num,
			ib_verbs_peer_t *peer)
{
  struct _qpent *ent;
  ib_verbs_qpreg_t *qpreg = &device->qpreg;
  int32_t hash = qp_num % 42;

  pthread_mutex_lock (&qpreg->lock);
  ent = qpreg->ents[hash].next;
  while ((ent != &qpreg->ents[hash]) && (ent->qp_num != qp_num))
    ent = ent->next;
  if (ent->qp_num == qp_num) {
    pthread_mutex_unlock (&qpreg->lock);
    return;
  }
  ent = (struct _qpent *) calloc (1, sizeof (*ent));
  ERR_ABORT (ent);
  /* TODO: ref reg->peer */
  ent->peer = peer;
  ent->next = &qpreg->ents[hash];
  ent->prev = ent->next->prev;
  ent->next->prev = ent;
  ent->prev->next = ent;
  ent->qp_num = qp_num;
  qpreg->count++;
  pthread_mutex_unlock (&qpreg->lock);
}


static void
ib_verbs_unregister_peer (ib_verbs_device_t *device,
			  int32_t qp_num)
{
  struct _qpent *ent;
  ib_verbs_qpreg_t *qpreg = &device->qpreg;
  int32_t hash = qp_num % 42;

  pthread_mutex_lock (&qpreg->lock);
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
  free (ent);
  qpreg->count--;
  pthread_mutex_unlock (&qpreg->lock);
}


static ib_verbs_peer_t *
ib_verbs_lookup_peer (ib_verbs_device_t *device,
		      int32_t qp_num)
{
  struct _qpent *ent;
  ib_verbs_qpreg_t *qpreg = &device->qpreg;
  ib_verbs_peer_t *peer;
  int32_t hash = qp_num % 42;

  pthread_mutex_lock (&qpreg->lock);
  ent = qpreg->ents[hash].next;
  while ((ent != &qpreg->ents[hash]) && (ent->qp_num != qp_num))
    ent = ent->next;
  peer = ent->peer;
  pthread_mutex_unlock (&qpreg->lock);
  return peer;
}


static void
__ib_verbs_destroy_qp (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;

  if (priv->peer.qp) {
    ib_verbs_unregister_peer (priv->device, priv->peer.qp->qp_num);
    ibv_destroy_qp (priv->peer.qp);
  }
  priv->peer.qp = NULL;

  return;
}


static int32_t
ib_verbs_create_qp (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  ib_verbs_options_t *options = &priv->options;
  ib_verbs_device_t *device = priv->device;
  int32_t ret = 0;
  ib_verbs_peer_t *peer;

  peer = &priv->peer;
  struct ibv_qp_init_attr init_attr = {
    .send_cq        = device->send_cq,
    .recv_cq        = device->recv_cq,
    .srq            = device->srq,
    .cap            = {
      .max_send_wr  = peer->send_count,
      .max_recv_wr  = peer->recv_count,
      .max_send_sge = 1,
      .max_recv_sge = 1
    },
    .qp_type = IBV_QPT_RC
  };
  
  struct ibv_qp_attr attr = {
    .qp_state        = IBV_QPS_INIT,
    .pkey_index      = 0,
    .port_num        = options->port,
    .qp_access_flags = 0
  };
  
  peer->qp = ibv_create_qp (device->pd, &init_attr);
  if (!peer->qp) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_CRITICAL,
	    "%s: could not create QP",
	    this->xl->name);
    ret = -1;
  } else if (ibv_modify_qp (peer->qp, &attr,
		     IBV_QP_STATE              |
		     IBV_QP_PKEY_INDEX         |
		     IBV_QP_PORT               |
		     IBV_QP_ACCESS_FLAGS)) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_ERROR,
	    "%s: failed to modify QP to INIT state",
	    this->xl->name);
    ret = -1;
  }

  peer->local_lid = ib_verbs_get_local_lid (device->context,
					      options->port);
  peer->local_qpn = peer->qp->qp_num;
  peer->local_psn = lrand48 () & 0xffffff;

  ib_verbs_register_peer (device, peer->qp->qp_num, peer);

  if (ret == -1)
    __ib_verbs_destroy_qp (this);

  return ret;
}


static void
ib_verbs_destroy_posts (transport_t *this)
{

}


static int32_t
__ib_verbs_create_posts (transport_t *this,
			 int32_t count,
			 int32_t size,
			 ib_verbs_queue_t *q)
{
  int32_t i;
  int32_t ret = 0;
  ib_verbs_private_t *priv = this->private;
  ib_verbs_device_t *device = priv->device;

  for (i=0 ; i<count ; i++) {
    ib_verbs_post_t *post;

    post = ib_verbs_new_post (device, size + 2048);
    if (!post) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "%s: post creation failed",
	      this->xl->name);
      ret = -1;
      break;
    }

    ib_verbs_put_post (q, post);
  }
  return ret;
}


static int32_t
ib_verbs_create_posts (transport_t *this)
{
  int32_t i, ret;
  ib_verbs_private_t *priv = this->private;
  ib_verbs_options_t *options = &priv->options;
  ib_verbs_device_t *device = priv->device;

  ret =  __ib_verbs_create_posts (this, options->send_count,
				  options->send_size,
				  &device->sendq);
  if (!ret)
    ret =  __ib_verbs_create_posts (this, options->recv_count,
				    options->recv_size,
				    &device->recvq);

  if (!ret) {
    for (i=0 ; i<options->recv_count ; i++) {
      ib_verbs_post_t *post = ib_verbs_get_post (&device->recvq);
      if (ib_verbs_post_recv (device->srq, post) != 0) {
	ret = -1;
	break;
      }
    }
  }

  if (ret)
    ib_verbs_destroy_posts (this);

  return ret;
}


static int32_t
ib_verbs_connect_qp (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  ib_verbs_options_t *options = &priv->options;
  struct ibv_qp_attr attr = {
    .qp_state               = IBV_QPS_RTR,
    .path_mtu               = options->mtu,
    .dest_qp_num            = priv->peer.remote_qpn,
    .rq_psn                 = priv->peer.remote_psn,
    .max_dest_rd_atomic     = 1,
    .min_rnr_timer          = 12,
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
      gf_log ("transport/ib-verbs",
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
    gf_log ("transport/ib-verbs",
	    GF_LOG_CRITICAL,
	    "Failed to modify QP to RTS\n");
    return -1;
  }

  return 0;
}

static int32_t
__ib_verbs_teardown (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;

  __ib_verbs_destroy_qp (this);

  if (!list_empty (&priv->peer.ioq)) {
    __ib_verbs_ioq_flush (&priv->peer);
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
__tcp_rwv (transport_t *this, struct iovec *vector, int count,
	   struct iovec **pending_vector, int *pending_count,
	   int write)
{
  ib_verbs_private_t *priv = NULL;
  int sock = -1;
  int ret = -1;
  struct iovec *opvector = vector;
  int opcount = count;
  int moved = 0;

  priv = this->private;
  sock = priv->sock;

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
	  total_bytes_xferd += ret;
	}
      else
	{
	  ret = readv (sock, opvector, opcount);

	  if (ret == -1 && errno == EAGAIN)
	    {
	      /* done for now */
	      break;
	    }
	  total_bytes_rcvd += ret;
	}

      if (ret == 0)
	{
	  gf_log (this->xl->name, GF_LOG_ERROR, "EOF from peer");
	  opcount = -1;
	  errno = ENOTCONN;
	  break;
	}

      if (ret == -1)
	{
	  if (errno == EINTR)
	    continue;

	  gf_log (this->xl->name, GF_LOG_ERROR,
		  "%s failed (%s)", write ? "writev" : "readv",
		  strerror (errno));
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
__tcp_readv (transport_t *this, struct iovec *vector, int count,
	     struct iovec **pending_vector, int *pending_count)
{
  int ret = -1;

  ret = __tcp_rwv (this, vector, count, pending_vector, pending_count, 0);

  return ret;
}


static int
__tcp_writev (transport_t *this, struct iovec *vector, int count,
	      struct iovec **pending_vector, int *pending_count)
{
  int ret = -1;
  ib_verbs_private_t *priv = this->private;

  ret = __tcp_rwv (this, vector, count, pending_vector, pending_count, 1);

  if (ret > 0) {
    /* TODO: Avoid multiple calls when socket is already registered for POLLOUT */
    priv->idx = event_select_on (this->xl->ctx->event_pool, priv->sock,
				 priv->idx, -1, 1);
  } else if (ret == 0) {
    priv->idx = event_select_on (this->xl->ctx->event_pool, priv->sock,
				 priv->idx, -1, 0);
  }

  return ret;
}


static void *
ib_verbs_recv_completion_proc (void *data)
{
  struct ibv_comp_channel *chan = data;
  int32_t ret;

  while (1) {
    struct ibv_cq *event_cq;
    void *event_ctx;
    ib_verbs_device_t *device;
    struct ibv_wc wc;

    ret = ibv_get_cq_event (chan, &event_cq, &event_ctx);
    if (ret) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "ibv_get_cq_event failed, terminating recv thread %d (%d)", ret, errno);
      continue;
    }

    device = event_ctx;
    
    ret = ibv_req_notify_cq (event_cq, 0);
    if (ret) {
      gf_log ("transport/ib-verbs", GF_LOG_ERROR,
	      "ibv_req_notify_cq on %s failed, terminating recv thread: %d (%d)",
	      device->device_name, ret, errno);
      continue;
    }

    device = (ib_verbs_device_t *) event_ctx;

    while ((ret = ibv_poll_cq (event_cq, 1, &wc)) > 0) {
      ib_verbs_post_t *post;
      ib_verbs_peer_t *peer;

      post = (ib_verbs_post_t *) (long) wc.wr_id;
      peer = ib_verbs_lookup_peer (device, wc.qp_num);

      if (wc.status != IBV_WC_SUCCESS) {
	gf_log ("transport/ib-verbs",
		GF_LOG_ERROR,
		"recv work request on `%s' returned error (%d)",
		device->device_name,
		wc.status);
	if (peer)
	  transport_disconnect (peer->trans);

	if (post) {
	  ib_verbs_post_recv (device->srq, post);
	}
      	continue;
      }

      if (peer) {
	ib_verbs_private_t *priv = peer->trans->private;
	
	pthread_mutex_lock (&priv->recv_mutex);
	{
	  while (priv->data_ptr)
	    pthread_cond_wait (&priv->recv_cond, &priv->recv_mutex);
	  
	  priv->data_ptr = post->buf;
	  priv->data_offset = 0;
	  priv->data_len = wc.byte_len;
	  
	  pthread_cond_broadcast (&priv->recv_cond);
	}
	pthread_mutex_unlock (&priv->recv_mutex);
	
	if ((ret = peer->trans->xl->notify (peer->trans->xl, GF_EVENT_POLLIN, 
					    peer->trans, NULL)) == -1) {
	  gf_log ("transport/ib-verbs",
		  GF_LOG_ERROR, 
		  "pollin notification to %s failed, disconnecting transport", 
		  peer->trans->xl->name);
	  transport_disconnect (peer->trans);
	}
      } else {
	gf_log ("transport/ib-verbs",
		GF_LOG_DEBUG,
		"could not lookup peer for qp_num: %d",
		wc.qp_num);
      }
      ib_verbs_post_recv (device->srq, post);
    }
    
    if (ret < 0) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "ibv_poll_cq on `%s' returned error (ret = %d, errno = %d)",
	      device->device_name, ret, errno);
      continue;
    }
    ibv_ack_cq_events (event_cq, 1);
  }
  return NULL;
}


static void *
ib_verbs_send_completion_proc (void *data)
{
  struct ibv_comp_channel *chan = data;

  int32_t ret;

  while (1) {
    struct ibv_cq *event_cq;
    void *event_ctx;
    ib_verbs_device_t *device;
    struct ibv_wc wc;

    ret = ibv_get_cq_event (chan, &event_cq, &event_ctx);
    if (ret) {
      gf_log ("transport/ib-verbs", GF_LOG_ERROR,
	      "ibv_get_cq_event on failed, terminating send thread: %d (%d)", ret, errno);
      continue;
    }
       
    device = event_ctx;

    ret = ibv_req_notify_cq (event_cq, 0);
    if (ret) {
      gf_log ("transport/ib-verbs",  GF_LOG_ERROR,
	      "ibv_req_notify_cq on %s failed, terminating send thread: %d (%d)",
	      device->device_name, ret, errno);
      continue;
    }

    while ((ret = ibv_poll_cq (event_cq, 1, &wc)) > 0) {
      ib_verbs_post_t *post;
      ib_verbs_peer_t *peer;

      post = (ib_verbs_post_t *) (long) wc.wr_id;
      peer = ib_verbs_lookup_peer (device, wc.qp_num);

      if (wc.status != IBV_WC_SUCCESS) {
	gf_log ("transport/ib-verbs",
		GF_LOG_ERROR,
		"send work request on `%s' returned error wc.status = %d, "
		"wc.vendor_err = %d, post->buf = %p, wc.byte_len = %d, "
		"post->reused = %d",
		device->device_name, wc.status, wc.vendor_err,
		post->buf, wc.byte_len, post->reused);
	if (peer)
	  transport_disconnect (peer->trans);
      }

      if (post) {
	      ib_verbs_put_post (&device->sendq, post);
      }
      
      if (peer) {
	ib_verbs_quota_put (peer);
      } else {
	gf_log ("transport/ib-verbs",
		GF_LOG_DEBUG,
		"could not lookup peer for qp_num: %d",
		wc.qp_num);
      }
    }

    if (ret < 0) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "ibv_poll_cq on `%s' returned error (ret = %d, errno = %d)",
	      device->device_name, ret, errno);
      continue;
    }
    ibv_ack_cq_events (event_cq, 1); 
  }

  return NULL;
}

static void
ib_verbs_options_init (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  ib_verbs_options_t *options = &priv->options;
  int32_t mtu;
  data_t *temp;

  /* TODO: validate arguments from options below */

  options->send_size = 1048576;
  options->recv_size = 1048576;
  options->send_count = 16;
  options->recv_count = 16;

  temp = dict_get (this->xl->options,
                   "ib-verbs-work-request-send-count");
  if (temp)
    options->send_count = data_to_int32 (temp);

  temp = dict_get (this->xl->options,
                   "ib-verbs-work-request-recv-count");
  if (temp)
    options->recv_count = data_to_int32 (temp);

  temp = dict_get (this->xl->options,
                   "ib-verbs-work-request-send-size");
  if (temp)
    options->send_size = data_to_int32 (temp);

  temp = dict_get (this->xl->options,
                   "ib-verbs-work-request-recv-size");
  if (temp)
    options->recv_size = data_to_int32 (temp);

  options->port = 1;
  temp = dict_get (this->xl->options,
                   "ib-verbs-port");
  if (temp)
    options->port = data_to_uint64 (temp);

  options->mtu = mtu = IBV_MTU_2048;
  temp = dict_get (this->xl->options,
                   "ib-verbs-mtu");
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
      gf_log ("transport/ib-verbs",
              GF_LOG_ERROR,
              "%s: unrecognized MTU value '%s', defaulting to '2048'",
              this->xl->name,
              data_to_str (temp));
    else
      gf_log ("transport/ib-verbs",
              GF_LOG_DEBUG,
              "%s: defaulting MTU to '2048'",
              this->xl->name);
    options->mtu = IBV_MTU_2048;
    break;
  }

  temp = dict_get (this->xl->options,
                   "ib-verbs-device-name");
  if (temp)
    options->device_name = strdup (temp->data);

  return;
}

static void
ib_verbs_queue_init (ib_verbs_queue_t *queue)
{
  pthread_mutex_init (&queue->lock, NULL);

  queue->active_posts.next = &queue->active_posts;
  queue->active_posts.prev = &queue->active_posts;
  queue->passive_posts.next = &queue->passive_posts;
  queue->passive_posts.prev = &queue->passive_posts;
}


static ib_verbs_device_t *
ib_verbs_get_device (transport_t *this,
		     struct ibv_device *ib_dev,
		     int32_t port)
{
  glusterfs_ctx_t *ctx = this->xl->ctx;
  ib_verbs_private_t *priv = this->private;
  ib_verbs_options_t *options = &priv->options;
  char *device_name = priv->options.device_name;
  int32_t ret = 0, i = 0;

  ib_verbs_device_t *trav;

  trav = ctx->ib;
  while (trav) {
    if ((!strcmp (trav->device_name, device_name)) && 
	(trav->port == port))
      break;
    trav = trav->next;
  }

  if (!trav) {
    struct ibv_context *ibctx = ibv_open_device (ib_dev);
  
    if (!ibctx) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "cannot open device `%s'",
	      device_name);
      return NULL;
    }

    trav = calloc (1, sizeof (*trav));
    ERR_ABORT (trav);
    priv->device = trav;

    trav->context = ibctx;
    trav->device_name = strdup (device_name);
    trav->port = port;

    trav->next = ctx->ib;
    ctx->ib = trav;

    trav->send_chan = ibv_create_comp_channel (trav->context);
    if (!trav->send_chan) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_CRITICAL,
	      "%s: could not create send completion channel",
	      device_name);
      /* TODO: cleanup current mess */
      return NULL;
    }
    
    trav->recv_chan = ibv_create_comp_channel (trav->context);
    if (!trav->recv_chan) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_CRITICAL,
	      "could not create recv completion channel");
      /* TODO: cleanup current mess */
      return NULL;
    }
      
    if (ib_verbs_create_cq (this) < 0) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "%s: could not create CQ",
	      this->xl->name);
      return NULL;
    }

    /* protection domain */
    trav->pd = ibv_alloc_pd (trav->context);

    if (!trav->pd) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_CRITICAL,
	      "%s: could not allocate protection domain",
	      this->xl->name);
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
      gf_log ("transport/ib-verbs",
		GF_LOG_CRITICAL,
	      "%s: could not create SRQ",
	      this->xl->name);
      return NULL;
    }
    
    /* queue init */
    ib_verbs_queue_init (&trav->sendq);
    ib_verbs_queue_init (&trav->recvq);

    if (ib_verbs_create_posts (this) < 0) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "%s: could not allocate posts",
	      this->xl->name);
      return NULL;
    }

    /* completion threads */
    ret = pthread_create (&trav->send_thread,
			  NULL,
			  ib_verbs_send_completion_proc,
			  trav->send_chan);
    if (ret) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "could not create send completion thread");
      return NULL;
    }
    ret = pthread_create (&trav->recv_thread,
			  NULL,
			  ib_verbs_recv_completion_proc,
			  trav->recv_chan);
    if (ret) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
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
ib_verbs_init (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  ib_verbs_options_t *options = &priv->options;

  ib_verbs_options_init (this);

  {
    struct ibv_device **dev_list;
    struct ibv_device *ib_dev = NULL;
    int32_t i;

    dev_list = ibv_get_device_list (NULL);

    if (!dev_list) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_CRITICAL,
	      "No IB devices found");
      return -1;
    }

    if (!options->device_name) {
      if (*dev_list) {
	options->device_name = strdup (ibv_get_device_name (*dev_list));
      } else {
	gf_log ("transport/ib-verbs", GF_LOG_CRITICAL,
		"IB device list is empty. Check for 'ib_uverbs' module");
	return -1;
      }
    }

    for (i = 0; dev_list[i]; i++) {
      if (!strcmp (ibv_get_device_name (dev_list[i]),
		   options->device_name)) {
	ib_dev = dev_list[i];
	break;
      }
    }

    if (!ib_dev) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "could not open device `%s' (does not exist)",
	      options->device_name);
      ibv_free_device_list (dev_list);
      return -1;
    }

    priv->device = ib_verbs_get_device (this, ib_dev, options->port);
    
    if (!priv->device) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "could not create ib_verbs device for %s", options->device_name);
      ibv_free_device_list (dev_list);
      return -1;
    }
    ibv_free_device_list (dev_list);
  }

  priv->peer.trans = this;
  INIT_LIST_HEAD (&priv->peer.ioq);
  
  pthread_mutex_init (&priv->read_mutex, NULL);
  pthread_mutex_init (&priv->write_mutex, NULL);
  pthread_mutex_init (&priv->recv_mutex, NULL);
  pthread_cond_init (&priv->recv_cond, NULL);

  return 0;
}

static int32_t
ib_verbs_disconnect (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  int32_t ret = 0;
 
  pthread_mutex_lock (&priv->write_mutex);
  {
    ret = __ib_verbs_disconnect (this);
  }
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}


static int32_t
__tcp_connect_finish (int fd)
{
  int ret = -1;
  int optval = 0;
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
ib_verbs_fill_handshake_data (char *buf, struct ib_verbs_nbio *nbio, ib_verbs_private_t *priv)
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
ib_verbs_fill_handshake_ack (char *buf, struct ib_verbs_nbio *nbio)
{
  sprintf (buf, "DONE\n");
  nbio->vector.iov_base = buf;
  nbio->vector.iov_len = strlen (buf) + 1;
  nbio->count = 1;
  return;
}

static int
ib_verbs_handshake_pollin (transport_t *this)
{
  int ret = 0;
  ib_verbs_private_t *priv = this->private;
  char *buf = priv->handshake.incoming.buf;
  int32_t recv_buf_size, send_buf_size;
  socklen_t sock_len;

  if (priv->handshake.incoming.state == IB_VERBS_HANDSHAKE_COMPLETE) {
    return -1;
  }

  pthread_mutex_lock (&priv->write_mutex);
  {
    while (priv->handshake.incoming.state != IB_VERBS_HANDSHAKE_COMPLETE)
      {
	switch (priv->handshake.incoming.state) 
	  {
	  case IB_VERBS_HANDSHAKE_START:
	    buf = priv->handshake.incoming.buf = calloc (1, 256);
	    ib_verbs_fill_handshake_data (buf, &priv->handshake.incoming, priv);
	    buf[0] = 0;
	    priv->handshake.incoming.state = IB_VERBS_HANDSHAKE_RECEIVING_DATA;
	    break;

	  case IB_VERBS_HANDSHAKE_RECEIVING_DATA:
	    ret = __tcp_readv (this, 
			       &priv->handshake.incoming.vector, 
			       priv->handshake.incoming.count,
			       &priv->handshake.incoming.pending_vector, 
			       &priv->handshake.incoming.pending_count);
	    if (ret == -1) {
	      goto unlock;
	    }

	    if (ret > 0) {
	      gf_log (this->xl->name, GF_LOG_DEBUG,
		      "partial header read on NB socket. continue later");
	      goto unlock;
	    }
	    
	    if (!ret) {
	      priv->handshake.incoming.state = IB_VERBS_HANDSHAKE_RECEIVED_DATA;
	    }
	    break;

	  case IB_VERBS_HANDSHAKE_RECEIVED_DATA:
	    if (strncmp (buf, "QP1:", 4)) {
	      gf_log ("transport/ib-verbs",
		      GF_LOG_CRITICAL,
		      "%s: remote-host's transport type is different",
		      this->xl->name);
	      ret = -1;
	      goto unlock;
	    }
	    ret = sscanf (buf,
			  "QP1:RECV_BLKSIZE=%08x:SEND_BLKSIZE=%08x\n"
			  "QP1:LID=%04x:QPN=%06x:PSN=%06x\n",
			  &recv_buf_size,
			  &send_buf_size,
			  &priv->peer.remote_lid,
			  &priv->peer.remote_qpn,
			  &priv->peer.remote_psn);

	    if (ret != 5) {
	      gf_log ("transport/ib-verbs",
		      GF_LOG_ERROR,
		      "%s: %d conversions in handshake data rather than 5",
		      this->xl->name,
		      ret);
	      ret = -1;
	      goto unlock;
	    }

	    if (recv_buf_size < priv->peer.recv_size)
	      priv->peer.recv_size = recv_buf_size;
	    if (send_buf_size < priv->peer.send_size)
	      priv->peer.send_size = send_buf_size;
	  
	    gf_log ("transport/ib-verbs",
		    GF_LOG_DEBUG,
		    "%s: transacted recv_size=%d send_size=%d",
		    this->xl->name, priv->peer.recv_size,
		    priv->peer.send_size);

	    priv->peer.quota = priv->peer.send_count;

	    if (ib_verbs_connect_qp (this)) {
	      gf_log ("transport/ib-verbs",
		      GF_LOG_ERROR,
		      "%s: failed to connect with remote QP",
		      this->xl->name);
	      ret = -1;
	      goto unlock;
	    }
	    ib_verbs_fill_handshake_ack (buf, &priv->handshake.incoming);
	    buf[0] = 0;
	    priv->handshake.incoming.state = IB_VERBS_HANDSHAKE_RECEIVING_ACK;
	    break;

	  case IB_VERBS_HANDSHAKE_RECEIVING_ACK:
	    ret = __tcp_readv (this, 
			       &priv->handshake.incoming.vector, 
			       priv->handshake.incoming.count,
			       &priv->handshake.incoming.pending_vector, 
			       &priv->handshake.incoming.pending_count);
	    if (ret == -1) {
	      goto unlock;
	    }

	    if (ret > 0) {
	      gf_log (this->xl->name, GF_LOG_DEBUG,
		      "partial header read on NB socket. continue later");
	      goto unlock;
	    }
	    
	    if (!ret) {
	      priv->handshake.incoming.state = IB_VERBS_HANDSHAKE_RECEIVED_ACK;
	    }
	    break;

	  case IB_VERBS_HANDSHAKE_RECEIVED_ACK:
	    if (strncmp (buf, "DONE", 4)) {
	      gf_log ("transport/ib-verbs", GF_LOG_ERROR,
		      "%s: handshake-3 did not return 'DONE' (%s)",
		      this->xl->name, buf);
	      ret = -1;
	      goto unlock;
	    }
	    ret = 0;
	    priv->connected = 1;
	    sock_len = sizeof (struct sockaddr_storage);
	    getpeername (priv->sock,
			 (struct sockaddr *) &this->peerinfo.sockaddr,
			 &sock_len);

	    FREE (priv->handshake.incoming.buf);
	    priv->handshake.incoming.buf = NULL;
	    priv->handshake.incoming.state = IB_VERBS_HANDSHAKE_COMPLETE;
	  }
      }
  }
 unlock:
  pthread_mutex_unlock (&priv->write_mutex);

  if (ret == -1) {
    transport_disconnect (this);
  } else {
    ret = 0;
  }

  if (!ret && priv->connected) {
    ret = this->xl->notify (this->xl, GF_EVENT_CHILD_UP, this);
  }

  return ret;
}

static int 
ib_verbs_handshake_pollout (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  char *buf = priv->handshake.outgoing.buf;
  int32_t ret = 0;

  if (priv->handshake.outgoing.state == IB_VERBS_HANDSHAKE_COMPLETE) {
    return 0;
  }

  pthread_mutex_unlock (&priv->write_mutex);
  {
    while (priv->handshake.outgoing.state != IB_VERBS_HANDSHAKE_COMPLETE)
      {
	switch (priv->handshake.outgoing.state) 
	  {
	  case IB_VERBS_HANDSHAKE_START:
	    buf = priv->handshake.outgoing.buf = calloc (1, 256);
	    ib_verbs_fill_handshake_data (buf, &priv->handshake.outgoing, priv);
	    priv->handshake.outgoing.state = IB_VERBS_HANDSHAKE_SENDING_DATA;
	    break;

	  case IB_VERBS_HANDSHAKE_SENDING_DATA:
	    ret = __tcp_writev (this, 
				&priv->handshake.outgoing.vector, 
				priv->handshake.outgoing.count,
				&priv->handshake.outgoing.pending_vector, 
				&priv->handshake.outgoing.pending_count);
	    if (ret == -1) {
	      goto unlock;
	    }

	    if (ret > 0) {
	      gf_log (this->xl->name, GF_LOG_DEBUG,
		      "partial header read on NB socket. continue later");
	      goto unlock;
	    }
	    
	    if (!ret) {
	      priv->handshake.outgoing.state = IB_VERBS_HANDSHAKE_SENT_DATA;
	    }
	    break;

	  case IB_VERBS_HANDSHAKE_SENT_DATA:
	    ib_verbs_fill_handshake_ack (buf, &priv->handshake.outgoing);
	    priv->handshake.outgoing.state = IB_VERBS_HANDSHAKE_SENDING_ACK;
	    break;

	  case IB_VERBS_HANDSHAKE_SENDING_ACK:
	    ret = __tcp_writev (this,
				&priv->handshake.outgoing.vector,
				priv->handshake.outgoing.count,
				&priv->handshake.outgoing.pending_vector,
				&priv->handshake.outgoing.pending_count);

	    if (ret == -1) {
	      goto unlock;
	    }

	    if (ret > 0) {
	      gf_log (this->xl->name, GF_LOG_DEBUG,
		      "partial header read on NB socket. continue later");
	      goto unlock;
	    }
	    
	    if (!ret) {
	      FREE (priv->handshake.outgoing.buf);
	      priv->handshake.outgoing.buf = NULL;
	      priv->handshake.outgoing.state = IB_VERBS_HANDSHAKE_COMPLETE;
	    }
	    break;
	  }
      }
  }
 unlock:
  pthread_mutex_unlock (&priv->write_mutex);

  if (ret == -1) {
    transport_disconnect (this);
  } else {
    ret = 0;
  }

  return ret;
}

static int
ib_verbs_handshake_pollerr (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  int32_t ret = 0;
  char need_unref = 0;

  gf_log ("transport/ib-verbs",
	  GF_LOG_DEBUG,
	  "%s: peer disconnected, cleaning up",
	  this->xl->name);

  pthread_mutex_lock (&priv->write_mutex);
  {
    __ib_verbs_teardown (this);

    if (priv->sock != -1) {
      event_unregister (this->xl->ctx->event_pool, priv->sock, priv->idx);
      need_unref = 1;

      if (close (priv->sock) != 0) {
	gf_log ("transport/ib-verbs", GF_LOG_ERROR,
		"close () - error: %s",
		strerror (errno));
	ret = -errno;
      }
      priv->tcp_connected = priv->connected = 0;
      priv->sock = -1;
    }

    if (priv->handshake.incoming.buf) {
      FREE (priv->handshake.incoming.buf);
      priv->handshake.incoming.buf = NULL;
    }

    priv->handshake.incoming.state = IB_VERBS_HANDSHAKE_START;

    if (priv->handshake.outgoing.buf) {
      FREE (priv->handshake.outgoing.buf);
      priv->handshake.outgoing.buf = NULL;
    }

    priv->handshake.outgoing.state = IB_VERBS_HANDSHAKE_START;
  }
  pthread_mutex_unlock (&priv->write_mutex);

  this->xl->notify (this->xl, GF_EVENT_POLLERR, this, NULL);

  if (need_unref)
    transport_unref (this);

  return 0;
}


static int
tcp_connect_finish (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  int error = 0, ret = 0;

  pthread_mutex_lock (&priv->write_mutex);
  {
    ret = __tcp_connect_finish (priv->sock);

    if (!ret) {
      this->myinfo.sockaddr_len = sizeof (this->myinfo.sockaddr);
      ret = getsockname (priv->sock,
			 (struct sockaddr *)&this->myinfo.sockaddr, 
			 &this->myinfo.sockaddr_len);
      if (ret == -1) 
	{
	  gf_log (this->xl->name, GF_LOG_ERROR,
		  "getsockname on new client-socket %d failed (%s)", 
		  priv->sock, strerror (errno));
	  close (priv->sock);
	  error = 1;
	  goto unlock;
	}

      get_transport_identifiers (this);
      priv->tcp_connected = 1;
    }

    if (ret == -1 && errno != EINPROGRESS) {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "tcp connect to %s failed (%s)", 
	      this->peerinfo.identifier, strerror (errno));
      error = 1;
    }
  }
 unlock:
  pthread_mutex_unlock (&priv->write_mutex);

  if (error) {
    transport_disconnect (this);
  }

  return ret;
}

static int
ib_verbs_event_handler (int fd, int idx, void *data,
			int poll_in, int poll_out, int poll_err)
{
  transport_t *this = data;
  ib_verbs_private_t *priv = this->private;
  int ret = 0;

  if (!priv->tcp_connected) {
    ret = tcp_connect_finish (this);
    if (priv->tcp_connected) {
      ib_verbs_options_t *options = &priv->options;

      priv->peer.send_count = options->send_count;
      priv->peer.recv_count = options->recv_count;
      priv->peer.send_size = options->send_size;
      priv->peer.recv_size = options->recv_size;

      if ((ret = ib_verbs_create_qp (this)) < 0) {
	gf_log ("transport/ib-verbs",
		GF_LOG_ERROR,
		"%s: could not create QP",
		this->xl->name);
	transport_disconnect (this);
      }
    }
  }

  if (!ret && poll_out && priv->tcp_connected) {
    ret = ib_verbs_handshake_pollout (this);
  }

  if (!ret && poll_in && priv->tcp_connected) {
    if (priv->handshake.incoming.state == IB_VERBS_HANDSHAKE_COMPLETE) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "%s: pollin received on tcp socket (peer: %s) after handshake is complete",
	      this->xl->name, this->peerinfo.identifier);
      ib_verbs_handshake_pollerr (this);
      return 0;
    }
    ret = ib_verbs_handshake_pollin (this);
  }

  if (poll_err) {
    ret = ib_verbs_handshake_pollerr (this);
  }

  return 0;
}

static int
__tcp_nonblock (int fd)
{
  int flags = 0;
  int ret = -1;

  flags = fcntl (fd, F_GETFL);

  if (flags != -1)
    ret = fcntl (fd, F_SETFL, flags | O_NONBLOCK);

  return ret;
}

static int32_t
ib_verbs_connect (struct transport *this)
{
  GF_ERROR_IF_NULL (this);
  dict_t *options = this->xl->options;
  
  ib_verbs_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);
  
  int32_t ret = 0;
  gf_boolean_t non_blocking = 1;
  struct sockaddr_storage sockaddr;
  socklen_t sockaddr_len = 0;

  if (priv->connected) {
    return 0;
  }

  if (dict_get (options, "non-blocking-io")) {
	  char *nb_connect = data_to_str (dict_get (this->xl->options,
						    "non-blocking-io"));
	  
	  if (gf_string2boolean (nb_connect, &non_blocking) == -1) {
		  gf_log (this->xl->name, GF_LOG_ERROR,
			  "'non-blocking-io' takes only boolean options, not taking any action");
		  non_blocking = 1;
	  }
  }

  ret = client_get_remote_sockaddr (this, (struct sockaddr *)&sockaddr, &sockaddr_len);
  if (ret != 0) {
    gf_log (this->xl->name,
	    GF_LOG_ERROR,
	    "cannot get remote address to connect");
    return ret;
  }

  pthread_mutex_lock (&priv->write_mutex);
  {
    if (priv->sock != -1) {
      ret = 0;
      goto unlock;
    }
  
    priv->sock = socket (((struct sockaddr *) &sockaddr)->sa_family , SOCK_STREAM, 0);
	
    if (priv->sock == -1) {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "socket () - error: %s", strerror (errno));
      ret = -errno;
      goto unlock;
    }

    gf_log (this->xl->name, GF_LOG_DEBUG,
	    "socket fd = %d", priv->sock);

    memcpy (&this->peerinfo.sockaddr, &sockaddr, sockaddr_len);
    this->peerinfo.sockaddr_len = sockaddr_len;

    ((struct sockaddr *) &this->myinfo.sockaddr)->sa_family = ((struct sockaddr *)&this->peerinfo.sockaddr)->sa_family;

    if (non_blocking) 
      {
	ret = __tcp_nonblock (priv->sock);
	
	if (ret == -1)
	  {
	    gf_log (this->xl->name, GF_LOG_ERROR,
		    "could not set socket %d to non blocking mode (%s)",
		    priv->sock, strerror (errno));
	    close (priv->sock);
	    priv->sock = -1;
	    goto unlock;
	  }
      }

    ret = client_bind (this, (struct sockaddr *)&this->myinfo.sockaddr, 
		       &this->myinfo.sockaddr_len, priv->sock);
    if (ret == -1)
      {
	gf_log (this->xl->name, GF_LOG_WARNING,
		"client bind failed", strerror (errno));
	close (priv->sock);
	priv->sock = -1;
	goto unlock;
      }

    ret = connect (priv->sock, (struct sockaddr *)&this->peerinfo.sockaddr, this->peerinfo.sockaddr_len);
    if (ret == -1 && errno != EINPROGRESS)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"connection attempt failed (%s)", strerror (errno));
	close (priv->sock);
	priv->sock = -1;
	goto unlock;
      }

    priv->tcp_connected = priv->connected = 0;

    transport_ref (this);

    priv->handshake.incoming.state = priv->handshake.outgoing.state = IB_VERBS_HANDSHAKE_START;
    priv->idx = event_register (this->xl->ctx->event_pool, priv->sock, 
				ib_verbs_event_handler, this, 1, 1); 
  }
 unlock:
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}

static int
ib_verbs_server_event_handler (int fd, int idx, void *data,
			       int poll_in, int poll_out, int poll_err)
{
  int32_t main_sock = -1;
  transport_t *this, *trans = data;
  ib_verbs_private_t *priv = NULL;
  ib_verbs_private_t *trans_priv = (ib_verbs_private_t *) trans->private;
  ib_verbs_options_t *options = NULL;

  if (!poll_in)
    return 0;

  this = calloc (1, sizeof (transport_t));
  ERR_ABORT (this);
  priv = calloc (1, sizeof (ib_verbs_private_t));
  ERR_ABORT (priv);
  this->private = priv;
  /* Copy all the ib_verbs related values in priv, from trans_priv as other than QP, 
     all the values remain same */
  priv->device = trans_priv->device;
  priv->options = trans_priv->options;
  options = &priv->options;

  this->ops = trans->ops;
  this->xl = trans->xl;

  memcpy (&this->myinfo.sockaddr, &trans->myinfo.sockaddr, trans->myinfo.sockaddr_len);
  this->myinfo.sockaddr_len = trans->myinfo.sockaddr_len;

  main_sock = (trans_priv)->sock;
  this->peerinfo.sockaddr_len = sizeof (this->peerinfo.sockaddr);
  priv->sock = accept (main_sock, (struct sockaddr *)&this->peerinfo.sockaddr, &this->peerinfo.sockaddr_len);
  if (priv->sock == -1) {
    gf_log ("ib-verbs/server",
	    GF_LOG_ERROR,
	    "accept() failed: %s",
	    strerror (errno));
    free (this->private);
    free (this);
    return -1;
  }

  priv->peer.trans = this;
  transport_ref (this);

  get_transport_identifiers (this);

  priv->tcp_connected = 1;
  priv->handshake.incoming.state = priv->handshake.outgoing.state = IB_VERBS_HANDSHAKE_START;

  priv->peer.send_count = options->send_count;
  priv->peer.recv_count = options->recv_count;
  priv->peer.send_size = options->send_size;
  priv->peer.recv_size = options->recv_size;
  INIT_LIST_HEAD (&priv->peer.ioq);

  if (ib_verbs_create_qp (this) < 0) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_ERROR,
	    "%s: could not create QP",
	    this->xl->name);
    transport_disconnect (this);
    return -1;
  }

  priv->idx = event_register (this->xl->ctx->event_pool, priv->sock,
			      ib_verbs_event_handler, this, 1, 1);

  pthread_mutex_init (&priv->read_mutex, NULL);
  pthread_mutex_init (&priv->write_mutex, NULL);
  pthread_mutex_init (&priv->recv_mutex, NULL);
  pthread_cond_init (&priv->recv_cond, NULL);

  return 0;
}

static int32_t
ib_verbs_listen (transport_t *this)
{
  struct sockaddr_storage sockaddr;
  socklen_t sockaddr_len;
  ib_verbs_private_t *priv = this->private;
  int opt = 1, ret = 0;
  char service[NI_MAXSERV], host[NI_MAXHOST];

  memset (&sockaddr, 0, sizeof (sockaddr));
  ret = server_get_local_sockaddr (this, (struct sockaddr *)&sockaddr, &sockaddr_len);
  if (ret != 0) {
    gf_log (this->xl->name,
	    GF_LOG_ERROR,
	    "cannot find network address of server to bind to");
    goto err;
  }

  priv->sock = socket (((struct sockaddr *)&sockaddr)->sa_family, SOCK_STREAM, 0);
  if (priv->sock == -1) {
    gf_log ("ib-verbs/server",
	    GF_LOG_CRITICAL,
	    "init: failed to create socket, error: %s",
	    strerror (errno));
    free (this->private);
    ret = -1;
    goto err;
  }

  memcpy (&this->myinfo.sockaddr, &sockaddr, sockaddr_len);
  this->myinfo.sockaddr_len = sockaddr_len;

  ret = getnameinfo ((struct sockaddr *)&this->myinfo.sockaddr, 
		     this->myinfo.sockaddr_len,
		     host, sizeof (host),
		     service, sizeof (service),
		     NI_NUMERICHOST);
  if (ret != 0) {
    gf_log (this->xl->name,
	    GF_LOG_ERROR,
	    "getnameinfo failed (%s)", gai_strerror (ret));
    goto err;
  }
  sprintf (this->myinfo.identifier, "%s:%s", host, service);
 
  setsockopt (priv->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));
  if (bind (priv->sock,
	    (struct sockaddr *)&sockaddr,
	    sockaddr_len) != 0) {
	ret = -1;
	gf_log ("ib-verbs/server",
		GF_LOG_CRITICAL,
		"init: failed to bind to socket for %s (%s)",
		this->myinfo.identifier, strerror (errno));
	goto err;
      }

  if (listen (priv->sock, 10) != 0) {
    gf_log ("ib-verbs/server",
	    GF_LOG_CRITICAL,
	    "init: listen () failed on socket for %s (%s)",
	    this->myinfo.identifier, strerror (errno));
    ret = -1;
    goto err;
  }

  /* Register the main socket */
  priv->idx = event_register (this->xl->ctx->event_pool, priv->sock,
			      ib_verbs_server_event_handler, transport_ref (this), 1, 0);

  err:
      return ret;
}

struct transport_ops tops = {
  .receive = ib_verbs_receive,
  .submit = ib_verbs_submit,
  .connect = ib_verbs_connect,
  .disconnect = ib_verbs_disconnect,
  .listen = ib_verbs_listen,
};

int32_t
init (transport_t *this)
{
  ib_verbs_private_t *priv = calloc (1, sizeof (*priv));
  this->private = priv;
  priv->sock = -1;

  if (ib_verbs_init (this)) {
    gf_log (this->xl->name,
	    GF_LOG_ERROR,
	    "Failed to initialize IB Device");
    return -1;
  }

  return 0;
}

void  
fini (struct transport *this)
{
  /* TODO: verify this function does graceful finish */
  ib_verbs_private_t *priv = this->private;
  free (priv);
  this->private = NULL;

  pthread_mutex_destroy (&priv->recv_mutex);
  pthread_mutex_destroy (&priv->write_mutex);
  pthread_mutex_destroy (&priv->read_mutex);
  pthread_cond_destroy (&priv->recv_cond);

  gf_log (this->xl->name,
	  GF_LOG_CRITICAL,
	  "called fini on transport: %p",
	  this);
  return;
}
