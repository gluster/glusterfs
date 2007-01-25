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

int32_t 
ib_verbs_readv (struct transport *this,
		const struct iovec *vector,
		int32_t count)
{
  /* TODO: Yet to write */
  return 0;
}

int32_t 
ib_verbs_writev (struct transport *this,
		 const struct iovec *vector,
		 int32_t count)
{
  ib_verbs_private_t *priv = this->private;

  if (!priv->connected)
    return -1;

  /* TODO: get the length from the data */
  int32_t i, len = 0;
  struct iovec *trav = vector;

  for (i = 0; i< count; i++) {
    len += trav[i].iov_len;
  }
  /*TODO: See if the buffer (memory region) is free, then send it */
  int32_t qp_idx = 0;
  if (len > CMD_BUF_SIZE) {
    qp_idx = IBVERBS_DATA_QP;
    if (priv->data_buf_size < len) {
      /* Already allocated data buffer is not enough, allocate bigger chunk */
      if (priv->buf[1])
	free (priv->buf[1]);
      priv->buf[1] = calloc (1, len + 1);
      priv->data_buf_size = len;
      priv->mr[1] = ibv_reg_mr(priv->pd, priv->buf[1], len, IBV_ACCESS_LOCAL_WRITE);
      if (!priv->mr[1]) {
	gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't allocate MR[0]\n");
	return -1;
      }
    }
    sprintf (priv->buf[0], "NeedDataMR with BufLen = %d\n", len - (len % 4) + 4);
    ib_verbs_post_send (priv, 40, IBVERBS_CMD_QP);
  } else
    qp_idx = IBVERBS_CMD_QP;
  len = 0;
  for (i = 0; i< count; i++) {
    len += trav[i].iov_len;
    memcpy (priv->buf[qp_idx] + len, trav[i].iov_base, trav[i].iov_len);
  }
  if (ib_verbs_post_send (priv, len, qp_idx) < 0) {
    return -EINTR;
  }

  return 0;
}


/* This function is used to write the buffer data into the remote buffer */
int32_t 
ib_verbs_post_send (ib_verbs_private_t *priv, int32_t len, int32_t qp_id)
{
  gf_log ("ib-verbs", GF_LOG_DEBUG,"write msg is \"%s\"", priv->buf[qp_id]);

  //  memcpy (priv->buf, buf, len);
  struct ibv_sge list = {
    .addr   = (uintptr_t) priv->buf[qp_id],
    .length = len,
    .lkey   = priv->mr[qp_id]->lkey
  };
  struct ibv_send_wr wr = {
    .wr_id      = (uint64_t)priv,
    .sg_list    = &list,
    .num_sge    = 1,
    .opcode     = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED,
  };
  struct ibv_send_wr *bad_wr;

  return ibv_post_send(priv->qp[qp_id], &wr, &bad_wr);
}

/* Function to post receive request in the QP */
int32_t 
ib_verbs_post_recv (ib_verbs_private_t *priv, int32_t len, int32_t qp_id)
{
  int32_t ret = -1;
  struct ibv_sge list = {
    .addr   = (uintptr_t) priv->buf[qp_id],
    .length = len,
    .lkey   = priv->mr[qp_id]->lkey
  };
  struct ibv_recv_wr wr = {
    .wr_id      = (uint64_t)priv,
    .sg_list    = &list,
    .num_sge    = 1,
  };
  struct ibv_recv_wr *bad_wr;

  ret = ibv_post_recv(priv->qp[qp_id], &wr, &bad_wr);
  return ret;
}

/* For reading from the CQ buf - Put this code in notify function */
// TODO
int32_t 
ib_verbs_read_recvbuf (ib_verbs_private_t *priv, char *buf, int32_t len)
{
  /*
  struct ibv_wc wc[2];
  struct ibv_cq *ev_cq;
  void *ev_ctx;
  int32_t len = 0;
  ibv_get_cq_event (priv->channel, &ev_cq, &ev_ctx);
  ibv_poll_cq (priv->cq, 2, wc);

  ib_verbs_private_t *wc_priv = (ib_verbs_private_t *)wc[0].wr_id;
  len = wc[0].byte_len; 

  if (len > CMD_BUF_SIZE)
    memcpy (buf, wc_priv->buf[IBVERBS_DATA_QP], len);
  else
    memcpy (buf, wc_priv->buf[IBVERBS_CMD_QP], len);
  */

  if (len > CMD_BUF_SIZE)
    memcpy (buf, priv->buf[IBVERBS_DATA_QP], len);
  else
    memcpy (buf, priv->buf[IBVERBS_CMD_QP], len);

  gf_log ("ib-verbs", GF_LOG_DEBUG,
	  "receive buf \" %s\"", buf);

  return len;
}

/* TODO : Complete the function */
int32_t 
ib_verbs_cq_notify (xlator_t *xl,
		    transport_t *trans,
		    int32_t event)
{
  ib_verbs_private_t *priv = (ib_verbs_private_t *)trans->private;

  struct ibv_wc wc[2];
  struct ibv_cq *event_cq;
  void *event_ctx;

  /* Get the event from Channel FD */
  ibv_get_cq_event (priv->channel, &event_cq, &event_ctx);

  /* Acknowledge the CQ event. ==NOT SO COMPULSARY== */
  //  ibv_ack_cq_events (priv->cq, 1);

  /* Request for CQ event */
  ibv_req_notify_cq (priv->cq, 0);

  /* This will poll in the CQ for event type */
  ibv_poll_cq (priv->cq, 2, wc);

  
  /* Read the buffer */
  /* Actually read and send the data to notify only if its recv queue thing */
  /* If its of send queue, check the linked list for more buffer */
    
  char msg[CMD_BUF_SIZE] = {0,};
  ib_verbs_read_recvbuf (priv, msg, 4096);
  gf_log ("ib-verbs/server", GF_LOG_DEBUG, "priv->buf is \"%s\"", msg);
  if (strncmp (msg, "NeedDataMR", 10) == 0) {
    /* TODO : Decide on what will be in the CMD_BUF when data buf is requested */
    /* Allocate another mr */
    gf_log ("ib-verbs", GF_LOG_DEBUG, "Allocating bigger block for receive");
    int32_t buflen = 0;
    sscanf (msg, "BufLen = %d", &buflen);
    if (buflen > priv->data_buf_size) {
      priv->buf[1] = calloc (1, buflen + 1);
      priv->data_buf_size = buflen;
      priv->mr[1] = ibv_reg_mr(priv->pd, priv->buf[1], buflen, IBV_ACCESS_LOCAL_WRITE);
      if (!priv->mr[1]) {
	gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't allocate MR[0]\n");
	return -1;
      }
    }
    ib_verbs_post_recv (priv, buflen, IBVERBS_DATA_QP);
    return 0;
  }

  ib_verbs_post_recv (priv, CMD_BUF_SIZE, IBVERBS_CMD_QP);

  priv->notify (trans->xl, trans, 0);
  
  return 0;
}

uint16_t 
ib_verbs_get_local_lid (struct ibv_context *context, int32_t port)
{
  struct ibv_port_attr attr;

  if (ibv_query_port(context, port, &attr))
    return 0;

  return attr.lid;
}

int32_t 
ib_verbs_ibv_connect (ib_verbs_private_t *priv, 
		      int32_t port, 
		      enum ibv_mtu mtu)
{
  int i = 0;
  for (i = 0; i < 2; i++) {
    struct ibv_qp_attr attr = {
      .qp_state               = IBV_QPS_RTR,
      .path_mtu               = mtu,
      .dest_qp_num            = priv->remote[i].qpn,
      .rq_psn                 = priv->remote[i].psn,
      .max_dest_rd_atomic     = 1,
      .min_rnr_timer          = 12,
      .ah_attr                = {
	.is_global      = 0,
	.dlid           = priv->remote[i].lid,
	.sl             = 0,
	.src_path_bits  = 0,
	.port_num       = port
      }
    };
    if (ibv_modify_qp(priv->qp[i], &attr,
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
    attr.sq_psn         = priv->local[i].psn;
    attr.max_rd_atomic  = 1;
    if (ibv_modify_qp(priv->qp[i], &attr,
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
ib_verbs_ibv_init (ib_verbs_private_t *priv)
{
  
  struct ibv_device **dev_list;
  struct ibv_device *ib_dev;
  char *ib_devname = NULL;
  int32_t page_size = 0;

  srand48(getpid() * time(NULL));

  /* allocate a memory aligned buffer */
  page_size = sysconf(_SC_PAGESIZE);

  priv->buf[0] =  memalign (page_size, CMD_BUF_SIZE); //TODO: buffer size should be more and queueing mechanism should come in
  memset (priv->buf[0], 0, CMD_BUF_SIZE);

  dev_list = ibv_get_device_list(NULL);
  if (!dev_list) {
    gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "No IB devices found\n");
    return -1;
  }

  // get ib_devname from options.
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

  gf_log ("transport/ib-verbs", GF_LOG_DEBUG, "device name is %s", 
	  ibv_get_device_name(ib_dev));
  priv->ib_dev = ib_dev;
  priv->data_buf_size = 0;

  priv->context = ibv_open_device (ib_dev);
  if (!priv->context) {
    gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't get context for %s\n",
	    ibv_get_device_name(ib_dev));
    return -1;
  }
  priv->channel = ibv_create_comp_channel(priv->context);
  if (!priv->channel) {
    gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't create completion channel\n");
    return -1;
  }
  
  priv->pd = ibv_alloc_pd(priv->context);
  if (!priv->pd) {
    gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't allocate PD\n");
    return -1;
  }

  priv->mr[0] = ibv_reg_mr(priv->pd, priv->buf[0], CMD_BUF_SIZE, IBV_ACCESS_LOCAL_WRITE);
  if (!priv->mr[0]) {
    gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't allocate MR[0]\n");
    return -1;
  }
  
  priv->cq = ibv_create_cq(priv->context, 100 + 1, NULL, priv->channel, 0); //TODO: rx_depth
  if (!priv->cq) {
    gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't create CQ\n");
    return -1;
  }

  if (ibv_req_notify_cq(priv->cq, 0)) {
    fprintf(stderr, "Couldn't request CQ notification\n");
    return -1;
  }

  return 0; 
}

int32_t 
ib_verbs_create_qp (ib_verbs_private_t *priv, int32_t qp_id)
{
  {
    struct ibv_qp_init_attr attr = {
      .send_cq = priv->cq,
      .recv_cq = priv->cq,
      .cap     = {
	.max_send_wr  = 1,
	.max_recv_wr  = 100,//TODO : can be changed
	.max_send_sge = 1,
	.max_recv_sge = 1
      },
      .qp_type = IBV_QPT_RC
    };
    
    priv->qp[qp_id] = ibv_create_qp(priv->pd, &attr);
    if (!priv->qp[qp_id])  {
      gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't create QP[%d]\n", qp_id);
      return -1;
    }
  }
  
  {
    struct ibv_qp_attr attr;
    
    attr.qp_state        = IBV_QPS_INIT;
    attr.pkey_index      = 0;
    attr.port_num        = 1; //port;
    attr.qp_access_flags = 0;
    
    if (ibv_modify_qp(priv->qp[qp_id], &attr,
		      IBV_QP_STATE              |
		      IBV_QP_PKEY_INDEX         |
		      IBV_QP_PORT               |
		      IBV_QP_ACCESS_FLAGS)) {
      gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Failed to modify QP[%d] to INIT\n", qp_id);
      return -1;
    }
  }

  priv->local[qp_id].lid = ib_verbs_get_local_lid (priv->context, 1); //port
  priv->local[qp_id].qpn = priv->qp[qp_id]->qp_num;
  priv->local[qp_id].psn = lrand48() & 0xffffff;
 
  if (!priv->local[qp_id].lid) {
    gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't get Local LID");
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
  int ret = 0;

  GF_ERROR_IF_NULL (priv);
  GF_ERROR_IF_NULL (buf);
  GF_ERROR_IF (len < 0);
  
  if (!priv->connected) {
    gf_log ("ib-verbs", GF_LOG_CRITICAL, "Not Connected");
    return -1;
  }
  
  ret = ib_verbs_read_recvbuf (priv, buf, len);

  return ret;
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


