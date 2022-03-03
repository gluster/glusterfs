/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <dlfcn.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/poll.h>
#include <stdint.h>

#include "rpc-transport.h"

#ifndef GF_OPTION_LIST_EMPTY
#define GF_OPTION_LIST_EMPTY(_opt) (_opt->value[0] == NULL)
#endif

int32_t
rpc_transport_count(const char *transport_type)
{
    char *transport_dup = NULL;
    char *saveptr = NULL;
    char *ptr = NULL;
    int count = 0;

    if (transport_type == NULL)
        return -1;

    transport_dup = gf_strdup(transport_type);
    if (transport_dup == NULL) {
        return -1;
    }

    ptr = strtok_r(transport_dup, ",", &saveptr);
    while (ptr != NULL) {
        count++;
        ptr = strtok_r(NULL, ",", &saveptr);
    }

    GF_FREE(transport_dup);
    return count;
}

int
rpc_transport_get_myaddr(rpc_transport_t *this, char *peeraddr, int addrlen,
                         struct sockaddr_storage *sa, size_t salen)
{
    int32_t ret = -1;
    GF_VALIDATE_OR_GOTO("rpc", this, out);

    ret = this->ops->get_myaddr(this, peeraddr, addrlen, sa, salen);

out:
    return ret;
}

int32_t
rpc_transport_get_peername(rpc_transport_t *this, char *hostname, int hostlen)
{
    int32_t ret = -1;
    GF_VALIDATE_OR_GOTO("rpc", this, out);

    ret = this->ops->get_peername(this, hostname, hostlen);
out:
    return ret;
}

int
rpc_transport_throttle(rpc_transport_t *this, gf_boolean_t onoff)
{
    if (!this->ops->throttle)
        return -ENOSYS;

    return this->ops->throttle(this, onoff);
}

int32_t
rpc_transport_get_peeraddr(rpc_transport_t *this, char *peeraddr, int addrlen,
                           struct sockaddr_storage *sa, size_t salen)
{
    int32_t ret = -1;
    GF_VALIDATE_OR_GOTO("rpc", this, out);

    ret = this->ops->get_peeraddr(this, peeraddr, addrlen, sa, salen);
out:
    return ret;
}

void
rpc_transport_pollin_destroy(rpc_transport_pollin_t *pollin)
{
    GF_VALIDATE_OR_GOTO("rpc", pollin, out);

    if (pollin->iobref) {
        iobref_unref(pollin->iobref);
    }

    if (pollin->private) {
        /* */
        GF_FREE(pollin->private);
    }

    GF_FREE(pollin);
out:
    return;
}

rpc_transport_pollin_t *
rpc_transport_pollin_alloc(rpc_transport_t *this, struct iovec *vector,
                           int count, struct iobuf *hdr_iobuf,
                           struct iobref *iobref, void *private)
{
    rpc_transport_pollin_t *msg = NULL;
    msg = GF_CALLOC(1, sizeof(*msg), gf_common_mt_rpc_trans_pollin_t);
    if (!msg) {
        goto out;
    }

    msg->trans = this;

    if (count > 1) {
        msg->vectored = 1;
    }

    memcpy(msg->vector, vector, count * sizeof(*vector));
    msg->count = count;
    msg->iobref = iobref_ref(iobref);
    msg->private = private;
    if (hdr_iobuf)
        iobref_add(iobref, hdr_iobuf);

out:
    return msg;
}

void
rpc_transport_cleanup(rpc_transport_t *trans)
{
    if (!trans)
        return;

    if (trans->fini)
        trans->fini(trans);

    if (trans->options) {
        dict_unref(trans->options);
        trans->options = NULL;
    }

    GF_FREE(trans->name);

    if (trans->xl)
        pthread_mutex_destroy(&trans->lock);

    if (trans->dl_handle)
        dlclose(trans->dl_handle);

    GF_FREE(trans);
}

rpc_transport_t *
rpc_transport_load(glusterfs_ctx_t *ctx, dict_t *options, char *trans_name)
{
    struct rpc_transport *trans = NULL, *return_trans = NULL;
    char *name = NULL;
    void *handle = NULL;
    char *type = NULL;
    static char str[] = "ERROR";
    int32_t ret = -1;
    int is_tcp = 0, is_unix = 0, is_ibsdp = 0;
    volume_opt_list_t *vol_opt = NULL;
    gf_boolean_t bind_insecure = _gf_false;
    xlator_t *this = NULL;
    gf_boolean_t success = _gf_false;

    GF_VALIDATE_OR_GOTO("rpc-transport", options, fail);
    GF_VALIDATE_OR_GOTO("rpc-transport", ctx, fail);
    GF_VALIDATE_OR_GOTO("rpc-transport", trans_name, fail);

    trans = GF_CALLOC(1, sizeof(struct rpc_transport),
                      gf_common_mt_rpc_trans_t);
    if (!trans)
        goto fail;

    trans->name = gf_strdup(trans_name);
    if (!trans->name)
        goto fail;

    trans->ctx = ctx;
    type = str;

    /* Backward compatibility */
    ret = dict_get_str_sizen(options, "transport-type", &type);
    if (ret < 0) {
        ret = dict_set_str_sizen(options, "transport-type", "socket");
        if (ret < 0)
            gf_log("dict", GF_LOG_DEBUG, "setting transport-type failed");
        else
            gf_log("rpc-transport", GF_LOG_DEBUG,
                   "missing 'option transport-type'. defaulting to "
                   "\"socket\"");
    } else {
        {
            /* Backward compatibility to handle * /client,
             * * /server.
             */
            char *tmp = strchr(type, '/');
            if (tmp)
                *tmp = '\0';
        }

        is_tcp = strcmp(type, "tcp");
        is_unix = strcmp(type, "unix");
        is_ibsdp = strcmp(type, "ib-sdp");
        if ((is_tcp == 0) || (is_unix == 0) || (is_ibsdp == 0)) {
            if (is_unix == 0)
                ret = dict_set_str_sizen(options, "transport.address-family",
                                         "unix");
            if (is_ibsdp == 0)
                ret = dict_set_str_sizen(options, "transport.address-family",
                                         "inet-sdp");

            if (ret < 0)
                gf_log("dict", GF_LOG_DEBUG, "setting address-family failed");

            ret = dict_set_str_sizen(options, "transport-type", "socket");
            if (ret < 0)
                gf_log("dict", GF_LOG_DEBUG, "setting transport-type failed");
        }
    }

    /* client-bind-insecure is for clients protocol, and
     * bind-insecure for glusterd. Both mutually exclusive
     */
    ret = dict_get_str_sizen(options, "client-bind-insecure", &type);
    if (ret)
        ret = dict_get_str_sizen(options, "bind-insecure", &type);
    if (ret == 0) {
        ret = gf_string2boolean(type, &bind_insecure);
        if (ret < 0) {
            gf_log("rcp-transport", GF_LOG_WARNING,
                   "bind-insecure option %s is not a"
                   " valid bool option",
                   type);
            goto fail;
        }
        if (_gf_true == bind_insecure)
            trans->bind_insecure = 1;
        else
            trans->bind_insecure = 0;
    } else {
        /* By default allow bind insecure */
        trans->bind_insecure = 1;
    }

    ret = dict_get_str_sizen(options, "transport-type", &type);
    if (ret < 0) {
        gf_log("rpc-transport", GF_LOG_ERROR,
               "'option transport-type <xx>' missing in volume '%s'",
               trans_name);
        goto fail;
    }

    ret = gf_asprintf(&name, "%s/%s.so", RPC_TRANSPORTDIR, type);
    if (-1 == ret) {
        goto fail;
    }

    if (dict_get_sizen(options, "notify-poller-death")) {
        trans->notify_poller_death = 1;
    }

    gf_log("rpc-transport", GF_LOG_DEBUG, "attempt to load file %s", name);

    handle = dlopen(name, RTLD_NOW);
    if (handle == NULL) {
        gf_log("rpc-transport", GF_LOG_ERROR, "%s", dlerror());
        gf_log("rpc-transport", GF_LOG_WARNING,
               "volume '%s': transport-type '%s' is not valid or "
               "not found on this machine",
               trans_name, type);
        goto fail;
    }

    trans->dl_handle = handle;

    trans->ops = dlsym(handle, "tops");
    if (trans->ops == NULL) {
        gf_log("rpc-transport", GF_LOG_ERROR, "dlsym (rpc_transport_ops) on %s",
               dlerror());
        goto fail;
    }

    *VOID(&(trans->init)) = dlsym(handle, "init");
    if (trans->init == NULL) {
        gf_log("rpc-transport", GF_LOG_ERROR,
               "dlsym (gf_rpc_transport_init) on %s", dlerror());
        goto fail;
    }

    *VOID(&(trans->fini)) = dlsym(handle, "fini");
    if (trans->fini == NULL) {
        gf_log("rpc-transport", GF_LOG_ERROR,
               "dlsym (gf_rpc_transport_fini) on %s", dlerror());
        goto fail;
    }

    *VOID(&(trans->reconfigure)) = dlsym(handle, "reconfigure");
    if (trans->reconfigure == NULL) {
        gf_log("rpc-transport", GF_LOG_DEBUG,
               "dlsym (gf_rpc_transport_reconfigure) on %s", dlerror());
    }

    vol_opt = GF_CALLOC(1, sizeof(volume_opt_list_t),
                        gf_common_mt_volume_opt_list_t);
    if (!vol_opt) {
        goto fail;
    }

    this = THIS;
    vol_opt->given_opt = dlsym(handle, "options");
    if (vol_opt->given_opt == NULL) {
        gf_log("rpc-transport", GF_LOG_DEBUG,
               "volume option validation not specified");
    } else {
        INIT_LIST_HEAD(&vol_opt->list);
        list_add_tail(&vol_opt->list, &(this->volume_options));
        if (xlator_options_validate_list(this, options, vol_opt, NULL)) {
            gf_log("rpc-transport", GF_LOG_ERROR,
                   "volume option validation failed");
            goto fail;
        }
    }

    trans->options = dict_ref(options);

    pthread_mutex_init(&trans->lock, NULL);
    trans->xl = this;

    ret = trans->init(trans);
    if (ret != 0) {
        gf_log("rpc-transport", GF_LOG_WARNING, "'%s' initialization failed",
               type);
        goto fail;
    }

    INIT_LIST_HEAD(&trans->list);
    GF_ATOMIC_INIT(trans->disconnect_progress, 0);

    return_trans = trans;

    GF_FREE(name);

    success = _gf_true;

fail:
    if (!success) {
        rpc_transport_cleanup(trans);
        GF_FREE(name);

        return_trans = NULL;
    }

    if (vol_opt) {
        if (!list_empty(&vol_opt->list)) {
            list_del_init(&vol_opt->list);
        }
        GF_FREE(vol_opt);
    }

    return return_trans;
}

int32_t
rpc_transport_submit_request(rpc_transport_t *this, rpc_transport_req_t *req)
{
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);
    GF_VALIDATE_OR_GOTO("rpc_transport", this->ops, fail);

    ret = this->ops->submit_request(this, req);
fail:
    return ret;
}

int32_t
rpc_transport_submit_reply(rpc_transport_t *this, rpc_transport_reply_t *reply)
{
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);
    GF_VALIDATE_OR_GOTO("rpc_transport", this->ops, fail);

    ret = this->ops->submit_reply(this, reply);
fail:
    return ret;
}

int32_t
rpc_transport_connect(rpc_transport_t *this, int port)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);

    ret = this->ops->connect(this, port);
fail:
    return ret;
}

int32_t
rpc_transport_listen(rpc_transport_t *this)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);

    ret = this->ops->listen(this);
fail:
    return ret;
}

int32_t
rpc_transport_disconnect(rpc_transport_t *this, gf_boolean_t wait)
{
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);

    ret = this->ops->disconnect(this, wait);

fail:
    return ret;
}

static void
rpc_transport_destroy(rpc_transport_t *this)
{
    struct dnscache6 *cache = NULL;

    if (this->clnt_options)
        dict_unref(this->clnt_options);
    if (this->options)
        dict_unref(this->options);
    if (this->fini)
        this->fini(this);

    pthread_mutex_destroy(&this->lock);

    GF_FREE(this->name);

    if (this->dl_handle)
        dlclose(this->dl_handle);

    if (this->ssl_name) {
        GF_FREE(this->ssl_name);
    }

    if (this->dnscache) {
        cache = this->dnscache;
        if (cache->first)
            freeaddrinfo(cache->first);
        GF_FREE(this->dnscache);
    }

    GF_FREE(this);
}

rpc_transport_t *
rpc_transport_ref(rpc_transport_t *this)
{
    rpc_transport_t *return_this = NULL;

    GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);

    GF_ATOMIC_INC(this->refcount);

    return_this = this;
fail:
    return return_this;
}

int32_t
rpc_transport_unref(rpc_transport_t *this)
{
    int32_t refcount = 0;
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);

    refcount = GF_ATOMIC_DEC(this->refcount);

    if (refcount == 0) {
        if (this->mydata)
            this->notify(this, this->mydata, RPC_TRANSPORT_CLEANUP, NULL);
        this->mydata = NULL;
        this->notify = NULL;
        rpc_transport_destroy(this);
    }

    ret = 0;
fail:
    return ret;
}

int32_t
rpc_transport_notify(rpc_transport_t *this, rpc_transport_event_t event,
                     void *data, ...)
{
    int32_t ret = -1;
    GF_VALIDATE_OR_GOTO("rpc", this, out);

    if (this->notify != NULL) {
        ret = this->notify(this, this->mydata, event, data);
    } else {
        ret = 0;
    }
out:
    return ret;
}

int
rpc_transport_register_notify(rpc_transport_t *trans,
                              rpc_transport_notify_t notify, void *mydata)
{
    int32_t ret = -1;
    GF_VALIDATE_OR_GOTO("rpc", trans, out);

    trans->notify = notify;
    trans->mydata = mydata;

    ret = 0;
out:
    return ret;
}

// give negative values to skip setting that value
// this function asserts if both the values are negative.
// why call it if you don't set it.
int
rpc_transport_keepalive_options_set(dict_t *options, int32_t interval,
                                    int32_t time, int32_t timeout)
{
    int ret = -1;

    GF_ASSERT(options);
    GF_ASSERT((interval > 0) || (time > 0));

    ret = dict_set_int32_sizen(options, "transport.socket.keepalive-interval",
                               interval);
    if (ret)
        goto out;

    ret = dict_set_int32_sizen(options, "transport.socket.keepalive-time",
                               time);
    if (ret)
        goto out;

    ret = dict_set_int32_sizen(options, "transport.tcp-user-timeout", timeout);
    if (ret)
        goto out;
out:
    return ret;
}

int
rpc_transport_unix_options_build(dict_t *dict, char *filepath,
                                 int frame_timeout)
{
    char *fpath = NULL;
    int ret = -1;

    GF_ASSERT(filepath);
    GF_VALIDATE_OR_GOTO("rpc-transport", dict, out);

    fpath = gf_strdup(filepath);
    if (!fpath) {
        ret = -1;
        goto out;
    }

    ret = dict_set_dynstr_sizen(dict, "transport.socket.connect-path", fpath);
    if (ret) {
        GF_FREE(fpath);
        goto out;
    }

    ret = dict_set_str_sizen(dict, "transport.address-family", "unix");
    if (ret)
        goto out;

    ret = dict_set_str_sizen(dict, "transport.socket.nodelay", "off");
    if (ret)
        goto out;

    ret = dict_set_str_sizen(dict, "transport-type", "socket");
    if (ret)
        goto out;

    ret = dict_set_str_sizen(dict, "transport.socket.keepalive", "off");
    if (ret)
        goto out;

    if (frame_timeout > 0) {
        ret = dict_set_int32_sizen(dict, "frame-timeout", frame_timeout);
        if (ret)
            goto out;
    }
out:
    return ret;
}

int
rpc_transport_inet_options_build(dict_t *dict, const char *hostname, int port,
                                 char *af)
{
    char *host = NULL;
    int ret = -1;
#ifdef IPV6_DEFAULT
    static char *addr_family = "inet6";
#else
    static char *addr_family = "inet";
#endif

    GF_ASSERT(hostname);
    GF_ASSERT(port >= 1024);
    GF_VALIDATE_OR_GOTO("rpc-transport", dict, out);

    host = gf_strdup((char *)hostname);
    if (!host) {
        ret = -1;
        goto out;
    }

    ret = dict_set_dynstr_sizen(dict, "remote-host", host);
    if (ret) {
        gf_log(THIS->name, GF_LOG_WARNING, "failed to set remote-host with %s",
               host);
        GF_FREE(host);
        goto out;
    }

    ret = dict_set_int32_sizen(dict, "remote-port", port);
    if (ret) {
        gf_log(THIS->name, GF_LOG_WARNING, "failed to set remote-port with %d",
               port);
        goto out;
    }

    ret = dict_set_str_sizen(dict, "address-family",
                             (af != NULL ? af : addr_family));
    if (ret) {
        gf_log(THIS->name, GF_LOG_WARNING, "failed to set address-family to %s",
               addr_family);
        goto out;
    }

    ret = dict_set_str_sizen(dict, "transport-type", "socket");
    if (ret) {
        gf_log(THIS->name, GF_LOG_WARNING,
               "failed to set trans-type with socket");
        goto out;
    }
out:
    return ret;
}
