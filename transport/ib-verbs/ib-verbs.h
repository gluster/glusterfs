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

#define CMD_BUF_SIZE 4096
#define CLIENT_PORT_CIELING 1023

struct wait_queue {
  struct wait_queue *next;
  char *buf;
  int32_t len;
};

  
struct _ib_devattr {
  int32_t lid;
  int32_t psn;
  int32_t qpn;
}; 
typedef struct _ib_devattr ib_devattr_t;

typedef struct ib_verbs_private ib_verbs_private_t;
struct ib_verbs_private {
  int32_t sock;
  unsigned char connected;
  unsigned char is_debug;
  in_addr_t addr;
  unsigned short port;
  char *volume;

  struct ibv_device *ib_dev;
  struct ibv_context      *context;
  struct ibv_comp_channel *channel;
  struct ibv_pd           *pd;
  struct ibv_cq           *cq;    /* Completion Queue */
  struct ibv_mr           *mr[2]; /* One for cmd, another for data */
  struct ibv_qp           *qp[2]; /* Different one per connection */

  ib_devattr_t local[2]; /* One per QP */
  ib_devattr_t remote[2]; /* One per QP */

  char *buf[2];
  int32_t data_buf_size;

  dict_t *options;
  int32_t (*notify) (xlator_t *xl, transport_t *trans, int32_t event); /* used by ib-verbs/server */
};

enum {
  IBVERBS_CMD_QP = 0,
  IBVERBS_DATA_QP = 1,
};

int32_t ib_verbs_disconnect (transport_t *this);
int32_t ib_verbs_recieve (transport_t *this, char *buf, int32_t len);
int32_t ib_verbs_submit (transport_t *this, char *buf, int32_t len);
int32_t ib_verbs_post_recv (ib_verbs_private_t *priv, int32_t len, int32_t qp_id);
int32_t ib_verbs_post_send (ib_verbs_private_t *priv, int32_t len, int32_t qp_id);
int32_t ib_verbs_ibv_init (ib_verbs_private_t *priv);
int32_t ib_verbs_ibv_connect (ib_verbs_private_t *priv, 
			      int32_t port, 
			      enum ibv_mtu mtu);
int32_t ib_verbs_create_qp (ib_verbs_private_t *priv, int32_t qp_id);
int32_t ib_verbs_cq_notify (xlator_t *xl, transport_t *trans, int32_t event);

#endif /* _XPORT_IB_VERBS_H */
