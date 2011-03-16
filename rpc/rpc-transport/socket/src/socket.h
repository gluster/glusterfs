/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _SOCKET_H
#define _SOCKET_H


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "event.h"
#include "rpc-transport.h"
#include "logging.h"
#include "dict.h"
#include "mem-pool.h"
#include "globals.h"

#ifndef MAX_IOVEC
#define MAX_IOVEC 16
#endif /* MAX_IOVEC */

#define GF_DEFAULT_SOCKET_LISTEN_PORT  GF_DEFAULT_BASE_PORT

#define RPC_MAX_FRAGMENT_SIZE 0x7fffffff

/* This is the size set through setsockopt for
 * both the TCP receive window size and the
 * send buffer size.
 * Till the time iobuf size becomes configurable, this size is set to include
 * two iobufs + the GlusterFS protocol headers.
 * Linux allows us to over-ride the max values for the system.
 * Should we over-ride them? Because if we set a value larger than the default
 * setsockopt will fail. Having larger values might be beneficial for
 * IB links.
 */
#define GF_DEFAULT_SOCKET_WINDOW_SIZE   (512 * GF_UNIT_KB)
#define GF_MAX_SOCKET_WINDOW_SIZE       (1 * GF_UNIT_MB)
#define GF_MIN_SOCKET_WINDOW_SIZE       (128 * GF_UNIT_KB)
#define GF_USE_DEFAULT_KEEPALIVE        (-1)

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
} sp_rpcfrag_state_t;

typedef enum {
        SP_STATE_SIMPLE_MSG_INIT,
        SP_STATE_READING_SIMPLE_MSG,
} sp_rpcfrag_simple_msg_state_t;

typedef enum {
        SP_STATE_VECTORED_REQUEST_INIT,
        SP_STATE_READING_CREDBYTES,
        SP_STATE_READ_CREDBYTES,        /* read credential data. */
        SP_STATE_READING_VERFBYTES,
        SP_STATE_READ_VERFBYTES,        /* read verifier data */
        SP_STATE_READING_PROG,
} sp_rpcfrag_vectored_request_state_t;

typedef enum {
        SP_STATE_REQUEST_HEADER_INIT,
        SP_STATE_READING_RPCHDR1,
        SP_STATE_READ_RPCHDR1,     /* read msg from beginning till and
                                    * including credlen
                                    */
} sp_rpcfrag_request_header_state_t;

struct ioq {
        union {
                struct list_head list;
                struct {
                        struct ioq    *next;
                        struct ioq    *prev;
                };
        };

        uint32_t           fraghdr;
        struct iovec       vector[MAX_IOVEC];
        int                count;
        struct iovec      *pending_vector;
        int                pending_count;
        struct iobref     *iobref;
};

typedef struct {
        sp_rpcfrag_request_header_state_t header_state;
        sp_rpcfrag_vectored_request_state_t vector_state;
} sp_rpcfrag_request_state_t;

typedef enum {
        SP_STATE_VECTORED_REPLY_STATUS_INIT,
        SP_STATE_READING_REPLY_STATUS,
        SP_STATE_READ_REPLY_STATUS,
} sp_rpcfrag_vectored_reply_status_state_t;

typedef enum {
        SP_STATE_ACCEPTED_SUCCESS_REPLY_INIT,
        SP_STATE_READING_PROC_HEADER,
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

typedef struct {
        int32_t                sock;
        int32_t                idx;
        unsigned char          connected; // -1 = not connected. 0 = in progress. 1 = connected
        char                   bio;
        char                   connect_finish_log;
        char                   submit_log;
        union {
                struct list_head     ioq;
                struct {
                        struct ioq        *ioq_next;
                        struct ioq        *ioq_prev;
                };
        };
        struct {
                sp_rpcrecord_state_t  record_state;
                struct {
                        char         *fragcurrent;
                        uint32_t      bytes_read;
                        uint32_t      remaining_size;
                        struct iovec  vector;
                        struct iovec *pending_vector;
                        union {
                                sp_rpcfrag_request_state_t        request;
                                sp_rpcfrag_vectored_reply_state_t reply;
                        } call_body;

                        sp_rpcfrag_simple_msg_state_t     simple_state;
                        sp_rpcfrag_state_t state;
                } frag;
                struct iobuf        *iobuf;
                size_t               iobuf_size;
                struct iovec         vector[2];
                int                  count;
                struct iovec         payload_vector;
                struct iobref       *iobref;
                rpc_request_info_t  *request_info;
                struct iovec        *pending_vector;
                int                  pending_count;
                uint32_t             fraghdr;
                char                 complete_record;
                msg_type_t           msg_type;
                size_t               total_bytes_read;
        } incoming;
        pthread_mutex_t        lock;
        int                    windowsize;
        char                   lowlat;
        char                   nodelay;
        int                    keepalive;
        int                    keepaliveidle;
        int                    keepaliveintvl;
} socket_private_t;


#endif
