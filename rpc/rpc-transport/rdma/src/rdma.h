/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _XPORT_RDMA_H
#define _XPORT_RDMA_H


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
#include <rdma/rdma_cma.h>

/* FIXME: give appropriate values to these macros */
#define GF_DEFAULT_RDMA_LISTEN_PORT (GF_DEFAULT_BASE_PORT + 1)


/* If you are changing GF_RDMA_MAX_SEGMENTS, please make sure to update
 * GLUSTERFS_GF_RDMA_MAX_HEADER_SIZE defined in glusterfs.h .
 */
#define GF_RDMA_MAX_SEGMENTS           8

#define GF_RDMA_VERSION                1
#define GF_RDMA_POOL_SIZE              512

/* Additional attributes */
#define GF_RDMA_TIMEOUT                14
#define GF_RDMA_RETRY_CNT              7
#define GF_RDMA_RNR_RETRY              7

typedef enum gf_rdma_errcode {
        ERR_VERS = 1,
        ERR_CHUNK = 2
}gf_rdma_errcode_t;

struct gf_rdma_err_vers {
        uint32_t gf_rdma_vers_low; /* Version range supported by peer */
        uint32_t gf_rdma_vers_high;
}__attribute__ ((packed));
typedef struct gf_rdma_err_vers gf_rdma_err_vers_t;

typedef enum gf_rdma_proc {
        GF_RDMA_MSG   = 0,           /* An RPC call or reply msg */
        GF_RDMA_NOMSG = 1,           /* An RPC call or reply msg - separate body */
        GF_RDMA_MSGP  = 2,           /* An RPC call or reply msg with padding */
        GF_RDMA_DONE  = 3,           /* Client signals reply completion */
        GF_RDMA_ERROR = 4            /* An RPC RDMA encoding error */
}gf_rdma_proc_t;

typedef enum gf_rdma_chunktype {
        gf_rdma_noch = 0,         /* no chunk */
        gf_rdma_readch,           /* some argument through rdma read */
        gf_rdma_areadch,          /* entire request through rdma read */
        gf_rdma_writech,          /* some result through rdma write */
        gf_rdma_replych           /* entire reply through rdma write */
}gf_rdma_chunktype_t;

/* If you are modifying __gf_rdma_header, please make sure to change
 * GLUSTERFS_GF_RDMA_MAX_HEADER_SIZE defined in glusterfs.h to reflect your changes
 */
struct __gf_rdma_header {
        uint32_t rm_xid;    /* Mirrors the RPC header xid */
        uint32_t rm_vers;   /* Version of this protocol */
        uint32_t rm_credit; /* Buffers requested/granted */
        uint32_t rm_type;   /* Type of message (enum gf_rdma_proc) */
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
                        gf_rdma_err_vers_t rm_version;
                }__attribute__ ((packed)) rm_error;

                uint32_t rm_chunks[0];    /* read, write and reply chunks */
        }__attribute__ ((packed)) rm_body;
} __attribute__((packed));
typedef struct __gf_rdma_header gf_rdma_header_t;

/* If you are modifying __gf_rdma_segment or __gf_rdma_read_chunk, please make sure
 * to change GLUSTERFS_GF_RDMA_MAX_HEADER_SIZE defined in glusterfs.h to reflect
 * your changes.
 */
struct __gf_rdma_segment {
        uint32_t rs_handle;       /* Registered memory handle */
        uint32_t rs_length;       /* Length of the chunk in bytes */
        uint64_t rs_offset;       /* Chunk virtual address or offset */
} __attribute__((packed));
typedef struct __gf_rdma_segment gf_rdma_segment_t;

/* read chunk(s), encoded as a linked list. */
struct __gf_rdma_read_chunk {
        uint32_t       rc_discrim;      /* 1 indicates presence */
        uint32_t       rc_position;     /* Position in XDR stream */
        gf_rdma_segment_t rc_target;
} __attribute__((packed));
typedef struct __gf_rdma_read_chunk gf_rdma_read_chunk_t;

/* write chunk, and reply chunk. */
struct __gf_rdma_write_chunk {
        gf_rdma_segment_t wc_target;
} __attribute__((packed));
typedef struct __gf_rdma_write_chunk gf_rdma_write_chunk_t;

/* write chunk(s), encoded as a counted array. */
struct __gf_rdma_write_array {
        uint32_t wc_discrim;      /* 1 indicates presence */
        uint32_t wc_nchunks;      /* Array count */
        struct __gf_rdma_write_chunk wc_array[0];
} __attribute__((packed));
typedef struct __gf_rdma_write_array gf_rdma_write_array_t;

/* options per transport end point */
struct __gf_rdma_options {
        int32_t port;
        char *device_name;
        enum ibv_mtu mtu;
        int32_t  send_count;
        int32_t  recv_count;
        uint64_t recv_size;
        uint64_t send_size;
	uint8_t  attr_timeout;
	uint8_t  attr_retry_cnt;
	uint8_t  attr_rnr_retry;
};
typedef struct __gf_rdma_options gf_rdma_options_t;

struct __gf_rdma_reply_info {
        uint32_t            rm_xid;      /* xid in network endian */
        gf_rdma_chunktype_t    type;        /*
                                          * can be either gf_rdma_replych
                                          * or gf_rdma_writech.
                                          */
        gf_rdma_write_array_t *wc_array;
        struct mem_pool    *pool;
};
typedef struct __gf_rdma_reply_info gf_rdma_reply_info_t;

struct __gf_rdma_ioq {
        union {
                struct list_head list;
                struct {
                        struct __gf_rdma_ioq    *next;
                        struct __gf_rdma_ioq    *prev;
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
                struct __gf_rdma_ioq_request {
                        /* used to build reply_chunk for GF_RDMA_NOMSG type msgs */
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

                gf_rdma_reply_info_t  *reply_info;
        }msg;

        struct mem_pool *pool;
};
typedef struct __gf_rdma_ioq gf_rdma_ioq_t;

typedef enum __gf_rdma_send_post_type {
        GF_RDMA_SEND_POST_NO_CHUNKLIST,    /* post which is sent using rdma-send
                                         * and the msg carries no
                                         * chunklists.
                                         */
        GF_RDMA_SEND_POST_READ_CHUNKLIST,  /* post which is sent using rdma-send
                                         * and the msg carries only read
                                         * chunklist.
                                         */
        GF_RDMA_SEND_POST_WRITE_CHUNKLIST, /* post which is sent using
                                         * rdma-send and the msg carries
                                         * only write chunklist.
                                         */
        GF_RDMA_SEND_POST_READ_WRITE_CHUNKLIST, /* post which is sent using
                                              * rdma-send and the msg
                                              * carries both read and
                                              * write chunklists.
                                              */
        GF_RDMA_SEND_POST_GF_RDMA_READ,              /* RDMA read */
        GF_RDMA_SEND_POST_GF_RDMA_WRITE,             /* RDMA write */
}gf_rdma_send_post_type_t;

/* represents one communication peer, two per transport_t */
struct __gf_rdma_peer {
        rpc_transport_t   *trans;
        struct rdma_cm_id *cm_id;
        struct ibv_qp     *qp;
        pthread_t          rdma_event_thread;
        char               quota_set;

        int32_t recv_count;
        int32_t send_count;
        int32_t recv_size;
        int32_t send_size;

        int32_t                        quota;
        union {
                struct list_head       ioq;
                struct {
                        gf_rdma_ioq_t *ioq_next;
                        gf_rdma_ioq_t *ioq_prev;
                };
        };

        /* QP attributes, needed to connect with remote QP */
        int32_t               local_lid;
        int32_t               local_psn;
        int32_t               local_qpn;
        int32_t               remote_lid;
        int32_t               remote_psn;
        int32_t               remote_qpn;
};
typedef struct __gf_rdma_peer gf_rdma_peer_t;

struct __gf_rdma_post_context {
        struct ibv_mr     *mr[GF_RDMA_MAX_SEGMENTS];
        int                mr_count;
        struct iovec       vector[MAX_IOVEC];
        int                count;
        struct iobref     *iobref;
        struct iobuf      *hdr_iobuf;
        char               is_request;
        int                gf_rdma_reads;
        gf_rdma_reply_info_t *reply_info;
};
typedef struct __gf_rdma_post_context gf_rdma_post_context_t;

typedef enum {
        GF_RDMA_SEND_POST,
        GF_RDMA_RECV_POST
} gf_rdma_post_type_t;

struct __gf_rdma_post {
        struct __gf_rdma_post *next, *prev;
        struct ibv_mr *mr;
        char *buf;
        int32_t buf_size;
        char aux;
        int32_t reused;
        struct __gf_rdma_device  *device;
        gf_rdma_post_type_t      type;
        gf_rdma_post_context_t   ctx;
        int                   refcount;
        pthread_mutex_t       lock;
};
typedef struct __gf_rdma_post gf_rdma_post_t;

struct __gf_rdma_queue {
        gf_rdma_post_t active_posts, passive_posts;
        int32_t active_count, passive_count;
        pthread_mutex_t lock;
};
typedef struct __gf_rdma_queue gf_rdma_queue_t;

struct __gf_rdma_qpreg {
        pthread_mutex_t lock;
        int32_t count;
        struct _qpent {
                struct _qpent *next, *prev;
                int32_t qp_num;
                gf_rdma_peer_t *peer;
        } ents[42];
};
typedef struct __gf_rdma_qpreg gf_rdma_qpreg_t;

/* context per device, stored in global glusterfs_ctx_t->ib */
struct __gf_rdma_device {
        struct __gf_rdma_device *next;
        const char *device_name;
        struct ibv_context *context;
        int32_t port;
        struct ibv_pd *pd;
        struct ibv_srq *srq;
        gf_rdma_qpreg_t qpreg;
        struct ibv_comp_channel *send_chan, *recv_chan;
        struct ibv_cq *send_cq, *recv_cq;
        gf_rdma_queue_t sendq, recvq;
        pthread_t send_thread, recv_thread, async_event_thread;
        struct mem_pool *request_ctx_pool;
        struct mem_pool *ioq_pool;
        struct mem_pool *reply_info_pool;
        struct list_head all_mr;
};
typedef struct __gf_rdma_device gf_rdma_device_t;


struct __gf_rdma_arena_mr {
        struct list_head list;
        struct iobuf_arena *iobuf_arena;
        struct ibv_mr *mr;
};

typedef struct __gf_rdma_arena_mr gf_rdma_arena_mr;
struct __gf_rdma_ctx {
        gf_rdma_device_t          *device;
        struct rdma_event_channel *rdma_cm_event_channel;
        pthread_t                  rdma_cm_thread;
        pthread_mutex_t            lock;
        int32_t                    dlcount;
};
typedef struct __gf_rdma_ctx gf_rdma_ctx_t;

struct __gf_rdma_request_context {
        struct ibv_mr   *mr[GF_RDMA_MAX_SEGMENTS];
        int              mr_count;
        struct mem_pool *pool;
        gf_rdma_peer_t     *peer;
        struct iobref   *iobref;
        struct iobref   *rsp_iobref;
};
typedef struct __gf_rdma_request_context gf_rdma_request_context_t;

typedef enum {
        GF_RDMA_SERVER_LISTENER,
        GF_RDMA_SERVER,
        GF_RDMA_CLIENT,
} gf_rdma_transport_entity_t;

struct __gf_rdma_private {
        int32_t        idx;
        unsigned char  connected;
        in_addr_t      addr;
        unsigned short port;

        /* IB Verbs Driver specific variables, pointers */
        gf_rdma_peer_t           peer;
        struct __gf_rdma_device *device;
        gf_rdma_options_t        options;

        /* Used by trans->op->receive */
        char    *data_ptr;
        int32_t  data_offset;
        int32_t  data_len;

        /* Mutex */
        pthread_mutex_t             write_mutex;
        rpc_transport_t            *listener;
        pthread_mutex_t             recv_mutex;
        pthread_cond_t              recv_cond;
        gf_rdma_transport_entity_t  entity;
};
typedef struct __gf_rdma_private    gf_rdma_private_t;

#endif /* _XPORT_GF_RDMA_H */
