/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "socket.h"
#include "name.h"
#include "dict.h"
#include "rpc-transport.h"
#include "logging.h"
#include "xlator.h"
#include "byte-order.h"
#include "common-utils.h"
#include "compat-errno.h"


/* ugly #includes below */
#include "protocol-common.h"
#include "glusterfs3-xdr.h"
#include "xdr-nfs3.h"
#include "rpcsvc.h"

#include <fcntl.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <rpc/xdr.h>
#define GF_LOG_ERRNO(errno) ((errno == ENOTCONN) ? GF_LOG_DEBUG : GF_LOG_ERROR)
#define SA(ptr) ((struct sockaddr *)ptr)


#define __socket_proto_reset_pending(priv) do {                 \
                memset (&priv->incoming.frag.vector, 0,         \
                        sizeof (priv->incoming.frag.vector));   \
                priv->incoming.frag.pending_vector =            \
                        &priv->incoming.frag.vector;            \
                priv->incoming.frag.pending_vector->iov_base =  \
                        priv->incoming.frag.fragcurrent;        \
                priv->incoming.pending_vector =                 \
                        priv->incoming.frag.pending_vector;     \
        } while (0);


#define __socket_proto_update_pending(priv)                             \
        do {                                                            \
                uint32_t remaining_fragsize = 0;                        \
                if (priv->incoming.frag.pending_vector->iov_len == 0) { \
                        remaining_fragsize = RPC_FRAGSIZE (priv->incoming.fraghdr) \
                                - priv->incoming.frag.bytes_read;       \
                                                                        \
                        priv->incoming.frag.pending_vector->iov_len =   \
                                remaining_fragsize > priv->incoming.frag.remaining_size \
                                ? priv->incoming.frag.remaining_size : remaining_fragsize; \
                                                                        \
                        priv->incoming.frag.remaining_size -=           \
                                priv->incoming.frag.pending_vector->iov_len; \
                }                                                       \
        } while (0);

#define __socket_proto_update_priv_after_read(priv, ret, bytes_read)    \
        {                                                               \
                priv->incoming.frag.fragcurrent += bytes_read;          \
                priv->incoming.frag.bytes_read += bytes_read;           \
                                                                        \
                if ((ret > 0) || (priv->incoming.frag.remaining_size != 0)) { \
                        if (priv->incoming.frag.remaining_size != 0 && ret == 0) {  \
                                __socket_proto_reset_pending (priv);    \
                        }                                               \
                                                                        \
                        gf_log (this->name, GF_LOG_TRACE, "partial read on non-blocking socket"); \
                                                                        \
                        break;                                          \
                }                                                       \
        }

#define __socket_proto_init_pending(priv, size)                         \
        do {                                                            \
                uint32_t remaining_fragsize = 0;                        \
                remaining_fragsize = RPC_FRAGSIZE (priv->incoming.fraghdr) \
                        - priv->incoming.frag.bytes_read;               \
                                                                        \
                __socket_proto_reset_pending (priv);                    \
                                                                        \
                priv->incoming.frag.pending_vector->iov_len =           \
                        remaining_fragsize > size ? size : remaining_fragsize; \
                                                                        \
                priv->incoming.frag.remaining_size =                    \
                        size - priv->incoming.frag.pending_vector->iov_len; \
                                                                        \
        } while (0);


/* This will be used in a switch case and breaks from the switch case if all
 * the pending data is not read.
 */
#define __socket_proto_read(priv, ret)                                  \
        {                                                               \
                size_t bytes_read = 0;                                  \
                                                                        \
                __socket_proto_update_pending (priv);                   \
                                                                        \
                ret = __socket_readv (this,                             \
                                      priv->incoming.pending_vector, 1, \
                                      &priv->incoming.pending_vector,   \
                                      &priv->incoming.pending_count,    \
                                      &bytes_read);                     \
                if (ret == -1) {                                        \
                        gf_log (this->name, GF_LOG_WARNING,             \
                                "reading from socket failed. Error (%s), " \
                                "peer (%s)", strerror (errno),          \
                                this->peerinfo.identifier);             \
                        break;                                          \
                }                                                       \
                __socket_proto_update_priv_after_read (priv, ret, bytes_read); \
        }


int socket_init (rpc_transport_t *this);

/*
 * return value:
 *   0 = success (completed)
 *  -1 = error
 * > 0 = incomplete
 */

int
__socket_rwv (rpc_transport_t *this, struct iovec *vector, int count,
              struct iovec **pending_vector, int *pending_count, size_t *bytes,
              int write)
{
        socket_private_t *priv = NULL;
        int               sock = -1;
        int               ret = -1;
        struct iovec     *opvector = NULL;
        int               opcount = 0;
        int               moved = 0;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;
        sock = priv->sock;

        opvector = vector;
        opcount  = count;

        if (bytes != NULL) {
                *bytes = 0;
        }

        while (opcount) {
                if (write) {
                        ret = writev (sock, opvector, opcount);

                        if (ret == 0 || (ret == -1 && errno == EAGAIN)) {
                                /* done for now */
                                break;
                        }
                        this->total_bytes_write += ret;
                } else {
                        ret = readv (sock, opvector, opcount);
                        if (ret == -1 && errno == EAGAIN) {
                                /* done for now */
                                break;
                        }
                        this->total_bytes_read += ret;
                }

                if (ret == 0) {
                        /* Mostly due to 'umount' in client */

                        gf_log (this->name, GF_LOG_DEBUG,
                                "EOF from peer %s", this->peerinfo.identifier);
                        opcount = -1;
                        errno = ENOTCONN;
                        break;
                }
                if (ret == -1) {
                        if (errno == EINTR)
                                continue;

                        gf_log (this->name, GF_LOG_WARNING,
                                "%s failed (%s)", write ? "writev" : "readv",
                                strerror (errno));
                        opcount = -1;
                        break;
                }

                if (bytes != NULL) {
                        *bytes += ret;
                }

                moved = 0;

                while (moved < ret) {
                        if ((ret - moved) >= opvector[0].iov_len) {
                                moved += opvector[0].iov_len;
                                opvector++;
                                opcount--;
                        } else {
                                opvector[0].iov_len -= (ret - moved);
                                opvector[0].iov_base += (ret - moved);
                                moved += (ret - moved);
                        }
                        while (opcount && !opvector[0].iov_len) {
                                opvector++;
                                opcount--;
                        }
                }
        }

        if (pending_vector)
                *pending_vector = opvector;

        if (pending_count)
                *pending_count = opcount;

out:
        return opcount;
}


int
__socket_readv (rpc_transport_t *this, struct iovec *vector, int count,
                struct iovec **pending_vector, int *pending_count,
                size_t *bytes)
{
        int ret = -1;

        ret = __socket_rwv (this, vector, count,
                            pending_vector, pending_count, bytes, 0);

        return ret;
}


int
__socket_writev (rpc_transport_t *this, struct iovec *vector, int count,
                 struct iovec **pending_vector, int *pending_count)
{
        int ret = -1;

        ret = __socket_rwv (this, vector, count,
                            pending_vector, pending_count, NULL, 1);

        return ret;
}


int
__socket_disconnect (rpc_transport_t *this)
{
        socket_private_t *priv = NULL;
        int               ret = -1;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        if (priv->sock != -1) {
                priv->connected = -1;
                ret = shutdown (priv->sock, SHUT_RDWR);
                if (ret) {
                        /* its already disconnected.. no need to understand
                           why it failed to shutdown in normal cases */
                        gf_log (this->name, GF_LOG_DEBUG,
                                "shutdown() returned %d. %s",
                                ret, strerror (errno));
                }
        }

out:
        return ret;
}


int
__socket_server_bind (rpc_transport_t *this)
{
        socket_private_t *priv = NULL;
        int               ret = -1;
        int               opt = 1;
        int               reuse_check_sock = -1;
        struct sockaddr_storage   unix_addr = {0};

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        ret = setsockopt (priv->sock, SOL_SOCKET, SO_REUSEADDR,
                          &opt, sizeof (opt));

        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "setsockopt() for SO_REUSEADDR failed (%s)",
                        strerror (errno));
        }

        //reuse-address doesnt work for unix type sockets
        if (AF_UNIX == SA (&this->myinfo.sockaddr)->sa_family) {
                memcpy (&unix_addr, SA (&this->myinfo.sockaddr),
                        this->myinfo.sockaddr_len);
                reuse_check_sock = socket (AF_UNIX, SOCK_STREAM, 0);
                if (reuse_check_sock > 0) {
                        ret = connect (reuse_check_sock, SA (&unix_addr),
                                       this->myinfo.sockaddr_len);
                        if ((ret == -1) && (ECONNREFUSED == errno)) {
                                unlink (((struct sockaddr_un*)&unix_addr)->sun_path);
                        }
                        close (reuse_check_sock);
                }
        }

        ret = bind (priv->sock, (struct sockaddr *)&this->myinfo.sockaddr,
                    this->myinfo.sockaddr_len);

        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "binding to %s failed: %s",
                        this->myinfo.identifier, strerror (errno));
                if (errno == EADDRINUSE) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Port is already in use");
                }
        }

out:
        return ret;
}


int
__socket_nonblock (int fd)
{
        int flags = 0;
        int ret = -1;

        flags = fcntl (fd, F_GETFL);

        if (flags != -1)
                ret = fcntl (fd, F_SETFL, flags | O_NONBLOCK);

        return ret;
}


int
__socket_nodelay (int fd)
{
        int     on = 1;
        int     ret = -1;

        ret = setsockopt (fd, IPPROTO_TCP, TCP_NODELAY,
                          &on, sizeof (on));
        if (!ret)
                gf_log (THIS->name, GF_LOG_TRACE,
                        "NODELAY enabled for socket %d", fd);

        return ret;
}


static int
__socket_keepalive (int fd, int keepalive_intvl, int keepalive_idle)
{
        int     on = 1;
        int     ret = -1;

        ret = setsockopt (fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof (on));
        if (ret == -1) {
                gf_log ("socket", GF_LOG_WARNING,
                        "failed to set keep alive option on socket %d", fd);
                goto err;
        }

        if (keepalive_intvl == GF_USE_DEFAULT_KEEPALIVE)
                goto done;

#if !defined(GF_LINUX_HOST_OS) && !defined(__NetBSD__)
#ifdef GF_SOLARIS_HOST_OS
        ret = setsockopt (fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive_intvl,
                          sizeof (keepalive_intvl));
#else
        ret = setsockopt (fd, IPPROTO_TCP, TCP_KEEPALIVE, &keepalive_intvl,
                          sizeof (keepalive_intvl));
#endif
        if (ret == -1) {
                gf_log ("socket", GF_LOG_WARNING,
                        "failed to set keep alive interval on socket %d", fd);
                goto err;
        }
#else
        ret = setsockopt (fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepalive_idle,
                          sizeof (keepalive_intvl));
        if (ret == -1) {
                gf_log ("socket", GF_LOG_WARNING,
                        "failed to set keep idle on socket %d", fd);
                goto err;
        }
        ret = setsockopt (fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepalive_intvl,
                          sizeof (keepalive_intvl));
        if (ret == -1) {
                gf_log ("socket", GF_LOG_WARNING,
                        "failed to set keep alive interval on socket %d", fd);
                goto err;
        }
#endif

done:
        gf_log (THIS->name, GF_LOG_TRACE, "Keep-alive enabled for socket %d, interval "
                "%d, idle: %d", fd, keepalive_intvl, keepalive_idle);

err:
        return ret;
}


int
__socket_connect_finish (int fd)
{
        int       ret = -1;
        int       optval = 0;
        socklen_t optlen = sizeof (int);

        ret = getsockopt (fd, SOL_SOCKET, SO_ERROR, (void *)&optval, &optlen);

        if (ret == 0 && optval) {
                errno = optval;
                ret = -1;
        }

        return ret;
}


void
__socket_reset (rpc_transport_t *this)
{
        socket_private_t *priv = NULL;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        /* TODO: use mem-pool on incoming data */

        if (priv->incoming.iobref) {
                iobref_unref (priv->incoming.iobref);
                priv->incoming.iobref = NULL;
        }

        if (priv->incoming.iobuf) {
                iobuf_unref (priv->incoming.iobuf);
        }

        if (priv->incoming.request_info != NULL) {
                GF_FREE (priv->incoming.request_info);
        }

        memset (&priv->incoming, 0, sizeof (priv->incoming));

        event_unregister (this->ctx->event_pool, priv->sock, priv->idx);

        close (priv->sock);
        priv->sock = -1;
        priv->idx = -1;
        priv->connected = -1;

out:
        return;
}


void
socket_set_lastfrag (uint32_t *fragsize) {
        (*fragsize) |= 0x80000000U;
}


void
socket_set_frag_header_size (uint32_t size, char *haddr)
{
        size = htonl (size);
        memcpy (haddr, &size, sizeof (size));
}


void
socket_set_last_frag_header_size (uint32_t size, char *haddr)
{
        socket_set_lastfrag (&size);
        socket_set_frag_header_size (size, haddr);
}

struct ioq *
__socket_ioq_new (rpc_transport_t *this, rpc_transport_msg_t *msg)
{
        struct ioq       *entry = NULL;
        int               count = 0;
        uint32_t          size  = 0;

        GF_VALIDATE_OR_GOTO ("socket", this, out);

        /* TODO: use mem-pool */
        entry = GF_CALLOC (1, sizeof (*entry), gf_common_mt_ioq);
        if (!entry)
                return NULL;

        count = msg->rpchdrcount + msg->proghdrcount + msg->progpayloadcount;

        GF_ASSERT (count <= (MAX_IOVEC - 1));

        size = iov_length (msg->rpchdr, msg->rpchdrcount)
                + iov_length (msg->proghdr, msg->proghdrcount)
                + iov_length (msg->progpayload, msg->progpayloadcount);

        if (size > RPC_MAX_FRAGMENT_SIZE) {
                gf_log (this->name, GF_LOG_ERROR,
                        "msg size (%u) bigger than the maximum allowed size on "
                        "sockets (%u)", size, RPC_MAX_FRAGMENT_SIZE);
                GF_FREE (entry);
                return NULL;
        }

        socket_set_last_frag_header_size (size, (char *)&entry->fraghdr);

        entry->vector[0].iov_base = (char *)&entry->fraghdr;
        entry->vector[0].iov_len = sizeof (entry->fraghdr);
        entry->count = 1;

        if (msg->rpchdr != NULL) {
                memcpy (&entry->vector[1], msg->rpchdr,
                        sizeof (struct iovec) * msg->rpchdrcount);
                entry->count += msg->rpchdrcount;
        }

        if (msg->proghdr != NULL) {
                memcpy (&entry->vector[entry->count], msg->proghdr,
                        sizeof (struct iovec) * msg->proghdrcount);
                entry->count += msg->proghdrcount;
        }

        if (msg->progpayload != NULL) {
                memcpy (&entry->vector[entry->count], msg->progpayload,
                        sizeof (struct iovec) * msg->progpayloadcount);
                entry->count += msg->progpayloadcount;
        }

        entry->pending_vector = entry->vector;
        entry->pending_count  = entry->count;

        if (msg->iobref != NULL)
                entry->iobref = iobref_ref (msg->iobref);

        INIT_LIST_HEAD (&entry->list);

out:
        return entry;
}


void
__socket_ioq_entry_free (struct ioq *entry)
{
        GF_VALIDATE_OR_GOTO ("socket", entry, out);

        list_del_init (&entry->list);
        if (entry->iobref)
                iobref_unref (entry->iobref);

        /* TODO: use mem-pool */
        GF_FREE (entry);

out:
        return;
}


void
__socket_ioq_flush (rpc_transport_t *this)
{
        socket_private_t *priv = NULL;
        struct ioq       *entry = NULL;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        while (!list_empty (&priv->ioq)) {
                entry = priv->ioq_next;
                __socket_ioq_entry_free (entry);
        }

out:
        return;
}


int
__socket_ioq_churn_entry (rpc_transport_t *this, struct ioq *entry)
{
        int ret = -1;

        ret = __socket_writev (this, entry->pending_vector,
                               entry->pending_count,
                               &entry->pending_vector,
                               &entry->pending_count);

        if (ret == 0) {
                /* current entry was completely written */
                GF_ASSERT (entry->pending_count == 0);
                __socket_ioq_entry_free (entry);
        }

        return ret;
}


int
__socket_ioq_churn (rpc_transport_t *this)
{
        socket_private_t *priv = NULL;
        int               ret = 0;
        struct ioq       *entry = NULL;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        while (!list_empty (&priv->ioq)) {
                /* pick next entry */
                entry = priv->ioq_next;

                ret = __socket_ioq_churn_entry (this, entry);

                if (ret != 0)
                        break;
        }

        if (list_empty (&priv->ioq)) {
                /* all pending writes done, not interested in POLLOUT */
                priv->idx = event_select_on (this->ctx->event_pool,
                                             priv->sock, priv->idx, -1, 0);
        }

out:
        return ret;
}


int
socket_event_poll_err (rpc_transport_t *this)
{
        socket_private_t *priv = NULL;
        int               ret = -1;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        pthread_mutex_lock (&priv->lock);
        {
                __socket_ioq_flush (this);
                __socket_reset (this);
        }
        pthread_mutex_unlock (&priv->lock);

        rpc_transport_notify (this, RPC_TRANSPORT_DISCONNECT, this);

out:
        return ret;
}


int
socket_event_poll_out (rpc_transport_t *this)
{
        socket_private_t *priv = NULL;
        int               ret = -1;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        pthread_mutex_lock (&priv->lock);
        {
                if (priv->connected == 1) {
                        ret = __socket_ioq_churn (this);

                        if (ret == -1) {
                                __socket_disconnect (this);
                        }
                }
        }
        pthread_mutex_unlock (&priv->lock);

        ret = rpc_transport_notify (this, RPC_TRANSPORT_MSG_SENT, NULL);

out:
        return ret;
}


inline int
__socket_read_simple_msg (rpc_transport_t *this)
{
        socket_private_t *priv           = NULL;
        int               ret            = 0;
        uint32_t          remaining_size = 0;
        size_t            bytes_read     = 0;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        switch (priv->incoming.frag.simple_state) {

        case SP_STATE_SIMPLE_MSG_INIT:
                remaining_size = RPC_FRAGSIZE (priv->incoming.fraghdr)
                        - priv->incoming.frag.bytes_read;

                __socket_proto_init_pending (priv, remaining_size);

                priv->incoming.frag.simple_state =
                        SP_STATE_READING_SIMPLE_MSG;

                /* fall through */

        case SP_STATE_READING_SIMPLE_MSG:
                ret = 0;

                remaining_size = RPC_FRAGSIZE (priv->incoming.fraghdr)
                        - priv->incoming.frag.bytes_read;

                if (remaining_size > 0) {
                        ret = __socket_readv (this,
                                              priv->incoming.pending_vector, 1,
                                              &priv->incoming.pending_vector,
                                              &priv->incoming.pending_count,
                                              &bytes_read);
                }

                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "reading from socket failed. Error (%s), "
                                "peer (%s)", strerror (errno),
                                this->peerinfo.identifier);
                        break;
                }

                priv->incoming.frag.bytes_read += bytes_read;
                priv->incoming.frag.fragcurrent += bytes_read;

                if (ret > 0) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "partial read on non-blocking socket.");
                        break;
                }

                if (ret == 0) {
                        priv->incoming.frag.simple_state
                                =  SP_STATE_SIMPLE_MSG_INIT;
                }
        }

out:
        return ret;
}


inline int
__socket_read_simple_request (rpc_transport_t *this)
{
        return __socket_read_simple_msg (this);
}


#define rpc_cred_addr(buf) (buf + RPC_MSGTYPE_SIZE + RPC_CALL_BODY_SIZE - 4)

#define rpc_verf_addr(fragcurrent) (fragcurrent - 4)

#define rpc_msgtype_addr(buf) (buf + 4)

#define rpc_prognum_addr(buf) (buf + RPC_MSGTYPE_SIZE + 4)
#define rpc_progver_addr(buf) (buf + RPC_MSGTYPE_SIZE + 8)
#define rpc_procnum_addr(buf) (buf + RPC_MSGTYPE_SIZE + 12)

inline int
__socket_read_vectored_request (rpc_transport_t *this, rpcsvc_vector_sizer vector_sizer)
{
        socket_private_t *priv                   = NULL;
        int               ret                    = 0;
        uint32_t          credlen                = 0, verflen = 0;
        char             *addr                   = NULL;
        struct iobuf     *iobuf                  = NULL;
        uint32_t          remaining_size         = 0;
        ssize_t           readsize               = 0;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        switch (priv->incoming.frag.call_body.request.vector_state) {
        case SP_STATE_VECTORED_REQUEST_INIT:
                priv->incoming.frag.call_body.request.vector_sizer_state = 0;
                addr = rpc_cred_addr (iobuf_ptr (priv->incoming.iobuf));

                /* also read verf flavour and verflen */
                credlen = ntoh32 (*((uint32_t *)addr))
                        +  RPC_AUTH_FLAVOUR_N_LENGTH_SIZE;

                __socket_proto_init_pending (priv, credlen);

                priv->incoming.frag.call_body.request.vector_state =
                        SP_STATE_READING_CREDBYTES;

                /* fall through */

        case SP_STATE_READING_CREDBYTES:
                __socket_proto_read (priv, ret);

                priv->incoming.frag.call_body.request.vector_state =
                        SP_STATE_READ_CREDBYTES;

                /* fall through */

        case SP_STATE_READ_CREDBYTES:
                addr = rpc_verf_addr (priv->incoming.frag.fragcurrent);
                verflen = ntoh32 (*((uint32_t *)addr));

                if (verflen == 0) {
                        priv->incoming.frag.call_body.request.vector_state
                                = SP_STATE_READ_VERFBYTES;
                        goto sp_state_read_verfbytes;
                }
                __socket_proto_init_pending (priv, verflen);

                priv->incoming.frag.call_body.request.vector_state
                        = SP_STATE_READING_VERFBYTES;

                /* fall through */

        case SP_STATE_READING_VERFBYTES:
                __socket_proto_read (priv, ret);

                priv->incoming.frag.call_body.request.vector_state =
                        SP_STATE_READ_VERFBYTES;

                /* fall through */

        case SP_STATE_READ_VERFBYTES:
sp_state_read_verfbytes:
                priv->incoming.frag.call_body.request.vector_sizer_state =
                        vector_sizer (priv->incoming.frag.call_body.request.vector_sizer_state,
                                      &readsize,
                                      priv->incoming.frag.fragcurrent);
                __socket_proto_init_pending (priv, readsize);
                priv->incoming.frag.call_body.request.vector_state
                        = SP_STATE_READING_PROGHDR;

                /* fall through */

        case SP_STATE_READING_PROGHDR:
                __socket_proto_read (priv, ret);
sp_state_reading_proghdr:
                priv->incoming.frag.call_body.request.vector_sizer_state =
                        vector_sizer (priv->incoming.frag.call_body.request.vector_sizer_state,
                                      &readsize,
                                      priv->incoming.frag.fragcurrent);
                if (readsize == 0) {
                        priv->incoming.frag.call_body.request.vector_state =
                                SP_STATE_READ_PROGHDR;
                } else {
                        __socket_proto_init_pending (priv, readsize);
                        __socket_proto_read (priv, ret);
                        goto sp_state_reading_proghdr;
                }

        case SP_STATE_READ_PROGHDR:
                if (priv->incoming.payload_vector.iov_base == NULL) {
                        iobuf = iobuf_get (this->ctx->iobuf_pool);
                        if (!iobuf) {
                                ret = -1;
                                break;
                        }

                        if (priv->incoming.iobref == NULL) {
                                priv->incoming.iobref = iobref_new ();
                                if (priv->incoming.iobref == NULL) {
                                        ret = -1;
                                        iobuf_unref (iobuf);
                                        break;
                                }
                        }

                        iobref_add (priv->incoming.iobref, iobuf);
                        iobuf_unref (iobuf);

                        priv->incoming.payload_vector.iov_base
                                = iobuf_ptr (iobuf);

                        priv->incoming.frag.fragcurrent = iobuf_ptr (iobuf);
                }

                priv->incoming.frag.call_body.request.vector_state =
                        SP_STATE_READING_PROG;

                /* fall through */

        case SP_STATE_READING_PROG:
                /* now read the remaining rpc msg into buffer pointed by
                 * fragcurrent
                 */

                ret = __socket_read_simple_msg (this);

                remaining_size = RPC_FRAGSIZE (priv->incoming.fraghdr)
                        - priv->incoming.frag.bytes_read;

                if ((ret == -1)
                    || ((ret == 0)
                        && (remaining_size == 0)
                        && RPC_LASTFRAG (priv->incoming.fraghdr))) {
                        priv->incoming.frag.call_body.request.vector_state
                                = SP_STATE_VECTORED_REQUEST_INIT;
                        priv->incoming.payload_vector.iov_len
                                = (unsigned long)priv->incoming.frag.fragcurrent
                                - (unsigned long)
                                priv->incoming.payload_vector.iov_base;
                }
                break;
        }

out:
        return ret;
}

inline int
__socket_read_request (rpc_transport_t *this)
{
        socket_private_t *priv               = NULL;
        uint32_t          prognum            = 0, procnum = 0, progver = 0;
        uint32_t          remaining_size     = 0;
        int               ret                = -1;
        char             *buf                = NULL;
        rpcsvc_vector_sizer     vector_sizer = NULL;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        switch (priv->incoming.frag.call_body.request.header_state) {

        case SP_STATE_REQUEST_HEADER_INIT:

                __socket_proto_init_pending (priv, RPC_CALL_BODY_SIZE);

                priv->incoming.frag.call_body.request.header_state
                        = SP_STATE_READING_RPCHDR1;

                /* fall through */

        case SP_STATE_READING_RPCHDR1:
                __socket_proto_read (priv, ret);

                priv->incoming.frag.call_body.request.header_state =
                        SP_STATE_READ_RPCHDR1;

                /* fall through */

        case SP_STATE_READ_RPCHDR1:
                buf = rpc_prognum_addr (iobuf_ptr (priv->incoming.iobuf));
                prognum = ntoh32 (*((uint32_t *)buf));

                buf = rpc_progver_addr (iobuf_ptr (priv->incoming.iobuf));
                progver = ntoh32 (*((uint32_t *)buf));

                buf = rpc_procnum_addr (iobuf_ptr (priv->incoming.iobuf));
                procnum = ntoh32 (*((uint32_t *)buf));

                if (this->listener) {
                        /* this check is needed as rpcsvc and rpc-clnt actor structures are
                         * not same */
                        vector_sizer = rpcsvc_get_program_vector_sizer ((rpcsvc_t *)this->mydata,
                                                                        prognum, progver, procnum);
                }

                if (vector_sizer) {
                        ret = __socket_read_vectored_request (this, vector_sizer);
                } else {
                        ret = __socket_read_simple_request (this);
                }

                remaining_size = RPC_FRAGSIZE (priv->incoming.fraghdr)
                        - priv->incoming.frag.bytes_read;

                if ((ret == -1)
                    || ((ret == 0)
                        && (remaining_size == 0)
                        && (RPC_LASTFRAG (priv->incoming.fraghdr)))) {
                        priv->incoming.frag.call_body.request.header_state =
                                SP_STATE_REQUEST_HEADER_INIT;
                }

                break;
        }

out:
        return ret;
}


inline int
__socket_read_accepted_successful_reply (rpc_transport_t *this)
{
        socket_private_t *priv                     = NULL;
        int               ret                      = 0;
        struct iobuf     *iobuf                    = NULL;
        uint32_t          gluster_read_rsp_hdr_len = 0;
        gfs3_read_rsp     read_rsp                 = {0, };

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        switch (priv->incoming.frag.call_body.reply.accepted_success_state) {

        case SP_STATE_ACCEPTED_SUCCESS_REPLY_INIT:
                gluster_read_rsp_hdr_len = xdr_sizeof ((xdrproc_t) xdr_gfs3_read_rsp,
                                                       &read_rsp);

                if (gluster_read_rsp_hdr_len == 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "xdr_sizeof on gfs3_read_rsp failed");
                        ret = -1;
                        goto out;
                }
                __socket_proto_init_pending (priv, gluster_read_rsp_hdr_len);

                priv->incoming.frag.call_body.reply.accepted_success_state
                        = SP_STATE_READING_PROC_HEADER;

                /* fall through */

        case SP_STATE_READING_PROC_HEADER:
                __socket_proto_read (priv, ret);

                priv->incoming.frag.call_body.reply.accepted_success_state
                        = SP_STATE_READ_PROC_HEADER;

                if (priv->incoming.payload_vector.iov_base == NULL) {
                        iobuf = iobuf_get (this->ctx->iobuf_pool);
                        if (iobuf == NULL) {
                                ret = -1;
                                goto out;
                        }

                        if (priv->incoming.iobref == NULL) {
                                priv->incoming.iobref = iobref_new ();
                                if (priv->incoming.iobref == NULL) {
                                        ret = -1;
                                        iobuf_unref (iobuf);
                                        goto out;
                                }
                        }

                        iobref_add (priv->incoming.iobref, iobuf);
                        iobuf_unref (iobuf);

                        priv->incoming.payload_vector.iov_base
                                = iobuf_ptr (iobuf);
                }

                priv->incoming.frag.fragcurrent
                        = priv->incoming.payload_vector.iov_base;

                /* fall through */

        case SP_STATE_READ_PROC_HEADER:
                /* now read the entire remaining msg into new iobuf */
                ret = __socket_read_simple_msg (this);
                if ((ret == -1)
                    || ((ret == 0)
                        && RPC_LASTFRAG (priv->incoming.fraghdr))) {
                        priv->incoming.frag.call_body.reply.accepted_success_state
                                = SP_STATE_ACCEPTED_SUCCESS_REPLY_INIT;
                }

                break;
        }

out:
        return ret;
}

#define rpc_reply_verflen_addr(fragcurrent) ((char *)fragcurrent - 4)
#define rpc_reply_accept_status_addr(fragcurrent) ((char *)fragcurrent - 4)

inline int
__socket_read_accepted_reply (rpc_transport_t *this)
{
        socket_private_t *priv           = NULL;
        int               ret            = -1;
        char             *buf            = NULL;
        uint32_t          verflen        = 0, len = 0;
        uint32_t          remaining_size = 0;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        switch (priv->incoming.frag.call_body.reply.accepted_state) {

        case SP_STATE_ACCEPTED_REPLY_INIT:
                __socket_proto_init_pending (priv,
                                             RPC_AUTH_FLAVOUR_N_LENGTH_SIZE);

                priv->incoming.frag.call_body.reply.accepted_state
                        = SP_STATE_READING_REPLY_VERFLEN;

                /* fall through */

        case SP_STATE_READING_REPLY_VERFLEN:
                __socket_proto_read (priv, ret);

                priv->incoming.frag.call_body.reply.accepted_state
                        = SP_STATE_READ_REPLY_VERFLEN;

                /* fall through */

        case SP_STATE_READ_REPLY_VERFLEN:
                buf = rpc_reply_verflen_addr (priv->incoming.frag.fragcurrent);

                verflen = ntoh32 (*((uint32_t *) buf));

                /* also read accept status along with verf data */
                len = verflen + RPC_ACCEPT_STATUS_LEN;

                __socket_proto_init_pending (priv, len);

                priv->incoming.frag.call_body.reply.accepted_state
                        = SP_STATE_READING_REPLY_VERFBYTES;

                /* fall through */

        case SP_STATE_READING_REPLY_VERFBYTES:
                __socket_proto_read (priv, ret);

                priv->incoming.frag.call_body.reply.accepted_state
                        = SP_STATE_READ_REPLY_VERFBYTES;

                buf = rpc_reply_accept_status_addr (priv->incoming.frag.fragcurrent);

                priv->incoming.frag.call_body.reply.accept_status
                        = ntoh32 (*(uint32_t *) buf);

                /* fall through */

        case SP_STATE_READ_REPLY_VERFBYTES:

                if (priv->incoming.frag.call_body.reply.accept_status
                    == SUCCESS) {
                        ret = __socket_read_accepted_successful_reply (this);
                } else {
                        /* read entire remaining msg into buffer pointed to by
                         * fragcurrent
                         */
                        ret = __socket_read_simple_msg (this);
                }

                remaining_size = RPC_FRAGSIZE (priv->incoming.fraghdr)
                        - priv->incoming.frag.bytes_read;

                if ((ret == -1)
                    || ((ret == 0)
                        && (remaining_size == 0)
                        && (RPC_LASTFRAG (priv->incoming.fraghdr)))) {
                        priv->incoming.frag.call_body.reply.accepted_state
                                = SP_STATE_ACCEPTED_REPLY_INIT;
                }

                break;
        }

out:
        return ret;
}


inline int
__socket_read_denied_reply (rpc_transport_t *this)
{
        return __socket_read_simple_msg (this);
}


#define rpc_reply_status_addr(fragcurrent) ((char *)fragcurrent - 4)


inline int
__socket_read_vectored_reply (rpc_transport_t *this)
{
        socket_private_t *priv           = NULL;
        int               ret            = 0;
        char             *buf            = NULL;
        uint32_t          remaining_size = 0;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        switch (priv->incoming.frag.call_body.reply.status_state) {

        case SP_STATE_ACCEPTED_REPLY_INIT:
                __socket_proto_init_pending (priv, RPC_REPLY_STATUS_SIZE);

                priv->incoming.frag.call_body.reply.status_state
                        = SP_STATE_READING_REPLY_STATUS;

                /* fall through */

        case SP_STATE_READING_REPLY_STATUS:
                __socket_proto_read (priv, ret);

                buf = rpc_reply_status_addr (priv->incoming.frag.fragcurrent);

                priv->incoming.frag.call_body.reply.accept_status
                        = ntoh32 (*((uint32_t *) buf));

                priv->incoming.frag.call_body.reply.status_state
                        = SP_STATE_READ_REPLY_STATUS;

                /* fall through */

        case SP_STATE_READ_REPLY_STATUS:
                if (priv->incoming.frag.call_body.reply.accept_status
                    == MSG_ACCEPTED) {
                        ret = __socket_read_accepted_reply (this);
                } else {
                        ret = __socket_read_denied_reply (this);
                }

                remaining_size = RPC_FRAGSIZE (priv->incoming.fraghdr)
                        - priv->incoming.frag.bytes_read;

                if ((ret == -1)
                    || ((ret == 0)
                        && (remaining_size == 0)
                        && (RPC_LASTFRAG (priv->incoming.fraghdr)))) {
                        priv->incoming.frag.call_body.reply.status_state
                                = SP_STATE_ACCEPTED_REPLY_INIT;
                        priv->incoming.payload_vector.iov_len
                                = (unsigned long)priv->incoming.frag.fragcurrent
                                - (unsigned long)
                                priv->incoming.payload_vector.iov_base;
                }
                break;
        }

out:
        return ret;
}


inline int
__socket_read_simple_reply (rpc_transport_t *this)
{
        return __socket_read_simple_msg (this);
}

#define rpc_xid_addr(buf) (buf)

inline int
__socket_read_reply (rpc_transport_t *this)
{
        socket_private_t   *priv         = NULL;
        char               *buf          = NULL;
        int32_t             ret          = -1;
        rpc_request_info_t *request_info = NULL;
        char                map_xid      = 0;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        buf = rpc_xid_addr (iobuf_ptr (priv->incoming.iobuf));

        if (priv->incoming.request_info == NULL) {
                priv->incoming.request_info = GF_CALLOC (1,
                                                         sizeof (*request_info),
                                                         gf_common_mt_rpc_trans_reqinfo_t);
                if (priv->incoming.request_info == NULL) {
                        goto out;
                }

                map_xid = 1;
        }

        request_info = priv->incoming.request_info;

        if (map_xid) {
                request_info->xid = ntoh32 (*((uint32_t *) buf));

                /* release priv->lock, so as to avoid deadlock b/w conn->lock
                 * and priv->lock, since we are doing an upcall here.
                 */
                pthread_mutex_unlock (&priv->lock);
                {
                        ret = rpc_transport_notify (this,
                                                    RPC_TRANSPORT_MAP_XID_REQUEST,
                                                    priv->incoming.request_info);
                }
                pthread_mutex_lock (&priv->lock);

                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "notify for event MAP_XID failed");
                        goto out;
                }
        }

        if ((request_info->prognum == GLUSTER3_1_FOP_PROGRAM)
            && (request_info->procnum == GF_FOP_READ)) {
                if (map_xid && request_info->rsp.rsp_payload_count != 0) {
                        priv->incoming.iobref
                                = iobref_ref (request_info->rsp.rsp_iobref);
                        priv->incoming.payload_vector
                                = *request_info->rsp.rsp_payload;
                }

                ret = __socket_read_vectored_reply (this);
        } else {
                ret = __socket_read_simple_reply (this);
        }
out:
        return ret;
}


/* returns the number of bytes yet to be read in a fragment */
inline int
__socket_read_frag (rpc_transport_t *this)
{
        socket_private_t *priv           = NULL;
        int32_t           ret            = 0;
        char             *buf            = NULL;
        uint32_t          remaining_size = 0;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        switch (priv->incoming.frag.state) {
        case SP_STATE_NADA:
                __socket_proto_init_pending (priv, RPC_MSGTYPE_SIZE);

                priv->incoming.frag.state = SP_STATE_READING_MSGTYPE;

                /* fall through */

        case SP_STATE_READING_MSGTYPE:
                __socket_proto_read (priv, ret);

                priv->incoming.frag.state = SP_STATE_READ_MSGTYPE;
                /* fall through */

        case SP_STATE_READ_MSGTYPE:
                buf = rpc_msgtype_addr (iobuf_ptr (priv->incoming.iobuf));
                priv->incoming.msg_type = ntoh32 (*((uint32_t *)buf));

                if (priv->incoming.msg_type == CALL) {
                        ret = __socket_read_request (this);
                } else if (priv->incoming.msg_type == REPLY) {
                        ret = __socket_read_reply (this);
                } else if (priv->incoming.msg_type == GF_UNIVERSAL_ANSWER) {
                        gf_log ("rpc", GF_LOG_ERROR,
                                "older version of protocol/process trying to "
                                "connect from %s. use newer version on that node",
                                this->peerinfo.identifier);
                } else {
                        gf_log ("rpc", GF_LOG_ERROR,
                                "wrong MSG-TYPE (%d) received from %s",
                                priv->incoming.msg_type,
                                this->peerinfo.identifier);
                        ret = -1;
                }

                remaining_size = RPC_FRAGSIZE (priv->incoming.fraghdr)
                        - priv->incoming.frag.bytes_read;

                if ((ret == -1)
                    || ((ret == 0)
                        && (remaining_size == 0)
                        && (RPC_LASTFRAG (priv->incoming.fraghdr)))) {
                        priv->incoming.frag.state = SP_STATE_NADA;
                }

                break;
        }

out:
        return ret;
}


inline
void __socket_reset_priv (socket_private_t *priv)
{
        if (priv->incoming.iobref) {
                iobref_unref (priv->incoming.iobref);
                priv->incoming.iobref = NULL;
        }

        if (priv->incoming.iobuf) {
                iobuf_unref (priv->incoming.iobuf);
        }

        if (priv->incoming.request_info != NULL) {
                GF_FREE (priv->incoming.request_info);
                priv->incoming.request_info = NULL;
        }

        memset (&priv->incoming.payload_vector, 0,
                sizeof (priv->incoming.payload_vector));

        priv->incoming.iobuf = NULL;
}


int
__socket_proto_state_machine (rpc_transport_t *this,
                              rpc_transport_pollin_t **pollin)
{
        int               ret    = -1;
        socket_private_t *priv   = NULL;
        struct iobuf     *iobuf  = NULL;
        struct iobref    *iobref = NULL;
        struct iovec      vector[2];

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;
        while (priv->incoming.record_state != SP_STATE_COMPLETE) {
                switch (priv->incoming.record_state) {

                case SP_STATE_NADA:
                        priv->incoming.total_bytes_read = 0;
                        priv->incoming.payload_vector.iov_len = 0;

                        priv->incoming.pending_vector = priv->incoming.vector;
                        priv->incoming.pending_vector->iov_base =
                                &priv->incoming.fraghdr;

                        priv->incoming.pending_vector->iov_len  =
                                sizeof (priv->incoming.fraghdr);

                        priv->incoming.record_state = SP_STATE_READING_FRAGHDR;

                        /* fall through */

                case SP_STATE_READING_FRAGHDR:
                        ret = __socket_readv (this,
                                              priv->incoming.pending_vector, 1,
                                              &priv->incoming.pending_vector,
                                              &priv->incoming.pending_count,
                                              NULL);
                        if (ret == -1) {
                                if (priv->read_fail_log == 1) {
                                        gf_log (this->name,
                                                ((priv->connected == 1) ?
                                                 GF_LOG_WARNING : GF_LOG_DEBUG),
                                                "reading from socket failed. Error (%s)"
                                                ", peer (%s)", strerror (errno),
                                                this->peerinfo.identifier);
                                }
                                goto out;
                        }

                        if (ret > 0) {
                                gf_log (this->name, GF_LOG_TRACE, "partial "
                                        "fragment header read");
                                goto out;
                        }

                        if (ret == 0) {
                                priv->incoming.record_state =
                                        SP_STATE_READ_FRAGHDR;
                        }
                        /* fall through */

                case SP_STATE_READ_FRAGHDR:

                        priv->incoming.fraghdr = ntoh32 (priv->incoming.fraghdr);
                        priv->incoming.record_state = SP_STATE_READING_FRAG;
                        priv->incoming.total_bytes_read
                                += RPC_FRAGSIZE(priv->incoming.fraghdr);
                        iobuf = iobuf_get2 (this->ctx->iobuf_pool,
                                            priv->incoming.total_bytes_read +
                                            sizeof (priv->incoming.fraghdr));
                        if (!iobuf) {
                                ret = -ENOMEM;
                                goto out;
                        }

                        priv->incoming.iobuf = iobuf;
                        priv->incoming.iobuf_size = 0;
                        priv->incoming.frag.fragcurrent = iobuf_ptr (iobuf);
                        /* fall through */

                case SP_STATE_READING_FRAG:
                        ret = __socket_read_frag (this);

                        if ((ret == -1)
                            || (priv->incoming.frag.bytes_read !=
                                RPC_FRAGSIZE (priv->incoming.fraghdr))) {
                                goto out;
                        }

                        priv->incoming.frag.bytes_read = 0;

                        if (!RPC_LASTFRAG (priv->incoming.fraghdr)) {
                                priv->incoming.record_state =
                                        SP_STATE_READING_FRAGHDR;
                                break;
                        }

                        /* we've read the entire rpc record, notify the
                         * upper layers.
                         */
                        if (pollin != NULL) {
                                int count = 0;
                                priv->incoming.iobuf_size
                                        = priv->incoming.total_bytes_read
                                        - priv->incoming.payload_vector.iov_len;

                                memset (vector, 0, sizeof (vector));

                                if (priv->incoming.iobref == NULL) {
                                        priv->incoming.iobref = iobref_new ();
                                        if (priv->incoming.iobref == NULL) {
                                                ret = -1;
                                                goto out;
                                        }
                                }

                                vector[count].iov_base
                                        = iobuf_ptr (priv->incoming.iobuf);
                                vector[count].iov_len
                                        = priv->incoming.iobuf_size;

                                iobref = priv->incoming.iobref;

                                count++;

                                if (priv->incoming.payload_vector.iov_base
                                    != NULL) {
                                        vector[count]
                                                = priv->incoming.payload_vector;
                                        count++;
                                }

                                *pollin = rpc_transport_pollin_alloc (this,
                                                                      vector,
                                                                      count,
                                                                      priv->incoming.iobuf,
                                                                      iobref,
                                                                      priv->incoming.request_info);
                                iobuf_unref (priv->incoming.iobuf);
                                priv->incoming.iobuf = NULL;

                                if (*pollin == NULL) {
                                        gf_log (this->name, GF_LOG_WARNING,
                                                "transport pollin allocation failed");
                                        ret = -1;
                                        goto out;
                                }
                                if (priv->incoming.msg_type == REPLY)
                                        (*pollin)->is_reply = 1;

                                priv->incoming.request_info = NULL;
                        }
                        priv->incoming.record_state = SP_STATE_COMPLETE;
                        break;

                case SP_STATE_COMPLETE:
                        /* control should not reach here */
                        gf_log (this->name, GF_LOG_WARNING, "control reached to "
                                "SP_STATE_COMPLETE, which should not have "
                                "happened");
                        break;
                }
        }

        if (priv->incoming.record_state == SP_STATE_COMPLETE) {
                priv->incoming.record_state = SP_STATE_NADA;
                __socket_reset_priv (priv);
        }

out:
        if ((ret == -1) && (errno == EAGAIN)) {
                ret = 0;
        }
        return ret;
}


int
socket_proto_state_machine (rpc_transport_t *this,
                            rpc_transport_pollin_t **pollin)
{
        socket_private_t *priv = NULL;
        int               ret = 0;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        pthread_mutex_lock (&priv->lock);
        {
                ret = __socket_proto_state_machine (this, pollin);
        }
        pthread_mutex_unlock (&priv->lock);

out:
        return ret;
}


int
socket_event_poll_in (rpc_transport_t *this)
{
        int                     ret    = -1;
        rpc_transport_pollin_t *pollin = NULL;

        ret = socket_proto_state_machine (this, &pollin);

        if (pollin != NULL) {
                ret = rpc_transport_notify (this, RPC_TRANSPORT_MSG_RECEIVED,
                                            pollin);

                rpc_transport_pollin_destroy (pollin);
        }

        return ret;
}


int
socket_connect_finish (rpc_transport_t *this)
{
        int                   ret        = -1;
        socket_private_t     *priv       = NULL;
        rpc_transport_event_t event      = 0;
        char                  notify_rpc = 0;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        pthread_mutex_lock (&priv->lock);
        {
                if (priv->connected)
                        goto unlock;

                ret = __socket_connect_finish (priv->sock);

                if (ret == -1 && errno == EINPROGRESS)
                        ret = 1;

                if (ret == -1 && errno != EINPROGRESS) {
                        if (!priv->connect_finish_log) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "connection to %s failed (%s)",
                                        this->peerinfo.identifier,
                                        strerror (errno));
                                priv->connect_finish_log = 1;
                        }
                        __socket_disconnect (this);
                        notify_rpc = 1;
                        event = RPC_TRANSPORT_DISCONNECT;
                        goto unlock;
                }

                if (ret == 0) {
                        notify_rpc = 1;

                        this->myinfo.sockaddr_len =
                                sizeof (this->myinfo.sockaddr);

                        ret = getsockname (priv->sock,
                                           SA (&this->myinfo.sockaddr),
                                           &this->myinfo.sockaddr_len);
                        if (ret == -1) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "getsockname on (%d) failed (%s)",
                                        priv->sock, strerror (errno));
                                __socket_disconnect (this);
                                event = GF_EVENT_POLLERR;
                                goto unlock;
                        }

                        priv->connected = 1;
                        priv->connect_finish_log = 0;
                        event = RPC_TRANSPORT_CONNECT;
                        get_transport_identifiers (this);
                }
        }
unlock:
        pthread_mutex_unlock (&priv->lock);

        if (notify_rpc) {
                rpc_transport_notify (this, event, this);
        }
out:
        return 0;
}


/* reads rpc_requests during pollin */
int
socket_event_handler (int fd, int idx, void *data,
                      int poll_in, int poll_out, int poll_err)
{
        rpc_transport_t      *this = NULL;
        socket_private_t *priv = NULL;
        int               ret = 0;

        this = data;
        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);
        GF_VALIDATE_OR_GOTO ("socket", this->xl, out);

        THIS = this->xl;
        priv = this->private;


        pthread_mutex_lock (&priv->lock);
        {
                priv->idx = idx;
        }
        pthread_mutex_unlock (&priv->lock);

        if (!priv->connected) {
                ret = socket_connect_finish (this);
        }

        if (!ret && poll_out) {
                ret = socket_event_poll_out (this);
        }

        if (!ret && poll_in) {
                ret = socket_event_poll_in (this);
        }

        if ((ret < 0) || poll_err) {
                /* Logging has happened already in earlier cases */
                gf_log ("transport", ((ret >= 0) ? GF_LOG_INFO : GF_LOG_DEBUG),
                        "disconnecting now");
                socket_event_poll_err (this);
                rpc_transport_unref (this);
        }

out:
        return 0;
}


int
socket_server_event_handler (int fd, int idx, void *data,
                             int poll_in, int poll_out, int poll_err)
{
        rpc_transport_t             *this = NULL;
        socket_private_t        *priv = NULL;
        int                      ret = 0;
        int                      new_sock = -1;
        rpc_transport_t             *new_trans = NULL;
        struct sockaddr_storage  new_sockaddr = {0, };
        socklen_t                addrlen = sizeof (new_sockaddr);
        socket_private_t        *new_priv = NULL;
        glusterfs_ctx_t         *ctx = NULL;

        this = data;
        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);
        GF_VALIDATE_OR_GOTO ("socket", this->xl, out);

        THIS = this->xl;
        priv = this->private;
        ctx  = this->ctx;

        pthread_mutex_lock (&priv->lock);
        {
                priv->idx = idx;

                if (poll_in) {
                        new_sock = accept (priv->sock, SA (&new_sockaddr),
                                           &addrlen);

                        if (new_sock == -1) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "accept on %d failed (%s)",
                                        priv->sock, strerror (errno));
                                goto unlock;
                        }

                        if (!priv->bio) {
                                ret = __socket_nonblock (new_sock);

                                if (ret == -1) {
                                        gf_log (this->name, GF_LOG_WARNING,
                                                "NBIO on %d failed (%s)",
                                                new_sock, strerror (errno));

                                        close (new_sock);
                                        goto unlock;
                                }
                        }

                        if (priv->nodelay) {
                                ret = __socket_nodelay (new_sock);
                                if (ret == -1) {
                                        gf_log (this->name, GF_LOG_WARNING,
                                                "setsockopt() failed for "
                                                "NODELAY (%s)",
                                                strerror (errno));
                                }
                        }

                        if (priv->keepalive) {
                                ret = __socket_keepalive (new_sock,
                                                          priv->keepaliveintvl,
                                                          priv->keepaliveidle);
                                if (ret == -1)
                                        gf_log (this->name, GF_LOG_WARNING,
                                                "Failed to set keep-alive: %s",
                                                strerror (errno));
                        }

                        new_trans = GF_CALLOC (1, sizeof (*new_trans),
                                               gf_common_mt_rpc_trans_t);
                        if (!new_trans)
                                goto unlock;

                        new_trans->name = gf_strdup (this->name);

                        memcpy (&new_trans->peerinfo.sockaddr, &new_sockaddr,
                                addrlen);
                        new_trans->peerinfo.sockaddr_len = addrlen;

                        new_trans->myinfo.sockaddr_len =
                                sizeof (new_trans->myinfo.sockaddr);

                        ret = getsockname (new_sock,
                                           SA (&new_trans->myinfo.sockaddr),
                                           &new_trans->myinfo.sockaddr_len);
                        if (ret == -1) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "getsockname on %d failed (%s)",
                                        new_sock, strerror (errno));
                                close (new_sock);
                                goto unlock;
                        }

                        get_transport_identifiers (new_trans);
                        socket_init (new_trans);
                        new_trans->ops = this->ops;
                        new_trans->init = this->init;
                        new_trans->fini = this->fini;
                        new_trans->ctx  = ctx;
                        new_trans->xl   = this->xl;
                        new_trans->mydata = this->mydata;
                        new_trans->notify = this->notify;
                        new_trans->listener = this;
                        new_priv = new_trans->private;

                        pthread_mutex_lock (&new_priv->lock);
                        {
                                new_priv->sock = new_sock;
                                new_priv->connected = 1;
                                rpc_transport_ref (new_trans);

                                new_priv->idx =
                                        event_register (ctx->event_pool,
                                                        new_sock,
                                                        socket_event_handler,
                                                        new_trans, 1, 0);

                                if (new_priv->idx == -1)
                                        ret = -1;
                        }
                        pthread_mutex_unlock (&new_priv->lock);
                        if (ret == -1) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "failed to register the socket with event");
                                goto unlock;
                        }

                        ret = rpc_transport_notify (this, RPC_TRANSPORT_ACCEPT,
                                                    new_trans);
                }
        }
unlock:
        pthread_mutex_unlock (&priv->lock);

out:
        return ret;
}


int
socket_disconnect (rpc_transport_t *this)
{
        socket_private_t *priv = NULL;
        int               ret = -1;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;

        pthread_mutex_lock (&priv->lock);
        {
                ret = __socket_disconnect (this);
        }
        pthread_mutex_unlock (&priv->lock);

out:
        return ret;
}


int
socket_connect (rpc_transport_t *this, int port)
{
        int                      ret = -1;
        int                      sock = -1;
        socket_private_t        *priv = NULL;
        socklen_t                sockaddr_len = 0;
        glusterfs_ctx_t         *ctx = NULL;
        sa_family_t              sa_family = {0, };
        union gf_sock_union      sock_union;

        GF_VALIDATE_OR_GOTO ("socket", this, err);
        GF_VALIDATE_OR_GOTO ("socket", this->private, err);

        priv = this->private;
        ctx = this->ctx;

        if (!priv) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                        "connect() called on uninitialized transport");
                goto err;
        }

        pthread_mutex_lock (&priv->lock);
        {
                sock = priv->sock;
        }
        pthread_mutex_unlock (&priv->lock);

        if (sock != -1) {
                gf_log_callingfn (this->name, GF_LOG_TRACE,
                                  "connect () called on transport already connected");
                errno = EINPROGRESS;
                ret = -1;
                goto err;
        }

        ret = socket_client_get_remote_sockaddr (this, &sock_union.sa,
                                                 &sockaddr_len, &sa_family);
        if (ret == -1) {
                /* logged inside client_get_remote_sockaddr */
                goto err;
        }

        if (port > 0) {
                sock_union.sin.sin_port = htons (port);
        }
        pthread_mutex_lock (&priv->lock);
        {
                if (priv->sock != -1) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "connect() -- already connected");
                        goto unlock;
                }

                memcpy (&this->peerinfo.sockaddr, &sock_union.storage,
                        sockaddr_len);
                this->peerinfo.sockaddr_len = sockaddr_len;

                priv->sock = socket (sa_family, SOCK_STREAM, 0);
                if (priv->sock == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "socket creation failed (%s)",
                                strerror (errno));
                        goto unlock;
                }

                /* Cant help if setting socket options fails. We can continue
                 * working nonetheless.
                 */
                if (setsockopt (priv->sock, SOL_SOCKET, SO_RCVBUF,
                                &priv->windowsize,
                                sizeof (priv->windowsize)) < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "setting receive window size failed: %d: %d: "
                                "%s", priv->sock, priv->windowsize,
                                strerror (errno));
                }

                if (setsockopt (priv->sock, SOL_SOCKET, SO_SNDBUF,
                                &priv->windowsize,
                                sizeof (priv->windowsize)) < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "setting send window size failed: %d: %d: "
                                "%s", priv->sock, priv->windowsize,
                                strerror (errno));
                }


                if (priv->nodelay) {
                        ret = __socket_nodelay (priv->sock);
                        if (ret == -1) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "setsockopt() failed for NODELAY (%s)",
                                        strerror (errno));
                        }
                }

                if (!priv->bio) {
                        ret = __socket_nonblock (priv->sock);

                        if (ret == -1) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "NBIO on %d failed (%s)",
                                        priv->sock, strerror (errno));
                                close (priv->sock);
                                priv->sock = -1;
                                goto unlock;
                        }
                }

                if (priv->keepalive) {
                        ret = __socket_keepalive (priv->sock,
                                                  priv->keepaliveintvl,
                                                  priv->keepaliveidle);
                        if (ret == -1)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set keep-alive: %s",
                                        strerror (errno));
                }

                SA (&this->myinfo.sockaddr)->sa_family =
                        SA (&this->peerinfo.sockaddr)->sa_family;

                ret = client_bind (this, SA (&this->myinfo.sockaddr),
                                   &this->myinfo.sockaddr_len, priv->sock);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "client bind failed: %s", strerror (errno));
                        close (priv->sock);
                        priv->sock = -1;
                        goto unlock;
                }

                ret = connect (priv->sock, SA (&this->peerinfo.sockaddr),
                               this->peerinfo.sockaddr_len);

                if (ret == -1 && errno != EINPROGRESS) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "connection attempt failed (%s)",
                                strerror (errno));
                        close (priv->sock);
                        priv->sock = -1;
                        goto unlock;
                }

                priv->connected = 0;

                rpc_transport_ref (this);

                priv->idx = event_register (ctx->event_pool, priv->sock,
                                            socket_event_handler, this, 1, 1);
                if (priv->idx == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to register the event");
                        ret = -1;
                }
        }
unlock:
        pthread_mutex_unlock (&priv->lock);

err:
        return ret;
}


int
socket_listen (rpc_transport_t *this)
{
        socket_private_t *       priv = NULL;
        int                      ret = -1;
        int                      sock = -1;
        struct sockaddr_storage  sockaddr;
        socklen_t                sockaddr_len = 0;
        peer_info_t             *myinfo = NULL;
        glusterfs_ctx_t         *ctx = NULL;
        sa_family_t              sa_family = {0, };

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv   = this->private;
        myinfo = &this->myinfo;
        ctx    = this->ctx;

        pthread_mutex_lock (&priv->lock);
        {
                sock = priv->sock;
        }
        pthread_mutex_unlock (&priv->lock);

        if (sock != -1)  {
                gf_log_callingfn (this->name, GF_LOG_DEBUG,
                                  "alreading listening");
                return ret;
        }

        ret = socket_server_get_local_sockaddr (this, SA (&sockaddr),
                                                &sockaddr_len, &sa_family);
        if (ret == -1) {
                return ret;
        }

        pthread_mutex_lock (&priv->lock);
        {
                if (priv->sock != -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "already listening");
                        goto unlock;
                }

                memcpy (&myinfo->sockaddr, &sockaddr, sockaddr_len);
                myinfo->sockaddr_len = sockaddr_len;

                priv->sock = socket (sa_family, SOCK_STREAM, 0);

                if (priv->sock == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "socket creation failed (%s)",
                                strerror (errno));
                        goto unlock;
                }

                /* Cant help if setting socket options fails. We can continue
                 * working nonetheless.
                 */
                if (setsockopt (priv->sock, SOL_SOCKET, SO_RCVBUF,
                                &priv->windowsize,
                                sizeof (priv->windowsize)) < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "setting receive window size failed: %d: %d: "
                                "%s", priv->sock, priv->windowsize,
                                strerror (errno));
                }

                if (setsockopt (priv->sock, SOL_SOCKET, SO_SNDBUF,
                                &priv->windowsize,
                                sizeof (priv->windowsize)) < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "setting send window size failed: %d: %d: "
                                "%s", priv->sock, priv->windowsize,
                                strerror (errno));
                }

                if (priv->nodelay) {
                        ret = __socket_nodelay (priv->sock);
                        if (ret == -1) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "setsockopt() failed for NODELAY (%s)",
                                        strerror (errno));
                        }
                }

                if (!priv->bio) {
                        ret = __socket_nonblock (priv->sock);

                        if (ret == -1) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "NBIO on %d failed (%s)",
                                        priv->sock, strerror (errno));
                                close (priv->sock);
                                priv->sock = -1;
                                goto unlock;
                        }
                }

                ret = __socket_server_bind (this);

                if (ret == -1) {
                        /* logged inside __socket_server_bind() */
                        close (priv->sock);
                        priv->sock = -1;
                        goto unlock;
                }

                if (priv->backlog)
                        ret = listen (priv->sock, priv->backlog);
                else
                        ret = listen (priv->sock, 10);

                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "could not set socket %d to listen mode (%s)",
                                priv->sock, strerror (errno));
                        close (priv->sock);
                        priv->sock = -1;
                        goto unlock;
                }

                rpc_transport_ref (this);

                priv->idx = event_register (ctx->event_pool, priv->sock,
                                            socket_server_event_handler,
                                            this, 1, 0);

                if (priv->idx == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "could not register socket %d with events",
                                priv->sock);
                        ret = -1;
                        close (priv->sock);
                        priv->sock = -1;
                        goto unlock;
                }
        }
unlock:
        pthread_mutex_unlock (&priv->lock);

out:
        return ret;
}


int32_t
socket_submit_request (rpc_transport_t *this, rpc_transport_req_t *req)
{
        socket_private_t *priv = NULL;
        int               ret = -1;
        char              need_poll_out = 0;
        char              need_append = 1;
        struct ioq       *entry = NULL;
        glusterfs_ctx_t  *ctx = NULL;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;
        ctx  = this->ctx;

        pthread_mutex_lock (&priv->lock);
        {
                if (priv->connected != 1) {
                        if (!priv->submit_log && !priv->connect_finish_log) {
                                gf_log (this->name, GF_LOG_INFO,
                                        "not connected (priv->connected = %d)",
                                        priv->connected);
                                priv->submit_log = 1;
                        }
                        goto unlock;
                }

                priv->submit_log = 0;
                entry = __socket_ioq_new (this, &req->msg);
                if (!entry)
                        goto unlock;

                if (list_empty (&priv->ioq)) {
                        ret = __socket_ioq_churn_entry (this, entry);

                        if (ret == 0)
                                need_append = 0;

                        if (ret > 0)
                                need_poll_out = 1;
                }

                if (need_append) {
                        list_add_tail (&entry->list, &priv->ioq);
                        ret = 0;
                }

                if (need_poll_out) {
                        /* first entry to wait. continue writing on POLLOUT */
                        priv->idx = event_select_on (ctx->event_pool,
                                                     priv->sock,
                                                     priv->idx, -1, 1);
                }
        }
unlock:
        pthread_mutex_unlock (&priv->lock);

out:
        return ret;
}


int32_t
socket_submit_reply (rpc_transport_t *this, rpc_transport_reply_t *reply)
{
        socket_private_t *priv = NULL;
        int               ret = -1;
        char              need_poll_out = 0;
        char              need_append = 1;
        struct ioq       *entry = NULL;
        glusterfs_ctx_t  *ctx = NULL;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        priv = this->private;
        ctx  = this->ctx;

        pthread_mutex_lock (&priv->lock);
        {
                if (priv->connected != 1) {
                        if (!priv->submit_log && !priv->connect_finish_log) {
                                gf_log (this->name, GF_LOG_INFO,
                                        "not connected (priv->connected = %d)",
                                        priv->connected);
                                priv->submit_log = 1;
                        }
                        goto unlock;
                }
                priv->submit_log = 0;
                entry = __socket_ioq_new (this, &reply->msg);
                if (!entry)
                        goto unlock;
                if (list_empty (&priv->ioq)) {
                        ret = __socket_ioq_churn_entry (this, entry);

                        if (ret == 0)
                                need_append = 0;

                        if (ret > 0)
                                need_poll_out = 1;
                }

                if (need_append) {
                        list_add_tail (&entry->list, &priv->ioq);
                        ret = 0;
                }

                if (need_poll_out) {
                        /* first entry to wait. continue writing on POLLOUT */
                        priv->idx = event_select_on (ctx->event_pool,
                                                     priv->sock,
                                                     priv->idx, -1, 1);
                }
        }

unlock:
        pthread_mutex_unlock (&priv->lock);

out:
        return ret;
}


int32_t
socket_getpeername (rpc_transport_t *this, char *hostname, int hostlen)
{
        int32_t ret = -1;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", hostname, out);

        if (hostlen < (strlen (this->peerinfo.identifier) + 1)) {
                goto out;
        }

        strcpy (hostname, this->peerinfo.identifier);
        ret = 0;
out:
        return ret;
}


int32_t
socket_getpeeraddr (rpc_transport_t *this, char *peeraddr, int addrlen,
                    struct sockaddr_storage *sa, socklen_t salen)
{
        int32_t ret = -1;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", sa, out);

        *sa = this->peerinfo.sockaddr;

        if (peeraddr != NULL) {
                ret = socket_getpeername (this, peeraddr, addrlen);
        }
        ret = 0;

out:
        return ret;
}


int32_t
socket_getmyname (rpc_transport_t *this, char *hostname, int hostlen)
{
        int32_t ret = -1;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", hostname, out);

        if (hostlen < (strlen (this->myinfo.identifier) + 1)) {
                goto out;
        }

        strcpy (hostname, this->myinfo.identifier);
        ret = 0;
out:
        return ret;
}


int32_t
socket_getmyaddr (rpc_transport_t *this, char *myaddr, int addrlen,
                  struct sockaddr_storage *sa, socklen_t salen)
{
        int32_t ret = 0;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", sa, out);

        *sa =  this->myinfo.sockaddr;

        if (myaddr != NULL) {
                ret = socket_getmyname (this, myaddr, addrlen);
        }

out:
        return ret;
}


struct rpc_transport_ops tops = {
        .listen             = socket_listen,
        .connect            = socket_connect,
        .disconnect         = socket_disconnect,
        .submit_request     = socket_submit_request,
        .submit_reply       = socket_submit_reply,
        .get_peername       = socket_getpeername,
        .get_peeraddr       = socket_getpeeraddr,
        .get_myname         = socket_getmyname,
        .get_myaddr         = socket_getmyaddr,
};

int
reconfigure (rpc_transport_t *this, dict_t *options)
{
        socket_private_t *priv = NULL;
        gf_boolean_t      tmp_bool = _gf_false;
        char             *optstr = NULL;
        int               ret = 0;

        GF_VALIDATE_OR_GOTO ("socket", this, out);
        GF_VALIDATE_OR_GOTO ("socket", this->private, out);

        if (!this || !this->private) {
                ret =-1;
                goto out;
        }

        priv = this->private;

        if (dict_get_str (this->options, "transport.socket.keepalive",
                          &optstr) == 0) {
                if (gf_string2boolean (optstr, &tmp_bool) == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'transport.socket.keepalive' takes only "
                                "boolean options, not taking any action");
                        priv->keepalive = 1;
                        ret = -1;
                        goto out;
                }
                gf_log (this->name, GF_LOG_DEBUG, "Reconfigured transport.socket.keepalive");

                priv->keepalive = tmp_bool;
        }
        else
                priv->keepalive = 1;
        ret = 0;
out:
        return ret;

}

int
socket_init (rpc_transport_t *this)
{
        socket_private_t *priv = NULL;
        gf_boolean_t      tmp_bool = 0;
        uint64_t          windowsize = GF_DEFAULT_SOCKET_WINDOW_SIZE;
        char             *optstr = NULL;
        uint32_t          keepalive = 0;
        uint32_t          backlog = 0;

        if (this->private) {
                gf_log_callingfn (this->name, GF_LOG_ERROR,
                                  "double init attempted");
                return -1;
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_common_mt_socket_private_t);
        if (!priv) {
                return -1;
        }

        pthread_mutex_init (&priv->lock, NULL);

        priv->sock = -1;
        priv->idx = -1;
        priv->connected = -1;
        priv->nodelay = 1;
        priv->bio = 0;
        priv->windowsize = GF_DEFAULT_SOCKET_WINDOW_SIZE;
        INIT_LIST_HEAD (&priv->ioq);

        /* All the below section needs 'this->options' to be present */
        if (!this->options)
                goto out;

        if (dict_get (this->options, "non-blocking-io")) {
                optstr = data_to_str (dict_get (this->options,
                                                "non-blocking-io"));

                if (gf_string2boolean (optstr, &tmp_bool) == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'non-blocking-io' takes only boolean options,"
                                " not taking any action");
                        tmp_bool = 1;
                }

                if (!tmp_bool) {
                        priv->bio = 1;
                        gf_log (this->name, GF_LOG_WARNING,
                                "disabling non-blocking IO");
                }
        }

        optstr = NULL;

        // By default, we enable NODELAY
        if (dict_get (this->options, "transport.socket.nodelay")) {
                optstr = data_to_str (dict_get (this->options,
                                                "transport.socket.nodelay"));

                if (gf_string2boolean (optstr, &tmp_bool) == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'transport.socket.nodelay' takes only "
                                "boolean options, not taking any action");
                        tmp_bool = 1;
                }
                if (!tmp_bool) {
                        priv->nodelay = 0;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "disabling nodelay");
                }
        }


        optstr = NULL;
        if (dict_get_str (this->options, "transport.window-size",
                          &optstr) == 0) {
                if (gf_string2bytesize (optstr, &windowsize) != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "invalid number format: %s", optstr);
                        return -1;
                }
        }

        optstr = NULL;

        /* Enable Keep-alive by default. */
        priv->keepalive = 1;
        priv->keepaliveintvl = 2;
        priv->keepaliveidle = 20;
        if (dict_get_str (this->options, "transport.socket.keepalive",
                          &optstr) == 0) {
                if (gf_string2boolean (optstr, &tmp_bool) == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'transport.socket.keepalive' takes only "
                                "boolean options, not taking any action");
                        tmp_bool = 1;
                }

                if (!tmp_bool)
                        priv->keepalive = 0;
        }

        if (dict_get_uint32 (this->options,
                             "transport.socket.keepalive-interval",
                             &keepalive) == 0) {
                priv->keepaliveintvl = keepalive;
        }

        if (dict_get_uint32 (this->options,
                             "transport.socket.keepalive-time",
                             &keepalive) == 0) {
                priv->keepaliveidle = keepalive;
        }

        if (dict_get_uint32 (this->options,
                             "transport.socket.listen-backlog",
                             &backlog) == 0) {
                priv->backlog = backlog;
        }

        optstr = NULL;

         /* Check if socket read failures are to be logged */
        priv->read_fail_log = 1;
        if (dict_get (this->options, "transport.socket.read-fail-log")) {
                optstr = data_to_str (dict_get (this->options, "transport.socket.read-fail-log"));
                if (gf_string2boolean (optstr, &tmp_bool) == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                   "'transport.socket.read-fail-log' takes only "
                                   "boolean options; logging socket read fails");
                }
                else if (tmp_bool == _gf_false) {
                        priv->read_fail_log = 0;
                }
        }

        priv->windowsize = (int)windowsize;
out:
        this->private = priv;

        return 0;
}


void
fini (rpc_transport_t *this)
{
        socket_private_t *priv = NULL;

        if (!this)
                return;

        priv = this->private;
        if (priv) {
                if (priv->sock != -1) {
                        pthread_mutex_lock (&priv->lock);
                        {
                                __socket_ioq_flush (this);
                                __socket_reset (this);
                        }
                        pthread_mutex_unlock (&priv->lock);
                }
                gf_log (this->name, GF_LOG_TRACE,
                        "transport %p destroyed", this);

                pthread_mutex_destroy (&priv->lock);
                GF_FREE (priv);
        }

        this->private = NULL;
}


int32_t
init (rpc_transport_t *this)
{
        int ret = -1;

        ret = socket_init (this);

        if (ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "socket_init() failed");
        }

        return ret;
}

struct volume_options options[] = {
        { .key   = {"remote-port",
                    "transport.remote-port",
                    "transport.socket.remote-port"},
          .type  = GF_OPTION_TYPE_INT
        },
        { .key   = {"transport.socket.listen-port", "listen-port"},
          .type  = GF_OPTION_TYPE_INT
        },
        { .key   = {"transport.socket.bind-address", "bind-address" },
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS
        },
        { .key   = {"transport.socket.connect-path", "connect-path"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"transport.socket.bind-path", "bind-path"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"transport.socket.listen-path", "listen-path"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = { "transport.address-family",
                     "address-family" },
          .value = {"inet", "inet6", "inet/inet6", "inet6/inet",
                    "unix", "inet-sdp" },
          .type  = GF_OPTION_TYPE_STR
        },

        { .key   = {"non-blocking-io"},
          .type  = GF_OPTION_TYPE_BOOL
        },
        { .key   = {"transport.window-size"},
          .type  = GF_OPTION_TYPE_SIZET,
          .min   = GF_MIN_SOCKET_WINDOW_SIZE,
          .max   = GF_MAX_SOCKET_WINDOW_SIZE,
        },
        { .key   = {"transport.socket.nodelay"},
          .type  = GF_OPTION_TYPE_BOOL
        },
        { .key   = {"transport.socket.lowlat"},
          .type  = GF_OPTION_TYPE_BOOL
        },
        { .key   = {"transport.socket.keepalive"},
          .type  = GF_OPTION_TYPE_BOOL
        },
        { .key   = {"transport.socket.keepalive-interval"},
          .type  = GF_OPTION_TYPE_INT
        },
        { .key   = {"transport.socket.keepalive-time"},
          .type  = GF_OPTION_TYPE_INT
        },
        { .key   = {"transport.socket.listen-backlog"},
          .type  = GF_OPTION_TYPE_INT
        },
        { .key   = {"transport.socket.read-fail-log"},
          .type  = GF_OPTION_TYPE_BOOL
        },
        { .key = {NULL} }
};
