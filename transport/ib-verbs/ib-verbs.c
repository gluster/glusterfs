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

#define IBVERBS_DEV_PORT 1

uint16_t 
ib_verbs_get_local_lid (struct ibv_context *context, int32_t port)
{
  struct ibv_port_attr attr;

  if (ibv_query_port(context, port, &attr))
    return 0;

  return attr.lid;
}

/* This function is used to write the buffer data into the remote buffer */
int32_t 
ib_verbs_post_send (transport_t *trans, 
		    ib_qp_struct_t *qp, 
		    ib_mr_struct_t *mr, 
		    int32_t len)
{
  if (!mr) {
    /* this case should not happen */
    gf_log ("ib-verbs-post-send", 
	    GF_LOG_ERROR, 
	    "Send buffer empty.. Critical error");
    return -1;
  }

  struct ibv_sge list = {
    .addr   = (uintptr_t) mr->buf,
    .length = len,
    .lkey   = mr->mr->lkey
  };

  ib_cq_comp_t *ibcq_comp = calloc (1, sizeof (ib_cq_comp_t));
  ibcq_comp->type = 0; // Send Q
  ibcq_comp->qp = qp;
  ibcq_comp->trans = trans;
  ibcq_comp->mr = mr;

  struct ibv_send_wr wr = {
    .wr_id      = (uint64_t)(long)ibcq_comp,
    .sg_list    = &list,
    .num_sge    = 1,
    .opcode     = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED,
  };
  struct ibv_send_wr *bad_wr;

  return ibv_post_send(qp->qp, &wr, &bad_wr);
}

/* Function to post receive request in the QP */
int32_t 
ib_verbs_post_recv (transport_t *trans, 
		    ib_qp_struct_t *qp, 
		    ib_mr_struct_t *mr)
{
  if (!mr && !mr->buf) {
    /* This case should not happen too */
    gf_log ("ib-verbs-post-recv", 
	    GF_LOG_CRITICAL, 
	    "Recv list empty");
    return -1;
  }

  struct ibv_sge list = {
    .addr   = (uintptr_t) mr->buf,
    .length = mr->buf_size,
    .lkey   = mr->mr->lkey
  };
  ib_cq_comp_t *ibcq_comp = calloc (1, sizeof (ib_cq_comp_t));
  ibcq_comp->type = 1; // Recv Q
  ibcq_comp->qp = qp;
  ibcq_comp->mr = mr;
  ibcq_comp->trans = trans;

  struct ibv_recv_wr wr = {
    .wr_id      = (uint64_t)(long)ibcq_comp,
    .sg_list    = &list,
    .num_sge    = 1,
  };
  struct ibv_recv_wr *bad_wr;

  return ibv_post_recv(qp->qp, &wr, &bad_wr);
}

int32_t 
ib_verbs_ibv_init (ib_verbs_dev_t *ibv)
{
  struct ibv_device **dev_list;
  struct ibv_device *ib_dev;
  char *ib_devname = NULL;
  int32_t i;

  /* Used to initialize the random generator, needed for PSN */
  srand48(getpid() * time(NULL));

  dev_list = ibv_get_device_list(NULL);
  if (!dev_list) {
    gf_log ("transport/ib-verbs", 
	    GF_LOG_CRITICAL, 
	    "No IB devices found\n");
    return -1;
  }

  /* TODO: get ib_devname from options. */
  if (!ib_devname) {
    ib_dev = *dev_list;
    if (!ib_dev) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_CRITICAL, 
	      "No IB devices found\n");
      return -1;
    }
  } else {
    for (; (ib_dev = *dev_list); ++dev_list)
      if (!strcmp(ibv_get_device_name(ib_dev), ib_devname))
	break;
    if (!ib_dev) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_CRITICAL, 
	      "IB device %s not found\n", ib_devname);
      return -1;
    }
  }

  ibv->context = ibv_open_device (ib_dev);
  if (!ibv->context) {
    gf_log ("transport/ib-verbs", 
	    GF_LOG_CRITICAL, 
	    "Couldn't get context for %s\n",
	    ibv_get_device_name(ib_dev));
    return -1;
  }

  ibv->pd = ibv_alloc_pd(ibv->context);
  if (!ibv->pd) {
    gf_log ("transport/ib-verbs", 
	    GF_LOG_CRITICAL, 
	    "Couldn't allocate PD\n");
    return -1;
  }

  for (i = 0; i < 2; i++) { 
    ibv->send_channel[i] = ibv_create_comp_channel(ibv->context);
    if (!ibv->send_channel[i]) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_CRITICAL, 
	      "Couldn't create send completion channel[%d]", i);
      return -1;
    }
    
    ibv->recv_channel[i] = ibv_create_comp_channel(ibv->context);
    if (!ibv->recv_channel[i]) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_CRITICAL, 
	      "Couldn't create recv completion channel[%d]", i);
      return -1;
    }
    
    /* TODO: Make sure this constant is enough :| */
    ibv->sendcq[i] = ibv_create_cq(ibv->context, 
				   5000, 
				   NULL, 
				   ibv->send_channel[i], 
				   0);
    if (!ibv->sendcq[i]) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_CRITICAL, 
	      "Couldn't create send CQ[%d]", i);
      return -1;
    }
    
    ibv->recvcq[i] = ibv_create_cq(ibv->context, 
				   5000, 
				   NULL, 
				   ibv->recv_channel[i], 
				   0);
    if (!ibv->recvcq[i]) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_CRITICAL, 
	      "Couldn't create recv CQ[%d]", i);
      return -1;
    }
    
    /* Required to get the CQ notifications */
    if (ibv_req_notify_cq(ibv->sendcq[i], 0)) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_ERROR, 
	      "Couldn't request Send CQ notification");
      return -1;
    }
    
    if (ibv_req_notify_cq(ibv->recvcq[i], 0)) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_ERROR, 
	      "Couldn't request Recv CQ notification");
      return -1;
    }
  }
  gf_log ("transport/ib-verbs", 
	  GF_LOG_DEBUG, 
	  "using the device %s for ib-verbs transport", 
	  ibv_get_device_name(ib_dev));
  ibv->ib_dev = ib_dev;
  
  /* this function is used to free the list got by get_device_list () */
  ibv_free_device_list(dev_list);

  return 0; 
}

/* CQ notify */
int32_t 
ib_verbs_recv_cq_notify (xlator_t *xl,
			 transport_t *trans,
			 int32_t event)
{
  ib_verbs_private_t *priv = (ib_verbs_private_t *)trans->private;
  
  struct ibv_wc wc;
  struct ibv_cq *event_cq;
  void *event_ctx; 
  
  /* Get the event from Channel FD */
  if (ibv_get_cq_event (priv->ibv.recv_channel[0], &event_cq, &event_ctx)) {
    gf_log ("ibv_get_cq_event", GF_LOG_CRITICAL, "recvcq");
  }

  /* Acknowledge the CQ event. ==NOT SO COMPULSARY== */
  ibv_ack_cq_events (priv->ibv.recvcq[0], IBVERBS_DEV_PORT);

  /* Request for CQ event */
  if (ibv_req_notify_cq (priv->ibv.recvcq[0], 0)) {
    gf_log ("ibv_req_notify_cq", GF_LOG_CRITICAL, "recvcq");
  }
  while (ibv_poll_cq (priv->ibv.recvcq[0], 1, &wc)) {
    /* Get the actual priv pointer from wc */
    if (wc.status != IBV_WC_SUCCESS) {
      gf_log ("ibverbs_recv_notify", 
	      GF_LOG_CRITICAL, 
	      "error condition (%d)", wc.status);
      return -1;
    }
    ib_cq_comp_t *ib_cq_comp = (ib_cq_comp_t *)(long)wc.wr_id;
    ib_qp_struct_t *qp = ib_cq_comp->qp;
    ib_mr_struct_t *mr = ib_cq_comp->mr;
    transport_t *my_trans = ib_cq_comp->trans;
    
    if (ib_cq_comp->type == 1) {
      priv = my_trans->private;
      
      /* Read the buffer */
      if (strncmp (mr->buf, "NeedDataMR", 10) == 0) {
	/* Check the existing misc buf list size, if smaller, allocate new and use. */
	int32_t buflen = 0;
	ib_mr_struct_t *temp_mr; 
	sscanf (mr->buf, "NeedDataMR:%d\n", &buflen);
	
	if (!priv->ibv.qp[1].recv_wr_list) {
	  temp_mr = calloc (1, sizeof (ib_mr_struct_t ));
	} else {
	  temp_mr = priv->ibv.qp[1].recv_wr_list;
	}
	
	if (buflen > temp_mr->buf_size) {
	  /* Free the buffer if already exists */
	  if (temp_mr->buf) {
	    ibv_dereg_mr (temp_mr->mr);
	    free (temp_mr->buf);
	  }
	  temp_mr->buf = valloc (buflen + 2048);
	  memset (temp_mr->buf, 0, buflen + 2048);
	  temp_mr->buf_size = buflen + 2048;
	  temp_mr->mr = ibv_reg_mr(priv->ibv.pd, 
				   temp_mr->buf, 
				   buflen + 2048, 
				   IBV_ACCESS_LOCAL_WRITE);
	  if (!temp_mr->mr) {
	    gf_log ("transport/ib-verbs", 
		    GF_LOG_CRITICAL, 
		    "Couldn't allocate QP[1]->MR\n");
	    free (ib_cq_comp);
	    /* TODO: Actually it should be a return -1 thing */
	    continue;
	  }
	}
	
	pthread_mutex_lock (&priv->read_mutex);
	mr->next = qp->recv_wr_list;
	qp->recv_wr_list = mr;
	pthread_mutex_unlock (&priv->read_mutex);
	
	if (ib_verbs_post_recv (my_trans, &priv->ibv.qp[1], temp_mr)) {
	  gf_log ("ib-verbs", 
		  GF_LOG_CRITICAL, 
		  "Failed to recv request to QP[1]");
	}
	
	if (ib_verbs_post_recv (my_trans, &priv->ibv.qp[0], mr)) {
	  gf_log ("ib-verbs", 
		  GF_LOG_CRITICAL, 
		  "Failed to recv request to QP[0]");
	}
	free (ib_cq_comp);
	
	/* Make sure we get the next buffer is MISC buffer */
	if (ibv_get_cq_event (priv->ibv.recv_channel[1], &event_cq, &event_ctx)) {
	  gf_log ("ibv_get_cq_event", 
		  GF_LOG_CRITICAL, 
		  "recvcq");
	}
	
	/* Acknowledge the CQ event. ==NOT SO COMPULSARY== */
	ibv_ack_cq_events (priv->ibv.recvcq[1], IBVERBS_DEV_PORT);
	
	/* Request for CQ event */
	if (ibv_req_notify_cq (priv->ibv.recvcq[1], 0)) {
	  gf_log ("ibv_req_notify_cq", GF_LOG_CRITICAL, "recvcq");
	}
	
	if (ibv_poll_cq (priv->ibv.recvcq[1], 1, &wc)) {
	  if (wc.status != IBV_WC_SUCCESS) {
	    gf_log ("ib-verbs", GF_LOG_CRITICAL, "error condition (recv_notify - part 2)");
	    return -1;
	  }
	  
	  ib_cq_comp = (ib_cq_comp_t *)(long)wc.wr_id;
	  qp = ib_cq_comp->qp;
	  mr = ib_cq_comp->mr;
	  my_trans = ib_cq_comp->trans;
	  
	  priv = my_trans->private;
	} else {
	  /* Error :O */
	  gf_log ("ib-verbs", 
		  GF_LOG_ERROR, 
		  "ibv_poll_cq returned 0");
	}
      }
      
      /* Used by receive */
      priv->data_ptr = mr->buf;
      priv->data_offset = 0;
      priv->ibv_comp = ib_cq_comp;
      
      /* Call the protocol's notify */
      if (priv->notify (my_trans->xl, my_trans, event)) {
	/* Error in data */
      }
      
      /* Put back the recv buffer in the queue */  
      pthread_mutex_lock (&priv->read_mutex);
      mr->next = qp->recv_wr_list;
      qp->recv_wr_list = mr;
      pthread_mutex_unlock (&priv->read_mutex);
      
      if (qp->qp_index == 0) {
	if (ib_verbs_post_recv (my_trans, qp, mr)) {
	  gf_log ("ib-verbs", 
		  GF_LOG_CRITICAL, 
		  "Failed to post recv request to QP");
	}
      }
    }
    free (ib_cq_comp);
  } /* End of while (poll_cq) */
  return 0;
}

int32_t 
ib_verbs_send_cq_notify (xlator_t *xl,
			 transport_t *trans,
			 int32_t event)
{
  ib_verbs_private_t *priv = (ib_verbs_private_t *)trans->private;
  
  struct ibv_wc wc;
  struct ibv_cq *event_cq;
  void *event_ctx; 
  
  /* Get the event from Channel FD */
  if (ibv_get_cq_event (priv->ibv.send_channel[0], &event_cq, &event_ctx)) {
    gf_log ("ibv_get_cq_event", GF_LOG_CRITICAL, "");
  }

  /* Acknowledge the CQ event. ==NOT SO COMPULSARY== */
  ibv_ack_cq_events (priv->ibv.sendcq[0], IBVERBS_DEV_PORT); //1 is the port

  /* Request for CQ event */
  if (ibv_req_notify_cq (priv->ibv.sendcq[0], 0)) {
    gf_log ("ibv_req_notify_cq", GF_LOG_CRITICAL, "sendcq");
  }

  while (ibv_poll_cq (priv->ibv.sendcq[0], 1, &wc)) {
    /* Get the actual priv pointer from wc */
    if (wc.status != IBV_WC_SUCCESS) {
      gf_log ("send_notify 0", 
	      GF_LOG_CRITICAL, 
	      "poll_cq returned error (%d)", wc.status);
      return -1;
    }

    ib_cq_comp_t *ib_cq_comp = (ib_cq_comp_t *)(long)wc.wr_id;
    ib_qp_struct_t *qp = ib_cq_comp->qp;
    ib_mr_struct_t *mr = ib_cq_comp->mr;
    
    if (ib_cq_comp->type == 0) {
      /* send complete */
      pthread_mutex_lock (&priv->write_mutex);
      mr->next = qp->send_wr_list;
      qp->send_wr_list = mr;
      pthread_mutex_unlock (&priv->write_mutex);
    } else {
      /* Error */
      gf_log ("ib-verbs-send-cq-notify", 
	      GF_LOG_ERROR, 
	      "unknow type of send buffer");
    }
    free (ib_cq_comp);
  } 

  return 0;
}

/* For the misc buffers */

int32_t 
ib_verbs_send_cq_notify1 (xlator_t *xl,
			  transport_t *trans,
			  int32_t event)
{
  ib_verbs_private_t *priv = (ib_verbs_private_t *)trans->private;
  
  struct ibv_wc wc;
  struct ibv_cq *event_cq;
  void *event_ctx; 
  
  /* Get the event from Channel FD */
  if (ibv_get_cq_event (priv->ibv.send_channel[1], &event_cq, &event_ctx)) {
    gf_log ("ibv_get_cq_event", GF_LOG_CRITICAL, "");
  }

  /* Acknowledge the CQ event. ==NOT SO COMPULSARY== */
  ibv_ack_cq_events (priv->ibv.sendcq[1], IBVERBS_DEV_PORT); //1 is the port

  /* Request for CQ event */
  if (ibv_req_notify_cq (priv->ibv.sendcq[1], 0)) {
    gf_log ("ibv_req_notify_cq", GF_LOG_CRITICAL, "sendcq");
  }

  while (ibv_poll_cq (priv->ibv.sendcq[1], 1, &wc)) {
    if (wc.status != IBV_WC_SUCCESS) {
      gf_log ("send_notify 1", 
	      GF_LOG_CRITICAL, 
	      "poll_cq returned error (%d)", wc.status);
      return -1;
    }

    /* Get the actual priv pointer from wc */
    ib_cq_comp_t *ib_cq_comp = (ib_cq_comp_t *)(long)wc.wr_id;
    ib_qp_struct_t *qp = ib_cq_comp->qp;
    ib_mr_struct_t *mr = ib_cq_comp->mr;
    
    if (ib_cq_comp->type == 0) {
      /* send complete */
      pthread_mutex_lock (&priv->write_mutex);
      mr->next = qp->send_wr_list;
      qp->send_wr_list = mr;
      pthread_mutex_unlock (&priv->write_mutex);
    } else {
      /* Error */
      gf_log ("ib-verbs-send-cq-notify1", 
	      GF_LOG_ERROR, 
	      "unknown type of send buffer");
    }
    free (ib_cq_comp);
  } 

  return 0;
}


/* Connect both QPs with the remote QP */
int32_t 
ib_verbs_ibv_connect (ib_verbs_private_t *priv, 
		      int32_t port, 
		      enum ibv_mtu mtu)
{
  int i = 0;
  for (i = 0; i < NUM_QP_PER_CONN; i++) {
    struct ibv_qp_attr attr = {
      .qp_state               = IBV_QPS_RTR,
      .path_mtu               = mtu,
      .dest_qp_num            = priv->ibv.qp[i].remote_qpn,
      .rq_psn                 = priv->ibv.qp[i].remote_psn,
      .max_dest_rd_atomic     = 1,
      .min_rnr_timer          = 12,
      .ah_attr                = {
	.is_global      = 0,
	.dlid           = priv->ibv.qp[i].remote_lid,
	.sl             = 0,
	.src_path_bits  = 0,
	.port_num       = port
      }
    };
    if (ibv_modify_qp(priv->ibv.qp[i].qp, &attr,
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
    attr.sq_psn         = priv->ibv.qp[i].local_psn;
    attr.max_rd_atomic  = 1;
    if (ibv_modify_qp(priv->ibv.qp[i].qp, &attr,
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
ib_verbs_create_qp (ib_verbs_private_t *priv)
{
  int i = 0;
  for (i = 0;i < NUM_QP_PER_CONN; i++) {
    {
      struct ibv_qp_init_attr attr = {
	.send_cq = priv->ibv.sendcq[i],
	.recv_cq = priv->ibv.recvcq[i],
	.cap     = {
	  .max_send_wr  = priv->ibv.qp[i].send_wr_count,
	  .max_recv_wr  = priv->ibv.qp[i].recv_wr_count,
	  .max_send_sge = 1,
	  .max_recv_sge = 1
	},
	.qp_type = IBV_QPT_RC
      };
      
      priv->ibv.qp[i].qp = ibv_create_qp(priv->ibv.pd, &attr);
      if (!priv->ibv.qp[i].qp)  {
	gf_log ("transport/ib-verbs", 
		GF_LOG_CRITICAL, 
		"Couldn't create QP[%d]\n", i);
      return -1;
      }
    }
    
    {
      struct ibv_qp_attr attr;
      
      attr.qp_state        = IBV_QPS_INIT;
      attr.pkey_index      = 0;
      attr.port_num        = IBVERBS_DEV_PORT;
      attr.qp_access_flags = 0;
    
      if (ibv_modify_qp(priv->ibv.qp[i].qp, &attr,
			IBV_QP_STATE              |
			IBV_QP_PKEY_INDEX         |
			IBV_QP_PORT               |
			IBV_QP_ACCESS_FLAGS)) {
	gf_log ("transport/ib-verbs", 
		GF_LOG_CRITICAL, 
		"Failed to modify QP[%d] to INIT\n", i);
	return -1;
      }
    }

    priv->ibv.qp[i].local_lid = ib_verbs_get_local_lid (priv->ibv.context, 1); //port
    priv->ibv.qp[i].local_qpn = priv->ibv.qp[i].qp->qp_num;
    priv->ibv.qp[i].local_psn = lrand48() & 0xffffff;
    
    if (!priv->ibv.qp[i].local_lid) {
      gf_log ("transport/ib-verbs", 
	      GF_LOG_CRITICAL, 
	      "Couldn't get Local LID");
      return -1;
    }
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
    ibv_get_cq_event (priv->ibv.recv_channel[0], &ev_cq, &ev_ctx);
    ibv_poll_cq (priv->ibv.recvcq[0], 1, &wc);
    if (wc.status != IBV_WC_SUCCESS) {
      gf_log ("ibverbs-recieve", 
	      GF_LOG_CRITICAL, 
	      "poll_cq error status (%d)", wc.status);
    }

    ibv_req_notify_cq (priv->ibv.recvcq[0], 0);

    /* Set the proper pointer for buffers */
    ib_cq_comp_t *ibcqcomp = (ib_cq_comp_t *)(long)wc.wr_id;
    priv->data_ptr = ibcqcomp->mr->buf;
    priv->data_offset = 0;
    priv->connected = 1;

    ibcqcomp->mr->next = ibcqcomp->qp->recv_wr_list;
    ibcqcomp->qp->recv_wr_list = ibcqcomp->mr;

    free (ibcqcomp);
    //ib_verbs_post_recv ();
  }

  /* Copy the data from the QP buffer to the requested buffer */
  memcpy (buf, priv->data_ptr + priv->data_offset, len);
  priv->data_offset += len;

  return 0;
}

int32_t 
ib_verbs_create_buf_list (ib_verbs_dev_t *ibv) 
{
  int i, count;
  for (i = 0; i<NUM_QP_PER_CONN; i++) {
    ibv->qp[i].qp_index = i;
    /* Send list */
    ib_mr_struct_t *temp = NULL, *last = NULL, *trav = NULL;
    for (count = 0; count < ibv->qp[i].send_wr_count; count++) {
      if (ibv->qp[i].send_wr_size) {
	temp = calloc (1, sizeof (ib_mr_struct_t));
	if (!last)
	  last = temp;
	if (trav)
	  trav->prev = temp;
	temp->next = trav;
	temp->buf = valloc (ibv->qp[i].send_wr_size + 2048);
	temp->buf_size = ibv->qp[i].send_wr_size + 2048;
	memset (temp->buf, 0, temp->buf_size);
	/* Register MR */
	temp->mr = ibv_reg_mr (ibv->pd, temp->buf, temp->buf_size, IBV_ACCESS_LOCAL_WRITE);
	if (!temp->mr) {
	  gf_log ("ib-verbs", GF_LOG_ERROR, "Couldn't allocate MR");
	  return -1;
	}
	trav = temp;
	ibv->qp[i].send_wr_list = temp;
      } else {
	ibv->qp[i].send_wr_list = calloc (1, sizeof (ib_mr_struct_t));
      }
      if (temp)
	temp->prev = last;
    }
    temp = NULL; trav = NULL;
    /* Create the recv list */
    for (count = 0; count <= ibv->qp[i].recv_wr_count; count++) {
      if (ibv->qp[i].recv_wr_size) {
	temp = calloc (1, sizeof (ib_mr_struct_t));
	temp->next = trav;
	if (trav)
	  trav->prev = temp;
	temp->buf = valloc (ibv->qp[i].recv_wr_size + 2048);
	temp->buf_size = ibv->qp[i].recv_wr_size + 2048;
	memset (temp->buf, 0, temp->buf_size);
	/* Register MR */
	temp->mr = ibv_reg_mr (ibv->pd, 
			       temp->buf, 
			       temp->buf_size, 
			       IBV_ACCESS_LOCAL_WRITE);
	if (!temp->mr) {
	  gf_log ("ib-verbs", GF_LOG_ERROR, "Couldn't allocate MR");
	  return -1;
	}
	trav = temp;
	ibv->qp[i].recv_wr_list = temp;
      } else {
	ibv->qp[i].recv_wr_list = calloc (1, sizeof (ib_mr_struct_t));
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
