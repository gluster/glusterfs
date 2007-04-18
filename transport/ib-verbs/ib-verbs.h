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

struct _ib_verbs_options {
  int32_t port;
  char *device_name;
  enum ibv_mtu mtu;
  int32_t send_count, send_size, recv_count, recv_size;
};

typedef struct _ib_verbs_options ib_verbs_options_t;

/* context per device, stored in global glusterfs_ctx_t->ib */
struct _ib_verbs_ctx {
  struct _ib_verbs_ctx *next;
  const char *device_name;
  struct ibv_context *ctx;
  struct ibv_comp_channel *send_chan[2], *recv_chan[2];
  struct ibv_cq *send_cq[2], *recv_cq[2];
  transport_t *send_trans, *recv_trans;
};
typedef struct _ib_verbs_ctx ib_verbs_ctx_t;

struct _ib_verbs_post {
  struct _ib_verbs_post *next;
  struct _ib_verbs_post *prev;
  struct ibv_mr *mr;
  char *buf;
  int32_t buf_size;
  int32_t used; /* yes or no (0|1) */
};
typedef struct _ib_verbs_post ib_verbs_post_t;

struct _ib_verbs_peer {
  /* pointer to QP */
  struct ibv_qp *qp;

  int32_t qp_index; 

  /* Memory related variables */
  ib_verbs_post_t *send_list;
  ib_verbs_post_t *recv_list;

  int32_t recv_count;
  int32_t send_count;
  int32_t recv_size;
  int32_t send_size;

  /* QP attributes, needed to connect with remote QP */
  int32_t local_lid;
  int32_t local_psn;
  int32_t local_qpn;
  int32_t remote_lid;
  int32_t remote_psn;
  int32_t remote_qpn;
}; 
typedef struct _ib_verbs_peer ib_verbs_peer_t;

struct _ib_verbs_comp {
  int32_t type;        /* activity type, Send CQ complete, Recv CQ complete or what antha */
  ib_verbs_peer_t *peer;  /* QP */
  ib_verbs_post_t *post;
  transport_t *trans;   

  void *private; /* every structure should have a private variable :p */
};
typedef struct _ib_verbs_comp ib_verbs_comp_t;

struct _ib_verbs_private {
  int32_t sock;
  unsigned char connected;
  unsigned char is_debug;
  in_addr_t addr;
  unsigned short port;
  char *volume;

  /* registered for polling*/
  int32_t registered;

  /* IB Verbs Driver specific variables, pointers */
  struct ibv_pd *pd;
  ib_verbs_peer_t peers[2];
  ib_verbs_ctx_t *ctx;
  ib_verbs_options_t options;

  /* CQ completion struct pointer */
  ib_verbs_comp_t *ibv_comp;

  /* Used by trans->op->receive */
  char *data_ptr;
  int32_t data_offset;

  /* Mutex */
  pthread_mutex_t read_mutex;
  pthread_mutex_t write_mutex;

  //  dict_t *options;
  /* Notify function, used by the protocol/<client/server> */
  int32_t (*notify) (xlator_t *xl, transport_t *trans, int32_t event); 
};
typedef struct _ib_verbs_private ib_verbs_private_t;

enum {
  IBVERBS_CMD_QP = 0, /* All operations which fits in Work Request Buffer size */
  IBVERBS_MISC_QP     /* Used for all the larger size operations */
};

/* Regular functions, used by the transports */
int32_t ib_verbs_writev (struct transport *this, const struct iovec *vector, int32_t count);
int32_t ib_verbs_recieve (transport_t *this, char *buf, int32_t len);

/* uses ibv_post_recv */
int32_t ib_verbs_post_recv (transport_t *trans,
			    ib_verbs_peer_t *peer,
			    ib_verbs_post_t *post);
/* ibv_post_send */
int32_t ib_verbs_post_send (transport_t *trans, 
			    ib_verbs_peer_t *peer, 
			    ib_verbs_post_t *post, 
			    int32_t len);

/* Device Init */
int32_t ib_verbs_init (transport_t *ibv);

int32_t ib_verbs_conn_setup (transport_t *trans);

/* Modify QP attr to connect with remote QP, (connects all the 3 QPs) */
int32_t ib_verbs_ibv_connect (ib_verbs_private_t *priv);

/* Create QP */
int32_t ib_verbs_create_qp (transport_t *this);

/* Create buffer list */
int32_t ib_verbs_create_buf_list (ib_verbs_private_t *ibv);

/* Used as notify function for all CQ activity */
int32_t ib_verbs_send_cq_notify (xlator_t *xl, transport_t *trans, int32_t event);
int32_t ib_verbs_recv_cq_notify (xlator_t *xl, transport_t *trans, int32_t event);
int32_t ib_verbs_send_cq_notify1 (xlator_t *xl, transport_t *trans, int32_t event);


int32_t ib_verbs_bail (transport_t *this);

#endif /* _XPORT_IB_VERBS_H */
