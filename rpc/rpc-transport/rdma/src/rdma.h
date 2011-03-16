/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _XPORT_RDMA_H
#define _XPORT_RDMA_H


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#ifndef MAX_IOVEC
#define MAX_IOVEC 16
#endif /* MAX_IOVEC */

#include "rpc-clnt.h"
#include "rpc-transport.h"
#include "xlator.h"
#include "event.h"
#include <stdio.h>
#include <list.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>

/* FIXME: give appropriate values to these macros */
#define GF_DEFAULT_RDMA_LISTEN_PORT (GF_DEFAULT_BASE_PORT + 1)

/* If you are changing RDMA_MAX_SEGMENTS, please make sure to update
 * GLUSTERFS_RDMA_MAX_HEADER_SIZE defined in glusterfs.h .
 */
#define RDMA_MAX_SEGMENTS           8

#define RDMA_VERSION                1
#define RDMA_POOL_SIZE              512

typedef enum rdma_errcode {
        ERR_VERS = 1,
        ERR_CHUNK = 2
}rdma_errcode_t;

struct rdma_err_vers {
        uint32_t rdma_vers_low; /* Version range supported by peer */
        uint32_t rdma_vers_high;
}__attribute__ ((packed));
typedef struct rdma_err_vers rdma_err_vers_t;

typedef enum rdma_proc {
        RDMA_MSG   = 0,           /* An RPC call or reply msg */
        RDMA_NOMSG = 1,           /* An RPC call or reply msg - separate body */
        RDMA_MSGP  = 2,           /* An RPC call or reply msg with padding */
        RDMA_DONE  = 3,           /* Client signals reply completion */
        RDMA_ERROR = 4            /* An RPC RDMA encoding error */
}rdma_proc_t;

typedef enum rdma_chunktype {
        rdma_noch = 0,         /* no chunk */
        rdma_readch,           /* some argument through rdma read */
        rdma_areadch,          /* entire request through rdma read */
        rdma_writech,          /* some result through rdma write */
        rdma_replych           /* entire reply through rdma write */
}rdma_chunktype_t;

/* If you are modifying __rdma_header, please make sure to change
 * GLUSTERFS_RDMA_MAX_HEADER_SIZE defined in glusterfs.h to reflect your changes
 */
struct __rdma_header {
        uint32_t rm_xid;    /* Mirrors the RPC header xid */
        uint32_t rm_vers;   /* Version of this protocol */
        uint32_t rm_credit; /* Buffers requested/granted */
        uint32_t rm_type;   /* Type of message (enum rdma_proc) */
        union {
                struct {                          /* no chunks */
                        uint32_t rm_empty[3];     /* 3 empty chunk lists */
                }__attribute__((packed)) rm_nochunks;

                struct {                          /* no chunks and padded */
                        uint32_t rm_align;        /* Padding alignment */
                        uint32_t rm_thresh;       /* Padding threshold */
                        uint32_t rm_pempty[3];    /* 3 empty chunk lists */
                }__attribute__((packed)) rm_padded;

                struct {
                        uint32_t rm_type;
                        rdma_err_vers_t rm_version;
                }__attribute__ ((packed)) rm_error;

                uint32_t rm_chunks[0];    /* read, write and reply chunks */
        }__attribute__ ((packed)) rm_body;
} __attribute__((packed));
typedef struct __rdma_header rdma_header_t;

/* If you are modifying __rdma_segment or __rdma_read_chunk, please make sure
 * to change GLUSTERFS_RDMA_MAX_HEADER_SIZE defined in glusterfs.h to reflect
 * your changes.
 */
struct __rdma_segment {
        uint32_t rs_handle;       /* Registered memory handle */
        uint32_t rs_length;       /* Length of the chunk in bytes */
        uint64_t rs_offset;       /* Chunk virtual address or offset */
} __attribute__((packed));
typedef struct __rdma_segment rdma_segment_t;

/* read chunk(s), encoded as a linked list. */
struct __rdma_read_chunk {
        uint32_t       rc_discrim;      /* 1 indicates presence */
        uint32_t       rc_position;     /* Position in XDR stream */
        rdma_segment_t rc_target;
} __attribute__((packed));
typedef struct __rdma_read_chunk rdma_read_chunk_t;

/* write chunk, and reply chunk. */
struct __rdma_write_chunk {
        rdma_segment_t wc_target;
} __attribute__((packed));
typedef struct __rdma_write_chunk rdma_write_chunk_t;

/* write chunk(s), encoded as a counted array. */
struct __rdma_write_array {
        uint32_t wc_discrim;      /* 1 indicates presence */
        uint32_t wc_nchunks;      /* Array count */
        struct __rdma_write_chunk wc_array[0];
} __attribute__((packed));
typedef struct __rdma_write_array rdma_write_array_t;

/* options per transport end point */
struct __rdma_options {
        int32_t port;
        char *device_name;
        enum ibv_mtu mtu;
        int32_t  send_count;
        int32_t  recv_count;
        uint64_t recv_size;
        uint64_t send_size;
};
typedef struct __rdma_options rdma_options_t;

struct __rdma_reply_info {
        uint32_t            rm_xid;      /* xid in network endian */
        rdma_chunktype_t    type;        /*
                                          * can be either rdma_replych
                                          * or rdma_writech.
                                          */
        rdma_write_array_t *wc_array;
        struct mem_pool    *pool;
};
typedef struct __rdma_reply_info rdma_reply_info_t;

struct __rdma_ioq {
        union {
                struct list_head list;
                struct {
                        struct __rdma_ioq    *next;
                        struct __rdma_ioq    *prev;
                };
        };

        char               is_request;
        struct iovec       rpchdr[MAX_IOVEC];
        int                rpchdr_count;
        struct iovec       proghdr[MAX_IOVEC];
        int                proghdr_count;
        struct iovec       prog_payload[MAX_IOVEC];
        int                prog_payload_count;

        struct iobref     *iobref;

        union {
                struct __rdma_ioq_request {
                        /* used to build reply_chunk for RDMA_NOMSG type msgs */
                        struct iovec rsphdr_vec[MAX_IOVEC];
                        int          rsphdr_count;

                        /*
                         * used to build write_array during operations like
                         * read.
                         */
                        struct iovec rsp_payload[MAX_IOVEC];
                        int          rsp_payload_count;

                        struct rpc_req *rpc_req; /* FIXME: hack! hack! should be
                                                  * cleaned up later
                                                  */
                        struct iobref  *rsp_iobref;
                }request;

                rdma_reply_info_t  *reply_info;
        }msg;

        struct mem_pool *pool;
};
typedef struct __rdma_ioq rdma_ioq_t;

typedef enum __rdma_send_post_type {
        RDMA_SEND_POST_NO_CHUNKLIST,    /* post which is sent using rdma-send
                                         * and the msg carries no
                                         * chunklists.
                                         */
        RDMA_SEND_POST_READ_CHUNKLIST,  /* post which is sent using rdma-send
                                         * and the msg carries only read
                                         * chunklist.
                                         */
        RDMA_SEND_POST_WRITE_CHUNKLIST, /* post which is sent using
                                         * rdma-send and the msg carries
                                         * only write chunklist.
                                         */
        RDMA_SEND_POST_READ_WRITE_CHUNKLIST, /* post which is sent using
                                              * rdma-send and the msg
                                              * carries both read and
                                              * write chunklists.
                                              */
        RDMA_SEND_POST_RDMA_READ,              /* RDMA read */
        RDMA_SEND_POST_RDMA_WRITE,             /* RDMA write */
}rdma_send_post_type_t;

/* represents one communication peer, two per transport_t */
struct __rdma_peer {
        rpc_transport_t *trans;
        struct ibv_qp *qp;

        int32_t recv_count;
        int32_t send_count;
        int32_t recv_size;
        int32_t send_size;

        int32_t quota;
        union {
                struct list_head     ioq;
                struct {
                        rdma_ioq_t        *ioq_next;
                        rdma_ioq_t        *ioq_prev;
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
typedef struct __rdma_peer rdma_peer_t;

struct __rdma_post_context {
        struct ibv_mr     *mr[RDMA_MAX_SEGMENTS];
        int                mr_count;
        struct iovec       vector[MAX_IOVEC];
        int                count;
        struct iobref     *iobref;
        struct iobuf      *hdr_iobuf;
        char               is_request;
        int                rdma_reads;
        rdma_reply_info_t *reply_info;
};
typedef struct __rdma_post_context rdma_post_context_t;

typedef enum {
        RDMA_SEND_POST,
        RDMA_RECV_POST
} rdma_post_type_t;

struct __rdma_post {
        struct __rdma_post *next, *prev;
        struct ibv_mr *mr;
        char *buf;
        int32_t buf_size;
        char aux;
        int32_t reused;
        struct __rdma_device  *device;
        rdma_post_type_t      type;
        rdma_post_context_t   ctx;
        int                   refcount;
        pthread_mutex_t       lock;
};
typedef struct __rdma_post rdma_post_t;

struct __rdma_queue {
        rdma_post_t active_posts, passive_posts;
        int32_t active_count, passive_count;
        pthread_mutex_t lock;
};
typedef struct __rdma_queue rdma_queue_t;

struct __rdma_qpreg {
        pthread_mutex_t lock;
        int32_t count;
        struct _qpent {
                struct _qpent *next, *prev;
                int32_t qp_num;
                rdma_peer_t *peer;
        } ents[42];
};
typedef struct __rdma_qpreg rdma_qpreg_t;

/* context per device, stored in global glusterfs_ctx_t->ib */
struct __rdma_device {
        struct __rdma_device *next;
        const char *device_name;
        struct ibv_context *context;
        int32_t port;
        struct ibv_pd *pd;
        struct ibv_srq *srq;
        rdma_qpreg_t qpreg;
        struct ibv_comp_channel *send_chan, *recv_chan;
        struct ibv_cq *send_cq, *recv_cq;
        rdma_queue_t sendq, recvq;
        pthread_t send_thread, recv_thread;
        struct mem_pool *request_ctx_pool;
        struct mem_pool *ioq_pool;
        struct mem_pool *reply_info_pool;
};
typedef struct __rdma_device rdma_device_t;

typedef enum {
        RDMA_HANDSHAKE_START = 0,
        RDMA_HANDSHAKE_SENDING_DATA,
        RDMA_HANDSHAKE_RECEIVING_DATA,
        RDMA_HANDSHAKE_SENT_DATA,
        RDMA_HANDSHAKE_RECEIVED_DATA,
        RDMA_HANDSHAKE_SENDING_ACK,
        RDMA_HANDSHAKE_RECEIVING_ACK,
        RDMA_HANDSHAKE_RECEIVED_ACK,
        RDMA_HANDSHAKE_COMPLETE,
} rdma_handshake_state_t;

struct rdma_nbio {
        int state;
        char *buf;
        int count;
        struct iovec vector;
        struct iovec *pending_vector;
        int pending_count;
};

struct __rdma_request_context {
        struct ibv_mr   *mr[RDMA_MAX_SEGMENTS];
        int              mr_count;
        struct mem_pool *pool;
        rdma_peer_t     *peer;
        struct iobref   *iobref;
        struct iobref   *rsp_iobref;
};
typedef struct __rdma_request_context rdma_request_context_t;

struct __rdma_private {
        int32_t sock;
        int32_t idx;
        unsigned char connected;
        unsigned char tcp_connected;
        unsigned char ib_connected;
        in_addr_t addr;
        unsigned short port;

        /* IB Verbs Driver specific variables, pointers */
        rdma_peer_t peer;
        struct __rdma_device *device;
        rdma_options_t options;

        /* Used by trans->op->receive */
        char *data_ptr;
        int32_t data_offset;
        int32_t data_len;

        /* Mutex */
        pthread_mutex_t read_mutex;
        pthread_mutex_t write_mutex;
        pthread_barrier_t handshake_barrier;
        char handshake_ret;
        char is_server;
        rpc_transport_t *listener;

        pthread_mutex_t recv_mutex;
        pthread_cond_t  recv_cond;

        /* used during rdma_handshake */
        struct {
                struct rdma_nbio incoming;
                struct rdma_nbio outgoing;
                int               state;
                rdma_header_t header;
                char *buf;
                size_t size;
        } handshake;
};
typedef struct __rdma_private rdma_private_t;

#endif /* _XPORT_RDMA_H */
