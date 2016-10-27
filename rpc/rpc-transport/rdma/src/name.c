/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <rdma/rdma_cma.h>

#ifndef AF_INET_SDP
#define AF_INET_SDP 27
#endif

#include "rpc-transport.h"
#include "rdma.h"
#include "common-utils.h"
#include "rpc-lib-messages.h"
#include "rpc-trans-rdma-messages.h"


int32_t
gf_resolve_ip6 (const char *hostname,
                uint16_t port,
                int family,
                void **dnscache,
                struct addrinfo **addr_info);


static void
_assign_port (struct sockaddr *sockaddr, uint16_t port)
{
        switch (sockaddr->sa_family) {
        case AF_INET6:
                ((struct sockaddr_in6 *)sockaddr)->sin6_port = htons (port);
                break;

        case AF_INET_SDP:
        case AF_INET:
                ((struct sockaddr_in *)sockaddr)->sin_port = htons (port);
                break;
        }
}

static int32_t
af_inet_bind_to_port_lt_ceiling (struct rdma_cm_id *cm_id,
                                 struct sockaddr *sockaddr,
                                 socklen_t sockaddr_len, uint32_t ceiling)
{
        int32_t         ret                             = -1;
        uint16_t        port                            = ceiling - 1;
        unsigned char   ports[GF_PORT_ARRAY_SIZE]       = {0,};
        int             i                               = 0;

loop:
        ret = gf_process_reserved_ports (ports, ceiling);

        while (port) {
                if (port == GF_CLIENT_PORT_CEILING) {
                        ret = -1;
                        break;
                }

                /* ignore the reserved ports */
                if (BIT_VALUE (ports, port)) {
                        port--;
                        continue;
                }

                _assign_port (sockaddr, port);

                ret = rdma_bind_addr (cm_id, sockaddr);

                if (ret == 0)
                        break;

                if (ret == -1 && errno == EACCES)
                        break;

                port--;
        }

        /* Incase if all the secure ports are exhausted, we are no more
         * binding to secure ports, hence instead of getting a random
         * port, lets define the range to restrict it from getting from
         * ports reserved for bricks i.e from range of 49152 - 65535
         * which further may lead to port clash */
        if (!port) {
                ceiling = port = GF_CLNT_INSECURE_PORT_CEILING;
                for (i = 0; i <= ceiling; i++)
                        BIT_CLEAR (ports, i);
                goto loop;
        }

        return ret;
}

#if 0
static int32_t
af_unix_client_bind (rpc_transport_t *this, struct sockaddr *sockaddr,
                     socklen_t sockaddr_len, struct rdma_cm_id *cm_id)
{
        data_t *path_data = NULL;
        struct sockaddr_un *addr = NULL;
        int32_t ret = -1;

        path_data = dict_get (this->options,
                              "transport.rdma.bind-path");
        if (path_data) {
                char *path = data_to_str (path_data);
                if (!path || strlen (path) > UNIX_PATH_MAX) {
                        gf_msg_debug (this->name, 0,
                                      "transport.rdma.bind-path not specified "
                                      "for unix socket, letting connect to "
                                      "assign default value");
                        goto err;
                }

                addr = (struct sockaddr_un *) sockaddr;
                strcpy (addr->sun_path, path);
                ret = bind (sock, (struct sockaddr *)addr, sockaddr_len);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                TRANS_MSG_SOCKET_BIND_ERROR,
                                "cannot bind to unix-domain socket %d ",
                                sock);
                        goto err;
                }
        }

err:
        return ret;
}
#endif

static int32_t
client_fill_address_family (rpc_transport_t *this, struct sockaddr *sockaddr)
{
        data_t *address_family_data = NULL;

        address_family_data = dict_get (this->options,
                                        "transport.address-family");
        if (!address_family_data) {
                data_t *remote_host_data = NULL, *connect_path_data = NULL;
                remote_host_data = dict_get (this->options, "remote-host");
                connect_path_data = dict_get (this->options,
                                              "transport.rdma.connect-path");

                if (!(remote_host_data || connect_path_data) ||
                    (remote_host_data && connect_path_data)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                TRANS_MSG_ADDR_FAMILY_NOT_SPECIFIED,
                                "address-family not specified and not able to "
                                "determine the same from other options "
                                "(remote-host:%s and connect-path:%s)",
                                data_to_str (remote_host_data),
                                data_to_str (connect_path_data));
                        return -1;
                }

                if (remote_host_data) {
                        gf_msg_debug (this->name, 0, "address-family not "
                                      "specified, guessing it to be "
                                      "inet/inet6");
                        sockaddr->sa_family = AF_UNSPEC;
                } else {
                        gf_msg_debug (this->name, 0, "address-family not "
                                      "specified, guessing it to be unix");
                        sockaddr->sa_family = AF_UNIX;
                }

        } else {
                char *address_family = data_to_str (address_family_data);
                if (!strcasecmp (address_family, "unix")) {
                        sockaddr->sa_family = AF_UNIX;
                } else if (!strcasecmp (address_family, "inet")) {
                        sockaddr->sa_family = AF_INET;
                } else if (!strcasecmp (address_family, "inet6")) {
                        sockaddr->sa_family = AF_INET6;
                } else if (!strcasecmp (address_family, "inet-sdp")) {
                        sockaddr->sa_family = AF_INET_SDP;
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                TRANS_MSG_UNKNOWN_ADDR_FAMILY,
                                "unknown address-family (%s) specified",
                                address_family);
                        sockaddr->sa_family = AF_UNSPEC;
                        return -1;
                }
        }

        return 0;
}

static int32_t
af_inet_client_get_remote_sockaddr (rpc_transport_t *this,
                                    struct sockaddr *sockaddr,
                                    socklen_t *sockaddr_len,
                                    int16_t remote_port)
{
        dict_t *options = this->options;
        data_t *remote_host_data = NULL;
        data_t *remote_port_data = NULL;
        char *remote_host = NULL;
        struct addrinfo *addr_info = NULL;
        int32_t ret = 0;

        remote_host_data = dict_get (options, "remote-host");
        if (remote_host_data == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        TRANS_MSG_REMOTE_HOST_ERROR, "option remote-host "
                        "missing in volume %s", this->name);
                ret = -1;
                goto err;
        }

        remote_host = data_to_str (remote_host_data);
        if (remote_host == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        TRANS_MSG_REMOTE_HOST_ERROR, "option remote-host "
                        "has data NULL in volume %s", this->name);
                ret = -1;
                goto err;
        }

        if (remote_port == 0) {
                remote_port_data = dict_get (options, "remote-port");
                if (remote_port_data == NULL) {
                        gf_msg_debug (this->name, 0, "option remote-port "
                                      "missing in volume %s. Defaulting to %d",
                                      this->name, GF_DEFAULT_RDMA_LISTEN_PORT);

                        remote_port = GF_DEFAULT_RDMA_LISTEN_PORT;
                } else {
                        remote_port = data_to_uint16 (remote_port_data);
                }
        }

        if (remote_port == -1) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        RDMA_MSG_INVALID_ENTRY, "option remote-port has "
                        "invalid port in volume %s", this->name);
                ret = -1;
                goto err;
        }

        /* TODO: gf_resolve is a blocking call. kick in some
           non blocking dns techniques */
        ret = gf_resolve_ip6 (remote_host, remote_port,
                              sockaddr->sa_family,
                              &this->dnscache, &addr_info);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0, TRANS_MSG_DNS_RESOL_FAILED,
                        "DNS resolution failed on host %s", remote_host);
                goto err;
        }

        memcpy (sockaddr, addr_info->ai_addr, addr_info->ai_addrlen);
        *sockaddr_len = addr_info->ai_addrlen;

err:
        return ret;
}

static int32_t
af_unix_client_get_remote_sockaddr (rpc_transport_t *this,
                                    struct sockaddr *sockaddr,
                                    socklen_t *sockaddr_len)
{
        struct sockaddr_un *sockaddr_un = NULL;
        char *connect_path = NULL;
        data_t *connect_path_data = NULL;
        int32_t ret = 0;

        connect_path_data = dict_get (this->options,
                                      "transport.rdma.connect-path");
        if (!connect_path_data) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        TRANS_MSG_CONNECT_PATH_ERROR, "option "
                        "transport.rdma.connect-path not specified for "
                        "address-family unix");
                ret = -1;
                goto err;
        }

        connect_path = data_to_str (connect_path_data);
        if (!connect_path) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        RDMA_MSG_INVALID_ENTRY, "connect-path is null-string");
                ret = -1;
                goto err;
        }

        if (strlen (connect_path) > UNIX_PATH_MAX) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        TRANS_MSG_CONNECT_PATH_ERROR,
                        "connect-path value length %"GF_PRI_SIZET" > "
                        "%d octets", strlen (connect_path), UNIX_PATH_MAX);
                ret = -1;
                goto err;
        }

        gf_msg_debug (this->name, 0, "using connect-path %s", connect_path);
        sockaddr_un = (struct sockaddr_un *)sockaddr;
        strcpy (sockaddr_un->sun_path, connect_path);
        *sockaddr_len = sizeof (struct sockaddr_un);

err:
        return ret;
}

static int32_t
af_unix_server_get_local_sockaddr (rpc_transport_t *this,
                                   struct sockaddr *addr,
                                   socklen_t *addr_len)
{
        data_t *listen_path_data = NULL;
        char *listen_path = NULL;
        int32_t ret = 0;
        struct sockaddr_un *sunaddr = (struct sockaddr_un *)addr;


        listen_path_data = dict_get (this->options,
                                     "transport.rdma.listen-path");
        if (!listen_path_data) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        TRANS_MSG_LISTEN_PATH_ERROR,
                        "missing option listen-path");
                ret = -1;
                goto err;
        }

        listen_path = data_to_str (listen_path_data);

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

        if (strlen (listen_path) > UNIX_PATH_MAX) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        TRANS_MSG_LISTEN_PATH_ERROR, "option listen-path has "
                        "value length %"GF_PRI_SIZET" > %d",
                        strlen (listen_path), UNIX_PATH_MAX);
                ret = -1;
                goto err;
        }

        sunaddr->sun_family = AF_UNIX;
        strcpy (sunaddr->sun_path, listen_path);
        *addr_len = sizeof (struct sockaddr_un);

err:
        return ret;
}

static int32_t
af_inet_server_get_local_sockaddr (rpc_transport_t *this,
                                   struct sockaddr *addr,
                                   socklen_t *addr_len)
{
        struct addrinfo hints, *res = 0;
        data_t *listen_port_data = NULL, *listen_host_data = NULL;
        uint16_t listen_port = -1;
        char service[NI_MAXSERV], *listen_host = NULL;
        dict_t *options = NULL;
        int32_t ret = 0;

        options = this->options;

        listen_port_data = dict_get (options, "transport.rdma.listen-port");
        listen_host_data = dict_get (options,
                                     "transport.rdma.bind-address");

        if (listen_port_data) {
                listen_port = data_to_uint16 (listen_port_data);
        } else {
                listen_port = GF_DEFAULT_RDMA_LISTEN_PORT;

                if (addr->sa_family == AF_INET6) {
                        struct sockaddr_in6 *in = (struct sockaddr_in6 *) addr;
                        in->sin6_addr = in6addr_any;
                        in->sin6_port = htons(listen_port);
                        *addr_len = sizeof(struct sockaddr_in6);
                        goto out;
                } else if (addr->sa_family == AF_INET) {
                        struct sockaddr_in *in = (struct sockaddr_in *) addr;
                        in->sin_addr.s_addr = htonl(INADDR_ANY);
                        in->sin_port = htons(listen_port);
                        *addr_len = sizeof(struct sockaddr_in);
                        goto out;
                }
        }

        if (listen_port == (uint16_t) -1)
                listen_port = GF_DEFAULT_RDMA_LISTEN_PORT;


        if (listen_host_data) {
                listen_host = data_to_str (listen_host_data);
        }

        memset (service, 0, sizeof (service));
        sprintf (service, "%d", listen_port);

        memset (&hints, 0, sizeof (hints));
        hints.ai_family = addr->sa_family;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags    = AI_ADDRCONFIG | AI_PASSIVE;

        ret = getaddrinfo(listen_host, service, &hints, &res);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, ret,
                        TRANS_MSG_GET_ADDR_INFO_FAILED,
                        "getaddrinfo failed for host %s, service %s",
                        listen_host, service);
                ret = -1;
                goto out;
        }

        memcpy (addr, res->ai_addr, res->ai_addrlen);
        *addr_len = res->ai_addrlen;

        freeaddrinfo (res);

out:
        return ret;
}

int32_t
gf_rdma_client_bind (rpc_transport_t *this, struct sockaddr *sockaddr,
                     socklen_t *sockaddr_len, struct rdma_cm_id *cm_id)
{
        int ret = 0;

        *sockaddr_len = sizeof (struct sockaddr_in6);
        switch (sockaddr->sa_family) {
        case AF_INET_SDP:
        case AF_INET:
                *sockaddr_len = sizeof (struct sockaddr_in);

        case AF_INET6:
                if (!this->bind_insecure) {
                        ret = af_inet_bind_to_port_lt_ceiling (cm_id, sockaddr,
                                                               *sockaddr_len,
                                                               GF_CLIENT_PORT_CEILING);
                        if (ret == -1) {
                                gf_msg (this->name, GF_LOG_WARNING, errno,
                                        RDMA_MSG_PORT_BIND_FAILED,
                                        "cannot bind rdma_cm_id to port "
                                        "less than %d", GF_CLIENT_PORT_CEILING);
                        }
                } else {
                        ret = af_inet_bind_to_port_lt_ceiling (cm_id, sockaddr,
                                                               *sockaddr_len,
                                                               GF_IANA_PRIV_PORTS_START);
                        if (ret == -1) {
                                gf_msg (this->name, GF_LOG_WARNING, errno,
                                        RDMA_MSG_PORT_BIND_FAILED,
                                        "cannot bind rdma_cm_id to port "
                                        "less than %d",
                                        GF_IANA_PRIV_PORTS_START);
                        }
                }
                break;

        case AF_UNIX:
                *sockaddr_len = sizeof (struct sockaddr_un);
#if 0
                ret = af_unix_client_bind (this, (struct sockaddr *)sockaddr,
                                           *sockaddr_len, sock);
#endif
                break;

        default:
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        TRANS_MSG_UNKNOWN_ADDR_FAMILY,
                        "unknown address family %d", sockaddr->sa_family);
                ret = -1;
                break;
        }

        return ret;
}

int32_t
gf_rdma_client_get_remote_sockaddr (rpc_transport_t *this,
                                    struct sockaddr *sockaddr,
                                    socklen_t *sockaddr_len,
                                    int16_t remote_port)
{
        int32_t ret = 0;
        char is_inet_sdp = 0;

        ret = client_fill_address_family (this, sockaddr);
        if (ret) {
                ret = -1;
                goto err;
        }

        switch (sockaddr->sa_family) {
        case AF_INET_SDP:
                sockaddr->sa_family = AF_INET;
                is_inet_sdp = 1;

        case AF_INET:
        case AF_INET6:
        case AF_UNSPEC:
                ret = af_inet_client_get_remote_sockaddr (this,
                                                          sockaddr,
                                                          sockaddr_len,
                                                          remote_port);

                if (is_inet_sdp) {
                        sockaddr->sa_family = AF_INET_SDP;
                }

                break;

        case AF_UNIX:
                ret = af_unix_client_get_remote_sockaddr (this,
                                                          sockaddr,
                                                          sockaddr_len);
                break;

        default:
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        TRANS_MSG_UNKNOWN_ADDR_FAMILY,
                        "unknown address-family %d", sockaddr->sa_family);
                ret = -1;
        }

err:
        return ret;
}

int32_t
gf_rdma_server_get_local_sockaddr (rpc_transport_t *this,
                                   struct sockaddr *addr,
                                   socklen_t *addr_len)
{
        data_t *address_family_data = NULL;
        int32_t ret = 0;
        char is_inet_sdp = 0;

        address_family_data = dict_get (this->options,
                                        "transport.address-family");
        if (address_family_data) {
                char *address_family = NULL;
                address_family = data_to_str (address_family_data);

                if (!strcasecmp (address_family, "inet")) {
                        addr->sa_family = AF_INET;
                } else if (!strcasecmp (address_family, "inet6")) {
                        addr->sa_family = AF_INET6;
                } else if (!strcasecmp (address_family, "inet-sdp")) {
                        addr->sa_family = AF_INET_SDP;
                } else if (!strcasecmp (address_family, "unix")) {
                        addr->sa_family = AF_UNIX;
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                TRANS_MSG_UNKNOWN_ADDR_FAMILY, "unknown address"
                                " family (%s) specified", address_family);
                        addr->sa_family = AF_UNSPEC;
                        ret = -1;
                        goto err;
                }
        } else {
                gf_msg_debug (this->name, 0, "option address-family not "
                              "specified, defaulting to inet");
                addr->sa_family = AF_INET;
        }

        switch (addr->sa_family) {
        case AF_INET_SDP:
                is_inet_sdp = 1;
                addr->sa_family = AF_INET;

        case AF_INET:
        case AF_INET6:
        case AF_UNSPEC:
                ret = af_inet_server_get_local_sockaddr (this, addr, addr_len);
                if (is_inet_sdp && !ret) {
                        addr->sa_family = AF_INET_SDP;
                }
                break;

        case AF_UNIX:
                ret = af_unix_server_get_local_sockaddr (this, addr, addr_len);
                break;
        }

err:
        return ret;
}

int32_t
fill_inet6_inet_identifiers (rpc_transport_t *this, struct sockaddr_storage *addr,
                             int32_t addr_len, char *identifier)
{
        int32_t ret = 0, tmpaddr_len = 0;
        char service[NI_MAXSERV], host[NI_MAXHOST];
        union gf_sock_union sock_union;

        memset (&sock_union, 0, sizeof (sock_union));
        sock_union.storage = *addr;
        tmpaddr_len = addr_len;

        if (sock_union.sa.sa_family == AF_INET6) {
                int32_t one_to_four, four_to_eight, twelve_to_sixteen;
                int16_t eight_to_ten, ten_to_twelve;

                one_to_four = four_to_eight = twelve_to_sixteen = 0;
                eight_to_ten = ten_to_twelve = 0;

                one_to_four = sock_union.sin6.sin6_addr.s6_addr32[0];
                four_to_eight = sock_union.sin6.sin6_addr.s6_addr32[1];
#ifdef GF_SOLARIS_HOST_OS
                eight_to_ten = S6_ADDR16(sock_union.sin6.sin6_addr)[4];
#else
                eight_to_ten = sock_union.sin6.sin6_addr.s6_addr16[4];
#endif

#ifdef GF_SOLARIS_HOST_OS
                ten_to_twelve = S6_ADDR16(sock_union.sin6.sin6_addr)[5];
#else
                ten_to_twelve = sock_union.sin6.sin6_addr.s6_addr16[5];
#endif
                twelve_to_sixteen = sock_union.sin6.sin6_addr.s6_addr32[3];

                /* ipv4 mapped ipv6 address has
                   bits 0-80: 0
                   bits 80-96: 0xffff
                   bits 96-128: ipv4 address
                */

                if (one_to_four == 0 &&
                    four_to_eight == 0 &&
                    eight_to_ten == 0 &&
                    ten_to_twelve == -1) {
                        struct sockaddr_in *in_ptr = &sock_union.sin;
                        memset (&sock_union, 0, sizeof (sock_union));

                        in_ptr->sin_family = AF_INET;
                        in_ptr->sin_port = ((struct sockaddr_in6 *)addr)->sin6_port;
                        in_ptr->sin_addr.s_addr = twelve_to_sixteen;
                        tmpaddr_len = sizeof (*in_ptr);
                }
        }

        ret = getnameinfo (&sock_union.sa,
                           tmpaddr_len,
                           host, sizeof (host),
                           service, sizeof (service),
                           NI_NUMERICHOST | NI_NUMERICSERV);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, ret,
                        TRANS_MSG_GET_NAME_INFO_FAILED,
                        "getnameinfo failed");
        }

        sprintf (identifier, "%s:%s", host, service);

        return ret;
}

int32_t
gf_rdma_get_transport_identifiers (rpc_transport_t *this)
{
        int32_t ret = 0;
        char is_inet_sdp = 0;

        switch (((struct sockaddr *) &this->myinfo.sockaddr)->sa_family) {
        case AF_INET_SDP:
                is_inet_sdp = 1;
                ((struct sockaddr *) &this->peerinfo.sockaddr)->sa_family = ((struct sockaddr *) &this->myinfo.sockaddr)->sa_family = AF_INET;

        case AF_INET:
        case AF_INET6: {
                ret = fill_inet6_inet_identifiers (this,
                                                   &this->myinfo.sockaddr,
                                                   this->myinfo.sockaddr_len,
                                                   this->myinfo.identifier);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                TRANS_MSG_INET_ERROR,
                                "can't fill inet/inet6 identifier for server");
                        goto err;
                }

                ret = fill_inet6_inet_identifiers (this,
                                                   &this->peerinfo.sockaddr,
                                                   this->peerinfo.sockaddr_len,
                                                   this->peerinfo.identifier);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                TRANS_MSG_INET_ERROR,
                                "can't fill inet/inet6 identifier for client");
                        goto err;
                }

                if (is_inet_sdp) {
                        ((struct sockaddr *) &this->peerinfo.sockaddr)->sa_family = ((struct sockaddr *) &this->myinfo.sockaddr)->sa_family = AF_INET_SDP;
                }
        }
        break;

        case AF_UNIX:
        {
                struct sockaddr_un *sunaddr = NULL;

                sunaddr = (struct sockaddr_un *) &this->myinfo.sockaddr;
                strcpy (this->myinfo.identifier, sunaddr->sun_path);

                sunaddr = (struct sockaddr_un *) &this->peerinfo.sockaddr;
                strcpy (this->peerinfo.identifier, sunaddr->sun_path);
        }
        break;

        default:
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        TRANS_MSG_UNKNOWN_ADDR_FAMILY,
                        "unknown address family (%d)",
                        ((struct sockaddr *) &this->myinfo.sockaddr)->sa_family);
                ret = -1;
                break;
        }

err:
        return ret;
}
