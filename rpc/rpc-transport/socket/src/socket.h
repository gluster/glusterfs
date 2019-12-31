/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _SOCKET_H
#define _SOCKET_H

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>
#ifdef HAVE_OPENSSL_DH_H
#include <openssl/dh.h>
#endif
#ifdef HAVE_OPENSSL_ECDH_H
#include <openssl/ecdh.h>
#endif

#include "rpc-transport.h"

#ifndef MAX_IOVEC
#define MAX_IOVEC 16
#endif /* MAX_IOVEC */

#define GF_DEFAULT_SOCKET_LISTEN_PORT GF_DEFAULT_BASE_PORT

#define RPC_MAX_FRAGMENT_SIZE 0x7fffffff

/* The default window size will be 0, indicating not to set
 * it to any size. Default size of Linux is found to be
 * performance friendly.
 * Linux allows us to over-ride the max values for the system.
 * Should we over-ride them? Because if we set a value larger than the default
 * setsockopt will fail. Having larger values might be beneficial for
 * IB links.
 */
#define GF_DEFAULT_SOCKET_WINDOW_SIZE (0)
#define GF_MAX_SOCKET_WINDOW_SIZE (1 * GF_UNIT_MB)
#define GF_MIN_SOCKET_WINDOW_SIZE (0)
#define GF_USE_DEFAULT_KEEPALIVE (-1)

#define GF_KEEPALIVE_TIME (20)
#define GF_KEEPALIVE_INTERVAL (2)
#define GF_KEEPALIVE_COUNT (9)

typedef enum {
    SP_STATE_NADA = 0,
    SP_STATE_COMPLETE,
    SP_STATE_READING_FRAGHDR,
    SP_STATE_READ_FRAGHDR,
    SP_STATE_READING_FRAG,
} sp_rpcrecord_state_t;

typedef enum {
    SP_STATE_RPCFRAG_INIT,
    SP_STATE_READING_MSGTYPE,
    SP_STATE_READ_MSGTYPE,
    SP_STATE_NOTIFYING_XID
} sp_rpcfrag_state_t;

typedef enum {
    SP_STATE_SIMPLE_MSG_INIT,
    SP_STATE_READING_SIMPLE_MSG,
} sp_rpcfrag_simple_msg_state_t;

typedef enum {
    SP_STATE_VECTORED_REQUEST_INIT,
    SP_STATE_READING_CREDBYTES,
    SP_STATE_READ_CREDBYTES, /* read credential data. */
    SP_STATE_READING_VERFBYTES,
    SP_STATE_READ_VERFBYTES, /* read verifier data */
    SP_STATE_READING_PROGHDR,
    SP_STATE_READ_PROGHDR,
    SP_STATE_READING_PROGHDR_XDATA,
    SP_STATE_READ_PROGHDR_XDATA, /* It's a bad "name" in the generic
                                    RPC state machine, but greatly
                                    aids code review (and xdata is
                                    the only "consumer" of this state)
                                 */
    SP_STATE_READING_PROG,
} sp_rpcfrag_vectored_request_state_t;

typedef enum {
    SP_STATE_REQUEST_HEADER_INIT,
    SP_STATE_READING_RPCHDR1,
    SP_STATE_READ_RPCHDR1, /* read msg from beginning till and
                            * including credlen
                            */
} sp_rpcfrag_request_header_state_t;

struct ioq {
    union {
        struct list_head list;
        struct {
            struct ioq *next;
            struct ioq *prev;
        };
    };

    struct iovec vector[MAX_IOVEC];
    struct iovec *pending_vector;
    int count;
    int pending_count;
    struct iobref *iobref;
    uint32_t fraghdr;
    char _pad[4];
};

typedef struct {
    sp_rpcfrag_request_header_state_t header_state;
    sp_rpcfrag_vectored_request_state_t vector_state;
    int vector_sizer_state;
} sp_rpcfrag_request_state_t;

typedef enum {
    SP_STATE_VECTORED_REPLY_STATUS_INIT,
    SP_STATE_READING_REPLY_STATUS,
    SP_STATE_READ_REPLY_STATUS,
} sp_rpcfrag_vectored_reply_status_state_t;

typedef enum {
    SP_STATE_ACCEPTED_SUCCESS_REPLY_INIT,
    SP_STATE_READING_PROC_HEADER,
    SP_STATE_READING_PROC_OPAQUE,
    SP_STATE_READ_PROC_OPAQUE,
    SP_STATE_READ_PROC_HEADER,
} sp_rpcfrag_vectored_reply_accepted_success_state_t;

typedef enum {
    SP_STATE_ACCEPTED_REPLY_INIT,
    SP_STATE_READING_REPLY_VERFLEN,
    SP_STATE_READ_REPLY_VERFLEN,
    SP_STATE_READING_REPLY_VERFBYTES,
    SP_STATE_READ_REPLY_VERFBYTES,
} sp_rpcfrag_vectored_reply_accepted_state_t;

typedef struct {
    uint32_t accept_status;
    sp_rpcfrag_vectored_reply_status_state_t status_state;
    sp_rpcfrag_vectored_reply_accepted_state_t accepted_state;
    sp_rpcfrag_vectored_reply_accepted_success_state_t accepted_success_state;
} sp_rpcfrag_vectored_reply_state_t;

struct gf_sock_incoming_frag {
    char *fragcurrent;
    uint32_t bytes_read;
    uint32_t remaining_size;
    struct iovec vector;
    struct iovec *pending_vector;
    union {
        sp_rpcfrag_request_state_t request;
        sp_rpcfrag_vectored_reply_state_t reply;
    } call_body;

    sp_rpcfrag_simple_msg_state_t simple_state;
    sp_rpcfrag_state_t state;
};

#define GF_SOCKET_RA_MAX 1024

struct gf_sock_incoming {
    char *proghdr_base_addr;
    struct iobuf *iobuf;
    size_t iobuf_size;
    struct gf_sock_incoming_frag frag;
    struct iovec vector[2];
    struct iovec payload_vector;
    struct iobref *iobref;
    rpc_request_info_t *request_info;
    struct iovec *pending_vector;
    int count;
    int pending_count;
    size_t total_bytes_read;

    size_t ra_read;
    size_t ra_max;
    size_t ra_served;
    char *ra_buf;
    uint32_t fraghdr;
    msg_type_t msg_type;
    sp_rpcrecord_state_t record_state;
    char _pad[4];
};

typedef struct {
    union {
        struct list_head ioq;
        struct {
            struct ioq *ioq_next;
            struct ioq *ioq_prev;
        };
    };
    pthread_mutex_t out_lock;
    pthread_mutex_t cond_lock;
    pthread_cond_t cond;
    int windowsize;
    int keepalive;
    int keepaliveidle;
    int keepaliveintvl;
    int keepalivecnt;
    int timeout;
    int log_ctr;
    int shutdown_log_ctr;
    /* ssl_error_required is used only during the SSL connection setup
     * phase.
     * It holds the error code returned by SSL_get_error() and is used to
     * arm the epoll event set for the required event for the specific fd.
     */
    int ssl_error_required;
    int ssl_session_id;

    GF_REF_DECL; /* refcount to keep track of socket_poller
                    threads */
    struct {
        pthread_mutex_t lock;
        pthread_cond_t cond;
        uint64_t in_progress;
    } notify;
    int32_t sock;
    int32_t idx;
    int32_t gen;
    uint32_t backlog;
    SSL_METHOD *ssl_meth;
    SSL_CTX *ssl_ctx;
    BIO *ssl_sbio;
    SSL *ssl_ssl;
    char *ssl_own_cert;
    char *ssl_private_key;
    char *ssl_ca_list;
    char *crl_path;
    struct gf_sock_incoming incoming;
    mgmt_ssl_t srvr_ssl;
    /* -1 = not connected. 0 = in progress. 1 = connected */
    char connected;
    /* 1 = connect failed for reasons other than EINPROGRESS/ENOENT
    see socket_connect for details */
    char connect_failed;
    char bio;
    char connect_finish_log;
    char submit_log;
    char nodelay;
    gf_boolean_t read_fail_log;
    gf_boolean_t ssl_enabled; /* outbound I/O */
    gf_boolean_t mgmt_ssl;    /* outbound mgmt */
    gf_boolean_t is_server;
    gf_boolean_t use_ssl;
    gf_boolean_t ssl_accepted;  /* To indicate SSL_accept() */
    gf_boolean_t ssl_connected; /* or SSL_connect() has been
                                 * been completed on this socket.
                                 * These are valid only when
                                 * use_ssl is true.
                                 */
    /* SSL_CTX is created for each transport. Since we are now using non-
     * blocking mechanism for SSL_accept() and SSL_connect(), the SSL
     * context is created on the first EPOLLIN event which may lead to
     * SSL_ERROR_WANT_READ/SSL_ERROR_WANT_WRITE and may not complete the
     * SSL connection at the first attempt.
     * ssl_context_created is a flag to note that we've created the SSL
     * context for the connection so that we don't blindly create any more
     * while !ssl_accepted or !ssl_connected.
     */
    gf_boolean_t ssl_context_created;
    gf_boolean_t accepted; /* explicit flag to be set in
                            * socket_event_handler() for
                            * newly accepted socket
                            */
    char _pad[4];
} socket_private_t;

#endif
