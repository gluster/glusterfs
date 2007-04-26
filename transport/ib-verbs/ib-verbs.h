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

#define CLIENT_PORT_CIELING 1023

/* options per transport end point */
struct _ib_verbs_options {
  int32_t port;
  char *device_name;
  enum ibv_mtu mtu;
  int32_t send_count, send_size, recv_count, recv_size;
};
typedef struct _ib_verbs_options ib_verbs_options_t;


/* represents one communication peer, two per transport_t */
struct _ib_verbs_peer {
  transport_t *trans;
  struct ibv_qp *qp;

  int32_t recv_count;
  int32_t send_count;
  int32_t recv_size;
  int32_t send_size;

  int32_t quota;
  pthread_cond_t has_quota;
  pthread_mutex_t lock;

  /* QP attributes, needed to connect with remote QP */
  int32_t local_lid;
  int32_t local_psn;
  int32_t local_qpn;
  int32_t remote_lid;
  int32_t remote_psn;
  int32_t remote_qpn;
};
typedef struct _ib_verbs_peer ib_verbs_peer_t;


struct _ib_verbs_post {
  struct _ib_verbs_post *next, *prev;
  struct ibv_mr *mr;
  char *buf;
  int32_t buf_size;
  char aux;
  pthread_barrier_t wait;
};
typedef struct _ib_verbs_post ib_verbs_post_t;


struct _ib_verbs_queue {
  ib_verbs_post_t active_posts, passive_posts;
  int32_t active_count, passive_count;
  pthread_mutex_t lock;
};
typedef struct _ib_verbs_queue ib_verbs_queue_t;


struct _ib_verbs_qpreg {
  pthread_mutex_t lock;
  int32_t count;
  struct _qpent {
    struct _qpent *next, *prev;
    int32_t qp_num;
    ib_verbs_peer_t *peer;
  } ents[42];
};
typedef struct _ib_verbs_qpreg ib_verbs_qpreg_t;

/* context per device, stored in global glusterfs_ctx_t->ib */
struct _ib_verbs_device {
  struct _ib_verbs_device *next;
  const char *device_name;
  struct ibv_context *context;
  int32_t port;
  struct ibv_pd *pd;
  struct ibv_srq *srq[2];
  ib_verbs_qpreg_t qpreg;
  struct ibv_comp_channel *send_chan, *recv_chan[2];
  struct ibv_cq *send_cq, *recv_cq[2];
  ib_verbs_queue_t sendq, recvq;
  pthread_t send_thread, recv_thread[2];
};
typedef struct _ib_verbs_device ib_verbs_device_t;

struct _ib_verbs_private {
  int32_t sock;
  unsigned char connected;
  unsigned char connection_in_progress;
  unsigned char ib_connected;
  in_addr_t addr;
  unsigned short port;

  /* IB Verbs Driver specific variables, pointers */
  ib_verbs_peer_t peers[2];
  ib_verbs_device_t *device;
  ib_verbs_options_t options;

  /* Used by trans->op->receive */
  char *data_ptr;
  int32_t data_offset;

  /* Mutex */
  pthread_mutex_t read_mutex;
  pthread_mutex_t write_mutex;
  pthread_barrier_t handshake_barrier;
  char handshake_ret;

  /* Notify function, used by the protocol/<client/server> */
  int32_t (*notify) (xlator_t *xl, transport_t *trans, int32_t event);
  int32_t (*notify_tmp) (xlator_t *xl, transport_t *trans, int32_t event);
};
typedef struct _ib_verbs_private ib_verbs_private_t;


/* Regular functions, used by the transports */
int32_t ib_verbs_writev (transport_t *this, const struct iovec *vector, int32_t count);
int32_t ib_verbs_recieve (transport_t *this, char *buf, int32_t len);

/* uses ibv_post_recv */
//int32_t ib_verbs_post_recv (transport_t *trans,
//			    ib_verbs_peer_t *peer,
//			    ib_verbs_post_t *post);
/* ibv_post_send */
//int32_t ib_verbs_post_send (struct ibv_qp *qp,
//			    ib_verbs_post_t *post, 
//			    int32_t len);

/* Device Init */
int32_t ib_verbs_init (transport_t *this);

int32_t ib_verbs_handshake (transport_t *this);

//int32_t ib_verbs_connect (transport_t *priv);

/* Create QP */
//int32_t ib_verbs_create_qp (transport_t *this);

int32_t ib_verbs_bail (transport_t *this);
int32_t ib_verbs_except (transport_t *this);
int32_t ib_verbs_teardown (transport_t *this);
int32_t ib_verbs_disconnect (transport_t *this);

int32_t ib_verbs_tcp_notify (xlator_t *xl, transport_t *this, int32_t event);
#endif /* _XPORT_IB_VERBS_H */
