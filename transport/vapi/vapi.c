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
#include "vapi.h"


int32_t 
vapi_full_write (vapi_private_t *priv, char *buf, int32_t len)
{
  struct ibv_sge list = {
    .addr   = (uintptr_t) buf,
    .length = len,
    .lkey   = priv->mr->lkey
  };
  struct ibv_send_wr wr = {
    .wr_id      = VAPI_SEND_WRID,
    .sg_list    = &list,
    .num_sge    = 1,
    .opcode     = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED,
  };
  struct ibv_send_wr *bad_wr;

  return ibv_post_send(priv->qp, &wr, &bad_wr);
}

int32_t 
vapi_full_read (vapi_private_t *priv, char *buf, int32_t len)
{
  struct ibv_sge list = {
    .addr   = (uintptr_t) buf,
    .length = len,
    .lkey   = priv->mr->lkey
  };
  struct ibv_recv_wr wr = {
    .wr_id      = VAPI_RECV_WRID,
    .sg_list    = &list,
    .num_sge    = 1,
  };
  struct ibv_recv_wr *bad_wr;

  return ibv_post_recv(priv->qp, &wr, &bad_wr);
}


uint16_t 
vapi_get_local_lid (struct ibv_context *context, int32_t port)
{
  struct ibv_port_attr attr;

  if (ibv_query_port(context, port, &attr))
    return 0;

  return attr.lid;
}

int32_t 
vapi_ibv_connect (vapi_private_t *priv, 
		  int32_t port, 
		  int32_t my_psn,
		  enum ibv_mtu mtu) 
{
  struct ibv_qp_attr attr = {
    .qp_state               = IBV_QPS_RTR,
    .path_mtu               = mtu,
    .dest_qp_num            = priv->remote.qpn,
    .rq_psn                 = priv->remote.psn,
    .max_dest_rd_atomic     = 1,
    .min_rnr_timer          = 12,
    .ah_attr                = {
      .is_global      = 0,
      .dlid           = priv->remote.lid,
      .sl             = 0,
      .src_path_bits  = 0,
      .port_num       = port
    }
  };
  if (ibv_modify_qp(priv->qp, &attr,
		    IBV_QP_STATE              |
		    IBV_QP_AV                 |
		    IBV_QP_PATH_MTU           |
		    IBV_QP_DEST_QPN           |
		    IBV_QP_RQ_PSN             |
		    IBV_QP_MAX_DEST_RD_ATOMIC |
		    IBV_QP_MIN_RNR_TIMER)) {
    gf_log ("transport/vapi", GF_LOG_CRITICAL, "Failed to modify QP to RTR\n");
    return -1;
  }

  attr.qp_state       = IBV_QPS_RTS;
  attr.timeout        = 14;
  attr.retry_cnt      = 7;
  attr.rnr_retry      = 7;
  attr.sq_psn         = my_psn;
  attr.max_rd_atomic  = 1;
  if (ibv_modify_qp(priv->qp, &attr,
		    IBV_QP_STATE              |
		    IBV_QP_TIMEOUT            |
		    IBV_QP_RETRY_CNT          |
		    IBV_QP_RNR_RETRY          |
		    IBV_QP_SQ_PSN             |
		    IBV_QP_MAX_QP_RD_ATOMIC)) {
    gf_log ("transport/vapi", GF_LOG_CRITICAL, "Failed to modify QP to RTS\n");
    return -1;
  }
  
  return 0;
}


int32_t 
vapi_ibv_init (vapi_private_t *priv, struct ibv_device *ibdev)
{
  priv->ib_dev = ibdev;
  priv->size = 4096; //todo
  priv->rx_depth = 500; //todo

  priv->context = ibv_open_device (ibdev);
  if (!priv->context) {
    gf_log ("t/v", GF_LOG_CRITICAL, "Couldn't get context for %s\n",
	    ibv_get_device_name(ibdev));
    return -1;
  }
  int32_t use_event = 0;
  if (use_event) {
    priv->channel = ibv_create_comp_channel(priv->context);
    if (!priv->channel) {
      gf_log ("t/v", GF_LOG_CRITICAL, "Couldn't create completion channel\n");
      return -1;
    }
  } else
    priv->channel = NULL;

  priv->pd = ibv_alloc_pd(priv->context);
  if (!priv->pd) {
    gf_log ("t/v", GF_LOG_CRITICAL, "Couldn't allocate PD\n");
    return -1;
  }

  char *buf = calloc (1, 4096);
  priv->mr = ibv_reg_mr(priv->pd, buf, priv->size, IBV_ACCESS_LOCAL_WRITE);
  if (!priv->mr) {
    gf_log ("t/v", GF_LOG_CRITICAL, "Couldn't allocate MR\n");
    return -1;
  }

  priv->cq = ibv_create_cq(priv->context, priv->rx_depth + 1, NULL,
			  priv->channel, 0);
  if (!priv->cq) {
    gf_log ("t/v", GF_LOG_CRITICAL, "Couldn't create CQ\n");
    return -1;
  }
  {
    struct ibv_qp_init_attr attr = {
      .send_cq = priv->cq,
      .recv_cq = priv->cq,
      .cap     = {
	.max_send_wr  = 1,
	.max_recv_wr  = priv->rx_depth,
	.max_send_sge = 1,
	.max_recv_sge = 1
      },
      .qp_type = IBV_QPT_RC
    };
    
    priv->qp = ibv_create_qp(priv->pd, &attr);
    if (!priv->qp)  {
      gf_log ("t/v", GF_LOG_CRITICAL, "Couldn't create QP\n");
      return -1;
    }
  }
  
  {
    struct ibv_qp_attr attr;
    
    attr.qp_state        = IBV_QPS_INIT;
    attr.pkey_index      = 0;
    attr.port_num        = 1; //port;
    attr.qp_access_flags = 0;
    
    if (ibv_modify_qp(priv->qp, &attr,
		      IBV_QP_STATE              |
		      IBV_QP_PKEY_INDEX         |
		      IBV_QP_PORT               |
		      IBV_QP_ACCESS_FLAGS)) {
      gf_log ("t/v", GF_LOG_CRITICAL, "Failed to modify QP to INIT\n");
      return -1;
    }
  }

  priv->local.lid = vapi_get_local_lid (priv->context, 1); //port
  priv->local.qpn = priv->qp->qp_num;
  priv->local.psn = lrand48() & 0xffffff;

  return 0; 
}

int32_t 
vapi_recieve (struct transport *this,
	      char *buf, 
	      int32_t len)
{
  GF_ERROR_IF_NULL (this);

  vapi_private_t *priv = this->private;
  int ret = 0;

  GF_ERROR_IF_NULL (priv);
  GF_ERROR_IF_NULL (buf);
  GF_ERROR_IF (len < 0);

  if (!priv->connected)
    return -1;
  
  //  pthread_mutex_lock (&priv->read_mutex);
  // ret = full_read (priv->sock, buf, len);
  ret = vapi_full_read (priv, buf, len);
  //  pthread_mutex_unlock (&priv->read_mutex);
  return ret;
}

int32_t 
vapi_disconnect (transport_t *this)
{
  vapi_private_t *priv = this->private;

  if (close (priv->sock) != 0) {
    gf_log ("transport/vapi",
	    GF_LOG_ERROR,
	    "vapi_disconnect: close () - error: %s",
	    strerror (errno));
    return -errno;
  }

  priv->connected = 0;
  return 0;
}


