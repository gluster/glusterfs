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

#ifndef _XPORT_IB_VERBS_H
#define _XPORT_IB_VERBS_H

#include <stdio.h>
#include <arpa/inet.h>

#include <infiniband/verbs.h>

#define CLIENT_PORT_CIELING 1023

struct wait_queue {
  struct wait_queue *next;
  char *buf;
  int32_t len;
};

  
struct _ibv_devattr {
  int32_t lid;
  int32_t psn;
  int32_t qpn;
}; 
typedef struct _ibv_devattr ibv_devattr_t;

typedef struct ib_verbs_private ib_verbs_private_t;
struct ib_verbs_private {
  int32_t sock;
  unsigned char connected;
  unsigned char is_debug;
  in_addr_t addr;
  unsigned short port;
  char *volume;
  pthread_mutex_t read_mutex;
  pthread_mutex_t write_mutex;

  struct ibv_device *ib_dev;
  struct ibv_context      *context;
  struct ibv_comp_channel *channel;
  struct ibv_pd           *pd;
  struct ibv_mr           *mr;
  struct ibv_cq           *cq;
  struct ibv_qp           *qp;
  char *buf;
  int32_t size;
  int32_t rx_depth;

  ibv_devattr_t local;
  ibv_devattr_t remote;

  //  pthread_mutex_t queue_mutex;
  //  struct wait_queue *queue;

  dict_t *options;
  int32_t (*notify) (xlator_t *xl, transport_t *trans, int32_t event); /* used by ib-verbs/server */
};

enum {
  VAPI_RECV_WRID = 1,
  VAPI_SEND_WRID = 2,
};

int32_t ib_verbs_disconnect (transport_t *this);
int32_t ib_verbs_recieve (transport_t *this, char *buf, int32_t len);
int32_t ib_verbs_submit (transport_t *this, char *buf, int32_t len);
int32_t ib_verbs_full_read (ib_verbs_private_t *priv, char *buf, int32_t len);
int32_t ib_verbs_full_write (ib_verbs_private_t *priv, char *buf, int32_t len);
int32_t ib_verbs_ibv_init (ib_verbs_private_t *priv);
int32_t ib_verbs_ibv_connect (ib_verbs_private_t *priv, int32_t port, int32_t my_psn, enum ibv_mtu mtu);


#endif /* _XPORT_IB_VERBS_H */
