/*
  (C) 2006,2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#include "dict.h"
#include "glusterfs.h"
#include "transport.h"
#include "protocol.h"
#include "logging.h"
#include "xlator.h"

#include "ib-verbs.h"

static uint16_t 
ib_verbs_get_local_lid (struct ibv_context *context, int32_t port)
{
  struct ibv_port_attr attr;

  if (ibv_query_port (context, port, &attr))
    return 0;

  return attr.lid;
}

/* This function is used to write the buffer data into the remote buffer */
int32_t 
ib_verbs_post_send (transport_t *trans,
		    ib_verbs_peer_t *peer,
		    ib_verbs_post_t *post,
		    int32_t len)
{
  if (!post) {
    /* this case should not happen */
    gf_log ("ib-verbs-post-send", 
	    GF_LOG_ERROR, 
	    "Send buffer empty.. Critical error");
    return -1;
  }

  struct ibv_sge list = {
    .addr   = (uintptr_t) post->buf,
    .length = len,
    .lkey   = post->mr->lkey
  };

  ib_verbs_comp_t *comp = calloc (1, sizeof (ib_verbs_comp_t));
  comp->type = 0; // Send Q
  comp->peer = peer;
  comp->trans = trans; /* TODO: tranposrt_ref */
  comp->post = post;

  struct ibv_send_wr wr = {
    .wr_id      = (uint64_t)(long)comp,
    .sg_list    = &list,
    .num_sge    = 1,
    .opcode     = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED,
  };
  struct ibv_send_wr *bad_wr;

  return ibv_post_send (peer->qp, &wr, &bad_wr);
}

/* Function to post receive request in the QP */
int32_t 
ib_verbs_post_recv (transport_t *trans, 
		    ib_verbs_peer_t *peer, 
		    ib_verbs_post_t *post)
{
  if (!post || !post->buf) {
    /* This case should not happen too */
    gf_log ("tranposrt/ib-verbs", 
	    GF_LOG_CRITICAL, 
	    "Recv list empty");
    return -1;
  }

  struct ibv_sge list = {
    .addr   = (uintptr_t) post->buf,
    .length = post->buf_size,
    .lkey   = post->mr->lkey
  };
  ib_verbs_comp_t *comp = calloc (1, sizeof (ib_verbs_comp_t));
  comp->type = 1; // Recv Q
  comp->peer = peer;
  comp->post = post;
  comp->trans = trans; /* TODO: transport_ref */

  struct ibv_recv_wr wr = {
    .wr_id      = (uint64_t)(long)comp,
    .sg_list    = &list,
    .num_sge    = 1,
  };
  struct ibv_recv_wr *bad_wr;

  return ibv_post_recv (peer->qp, &wr, &bad_wr);
}

static ib_verbs_ctx_t *
ib_verbs_get_context (glusterfs_ctx_t *ctx,
		      struct ibv_device *ib_dev)
{
  const char *device_name = ibv_get_device_name (ib_dev);
  ib_verbs_ctx_t *trav;

  trav = ctx->ib;
  while (trav) {
    if (!strcmp (trav->device_name, device_name))
      break;
    trav = trav->next;
  }
  if (!trav) {
    struct ibv_context *ibctx = ibv_open_device (ib_dev);
    int32_t i;
    if (ibctx) {
      trav = calloc (1, sizeof (*trav));
      trav->ctx = ibctx;
      trav->device_name = strdup (device_name);

      trav->next = ctx->ib;

      for (i = 0; i < 2; i++) { 
	trav->send_chan[i] = ibv_create_comp_channel (trav->ctx);
	if (!trav->send_chan[i]) {
	  gf_log ("transport/ib-verbs", 
		  GF_LOG_CRITICAL, 
		  "could not create send completion channel[%d]", i);
	  /* TODO: cleanup current mess */
	  return NULL;
	}
    
	trav->recv_chan[i] = ibv_create_comp_channel (trav->ctx);
	if (!trav->recv_chan[i]) {
	  gf_log ("transport/ib-verbs", 
		  GF_LOG_CRITICAL, 
		  "could not create recv completion channel[%d]", i);
	  /* TODO: cleanup current mess */
	  return NULL;
	}

	trav->send_cq[i] = ibv_create_cq (trav->ctx, 
					  5000, 
					  NULL, 
					  trav->send_chan[i], 
					  0);
	if (!trav->send_cq[i]) {
	  gf_log ("transport/ib-verbs", 
		  GF_LOG_CRITICAL, 
		  "could not create send_cq[%d]", i);
	  return NULL;
	}
    
	trav->recv_cq[i] = ibv_create_cq (trav->ctx, 
					  5000, 
					  NULL, 
					  trav->recv_chan[i], 
					  0);
	if (!trav->recv_cq[i]) {
	  gf_log ("transport/ib-verbs", 
		  GF_LOG_CRITICAL, 
		  "Couldn't create recv CQ[%d]", i);
	  return NULL;
	}

	if (ibv_req_notify_cq (trav->send_cq[i], 0)) {
	  gf_log ("transport/ib-verbs",
		  GF_LOG_ERROR, 
		  "could not request send_cq notification");
	  return NULL;
	}
    
	if (ibv_req_notify_cq(trav->recv_cq[i], 0)) {
	  gf_log ("transport/ib-verbs", 
		  GF_LOG_ERROR, 
		  "could not request recv_cq notification");
	  return NULL;
	}
      }

      trav->send_trans = calloc (1, sizeof (transport_t));
      trav->send_trans->notify = ib_verbs_send_cq_notify;
      trav->send_trans->private = trav;

      trav->recv_trans = calloc (1, sizeof (transport_t));
      trav->recv_trans->notify = ib_verbs_recv_cq_notify;
      trav->recv_trans->private = trav;

      poll_register (ctx, trav->send_chan[0]->fd, trav->send_trans);
      poll_register (ctx, trav->recv_chan[0]->fd, trav->recv_trans);

      ctx->ib = trav;
    }
  }
  return trav;
}

static void
ib_verbs_options_init (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  ib_verbs_options_t *options = &priv->options;
  int32_t mtu;
  data_t *temp;

  /* TODO: validate arguments from options below */

  options->send_size = 131072;
  options->recv_size = 131072;
  options->send_count = 64;
  options->recv_count = 64;

  temp = dict_get (this->xl->options,
		   "ib-verbs-work-request-send-count");
  if (temp)
    options->send_count = data_to_int (temp);

  temp = dict_get (this->xl->options,
		   "ib-verbs-work-request-recv-count");
  if (temp)
    options->recv_count = data_to_int (temp);

  temp = dict_get (this->xl->options,
		   "ib-verbs-work-request-send-size");
  if (temp)
    options->send_size = data_to_int (temp);

  temp = dict_get (this->xl->options,
		   "ib-verbs-work-request-recv-size");
  if (temp)
    options->recv_size = data_to_int (temp);

  options->port = 1;
  temp = dict_get (this->xl->options,
		   "ib-verbs-port");
  if (temp)
    options->port = data_to_int (temp);

  options->mtu = IBV_MTU_2048;
  temp = dict_get (this->xl->options,
		   "ib-verbs-mtu");
  if (temp)
    mtu = data_to_int (temp);
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
    gf_log ("transport/ib-verbs",
	    GF_LOG_ERROR,
	    "%s: unrecognized MTU value '%s', defaulting to '2048'",
	    this->xl->name,
	    data_to_str (temp));
    break;
  }
}

int32_t 
ib_verbs_init (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  ib_verbs_options_t *options = &priv->options;
  struct ibv_device **dev_list;
  struct ibv_device *ib_dev;
  char *ib_devname = NULL;

  /* Used to initialize the random generator, needed for PSN */
  srand48 (getpid() * time(NULL));

  dev_list = ibv_get_device_list (NULL);
  if (!dev_list) {
    gf_log ("transport/ib-verbs", 
	    GF_LOG_CRITICAL, 
	    "No IB devices found");
    return -1;
  }

  ib_verbs_options_init (this);

  ib_devname = options->device_name;
  if (!ib_devname) {
    ib_dev = *dev_list;
  } else {
    for (; (ib_dev = *dev_list); ++dev_list) {
      if (!strcmp (ibv_get_device_name (ib_dev), ib_devname))
	break;
    }
  }

  ibv_free_device_list (dev_list);

  if (!ib_dev) {
    gf_log ("transport/ib-verbs", 
	    GF_LOG_CRITICAL, 
	    "No IB devices found");
    return -1;
  }

  priv->ctx = ib_verbs_get_context (this->xl->ctx, ib_dev);

  if (!priv->ctx) {
    gf_log ("transport/ib-verbs", 
	    GF_LOG_CRITICAL, 
	    "%s: could not get context for %s",
	    this->xl->name,
	    ibv_get_device_name (ib_dev));
    return -1;
  }

  priv->pd = ibv_alloc_pd (priv->ctx->ctx);

  if (!priv->pd) {
    gf_log ("transport/ib-verbs", 
	    GF_LOG_CRITICAL, 
	    "%s: could not allocate protection domain",
	    this->xl->name);
    return -1;
  }

  return 0; 
}

/* CQ notify */
int32_t 
ib_verbs_recv_cq_notify (xlator_t *xl,
			 transport_t *ctx_trans,
			 int32_t event)
{
  ib_verbs_ctx_t *ctx = ctx_trans->private;
  
  struct ibv_wc wc;
  struct ibv_cq *event_cq;
  void *event_ctx; 
  ib_verbs_private_t *priv;
  char aux_buf = 0;
  
  /* Get the event from Channel FD */
  if (ibv_get_cq_event (ctx->recv_chan[0], &event_cq, &event_ctx)) {
    gf_log ("transport/ib-verbs", 
	    GF_LOG_CRITICAL,
	    "ibv_get_cq_event failed");
  }

  /* Acknowledge the CQ event. ==NOT SO COMPULSARY== */
  ibv_ack_cq_events (ctx->recv_cq[0], 1);

  /* Request for CQ event */
  if (ibv_req_notify_cq (ctx->recv_cq[0], 0)) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_CRITICAL, 
	    "ibv_req_notify_cq failed");
  }

  while (ibv_poll_cq (ctx->recv_cq[0], 1, &wc)) {
    /* Get the actual priv pointer from wc */
    if (wc.status != IBV_WC_SUCCESS) {
      gf_log ("ibverbs_recv_notify", 
	      GF_LOG_CRITICAL, 
	      "error condition (%d)", wc.status);
      return -1;
    }
    ib_verbs_comp_t *comp = (ib_verbs_comp_t *)(long)wc.wr_id;
    ib_verbs_peer_t *peer = comp->peer;
    ib_verbs_post_t *post = comp->post;
    transport_t *my_trans = comp->trans;
    
    if (comp->type != 1) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "non-recv type (%d) work-request completed on recv_cq[0]",
	      comp->type);
      return -1;
    }
    free (comp);

    priv = my_trans->private;
      
    /* Read the buffer */
    if (strncmp (post->buf, "NeedDataMR", 10) == 0) {
      /* Check the existing misc buf list size, if smaller, allocate new and use. */
      int32_t buflen = 0;
      ib_verbs_post_t *big_post; 
      sscanf (post->buf, "NeedDataMR:%d\n", &buflen);

      aux_buf = 1;
      big_post = calloc (1, sizeof (ib_verbs_post_t));
	
      big_post->buf = valloc (buflen + 2048);
      big_post->buf_size = buflen + 2048;
      big_post->mr = ibv_reg_mr (priv->pd, 
				 big_post->buf, 
				 buflen + 2048, 
				 IBV_ACCESS_LOCAL_WRITE);
      if (!big_post->mr) {
	gf_log ("transport/ib-verbs", 
		GF_LOG_CRITICAL, 
		"could not allocate QP[1]->MR");
	free (big_post->buf);
	free (big_post);
	/* TODO: Actually it should be a return -1 thing */
	continue;
      }

      if (ib_verbs_post_recv (my_trans, &priv->peers[0], post)) {
	gf_log ("transport/ib-verbs",
		GF_LOG_CRITICAL, 
		"Failed to post recv request to QP[0]");
      }
	
      if (ib_verbs_post_recv (my_trans, &priv->peers[1], big_post)) {
	gf_log ("transport/ib-verbs", 
		GF_LOG_CRITICAL, 
		"Failed to post recv request to QP[1]");
      }
	
      /* Make sure we get the next buffer is MISC buffer */
      if (ibv_get_cq_event (priv->ctx->recv_chan[1], 
			    &event_cq, &event_ctx)) {
	gf_log ("transport/ib-verbs", 
		GF_LOG_CRITICAL, 
		"ibv_get_cq_event on recv_chan[1] failed");
      }
	
      /* Acknowledge the CQ event. ==NOT SO COMPULSARY== */
      ibv_ack_cq_events (priv->ctx->recv_cq[1], 1);
	
      /* Request for CQ event */
      if (ibv_req_notify_cq (priv->ctx->recv_cq[1], 0)) {
	gf_log ("transport/ib-verbs", GF_LOG_CRITICAL,
		"ibv_req_notify_cq on recv_cq[1] failed");
      }
	
      if (ibv_poll_cq (priv->ctx->recv_cq[1], 1, &wc)) {
	if (wc.status != IBV_WC_SUCCESS) {
	  gf_log ("transport/ib-verbs",
		  GF_LOG_CRITICAL,
		  "error condition (recv_notify - part 2)");
	  return -1;
	}
	  
	comp = (ib_verbs_comp_t *)(long)wc.wr_id;
	peer = comp->peer;
	post = comp->post;
	my_trans = comp->trans;
	  
	priv = my_trans->private;

	free (comp);
      } else {
	/* Error :O */
	gf_log ("transport/ib-verbs", 
		GF_LOG_ERROR, 
		"ibv_poll_cq returned 0");
      }
    }
      /* Used by receive */
    priv->data_ptr = post->buf;
    priv->data_offset = 0;
    //    priv->ibv_comp = ib_cq_comp;

    /* Call the protocol's notify */
    if (priv->notify (my_trans->xl, my_trans, event)) {
      /* TODO: handle by disconnecting peer */
    }
      
    if (!aux_buf) {
      if (ib_verbs_post_recv (my_trans, peer, post)) {
	gf_log ("transport/ib-verbs", 
		GF_LOG_CRITICAL, 
		"Failed to post recv request to QP");
      }
    } else {
      ibv_dereg_mr (post->mr);
      free (post->buf);
      free (post);
    }
  }

  return 0;
}

int32_t 
ib_verbs_send_cq_notify (xlator_t *xl,
			 transport_t *ctx_trans,
			 int32_t event)
{
  ib_verbs_ctx_t *ctx = (ib_verbs_ctx_t *)ctx_trans->private;
  
  struct ibv_wc wc;
  struct ibv_cq *event_cq;
  void *event_ctx; 
  
  /* Get the event from Channel FD */
  if (ibv_get_cq_event (ctx->send_chan[0], &event_cq, &event_ctx)) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_CRITICAL, "ibv_get_cq_event failed");
  }

  /* Acknowledge the CQ event. ==NOT SO COMPULSARY== */
  ibv_ack_cq_events (ctx->send_cq[0], 1); 

  /* Request for CQ event */
  if (ibv_req_notify_cq (ctx->send_cq[0], 0)) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_CRITICAL, "ibv_req_notify_cq failed");
  }

  while (ibv_poll_cq (ctx->send_cq[0], 1, &wc)) {
    /* Get the actual priv pointer from wc */
    if (wc.status != IBV_WC_SUCCESS) {
      gf_log ("send_notify 0", 
	      GF_LOG_CRITICAL, 
	      "poll_cq returned error (%d)", wc.status);
      return -1;
    }

    ib_verbs_comp_t *comp = (ib_verbs_comp_t *)(long)wc.wr_id;
    ib_verbs_peer_t *peer = comp->peer;
    ib_verbs_post_t *post = comp->post;
    transport_t *my_trans = comp->trans; /* TODO: transport_unref */
    ib_verbs_private_t *priv = my_trans->private;
    
    if (comp->type != 0) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_ERROR, 
	      "unknow type of send buffer");
      return -1;
    }
      /* send complete */
    pthread_mutex_lock (&priv->write_mutex);
    post->next = peer->send_list;
    peer->send_list = post;
    pthread_mutex_unlock (&priv->write_mutex);

    free (comp);
  } 

  return 0;
}

/* For the misc buffers */

int32_t 
ib_verbs_send_cq_notify1 (xlator_t *xl,
			  transport_t *ctx_trans,
			  int32_t event)
{
  ib_verbs_ctx_t *ctx = (ib_verbs_ctx_t *)ctx_trans->private;
  
  struct ibv_wc wc;
  struct ibv_cq *event_cq;
  void *event_ctx; 
  
  /* Get the event from Channel FD */
  if (ibv_get_cq_event (ctx->send_chan[1], &event_cq, &event_ctx)) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_CRITICAL, "ibv_get_cq_event failed");
  }

  /* Acknowledge the CQ event. ==NOT SO COMPULSARY== */
  ibv_ack_cq_events (ctx->send_cq[1], 1); 

  /* Request for CQ event */
  if (ibv_req_notify_cq (ctx->send_cq[1], 0)) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_CRITICAL, "ibv_req_notify_cq failed");
  }

  while (ibv_poll_cq (ctx->send_cq[1], 1, &wc)) {
    /* Get the actual priv pointer from wc */
    if (wc.status != IBV_WC_SUCCESS) {
      gf_log ("send_notify 0", 
	      GF_LOG_CRITICAL, 
	      "poll_cq returned error (%d)", wc.status);
      return -1;
    }

    ib_verbs_comp_t *comp = (ib_verbs_comp_t *)(long)wc.wr_id;
    ib_verbs_peer_t *peer = comp->peer;
    ib_verbs_post_t *post = comp->post;
    transport_t *my_trans = comp->trans; /* TODO: transport_unref */
    ib_verbs_private_t *priv = my_trans->private;
    
    if (comp->type != 0) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_ERROR, 
	      "unknow type of send buffer");
      return -1;
    }
      /* send complete */
    pthread_mutex_lock (&priv->write_mutex);
    post->next = peer->send_list;
    peer->send_list = post;
    pthread_mutex_unlock (&priv->write_mutex);

    free (comp);
  } 

  return 0;
}


/* Connect both QPs with the remote QP */
int32_t 
ib_verbs_ibv_connect (ib_verbs_private_t *priv)
{
  int i = 0;
  ib_verbs_options_t *options = &priv->options;
  for (i = 0; i < NUM_QP_PER_CONN; i++) {
    struct ibv_qp_attr attr = {
      .qp_state               = IBV_QPS_RTR,
      .path_mtu               = options->mtu,
      .dest_qp_num            = priv->peers[i].remote_qpn,
      .rq_psn                 = priv->peers[i].remote_psn,
      .max_dest_rd_atomic     = 1,
      .min_rnr_timer          = 12,
      .ah_attr                = {
	.is_global      = 0,
	.dlid           = priv->peers[i].remote_lid,
	.sl             = 0,
	.src_path_bits  = 0,
	.port_num       = options->port
      }
    };
    if (ibv_modify_qp(priv->peers[i].qp, &attr,
		      IBV_QP_STATE              |
		      IBV_QP_AV                 |
		      IBV_QP_PATH_MTU           |
		      IBV_QP_DEST_QPN           |
		      IBV_QP_RQ_PSN             |
		      IBV_QP_MAX_DEST_RD_ATOMIC |
		      IBV_QP_MIN_RNR_TIMER)) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_CRITICAL, 
	      "Failed to modify QP[%d] to RTR\n", i);
      return -1;
    }
    
    attr.qp_state       = IBV_QPS_RTS;
    attr.timeout        = 14;
    attr.retry_cnt      = 7;
    attr.rnr_retry      = 7;
    attr.sq_psn         = priv->peers[i].local_psn;
    attr.max_rd_atomic  = 1;
    if (ibv_modify_qp (priv->peers[i].qp, &attr,
		       IBV_QP_STATE              |
		       IBV_QP_TIMEOUT            |
		       IBV_QP_RETRY_CNT          |
		       IBV_QP_RNR_RETRY          |
		       IBV_QP_SQ_PSN             |
		       IBV_QP_MAX_QP_RD_ATOMIC)) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_CRITICAL, 
	      "Failed to modify QP[%d] to RTS\n", i);
      return -1;
    }
  }
  return 0;
}

int32_t 
ib_verbs_create_qp (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  ib_verbs_options_t *options = &priv->options;
  int32_t port = options->port;
  int i = 0;

  for (i = 0;i < NUM_QP_PER_CONN; i++) {
    {
      struct ibv_qp_init_attr attr = {
	.send_cq = priv->ctx->send_cq[i],
	.recv_cq = priv->ctx->recv_cq[i],
	.cap     = {
	  .max_send_wr  = priv->peers[i].send_count,
	  .max_recv_wr  = priv->peers[i].recv_count,
	  .max_send_sge = 1,
	  .max_recv_sge = 1
	},
	.qp_type = IBV_QPT_RC
      };
      
      priv->peers[i].qp = ibv_create_qp (priv->pd, &attr);
      if (!priv->peers[i].qp)  {
	gf_log ("transport/ib-verbs", 
		GF_LOG_CRITICAL, 
		"could create QP[%d]\n", i);
	/* TODO: cleanup current mess */
	return -1;
      }
    }
    
    {
      struct ibv_qp_attr attr;
      
      attr.qp_state        = IBV_QPS_INIT;
      attr.pkey_index      = 0;
      attr.port_num        = options->port;
      attr.qp_access_flags = 0;
    
      if (ibv_modify_qp (priv->peers[i].qp, &attr,
			 IBV_QP_STATE              |
			 IBV_QP_PKEY_INDEX         |
			 IBV_QP_PORT               |
			 IBV_QP_ACCESS_FLAGS)) {
	gf_log ("transport/ib-verbs", 
		GF_LOG_CRITICAL, 
		"Failed to modify QP[%d] to INIT state", i);
	return -1;
      }
    }

    priv->peers[i].local_lid = ib_verbs_get_local_lid (priv->ctx->ctx, port);
    priv->peers[i].local_qpn = priv->peers[i].qp->qp_num;
    priv->peers[i].local_psn = lrand48() & 0xffffff;
    
    if (!priv->peers[i].local_lid) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_CRITICAL,
	      "could not get Local LID");
      return -1;
    }
  }
  return 0;
}

int32_t
ib_verbs_conn_setup (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;
  ib_verbs_options_t *options = &priv->options;
  char buf[256] = {0,};
  int32_t recv_buf_size[2], send_buf_size[2];
  int32_t ret;

  {
    /* Get the ibv options from xl->options */
    priv->peers[0].send_count = options->send_count;
    priv->peers[0].recv_count = options->recv_count;
    priv->peers[0].send_size = options->send_size;
    priv->peers[0].recv_size = options->recv_size;
    priv->peers[1].send_count = 2;
    priv->peers[1].recv_count = 2;
    
    sprintf (buf,
	     "QP1:RECV_BLKSIZE=%08x:SEND_BLKSIZE=%08x\n"
	     "QP2:RECV_BLKSIZE=%08x:SEND_BLKSIZE=%08x\n",
	     priv->peers[0].recv_size,
	     priv->peers[0].send_size,
	     priv->peers[1].recv_size,
	     priv->peers[1].send_size);

    /* TODO: check return values of write and read */
    if (gf_full_write (priv->sock, buf, sizeof buf) != 0) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "%s: could not send IB handshake-1 data",
	      this->xl->name);
      return -1;
    }

    buf[0] = '\0';
    if (gf_full_read (priv->sock, buf, sizeof buf) != 0) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "%s: could not recv IB handshake-1 data",
	      this->xl->name);
      return -1;
    }

    if (strncmp (buf, "QP1:", 4)) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_CRITICAL,
	      "%s: remote-host's transport type is different",
	      this->xl->name);
      return -1;
    }
    ret = sscanf (buf,
		  "QP1:RECV_BLKSIZE=%08x:SEND_BLKSIZE=%08x\n"
		  "QP2:RECV_BLKSIZE=%08x:SEND_BLKSIZE=%08x\n",
		  &send_buf_size[0],
		  &recv_buf_size[0],
		  &send_buf_size[1],
		  &recv_buf_size[1]);

    if (ret != 4) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "%s: %d conversions in handshake data rather than 10",
	      this->xl->name,
	      ret);
      return -1;
    }

    if (recv_buf_size[0] < priv->peers[0].recv_size)
      priv->peers[0].recv_size = recv_buf_size[0];
    if (recv_buf_size[1] < priv->peers[1].recv_size)
      priv->peers[1].recv_size = recv_buf_size[1];
    if (send_buf_size[0] < priv->peers[0].send_size)
      priv->peers[0].send_size = send_buf_size[0];
    if (send_buf_size[1] < priv->peers[1].send_size)
      priv->peers[1].send_size = send_buf_size[1];
  }

  if (ib_verbs_create_qp (this) < 0) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_ERROR,
	    "%s: could not create QP",
	    this->xl->name);
    return -1;
  }

  {
    sprintf (buf,
	     "QP1:LID=%04x:QPN=%06x:PSN=%06x\n"
	     "QP2:LID=%04x:QPN=%06x:PSN=%06x\n",
	     priv->peers[0].local_lid,
	     priv->peers[0].local_qpn,
	     priv->peers[0].local_psn,
	     priv->peers[1].local_lid,
	     priv->peers[1].local_qpn,
	     priv->peers[1].local_psn);

    /* TODO: check return values of write and read */
    if (gf_full_write (priv->sock, buf, sizeof buf) != 0) {
      gf_log ("transport/ib-verbs",
	      GF_LOG_ERROR,
	      "%s: could not send IB handshake-2 data",
	      this->xl->name);
      return -1;
    }

    buf[0] = '\0';
    if (gf_full_read (priv->sock, buf, sizeof buf) != 0) {
      gf_log ("ib-verbs/client",
	      GF_LOG_ERROR,
	      "%s: could not recv IB handshake-2 data",
	      this->xl->name);
      return -1;
    }

    if (strncmp (buf, "QP1:", 4)) {
      gf_log ("ib-verbs/client",
	      GF_LOG_CRITICAL,
	      "%s: remote-host's transport type is different",
	      this->xl->name);
      return -1;
    }
    ret = sscanf (buf,
		  "QP1:LID=%04x:QPN=%06x:PSN=%06x\n"
		  "QP2:LID=%04x:QPN=%06x:PSN=%06x\n",
		  &priv->peers[0].remote_lid,
		  &priv->peers[0].remote_qpn,
		  &priv->peers[0].remote_psn,
		  &priv->peers[1].remote_lid,
		  &priv->peers[1].remote_qpn,
		  &priv->peers[1].remote_psn);

    if (ret != 6) {
      gf_log ("ib-verbs/client",
	      GF_LOG_ERROR,
	      "%s: %d conversions in handshake data rather than 10",
	      this->xl->name,
	      ret);
      return -1;
    }
  }
  
  // allocate buffers and MRs
  if (ib_verbs_create_buf_list (priv) < 0) {
    gf_log ("ib-verbs/client", 
	    GF_LOG_ERROR, 
	    "%s: could not allocate buffer for QPs",
	    this->xl->name);
    /* destroy qp */
    return -1;
  }

  /* Keep the read requests in the queue */
  int32_t i;
  ib_verbs_post_t *mr;
  for (i = 0; i < priv->peers[0].recv_count; i++) {
    mr = priv->peers[0].recv_list;
    priv->peers[0].recv_list = mr->next;
    /* TODO: inspect return value below */
    ib_verbs_post_recv (this, &priv->peers[0], mr);
    /* if failed, 
       free buf list
       destroy qp
    */
  }
  
  if (ib_verbs_ibv_connect (priv)) {
    gf_log ("ib-verbs/client", 
	    GF_LOG_ERROR, 
	    "%s: failed to connect with remote QP",
	    this->xl->name);
    /* TODO:
       free buf list
       destroy qp
       undo post recv's
    */
    return -1;
  }

  return 0;
}

int32_t 
ib_verbs_recieve (struct transport *this,
		  char *buf, 
		  int32_t len)
{
  GF_ERROR_IF_NULL (this);
  
  ib_verbs_private_t *priv = this->private;

  GF_ERROR_IF_NULL (priv);
  GF_ERROR_IF_NULL (buf);
  GF_ERROR_IF (len < 0);
  
  if (!priv->connected) {
    /* Should only be used by client while do_handshake as its synchronous call as of now */
    struct ibv_wc wc;
    struct ibv_cq *ev_cq;
    void *ev_ctx;

    /* Get the event from CQ */
    ibv_get_cq_event (priv->ctx->recv_chan[0], &ev_cq, &ev_ctx);
    ibv_poll_cq (priv->ctx->recv_cq[0], 1, &wc);
    if (wc.status != IBV_WC_SUCCESS) {
      gf_log ("ibverbs-recieve", 
	      GF_LOG_CRITICAL, 
	      "poll_cq error status (%d)", wc.status);
    }

    ibv_req_notify_cq (priv->ctx->recv_cq[0], 0);

    /* Set the proper pointer for buffers */
    ib_verbs_comp_t *ibcqcomp = (ib_verbs_comp_t *)(long)wc.wr_id;
    priv->data_ptr = ibcqcomp->post->buf;
    priv->data_offset = 0;
    priv->connected = 1;

    ibcqcomp->post->next = ibcqcomp->peer->recv_list;
    ibcqcomp->peer->recv_list = ibcqcomp->post;

    free (ibcqcomp);
    //ib_verbs_post_recv ();
  }

  /* TODO: do boundry checks for priv->data_ptr/offset */

  /* Copy the data from the QP buffer to the requested buffer */
  memcpy (buf, priv->data_ptr + priv->data_offset, len);
  priv->data_offset += len;

  return 0;
}

int32_t 
ib_verbs_create_buf_list (ib_verbs_private_t *priv) 
{
  int i, count;

  for (i = 0; i<NUM_QP_PER_CONN; i++) {
    priv->peers[i].qp_index = i;
    /* Send list */
    ib_verbs_post_t *temp = NULL, *last = NULL, *trav = NULL;
    for (count = 0; count < priv->peers[i].send_count; count++) {
      if (priv->peers[i].send_size) {
	temp = calloc (1, sizeof (ib_verbs_post_t));
	if (!last)
	  last = temp;
	if (trav)
	  trav->prev = temp;
	temp->next = trav;
	temp->buf = valloc (priv->peers[i].send_size + 2048);
	temp->buf_size = priv->peers[i].send_size + 2048;
	memset (temp->buf, 0, temp->buf_size);
	/* Register MR */
	temp->mr = ibv_reg_mr (priv->pd,
			       temp->buf,
			       temp->buf_size,
			       IBV_ACCESS_LOCAL_WRITE);
	if (!temp->mr) {
	  gf_log ("ib-verbs", GF_LOG_ERROR, "Couldn't allocate MR");
	  return -1;
	}
	trav = temp;
	priv->peers[i].send_list = temp;
      } else {
	priv->peers[i].send_list = calloc (1, sizeof (ib_verbs_post_t));
      }
      if (temp)
	temp->prev = last;
    }
    temp = NULL; trav = NULL;
    /* Create the recv list */
    for (count = 0; count <= priv->peers[i].recv_count; count++) {
      if (priv->peers[i].recv_size) {
	temp = calloc (1, sizeof (ib_verbs_post_t));
	temp->next = trav;
	if (trav)
	  trav->prev = temp;
	temp->buf = valloc (priv->peers[i].recv_size + 2048);
	temp->buf_size = priv->peers[i].recv_size + 2048;
	memset (temp->buf, 0, temp->buf_size);
	/* Register MR */
	temp->mr = ibv_reg_mr (priv->pd, 
			       temp->buf, 
			       temp->buf_size, 
			       IBV_ACCESS_LOCAL_WRITE);
	if (!temp->mr) {
	  gf_log ("ib-verbs", GF_LOG_ERROR, "Couldn't allocate MR");
	  return -1;
	}
	trav = temp;
	priv->peers[i].recv_list = temp;
      } else {
	priv->peers[i].recv_list = calloc (1, sizeof (ib_verbs_post_t));
      }
    }
  }

  return 0;
}

int32_t
ib_verbs_bail (transport_t *this)
{

  return 0;
}
