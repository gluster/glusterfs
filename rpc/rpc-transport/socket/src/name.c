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
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>

#ifndef AF_INET_SDP
#define AF_INET_SDP 27
#endif

#include "rpc-transport.h"
#include "socket.h"

static void
_assign_port(struct sockaddr *sockaddr, uint16_t port)
{
    switch (sockaddr->sa_family) {
        case AF_INET6:
            ((struct sockaddr_in6 *)sockaddr)->sin6_port = htons(port);
            break;

        case AF_INET_SDP:
        case AF_INET:
            ((struct sockaddr_in *)sockaddr)->sin_port = htons(port);
            break;
    }
}

static int32_t
af_inet_bind_to_port_lt_ceiling(int fd, struct sockaddr *sockaddr,
                                socklen_t sockaddr_len, uint32_t ceiling)
{
#if GF_DISABLE_PRIVPORT_TRACKING
    _assign_port(sockaddr, 0);
    return bind(fd, sockaddr, sockaddr_len);
#else
    int32_t ret = -1;
    uint16_t port = ceiling - 1;
    unsigned char ports[GF_PORT_ARRAY_SIZE] = {
        0,
    };
    int i = 0;

loop:
    ret = gf_process_reserved_ports(ports, ceiling);

    while (port) {
        if (port == GF_CLIENT_PORT_CEILING) {
            ret = -1;
            break;
        }

        /* ignore the reserved ports */
        if (BIT_VALUE(ports, port)) {
            port--;
            continue;
        }

        _assign_port(sockaddr, port);

        ret = bind(fd, sockaddr, sockaddr_len);

        if (ret == 0)
            break;

        if (ret == -1 && errno == EACCES)
            break;

        port--;
    }

    /* In case if all the secure ports are exhausted, we are no more
     * binding to secure ports, hence instead of getting a random
     * port, lets define the range to restrict it from getting from
     * ports reserved for bricks i.e from range of 49152 - 65535
     * which further may lead to port clash */
    if (!port) {
        ceiling = port = GF_CLNT_INSECURE_PORT_CEILING;
        for (i = 0; i <= ceiling; i++)
            BIT_CLEAR(ports, i);
        goto loop;
    }

    return ret;
#endif /* GF_DISABLE_PRIVPORT_TRACKING */
}

static int32_t
af_unix_client_bind(rpc_transport_t *this, struct sockaddr *sockaddr,
                    socklen_t sockaddr_len, int sock)
{
    data_t *path_data = NULL;
    struct sockaddr_un *addr = NULL;
    int32_t ret = 0;

    path_data = dict_get_sizen(this->options, "transport.socket.bind-path");
    if (path_data) {
        char *path = data_to_str(path_data);
        if (!path || path_data->len > 108) { /* 108 = addr->sun_path length */
            gf_log(this->name, GF_LOG_TRACE,
                   "bind-path not specified for unix socket, "
                   "letting connect to assign default value");
            goto err;
        }

        addr = (struct sockaddr_un *)sockaddr;

        strncpy(addr->sun_path, path, sizeof(addr->sun_path));
        addr->sun_path[sizeof(addr->sun_path) - 1] = '\0';

        ret = bind(sock, (struct sockaddr *)addr, sockaddr_len);

        if (ret == -1) {
            gf_log(this->name, GF_LOG_ERROR,
                   "cannot bind to unix-domain socket %d (%s)", sock,
                   strerror(errno));
        }
    } else {
        gf_log(this->name, GF_LOG_TRACE,
               "bind-path not specified for unix socket, "
               "letting connect to assign default value");
    }

err:
    return ret;
}

static int32_t
client_fill_address_family(rpc_transport_t *this, sa_family_t *sa_family)
{
    data_t *address_family_data = NULL;
    int32_t ret = -1;

    if (sa_family == NULL) {
        gf_log_callingfn("", GF_LOG_WARNING, "sa_family argument is NULL");
        goto out;
    }

    address_family_data = dict_get_sizen(this->options,
                                         "transport.address-family");
    if (!address_family_data) {
        data_t *remote_host_data = NULL, *connect_path_data = NULL;
        remote_host_data = dict_get_sizen(this->options, "remote-host");
        connect_path_data = dict_get_sizen(this->options,
                                           "transport.socket.connect-path");

        if (!(remote_host_data || connect_path_data) ||
            (remote_host_data && connect_path_data)) {
            gf_log(this->name, GF_LOG_ERROR,
                   "transport.address-family not specified. "
                   "Could not guess default value from (remote-host:%s or "
                   "transport.unix.connect-path:%s) options",
                   data_to_str(remote_host_data),
                   data_to_str(connect_path_data));
            *sa_family = AF_UNSPEC;
            goto out;
        }

        if (remote_host_data) {
            gf_log(this->name, GF_LOG_DEBUG,
                   "address-family not specified, marking it as unspec "
                   "for getaddrinfo to resolve from (remote-host: %s)",
                   data_to_str(remote_host_data));
            *sa_family = AF_UNSPEC;
        } else {
            gf_log(this->name, GF_LOG_DEBUG,
                   "address-family not specified, guessing it "
                   "to be unix from (transport.unix.connect-path: %s)",
                   data_to_str(connect_path_data));
            *sa_family = AF_UNIX;
        }

    } else {
        const char *address_family = data_to_str(address_family_data);
        if (!strcasecmp(address_family, "unix")) {
            *sa_family = AF_UNIX;
        } else if (!strcasecmp(address_family, "inet")) {
            *sa_family = AF_INET;
        } else if (!strcasecmp(address_family, "inet6")) {
            *sa_family = AF_INET6;
        } else if (!strcasecmp(address_family, "inet-sdp")) {
            *sa_family = AF_INET_SDP;
        } else {
            gf_log(this->name, GF_LOG_ERROR,
                   "unknown address-family (%s) specified", address_family);
            *sa_family = AF_UNSPEC;
            goto out;
        }
    }

    ret = 0;

out:
    return ret;
}

static int32_t
gf_resolve_ip6(const char *hostname, uint16_t port, int family, void **dnscache,
               struct addrinfo **addr_info)
{
    int32_t ret = 0;
    struct addrinfo hints;
    struct dnscache6 *cache = NULL;
    char service[NI_MAXSERV], host[NI_MAXHOST];

    if (!hostname) {
        gf_msg_callingfn("resolver", GF_LOG_WARNING, 0, LG_MSG_HOSTNAME_NULL,
                         "hostname is NULL");
        return -1;
    }

    if (!*dnscache) {
        *dnscache = GF_CALLOC(1, sizeof(struct dnscache6),
                              gf_common_mt_dnscache6);
        if (!*dnscache)
            return -1;
    }

    cache = *dnscache;
    if (cache->first && !cache->next) {
        freeaddrinfo(cache->first);
        cache->first = cache->next = NULL;
        gf_msg_trace("resolver", 0, "flushing DNS cache");
    }

    if (!cache->first) {
        char *port_str = NULL;
        gf_msg_trace("resolver", 0,
                     "DNS cache not present, freshly "
                     "probing hostname: %s",
                     hostname);

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = family;
        hints.ai_socktype = SOCK_STREAM;

        ret = gf_asprintf(&port_str, "%d", port);
        if (-1 == ret) {
            return -1;
        }
        if ((ret = getaddrinfo(hostname, port_str, &hints, &cache->first)) !=
            0) {
            gf_smsg("resolver", GF_LOG_ERROR, 0, LG_MSG_GETADDRINFO_FAILED,
                    "family=%d", family, "ret=%s", gai_strerror(ret), NULL);

            GF_FREE(*dnscache);
            *dnscache = NULL;
            GF_FREE(port_str);
            return -1;
        }
        GF_FREE(port_str);

        cache->next = cache->first;
    }

    if (cache->next) {
        ret = getnameinfo((struct sockaddr *)cache->next->ai_addr,
                          cache->next->ai_addrlen, host, sizeof(host), service,
                          sizeof(service), NI_NUMERICHOST);
        if (ret != 0) {
            gf_smsg("resolver", GF_LOG_ERROR, 0, LG_MSG_GETNAMEINFO_FAILED,
                    "ret=%s", gai_strerror(ret), NULL);
            goto err;
        }

        gf_msg_debug("resolver", 0,
                     "returning ip-%s (port-%s) for "
                     "hostname: %s and port: %d",
                     host, service, hostname, port);

        *addr_info = cache->next;
    }

    if (cache->next)
        cache->next = cache->next->ai_next;
    if (cache->next) {
        ret = getnameinfo((struct sockaddr *)cache->next->ai_addr,
                          cache->next->ai_addrlen, host, sizeof(host), service,
                          sizeof(service), NI_NUMERICHOST);
        if (ret != 0) {
            gf_smsg("resolver", GF_LOG_ERROR, 0, LG_MSG_GETNAMEINFO_FAILED,
                    "ret=%s", gai_strerror(ret), NULL);
            goto err;
        }

        gf_msg_debug("resolver", 0,
                     "next DNS query will return: "
                     "ip-%s port-%s",
                     host, service);
    }

    return 0;

err:
    freeaddrinfo(cache->first);
    cache->first = cache->next = NULL;
    GF_FREE(cache);
    *dnscache = NULL;
    return -1;
}

static int32_t
af_inet_client_get_remote_sockaddr(rpc_transport_t *this,
                                   struct sockaddr *sockaddr,
                                   socklen_t *sockaddr_len)
{
    dict_t *options = this->options;
    data_t *remote_host_data = NULL;
    data_t *remote_port_data = NULL;
    char *remote_host = NULL;
    uint16_t remote_port = GF_DEFAULT_SOCKET_LISTEN_PORT;
    struct addrinfo *addr_info = NULL;
    int32_t ret = 0;
    struct in6_addr serveraddr_ipv6;
    struct in_addr serveraddr_ipv4;

    remote_host_data = dict_get_sizen(options, "remote-host");
    if (remote_host_data == NULL) {
        gf_log(this->name, GF_LOG_ERROR,
               "option remote-host missing in volume %s", this->name);
        ret = -1;
        goto err;
    }

    remote_host = data_to_str(remote_host_data);
    if (remote_host == NULL) {
        gf_log(this->name, GF_LOG_ERROR,
               "option remote-host has data NULL in volume %s", this->name);
        ret = -1;
        goto err;
    }

    remote_port_data = dict_get_sizen(options, "remote-port");
    if (remote_port_data == NULL) {
        gf_log(this->name, GF_LOG_TRACE,
               "option remote-port missing in volume %s. Defaulting to %d",
               this->name, GF_DEFAULT_SOCKET_LISTEN_PORT);
    } else {
        remote_port = data_to_uint16(remote_port_data);
        if (remote_port == (uint16_t)-1) {
            gf_log(this->name, GF_LOG_ERROR,
                   "option remote-port has invalid port in volume %s",
                   this->name);
            ret = -1;
            goto err;
        }
    }

    /* Need to update transport-address family if address-family is not provided
       to command-line arguments
    */
    if (inet_pton(AF_INET6, remote_host, &serveraddr_ipv6)) {
        sockaddr->sa_family = AF_INET6;
    } else if (inet_pton(AF_INET, remote_host, &serveraddr_ipv4)) {
        sockaddr->sa_family = AF_INET;
    } else {
        sockaddr->sa_family = AF_UNSPEC;
    }

    /* TODO: gf_resolve is a blocking call. kick in some
       non blocking dns techniques */
    ret = gf_resolve_ip6(remote_host, remote_port, sockaddr->sa_family,
                         &this->dnscache, &addr_info);
    if (ret == -1) {
        gf_log(this->name, GF_LOG_ERROR, "DNS resolution failed on host %s",
               remote_host);
        goto err;
    }

    memcpy(sockaddr, addr_info->ai_addr, addr_info->ai_addrlen);
    *sockaddr_len = addr_info->ai_addrlen;

err:
    return ret;
}

static int32_t
af_unix_client_get_remote_sockaddr(rpc_transport_t *this,
                                   struct sockaddr *sockaddr,
                                   socklen_t *sockaddr_len)
{
    struct sockaddr_un *sockaddr_un = NULL;
    char *connect_path = NULL;
    data_t *connect_path_data = NULL;
    int32_t ret = -1;

    connect_path_data = dict_get_sizen(this->options,
                                       "transport.socket.connect-path");
    if (!connect_path_data) {
        gf_log(this->name, GF_LOG_ERROR,
               "option transport.unix.connect-path not specified for "
               "address-family unix");
        goto err;
    }

    /* 108 = sockaddr_un->sun_path length */
    if ((connect_path_data->len + 1) > 108) {
        gf_log(this->name, GF_LOG_ERROR,
               "connect-path value length %d > %d octets",
               connect_path_data->len + 1, UNIX_PATH_MAX);
        goto err;
    }

    connect_path = data_to_str(connect_path_data);
    if (!connect_path) {
        gf_log(this->name, GF_LOG_ERROR,
               "transport.unix.connect-path is null-string");
        goto err;
    }

    gf_log(this->name, GF_LOG_TRACE, "using connect-path %s", connect_path);
    sockaddr_un = (struct sockaddr_un *)sockaddr;
    strcpy(sockaddr_un->sun_path, connect_path);
    *sockaddr_len = sizeof(struct sockaddr_un);

    ret = 0;
err:
    return ret;
}

static int32_t
af_unix_server_get_local_sockaddr(rpc_transport_t *this, struct sockaddr *addr,
                                  socklen_t *addr_len)
{
    data_t *listen_path_data = NULL;
    char *listen_path = NULL;
    int32_t ret = 0;
    struct sockaddr_un *sunaddr = (struct sockaddr_un *)addr;

    listen_path_data = dict_get_sizen(this->options,
                                      "transport.socket.listen-path");
    if (!listen_path_data) {
        gf_log(this->name, GF_LOG_ERROR,
               "missing option transport.socket.listen-path");
        ret = -1;
        goto err;
    }

    listen_path = data_to_str(listen_path_data);

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

    if ((listen_path_data->len + 1) > UNIX_PATH_MAX) {
        gf_log(this->name, GF_LOG_ERROR,
               "option transport.unix.listen-path has value length "
               "%" GF_PRI_SIZET " > %d",
               strlen(listen_path), UNIX_PATH_MAX);
        ret = -1;
        goto err;
    }

    sunaddr->sun_family = AF_UNIX;
    strcpy(sunaddr->sun_path, listen_path);
    *addr_len = sizeof(struct sockaddr_un);

err:
    return ret;
}

static int32_t
af_inet_server_get_local_sockaddr(rpc_transport_t *this, struct sockaddr *addr,
                                  socklen_t *addr_len)
{
    struct addrinfo hints, *res = 0, *rp = NULL;
    data_t *listen_port_data = NULL, *listen_host_data = NULL;
    uint16_t listen_port = 0;
    char service[NI_MAXSERV], *listen_host = NULL;
    dict_t *options = NULL;
    int32_t ret = 0;

    /* initializes addr_len */
    *addr_len = 0;

    options = this->options;

    listen_port_data = dict_get_sizen(options, "transport.socket.listen-port");
    if (listen_port_data) {
        listen_port = data_to_uint16(listen_port_data);
    } else {
        listen_port = GF_DEFAULT_SOCKET_LISTEN_PORT;
    }

    listen_host_data = dict_get_sizen(options, "transport.socket.bind-address");
    if (listen_host_data) {
        listen_host = data_to_str(listen_host_data);
    } else {
        if (addr->sa_family == AF_INET6) {
            struct sockaddr_in6 *in = (struct sockaddr_in6 *)addr;
            in->sin6_addr = in6addr_any;
            in->sin6_port = htons(listen_port);
            *addr_len = sizeof(struct sockaddr_in6);
            goto out;
        } else if (addr->sa_family == AF_INET) {
            struct sockaddr_in *in = (struct sockaddr_in *)addr;
            in->sin_addr.s_addr = htonl(INADDR_ANY);
            in->sin_port = htons(listen_port);
            *addr_len = sizeof(struct sockaddr_in);
            goto out;
        }
    }

    sprintf(service, "%d", listen_port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = addr->sa_family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    ret = getaddrinfo(listen_host, service, &hints, &res);
    if (ret != 0) {
        gf_log(this->name, GF_LOG_ERROR,
               "getaddrinfo failed for host %s, service %s (%s)", listen_host,
               service, gai_strerror(ret));
        ret = -1;
        goto out;
    }
    /* IPV6 server can handle both ipv4 and ipv6 clients */
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_addr == NULL)
            continue;
        if (rp->ai_family == AF_INET6) {
            memcpy(addr, rp->ai_addr, rp->ai_addrlen);
            *addr_len = rp->ai_addrlen;
        }
    }

    if (!(*addr_len)) {
        if (res && res->ai_addr) {
            memcpy(addr, res->ai_addr, res->ai_addrlen);
            *addr_len = res->ai_addrlen;
        } else {
            ret = -1;
        }
    }

    freeaddrinfo(res);

out:
    return ret;
}

int32_t
client_bind(rpc_transport_t *this, struct sockaddr *sockaddr,
            socklen_t *sockaddr_len, int sock)
{
    int ret = 0;

    *sockaddr_len = sizeof(struct sockaddr_in6);
    switch (sockaddr->sa_family) {
        case AF_INET_SDP:
        case AF_INET:
            *sockaddr_len = sizeof(struct sockaddr_in);
        /* Fall through */
        case AF_INET6:
            if (!this->bind_insecure) {
                ret = af_inet_bind_to_port_lt_ceiling(
                    sock, sockaddr, *sockaddr_len, GF_CLIENT_PORT_CEILING);
                if (ret == -1) {
                    gf_log(this->name, GF_LOG_DEBUG,
                           "cannot bind inet socket (%d) "
                           "to port less than %d (%s)",
                           sock, GF_CLIENT_PORT_CEILING, strerror(errno));
                    ret = 0;
                }
            } else {
                ret = af_inet_bind_to_port_lt_ceiling(
                    sock, sockaddr, *sockaddr_len, GF_IANA_PRIV_PORTS_START);
                if (ret == -1) {
                    gf_log(this->name, GF_LOG_DEBUG,
                           "failed while binding to less than "
                           "%d (%s)",
                           GF_IANA_PRIV_PORTS_START, strerror(errno));
                    ret = 0;
                }
            }
            break;

        case AF_UNIX:
            *sockaddr_len = sizeof(struct sockaddr_un);
            ret = af_unix_client_bind(this, (struct sockaddr *)sockaddr,
                                      *sockaddr_len, sock);
            break;

        default:
            gf_log(this->name, GF_LOG_ERROR, "unknown address family %d",
                   sockaddr->sa_family);
            ret = -1;
            break;
    }

    return ret;
}

int32_t
socket_client_get_remote_sockaddr(rpc_transport_t *this,
                                  struct sockaddr *sockaddr,
                                  socklen_t *sockaddr_len,
                                  sa_family_t *sa_family)
{
    int32_t ret = 0;

    GF_VALIDATE_OR_GOTO("socket", sockaddr, err);
    GF_VALIDATE_OR_GOTO("socket", sockaddr_len, err);
    GF_VALIDATE_OR_GOTO("socket", sa_family, err);

    ret = client_fill_address_family(this, &sockaddr->sa_family);
    if (ret) {
        ret = -1;
        goto err;
    }

    *sa_family = sockaddr->sa_family;

    switch (sockaddr->sa_family) {
        case AF_INET_SDP:
            sockaddr->sa_family = AF_INET;
        /* Fall through */
        case AF_INET:
        case AF_INET6:
        case AF_UNSPEC:
            ret = af_inet_client_get_remote_sockaddr(this, sockaddr,
                                                     sockaddr_len);
            break;

        case AF_UNIX:
            ret = af_unix_client_get_remote_sockaddr(this, sockaddr,
                                                     sockaddr_len);
            break;

        default:
            gf_log(this->name, GF_LOG_ERROR, "unknown address-family %d",
                   sockaddr->sa_family);
            ret = -1;
    }

    /* Address-family is updated based on remote_host in
       af_inet_client_get_remote_sockaddr
    */
    if (*sa_family != sockaddr->sa_family) {
        *sa_family = sockaddr->sa_family;
    }

err:
    return ret;
}

static int32_t
server_fill_address_family(rpc_transport_t *this, sa_family_t *sa_family)
{
    data_t *address_family_data = NULL;
    int32_t ret = -1;

#ifdef IPV6_DEFAULT
    const char *addr_family = "inet6";
    sa_family_t default_family = AF_INET6;
#else
    const char *addr_family = "inet";
    sa_family_t default_family = AF_INET;
#endif

    GF_VALIDATE_OR_GOTO("socket", sa_family, out);

    address_family_data = dict_get_sizen(this->options,
                                         "transport.address-family");
    if (address_family_data) {
        char *address_family = NULL;
        address_family = data_to_str(address_family_data);

        if (!strcasecmp(address_family, "inet")) {
            *sa_family = AF_INET;
        } else if (!strcasecmp(address_family, "inet6")) {
            *sa_family = AF_INET6;
        } else if (!strcasecmp(address_family, "inet-sdp")) {
            *sa_family = AF_INET_SDP;
        } else if (!strcasecmp(address_family, "unix")) {
            *sa_family = AF_UNIX;
        } else {
            gf_log(this->name, GF_LOG_ERROR,
                   "unknown address family (%s) specified", address_family);
            *sa_family = AF_UNSPEC;
            goto out;
        }
    } else {
        gf_log(this->name, GF_LOG_DEBUG,
               "option address-family not specified, "
               "defaulting to %s",
               addr_family);
        *sa_family = default_family;
    }

    ret = 0;
out:
    return ret;
}

int32_t
socket_server_get_local_sockaddr(rpc_transport_t *this, struct sockaddr *addr,
                                 socklen_t *addr_len, sa_family_t *sa_family)
{
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("socket", sa_family, err);
    GF_VALIDATE_OR_GOTO("socket", addr, err);
    GF_VALIDATE_OR_GOTO("socket", addr_len, err);

    ret = server_fill_address_family(this, &addr->sa_family);
    if (ret == -1) {
        goto err;
    }

    *sa_family = addr->sa_family;

    switch (addr->sa_family) {
        case AF_INET_SDP:
            addr->sa_family = AF_INET;
        /* Fall through */
        case AF_INET:
        case AF_INET6:
        case AF_UNSPEC:
            ret = af_inet_server_get_local_sockaddr(this, addr, addr_len);
            break;

        case AF_UNIX:
            ret = af_unix_server_get_local_sockaddr(this, addr, addr_len);
            break;
    }

    if (*sa_family == AF_UNSPEC) {
        *sa_family = addr->sa_family;
    }

err:
    return ret;
}

static int32_t
fill_inet6_inet_identifiers(rpc_transport_t *this,
                            struct sockaddr_storage *addr, int32_t addr_len,
                            char *identifier)
{
    union gf_sock_union sock_union;

    char service[NI_MAXSERV] = {
        0,
    };
    char host[NI_MAXHOST] = {
        0,
    };
    int32_t ret = 0;
    int32_t tmpaddr_len = 0;
    int32_t one_to_four = 0;
    int32_t four_to_eight = 0;
    int32_t twelve_to_sixteen = 0;
    int16_t eight_to_ten = 0;
    int16_t ten_to_twelve = 0;

    memset(&sock_union, 0, sizeof(sock_union));
    sock_union.storage = *addr;
    tmpaddr_len = addr_len;

    if (sock_union.sa.sa_family == AF_INET6) {
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

        if (one_to_four == 0 && four_to_eight == 0 && eight_to_ten == 0 &&
            ten_to_twelve == -1) {
            struct sockaddr_in *in_ptr = &sock_union.sin;
            memset(&sock_union, 0, sizeof(sock_union));

            in_ptr->sin_family = AF_INET;
            in_ptr->sin_port = ((struct sockaddr_in6 *)addr)->sin6_port;
            in_ptr->sin_addr.s_addr = twelve_to_sixteen;
            tmpaddr_len = sizeof(*in_ptr);
        }
    }

    ret = getnameinfo(&sock_union.sa, tmpaddr_len, host, sizeof(host), service,
                      sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV);
    if (ret != 0) {
        gf_log(this->name, GF_LOG_ERROR, "getnameinfo failed (%s)",
               gai_strerror(ret));
    }

    sprintf(identifier, "%s:%s", host, service);

    return ret;
}

int32_t
get_transport_identifiers(rpc_transport_t *this)
{
    int32_t ret = 0;
    char is_inet_sdp = 0;

    switch (((struct sockaddr *)&this->myinfo.sockaddr)->sa_family) {
        case AF_INET_SDP:
            is_inet_sdp = 1;
            ((struct sockaddr *)&this->peerinfo.sockaddr)
                ->sa_family = ((struct sockaddr *)&this->myinfo.sockaddr)
                                  ->sa_family = AF_INET;
        /* Fall through */
        case AF_INET:
        case AF_INET6: {
            ret = fill_inet6_inet_identifiers(this, &this->myinfo.sockaddr,
                                              this->myinfo.sockaddr_len,
                                              this->myinfo.identifier);
            if (ret == -1) {
                gf_log(this->name, GF_LOG_ERROR,
                       "cannot fill inet/inet6 identifier for server");
                goto err;
            }

            ret = fill_inet6_inet_identifiers(this, &this->peerinfo.sockaddr,
                                              this->peerinfo.sockaddr_len,
                                              this->peerinfo.identifier);
            if (ret == -1) {
                gf_log(this->name, GF_LOG_ERROR,
                       "cannot fill inet/inet6 identifier for client");
                goto err;
            }

            if (is_inet_sdp) {
                ((struct sockaddr *)&this->peerinfo.sockaddr)
                    ->sa_family = ((struct sockaddr *)&this->myinfo.sockaddr)
                                      ->sa_family = AF_INET_SDP;
            }
        } break;

        case AF_UNIX: {
            struct sockaddr_un *sunaddr = NULL;

            sunaddr = (struct sockaddr_un *)&this->myinfo.sockaddr;
            strcpy(this->myinfo.identifier, sunaddr->sun_path);

            sunaddr = (struct sockaddr_un *)&this->peerinfo.sockaddr;
            strcpy(this->peerinfo.identifier, sunaddr->sun_path);
        } break;

        default:
            gf_log(this->name, GF_LOG_ERROR, "unknown address family (%d)",
                   ((struct sockaddr *)&this->myinfo.sockaddr)->sa_family);
            ret = -1;
            break;
    }

err:
    return ret;
}
