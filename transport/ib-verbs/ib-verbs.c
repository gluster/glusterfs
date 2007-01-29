/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
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

int32_t 
ib_verbs_readv (struct transport *this,
		const struct iovec *vector,
		int32_t count)
{
  /* I am not going to write this function :p */
  return 0;
}


/* This function is used to write the buffer data into the remote buffer */
int32_t 
ib_verbs_post_send (transport_t *trans, ib_qp_struct_t *qp, int32_t len)
{
  if (!qp->send_wr_list)
    return -1;
  struct ibv_sge list = {
    .addr   = (uintptr_t) qp->send_wr_list->buf,
    .length = len,
    .lkey   = qp->send_wr_list->mr->lkey
  };
  
  ib_cq_comp_t *ibcq_comp = calloc (1, sizeof (ib_cq_comp_t));
  ibcq_comp->type = 0; // Send Q
  ibcq_comp->qp = qp;
  ibcq_comp->trans = trans;
  ibcq_comp->mr = qp->send_wr_list;
  qp->send_wr_list = qp->send_wr_list->next;

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
ib_verbs_post_recv (transport_t *trans, ib_qp_struct_t *qp)
{
  int32_t ret = -1;
  if (!qp->recv_wr_list) 
    return ret;

  struct ibv_sge list = {
    .addr   = (uintptr_t) qp->recv_wr_list->buf,
    .length = qp->recv_wr_list->buf_size,
    .lkey   = qp->recv_wr_list->mr->lkey
  };
  ib_cq_comp_t *ibcq_comp = calloc (1, sizeof (ib_cq_comp_t));
  ibcq_comp->type = 1; // Recv Q
  ibcq_comp->qp = qp;
  ibcq_comp->mr = qp->recv_wr_list;
  ibcq_comp->trans = trans;
  qp->recv_wr_list = qp->recv_wr_list->next;

  struct ibv_recv_wr wr = {
    .wr_id      = (uint64_t)(long)ibcq_comp,
    .sg_list    = &list,
    .num_sge    = 1,
  };
  struct ibv_recv_wr *bad_wr;

  ret = ibv_post_recv(qp->qp, &wr, &bad_wr);
  return ret;
}

int32_t 
ib_verbs_ibv_init (ib_verbs_dev_t *ibv)
{
  struct ibv_device **dev_list;
  struct ibv_device *ib_dev;
  char *ib_devname = NULL;

  srand48(getpid() * time(NULL));

  dev_list = ibv_get_device_list(NULL);
  if (!dev_list) {
    gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "No IB devices found\n");
    return -1;
  }

  //TODO: get ib_devname from options.
  if (!ib_devname) {
    ib_dev = *dev_list;
    if (!ib_dev) {
      gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "No IB devices found\n");
      return -1;
    }
  } else {
    for (; (ib_dev = *dev_list); ++dev_list)
      if (!strcmp(ibv_get_device_name(ib_dev), ib_devname))
	break;
    if (!ib_dev) {
      gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "IB device %s not found\n", ib_devname);
      return -1;
    }
  }

  gf_log ("transport/ib-verbs", GF_LOG_DEBUG, "using the device %s for ib-verbs transport", 
	  ibv_get_device_name(ib_dev));
  ibv->ib_dev = ib_dev;

  ibv->context = ibv_open_device (ib_dev);
  if (!ibv->context) {
    gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't get context for %s\n",
	    ibv_get_device_name(ib_dev));
    return -1;
  }
  ibv->channel = ibv_create_comp_channel(ibv->context);
  if (!ibv->channel) {
    gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't create completion channel\n");
    return -1;
  }
  
  ibv->pd = ibv_alloc_pd(ibv->context);
  if (!ibv->pd) {
    gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't allocate PD\n");
    return -1;
  }

  ibv->cq = ibv_create_cq(ibv->context, 100 + 1, NULL, ibv->channel, 0); //TODO: rx_depth
  if (!ibv->cq) {
    gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't create CQ\n");
    return -1;
  }

  /* Required to get the CQ notifications */
  if (ibv_req_notify_cq(ibv->cq, 0)) {
    fprintf(stderr, "Couldn't request CQ notification\n");
    return -1;
  }

  return 0; 
}

int32_t 
ib_verbs_writev (struct transport *this,
		 const struct iovec *vector,
		 int32_t count)
{
  ib_verbs_private_t *priv = this->private;

  int32_t i, len = 0;
  const struct iovec *trav = vector;

  for (i = 0; i< count; i++) {
    len += trav[i].iov_len;
  }

  /* See if the buffer (memory region) is free, then send it */
  int32_t qp_idx = 0;
  if (len <= priv->ibv.qp[0].send_wr_size + 2048) {
    qp_idx = IBVERBS_CMD_QP;
  } else {
    qp_idx = IBVERBS_MISC_QP;
    
    if (!priv->ibv.qp[1].send_wr_list)
      priv->ibv.qp[1].send_wr_list = calloc (1, sizeof (ib_mr_struct_t));

    if (priv->ibv.qp[1].send_wr_list->buf_size < len) {
      /* Already allocated data buffer is not enough, allocate bigger chunk */
      if (priv->ibv.qp[1].send_wr_list->buf)
	free (priv->ibv.qp[1].send_wr_list->buf);

      priv->ibv.qp[1].send_wr_list->buf = valloc (len + 2048);
      priv->ibv.qp[1].send_wr_list->buf_size = len + 2048;
      memset (priv->ibv.qp[1].send_wr_list->buf, 0, len + 2048);

      priv->ibv.qp[1].send_wr_list->mr = ibv_reg_mr(priv->ibv.pd, 
						    priv->ibv.qp[1].send_wr_list->buf, 
						    len + 2048,
						    IBV_ACCESS_LOCAL_WRITE);
      if (!priv->ibv.qp[1].send_wr_list->mr) {
	gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't allocate MR\n");
	return -1;
      }
    }
    sprintf (priv->ibv.qp[0].send_wr_list->buf, 
	     "NeedDataMR:%d\n", len + 4);
    write (2, priv->ibv.qp[0].send_wr_list->buf, 40); 
    ib_verbs_post_send (this, &priv->ibv.qp[0], 40);
  }  
  
  len = 0;
  for (i = 0; i< count; i++) {
    memcpy (priv->ibv.qp[qp_idx].send_wr_list->buf + len, trav[i].iov_base, trav[i].iov_len);
    len += trav[i].iov_len;
  }

  if (ib_verbs_post_send (this, &priv->ibv.qp[qp_idx], len) < 0) {
    return -EINTR;
  }
  return 0;
}

/* CQ notify */
int32_t 
ib_verbs_cq_notify (xlator_t *xl,
		    transport_t *trans,
		    int32_t event)
{
  ib_verbs_private_t *priv = (ib_verbs_private_t *)trans->private;

  struct ibv_wc wc;
  struct ibv_cq *event_cq;
  void *event_ctx; 

  /* Get the event from Channel FD */
  ibv_get_cq_event (priv->ibv.channel, &event_cq, &event_ctx);

  /* Acknowledge the CQ event. ==NOT SO COMPULSARY== */
  ibv_ack_cq_events (priv->ibv.cq, IBVERBS_DEV_PORT); //1 is the port

  /* Request for CQ event */
  ibv_req_notify_cq (priv->ibv.cq, 0);

  /* This will poll in the CQ for event type */
  ibv_poll_cq (priv->ibv.cq, 1, &wc);

  /* Get the actual priv pointer from wc */
  ib_cq_comp_t *ib_cq_comp = (ib_cq_comp_t *)(long)wc.wr_id;
  ib_qp_struct_t *qp = ib_cq_comp->qp;
  ib_mr_struct_t *mr = ib_cq_comp->mr;
  transport_t *my_trans = ib_cq_comp->trans;

  priv = my_trans->private;

  if (ib_cq_comp->type == 0) {
    /* send complete */
    //TODO: mark the block as free 
    mr->next = qp->send_wr_list;
    qp->send_wr_list = mr;

    return 0;
  }

  /* Read the buffer */
  /* Actually read and send the data to notify only if its recv queue thing */
  /* If its of send queue, check the linked list for more buffer */

  if (strncmp (mr->buf, "NeedDataMR", 10) == 0) {
    /* Check the existing misc buf list size, if smaller, allocate new and use. */
    int32_t buflen = 0;

    write (2, mr->buf, 40); //DEBUG

    sscanf (mr->buf, "NeedDataMR:%d\n", &buflen);

    if (!priv->ibv.qp[1].recv_wr_list)
      priv->ibv.qp[1].recv_wr_list = calloc (1, sizeof (ib_mr_struct_t ));

    if (buflen > priv->ibv.qp[1].recv_wr_list->buf_size) {
      /* Free the buffer if already exists */
      if (!priv->ibv.qp[1].recv_wr_list->buf) 
	free (priv->ibv.qp[1].recv_wr_list->buf);

      priv->ibv.qp[1].recv_wr_list->buf = valloc (buflen + 2048);
      memset (priv->ibv.qp[1].recv_wr_list->buf, 0, buflen + 2048);
      priv->ibv.qp[1].recv_wr_list->buf_size = buflen + 2048;
      priv->ibv.qp[1].recv_wr_list->mr = ibv_reg_mr(priv->ibv.pd, 
						    priv->ibv.qp[1].recv_wr_list->buf, 
						    buflen + 2048, 
						    IBV_ACCESS_LOCAL_WRITE);
      if (!priv->ibv.qp[1].recv_wr_list->mr) {
	gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't allocate QP[1]->MR\n");
	return -1;
      }
    }

    mr->next = priv->ibv.qp[0].recv_wr_list;
    priv->ibv.qp[0].recv_wr_list = mr;

    ib_verbs_post_recv (my_trans, &priv->ibv.qp[1]);
  
    return 0;
  }

  priv->ibv_comp = ib_cq_comp;
  
  /* Used by receive */
  priv->data_ptr = mr->buf;
  priv->data_offset = 0;
  my_trans->buf = str_to_data (mr->buf);
  
  /* Call the protocol's notify */
  priv->notify (my_trans->xl, my_trans, event);

  /* Put back the recv buffer in the queue */  
  mr->next = qp->recv_wr_list;
  qp->recv_wr_list = mr;
  
  ib_verbs_post_recv (my_trans, qp);

  return 0;
}

/* Connect all the 3 QPs with the remote QP */
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
      gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Failed to modify QP[%d] to RTR\n", i);
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
      gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Failed to modify QP[%d] to RTS\n", i);
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
	.send_cq = priv->ibv.cq,
	.recv_cq = priv->ibv.cq,
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
	gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't create QP[%d]\n", i);
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
	gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Failed to modify QP[%d] to INIT\n", i);
	return -1;
      }
    }

    priv->ibv.qp[i].local_lid = ib_verbs_get_local_lid (priv->ibv.context, 1); //port
    priv->ibv.qp[i].local_qpn = priv->ibv.qp[i].qp->qp_num;
    priv->ibv.qp[i].local_psn = lrand48() & 0xffffff;
    
    if (!priv->ibv.qp[i].local_lid) {
      gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't get Local LID");
      return -1;
    }
  }
  return 0;
}

//TODO
int32_t 
ib_verbs_recieve (struct transport *this,
		  char *buf, 
		  int32_t len)
{
  GF_ERROR_IF_NULL (this);
  
  ib_verbs_private_t *priv = this->private;
  int ret = 0;

  GF_ERROR_IF_NULL (priv);
  GF_ERROR_IF_NULL (buf);
  GF_ERROR_IF (len < 0);
  
  if (!priv->connected) {
    /* Should only be used by client while do_handshake as its synchronous call as of now */
    struct ibv_wc wc;
    struct ibv_cq *ev_cq;
    void *ev_ctx;
    /* Get the event from CQ */
    ibv_get_cq_event (priv->ibv.channel, &ev_cq, &ev_ctx);
    ibv_poll_cq (priv->ibv.cq, 1, &wc);
    ibv_req_notify_cq (priv->ibv.cq, 0);

    /* Set the proper pointer for buffers */
    ib_cq_comp_t *ibcqcomp = (ib_cq_comp_t *)(long)wc.wr_id;
    priv->data_ptr = ibcqcomp->mr->buf;
    priv->data_offset = 0;
    this->buf = str_to_data (priv->data_ptr);
    priv->connected = 1;
  }

  /* Copy the data from the QP buffer to the requested buffer */
  memcpy (buf, priv->data_ptr + priv->data_offset, len);
  priv->data_offset += len;

  return ret;
}

int32_t 
ib_verbs_create_buf_list (ib_verbs_dev_t *ibv) 
{
  int i, count;
  for (i = 0; i<NUM_QP_PER_CONN; i++) {
    /* Send list */
    ib_mr_struct_t *temp, *trav = NULL;
    for (count = 0; count < ibv->qp[i].send_wr_count; count++) {
      if (ibv->qp[i].send_wr_size) {
	temp = calloc (1, sizeof (ib_mr_struct_t));
	temp->next = trav;
	temp->buf = valloc (ibv->qp[i].send_wr_size + 2048);
	temp->buf_size = ibv->qp[i].send_wr_size + 2048;
	memset (temp->buf, 0, temp->buf_size);
	//Register MR
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
    }
    temp = NULL; trav = NULL;
    /* Create the recv list */
    for (count = 0; count < ibv->qp[i].recv_wr_count; count++) {
      if (ibv->qp[i].recv_wr_size) {
	temp = calloc (1, sizeof (ib_mr_struct_t));
	temp->next = trav;
	temp->buf = valloc (ibv->qp[i].recv_wr_size + 2048);
	temp->buf_size = ibv->qp[i].recv_wr_size + 2048;
	memset (temp->buf, 0, temp->buf_size);
	//Register MR
	temp->mr = ibv_reg_mr (ibv->pd, temp->buf, temp->buf_size, IBV_ACCESS_LOCAL_WRITE);
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
ib_verbs_disconnect (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;

  if (close (priv->sock) != 0) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_ERROR,
	    "ib_verbs_disconnect: close () - error: %s",
	    strerror (errno));
    return -errno;
  }

  priv->connected = 0;
  return 0;
}
