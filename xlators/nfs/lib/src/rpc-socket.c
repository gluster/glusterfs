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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpc-socket.h"
#include "rpcsvc.h"
#include "dict.h"
#include "logging.h"
#include "byte-order.h"
#include "common-utils.h"
#include "compat-errno.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0
#endif /* AI_ADDRCONFIG */

static int
nfs_rpcsvc_socket_server_get_local_socket (int addrfam, char *listenhost,
                                           uint16_t listenport,
                                           struct sockaddr *addr,
                                           socklen_t *addr_len)
{
        struct addrinfo         hints, *res = 0;
        char                    service[NI_MAXSERV];
        int                     ret = -1;

        memset (service, 0, sizeof (service));
        sprintf (service, "%d", listenport);

        memset (&hints, 0, sizeof (hints));
        addr->sa_family = hints.ai_family = addrfam;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags    = AI_ADDRCONFIG | AI_PASSIVE;

        ret = getaddrinfo(listenhost, service, &hints, &res);
        if (ret != 0) {
                gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR,
                        "getaddrinfo failed for host %s, service %s (%s)",
                        listenhost, service, gai_strerror (ret));
                ret = -1;
                goto err;
        }

        memcpy (addr, res->ai_addr, res->ai_addrlen);
        *addr_len = res->ai_addrlen;

        freeaddrinfo (res);
        ret = 0;

err:
        return ret;
}


int
nfs_rpcsvc_socket_listen (int addrfam, char *listenhost, uint16_t listenport)
{
        int                     sock = -1;
        struct sockaddr_storage sockaddr;
        socklen_t               sockaddr_len;
        int                     flags = 0;
        int                     ret = -1;
        int                     opt = 1;

        ret = nfs_rpcsvc_socket_server_get_local_socket (addrfam, listenhost,
                                                         listenport,
                                                         SA (&sockaddr),
                                                         &sockaddr_len);

        if (ret == -1)
                return ret;

        sock = socket (SA (&sockaddr)->sa_family, SOCK_STREAM, 0);
        if (sock == -1) {
                gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR, "socket creation failed"
                        " (%s)", strerror (errno));
                goto err;
        }

        flags = fcntl (sock, F_GETFL);
        if (flags == -1) {
                gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR, "cannot get socket flags"
                        " (%s)", strerror(errno));
                goto close_err;
        }

        ret = fcntl (sock, F_SETFL, flags | O_NONBLOCK);
        if (ret == -1) {
                gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR, "cannot set socket "
                        "non-blocking (%s)", strerror (errno));
                goto close_err;
        }

        ret = setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));
        if (ret == -1) {
                gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR, "setsockopt() for "
                        "SO_REUSEADDR failed (%s)", strerror (errno));
                goto close_err;
        }

        ret = bind (sock, (struct sockaddr *)&sockaddr, sockaddr_len);
        if (ret == -1) {
                if (errno != EADDRINUSE) {
                        gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR, "binding socket "
                                "failed: %s", strerror (errno));
                        goto close_err;
                }
        }

        ret = listen (sock, 10);
        if (ret == -1) {
                gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR, "could not listen on"
                        " socket (%s)", strerror (errno));
                goto close_err;
        }

        return sock;

close_err:
        close (sock);
        sock = -1;

err:
        return sock;
}


int
nfs_rpcsvc_socket_accept (int listenfd)
{
        int                     new_sock = -1;
        struct sockaddr_storage new_sockaddr = {0, };
        socklen_t               addrlen = sizeof (new_sockaddr);
        int                     flags = 0;
        int                     ret = -1;
        int                     on = 1;

        new_sock = accept (listenfd, SA (&new_sockaddr), &addrlen);
        if (new_sock == -1) {
                gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR,"accept on socket failed");
                goto err;
        }

        flags = fcntl (new_sock, F_GETFL);
        if (flags == -1) {
                gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR, "cannot get socket flags"
                        " (%s)", strerror(errno));
                goto close_err;
        }

        ret = fcntl (new_sock, F_SETFL, flags | O_NONBLOCK);
        if (ret == -1) {
                gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR, "cannot set socket "
                        "non-blocking (%s)", strerror (errno));
                goto close_err;
        }

#ifdef TCP_NODELAY
        ret = setsockopt(new_sock, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        if (ret == -1) {
                gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR, "cannot set no-delay "
                        " socket option");
        }
#endif

        return new_sock;

close_err:
        close (new_sock);
        new_sock = -1;

err:
        return new_sock;
}

ssize_t
nfs_rpcsvc_socket_read (int sockfd, char *readaddr, size_t readsize)
{
        ssize_t         dataread = 0;
        ssize_t         readlen = -1;

        if (!readaddr)
                return -1;

        while (readsize > 0) {
                readlen = read (sockfd, readaddr, readsize);
                if (readlen == -1) {
                        if (errno != EAGAIN) {
                                dataread = -1;
                                break;
                        } else
                                break;
                } else if (readlen == 0)
                        break;

                dataread += readlen;
                readaddr += readlen;
                readsize -= readlen;
        }

        return dataread;
}


ssize_t
nfs_rpcsvc_socket_write (int sockfd, char *buffer, size_t size, int *eagain)
{
        size_t          writelen = -1;
        ssize_t         written = 0;

        if (!buffer)
                return -1;

        while (size > 0) {
                writelen = write (sockfd, buffer, size);
                if (writelen == -1) {
                        if (errno != EAGAIN) {
                                written = -1;
                                break;
                        } else {
                                *eagain = 1;
                                break;
                        }
                } else if (writelen == 0)
                        break;

                written += writelen;
                size -= writelen;
                buffer += writelen;
        }

        return written;
}


int
nfs_rpcsvc_socket_peername (int sockfd, char *hostname, int hostlen)
{
        struct sockaddr         sa;
        socklen_t               sl = sizeof (sa);
        int                     ret = EAI_FAIL;

        if (!hostname)
                return ret;

        ret = getpeername (sockfd, &sa, &sl);
        if (ret == -1) {
                gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR, "Failed to get peer name:"
                        " %s", strerror (errno));
                ret = EAI_FAIL;
                goto err;
        }

        ret = getnameinfo (&sa, sl, hostname, hostlen, NULL, 0, 0);
        if (ret != 0)
                goto err;

err:
        return ret;
}


int
nfs_rpcsvc_socket_peeraddr (int sockfd, char *addrstr, int addrlen,
                            struct sockaddr *returnsa, socklen_t sasize)
{
        struct sockaddr         sa;
        int                     ret = EAI_FAIL;

        if (returnsa)
                ret = getpeername (sockfd, returnsa, &sasize);
        else {
                sasize = sizeof (sa);
                ret = getpeername (sockfd, &sa, &sasize);
        }

        if (ret == -1) {
                gf_log (GF_RPCSVC_SOCK, GF_LOG_ERROR, "Failed to get peer addr:"
                        " %s", strerror (errno));
                ret = EAI_FAIL;
                goto err;
        }

        /* If caller did not specify a string into which the address can be
         * stored, dont bother getting it.
         */
        if (!addrstr) {
                ret = 0;
                goto err;
        }

        if (returnsa)
                ret = getnameinfo (returnsa, sasize, addrstr, addrlen, NULL, 0,
                                   NI_NUMERICHOST);
        else
                ret = getnameinfo (&sa, sasize, addrstr, addrlen, NULL, 0,
                                   NI_NUMERICHOST);

err:
        return ret;
}


int
nfs_rpcsvc_socket_block_tx (int sockfd)
{

        int     ret = -1;
        int     on = 1;

#ifdef TCP_CORK
        ret = setsockopt(sockfd, IPPROTO_TCP, TCP_CORK, &on, sizeof(on));
#endif

#ifdef TCP_NOPUSH
        ret = setsockopt(sockfd, IPPROTO_TCP, TCP_NOPUSH, &on, sizeof(on));
#endif

        return ret;
}


int
nfs_rpcsvc_socket_unblock_tx (int sockfd)
{
        int     ret = -1;
        int     off = 0;

#ifdef TCP_CORK
        ret = setsockopt(sockfd, IPPROTO_TCP, TCP_CORK, &off, sizeof(off));
#endif

#ifdef TCP_NOPUSH
        ret = setsockopt(sockfd, IPPROTO_TCP, TCP_NOPUSH, &off, sizeof(off));
#endif
        return ret;
}

