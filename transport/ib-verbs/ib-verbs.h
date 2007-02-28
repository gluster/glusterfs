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

#ifndef _XPORT_IB_VERBS_H
#define _XPORT_IB_VERBS_H

#include <stdio.h>
#include <arpa/inet.h>

#include <infiniband/verbs.h>

#define NUM_QP_PER_CONN 2
#define CLIENT_PORT_CIELING 1023

struct ib_verbs_private_struct;

struct _ib_mr_struct {
  struct _ib_mr_struct *next;
  struct _ib_mr_struct *prev;
  struct ibv_mr *mr;
  char *buf;
  int32_t buf_size;
  int32_t used; /* yes or no (0|1) */
};
typedef struct _ib_mr_struct ib_mr_struct_t;

struct _ib_qp_struct {
  /* pointer to QP */
  struct ibv_qp *qp;

  int32_t qp_index; 

  /* Memory related variables */
  ib_mr_struct_t *send_wr_list;
  ib_mr_struct_t *recv_wr_list;

  int32_t recv_wr_count;
  int32_t send_wr_count;
  int32_t recv_wr_size;
  int32_t send_wr_size;

  /* QP attributes, needed to connect with remote QP */
  int32_t local_lid;
  int32_t local_psn;
  int32_t local_qpn;
  int32_t remote_lid;
  int32_t remote_psn;
  int32_t remote_qpn;
}; 
typedef struct _ib_qp_struct ib_qp_struct_t;

struct ib_verbs_dev_struct {
  struct ibv_device       *ib_dev;
  struct ibv_context      *context;
  struct ibv_pd           *pd;
  struct ibv_comp_channel *send_channel[2]; /* Used for polling on sendcq activity */
  struct ibv_comp_channel *recv_channel[2]; /* Used for polling on recvcq activity */
  struct ibv_cq           *sendcq[2];       /* Send Completion Queue */
  struct ibv_cq           *recvcq[2];       /* Receive CQ */
  ib_qp_struct_t          qp[NUM_QP_PER_CONN];  /* Need 2 qps */
};
typedef struct ib_verbs_dev_struct ib_verbs_dev_t;

struct _ib_cq_comp {
  int32_t type;        /* activity type, Send CQ complete, Recv CQ complete or what antha */
  ib_qp_struct_t *qp;  /* QP */
  ib_mr_struct_t *mr;
  transport_t *trans;   

  void *private; /* every structure should have a private variable :p */
};
typedef struct _ib_cq_comp ib_cq_comp_t;

struct ib_verbs_private_struct {
  int32_t sock;
  unsigned char connected;
  unsigned char is_debug;
  in_addr_t addr;
  unsigned short port;
  char *volume;

  /* registered for polling*/
  int32_t registered;

  /* IB Verbs Driver specific variables, pointers */
  ib_verbs_dev_t ibv;

  /* CQ completion struct pointer */
  ib_cq_comp_t *ibv_comp;

  /* Used by trans->op->receive */
  char *data_ptr;
  int32_t data_offset;

  /* Mutex */
  pthread_mutex_t read_mutex;
  pthread_mutex_t write_mutex;

  dict_t *options;
  /* Notify function, used by the protocol/<client/server> */
  int32_t (*notify) (xlator_t *xl, transport_t *trans, int32_t event); 
};
typedef struct ib_verbs_private_struct ib_verbs_private_t;

enum {
  IBVERBS_CMD_QP = 0, /* All operations which fits in Work Request Buffer size */
  IBVERBS_MISC_QP     /* Used for all the larger size operations */
};

/* Regular functions, used by the transports */
int32_t ib_verbs_readv (struct transport *this,	const struct iovec *vector, int32_t count);
int32_t ib_verbs_writev (struct transport *this, const struct iovec *vector, int32_t count);

int32_t ib_verbs_recieve (transport_t *this, char *buf, int32_t len);

/* uses ibv_post_recv */
int32_t ib_verbs_post_recv (transport_t *trans, ib_qp_struct_t *qp, ib_mr_struct_t *mr);
/* ibv_post_send */
int32_t ib_verbs_post_send (transport_t *trans, 
			    ib_qp_struct_t *qp, 
			    ib_mr_struct_t *mr, 
			    int32_t len);

/* Device Init */
int32_t ib_verbs_ibv_init (ib_verbs_dev_t *ibv);

/* Modify QP attr to connect with remote QP, (connects all the 3 QPs) */
int32_t ib_verbs_ibv_connect (ib_verbs_private_t *priv, 
			      int32_t port, 
			      enum ibv_mtu mtu);
/* Create QP */
int32_t ib_verbs_create_qp (ib_verbs_private_t *priv);

/* Create buffer list */
int32_t ib_verbs_create_buf_list (ib_verbs_dev_t *ibv);

/* Used as notify function for all CQ activity */
int32_t ib_verbs_send_cq_notify (xlator_t *xl, transport_t *trans, int32_t event);
int32_t ib_verbs_recv_cq_notify (xlator_t *xl, transport_t *trans, int32_t event);
int32_t ib_verbs_send_cq_notify1 (xlator_t *xl, transport_t *trans, int32_t event);


#endif /* _XPORT_IB_VERBS_H */
