/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "socket.h"
#include "name.h"
#include <glusterfs/dict.h>
#include <glusterfs/syscall.h>
#include <glusterfs/byte-order.h>
#include <glusterfs/compat-errno.h>
#include "socket-mem-types.h"

/* ugly #includes below */
#include "protocol-common.h"
#include "glusterfs3-xdr.h"
#include "glusterfs4-xdr.h"
#include "rpcsvc.h"

/* for TCP_USER_TIMEOUT */
#if !defined(TCP_USER_TIMEOUT) && defined(GF_LINUX_HOST_OS)
#include <linux/tcp.h>
#else
#include <netinet/tcp.h>
#endif

#include <errno.h>
#include <rpc/xdr.h>
#include <sys/ioctl.h>
#define GF_LOG_ERRNO(errno) ((errno == ENOTCONN) ? GF_LOG_DEBUG : GF_LOG_ERROR)
#define SA(ptr) ((struct sockaddr *)ptr)

#define SSL_ENABLED_OPT "transport.socket.ssl-enabled"
#define SSL_OWN_CERT_OPT "transport.socket.ssl-own-cert"
#define SSL_PRIVATE_KEY_OPT "transport.socket.ssl-private-key"
#define SSL_CA_LIST_OPT "transport.socket.ssl-ca-list"
#define SSL_CERT_DEPTH_OPT "transport.socket.ssl-cert-depth"
#define SSL_CIPHER_LIST_OPT "transport.socket.ssl-cipher-list"
#define SSL_DH_PARAM_OPT "transport.socket.ssl-dh-param"
#define SSL_EC_CURVE_OPT "transport.socket.ssl-ec-curve"
#define SSL_CRL_PATH_OPT "transport.socket.ssl-crl-path"
#define OWN_THREAD_OPT "transport.socket.own-thread"

/* TBD: do automake substitutions etc. (ick) to set these. */
#if !defined(DEFAULT_ETC_SSL)
#ifdef GF_LINUX_HOST_OS
#define DEFAULT_ETC_SSL "/etc/ssl"
#endif
#ifdef GF_BSD_HOST_OS
#define DEFAULT_ETC_SSL "/etc/openssl"
#endif
#ifdef GF_DARWIN_HOST_OS
#define DEFAULT_ETC_SSL "/usr/local/etc/openssl"
#endif
#if !defined(DEFAULT_ETC_SSL)
#define DEFAULT_ETC_SSL "/etc/ssl"
#endif
#endif

#if !defined(DEFAULT_CERT_PATH)
#define DEFAULT_CERT_PATH DEFAULT_ETC_SSL "/glusterfs.pem"
#endif
#if !defined(DEFAULT_KEY_PATH)
#define DEFAULT_KEY_PATH DEFAULT_ETC_SSL "/glusterfs.key"
#endif
#if !defined(DEFAULT_CA_PATH)
#define DEFAULT_CA_PATH DEFAULT_ETC_SSL "/glusterfs.ca"
#endif
#if !defined(DEFAULT_VERIFY_DEPTH)
#define DEFAULT_VERIFY_DEPTH 1
#endif
#define DEFAULT_CIPHER_LIST "EECDH:EDH:HIGH:!3DES:!RC4:!DES:!MD5:!aNULL:!eNULL"
#define DEFAULT_DH_PARAM DEFAULT_ETC_SSL "/dhparam.pem"
#define DEFAULT_EC_CURVE "prime256v1"

#define POLL_MASK_INPUT (POLLIN | POLLPRI)
#define POLL_MASK_OUTPUT (POLLOUT)
#define POLL_MASK_ERROR (POLLERR | POLLHUP | POLLNVAL)

typedef int
SSL_unary_func(SSL *);
typedef int
SSL_trinary_func(SSL *, void *, int);
static int
ssl_setup_connection_params(rpc_transport_t *this);

#define __socket_proto_reset_pending(priv)                                     \
    do {                                                                       \
        struct gf_sock_incoming_frag *frag;                                    \
        frag = &priv->incoming.frag;                                           \
                                                                               \
        memset(&frag->vector, 0, sizeof(frag->vector));                        \
        frag->pending_vector = &frag->vector;                                  \
        frag->pending_vector->iov_base = frag->fragcurrent;                    \
        priv->incoming.pending_vector = frag->pending_vector;                  \
    } while (0)

#define __socket_proto_update_pending(priv)                                    \
    do {                                                                       \
        uint32_t remaining;                                                    \
        struct gf_sock_incoming_frag *frag;                                    \
        frag = &priv->incoming.frag;                                           \
        if (frag->pending_vector->iov_len == 0) {                              \
            remaining = (RPC_FRAGSIZE(priv->incoming.fraghdr) -                \
                         frag->bytes_read);                                    \
                                                                               \
            frag->pending_vector->iov_len = (remaining > frag->remaining_size) \
                                                ? frag->remaining_size         \
                                                : remaining;                   \
                                                                               \
            frag->remaining_size -= frag->pending_vector->iov_len;             \
        }                                                                      \
    } while (0)

#define __socket_proto_update_priv_after_read(priv, ret, bytes_read)           \
    {                                                                          \
        struct gf_sock_incoming_frag *frag;                                    \
        frag = &priv->incoming.frag;                                           \
                                                                               \
        frag->fragcurrent += bytes_read;                                       \
        frag->bytes_read += bytes_read;                                        \
                                                                               \
        if ((ret > 0) || (frag->remaining_size != 0)) {                        \
            if (frag->remaining_size != 0 && ret == 0) {                       \
                __socket_proto_reset_pending(priv);                            \
            }                                                                  \
                                                                               \
            gf_log(this->name, GF_LOG_TRACE,                                   \
                   "partial read on non-blocking socket");                     \
            ret = 0;                                                           \
            break;                                                             \
        }                                                                      \
    }

#define __socket_proto_init_pending(priv, size)                                \
    do {                                                                       \
        uint32_t remaining = 0;                                                \
        struct gf_sock_incoming_frag *frag;                                    \
        frag = &priv->incoming.frag;                                           \
                                                                               \
        remaining = (RPC_FRAGSIZE(priv->incoming.fraghdr) - frag->bytes_read); \
                                                                               \
        __socket_proto_reset_pending(priv);                                    \
                                                                               \
        frag->pending_vector->iov_len = (remaining > size) ? size : remaining; \
                                                                               \
        frag->remaining_size = (size - frag->pending_vector->iov_len);         \
                                                                               \
    } while (0)

/* This will be used in a switch case and breaks from the switch case if all
 * the pending data is not read.
 */
#define __socket_proto_read(priv, ret)                                         \
    {                                                                          \
        size_t bytes_read = 0;                                                 \
        struct gf_sock_incoming *in;                                           \
        in = &priv->incoming;                                                  \
                                                                               \
        __socket_proto_update_pending(priv);                                   \
                                                                               \
        ret = __socket_readv(this, in->pending_vector, 1, &in->pending_vector, \
                             &in->pending_count, &bytes_read);                 \
        if (ret < 0)                                                           \
            break;                                                             \
        __socket_proto_update_priv_after_read(priv, ret, bytes_read);          \
    }

struct socket_connect_error_state_ {
    xlator_t *this;
    rpc_transport_t *trans;
    gf_boolean_t refd;
};
typedef struct socket_connect_error_state_ socket_connect_error_state_t;

static int
socket_init(rpc_transport_t *this);
static int
__socket_nonblock(int fd);

static void
socket_dump_info(struct sockaddr *sa, int is_server, int is_ssl, int sock,
                 char *log_domain, char *log_label)
{
    char addr_buf[INET6_ADDRSTRLEN + 1] = {
        0,
    };
    char *addr = NULL;
    const char *peer_type = NULL;
    int af = sa->sa_family;
    int so_error = -1;
    socklen_t slen = sizeof(so_error);

    if (af == AF_UNIX) {
        addr = ((struct sockaddr_un *)(sa))->sun_path;
    } else {
        if (af == AF_INET6) {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)(sa);

            inet_ntop(af, &sin6->sin6_addr, addr_buf, sizeof(addr_buf));
            addr = addr_buf;
        } else {
            struct sockaddr_in *sin = (struct sockaddr_in *)(sa);

            inet_ntop(af, &sin->sin_addr, addr_buf, sizeof(addr_buf));
            addr = addr_buf;
        }
    }
    if (is_server)
        peer_type = "server";
    else
        peer_type = "client";

    (void)getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &slen);

    gf_log(log_domain, GF_LOG_TRACE,
           "$$$ %s: %s (af:%d,sock:%d) %s %s (errno:%d:%s)", peer_type,
           log_label, af, sock, addr, (is_ssl ? "SSL" : "non-SSL"), so_error,
           strerror(so_error));
}

static void
ssl_dump_error_stack(const char *caller)
{
    unsigned long errnum = 0;
    char errbuf[120] = {
        0,
    };

    /* OpenSSL docs explicitly give 120 as the error-string length. */

    while ((errnum = ERR_get_error())) {
        ERR_error_string(errnum, errbuf);
        gf_log(caller, GF_LOG_ERROR, "  %s", errbuf);
    }
}

static int
ssl_do(rpc_transport_t *this, void *buf, size_t len, SSL_trinary_func *func)
{
    int r = (-1);
    socket_private_t *priv = NULL;

    priv = this->private;

    if (buf) {
        if (priv->connected == -1) {
            /*
             * Fields in the SSL structure (especially
             * the BIO pointers) are not valid at this
             * point, so we'll segfault if we pass them
             * to SSL_read/SSL_write.
             */
            gf_log(this->name, GF_LOG_INFO, "lost connection in %s", __func__);
            return -1;
        }
        r = func(priv->ssl_ssl, buf, len);
    } else {
        /* This should be treated as error */
        gf_log(this->name, GF_LOG_ERROR, "buffer is empty %s", __func__);
        goto out;
    }
    switch (SSL_get_error(priv->ssl_ssl, r)) {
        case SSL_ERROR_NONE:
        /* fall through */
        case SSL_ERROR_WANT_READ:
        /* fall through */
        case SSL_ERROR_WANT_WRITE:
            errno = EAGAIN;
            return r;

        case SSL_ERROR_SYSCALL:
            /* Sometimes SSL_ERROR_SYSCALL returns errno as
             * EAGAIN. In such a case we should reattempt operation
             * So, for now, just return the return value and the
             * errno as is.
             */
            gf_log(this->name, GF_LOG_DEBUG,
                   "syscall error (probably remote disconnect) "
                   "errno:%d:%s",
                   errno, strerror(errno));
            return r;
        default:
            errno = EIO;
            goto out; /* "break" would just loop again */
    }
out:
    return -1;
}

#define ssl_read_one(t, b, l)                                                  \
    ssl_do((t), (b), (l), (SSL_trinary_func *)SSL_read)
#define ssl_write_one(t, b, l)                                                 \
    ssl_do((t), (b), (l), (SSL_trinary_func *)SSL_write)

/* set crl verify flags only for server */
/* see man X509_VERIFY_PARAM_SET_FLAGS(3)
 * X509_V_FLAG_CRL_CHECK enables CRL checking for the certificate chain
 * leaf certificate. An error occurs if a suitable CRL cannot be found.
 * Since we're never going to revoke a gluster node cert, we better disable
 * CRL check for server certs to avoid getting error and failed connection
 * attempts.
 */
static void
ssl_clear_crl_verify_flags(SSL_CTX *ssl_ctx)
{
#ifdef X509_V_FLAG_CRL_CHECK_ALL
#ifdef HAVE_SSL_CTX_GET0_PARAM
    X509_VERIFY_PARAM *vpm;

    vpm = SSL_CTX_get0_param(ssl_ctx);
    if (vpm) {
        X509_VERIFY_PARAM_clear_flags(
            vpm, (X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL));
    }
#else
    /* CRL verify flag need not be cleared for rhel6 kind of clients */
#endif
#else
    gf_log(this->name, GF_LOG_ERROR, "OpenSSL version does not support CRL");
#endif
    return;
}

/* set crl verify flags only for server */
static void
ssl_set_crl_verify_flags(SSL_CTX *ssl_ctx)
{
#ifdef X509_V_FLAG_CRL_CHECK_ALL
#ifdef HAVE_SSL_CTX_GET0_PARAM
    X509_VERIFY_PARAM *vpm;

    vpm = SSL_CTX_get0_param(ssl_ctx);
    if (vpm) {
        unsigned long flags;

        flags = X509_VERIFY_PARAM_get_flags(vpm);
        flags |= (X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
        X509_VERIFY_PARAM_set_flags(vpm, flags);
    }
#else
    X509_STORE *x509store;

    x509store = SSL_CTX_get_cert_store(ssl_ctx);
    X509_STORE_set_flags(x509store,
                         X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
#endif
#else
    gf_log(this->name, GF_LOG_ERROR, "OpenSSL version does not support CRL");
#endif
}

static int
ssl_setup_connection_prefix(rpc_transport_t *this, gf_boolean_t server)
{
    int ret = -1;
    socket_private_t *priv = NULL;

    priv = this->private;

    if (ssl_setup_connection_params(this) < 0) {
        gf_log(this->name, GF_LOG_TRACE,
               "+ ssl_setup_connection_params() failed!");
        goto done;
    } else {
        gf_log(this->name, GF_LOG_TRACE,
               "+ ssl_setup_connection_params() done!");
    }

    priv->ssl_error_required = SSL_ERROR_NONE;
    priv->ssl_connected = _gf_false;
    priv->ssl_accepted = _gf_false;
    priv->ssl_context_created = _gf_false;

    if (!server && priv->crl_path)
        ssl_clear_crl_verify_flags(priv->ssl_ctx);

    priv->ssl_ssl = SSL_new(priv->ssl_ctx);
    if (!priv->ssl_ssl) {
        gf_log(this->name, GF_LOG_ERROR, "SSL_new failed");
        ssl_dump_error_stack(this->name);
        goto done;
    }

    priv->ssl_sbio = BIO_new_socket(priv->sock, BIO_NOCLOSE);
    if (!priv->ssl_sbio) {
        gf_log(this->name, GF_LOG_ERROR, "BIO_new_socket failed");
        ssl_dump_error_stack(this->name);
        goto free_ssl;
    }

    SSL_set_bio(priv->ssl_ssl, priv->ssl_sbio, priv->ssl_sbio);
    ret = 0;
    goto done;

free_ssl:
    SSL_free(priv->ssl_ssl);
    priv->ssl_ssl = NULL;
done:
    return ret;
}

static char *
ssl_setup_connection_postfix(rpc_transport_t *this)
{
    X509 *peer = NULL;
    char peer_CN[256] = "";
    socket_private_t *priv = NULL;

    priv = this->private;

    /* Make sure _SSL verification_ succeeded, yielding an identity. */
    if (SSL_get_verify_result(priv->ssl_ssl) != X509_V_OK) {
        goto ssl_error;
    }
    peer = SSL_get_peer_certificate(priv->ssl_ssl);
    if (!peer) {
        goto ssl_error;
    }

    SSL_set_mode(priv->ssl_ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);

    /* Finally, everything seems OK. */
    X509_NAME_get_text_by_NID(X509_get_subject_name(peer), NID_commonName,
                              peer_CN, sizeof(peer_CN) - 1);
    peer_CN[sizeof(peer_CN) - 1] = '\0';
    gf_log(this->name, GF_LOG_DEBUG, "peer CN = %s", peer_CN);
    gf_log(this->name, GF_LOG_DEBUG,
           "SSL verification succeeded (client: %s) (server: %s)",
           this->peerinfo.identifier, this->myinfo.identifier);
    X509_free(peer);
    return gf_strdup(peer_CN);

    /* Error paths. */
ssl_error:
    gf_log(this->name, GF_LOG_ERROR,
           "SSL connect error (client: %s) (server: %s)",
           this->peerinfo.identifier, this->myinfo.identifier);
    ssl_dump_error_stack(this->name);

    SSL_free(priv->ssl_ssl);
    priv->ssl_ssl = NULL;
    return NULL;
}

static int
ssl_complete_connection(rpc_transport_t *this)
{
    int ret = -1; /*  1 : implies go back to epoll_wait()
                   *  0 : implies successful ssl connection
                   * -1: implies continue processing current event
                   *     as if EPOLLERR has been encountered
                   */
    char *cname = NULL;
    int r = -1;
    int ssl_error = -1;
    socket_private_t *priv = NULL;

    priv = this->private;

    if (priv->is_server) {
        r = SSL_accept(priv->ssl_ssl);
    } else {
        r = SSL_connect(priv->ssl_ssl);
    }

    ssl_error = SSL_get_error(priv->ssl_ssl, r);
    priv->ssl_error_required = ssl_error;

    switch (ssl_error) {
        case SSL_ERROR_NONE:
            cname = ssl_setup_connection_postfix(this);
            if (!cname) {
                /* we've failed to get the cname so
                 * we must close the connection
                 *
                 * treat this as EPOLLERR
                 */
                gf_log(this->name, GF_LOG_TRACE, "error getting cname");
                errno = ECONNRESET;
                ret = -1;
            } else {
                this->ssl_name = cname;
                if (priv->is_server) {
                    priv->ssl_accepted = _gf_true;
                    gf_log(this->name, GF_LOG_TRACE, "ssl_accepted!");
                } else {
                    priv->ssl_connected = _gf_true;
                    gf_log(this->name, GF_LOG_TRACE, "ssl_connected!");
                }
                ret = 0;
            }
            break;

        case SSL_ERROR_WANT_READ:
        /* fall through */
        case SSL_ERROR_WANT_WRITE:
            errno = EAGAIN;
            break;

        case SSL_ERROR_SYSCALL:
            /* Sometimes SSL_ERROR_SYSCALL returns with errno as EAGAIN
             * So, we should retry the operation.
             * So, for now, we just return the return value and errno as is.
             */
            break;

        case SSL_ERROR_SSL:
            /* treat this as EPOLLERR */
            ret = -1;
            break;

        default:
            /* treat this as EPOLLERR */
            errno = EIO;
            ret = -1;
            break;
    }
    return ret;
}

static void
ssl_teardown_connection(socket_private_t *priv)
{
    if (priv->ssl_ssl) {
        SSL_shutdown(priv->ssl_ssl);
        SSL_clear(priv->ssl_ssl);
        SSL_free(priv->ssl_ssl);
        SSL_CTX_free(priv->ssl_ctx);
        priv->ssl_ssl = NULL;
        priv->ssl_ctx = NULL;
        if (priv->ssl_private_key) {
            GF_FREE(priv->ssl_private_key);
            priv->ssl_private_key = NULL;
        }
        if (priv->ssl_own_cert) {
            GF_FREE(priv->ssl_own_cert);
            priv->ssl_own_cert = NULL;
        }
        if (priv->ssl_ca_list) {
            GF_FREE(priv->ssl_ca_list);
            priv->ssl_ca_list = NULL;
        }
    }
    priv->use_ssl = _gf_false;
}

static ssize_t
__socket_ssl_readv(rpc_transport_t *this, struct iovec *opvector, int opcount)
{
    socket_private_t *priv = NULL;
    int sock = -1;
    int ret = -1;

    priv = this->private;
    sock = priv->sock;

    if (priv->use_ssl) {
        gf_log(this->name, GF_LOG_TRACE, "***** reading over SSL");
        ret = ssl_read_one(this, opvector->iov_base, opvector->iov_len);
    } else {
        gf_log(this->name, GF_LOG_TRACE, "***** reading over non-SSL");
        ret = sys_readv(sock, opvector, IOV_MIN(opcount));
    }

    return ret;
}

static ssize_t
__socket_ssl_read(rpc_transport_t *this, void *buf, size_t count)
{
    struct iovec iov = {
        0,
    };
    int ret = -1;

    iov.iov_base = buf;
    iov.iov_len = count;

    ret = __socket_ssl_readv(this, &iov, 1);

    return ret;
}

static int
__socket_cached_read(rpc_transport_t *this, struct iovec *opvector, int opcount)
{
    socket_private_t *priv = NULL;
    struct gf_sock_incoming *in = NULL;
    int req_len = -1;
    int ret = -1;

    priv = this->private;
    in = &priv->incoming;
    req_len = iov_length(opvector, opcount);

    if (in->record_state == SP_STATE_READING_FRAGHDR) {
        in->ra_read = 0;
        in->ra_served = 0;
        in->ra_max = 0;
        in->ra_buf = NULL;
        goto uncached;
    }

    if (!in->ra_max) {
        /* first call after passing SP_STATE_READING_FRAGHDR */
        in->ra_max = min(RPC_FRAGSIZE(in->fraghdr), GF_SOCKET_RA_MAX);
        /* Note that the in->iobuf is the primary iobuf into which
           headers are read into, and in->frag.fragcurrent points to
           some position in the buffer. By using this itself as our
           read-ahead cache, we can avoid memory copies in iov_load
        */
        in->ra_buf = in->frag.fragcurrent;
    }

    /* fill read-ahead */
    if (in->ra_read < in->ra_max) {
        ret = __socket_ssl_read(this, &in->ra_buf[in->ra_read],
                                (in->ra_max - in->ra_read));
        if (ret > 0)
            in->ra_read += ret;

        /* we proceed to test if there is still cached data to
           be served even if readahead could not progress */
    }

    /* serve cached */
    if (in->ra_served < in->ra_read) {
        ret = iov_load(opvector, opcount, &in->ra_buf[in->ra_served],
                       min(req_len, (in->ra_read - in->ra_served)));

        in->ra_served += ret;
        /* Do not read uncached and cached in the same call */
        goto out;
    }

    if (in->ra_read < in->ra_max)
        /* If there was no cached data to be served, (and we are
           guaranteed to have already performed an attempt to progress
           readahead above), and we have not yet read out the full
           readahead capacity, then bail out for now without doing
           the uncached read below (as that will overtake future cached
           read)
        */
        goto out;
uncached:
    ret = __socket_ssl_readv(this, opvector, opcount);
out:
    return ret;
}

static gf_boolean_t
__does_socket_rwv_error_need_logging(socket_private_t *priv, int write)
{
    int read = !write;

    if (priv->connected == -1) /* Didn't even connect, of course it fails */
        return _gf_false;

    if (read && (priv->read_fail_log == _gf_false))
        return _gf_false;

    return _gf_true;
}

/*
 * return value:
 *   0 = success (completed)
 *  -1 = error
 * > 0 = incomplete
 */

static int
__socket_rwv(rpc_transport_t *this, struct iovec *vector, int count,
             struct iovec **pending_vector, int *pending_count, size_t *bytes,
             int write)
{
    socket_private_t *priv = NULL;
    int sock = -1;
    int ret = -1;
    struct iovec *opvector = NULL;
    int opcount = 0;
    int moved = 0;

    GF_VALIDATE_OR_GOTO("socket", this->private, out);

    priv = this->private;
    sock = priv->sock;

    opvector = vector;
    opcount = count;

    if (bytes != NULL) {
        *bytes = 0;
    }

    while (opcount > 0) {
        if (opvector->iov_len == 0) {
            gf_log(this->name, GF_LOG_DEBUG,
                   "would have passed zero length to read/write");
            ++opvector;
            --opcount;
            continue;
        }
        if (priv->use_ssl && !priv->ssl_ssl) {
            /*
             * We could end up here with priv->ssl_ssl still NULL
             * if (a) the connection failed and (b) some fool
             * called other socket functions anyway.  Demoting to
             * non-SSL might be insecure, so just fail it outright.
             */
            ret = -1;
            gf_log(this->name, GF_LOG_TRACE,
                   "### no priv->ssl_ssl yet; ret = -1;");
        } else if (write) {
            if (priv->use_ssl) {
                ret = ssl_write_one(this, opvector->iov_base,
                                    opvector->iov_len);
            } else {
                ret = sys_writev(sock, opvector, IOV_MIN(opcount));
            }

            if ((ret == 0) || ((ret < 0) && (errno == EAGAIN))) {
                /* done for now */
                break;
            } else if (ret > 0)
                this->total_bytes_write += ret;
        } else {
            ret = __socket_cached_read(this, opvector, opcount);
            if (ret == 0) {
                gf_log(this->name, GF_LOG_DEBUG,
                       "EOF on socket %d (errno:%d:%s); returning ENODATA",
                       sock, errno, strerror(errno));

                errno = ENODATA;
                ret = -1;
            }
            if ((ret < 0) && (errno == EAGAIN)) {
                /* done for now */
                break;
            } else if (ret > 0)
                this->total_bytes_read += ret;
        }

        if (ret == 0) {
            /* Mostly due to 'umount' in client */

            gf_log(this->name, GF_LOG_DEBUG, "EOF from peer %s",
                   this->peerinfo.identifier);
            opcount = -1;
            errno = ENOTCONN;
            break;
        }
        if (ret < 0) {
            if (errno == EINTR)
                continue;

            if (__does_socket_rwv_error_need_logging(priv, write)) {
                GF_LOG_OCCASIONALLY(priv->log_ctr, this->name, GF_LOG_WARNING,
                                    "%s on %s failed (%s)",
                                    write ? "writev" : "readv",
                                    this->peerinfo.identifier, strerror(errno));
            }

            if (priv->use_ssl && priv->ssl_ssl) {
                ssl_dump_error_stack(this->name);
            }
            opcount = -1;
            break;
        }

        if (bytes != NULL) {
            *bytes += ret;
        }

        moved = 0;

        while (moved < ret) {
            if (!opcount) {
                gf_log(this->name, GF_LOG_DEBUG, "ran out of iov, moved %d/%d",
                       moved, ret);
                goto ran_out;
            }
            if (!opvector[0].iov_len) {
                opvector++;
                opcount--;
                continue;
            }
            if ((ret - moved) >= opvector[0].iov_len) {
                moved += opvector[0].iov_len;
                opvector++;
                opcount--;
            } else {
                opvector[0].iov_len -= (ret - moved);
                opvector[0].iov_base += (ret - moved);
                moved += (ret - moved);
            }
        }
    }

ran_out:

    if (pending_vector)
        *pending_vector = opvector;

    if (pending_count)
        *pending_count = opcount;

out:
    return opcount;
}

static int
__socket_readv(rpc_transport_t *this, struct iovec *vector, int count,
               struct iovec **pending_vector, int *pending_count, size_t *bytes)
{
    return __socket_rwv(this, vector, count, pending_vector, pending_count,
                        bytes, 0);
}

static int
__socket_writev(rpc_transport_t *this, struct iovec *vector, int count,
                struct iovec **pending_vector, int *pending_count)
{
    return __socket_rwv(this, vector, count, pending_vector, pending_count,
                        NULL, 1);
}

static int
__socket_shutdown(rpc_transport_t *this)
{
    int ret = -1;
    socket_private_t *priv = this->private;

    priv->connected = -1;
    ret = shutdown(priv->sock, SHUT_RDWR);
    if (ret) {
        /* its already disconnected.. no need to understand
           why it failed to shutdown in normal cases */
        gf_log(this->name, GF_LOG_DEBUG, "shutdown() returned %d. %s", ret,
               strerror(errno));
    } else {
        GF_LOG_OCCASIONALLY(priv->shutdown_log_ctr, this->name, GF_LOG_INFO,
                            "intentional socket shutdown(%d)", priv->sock);
    }

    return ret;
}

static int
__socket_teardown_connection(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;

    priv = this->private;

    if (priv->use_ssl)
        ssl_teardown_connection(priv);

    return __socket_shutdown(this);
}

static int
__socket_disconnect(rpc_transport_t *this)
{
    int ret = -1;
    socket_private_t *priv = NULL;

    priv = this->private;

    gf_log(this->name, GF_LOG_TRACE, "disconnecting %p, sock=%d", this,
           priv->sock);

    if (priv->sock >= 0) {
        gf_log_callingfn(this->name, GF_LOG_TRACE,
                         "tearing down socket connection");
        ret = __socket_teardown_connection(this);
        if (ret) {
            gf_log(this->name, GF_LOG_DEBUG,
                   "__socket_teardown_connection () failed: %s",
                   strerror(errno));
        }
    }

    return ret;
}

static int
__socket_server_bind(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    glusterfs_ctx_t *ctx = NULL;
    cmd_args_t *cmd_args = NULL;
    struct sockaddr_storage unix_addr = {0};
    int ret = -1;
    int opt = 1;
    int reuse_check_sock = -1;
    uint16_t sin_port = 0;
    int retries = 0;

    priv = this->private;
    ctx = this->ctx;
    cmd_args = &ctx->cmd_args;

    ret = setsockopt(priv->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (ret != 0) {
        gf_log(this->name, GF_LOG_ERROR,
               "setsockopt() for SO_REUSEADDR failed (%s)", strerror(errno));
    }

    /* reuse-address doesn't work for unix type sockets */
    if (AF_UNIX == SA(&this->myinfo.sockaddr)->sa_family) {
        memcpy(&unix_addr, SA(&this->myinfo.sockaddr),
               this->myinfo.sockaddr_len);
        reuse_check_sock = sys_socket(AF_UNIX, SOCK_STREAM, 0);
        if (reuse_check_sock >= 0) {
            ret = connect(reuse_check_sock, SA(&unix_addr),
                          this->myinfo.sockaddr_len);
            if ((ret != 0) && (ECONNREFUSED == errno)) {
                sys_unlink(((struct sockaddr_un *)&unix_addr)->sun_path);
            }
            gf_log(this->name, GF_LOG_INFO,
                   "closing (AF_UNIX) reuse check socket %d", reuse_check_sock);
            sys_close(reuse_check_sock);
        }
    }

    if (AF_UNIX != SA(&this->myinfo.sockaddr)->sa_family) {
        sin_port = (int)ntohs(
            ((struct sockaddr_in *)&this->myinfo.sockaddr)->sin_port);
        if (!sin_port) {
            sin_port = GF_DEFAULT_SOCKET_LISTEN_PORT;
            ((struct sockaddr_in *)&this->myinfo.sockaddr)->sin_port = htons(
                sin_port);
        }
        retries = 10;
        while (retries) {
            ret = bind(priv->sock, (struct sockaddr *)&this->myinfo.sockaddr,
                       this->myinfo.sockaddr_len);
            if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "binding to %s failed: %s",
                       this->myinfo.identifier, strerror(errno));
                if (errno == EADDRINUSE) {
                    gf_log(this->name, GF_LOG_ERROR, "Port is already in use");
                    sleep(1);
                    retries--;
                } else {
                    break;
                }
            } else {
                break;
            }
        }
    } else {
        ret = bind(priv->sock, (struct sockaddr *)&this->myinfo.sockaddr,
                   this->myinfo.sockaddr_len);

        if (ret != 0) {
            gf_log(this->name, GF_LOG_ERROR, "binding to %s failed: %s",
                   this->myinfo.identifier, strerror(errno));
            if (errno == EADDRINUSE) {
                gf_log(this->name, GF_LOG_ERROR, "Port is already in use");
            }
        }
    }
    if (AF_UNIX != SA(&this->myinfo.sockaddr)->sa_family) {
        if (getsockname(priv->sock, SA(&this->myinfo.sockaddr),
                        &this->myinfo.sockaddr_len) != 0) {
            gf_log(this->name, GF_LOG_WARNING,
                   "getsockname on (%d) failed (%s)", priv->sock,
                   strerror(errno));
            ret = -1;
            goto out;
        }
        if (!cmd_args->brick_port) {
            cmd_args->brick_port = (int)ntohs(
                ((struct sockaddr_in *)&this->myinfo.sockaddr)->sin_port);
            gf_log(this->name, GF_LOG_INFO,
                   "process started listening on port (%d)",
                   cmd_args->brick_port);
        }
    }

out:
    return ret;
}

static int
__socket_nonblock(int fd)
{
    int flags = 0;
    int ret = -1;

    flags = fcntl(fd, F_GETFL);

    if (flags >= 0)
        ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return ret;
}

static int
__socket_nodelay(int fd)
{
    int on = 1;
    int ret = -1;

    ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    if (!ret)
        gf_log(THIS->name, GF_LOG_TRACE, "NODELAY enabled for socket %d", fd);

    return ret;
}

static int
__socket_keepalive(int fd, int family, int keepaliveintvl, int keepaliveidle,
                   int keepalivecnt, int timeout)
{
    int on = 1;
    int ret = -1;
#if defined(TCP_USER_TIMEOUT)
    int timeout_ms = timeout * 1000;
#endif

    ret = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
    if (ret != 0) {
        gf_log("socket", GF_LOG_WARNING,
               "failed to set keep alive option on socket %d", fd);
        goto err;
    }

    if (keepaliveintvl == GF_USE_DEFAULT_KEEPALIVE)
        goto done;

#if !defined(GF_LINUX_HOST_OS) && !defined(__NetBSD__)
#if defined(GF_SOLARIS_HOST_OS) || defined(__FreeBSD__)
    ret = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepaliveintvl,
                     sizeof(keepaliveintvl));
#else
    ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &keepaliveintvl,
                     sizeof(keepaliveintvl));
#endif
    if (ret != 0) {
        gf_log("socket", GF_LOG_WARNING,
               "failed to set keep alive interval on socket %d", fd);
        goto err;
    }
#else
    if (family != AF_INET && family != AF_INET6)
        goto done;

    ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepaliveidle,
                     sizeof(keepaliveidle));
    if (ret != 0) {
        gf_log("socket", GF_LOG_WARNING,
               "failed to set keep idle %d on socket %d, %s", keepaliveidle, fd,
               strerror(errno));
        goto err;
    }
    ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepaliveintvl,
                     sizeof(keepaliveintvl));
    if (ret != 0) {
        gf_log("socket", GF_LOG_WARNING,
               "failed to set keep interval %d on socket %d, %s",
               keepaliveintvl, fd, strerror(errno));
        goto err;
    }

#if defined(TCP_USER_TIMEOUT)
    if (timeout_ms < 0)
        goto done;
    ret = setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout_ms,
                     sizeof(timeout_ms));
    if (ret != 0) {
        gf_log("socket", GF_LOG_WARNING,
               "failed to set "
               "TCP_USER_TIMEOUT %d on socket %d, %s",
               timeout_ms, fd, strerror(errno));
        goto err;
    }
#endif
#if defined(TCP_KEEPCNT)
    ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepalivecnt,
                     sizeof(keepalivecnt));
    if (ret != 0) {
        gf_log("socket", GF_LOG_WARNING,
               "failed to set "
               "TCP_KEEPCNT %d on socket %d, %s",
               keepalivecnt, fd, strerror(errno));
        goto err;
    }
#endif
#endif

done:
    gf_log(THIS->name, GF_LOG_TRACE,
           "Keep-alive enabled for socket: %d, "
           "(idle: %d, interval: %d, max-probes: %d, timeout: %d)",
           fd, keepaliveidle, keepaliveintvl, keepalivecnt, timeout);

err:
    return ret;
}

static int
__socket_connect_finish(int fd)
{
    int ret = -1;
    int optval = 0;
    socklen_t optlen = sizeof(int);

    ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&optval, &optlen);

    if (ret == 0 && optval) {
        errno = optval;
        ret = -1;
    }

    return ret;
}

static void
__socket_reset(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;

    priv = this->private;

    /* TODO: use mem-pool on incoming data */

    if (priv->incoming.iobref) {
        iobref_unref(priv->incoming.iobref);
        priv->incoming.iobref = NULL;
    }

    if (priv->incoming.iobuf) {
        iobuf_unref(priv->incoming.iobuf);
        priv->incoming.iobuf = NULL;
    }

    GF_FREE(priv->incoming.request_info);

    memset(&priv->incoming, 0, sizeof(priv->incoming));

    gf_event_unregister_close(this->ctx->event_pool, priv->sock, priv->idx);
    if (priv->use_ssl && priv->ssl_ssl) {
        SSL_clear(priv->ssl_ssl);
        SSL_free(priv->ssl_ssl);
        priv->ssl_ssl = NULL;
    }
    if (priv->ssl_ctx) {
        SSL_CTX_free(priv->ssl_ctx);
        priv->ssl_ctx = NULL;
    }
    priv->sock = -1;
    priv->idx = -1;
    priv->connected = -1;
    priv->ssl_connected = _gf_false;
    priv->ssl_accepted = _gf_false;
    priv->ssl_context_created = _gf_false;

    if (priv->ssl_private_key) {
        GF_FREE(priv->ssl_private_key);
        priv->ssl_private_key = NULL;
    }
    if (priv->ssl_own_cert) {
        GF_FREE(priv->ssl_own_cert);
        priv->ssl_own_cert = NULL;
    }
    if (priv->ssl_ca_list) {
        GF_FREE(priv->ssl_ca_list);
        priv->ssl_ca_list = NULL;
    }
}

static void
socket_set_lastfrag(uint32_t *fragsize)
{
    (*fragsize) |= 0x80000000U;
}

static void
socket_set_frag_header_size(uint32_t size, char *haddr)
{
    size = htonl(size);
    memcpy(haddr, &size, sizeof(size));
}

static void
socket_set_last_frag_header_size(uint32_t size, char *haddr)
{
    socket_set_lastfrag(&size);
    socket_set_frag_header_size(size, haddr);
}

static struct ioq *
__socket_ioq_new(rpc_transport_t *this, rpc_transport_msg_t *msg)
{
    struct ioq *entry = NULL;
    int count = 0;
    uint32_t size = 0;

    /* TODO: use mem-pool */
    entry = GF_CALLOC(1, sizeof(*entry), gf_common_mt_ioq);
    if (!entry)
        return NULL;

    count = msg->rpchdrcount + msg->proghdrcount + msg->progpayloadcount;

    GF_ASSERT(count <= (MAX_IOVEC - 1));

    size = iov_length(msg->rpchdr, msg->rpchdrcount) +
           iov_length(msg->proghdr, msg->proghdrcount) +
           iov_length(msg->progpayload, msg->progpayloadcount);

    if (size > RPC_MAX_FRAGMENT_SIZE) {
        gf_log(this->name, GF_LOG_ERROR,
               "msg size (%u) bigger than the maximum allowed size on "
               "sockets (%u)",
               size, RPC_MAX_FRAGMENT_SIZE);
        GF_FREE(entry);
        return NULL;
    }

    socket_set_last_frag_header_size(size, (char *)&entry->fraghdr);

    entry->vector[0].iov_base = (char *)&entry->fraghdr;
    entry->vector[0].iov_len = sizeof(entry->fraghdr);
    entry->count = 1;

    if (msg->rpchdr != NULL) {
        memcpy(&entry->vector[1], msg->rpchdr,
               sizeof(struct iovec) * msg->rpchdrcount);
        entry->count += msg->rpchdrcount;
    }

    if (msg->proghdr != NULL) {
        memcpy(&entry->vector[entry->count], msg->proghdr,
               sizeof(struct iovec) * msg->proghdrcount);
        entry->count += msg->proghdrcount;
    }

    if (msg->progpayload != NULL) {
        memcpy(&entry->vector[entry->count], msg->progpayload,
               sizeof(struct iovec) * msg->progpayloadcount);
        entry->count += msg->progpayloadcount;
    }

    entry->pending_vector = entry->vector;
    entry->pending_count = entry->count;

    if (msg->iobref != NULL)
        entry->iobref = iobref_ref(msg->iobref);

    INIT_LIST_HEAD(&entry->list);

    return entry;
}

static void
__socket_ioq_entry_free(struct ioq *entry)
{
    GF_VALIDATE_OR_GOTO("socket", entry, out);

    list_del_init(&entry->list);
    if (entry->iobref)
        iobref_unref(entry->iobref);

    /* TODO: use mem-pool */
    GF_FREE(entry);

out:
    return;
}

static void
__socket_ioq_flush(socket_private_t *priv)
{
    struct ioq *entry = NULL;

    while (!list_empty(&priv->ioq)) {
        entry = priv->ioq_next;
        __socket_ioq_entry_free(entry);
    }
}

static int
__socket_ioq_churn_entry(rpc_transport_t *this, struct ioq *entry)
{
    int ret = -1;

    ret = __socket_writev(this, entry->pending_vector, entry->pending_count,
                          &entry->pending_vector, &entry->pending_count);

    if (ret == 0) {
        /* current entry was completely written */
        GF_ASSERT(entry->pending_count == 0);
        __socket_ioq_entry_free(entry);
    }

    return ret;
}

static int
__socket_ioq_churn(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    int ret = 0;
    struct ioq *entry = NULL;

    priv = this->private;

    while (!list_empty(&priv->ioq)) {
        /* pick next entry */
        entry = priv->ioq_next;

        ret = __socket_ioq_churn_entry(this, entry);

        if (ret != 0)
            break;
    }

    if (list_empty(&priv->ioq)) {
        /* all pending writes done, not interested in POLLOUT */
        priv->idx = gf_event_select_on(this->ctx->event_pool, priv->sock,
                                       priv->idx, -1, 0);
    }

    return ret;
}

static gf_boolean_t
socket_event_poll_err(rpc_transport_t *this, int gen, int idx)
{
    socket_private_t *priv = NULL;
    gf_boolean_t socket_closed = _gf_false;

    priv = this->private;

    pthread_mutex_lock(&priv->out_lock);
    {
        if ((priv->gen == gen) && (priv->idx == idx) && (priv->sock >= 0)) {
            __socket_ioq_flush(priv);
            __socket_reset(this);
            socket_closed = _gf_true;
        }
    }
    pthread_mutex_unlock(&priv->out_lock);

    if (socket_closed) {
        pthread_mutex_lock(&priv->notify.lock);
        {
            while (priv->notify.in_progress)
                pthread_cond_wait(&priv->notify.cond, &priv->notify.lock);
        }
        pthread_mutex_unlock(&priv->notify.lock);

        rpc_transport_notify(this, RPC_TRANSPORT_DISCONNECT, this);
    }

    return socket_closed;
}

static int
socket_event_poll_out(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    int ret = -1;

    priv = this->private;

    pthread_mutex_lock(&priv->out_lock);
    {
        if (priv->connected == 1) {
            ret = __socket_ioq_churn(this);

            if (ret < 0) {
                gf_log(this->name, GF_LOG_TRACE,
                       "__socket_ioq_churn returned -1; "
                       "disconnecting socket");
                __socket_disconnect(this);
            }
        }
    }
    pthread_mutex_unlock(&priv->out_lock);

    if (ret == 0)
        rpc_transport_notify(this, RPC_TRANSPORT_MSG_SENT, NULL);

    if (ret > 0)
        ret = 0;

    return ret;
}

static int
__socket_read_simple_msg(rpc_transport_t *this)
{
    int ret = 0;
    uint32_t remaining_size = 0;
    size_t bytes_read = 0;
    socket_private_t *priv = NULL;
    struct gf_sock_incoming *in = NULL;
    struct gf_sock_incoming_frag *frag = NULL;

    GF_VALIDATE_OR_GOTO("socket", this, out);
    GF_VALIDATE_OR_GOTO("socket", this->private, out);

    priv = this->private;

    in = &priv->incoming;
    frag = &in->frag;

    switch (frag->simple_state) {
        case SP_STATE_SIMPLE_MSG_INIT:
            remaining_size = RPC_FRAGSIZE(in->fraghdr) - frag->bytes_read;

            __socket_proto_init_pending(priv, remaining_size);

            frag->simple_state = SP_STATE_READING_SIMPLE_MSG;

            /* fall through */

        case SP_STATE_READING_SIMPLE_MSG:
            ret = 0;

            remaining_size = RPC_FRAGSIZE(in->fraghdr) - frag->bytes_read;

            if (remaining_size > 0) {
                ret = __socket_readv(this, in->pending_vector, 1,
                                     &in->pending_vector, &in->pending_count,
                                     &bytes_read);
            }

            if (ret < 0) {
                gf_log(this->name, GF_LOG_WARNING,
                       "reading from socket failed. Error (%s), "
                       "peer (%s)",
                       strerror(errno), this->peerinfo.identifier);
                break;
            }

            frag->bytes_read += bytes_read;
            frag->fragcurrent += bytes_read;

            if (ret > 0) {
                gf_log(this->name, GF_LOG_TRACE,
                       "partial read on non-blocking socket.");
                ret = 0;
                break;
            }

            if (ret == 0) {
                frag->simple_state = SP_STATE_SIMPLE_MSG_INIT;
            }
    }

out:
    return ret;
}

#define rpc_cred_addr(buf) (buf + RPC_MSGTYPE_SIZE + RPC_CALL_BODY_SIZE - 4)

#define rpc_verf_addr(fragcurrent) (fragcurrent - 4)

#define rpc_msgtype_addr(buf) (buf + 4)

#define rpc_prognum_addr(buf) (buf + RPC_MSGTYPE_SIZE + 4)
#define rpc_progver_addr(buf) (buf + RPC_MSGTYPE_SIZE + 8)
#define rpc_procnum_addr(buf) (buf + RPC_MSGTYPE_SIZE + 12)

static int
__socket_read_vectored_request(rpc_transport_t *this,
                               rpcsvc_vector_sizer vector_sizer)
{
    socket_private_t *priv = NULL;
    int ret = 0;
    uint32_t credlen = 0, verflen = 0;
    char *addr = NULL;
    struct iobuf *iobuf = NULL;
    uint32_t remaining_size = 0;
    ssize_t readsize = 0;
    size_t size = 0;
    struct gf_sock_incoming *in = NULL;
    struct gf_sock_incoming_frag *frag = NULL;
    sp_rpcfrag_request_state_t *request = NULL;

    priv = this->private;

    /* used to reduce the indirection */
    in = &priv->incoming;
    frag = &in->frag;
    request = &frag->call_body.request;

    switch (request->vector_state) {
        case SP_STATE_VECTORED_REQUEST_INIT:
            request->vector_sizer_state = 0;

            addr = rpc_cred_addr(iobuf_ptr(in->iobuf));

            /* also read verf flavour and verflen */
            credlen = ntoh32(*((uint32_t *)addr)) +
                      RPC_AUTH_FLAVOUR_N_LENGTH_SIZE;

            __socket_proto_init_pending(priv, credlen);

            request->vector_state = SP_STATE_READING_CREDBYTES;

            /* fall through */

        case SP_STATE_READING_CREDBYTES:
            __socket_proto_read(priv, ret);

            request->vector_state = SP_STATE_READ_CREDBYTES;

            /* fall through */

        case SP_STATE_READ_CREDBYTES:
            addr = rpc_verf_addr(frag->fragcurrent);
            verflen = ntoh32(*((uint32_t *)addr));

            if (verflen == 0) {
                request->vector_state = SP_STATE_READ_VERFBYTES;
                goto sp_state_read_verfbytes;
            }
            __socket_proto_init_pending(priv, verflen);

            request->vector_state = SP_STATE_READING_VERFBYTES;

            /* fall through */

        case SP_STATE_READING_VERFBYTES:
            __socket_proto_read(priv, ret);

            request->vector_state = SP_STATE_READ_VERFBYTES;

            /* fall through */

        case SP_STATE_READ_VERFBYTES:
        sp_state_read_verfbytes:
            /* set the base_addr 'persistently' across multiple calls
               into the state machine */
            in->proghdr_base_addr = frag->fragcurrent;

            request->vector_sizer_state = vector_sizer(
                request->vector_sizer_state, &readsize, in->proghdr_base_addr,
                frag->fragcurrent);
            __socket_proto_init_pending(priv, readsize);

            request->vector_state = SP_STATE_READING_PROGHDR;

            /* fall through */

        case SP_STATE_READING_PROGHDR:
            __socket_proto_read(priv, ret);

            request->vector_state = SP_STATE_READ_PROGHDR;

            /* fall through */

        case SP_STATE_READ_PROGHDR:
        sp_state_read_proghdr:
            request->vector_sizer_state = vector_sizer(
                request->vector_sizer_state, &readsize, in->proghdr_base_addr,
                frag->fragcurrent);
            if (readsize == 0) {
                request->vector_state = SP_STATE_READ_PROGHDR_XDATA;
                goto sp_state_read_proghdr_xdata;
            }

            __socket_proto_init_pending(priv, readsize);

            request->vector_state = SP_STATE_READING_PROGHDR_XDATA;

            /* fall through */

        case SP_STATE_READING_PROGHDR_XDATA:
            __socket_proto_read(priv, ret);

            request->vector_state = SP_STATE_READ_PROGHDR;
            /* check if the vector_sizer() has more to say */
            goto sp_state_read_proghdr;

        case SP_STATE_READ_PROGHDR_XDATA:
        sp_state_read_proghdr_xdata:
            if (in->payload_vector.iov_base == NULL) {
                size = RPC_FRAGSIZE(in->fraghdr) - frag->bytes_read;
                iobuf = iobuf_get2(this->ctx->iobuf_pool, size);
                if (!iobuf) {
                    ret = -1;
                    break;
                }

                if (in->iobref == NULL) {
                    in->iobref = iobref_new();
                    if (in->iobref == NULL) {
                        ret = -1;
                        iobuf_unref(iobuf);
                        break;
                    }
                }

                iobref_add(in->iobref, iobuf);

                in->payload_vector.iov_base = iobuf_ptr(iobuf);
                frag->fragcurrent = iobuf_ptr(iobuf);

                iobuf_unref(iobuf);
            }

            request->vector_state = SP_STATE_READING_PROG;

            /* fall through */

        case SP_STATE_READING_PROG:
            /* now read the remaining rpc msg into buffer pointed by
             * fragcurrent
             */

            ret = __socket_read_simple_msg(this);

            remaining_size = RPC_FRAGSIZE(in->fraghdr) - frag->bytes_read;

            if ((ret < 0) || ((ret == 0) && (remaining_size == 0) &&
                              RPC_LASTFRAG(in->fraghdr))) {
                request->vector_state = SP_STATE_VECTORED_REQUEST_INIT;
                in->payload_vector.iov_len = ((unsigned long)frag->fragcurrent -
                                              (unsigned long)
                                                  in->payload_vector.iov_base);
            }
            break;
    }

    return ret;
}

static int
__socket_read_request(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    uint32_t prognum = 0, procnum = 0, progver = 0;
    uint32_t remaining_size = 0;
    int ret = -1;
    char *buf = NULL;
    rpcsvc_vector_sizer vector_sizer = NULL;
    struct gf_sock_incoming *in = NULL;
    struct gf_sock_incoming_frag *frag = NULL;
    sp_rpcfrag_request_state_t *request = NULL;

    priv = this->private;

    /* used to reduce the indirection */
    in = &priv->incoming;
    frag = &in->frag;
    request = &frag->call_body.request;

    switch (request->header_state) {
        case SP_STATE_REQUEST_HEADER_INIT:

            __socket_proto_init_pending(priv, RPC_CALL_BODY_SIZE);

            request->header_state = SP_STATE_READING_RPCHDR1;

            /* fall through */

        case SP_STATE_READING_RPCHDR1:
            __socket_proto_read(priv, ret);

            request->header_state = SP_STATE_READ_RPCHDR1;

            /* fall through */

        case SP_STATE_READ_RPCHDR1:
            buf = rpc_prognum_addr(iobuf_ptr(in->iobuf));
            prognum = ntoh32(*((uint32_t *)buf));

            buf = rpc_progver_addr(iobuf_ptr(in->iobuf));
            progver = ntoh32(*((uint32_t *)buf));

            buf = rpc_procnum_addr(iobuf_ptr(in->iobuf));
            procnum = ntoh32(*((uint32_t *)buf));

            if (priv->is_server) {
                /* this check is needed as rpcsvc and rpc-clnt
                 * actor structures are not same */
                vector_sizer = rpcsvc_get_program_vector_sizer(
                    (rpcsvc_t *)this->mydata, prognum, progver, procnum);
            }

            if (vector_sizer) {
                ret = __socket_read_vectored_request(this, vector_sizer);
            } else {
                ret = __socket_read_simple_msg(this);
            }

            remaining_size = RPC_FRAGSIZE(in->fraghdr) - frag->bytes_read;

            if ((ret < 0) || ((ret == 0) && (remaining_size == 0) &&
                              (RPC_LASTFRAG(in->fraghdr)))) {
                request->header_state = SP_STATE_REQUEST_HEADER_INIT;
            }

            break;
    }

    return ret;
}

static int
__socket_read_accepted_successful_reply(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    int ret = 0;
    struct iobuf *iobuf = NULL;
    gfs3_read_rsp read_rsp = {
        0,
    };
    ssize_t size = 0;
    ssize_t default_read_size = 0;
    XDR xdr;
    struct gf_sock_incoming *in = NULL;
    struct gf_sock_incoming_frag *frag = NULL;
    uint32_t remaining_size = 0;

    priv = this->private;

    /* used to reduce the indirection */
    in = &priv->incoming;
    frag = &in->frag;

    switch (frag->call_body.reply.accepted_success_state) {
        case SP_STATE_ACCEPTED_SUCCESS_REPLY_INIT:
            default_read_size = xdr_sizeof((xdrproc_t)xdr_gfs3_read_rsp,
                                           &read_rsp);

            /* We need to store the current base address because we will
             * need it after a partial read. */
            in->proghdr_base_addr = frag->fragcurrent;

            __socket_proto_init_pending(priv, default_read_size);

            frag->call_body.reply
                .accepted_success_state = SP_STATE_READING_PROC_HEADER;

            /* fall through */

        case SP_STATE_READING_PROC_HEADER:
            __socket_proto_read(priv, ret);

            /* there can be 'xdata' in read response, figure it out */
            default_read_size = frag->fragcurrent - in->proghdr_base_addr;
            xdrmem_create(&xdr, in->proghdr_base_addr, default_read_size,
                          XDR_DECODE);

            /* This will fail if there is xdata sent from server, if not,
               well and good, we don't need to worry about  */
            xdr_gfs3_read_rsp(&xdr, &read_rsp);

            free(read_rsp.xdata.xdata_val);

            /* need to round off to proper gf_roof (%4), as XDR packing pads
               the end of opaque object with '0' */
            size = gf_roof(read_rsp.xdata.xdata_len, 4);

            if (!size) {
                frag->call_body.reply
                    .accepted_success_state = SP_STATE_READ_PROC_OPAQUE;
                goto read_proc_opaque;
            }

            __socket_proto_init_pending(priv, size);

            frag->call_body.reply
                .accepted_success_state = SP_STATE_READING_PROC_OPAQUE;
            /* fall through */

        case SP_STATE_READING_PROC_OPAQUE:
            __socket_proto_read(priv, ret);

            frag->call_body.reply
                .accepted_success_state = SP_STATE_READ_PROC_OPAQUE;
            /* fall through */

        case SP_STATE_READ_PROC_OPAQUE:
        read_proc_opaque:
            if (in->payload_vector.iov_base == NULL) {
                size = (RPC_FRAGSIZE(in->fraghdr) - frag->bytes_read);

                iobuf = iobuf_get2(this->ctx->iobuf_pool, size);
                if (iobuf == NULL) {
                    ret = -1;
                    goto out;
                }

                if (in->iobref == NULL) {
                    in->iobref = iobref_new();
                    if (in->iobref == NULL) {
                        ret = -1;
                        iobuf_unref(iobuf);
                        goto out;
                    }
                }

                ret = iobref_add(in->iobref, iobuf);
                iobuf_unref(iobuf);
                if (ret < 0) {
                    goto out;
                }

                in->payload_vector.iov_base = iobuf_ptr(iobuf);
                in->payload_vector.iov_len = size;
            }

            frag->fragcurrent = in->payload_vector.iov_base;

            frag->call_body.reply
                .accepted_success_state = SP_STATE_READ_PROC_HEADER;

            /* fall through */

        case SP_STATE_READ_PROC_HEADER:
            /* now read the entire remaining msg into new iobuf */
            ret = __socket_read_simple_msg(this);
            remaining_size = RPC_FRAGSIZE(in->fraghdr) - frag->bytes_read;
            if ((ret < 0) || ((ret == 0) && (remaining_size == 0) &&
                              RPC_LASTFRAG(in->fraghdr))) {
                frag->call_body.reply.accepted_success_state =
                    SP_STATE_ACCEPTED_SUCCESS_REPLY_INIT;
            }

            break;
    }

out:
    return ret;
}

static int
__socket_read_accepted_successful_reply_v2(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    int ret = 0;
    struct iobuf *iobuf = NULL;
    gfx_read_rsp read_rsp = {
        0,
    };
    ssize_t size = 0;
    ssize_t default_read_size = 0;
    XDR xdr;
    struct gf_sock_incoming *in = NULL;
    struct gf_sock_incoming_frag *frag = NULL;
    uint32_t remaining_size = 0;

    priv = this->private;

    /* used to reduce the indirection */
    in = &priv->incoming;
    frag = &in->frag;

    switch (frag->call_body.reply.accepted_success_state) {
        case SP_STATE_ACCEPTED_SUCCESS_REPLY_INIT:
            default_read_size = xdr_sizeof((xdrproc_t)xdr_gfx_read_rsp,
                                           &read_rsp);

            /* We need to store the current base address because we will
             * need it after a partial read. */
            in->proghdr_base_addr = frag->fragcurrent;

            __socket_proto_init_pending(priv, default_read_size);

            frag->call_body.reply
                .accepted_success_state = SP_STATE_READING_PROC_HEADER;

            /* fall through */

        case SP_STATE_READING_PROC_HEADER:
            __socket_proto_read(priv, ret);

            /* there can be 'xdata' in read response, figure it out */
            default_read_size = frag->fragcurrent - in->proghdr_base_addr;

            xdrmem_create(&xdr, in->proghdr_base_addr, default_read_size,
                          XDR_DECODE);

            /* This will fail if there is xdata sent from server, if not,
               well and good, we don't need to worry about  */
            xdr_gfx_read_rsp(&xdr, &read_rsp);

            free(read_rsp.xdata.pairs.pairs_val);

            /* need to round off to proper gf_roof (%4), as XDR packing pads
               the end of opaque object with '0' */
            size = gf_roof(read_rsp.xdata.xdr_size, 4);

            if (!size) {
                frag->call_body.reply
                    .accepted_success_state = SP_STATE_READ_PROC_OPAQUE;
                goto read_proc_opaque;
            }

            __socket_proto_init_pending(priv, size);

            frag->call_body.reply
                .accepted_success_state = SP_STATE_READING_PROC_OPAQUE;
            /* fall through */

        case SP_STATE_READING_PROC_OPAQUE:
            __socket_proto_read(priv, ret);

            frag->call_body.reply
                .accepted_success_state = SP_STATE_READ_PROC_OPAQUE;
            /* fall through */

        case SP_STATE_READ_PROC_OPAQUE:
        read_proc_opaque:
            if (in->payload_vector.iov_base == NULL) {
                size = (RPC_FRAGSIZE(in->fraghdr) - frag->bytes_read);

                iobuf = iobuf_get2(this->ctx->iobuf_pool, size);
                if (iobuf == NULL) {
                    ret = -1;
                    goto out;
                }

                if (in->iobref == NULL) {
                    in->iobref = iobref_new();
                    if (in->iobref == NULL) {
                        ret = -1;
                        iobuf_unref(iobuf);
                        goto out;
                    }
                }

                ret = iobref_add(in->iobref, iobuf);
                iobuf_unref(iobuf);
                if (ret < 0) {
                    goto out;
                }

                in->payload_vector.iov_base = iobuf_ptr(iobuf);
                in->payload_vector.iov_len = size;
            }

            frag->fragcurrent = in->payload_vector.iov_base;

            frag->call_body.reply
                .accepted_success_state = SP_STATE_READ_PROC_HEADER;

            /* fall through */

        case SP_STATE_READ_PROC_HEADER:
            /* now read the entire remaining msg into new iobuf */
            ret = __socket_read_simple_msg(this);
            remaining_size = RPC_FRAGSIZE(in->fraghdr) - frag->bytes_read;
            if ((ret < 0) || ((ret == 0) && (remaining_size == 0) &&
                              RPC_LASTFRAG(in->fraghdr))) {
                frag->call_body.reply.accepted_success_state =
                    SP_STATE_ACCEPTED_SUCCESS_REPLY_INIT;
            }

            break;
    }

out:
    return ret;
}

#define rpc_reply_verflen_addr(fragcurrent) ((char *)fragcurrent - 4)
#define rpc_reply_accept_status_addr(fragcurrent) ((char *)fragcurrent - 4)

static int
__socket_read_accepted_reply(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    int ret = -1;
    char *buf = NULL;
    uint32_t verflen = 0, len = 0;
    uint32_t remaining_size = 0;
    struct gf_sock_incoming *in = NULL;
    struct gf_sock_incoming_frag *frag = NULL;

    priv = this->private;
    /* used to reduce the indirection */
    in = &priv->incoming;
    frag = &in->frag;

    switch (frag->call_body.reply.accepted_state) {
        case SP_STATE_ACCEPTED_REPLY_INIT:
            __socket_proto_init_pending(priv, RPC_AUTH_FLAVOUR_N_LENGTH_SIZE);

            frag->call_body.reply
                .accepted_state = SP_STATE_READING_REPLY_VERFLEN;

            /* fall through */

        case SP_STATE_READING_REPLY_VERFLEN:
            __socket_proto_read(priv, ret);

            frag->call_body.reply.accepted_state = SP_STATE_READ_REPLY_VERFLEN;

            /* fall through */

        case SP_STATE_READ_REPLY_VERFLEN:
            buf = rpc_reply_verflen_addr(frag->fragcurrent);

            verflen = ntoh32(*((uint32_t *)buf));

            /* also read accept status along with verf data */
            len = verflen + RPC_ACCEPT_STATUS_LEN;

            __socket_proto_init_pending(priv, len);

            frag->call_body.reply
                .accepted_state = SP_STATE_READING_REPLY_VERFBYTES;

            /* fall through */

        case SP_STATE_READING_REPLY_VERFBYTES:
            __socket_proto_read(priv, ret);

            frag->call_body.reply
                .accepted_state = SP_STATE_READ_REPLY_VERFBYTES;

            buf = rpc_reply_accept_status_addr(frag->fragcurrent);

            frag->call_body.reply.accept_status = ntoh32(*(uint32_t *)buf);

            /* fall through */

        case SP_STATE_READ_REPLY_VERFBYTES:

            if (frag->call_body.reply.accept_status == SUCCESS) {
                /* Need two different methods here for different protocols
                   Mainly because the exact XDR is used to calculate the
                   size of response */
                if ((in->request_info->procnum == GFS3_OP_READ) &&
                    (in->request_info->prognum == GLUSTER_FOP_PROGRAM) &&
                    (in->request_info->progver == GLUSTER_FOP_VERSION_v2)) {
                    ret = __socket_read_accepted_successful_reply_v2(this);
                } else {
                    ret = __socket_read_accepted_successful_reply(this);
                }
            } else {
                /* read entire remaining msg into buffer pointed to by
                 * fragcurrent
                 */
                ret = __socket_read_simple_msg(this);
            }

            remaining_size = RPC_FRAGSIZE(in->fraghdr) - frag->bytes_read;

            if ((ret < 0) || ((ret == 0) && (remaining_size == 0) &&
                              (RPC_LASTFRAG(in->fraghdr)))) {
                frag->call_body.reply
                    .accepted_state = SP_STATE_ACCEPTED_REPLY_INIT;
            }

            break;
    }

    return ret;
}

static int
__socket_read_denied_reply(rpc_transport_t *this)
{
    return __socket_read_simple_msg(this);
}

#define rpc_reply_status_addr(fragcurrent) ((char *)fragcurrent - 4)

static int
__socket_read_vectored_reply(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    int ret = 0;
    char *buf = NULL;
    uint32_t remaining_size = 0;
    struct gf_sock_incoming *in = NULL;
    struct gf_sock_incoming_frag *frag = NULL;

    priv = this->private;
    in = &priv->incoming;
    frag = &in->frag;

    switch (frag->call_body.reply.status_state) {
        case SP_STATE_ACCEPTED_REPLY_INIT:
            __socket_proto_init_pending(priv, RPC_REPLY_STATUS_SIZE);

            frag->call_body.reply.status_state = SP_STATE_READING_REPLY_STATUS;

            /* fall through */

        case SP_STATE_READING_REPLY_STATUS:
            __socket_proto_read(priv, ret);

            buf = rpc_reply_status_addr(frag->fragcurrent);

            frag->call_body.reply.accept_status = ntoh32(*((uint32_t *)buf));

            frag->call_body.reply.status_state = SP_STATE_READ_REPLY_STATUS;

            /* fall through */

        case SP_STATE_READ_REPLY_STATUS:
            if (frag->call_body.reply.accept_status == MSG_ACCEPTED) {
                ret = __socket_read_accepted_reply(this);
            } else {
                ret = __socket_read_denied_reply(this);
            }

            remaining_size = RPC_FRAGSIZE(in->fraghdr) - frag->bytes_read;

            if ((ret < 0) || ((ret == 0) && (remaining_size == 0) &&
                              (RPC_LASTFRAG(in->fraghdr)))) {
                frag->call_body.reply
                    .status_state = SP_STATE_VECTORED_REPLY_STATUS_INIT;
                in->payload_vector.iov_len = (unsigned long)frag->fragcurrent -
                                             (unsigned long)
                                                 in->payload_vector.iov_base;
            }
            break;
    }

    return ret;
}

static int
__socket_read_simple_reply(rpc_transport_t *this)
{
    return __socket_read_simple_msg(this);
}

#define rpc_xid_addr(buf) (buf)

static int
__socket_read_reply(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    char *buf = NULL;
    int32_t ret = -1;
    rpc_request_info_t *request_info = NULL;
    char map_xid = 0;
    struct gf_sock_incoming *in = NULL;
    struct gf_sock_incoming_frag *frag = NULL;

    priv = this->private;
    in = &priv->incoming;
    frag = &in->frag;

    buf = rpc_xid_addr(iobuf_ptr(in->iobuf));

    if (in->request_info == NULL) {
        in->request_info = GF_CALLOC(1, sizeof(*request_info),
                                     gf_common_mt_rpc_trans_reqinfo_t);
        if (in->request_info == NULL) {
            goto out;
        }

        map_xid = 1;
    }

    request_info = in->request_info;

    if (map_xid) {
        request_info->xid = ntoh32(*((uint32_t *)buf));

        /* release priv->lock, so as to avoid deadlock b/w conn->lock
         * and priv->lock, since we are doing an upcall here.
         */
        frag->state = SP_STATE_NOTIFYING_XID;
        ret = rpc_transport_notify(this, RPC_TRANSPORT_MAP_XID_REQUEST,
                                   in->request_info);

        /* Transition back to externally visible state. */
        frag->state = SP_STATE_READ_MSGTYPE;

        if (ret < 0) {
            gf_log(this->name, GF_LOG_WARNING,
                   "notify for event MAP_XID failed for %s",
                   this->peerinfo.identifier);
            goto out;
        }
    }

    if ((request_info->prognum == GLUSTER_FOP_PROGRAM) &&
        (request_info->procnum == GF_FOP_READ)) {
        if (map_xid && request_info->rsp.rsp_payload_count != 0) {
            in->iobref = iobref_ref(request_info->rsp.rsp_iobref);
            in->payload_vector = *request_info->rsp.rsp_payload;
        }

        ret = __socket_read_vectored_reply(this);
    } else {
        ret = __socket_read_simple_reply(this);
    }
out:
    return ret;
}

/* returns the number of bytes yet to be read in a fragment */
static int
__socket_read_frag(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    int32_t ret = 0;
    char *buf = NULL;
    uint32_t remaining_size = 0;
    struct gf_sock_incoming *in = NULL;
    struct gf_sock_incoming_frag *frag = NULL;

    priv = this->private;
    /* used to reduce the indirection */
    in = &priv->incoming;
    frag = &in->frag;

    switch (frag->state) {
        case SP_STATE_NADA:
            __socket_proto_init_pending(priv, RPC_MSGTYPE_SIZE);

            frag->state = SP_STATE_READING_MSGTYPE;

            /* fall through */

        case SP_STATE_READING_MSGTYPE:
            __socket_proto_read(priv, ret);

            frag->state = SP_STATE_READ_MSGTYPE;
            /* fall through */

        case SP_STATE_READ_MSGTYPE:
            buf = rpc_msgtype_addr(iobuf_ptr(in->iobuf));
            in->msg_type = ntoh32(*((uint32_t *)buf));

            if (in->msg_type == CALL) {
                ret = __socket_read_request(this);
            } else if (in->msg_type == REPLY) {
                ret = __socket_read_reply(this);
            } else if (in->msg_type == (msg_type_t)GF_UNIVERSAL_ANSWER) {
                gf_log("rpc", GF_LOG_ERROR,
                       "older version of protocol/process trying to "
                       "connect from %s. use newer version on that node",
                       this->peerinfo.identifier);
            } else {
                gf_log("rpc", GF_LOG_ERROR,
                       "wrong MSG-TYPE (%d) received from %s", in->msg_type,
                       this->peerinfo.identifier);
                ret = -1;
            }

            remaining_size = RPC_FRAGSIZE(in->fraghdr) - frag->bytes_read;

            if ((ret < 0) || ((ret == 0) && (remaining_size == 0) &&
                              (RPC_LASTFRAG(in->fraghdr)))) {
                /* frag->state = SP_STATE_NADA; */
                frag->state = SP_STATE_RPCFRAG_INIT;
            }

            break;

        case SP_STATE_NOTIFYING_XID:
            /* Another epoll thread is notifying higher layers
             *of reply's xid. */
            errno = EAGAIN;
            return -1;
            break;
    }

    return ret;
}

static void
__socket_reset_priv(socket_private_t *priv)
{
    struct gf_sock_incoming *in = NULL;

    /* used to reduce the indirection */
    in = &priv->incoming;

    if (in->iobref) {
        iobref_unref(in->iobref);
        in->iobref = NULL;
    }

    if (in->iobuf) {
        iobuf_unref(in->iobuf);
        in->iobuf = NULL;
    }

    if (in->request_info != NULL) {
        GF_FREE(in->request_info);
        in->request_info = NULL;
    }

    memset(&in->payload_vector, 0, sizeof(in->payload_vector));
}

static int
__socket_proto_state_machine(rpc_transport_t *this,
                             rpc_transport_pollin_t **pollin)
{
    int ret = -1;
    socket_private_t *priv = NULL;
    struct iobuf *iobuf = NULL;
    struct iobref *iobref = NULL;
    struct iovec vector[2];
    struct gf_sock_incoming *in = NULL;
    struct gf_sock_incoming_frag *frag = NULL;

    GF_VALIDATE_OR_GOTO("socket", this, out);
    GF_VALIDATE_OR_GOTO("socket", this->private, out);

    priv = this->private;
    /* used to reduce the indirection */
    in = &priv->incoming;
    frag = &in->frag;

    while (in->record_state != SP_STATE_COMPLETE) {
        switch (in->record_state) {
            case SP_STATE_NADA:
                in->total_bytes_read = 0;
                in->payload_vector.iov_len = 0;

                in->pending_vector = in->vector;
                in->pending_vector->iov_base = &in->fraghdr;

                in->pending_vector->iov_len = sizeof(in->fraghdr);

                in->record_state = SP_STATE_READING_FRAGHDR;

                /* fall through */

            case SP_STATE_READING_FRAGHDR:
                ret = __socket_readv(this, in->pending_vector, 1,
                                     &in->pending_vector, &in->pending_count,
                                     NULL);
                if (ret < 0)
                    goto out;

                if (ret > 0) {
                    gf_log(this->name, GF_LOG_TRACE,
                           "partial "
                           "fragment header read");
                    ret = 0;
                    goto out;
                }

                if (ret == 0) {
                    in->record_state = SP_STATE_READ_FRAGHDR;
                }
                /* fall through */

            case SP_STATE_READ_FRAGHDR:

                in->fraghdr = ntoh32(in->fraghdr);
                in->total_bytes_read += RPC_FRAGSIZE(in->fraghdr);

                if (in->total_bytes_read >= GF_UNIT_GB) {
                    ret = -1;
                    goto out;
                }

                iobuf = iobuf_get2(
                    this->ctx->iobuf_pool,
                    (in->total_bytes_read + sizeof(in->fraghdr)));
                if (!iobuf) {
                    ret = -1;
                    goto out;
                }

                if (in->iobuf == NULL) {
                    /* first fragment */
                    frag->fragcurrent = iobuf_ptr(iobuf);
                } else {
                    /* second or further fragment */
                    memcpy(iobuf_ptr(iobuf), iobuf_ptr(in->iobuf),
                           in->total_bytes_read - RPC_FRAGSIZE(in->fraghdr));
                    iobuf_unref(in->iobuf);
                    frag->fragcurrent = (char *)iobuf_ptr(iobuf) +
                                        in->total_bytes_read -
                                        RPC_FRAGSIZE(in->fraghdr);
                    frag->pending_vector->iov_base = frag->fragcurrent;
                    in->pending_vector = frag->pending_vector;
                }

                in->iobuf = iobuf;
                in->iobuf_size = 0;
                in->record_state = SP_STATE_READING_FRAG;
                /* fall through */

            case SP_STATE_READING_FRAG:
                ret = __socket_read_frag(this);

                if ((ret < 0) ||
                    (frag->bytes_read != RPC_FRAGSIZE(in->fraghdr))) {
                    goto out;
                }

                frag->bytes_read = 0;

                if (!RPC_LASTFRAG(in->fraghdr)) {
                    in->pending_vector = in->vector;
                    in->pending_vector->iov_base = &in->fraghdr;
                    in->pending_vector->iov_len = sizeof(in->fraghdr);
                    in->record_state = SP_STATE_READING_FRAGHDR;
                    break;
                }

                /* we've read the entire rpc record, notify the
                 * upper layers.
                 */
                if (pollin != NULL) {
                    int count = 0;
                    in->iobuf_size = (in->total_bytes_read -
                                      in->payload_vector.iov_len);

                    memset(vector, 0, sizeof(vector));

                    if (in->iobref == NULL) {
                        in->iobref = iobref_new();
                        if (in->iobref == NULL) {
                            ret = -1;
                            goto out;
                        }
                    }

                    vector[count].iov_base = iobuf_ptr(in->iobuf);
                    vector[count].iov_len = in->iobuf_size;

                    iobref = in->iobref;

                    count++;

                    if (in->payload_vector.iov_base != NULL) {
                        vector[count] = in->payload_vector;
                        count++;
                    }

                    *pollin = rpc_transport_pollin_alloc(this, vector, count,
                                                         in->iobuf, iobref,
                                                         in->request_info);
                    iobuf_unref(in->iobuf);
                    in->iobuf = NULL;

                    if (*pollin == NULL) {
                        gf_log(this->name, GF_LOG_WARNING,
                               "transport pollin allocation failed");
                        ret = -1;
                        goto out;
                    }
                    if (in->msg_type == REPLY)
                        (*pollin)->is_reply = 1;

                    in->request_info = NULL;
                }
                in->record_state = SP_STATE_COMPLETE;
                break;

            case SP_STATE_COMPLETE:
                /* control should not reach here */
                gf_log(this->name, GF_LOG_WARNING,
                       "control reached to "
                       "SP_STATE_COMPLETE, which should not have "
                       "happened");
                break;
        }
    }

    if (in->record_state == SP_STATE_COMPLETE) {
        in->record_state = SP_STATE_NADA;
        __socket_reset_priv(priv);
    }

out:
    return ret;
}

static int
socket_proto_state_machine(rpc_transport_t *this,
                           rpc_transport_pollin_t **pollin)
{
    return __socket_proto_state_machine(this, pollin);
}

static void
socket_event_poll_in_async(xlator_t *xl, gf_async_t *async)
{
    rpc_transport_pollin_t *pollin;
    rpc_transport_t *this;
    socket_private_t *priv;

    pollin = caa_container_of(async, rpc_transport_pollin_t, async);
    this = pollin->trans;
    priv = this->private;

    rpc_transport_notify(this, RPC_TRANSPORT_MSG_RECEIVED, pollin);

    rpc_transport_unref(this);

    rpc_transport_pollin_destroy(pollin);

    pthread_mutex_lock(&priv->notify.lock);
    {
        --priv->notify.in_progress;

        if (!priv->notify.in_progress)
            pthread_cond_signal(&priv->notify.cond);
    }
    pthread_mutex_unlock(&priv->notify.lock);
}

static int
socket_event_poll_in(rpc_transport_t *this, gf_boolean_t notify_handled)
{
    int ret = -1;
    rpc_transport_pollin_t *pollin = NULL;
    socket_private_t *priv = this->private;
    glusterfs_ctx_t *ctx = NULL;

    ctx = this->ctx;

    ret = socket_proto_state_machine(this, &pollin);

    if (pollin) {
        pthread_mutex_lock(&priv->notify.lock);
        {
            priv->notify.in_progress++;
        }
        pthread_mutex_unlock(&priv->notify.lock);
    }

    if (notify_handled && (ret >= 0))
        gf_event_handled(ctx->event_pool, priv->sock, priv->idx, priv->gen);

    if (pollin) {
        rpc_transport_ref(this);
        gf_async(&pollin->async, THIS, socket_event_poll_in_async);
    }

    return ret;
}

static int
socket_connect_finish(rpc_transport_t *this)
{
    int ret = -1;
    socket_private_t *priv = NULL;
    rpc_transport_event_t event = 0;
    char notify_rpc = 0;

    priv = this->private;

    pthread_mutex_lock(&priv->out_lock);
    {
        if (priv->connected != 0)
            goto unlock;

        get_transport_identifiers(this);

        ret = __socket_connect_finish(priv->sock);

        if ((ret < 0) && (errno == EINPROGRESS))
            ret = 1;

        if ((ret < 0) && (errno != EINPROGRESS)) {
            if (!priv->connect_finish_log) {
                gf_log(this->name, GF_LOG_ERROR,
                       "connection to %s failed (%s); "
                       "disconnecting socket",
                       this->peerinfo.identifier, strerror(errno));
                priv->connect_finish_log = 1;
            }
            __socket_disconnect(this);
            goto unlock;
        }

        if (ret == 0) {
            notify_rpc = 1;

            this->myinfo.sockaddr_len = sizeof(this->myinfo.sockaddr);

            ret = getsockname(priv->sock, SA(&this->myinfo.sockaddr),
                              &this->myinfo.sockaddr_len);
            if (ret != 0) {
                gf_log(this->name, GF_LOG_WARNING,
                       "getsockname on (%d) failed (%s) - "
                       "disconnecting socket",
                       priv->sock, strerror(errno));
                __socket_disconnect(this);
                event = RPC_TRANSPORT_DISCONNECT;
                goto unlock;
            }

            priv->connected = 1;
            priv->connect_finish_log = 0;
            event = RPC_TRANSPORT_CONNECT;
        }
    }
unlock:
    pthread_mutex_unlock(&priv->out_lock);

    if (notify_rpc) {
        rpc_transport_notify(this, event, this);
    }

    return ret;
}

static int
socket_disconnect(rpc_transport_t *this, gf_boolean_t wait);

/* socket_is_connected() is for use only in socket_event_handler() */
static inline gf_boolean_t
socket_is_connected(socket_private_t *priv)
{
    if (priv->use_ssl) {
        return priv->is_server ? priv->ssl_accepted : priv->ssl_connected;
    } else {
        return priv->is_server ? priv->accepted : priv->connected;
    }
}

static void
ssl_rearm_event_fd(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    glusterfs_ctx_t *ctx = NULL;
    int idx = -1;
    int gen = -1;
    int fd = -1;

    priv = this->private;
    ctx = this->ctx;

    idx = priv->idx;
    gen = priv->gen;
    fd = priv->sock;

    if (priv->ssl_error_required == SSL_ERROR_WANT_READ)
        gf_event_select_on(ctx->event_pool, fd, idx, 1, -1);
    if (priv->ssl_error_required == SSL_ERROR_WANT_WRITE)
        gf_event_select_on(ctx->event_pool, fd, idx, -1, 1);
    gf_event_handled(ctx->event_pool, fd, idx, gen);
}

static int
ssl_handle_server_connection_attempt(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    glusterfs_ctx_t *ctx = NULL;
    int idx = -1;
    int gen = -1;
    int ret = -1;
    int fd = -1;

    priv = this->private;
    ctx = this->ctx;

    idx = priv->idx;
    gen = priv->gen;
    fd = priv->sock;

    if (!priv->ssl_context_created) {
        ret = ssl_setup_connection_prefix(this, _gf_true);
        if (ret < 0) {
            gf_log(this->name, GF_LOG_TRACE,
                   "> ssl_setup_connection_prefix() failed!");
            ret = -1;
            goto out;
        } else {
            priv->ssl_context_created = _gf_true;
        }
    }
    ret = ssl_complete_connection(this);
    if (ret == 0) {
        /* nothing to do */
        gf_event_select_on(ctx->event_pool, fd, idx, 1, 0);
        gf_event_handled(ctx->event_pool, fd, idx, gen);
        ret = 1;
    } else {
        if (errno == EAGAIN) {
            ssl_rearm_event_fd(this);
            ret = 1;
        } else {
            ret = -1;
            gf_log(this->name, GF_LOG_TRACE,
                   "ssl_complete_connection returned error");
        }
    }
out:
    return ret;
}

static int
ssl_handle_client_connection_attempt(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    glusterfs_ctx_t *ctx = NULL;
    int idx = -1;
    int ret = -1;
    int fd = -1;

    priv = this->private;
    ctx = this->ctx;

    idx = priv->idx;
    fd = priv->sock;

    /* SSL client */
    if (priv->connect_failed) {
        gf_log(this->name, GF_LOG_TRACE, ">>> disconnecting SSL socket");
        (void)socket_disconnect(this, _gf_false);
        /* Force ret to be -1, as we are officially done with
           this socket */
        ret = -1;
    } else {
        if (!priv->ssl_context_created) {
            ret = ssl_setup_connection_prefix(this, _gf_false);
            if (ret < 0) {
                gf_log(this->name, GF_LOG_TRACE,
                       "> ssl_setup_connection_prefix() "
                       "failed!");
                ret = -1;
                goto out;
            } else {
                priv->ssl_context_created = _gf_true;
            }
        }
        ret = ssl_complete_connection(this);
        if (ret == 0) {
            ret = socket_connect_finish(this);
            gf_event_select_on(ctx->event_pool, fd, idx, 1, 0);
            gf_log(this->name, GF_LOG_TRACE, ">>> completed client connect");
        } else {
            if (errno == EAGAIN) {
                gf_log(this->name, GF_LOG_TRACE,
                       ">>> retrying client connect 2");
                ssl_rearm_event_fd(this);
                ret = 1;
            } else {
                /* this is a connection failure */
                (void)socket_connect_finish(this);
                gf_log(this->name, GF_LOG_TRACE,
                       "ssl_complete_connection "
                       "returned error");
                ret = -1;
            }
        }
    }
out:
    return ret;
}

static int
socket_handle_client_connection_attempt(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    glusterfs_ctx_t *ctx = NULL;
    int idx = -1;
    int gen = -1;
    int ret = -1;
    int fd = -1;

    priv = this->private;
    ctx = this->ctx;

    idx = priv->idx;
    gen = priv->gen;
    fd = priv->sock;

    /* non-SSL client */
    if (priv->connect_failed) {
        /* connect failed with some other error than
           EINPROGRESS or ENOENT, so nothing more to
           do, fail reading/writing anything even if
           poll_in or poll_out
           is set
           */
        gf_log("transport", GF_LOG_DEBUG,
               "connect failed with some other error "
               "than EINPROGRESS or ENOENT, so "
               "nothing more to do; disconnecting "
               "socket");
        (void)socket_disconnect(this, _gf_false);

        /* Force ret to be -1, as we are officially
         * done with this socket
         */
        ret = -1;
    } else {
        ret = socket_connect_finish(this);
        gf_log(this->name, GF_LOG_TRACE, "socket_connect_finish() returned %d",
               ret);
        if (ret == 0 || ret == 1) {
            /* we don't want to do any reads or
             * writes on the connection yet in
             * socket_event_handler, so just
             * return 1
             */
            ret = 1;
            gf_event_handled(ctx->event_pool, fd, idx, gen);
        }
    }
    return ret;
}

static int
socket_complete_connection(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    glusterfs_ctx_t *ctx = NULL;
    int idx = -1;
    int gen = -1;
    int ret = -1;
    int fd = -1;

    priv = this->private;
    ctx = this->ctx;

    idx = priv->idx;
    gen = priv->gen;
    fd = priv->sock;

    if (priv->use_ssl) {
        if (priv->is_server) {
            ret = ssl_handle_server_connection_attempt(this);
        } else {
            ret = ssl_handle_client_connection_attempt(this);
        }
    } else {
        if (priv->is_server) {
            /* non-SSL server: nothing much to do
             * connection has already been accepted in
             * socket_server_event_handler()
             */
            priv->accepted = _gf_true;
            gf_event_handled(ctx->event_pool, fd, idx, gen);
            ret = 1;
        } else {
            ret = socket_handle_client_connection_attempt(this);
        }
    }
    return ret;
}

/* reads rpc_requests during pollin */
static void
socket_event_handler(int fd, int idx, int gen, void *data, int poll_in,
                     int poll_out, int poll_err, char event_thread_died)
{
    rpc_transport_t *this = NULL;
    socket_private_t *priv = NULL;
    int ret = -1;
    glusterfs_ctx_t *ctx = NULL;
    gf_boolean_t socket_closed = _gf_false, notify_handled = _gf_false;

    this = data;

    if (event_thread_died) {
        /* to avoid duplicate notifications, notify only for listener sockets */
        return;
    }

    /* At this point we are sure no other thread is using the transport because
     * we cannot receive more events until we call gf_event_handled(). However
     * this function may call gf_event_handled() in some cases. When this is
     * done, the transport may be destroyed at any moment if another thread
     * handled an error event. To prevent that we take a reference here. */
    rpc_transport_ref(this);

    GF_VALIDATE_OR_GOTO("socket", this, out);
    GF_VALIDATE_OR_GOTO("socket", this->private, out);
    GF_VALIDATE_OR_GOTO("socket", this->xl, out);

    THIS = this->xl;
    priv = this->private;
    ctx = this->ctx;

    pthread_mutex_lock(&priv->out_lock);
    {
        priv->idx = idx;
        priv->gen = gen;
    }
    pthread_mutex_unlock(&priv->out_lock);

    gf_log(this->name, GF_LOG_TRACE, "%s (sock:%d) in:%d, out:%d, err:%d",
           (priv->is_server ? "server" : "client"), priv->sock, poll_in,
           poll_out, poll_err);

    if (!poll_err) {
        if (!socket_is_connected(priv)) {
            gf_log(this->name, GF_LOG_TRACE,
                   "%s (sock:%d) socket is not connected, "
                   "completing connection",
                   (priv->is_server ? "server" : "client"), priv->sock);

            ret = socket_complete_connection(this);

            gf_log(this->name, GF_LOG_TRACE,
                   "(sock:%d) "
                   "socket_complete_connection() returned %d",
                   priv->sock, ret);

            if (ret > 0) {
                gf_log(this->name, GF_LOG_TRACE,
                       "(sock:%d) returning to wait on socket", priv->sock);
                goto out;
            }
        } else {
            char *sock_type = (priv->is_server ? "Server" : "Client");

            gf_log(this->name, GF_LOG_TRACE,
                   "%s socket (%d) is already connected", sock_type,
                   priv->sock);
            ret = 0;
        }
    }

    if (!ret && poll_out) {
        ret = socket_event_poll_out(this);
        gf_log(this->name, GF_LOG_TRACE,
               "(sock:%d) "
               "socket_event_poll_out returned %d",
               priv->sock, ret);
    }

    if (!ret && poll_in) {
        ret = socket_event_poll_in(this, !poll_err);
        gf_log(this->name, GF_LOG_TRACE,
               "(sock:%d) "
               "socket_event_poll_in returned %d",
               priv->sock, ret);
        notify_handled = _gf_true;
    }

    if ((ret < 0) || poll_err) {
        struct sockaddr *sa = SA(&this->peerinfo.sockaddr);

        if (priv->is_server &&
            SA(&this->myinfo.sockaddr)->sa_family == AF_UNIX) {
            sa = SA(&this->myinfo.sockaddr);
        }

        socket_dump_info(sa, priv->is_server, priv->use_ssl, priv->sock,
                         this->name, "disconnecting from");

        /* Dump the SSL error stack to clear any errors that may otherwise
         * resurface in the future.
         */
        if (priv->use_ssl && priv->ssl_ssl) {
            ssl_dump_error_stack(this->name);
        }

        /* Logging has happened already in earlier cases */
        gf_log("transport", ((ret >= 0) ? GF_LOG_INFO : GF_LOG_DEBUG),
               "EPOLLERR - disconnecting (sock:%d) (%s)", priv->sock,
               (priv->use_ssl ? "SSL" : "non-SSL"));

        socket_closed = socket_event_poll_err(this, gen, idx);

        if (socket_closed)
            rpc_transport_unref(this);

    } else if (!notify_handled) {
        gf_event_handled(ctx->event_pool, fd, idx, gen);
    }

out:
    rpc_transport_unref(this);
}

static void
socket_server_event_handler(int fd, int idx, int gen, void *data, int poll_in,
                            int poll_out, int poll_err, char event_thread_died)
{
    rpc_transport_t *this = NULL;
    socket_private_t *priv = NULL;
    int ret = 0;
    int new_sock = -1;
    rpc_transport_t *new_trans = NULL;
    struct sockaddr_storage new_sockaddr = {
        0,
    };
    socklen_t addrlen = sizeof(new_sockaddr);
    socket_private_t *new_priv = NULL;
    glusterfs_ctx_t *ctx = NULL;

    this = data;
    GF_VALIDATE_OR_GOTO("socket", this, out);
    GF_VALIDATE_OR_GOTO("socket", this->private, out);
    GF_VALIDATE_OR_GOTO("socket", this->xl, out);
    GF_VALIDATE_OR_GOTO("socket", this->ctx, out);

    THIS = this->xl;
    priv = this->private;
    ctx = this->ctx;

    if (event_thread_died) {
        rpc_transport_notify(this, RPC_TRANSPORT_EVENT_THREAD_DIED,
                             (void *)(unsigned long)gen);
        return;
    }

    /* NOTE:
     * We have done away with the critical section in this function. since
     * there's little that it helps with. There's no other code that
     * attempts to unref the listener socket/transport from any other
     * thread context while we are using it here.
     */
    priv->idx = idx;
    priv->gen = gen;

    if (poll_err) {
        socket_event_poll_err(this, gen, idx);
        goto out;
    }

    if (poll_in) {
        int aflags = 0;

        if (!priv->bio)
            aflags = O_NONBLOCK;

        new_sock = sys_accept(priv->sock, SA(&new_sockaddr), &addrlen, aflags);

        gf_event_handled(ctx->event_pool, fd, idx, gen);

        if (new_sock < 0) {
            gf_log(this->name, GF_LOG_WARNING, "accept on %d failed (%s)",
                   priv->sock, strerror(errno));
            goto out;
        }

        if (new_sockaddr.ss_family != AF_UNIX) {
            if (priv->nodelay) {
                ret = __socket_nodelay(new_sock);
                if (ret != 0) {
                    gf_log(this->name, GF_LOG_WARNING,
                           "setsockopt() failed for "
                           "NODELAY (%s)",
                           strerror(errno));
                }
            }

            if (priv->keepalive) {
                ret = __socket_keepalive(
                    new_sock, new_sockaddr.ss_family, priv->keepaliveintvl,
                    priv->keepaliveidle, priv->keepalivecnt, priv->timeout);
                if (ret != 0)
                    gf_log(this->name, GF_LOG_WARNING,
                           "Failed to set keep-alive: %s", strerror(errno));
            }
        }

        new_trans = GF_CALLOC(1, sizeof(*new_trans), gf_common_mt_rpc_trans_t);
        if (!new_trans) {
            sys_close(new_sock);
            gf_log(this->name, GF_LOG_WARNING,
                   "transport object allocation failure; "
                   "closed newly accepted socket %d",
                   new_sock);
            goto out;
        }

        ret = pthread_mutex_init(&new_trans->lock, NULL);
        if (ret != 0) {
            gf_log(this->name, GF_LOG_WARNING,
                   "pthread_mutex_init() failed: %s; closing newly accepted "
                   "socket %d",
                   strerror(errno), new_sock);
            sys_close(new_sock);
            GF_FREE(new_trans);
            goto out;
        }
        INIT_LIST_HEAD(&new_trans->list);

        new_trans->name = gf_strdup(this->name);

        memcpy(&new_trans->peerinfo.sockaddr, &new_sockaddr, addrlen);
        new_trans->peerinfo.sockaddr_len = addrlen;

        new_trans->myinfo.sockaddr_len = sizeof(new_trans->myinfo.sockaddr);

        ret = getsockname(new_sock, SA(&new_trans->myinfo.sockaddr),
                          &new_trans->myinfo.sockaddr_len);
        if (ret != 0) {
            gf_log(this->name, GF_LOG_WARNING,
                   "getsockname on socket %d "
                   "failed (errno:%s); closing newly accepted socket",
                   new_sock, strerror(errno));
            sys_close(new_sock);
            GF_FREE(new_trans->name);
            GF_FREE(new_trans);
            goto out;
        }

        get_transport_identifiers(new_trans);
        gf_log(this->name, GF_LOG_TRACE, "XXX server:%s, client:%s",
               new_trans->myinfo.identifier, new_trans->peerinfo.identifier);

        /* Make options available to local socket_init() to create new
         * SSL_CTX per transport. A separate SSL_CTX per transport is
         * required to avoid setting crl checking options for client
         * connections. The verification options eventually get copied
         * to the SSL object. Unfortunately, there's no way to identify
         * whether socket_init() is being called after a client-side
         * connect() or a server-side accept(). Although, we could pass
         * a flag from the transport init() to the socket_init() and
         * from this place, this doesn't identify the case where the
         * server-side transport loading is done for the first time.
         * Also, SSL doesn't apply for UNIX sockets.
         */
        if (new_sockaddr.ss_family != AF_UNIX)
            new_trans->options = dict_ref(this->options);
        new_trans->ctx = this->ctx;

        ret = socket_init(new_trans);

        /* reset options to NULL to avoid double free */
        if (new_sockaddr.ss_family != AF_UNIX) {
            dict_unref(new_trans->options);
            new_trans->options = NULL;
        }

        if (ret != 0) {
            gf_log(this->name, GF_LOG_WARNING,
                   "initialization of new_trans "
                   "failed; closing newly accepted socket %d",
                   new_sock);
            sys_close(new_sock);
            GF_FREE(new_trans->name);
            GF_FREE(new_trans);
            goto out;
        }
        new_trans->ops = this->ops;
        new_trans->init = this->init;
        new_trans->fini = this->fini;
        new_trans->ctx = ctx;
        new_trans->xl = this->xl;
        new_trans->mydata = this->mydata;
        new_trans->notify = this->notify;
        new_trans->listener = this;
        new_trans->notify_poller_death = this->poller_death_accept;
        new_priv = new_trans->private;

        if (new_sockaddr.ss_family == AF_UNIX) {
            new_priv->use_ssl = _gf_false;
        } else {
            switch (priv->srvr_ssl) {
                case MGMT_SSL_ALWAYS:
                    /* Glusterd with secure_mgmt. */
                    new_priv->use_ssl = _gf_true;
                    break;
                case MGMT_SSL_COPY_IO:
                    /* Glusterfsd. */
                    new_priv->use_ssl = priv->ssl_enabled;
                    break;
                default:
                    new_priv->use_ssl = _gf_false;
            }
        }

        new_priv->sock = new_sock;

        new_priv->ssl_enabled = priv->ssl_enabled;
        new_priv->connected = 1;
        new_priv->is_server = _gf_true;

        /*
         * This is the first ref on the newly accepted
         * transport.
         */
        rpc_transport_ref(new_trans);

        {
            /* Take a ref on the new_trans to avoid
             * getting deleted when event_register()
             * causes socket_event_handler() to race
             * ahead of this path to eventually find
             * a disconnect and unref the transport
             */
            rpc_transport_ref(new_trans);

            /* Send a notification to RPCSVC layer
             * to save the new_trans in its service
             * list before we register the new_sock
             * with epoll to begin receiving notifications
             * for data handling.
             */
            ret = rpc_transport_notify(this, RPC_TRANSPORT_ACCEPT, new_trans);

            if (ret >= 0) {
                new_priv->idx = gf_event_register(
                    ctx->event_pool, new_sock, socket_event_handler, new_trans,
                    1, 0, new_trans->notify_poller_death);
                if (new_priv->idx == -1) {
                    ret = -1;
                    gf_log(this->name, GF_LOG_ERROR,
                           "failed to register the socket "
                           "with event");

                    /* event_register() could have failed for some
                     * reason, implying that the new_sock cannot be
                     * added to the epoll set. If we won't get any
                     * more notifications for new_sock from epoll,
                     * then we better remove the corresponding
                     * new_trans object from the RPCSVC service list.
                     * Since we've notified RPC service of new_trans
                     * before we attempted event_register(), we better
                     * unlink the new_trans from the RPCSVC service list
                     * to cleanup the stateby sending out a DISCONNECT
                     * notification.
                     */
                    rpc_transport_notify(this, RPC_TRANSPORT_DISCONNECT,
                                         new_trans);
                }
            }

            /* this rpc_transport_unref() is for managing race between
             * 1. socket_server_event_handler and
             * 2. socket_event_handler
             * trying to add and remove new_trans from the rpcsvc
             * service list
             * now that we are done with the notifications, lets
             * reduce the reference
             */
            rpc_transport_unref(new_trans);
        }

        if (ret < 0) {
            gf_log(this->name, GF_LOG_WARNING, "closing newly accepted socket");
            sys_close(new_sock);
            /* this unref is to actually cause the destruction of
             * the new_trans since we've failed at everything so far
             */
            rpc_transport_unref(new_trans);
        }
    }
out:
    return;
}

static int
socket_disconnect(rpc_transport_t *this, gf_boolean_t wait)
{
    socket_private_t *priv = NULL;
    int ret = -1;

    priv = this->private;

    pthread_mutex_lock(&priv->out_lock);
    {
        ret = __socket_disconnect(this);
    }
    pthread_mutex_unlock(&priv->out_lock);

    return ret;
}

void *
socket_connect_error_cbk(void *opaque)
{
    socket_connect_error_state_t *arg;

    GF_ASSERT(opaque);

    arg = opaque;
    THIS = arg->this;

    rpc_transport_notify(arg->trans, RPC_TRANSPORT_DISCONNECT, arg->trans);

    if (arg->refd)
        rpc_transport_unref(arg->trans);

    GF_FREE(opaque);
    return NULL;
}

static void
socket_fix_ssl_opts(rpc_transport_t *this, socket_private_t *priv,
                    uint16_t port)
{
    if (port == GF_DEFAULT_SOCKET_LISTEN_PORT) {
        gf_log(this->name, GF_LOG_DEBUG, "%s SSL for portmapper connection",
               priv->mgmt_ssl ? "enabling" : "disabling");
        priv->use_ssl = priv->mgmt_ssl;
    } else if (priv->ssl_enabled && !priv->use_ssl) {
        gf_log(this->name, GF_LOG_DEBUG, "re-enabling SSL for I/O connection");
        priv->use_ssl = _gf_true;
    }
}

/*
 * If we might just be trying to connect prematurely, e.g. to a brick that's
 * slow coming up, all we need is a simple retry.  Don't worry about sleeping
 * in some arbitrary thread.  The connect(2) could already have the exact same
 * effect, and we deal with it in that case so we can deal with it for sleep(2)
 * as well.
 */
static int
connect_loop(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    int ret;
    int connect_fails = 0;

    for (;;) {
        ret = connect(sockfd, addr, addrlen);
        if (ret >= 0) {
            break;
        }
        if ((errno != ENOENT) || (++connect_fails >= 5)) {
            break;
        }
        sleep(1);
    }

    return ret;
}

static int
socket_connect(rpc_transport_t *this, int port)
{
    int ret = -1;
    int th_ret = -1;
    int sock = -1;
    socket_private_t *priv = NULL;
    socklen_t sockaddr_len = 0;
    glusterfs_ctx_t *ctx = NULL;
    sa_family_t sa_family = {
        0,
    };
    char *local_addr = NULL;
    union gf_sock_union sock_union;
    struct sockaddr_in *addr = NULL;
    gf_boolean_t refd = _gf_false;
    socket_connect_error_state_t *arg = NULL;
    pthread_t th_id = {
        0,
    };
    gf_boolean_t ign_enoent = _gf_false;
    gf_boolean_t connect_attempted = _gf_false;

    GF_VALIDATE_OR_GOTO("socket", this, err);
    GF_VALIDATE_OR_GOTO("socket", this->private, err);

    priv = this->private;
    ctx = this->ctx;

    if (!priv) {
        gf_log_callingfn(this->name, GF_LOG_WARNING,
                         "connect() called on uninitialized transport");
        goto err;
    }

    pthread_mutex_lock(&priv->out_lock);
    {
        if (priv->sock >= 0) {
            gf_log_callingfn(this->name, GF_LOG_TRACE,
                             "connect () called on transport "
                             "already connected");
            errno = EINPROGRESS;
            ret = -1;
            goto unlock;
        }

        gf_log(this->name, GF_LOG_TRACE, "connecting %p, sock=%d", this,
               priv->sock);

        ret = socket_client_get_remote_sockaddr(this, &sock_union.sa,
                                                &sockaddr_len, &sa_family);
        if (ret < 0) {
            /* logged inside client_get_remote_sockaddr */
            goto unlock;
        }

        if (sa_family == AF_UNIX) {
            priv->ssl_enabled = _gf_false;
            priv->mgmt_ssl = _gf_false;
        } else {
            if (port > 0) {
                sock_union.sin.sin_port = htons(port);
            }
            socket_fix_ssl_opts(this, priv, ntohs(sock_union.sin.sin_port));
        }

        memcpy(&this->peerinfo.sockaddr, &sock_union.storage, sockaddr_len);
        this->peerinfo.sockaddr_len = sockaddr_len;

        priv->sock = sys_socket(sa_family, SOCK_STREAM, 0);
        if (priv->sock < 0) {
            gf_log(this->name, GF_LOG_ERROR, "socket creation failed (%s)",
                   strerror(errno));
            ret = -1;
            goto unlock;
        }

        /* Can't help if setting socket options fails. We can continue
         * working nonetheless.
         */
        if (priv->windowsize != 0) {
            if (setsockopt(priv->sock, SOL_SOCKET, SO_RCVBUF, &priv->windowsize,
                           sizeof(priv->windowsize)) != 0) {
                gf_log(this->name, GF_LOG_ERROR,
                       "setting receive window "
                       "size failed: %d: %d: %s",
                       priv->sock, priv->windowsize, strerror(errno));
            }

            if (setsockopt(priv->sock, SOL_SOCKET, SO_SNDBUF, &priv->windowsize,
                           sizeof(priv->windowsize)) != 0) {
                gf_log(this->name, GF_LOG_ERROR,
                       "setting send window size "
                       "failed: %d: %d: %s",
                       priv->sock, priv->windowsize, strerror(errno));
            }
        }

        /* Make sure we are not vulnerable to someone setting
         * net.ipv6.bindv6only to 1 so that gluster services are
         * available over IPv4 & IPv6.
         */
#ifdef IPV6_DEFAULT
        int disable_v6only = 0;
        if (setsockopt(priv->sock, IPPROTO_IPV6, IPV6_V6ONLY,
                       (void *)&disable_v6only, sizeof(disable_v6only)) < 0) {
            gf_log(this->name, GF_LOG_WARNING,
                   "Error disabling sockopt IPV6_V6ONLY: \"%s\"",
                   strerror(errno));
        }
#endif

        if (sa_family != AF_UNIX) {
            if (priv->nodelay) {
                ret = __socket_nodelay(priv->sock);
                if (ret != 0) {
                    gf_log(this->name, GF_LOG_ERROR,
                           "NODELAY on %d failed (%s)", priv->sock,
                           strerror(errno));
                }
            }

            if (priv->keepalive) {
                ret = __socket_keepalive(
                    priv->sock, sa_family, priv->keepaliveintvl,
                    priv->keepaliveidle, priv->keepalivecnt, priv->timeout);
                if (ret != 0)
                    gf_log(this->name, GF_LOG_ERROR,
                           "Failed to set keep-alive: %s", strerror(errno));
            }
        }

        SA(&this->myinfo.sockaddr)->sa_family = SA(&this->peerinfo.sockaddr)
                                                    ->sa_family;

        /* If a source addr is explicitly specified, use it */
        ret = dict_get_str_sizen(this->options, "transport.socket.source-addr",
                                 &local_addr);
        if (!ret && SA(&this->myinfo.sockaddr)->sa_family == AF_INET) {
            addr = (struct sockaddr_in *)(&this->myinfo.sockaddr);
            ret = inet_pton(AF_INET, local_addr, &(addr->sin_addr.s_addr));
        }

        /* If client wants ENOENT to be ignored */
        ign_enoent = dict_get_str_boolean(
            this->options, "transport.socket.ignore-enoent", _gf_false);

        ret = client_bind(this, SA(&this->myinfo.sockaddr),
                          &this->myinfo.sockaddr_len, priv->sock);
        if (ret < 0) {
            gf_log(this->name, GF_LOG_WARNING, "client bind failed: %s",
                   strerror(errno));
            goto handler;
        }

        /* make socket non-blocking for all types of sockets */
        if (!priv->bio) {
            ret = __socket_nonblock(priv->sock);
            if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "NBIO on %d failed (%s)",
                       priv->sock, strerror(errno));
                goto handler;
            } else {
                gf_log(this->name, GF_LOG_TRACE,
                       ">>> connect() with non-blocking IO for ALL");
            }
        }
        this->connect_failed = _gf_false;
        priv->connect_failed = 0;
        priv->connected = 0;

        socket_dump_info(SA(&this->peerinfo.sockaddr), priv->is_server,
                         priv->use_ssl, priv->sock, this->name,
                         "connecting to");

        if (ign_enoent) {
            ret = connect_loop(priv->sock, SA(&this->peerinfo.sockaddr),
                               this->peerinfo.sockaddr_len);
        } else {
            ret = connect(priv->sock, SA(&this->peerinfo.sockaddr),
                          this->peerinfo.sockaddr_len);
        }

        connect_attempted = _gf_true;

        if ((ret != 0) && (errno == ENOENT) && ign_enoent) {
            gf_log(this->name, GF_LOG_WARNING,
                   "Ignore failed connection attempt on %s, (%s) ",
                   this->peerinfo.identifier, strerror(errno));

            /* connect failed with some other error than EINPROGRESS
            so, getsockopt (... SO_ERROR ...), will not catch any
            errors and return them to us, we need to remember this
            state, and take actions in socket_event_handler
            appropriately */
            /* TBD: What about ENOENT, we will do getsockopt there
            as well, so how is that exempt from such a problem? */
            priv->connect_failed = 1;
            this->connect_failed = _gf_true;

            goto handler;
        }

        if ((ret != 0) && (errno != EINPROGRESS) && (errno != ENOENT)) {
            /* For unix path based sockets, the socket path is
             * cryptic (md5sum of path) and may not be useful for
             * the user in debugging so log it in DEBUG
             */
            gf_log(this->name,
                   ((sa_family == AF_UNIX) ? GF_LOG_DEBUG : GF_LOG_ERROR),
                   "connection attempt on %s failed, (%s)",
                   this->peerinfo.identifier, strerror(errno));

            /* connect failed with some other error than EINPROGRESS
            so, getsockopt (... SO_ERROR ...), will not catch any
            errors and return them to us, we need to remember this
            state, and take actions in socket_event_handler
            appropriately */
            /* TBD: What about ENOENT, we will do getsockopt there
            as well, so how is that exempt from such a problem? */
            priv->connect_failed = 1;

            goto handler;
        } else {
            /* reset connect_failed so that any previous attempts
            state is not carried forward */
            priv->connect_failed = 0;
            ret = 0;
        }

    handler:
        if (ret < 0 && !connect_attempted) {
            /* Ignore error from connect. epoll events
               should be handled in the socket handler.  shutdown(2)
               will result in EPOLLERR, so cleanup is done in
               socket_event_handler or socket_poller */
            shutdown(priv->sock, SHUT_RDWR);
            ret = 0;
            gf_log(this->name, GF_LOG_INFO,
                   "intentional client shutdown(%d, SHUT_RDWR)", priv->sock);
        }

        priv->connected = 0;
        priv->is_server = _gf_false;
        rpc_transport_ref(this);
        refd = _gf_true;

        this->listener = this;
        priv->idx = gf_event_register(ctx->event_pool, priv->sock,
                                      socket_event_handler, this, 1, 1,
                                      this->notify_poller_death);
        if (priv->idx == -1) {
            gf_log("", GF_LOG_WARNING,
                   "failed to register the event; "
                   "closing socket %d",
                   priv->sock);
            sys_close(priv->sock);
            priv->sock = -1;
            ret = -1;
        }

    unlock:
        sock = priv->sock;
    }
    pthread_mutex_unlock(&priv->out_lock);

err:
    /* if sock >= 0, then cleanup is done from the event handler */
    if ((ret < 0) && (sock < 0)) {
        /* Cleaup requires to send notification to upper layer which
           intern holds the big_lock. There can be dead-lock situation
           if big_lock is already held by the current thread.
           So transfer the ownership to separate thread for cleanup.
        */
        arg = GF_CALLOC(1, sizeof(*arg), gf_sock_connect_error_state_t);
        arg->this = THIS;
        arg->trans = this;
        arg->refd = refd;
        th_ret = gf_thread_create_detached(&th_id, socket_connect_error_cbk,
                                           arg, "scleanup");
        if (th_ret) {
            /* Error will be logged by gf_thread_create_attached */
            gf_log(this->name, GF_LOG_ERROR,
                   "Thread creation "
                   "failed");
            GF_FREE(arg);
            GF_ASSERT(0);
        }

        ret = 0;
    }

    return ret;
}

static int
socket_listen(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    int ret = -1;
    int sock = -1;
    struct sockaddr_storage sockaddr;
    socklen_t sockaddr_len = 0;
    peer_info_t *myinfo = NULL;
    glusterfs_ctx_t *ctx = NULL;
    sa_family_t sa_family = {
        0,
    };

    GF_VALIDATE_OR_GOTO("socket", this, out);
    GF_VALIDATE_OR_GOTO("socket", this->private, out);

    priv = this->private;
    myinfo = &this->myinfo;
    ctx = this->ctx;

    pthread_mutex_lock(&priv->out_lock);
    {
        sock = priv->sock;
    }
    pthread_mutex_unlock(&priv->out_lock);

    if (sock >= 0) {
        gf_log_callingfn(this->name, GF_LOG_DEBUG, "already listening");
        return ret;
    }

    ret = socket_server_get_local_sockaddr(this, SA(&sockaddr), &sockaddr_len,
                                           &sa_family);
    if (ret < 0) {
        return ret;
    }

    pthread_mutex_lock(&priv->out_lock);
    {
        if (priv->sock >= 0) {
            gf_log(this->name, GF_LOG_DEBUG, "already listening");
            goto unlock;
        }

        memcpy(&myinfo->sockaddr, &sockaddr, sockaddr_len);
        myinfo->sockaddr_len = sockaddr_len;

        priv->sock = sys_socket(sa_family, SOCK_STREAM, 0);

        if (priv->sock < 0) {
            gf_log(this->name, GF_LOG_ERROR, "socket creation failed (%s)",
                   strerror(errno));
            goto unlock;
        }

        /* Can't help if setting socket options fails. We can continue
         * working nonetheless.
         */
        if (priv->windowsize != 0) {
            if (setsockopt(priv->sock, SOL_SOCKET, SO_RCVBUF, &priv->windowsize,
                           sizeof(priv->windowsize)) != 0) {
                gf_log(this->name, GF_LOG_ERROR,
                       "setting receive window size "
                       "failed: %d: %d: %s",
                       priv->sock, priv->windowsize, strerror(errno));
            }

            if (setsockopt(priv->sock, SOL_SOCKET, SO_SNDBUF, &priv->windowsize,
                           sizeof(priv->windowsize)) != 0) {
                gf_log(this->name, GF_LOG_ERROR,
                       "setting send window size failed:"
                       " %d: %d: %s",
                       priv->sock, priv->windowsize, strerror(errno));
            }
        }

        if (priv->nodelay && (sa_family != AF_UNIX)) {
            ret = __socket_nodelay(priv->sock);
            if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR,
                       "setsockopt() failed for NODELAY (%s)", strerror(errno));
            }
        }

        if (!priv->bio) {
            ret = __socket_nonblock(priv->sock);

            if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR,
                       "NBIO on socket %d failed "
                       "(errno:%s); closing socket",
                       priv->sock, strerror(errno));
                sys_close(priv->sock);
                priv->sock = -1;
                goto unlock;
            }
        }

        /* coverity[SLEEP] */
        ret = __socket_server_bind(this);

        if (ret < 0) {
            /* logged inside __socket_server_bind() */
            gf_log(this->name, GF_LOG_ERROR,
                   "__socket_server_bind failed;"
                   "closing socket %d",
                   priv->sock);
            sys_close(priv->sock);
            priv->sock = -1;
            goto unlock;
        }

        socket_dump_info(SA(&this->myinfo.sockaddr), priv->is_server,
                         priv->use_ssl, priv->sock, this->name, "listening on");

        ret = listen(priv->sock, priv->backlog);

        if (ret != 0) {
            gf_log(this->name, GF_LOG_ERROR,
                   "could not set socket %d to listen mode (errno:%s); "
                   "closing socket",
                   priv->sock, strerror(errno));
            sys_close(priv->sock);
            priv->sock = -1;
            goto unlock;
        }

        rpc_transport_ref(this);

        priv->idx = gf_event_register(ctx->event_pool, priv->sock,
                                      socket_server_event_handler, this, 1, 0,
                                      this->notify_poller_death);

        if (priv->idx == -1) {
            gf_log(this->name, GF_LOG_WARNING,
                   "could not register socket %d with events; "
                   "closing socket",
                   priv->sock);
            ret = -1;
            sys_close(priv->sock);
            priv->sock = -1;
            goto unlock;
        }
    }
unlock:
    pthread_mutex_unlock(&priv->out_lock);

out:
    return ret;
}

static int32_t
socket_submit_outgoing_msg(rpc_transport_t *this, rpc_transport_msg_t *msg)
{
    int ret = -1;
    char need_poll_out = 0;
    char need_append = 1;
    struct ioq *entry = NULL;
    glusterfs_ctx_t *ctx = NULL;
    socket_private_t *priv = NULL;

    GF_VALIDATE_OR_GOTO("socket", this, out);
    GF_VALIDATE_OR_GOTO("socket", this->private, out);

    priv = this->private;
    ctx = this->ctx;

    pthread_mutex_lock(&priv->out_lock);
    {
        if (priv->connected != 1) {
            if (!priv->submit_log && !priv->connect_finish_log) {
                gf_log(this->name, GF_LOG_INFO,
                       "not connected (priv->connected = %d)", priv->connected);
                priv->submit_log = 1;
            }
            goto unlock;
        }

        priv->submit_log = 0;
        entry = __socket_ioq_new(this, msg);
        if (!entry)
            goto unlock;

        if (list_empty(&priv->ioq)) {
            ret = __socket_ioq_churn_entry(this, entry);

            if (ret == 0) {
                need_append = 0;
            }
            if (ret > 0) {
                need_poll_out = 1;
            }
        }

        if (need_append) {
            list_add_tail(&entry->list, &priv->ioq);
            ret = 0;
        }
        if (need_poll_out) {
            /* first entry to wait. continue writing on POLLOUT */
            priv->idx = gf_event_select_on(ctx->event_pool, priv->sock,
                                           priv->idx, -1, 1);
        }
    }
unlock:
    pthread_mutex_unlock(&priv->out_lock);

out:
    return ret;
}

static int32_t
socket_submit_request(rpc_transport_t *this, rpc_transport_req_t *req)
{
    return socket_submit_outgoing_msg(this, &req->msg);
}

static int32_t
socket_submit_reply(rpc_transport_t *this, rpc_transport_reply_t *reply)
{
    return socket_submit_outgoing_msg(this, &reply->msg);
}

static int32_t
socket_getpeername(rpc_transport_t *this, char *hostname, int hostlen)
{
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("socket", this, out);
    GF_VALIDATE_OR_GOTO("socket", hostname, out);

    if (hostlen < (strlen(this->peerinfo.identifier) + 1)) {
        goto out;
    }

    strcpy(hostname, this->peerinfo.identifier);
    ret = 0;
out:
    return ret;
}

static int32_t
socket_getpeeraddr(rpc_transport_t *this, char *peeraddr, int addrlen,
                   struct sockaddr_storage *sa, socklen_t salen)
{
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("socket", this, out);
    GF_VALIDATE_OR_GOTO("socket", sa, out);
    ret = 0;

    *sa = this->peerinfo.sockaddr;

    if (peeraddr != NULL) {
        ret = socket_getpeername(this, peeraddr, addrlen);
    }
out:
    return ret;
}

static int32_t
socket_getmyname(rpc_transport_t *this, char *hostname, int hostlen)
{
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("socket", this, out);
    GF_VALIDATE_OR_GOTO("socket", hostname, out);

    if (hostlen < (strlen(this->myinfo.identifier) + 1)) {
        goto out;
    }

    strcpy(hostname, this->myinfo.identifier);
    ret = 0;
out:
    return ret;
}

static int32_t
socket_getmyaddr(rpc_transport_t *this, char *myaddr, int addrlen,
                 struct sockaddr_storage *sa, socklen_t salen)
{
    int32_t ret = 0;

    GF_VALIDATE_OR_GOTO("socket", this, out);
    GF_VALIDATE_OR_GOTO("socket", sa, out);

    *sa = this->myinfo.sockaddr;

    if (myaddr != NULL) {
        ret = socket_getmyname(this, myaddr, addrlen);
    }

out:
    return ret;
}

static int
socket_throttle(rpc_transport_t *this, gf_boolean_t onoff)
{
    socket_private_t *priv = NULL;

    priv = this->private;

    /* The way we implement throttling is by taking off
       POLLIN event from the polled flags. This way we
       never get called with the POLLIN event and therefore
       will never read() any more data until throttling
       is turned off.
    */
    pthread_mutex_lock(&priv->out_lock);
    {
        /* Throttling is useless on a disconnected transport. In fact,
         * it's dangerous since priv->idx and priv->sock are set to -1
         * on a disconnected transport, which breaks epoll's event to
         * registered fd mapping. */

        if (priv->connected == 1)
            priv->idx = gf_event_select_on(this->ctx->event_pool, priv->sock,
                                           priv->idx, (int)!onoff, -1);
    }
    pthread_mutex_unlock(&priv->out_lock);
    return 0;
}

struct rpc_transport_ops tops = {
    .listen = socket_listen,
    .connect = socket_connect,
    .disconnect = socket_disconnect,
    .submit_request = socket_submit_request,
    .submit_reply = socket_submit_reply,
    .get_peername = socket_getpeername,
    .get_peeraddr = socket_getpeeraddr,
    .get_myname = socket_getmyname,
    .get_myaddr = socket_getmyaddr,
    .throttle = socket_throttle,
};

int
reconfigure(rpc_transport_t *this, dict_t *options)
{
    socket_private_t *priv = NULL;
    gf_boolean_t tmp_bool = _gf_false;
    char *optstr = NULL;
    int ret = -1;
    uint32_t backlog = 0;
    uint64_t windowsize = 0;
    data_t *data;

    GF_VALIDATE_OR_GOTO("socket", this, out);
    GF_VALIDATE_OR_GOTO("socket", this->private, out);

    priv = this->private;

    if (dict_get_str_sizen(options, "transport.socket.keepalive", &optstr) ==
        0) {
        if (gf_string2boolean(optstr, &tmp_bool) != 0) {
            gf_log(this->name, GF_LOG_ERROR,
                   "'transport.socket.keepalive' takes only "
                   "boolean options, not taking any action");
            priv->keepalive = 1;
            goto out;
        }
        gf_log(this->name, GF_LOG_DEBUG,
               "Reconfigured transport.socket.keepalive");

        priv->keepalive = tmp_bool;
    } else
        priv->keepalive = 1;

    if (dict_get_int32_sizen(options, "transport.tcp-user-timeout",
                             &(priv->timeout)) != 0)
        priv->timeout = GF_NETWORK_TIMEOUT;
    gf_log(this->name, GF_LOG_DEBUG,
           "Reconfigured transport.tcp-user-timeout=%d", priv->timeout);

    if (dict_get_uint32(options, "transport.listen-backlog", &backlog) == 0) {
        priv->backlog = backlog;
        gf_log(this->name, GF_LOG_DEBUG,
               "Reconfigured transport.listen-backlog=%d", priv->backlog);
    }

    if (priv->keepalive) {
        if (dict_get_int32_sizen(options, "transport.socket.keepalive-time",
                                 &(priv->keepaliveidle)) != 0)
            priv->keepaliveidle = GF_KEEPALIVE_TIME;
        gf_log(this->name, GF_LOG_DEBUG,
               "Reconfigured transport.socket.keepalive-time=%d",
               priv->keepaliveidle);

        if (dict_get_int32_sizen(options, "transport.socket.keepalive-interval",
                                 &(priv->keepaliveintvl)) != 0)
            priv->keepaliveintvl = GF_KEEPALIVE_INTERVAL;
        gf_log(this->name, GF_LOG_DEBUG,
               "Reconfigured transport.socket.keepalive-interval=%d",
               priv->keepaliveintvl);

        if (dict_get_int32_sizen(options, "transport.socket.keepalive-count",
                                 &(priv->keepalivecnt)) != 0)
            priv->keepalivecnt = GF_KEEPALIVE_COUNT;
        gf_log(this->name, GF_LOG_DEBUG,
               "Reconfigured transport.socket.keepalive-count=%d",
               priv->keepalivecnt);
    }

    optstr = NULL;
    if (dict_get_str_sizen(options, "tcp-window-size", &optstr) == 0) {
        if (gf_string2uint64(optstr, &windowsize) != 0) {
            gf_log(this->name, GF_LOG_ERROR, "invalid number format: %s",
                   optstr);
            goto out;
        }
    }

    priv->windowsize = (int)windowsize;

    data = dict_get_sizen(options, "non-blocking-io");
    if (data) {
        optstr = data_to_str(data);

        if (gf_string2boolean(optstr, &tmp_bool) != 0) {
            gf_log(this->name, GF_LOG_ERROR,
                   "'non-blocking-io' takes only boolean options,"
                   " not taking any action");
            tmp_bool = 1;
        }

        if (!tmp_bool) {
            priv->bio = 1;
            gf_log(this->name, GF_LOG_WARNING, "disabling non-blocking IO");
        }
    }

    if (!priv->bio) {
        ret = __socket_nonblock(priv->sock);
        if (ret != 0) {
            gf_log(this->name, GF_LOG_WARNING, "NBIO on %d failed (%s)",
                   priv->sock, strerror(errno));
            goto out;
        }
    }

    ret = 0;
out:
    return ret;
}

#if OPENSSL_VERSION_NUMBER < 0x1010000f
static pthread_mutex_t *lock_array = NULL;

static void
locking_func(int mode, int type, const char *file, int line)
{
    if (mode & CRYPTO_UNLOCK) {
        pthread_mutex_unlock(&lock_array[type]);
    } else {
        pthread_mutex_lock(&lock_array[type]);
    }
}

#if OPENSSL_VERSION_NUMBER >= 0x1000000f
static void
threadid_func(CRYPTO_THREADID *id)
{
    /*
     * We're not supposed to know whether a pthread_t is a number or a
     * pointer, but we definitely need an unsigned long.  Even though it
     * happens to be an unsigned long already on Linux, do the cast just in
     * case that's not so on another platform.  Note that this can still
     * break if any platforms are left where a pointer is larger than an
     * unsigned long.  In that case there's not much we can do; hopefully
     * anyone porting to such a platform will be aware enough to notice the
     * compile warnings about truncating the pointer value.
     */
    CRYPTO_THREADID_set_numeric(id, (unsigned long)pthread_self());
}
#else  /* older openssl */
static unsigned long
legacy_threadid_func(void)
{
    /* See comments above, it applies here too. */
    return (unsigned long)pthread_self();
}
#endif /* OPENSSL_VERSION_NUMBER >= 0x1000000f */
#endif /* OPENSSL_VERSION_NUMBER < 0x1010000f */

static void
init_openssl_mt(void)
{
    static gf_boolean_t initialized = _gf_false;

    if (initialized) {
        /* this only needs to be initialized once GLOBALLY no
           matter how many translators/sockets we end up with. */
        return;
    }

    SSL_library_init();
    SSL_load_error_strings();

    initialized = _gf_true;

#if OPENSSL_VERSION_NUMBER < 0x1010000f
    int num_locks = CRYPTO_num_locks();
    int i;

    lock_array = GF_CALLOC(num_locks, sizeof(pthread_mutex_t),
                           gf_sock_mt_lock_array);
    if (lock_array) {
        for (i = 0; i < num_locks; ++i) {
            pthread_mutex_init(&lock_array[i], NULL);
        }
#if OPENSSL_VERSION_NUMBER >= 0x1000000f
        CRYPTO_THREADID_set_callback(threadid_func);
#else /* older openssl */
        CRYPTO_set_id_callback(legacy_threadid_func);
#endif
        CRYPTO_set_locking_callback(locking_func);
    }
#endif
}

static void __attribute__((destructor)) fini_openssl_mt(void)
{
#if OPENSSL_VERSION_NUMBER < 0x1010000f
    int i;

    if (!lock_array) {
        return;
    }

    CRYPTO_set_locking_callback(NULL);
#if OPENSSL_VERSION_NUMBER >= 0x1000000f
    CRYPTO_THREADID_set_callback(NULL);
#else /* older openssl */
    CRYPTO_set_id_callback(NULL);
#endif

    for (i = 0; i < CRYPTO_num_locks(); ++i) {
        pthread_mutex_destroy(&lock_array[i]);
    }

    GF_FREE(lock_array);
    lock_array = NULL;
#endif

    ERR_free_strings();
}

/* The function returns 0 if AES bit is enabled on the CPU */
static int
ssl_check_aes_bit(void)
{
    FILE *fp = fopen("/proc/cpuinfo", "r");
    int ret = 1;
    size_t len = 0;
    char *line = NULL;
    char *match = NULL;

    GF_ASSERT(fp != NULL);

    while (getline(&line, &len, fp) > 0) {
        if (!strncmp(line, "flags", 5)) {
            match = strstr(line, " aes");
            if ((match != NULL) && ((match[4] == ' ') || (match[4] == 0))) {
                ret = 0;
                break;
            }
        }
    }

    free(line);
    fclose(fp);

    return ret;
}

static int
ssl_setup_connection_params(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    char *optstr = NULL;
    static int session_id = 1;
    int32_t cert_depth = DEFAULT_VERIFY_DEPTH;
    char *cipher_list = DEFAULT_CIPHER_LIST;
    char *dh_param = DEFAULT_DH_PARAM;
    char *ec_curve = DEFAULT_EC_CURVE;
    gf_boolean_t dh_flag = _gf_false;

    priv = this->private;

    if (priv->ssl_ctx != NULL) {
        gf_log(this->name, GF_LOG_TRACE, "found old SSL context!");
        return 0;
    }

    if (!priv->ssl_enabled && !priv->mgmt_ssl) {
        return 0;
    }

    if (!ssl_check_aes_bit()) {
        cipher_list = "AES128:" DEFAULT_CIPHER_LIST;
    }

    priv->ssl_own_cert = DEFAULT_CERT_PATH;
    if (dict_get_str_sizen(this->options, SSL_OWN_CERT_OPT, &optstr) == 0) {
        if (!priv->ssl_enabled) {
            gf_log(this->name, GF_LOG_WARNING,
                   "%s specified without %s (ignored)", SSL_OWN_CERT_OPT,
                   SSL_ENABLED_OPT);
        }
        priv->ssl_own_cert = optstr;
    }
    priv->ssl_own_cert = gf_strdup(priv->ssl_own_cert);

    priv->ssl_private_key = DEFAULT_KEY_PATH;
    if (dict_get_str_sizen(this->options, SSL_PRIVATE_KEY_OPT, &optstr) == 0) {
        if (!priv->ssl_enabled) {
            gf_log(this->name, GF_LOG_WARNING,
                   "%s specified without %s (ignored)", SSL_PRIVATE_KEY_OPT,
                   SSL_ENABLED_OPT);
        }
        priv->ssl_private_key = optstr;
    }
    priv->ssl_private_key = gf_strdup(priv->ssl_private_key);

    priv->ssl_ca_list = DEFAULT_CA_PATH;
    if (dict_get_str_sizen(this->options, SSL_CA_LIST_OPT, &optstr) == 0) {
        if (!priv->ssl_enabled) {
            gf_log(this->name, GF_LOG_WARNING,
                   "%s specified without %s (ignored)", SSL_CA_LIST_OPT,
                   SSL_ENABLED_OPT);
        }
        priv->ssl_ca_list = optstr;
    }
    priv->ssl_ca_list = gf_strdup(priv->ssl_ca_list);

    optstr = NULL;
    if (dict_get_str_sizen(this->options, SSL_CRL_PATH_OPT, &optstr) == 0) {
        if (!priv->ssl_enabled) {
            gf_log(this->name, GF_LOG_WARNING,
                   "%s specified without %s (ignored)", SSL_CRL_PATH_OPT,
                   SSL_ENABLED_OPT);
        }
        if (strcasecmp(optstr, "NULL") == 0)
            priv->crl_path = NULL;
        else
            priv->crl_path = gf_strdup(optstr);
    }

    if (!priv->mgmt_ssl) {
        if (!dict_get_int32_sizen(this->options, SSL_CERT_DEPTH_OPT,
                                  &cert_depth)) {
        }
    } else {
        cert_depth = this->ctx->ssl_cert_depth;
    }
    gf_log(this->name, priv->ssl_enabled ? GF_LOG_INFO : GF_LOG_DEBUG,
           "SSL support for MGMT is %s IO path is %s certificate depth is %d "
           "for peer %s",
           (priv->mgmt_ssl ? "ENABLED" : "NOT enabled"),
           (priv->ssl_enabled ? "ENABLED" : "NOT enabled"), cert_depth,
           this->peerinfo.identifier);

    if (!dict_get_str_sizen(this->options, SSL_CIPHER_LIST_OPT, &cipher_list)) {
        gf_log(this->name, GF_LOG_INFO, "using cipher list %s", cipher_list);
    }
    if (!dict_get_str_sizen(this->options, SSL_DH_PARAM_OPT, &dh_param)) {
        dh_flag = _gf_true;
        gf_log(this->name, GF_LOG_INFO, "using DH parameters %s", dh_param);
    }
    if (!dict_get_str_sizen(this->options, SSL_EC_CURVE_OPT, &ec_curve)) {
        gf_log(this->name, GF_LOG_INFO, "using EC curve %s", ec_curve);
    }

    if (priv->ssl_enabled || priv->mgmt_ssl) {
        BIO *bio = NULL;

#if HAVE_TLS_METHOD
        priv->ssl_meth = (SSL_METHOD *)TLS_method();
#elif HAVE_TLSV1_2_METHOD
        priv->ssl_meth = (SSL_METHOD *)TLSv1_2_method();
#else
/*
 * Nobody should use an OpenSSL so old it does not support TLS 1.2.
 * If that is really required, build with -DUSE_INSECURE_OPENSSL
 */
#ifndef USE_INSECURE_OPENSSL
#error Old and insecure OpenSSL, use -DUSE_INSECURE_OPENSSL to use it anyway
#endif
        /* SSLv23_method uses highest available protocol */
        priv->ssl_meth = SSLv23_method();
#endif
        priv->ssl_ctx = SSL_CTX_new(priv->ssl_meth);

        SSL_CTX_set_options(priv->ssl_ctx, SSL_OP_NO_SSLv2);
        SSL_CTX_set_options(priv->ssl_ctx, SSL_OP_NO_SSLv3);
#ifdef SSL_OP_NO_TICKET
        SSL_CTX_set_options(priv->ssl_ctx, SSL_OP_NO_TICKET);
#endif
#ifdef SSL_OP_NO_COMPRESSION
        SSL_CTX_set_options(priv->ssl_ctx, SSL_OP_NO_COMPRESSION);
#endif
        /* Upload file to bio wrapper only if dh param is configured
         */
        if (dh_flag) {
            if ((bio = BIO_new_file(dh_param, "r")) == NULL) {
                gf_log(this->name, GF_LOG_ERROR,
                       "failed to open %s, "
                       "DH ciphers are disabled",
                       dh_param);
            }
        }

        if (bio != NULL) {
#ifdef HAVE_OPENSSL_DH_H
            DH *dh;
            unsigned long err;

            dh = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
            BIO_free(bio);
            if (dh != NULL) {
                SSL_CTX_set_options(priv->ssl_ctx, SSL_OP_SINGLE_DH_USE);
                SSL_CTX_set_tmp_dh(priv->ssl_ctx, dh);
                DH_free(dh);
            } else {
                err = ERR_get_error();
                gf_log(this->name, GF_LOG_ERROR,
                       "failed to read DH param from %s: %s "
                       "DH ciphers are disabled.",
                       dh_param, ERR_error_string(err, NULL));
            }
#else  /* HAVE_OPENSSL_DH_H */
            BIO_free(bio);
            gf_log(this->name, GF_LOG_ERROR, "OpenSSL has no DH support");
#endif /* HAVE_OPENSSL_DH_H */
        }

        if (ec_curve != NULL) {
#ifdef HAVE_OPENSSL_ECDH_H
            EC_KEY *ecdh = NULL;
            int nid;
            unsigned long err;

            nid = OBJ_sn2nid(ec_curve);
            if (nid != 0)
                ecdh = EC_KEY_new_by_curve_name(nid);

            if (ecdh != NULL) {
                SSL_CTX_set_options(priv->ssl_ctx, SSL_OP_SINGLE_ECDH_USE);
                SSL_CTX_set_tmp_ecdh(priv->ssl_ctx, ecdh);
                EC_KEY_free(ecdh);
            } else {
                err = ERR_get_error();
                gf_log(this->name, GF_LOG_ERROR,
                       "failed to load EC curve %s: %s. "
                       "ECDH ciphers are disabled.",
                       ec_curve, ERR_error_string(err, NULL));
            }
#else  /* HAVE_OPENSSL_ECDH_H */
            gf_log(this->name, GF_LOG_ERROR, "OpenSSL has no ECDH support");
#endif /* HAVE_OPENSSL_ECDH_H */
        }

        /* This must be done after DH and ECDH setups */
        if (SSL_CTX_set_cipher_list(priv->ssl_ctx, cipher_list) == 0) {
            gf_log(this->name, GF_LOG_ERROR,
                   "failed to find any valid ciphers");
            goto err;
        }

        SSL_CTX_set_options(priv->ssl_ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);

        if (!SSL_CTX_use_certificate_chain_file(priv->ssl_ctx,
                                                priv->ssl_own_cert)) {
            gf_log(this->name, GF_LOG_ERROR, "could not load our cert at %s",
                   priv->ssl_own_cert);
            ssl_dump_error_stack(this->name);
            goto err;
        }

        if (!SSL_CTX_use_PrivateKey_file(priv->ssl_ctx, priv->ssl_private_key,
                                         SSL_FILETYPE_PEM)) {
            gf_log(this->name, GF_LOG_ERROR, "could not load private key at %s",
                   priv->ssl_private_key);
            ssl_dump_error_stack(this->name);
            goto err;
        }

        if (!SSL_CTX_load_verify_locations(priv->ssl_ctx, priv->ssl_ca_list,
                                           priv->crl_path)) {
            gf_log(this->name, GF_LOG_ERROR, "could not load CA list");
            goto err;
        }

        SSL_CTX_set_verify_depth(priv->ssl_ctx, cert_depth);

        if (priv->crl_path)
            ssl_set_crl_verify_flags(priv->ssl_ctx);

        priv->ssl_session_id = session_id++;
        SSL_CTX_set_session_id_context(priv->ssl_ctx,
                                       (void *)&priv->ssl_session_id,
                                       sizeof(priv->ssl_session_id));

        SSL_CTX_set_verify(priv->ssl_ctx, SSL_VERIFY_PEER, 0);

        /*
         * Since glusterfs shares the same settings for client-side
         * and server-side of SSL, we need to ignore any certificate
         * usage specification (SSL client vs SSL server), otherwise
         * SSL connexions will fail with 'unsupported cerritifcate"
         */
        SSL_CTX_set_purpose(priv->ssl_ctx, X509_PURPOSE_ANY);
    }
    return 0;

err:
    return -1;
}

static int
socket_init(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;
    gf_boolean_t tmp_bool = 0;
    uint64_t windowsize = GF_DEFAULT_SOCKET_WINDOW_SIZE;
    char *optstr = NULL;
    data_t *data;

    if (this->private) {
        gf_log_callingfn(this->name, GF_LOG_ERROR, "double init attempted");
        return -1;
    }

    priv = GF_CALLOC(1, sizeof(*priv), gf_common_mt_socket_private_t);
    if (!priv) {
        return -1;
    }

    this->private = priv;
    pthread_mutex_init(&priv->out_lock, NULL);
    pthread_mutex_init(&priv->cond_lock, NULL);
    pthread_cond_init(&priv->cond, NULL);

    /*GF_REF_INIT (priv, socket_poller_mayday);*/

    priv->sock = -1;
    priv->idx = -1;
    priv->connected = -1;
    priv->nodelay = 1;
    priv->bio = 0;
    priv->ssl_accepted = _gf_false;
    priv->ssl_connected = _gf_false;
    priv->windowsize = GF_DEFAULT_SOCKET_WINDOW_SIZE;
    INIT_LIST_HEAD(&priv->ioq);
    pthread_mutex_init(&priv->notify.lock, NULL);
    pthread_cond_init(&priv->notify.cond, NULL);

    /* All the below section needs 'this->options' to be present */
    if (!this->options)
        goto out;

    data = dict_get_sizen(this->options, "non-blocking-io");
    if (data) {
        optstr = data_to_str(data);

        if (gf_string2boolean(optstr, &tmp_bool) != 0) {
            gf_log(this->name, GF_LOG_ERROR,
                   "'non-blocking-io' takes only boolean options,"
                   " not taking any action");
            tmp_bool = 1;
        }

        if (!tmp_bool) {
            priv->bio = 1;
            gf_log(this->name, GF_LOG_WARNING, "disabling non-blocking IO");
        }
    }

    optstr = NULL;

    /* By default, we enable NODELAY */
    data = dict_get_sizen(this->options, "transport.socket.nodelay");
    if (data) {
        optstr = data_to_str(data);

        if (gf_string2boolean(optstr, &tmp_bool) != 0) {
            gf_log(this->name, GF_LOG_ERROR,
                   "'transport.socket.nodelay' takes only "
                   "boolean options, not taking any action");
            tmp_bool = 1;
        }
        if (!tmp_bool) {
            priv->nodelay = 0;
            gf_log(this->name, GF_LOG_DEBUG, "disabling nodelay");
        }
    }

    optstr = NULL;
    if (dict_get_str_sizen(this->options, "tcp-window-size", &optstr) == 0) {
        if (gf_string2uint64(optstr, &windowsize) != 0) {
            gf_log(this->name, GF_LOG_ERROR, "invalid number format: %s",
                   optstr);
            return -1;
        }
    }

    priv->windowsize = (int)windowsize;

    optstr = NULL;
    /* Enable Keep-alive by default. */
    priv->keepalive = 1;
    priv->keepaliveintvl = GF_KEEPALIVE_INTERVAL;
    priv->keepaliveidle = GF_KEEPALIVE_TIME;
    priv->keepalivecnt = GF_KEEPALIVE_COUNT;
    if (dict_get_str_sizen(this->options, "transport.socket.keepalive",
                           &optstr) == 0) {
        if (gf_string2boolean(optstr, &tmp_bool) != 0) {
            gf_log(this->name, GF_LOG_ERROR,
                   "'transport.socket.keepalive' takes only "
                   "boolean options, not taking any action");
            tmp_bool = 1;
        }

        if (!tmp_bool)
            priv->keepalive = 0;
    }

    if (dict_get_int32_sizen(this->options, "transport.tcp-user-timeout",
                             &(priv->timeout)) != 0)
        priv->timeout = GF_NETWORK_TIMEOUT;
    gf_log(this->name, GF_LOG_DEBUG, "Configured transport.tcp-user-timeout=%d",
           priv->timeout);

    if (priv->keepalive) {
        if (dict_get_int32_sizen(this->options,
                                 "transport.socket.keepalive-time",
                                 &(priv->keepaliveidle)) != 0) {
            priv->keepaliveidle = GF_KEEPALIVE_TIME;
        }

        if (dict_get_int32_sizen(this->options,
                                 "transport.socket.keepalive-interval",
                                 &(priv->keepaliveintvl)) != 0) {
            priv->keepaliveintvl = GF_KEEPALIVE_INTERVAL;
        }

        if (dict_get_int32_sizen(this->options,
                                 "transport.socket.keepalive-count",
                                 &(priv->keepalivecnt)) != 0)
            priv->keepalivecnt = GF_KEEPALIVE_COUNT;
        gf_log(this->name, GF_LOG_DEBUG,
               "Reconfigured transport.keepalivecnt=%d", priv->keepalivecnt);
    }

    if (dict_get_uint32(this->options, "transport.listen-backlog",
                        &(priv->backlog)) != 0) {
        priv->backlog = GLUSTERFS_SOCKET_LISTEN_BACKLOG;
    }

    optstr = NULL;

    /* Check if socket read failures are to be logged */
    priv->read_fail_log = 1;
    data = dict_get_sizen(this->options, "transport.socket.read-fail-log");
    if (data) {
        optstr = data_to_str(data);
        if (gf_string2boolean(optstr, &tmp_bool) != 0) {
            gf_log(this->name, GF_LOG_WARNING,
                   "'transport.socket.read-fail-log' takes only "
                   "boolean options; logging socket read fails");
        } else if (tmp_bool == _gf_false) {
            priv->read_fail_log = 0;
        }
    }

    priv->windowsize = (int)windowsize;

    priv->ssl_enabled = _gf_false;
    if (dict_get_str_sizen(this->options, SSL_ENABLED_OPT, &optstr) == 0) {
        if (gf_string2boolean(optstr, &priv->ssl_enabled) != 0) {
            gf_log(this->name, GF_LOG_ERROR,
                   "invalid value given for ssl-enabled boolean");
        }
    }
    priv->mgmt_ssl = this->ctx->secure_mgmt;
    priv->srvr_ssl = this->ctx->secure_srvr;

    ssl_setup_connection_params(this);
out:
    this->private = priv;
    return 0;
}

void
fini(rpc_transport_t *this)
{
    socket_private_t *priv = NULL;

    if (!this)
        return;

    priv = this->private;
    if (priv) {
        if (priv->sock >= 0) {
            pthread_mutex_lock(&priv->out_lock);
            {
                __socket_ioq_flush(priv);
                __socket_reset(this);
            }
            pthread_mutex_unlock(&priv->out_lock);
        }
        gf_log(this->name, GF_LOG_TRACE, "transport %p destroyed", this);

        pthread_mutex_destroy(&priv->out_lock);
        pthread_mutex_destroy(&priv->cond_lock);
        pthread_cond_destroy(&priv->cond);

        GF_ASSERT(priv->notify.in_progress == 0);
        pthread_mutex_destroy(&priv->notify.lock);
        pthread_cond_destroy(&priv->notify.cond);

        if (priv->use_ssl && priv->ssl_ssl) {
            SSL_clear(priv->ssl_ssl);
            SSL_free(priv->ssl_ssl);
            priv->ssl_ssl = NULL;
        }
        if (priv->ssl_ctx) {
            SSL_CTX_free(priv->ssl_ctx);
            priv->ssl_ctx = NULL;
        }

        if (priv->ssl_private_key) {
            GF_FREE(priv->ssl_private_key);
        }
        if (priv->ssl_own_cert) {
            GF_FREE(priv->ssl_own_cert);
        }
        if (priv->ssl_ca_list) {
            GF_FREE(priv->ssl_ca_list);
        }
        GF_FREE(priv);
    }

    this->private = NULL;
}

int32_t
init(rpc_transport_t *this)
{
    int ret = -1;

    init_openssl_mt();

    ret = socket_init(this);

    if (ret < 0) {
        gf_log(this->name, GF_LOG_DEBUG, "socket_init() failed");
    }

    return ret;
}

struct volume_options options[] = {
    {.key = {"remote-port", "transport.remote-port",
             "transport.socket.remote-port"},
     .type = GF_OPTION_TYPE_INT},
    {.key = {"transport.socket.listen-port", "listen-port"},
     .type = GF_OPTION_TYPE_INT},
    {.key = {"transport.socket.bind-address", "bind-address"},
     .type = GF_OPTION_TYPE_INTERNET_ADDRESS},
    {.key = {"transport.socket.connect-path", "connect-path"},
     .type = GF_OPTION_TYPE_ANY},
    {.key = {"transport.socket.bind-path", "bind-path"},
     .type = GF_OPTION_TYPE_ANY},
    {.key = {"transport.socket.listen-path", "listen-path"},
     .type = GF_OPTION_TYPE_ANY},
    {.key = {"transport.address-family", "address-family"},
     .value = {"inet", "inet6", "unix", "inet-sdp"},
     .op_version = {GD_OP_VERSION_3_7_4},
     .type = GF_OPTION_TYPE_STR},
    {.key = {"non-blocking-io"}, .type = GF_OPTION_TYPE_BOOL},
    {.key = {"tcp-window-size"},
     .type = GF_OPTION_TYPE_SIZET,
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE,
     .description = "Option to set TCP SEND/RECV BUFFER SIZE",
     .min = GF_MIN_SOCKET_WINDOW_SIZE,
     .max = GF_MAX_SOCKET_WINDOW_SIZE},
    {
        .key = {"transport.listen-backlog"},
        .type = GF_OPTION_TYPE_SIZET,
        .op_version = {GD_OP_VERSION_3_11_1},
        .flags = OPT_FLAG_SETTABLE,
        .description = "This option uses the value of backlog argument that "
                       "defines the maximum length to which the queue of "
                       "pending connections for socket fd may grow.",
        .default_value = "1024",
    },
    {.key = {"transport.tcp-user-timeout"},
     .type = GF_OPTION_TYPE_INT,
     .op_version = {GD_OP_VERSION_3_10_2},
     .default_value = TOSTRING(GF_NETWORK_TIMEOUT)},
    {.key = {"transport.socket.nodelay"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "1"},
    {.key = {"transport.socket.keepalive"},
     .type = GF_OPTION_TYPE_BOOL,
     .op_version = {1},
     .default_value = "1"},
    {.key = {"transport.socket.keepalive-interval"},
     .type = GF_OPTION_TYPE_INT,
     .op_version = {GD_OP_VERSION_3_10_2},
     .default_value = "2"},
    {.key = {"transport.socket.keepalive-time"},
     .type = GF_OPTION_TYPE_INT,
     .op_version = {GD_OP_VERSION_3_10_2},
     .default_value = "20"},
    {.key = {"transport.socket.keepalive-count"},
     .type = GF_OPTION_TYPE_INT,
     .op_version = {GD_OP_VERSION_3_10_2},
     .default_value = "9"},
    {.key = {"transport.socket.read-fail-log"}, .type = GF_OPTION_TYPE_BOOL},
    {.key = {SSL_ENABLED_OPT}, .type = GF_OPTION_TYPE_BOOL},
    {.key = {SSL_OWN_CERT_OPT}, .type = GF_OPTION_TYPE_STR},
    {.key = {SSL_PRIVATE_KEY_OPT}, .type = GF_OPTION_TYPE_STR},
    {.key = {SSL_CA_LIST_OPT}, .type = GF_OPTION_TYPE_STR},
    {.key = {SSL_CERT_DEPTH_OPT}, .type = GF_OPTION_TYPE_STR},
    {.key = {SSL_CIPHER_LIST_OPT}, .type = GF_OPTION_TYPE_STR},
    {.key = {SSL_DH_PARAM_OPT}, .type = GF_OPTION_TYPE_STR},
    {.key = {SSL_EC_CURVE_OPT}, .type = GF_OPTION_TYPE_STR},
    {.key = {SSL_CRL_PATH_OPT}, .type = GF_OPTION_TYPE_STR},
    {.key = {OWN_THREAD_OPT}, .type = GF_OPTION_TYPE_BOOL},
    {.key = {"ssl-own-cert"},
     .op_version = {GD_OP_VERSION_3_7_4},
     .flags = OPT_FLAG_SETTABLE,
     .type = GF_OPTION_TYPE_STR,
     .description = "SSL certificate. Ignored if SSL is not enabled."},
    {.key = {"ssl-private-key"},
     .op_version = {GD_OP_VERSION_3_7_4},
     .flags = OPT_FLAG_SETTABLE,
     .type = GF_OPTION_TYPE_STR,
     .description = "SSL private key. Ignored if SSL is not enabled."},
    {.key = {"ssl-ca-list"},
     .op_version = {GD_OP_VERSION_3_7_4},
     .flags = OPT_FLAG_SETTABLE,
     .type = GF_OPTION_TYPE_STR,
     .description = "SSL CA list. Ignored if SSL is not enabled."},
    {.key = {"ssl-cert-depth"},
     .type = GF_OPTION_TYPE_INT,
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE,
     .description = "Maximum certificate-chain depth.  If zero, the "
                    "peer's certificate itself must be in the local "
                    "certificate list.  Otherwise, there may be up to N "
                    "signing certificates between the peer's and the "
                    "local list.  Ignored if SSL is not enabled."},
    {.key = {"ssl-cipher-list"},
     .type = GF_OPTION_TYPE_STR,
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE,
     .description = "Allowed SSL ciphers. Ignored if SSL is not enabled."},
    {.key = {"ssl-dh-param"},
     .type = GF_OPTION_TYPE_STR,
     .op_version = {GD_OP_VERSION_3_7_4},
     .flags = OPT_FLAG_SETTABLE,
     .description = "DH parameters file. Ignored if SSL is not enabled."},
    {.key = {"ssl-ec-curve"},
     .type = GF_OPTION_TYPE_STR,
     .op_version = {GD_OP_VERSION_3_7_4},
     .flags = OPT_FLAG_SETTABLE,
     .description = "ECDH curve name. Ignored if SSL is not enabled."},
    {.key = {"ssl-crl-path"},
     .type = GF_OPTION_TYPE_STR,
     .op_version = {GD_OP_VERSION_3_7_4},
     .flags = OPT_FLAG_SETTABLE,
     .description = "Path to directory containing CRL. "
                    "Ignored if SSL is not enabled."},
    {.key = {NULL}}};
