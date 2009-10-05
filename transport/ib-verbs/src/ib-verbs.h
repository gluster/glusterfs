/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _XPORT_IB_VERBS_H
#define _XPORT_IB_VERBS_H


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#ifndef MAX_IOVEC
#define MAX_IOVEC 16
#endif /* MAX_IOVEC */

#include "xlator.h"
#include "event.h"

#include <stdio.h>
#include <list.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>

#define GF_DEFAULT_IBVERBS_LISTEN_PORT 6997

/* options per transport end point */
struct _ib_verbs_options {
        int32_t port;
        char *device_name;
        enum ibv_mtu mtu;
        int32_t  send_count;
        int32_t  recv_count;
        uint64_t recv_size;
        uint64_t send_size;
};
typedef struct _ib_verbs_options ib_verbs_options_t;


struct _ib_verbs_header {
        char     colonO[3];
        uint32_t size1;
        uint32_t size2;
        char     version;
} __attribute__((packed));
typedef struct _ib_verbs_header ib_verbs_header_t;

struct _ib_verbs_ioq {
        union {
                struct list_head list;
                struct {
                        struct _ib_verbs_ioq    *next;
                        struct _ib_verbs_ioq    *prev;
                };
        };
        ib_verbs_header_t  header;
        struct iovec       vector[MAX_IOVEC];
        int                count;
        char              *buf;
        struct iobref     *iobref;
};
typedef struct _ib_verbs_ioq ib_verbs_ioq_t;

/* represents one communication peer, two per transport_t */
struct _ib_verbs_peer {
        transport_t *trans;
        struct ibv_qp *qp;

        int32_t recv_count;
        int32_t send_count;
        int32_t recv_size;
        int32_t send_size;

        int32_t quota;
        union {
                struct list_head     ioq;
                struct {
                        ib_verbs_ioq_t        *ioq_next;
                        ib_verbs_ioq_t        *ioq_prev;
                };
        };

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
        int32_t reused;
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
        struct ibv_srq *srq;
        ib_verbs_qpreg_t qpreg;
        struct ibv_comp_channel *send_chan, *recv_chan;
        struct ibv_cq *send_cq, *recv_cq;
        ib_verbs_queue_t sendq, recvq;
        pthread_t send_thread, recv_thread;
};
typedef struct _ib_verbs_device ib_verbs_device_t;

typedef enum {
        IB_VERBS_HANDSHAKE_START = 0,
        IB_VERBS_HANDSHAKE_SENDING_DATA,
        IB_VERBS_HANDSHAKE_RECEIVING_DATA,
        IB_VERBS_HANDSHAKE_SENT_DATA,
        IB_VERBS_HANDSHAKE_RECEIVED_DATA,
        IB_VERBS_HANDSHAKE_SENDING_ACK,
        IB_VERBS_HANDSHAKE_RECEIVING_ACK,
        IB_VERBS_HANDSHAKE_RECEIVED_ACK,
        IB_VERBS_HANDSHAKE_COMPLETE,
} ib_verbs_handshake_state_t;

struct ib_verbs_nbio {
        int state;
        char *buf;
        int count;
        struct iovec vector;
        struct iovec *pending_vector;
        int pending_count;
};


struct _ib_verbs_private {
        int32_t sock;
        int32_t idx;
        unsigned char connected;
        unsigned char tcp_connected;
        unsigned char ib_connected;
        in_addr_t addr;
        unsigned short port;

        /* IB Verbs Driver specific variables, pointers */
        ib_verbs_peer_t peer;
        ib_verbs_device_t *device;
        ib_verbs_options_t options;

        /* Used by trans->op->receive */
        char *data_ptr;
        int32_t data_offset;
        int32_t data_len;

        /* Mutex */
        pthread_mutex_t read_mutex;
        pthread_mutex_t write_mutex;
        pthread_barrier_t handshake_barrier;
        char handshake_ret;

        pthread_mutex_t recv_mutex;

        /* used during ib_verbs_handshake */
        struct {
                struct ib_verbs_nbio incoming;
                struct ib_verbs_nbio outgoing;
                int               state;
                ib_verbs_header_t header;
                char *buf;
                size_t size;
        } handshake;
};
typedef struct _ib_verbs_private ib_verbs_private_t;

#endif /* _XPORT_IB_VERBS_H */
