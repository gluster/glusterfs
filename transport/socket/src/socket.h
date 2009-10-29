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

#ifndef _SOCKET_H
#define _SOCKET_H


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "event.h"
#include "transport.h"
#include "logging.h"
#include "dict.h"
#include "mem-pool.h"

#ifndef MAX_IOVEC
#define MAX_IOVEC 16
#endif /* MAX_IOVEC */

#define GF_DEFAULT_SOCKET_LISTEN_PORT 6996

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

typedef enum {
        SOCKET_PROTO_STATE_NADA = 0,
        SOCKET_PROTO_STATE_HEADER_COMING,
        SOCKET_PROTO_STATE_HEADER_CAME,
        SOCKET_PROTO_STATE_DATA_COMING,
        SOCKET_PROTO_STATE_DATA_CAME,
        SOCKET_PROTO_STATE_COMPLETE,
} socket_proto_state_t;

struct socket_header {
        char     colonO[3];
        uint32_t size1;
        uint32_t size2;
        char     version;
} __attribute__((packed));


struct ioq {
        union {
                struct list_head list;
                struct {
                        struct ioq    *next;
                        struct ioq    *prev;
                };
        };
        struct socket_header  header;
        struct iovec       vector[MAX_IOVEC];
        int                count;
        struct iovec      *pending_vector;
        int                pending_count;
        char              *buf;
        struct iobref     *iobref;
};


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
                int                  state;
                struct socket_header header;
                char                *hdr_p;
                size_t               hdrlen;
                struct iobuf        *iobuf;
                char                *buf_p;
                size_t               buflen;
                struct iovec         vector[2];
                int                  count;
                struct iovec        *pending_vector;
                int                  pending_count;
        } incoming;
        pthread_mutex_t        lock;
        int                    windowsize;
        char                   lowlat;
        char                   nodelay;
} socket_private_t;


#endif
