/*
  Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <sys/time.h>
#include <sys/resource.h>

#include "server.h"
#include "server-helpers.h"
#include "glusterfs3-xdr.h"
#include <glusterfs/call-stub.h>
#include <glusterfs/statedump.h>
#include <glusterfs/defaults.h>
#include "authenticate.h"
#include <glusterfs/gf-event.h>
#include <glusterfs/events.h>
#include "server-messages.h"
#include "rpc-clnt.h"

rpcsvc_cbk_program_t server_cbk_prog = {
    .progname = "Gluster Callback",
    .prognum = GLUSTER_CBK_PROGRAM,
    .progver = GLUSTER_CBK_VERSION,
};

struct iobuf *
gfs_serialize_reply(rpcsvc_request_t *req, void *arg, struct iovec *outmsg,
                    xdrproc_t xdrproc)
{
    struct iobuf *iob = NULL;
    ssize_t retlen = 0;
    ssize_t xdr_size = 0;

    GF_VALIDATE_OR_GOTO("server", req, ret);

    /* First, get the io buffer into which the reply in arg will
     * be serialized.
     */
    if (arg && xdrproc) {
        xdr_size = xdr_sizeof(xdrproc, arg);
        iob = iobuf_get2(req->svc->ctx->iobuf_pool, xdr_size);
        if (!iob) {
            gf_msg_callingfn(THIS->name, GF_LOG_ERROR, ENOMEM, PS_MSG_NO_MEMORY,
                             "Failed to get iobuf");
            goto ret;
        };

        iobuf_to_iovec(iob, outmsg);
        /* Use the given serializer to translate the give C structure in arg
         * to XDR format which will be written into the buffer in outmsg.
         */
        /* retlen is used to received the error since size_t is unsigned and we
         * need -1 for error notification during encoding.
         */

        retlen = xdr_serialize_generic(*outmsg, arg, xdrproc);
        if (retlen == -1) {
            /* Failed to Encode 'GlusterFS' msg in RPC is not exactly
               failure of RPC return values.. client should get
               notified about this, so there are no missing frames */
            gf_msg_callingfn("", GF_LOG_ERROR, 0, PS_MSG_ENCODE_MSG_FAILED,
                             "Failed to encode message");
            req->rpc_err = GARBAGE_ARGS;
            retlen = 0;
        }
    }
    outmsg->iov_len = retlen;
ret:
    return iob;
}

int
server_submit_reply(call_frame_t *frame, rpcsvc_request_t *req, void *arg,
                    struct iovec *payload, int payloadcount,
                    struct iobref *iobref, xdrproc_t xdrproc)
{
    struct iobuf *iob = NULL;
    int ret = -1;
    struct iovec rsp = {
        0,
    };
    server_state_t *state = NULL;
    char new_iobref = 0;
    client_t *client = NULL;

    GF_VALIDATE_OR_GOTO("server", req, ret);

    if (frame) {
        state = CALL_STATE(frame);
        frame->local = NULL;
        client = frame->root->client;
    }

    if (!iobref) {
        iobref = iobref_new();
        if (!iobref) {
            goto ret;
        }

        new_iobref = 1;
    }

    iob = gfs_serialize_reply(req, arg, &rsp, xdrproc);
    if (!iob) {
        gf_smsg("", GF_LOG_ERROR, 0, PS_MSG_SERIALIZE_REPLY_FAILED, NULL);
        goto ret;
    }

    iobref_add(iobref, iob);

    /* Then, submit the message for transmission. */
    ret = rpcsvc_submit_generic(req, &rsp, 1, payload, payloadcount, iobref);

    /* TODO: this is demo purpose only */
    /* ret = rpcsvc_callback_submit (req->svc, req->trans, req->prog,
       GF_CBK_NULL, &rsp, 1);
    */
    /* Now that we've done our job of handing the message to the RPC layer
     * we can safely unref the iob in the hope that RPC layer must have
     * ref'ed the iob on receiving into the txlist.
     */
    iobuf_unref(iob);
    if (ret == -1) {
        gf_msg_callingfn("", GF_LOG_ERROR, 0, PS_MSG_REPLY_SUBMIT_FAILED,
                         "Reply submission failed");
        if (frame && client) {
            server_connection_cleanup(frame->this, client,
                                      INTERNAL_LOCKS | POSIX_LOCKS, NULL);
        } else {
            gf_msg_callingfn("", GF_LOG_ERROR, 0, PS_MSG_REPLY_SUBMIT_FAILED,
                             "Reply submission failed");
            /* TODO: Failure of open(dir), create, inodelk, entrylk
               or lk fops send failure must be handled specially. */
        }
        goto ret;
    }

    ret = 0;
ret:
    if (client)
        gf_client_unref(client);

    if (frame)
        STACK_DESTROY(frame->root);

    if (new_iobref)
        iobref_unref(iobref);

    if (state)
        free_state(state);

    return ret;
}

int
server_priv_to_dict(xlator_t *this, dict_t *dict, char *brickname)
{
    server_conf_t *conf = NULL;
    rpc_transport_t *xprt = NULL;
    peer_info_t *peerinfo = NULL;
    char key[32] = {
        0,
    };
    int keylen;
    int count = 0;
    int ret = -1;

    GF_VALIDATE_OR_GOTO(THIS->name, this, out);
    GF_VALIDATE_OR_GOTO(THIS->name, dict, out);

    conf = this->private;
    if (!conf)
        return 0;
    // TODO: Dump only specific info to dict

    pthread_mutex_lock(&conf->mutex);
    {
        list_for_each_entry(xprt, &conf->xprt_list, list)
        {
            if ((xprt->xl_private) && (xprt->xl_private->bound_xl) &&
                (xprt->xl_private->bound_xl->name) && (brickname) &&
                (!strcmp(brickname, xprt->xl_private->bound_xl->name))) {
                peerinfo = &xprt->peerinfo;
                keylen = snprintf(key, sizeof(key), "client%d.hostname", count);
                ret = dict_set_strn(dict, key, keylen, peerinfo->identifier);
                if (ret)
                    goto unlock;

                snprintf(key, sizeof(key), "client%d.bytesread", count);
                ret = dict_set_uint64(dict, key, xprt->total_bytes_read);
                if (ret)
                    goto unlock;

                snprintf(key, sizeof(key), "client%d.byteswrite", count);
                ret = dict_set_uint64(dict, key, xprt->total_bytes_write);
                if (ret)
                    goto unlock;

                snprintf(key, sizeof(key), "client%d.opversion", count);
                ret = dict_set_uint32(dict, key, peerinfo->max_op_version);
                if (ret)
                    goto unlock;

                keylen = snprintf(key, sizeof(key), "client%d.name", count);
                ret = dict_set_strn(dict, key, keylen,
                                    xprt->xl_private->client_name);
                if (ret)
                    goto unlock;

                count++;
            }
        }
    }
unlock:
    pthread_mutex_unlock(&conf->mutex);
    if (ret)
        goto out;

    ret = dict_set_int32_sizen(dict, "clientcount", count);

out:
    return ret;
}

int
server_priv(xlator_t *this)
{
    server_conf_t *conf = NULL;
    rpc_transport_t *xprt = NULL;
    char key[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };
    uint64_t total_read = 0;
    uint64_t total_write = 0;
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("server", this, out);

    conf = this->private;
    if (!conf)
        return 0;

    gf_proc_dump_build_key(key, "xlator.protocol.server", "priv");
    gf_proc_dump_add_section("%s", key);

    ret = pthread_mutex_trylock(&conf->mutex);
    if (ret != 0)
        goto out;
    {
        list_for_each_entry(xprt, &conf->xprt_list, list)
        {
            total_read += xprt->total_bytes_read;
            total_write += xprt->total_bytes_write;
        }
    }
    pthread_mutex_unlock(&conf->mutex);

    gf_proc_dump_build_key(key, "server", "total-bytes-read");
    gf_proc_dump_write(key, "%" PRIu64, total_read);

    gf_proc_dump_build_key(key, "server", "total-bytes-write");
    gf_proc_dump_write(key, "%" PRIu64, total_write);

    rpcsvc_statedump(conf->rpc);

    ret = 0;
out:
    if (ret)
        gf_proc_dump_write("Unable to print priv",
                           "(Lock acquisition failed) %s",
                           this ? this->name : "server");

    return ret;
}

static int
get_auth_types(dict_t *this, char *key, data_t *value, void *data)
{
    dict_t *auth_dict = NULL;
    char *saveptr = NULL;
    char *tmp = NULL;
    char *key_cpy = NULL;
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("server", this, out);
    GF_VALIDATE_OR_GOTO("server", key, out);
    GF_VALIDATE_OR_GOTO("server", data, out);

    auth_dict = data;
    key_cpy = gf_strdup(key);
    GF_VALIDATE_OR_GOTO("server", key_cpy, out);

    tmp = strtok_r(key_cpy, ".", &saveptr);
    ret = strcmp(tmp, "auth");
    if (ret == 0) {
        tmp = strtok_r(NULL, ".", &saveptr);
        if (strcmp(tmp, "ip") == 0) {
            /* TODO: backward compatibility, remove when
               newer versions are available */
            tmp = "addr";
            gf_smsg("server", GF_LOG_WARNING, 0, PS_MSG_AUTH_IP_ERROR, NULL);
        }
        ret = dict_set_dynptr(auth_dict, tmp, NULL, 0);
        if (ret < 0) {
            gf_msg_debug("server", 0,
                         "failed to "
                         "dict_set_dynptr");
        }
    }

    GF_FREE(key_cpy);
out:
    return 0;
}

int
_check_for_auth_option(dict_t *d, char *k, data_t *v, void *tmp)
{
    int ret = 0;
    xlator_t *xl = NULL;
    char *tail = NULL;

    xl = tmp;

    tail = strtail(k, "auth.");
    if (!tail)
        goto out;

    if (strncmp(tail, "addr.", 5) != 0) {
        gf_smsg(xl->name, GF_LOG_TRACE, 0, PS_MSG_SKIP_FORMAT_CHK, "option=%s",
                k, NULL);
        goto out;
    }

    /* fast fwd through module type */
    tail = strchr(tail, '.');
    if (!tail)
        goto out;
    tail++;

    tail = strtail(tail, xl->name);
    if (!tail)
        goto out;

    if (*tail == '.') {
        /* when we are here, the key is checked for
         * valid auth.allow.<xlator>
         * Now we verify the ip address
         */
        ret = xlator_option_validate_addr_list(xl, "auth-*", v->data, NULL,
                                               NULL);
        if (ret)
            gf_smsg(xl->name, GF_LOG_ERROR, 0, PS_MSG_INTERNET_ADDR_ERROR,
                    "data=%s", v->data, NULL);
    }
out:
    return ret;
}

int
validate_auth_options(xlator_t *this, dict_t *dict)
{
    int error = -1;
    xlator_list_t *trav = NULL;

    GF_VALIDATE_OR_GOTO("server", this, out);
    GF_VALIDATE_OR_GOTO("server", dict, out);

    trav = this->children;
    while (trav) {
        error = dict_foreach(dict, _check_for_auth_option, trav->xlator);

        if (-1 == error) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_AUTHENTICATE_ERROR,
                    "name=%s", trav->xlator->name, NULL);
            break;
        }
        trav = trav->next;
    }

out:
    return error;
}

void
server_call_xlator_mem_cleanup(xlator_t *this, char *victim_name)
{
    pthread_t th_id = {
        0,
    };
    int th_ret = -1;
    server_cleanup_xprt_arg_t *arg = NULL;

    if (!victim_name)
        return;

    gf_log(this->name, GF_LOG_INFO, "Create graph janitor thread for brick %s",
           victim_name);

    arg = calloc(1, sizeof(*arg));
    arg->this = this;
    arg->victim_name = strdup(victim_name);
    if (!arg->victim_name) {
        gf_smsg(this->name, GF_LOG_CRITICAL, ENOMEM, LG_MSG_NO_MEMORY,
                "Memory allocation is failed");
        free(arg);
        return;
    }

    th_ret = gf_thread_create_detached(&th_id, server_graph_janitor_threads,
                                       arg, "graphjanitor");
    if (th_ret) {
        gf_log(this->name, GF_LOG_ERROR,
               "graph janitor Thread"
               " creation is failed for brick %s",
               victim_name);
        free(arg->victim_name);
        free(arg);
    }
}

int
server_rpc_notify(rpcsvc_t *rpc, void *xl, rpcsvc_event_t event, void *data)
{
    gf_boolean_t detached = _gf_false;
    xlator_t *this = NULL;
    rpc_transport_t *trans = NULL;
    server_conf_t *conf = NULL;
    client_t *client = NULL;
    char *auth_path = NULL;
    int ret = -1;
    char *xlator_name = NULL;
    uint64_t xprtrefcount = 0;
    gf_boolean_t fd_exist = _gf_false;

    this = xl;
    trans = data;

    if (!this || !data || !this->ctx || !this->ctx->active) {
        gf_msg_callingfn("server", GF_LOG_WARNING, 0, PS_MSG_RPC_NOTIFY_ERROR,
                         "Calling rpc_notify without initializing");
        goto out;
    }

    conf = this->private;

    switch (event) {
        case RPCSVC_EVENT_ACCEPT: {
            /* Have a structure per new connection */
            /* TODO: Should we create anything here at all ? * /
               client->conn = create_server_conn_state (this, trans);
               if (!client->conn)
               goto out;

               trans->protocol_private = client->conn;
            */

            pthread_mutex_lock(&conf->mutex);
            rpc_transport_ref(trans);
            list_add_tail(&trans->list, &conf->xprt_list);
            pthread_mutex_unlock(&conf->mutex);

            break;
        }
        case RPCSVC_EVENT_DISCONNECT:

            /* A DISCONNECT event could come without an ACCEPT event
             * happening for this transport. This happens when the server is
             * expecting encrypted connections by the client tries to
             * connect unecnrypted
             */
            if (list_empty(&trans->list)) {
                break;
            }

            /* Set the disconnect_progress flag to 1 to avoid races
               during brick detach while brick mux is enabled
            */
            GF_ATOMIC_INIT(trans->disconnect_progress, 1);
            /* transport has to be removed from the list upon disconnect
             * irrespective of whether lock self heal is off or on, since
             * new transport will be created upon reconnect.
             */
            pthread_mutex_lock(&conf->mutex);
            client = trans->xl_private;
            if (!client)
                list_del_init(&trans->list);
            pthread_mutex_unlock(&conf->mutex);

            if (!client)
                goto unref_transport;

            gf_smsg(this->name, GF_LOG_INFO, 0, PS_MSG_CLIENT_DISCONNECTING,
                    "client-uid=%s", client->client_uid, NULL);

            ret = dict_get_str_sizen(this->options, "auth-path", &auth_path);
            if (ret) {
                gf_smsg(this->name, GF_LOG_WARNING, 0, PS_MSG_DICT_GET_FAILED,
                        "type=auth-path", NULL);
                auth_path = NULL;
            }

            gf_client_ref(client);
            gf_client_put(client, &detached);
            if (detached) {
                server_connection_cleanup(
                    this, client, INTERNAL_LOCKS | POSIX_LOCKS, &fd_exist);
                gf_event(EVENT_CLIENT_DISCONNECT,
                         "client_uid=%s;"
                         "client_identifier=%s;server_identifier=%s;"
                         "brick_path=%s",
                         client->client_uid, trans->peerinfo.identifier,
                         trans->myinfo.identifier, auth_path);
            }

            /*
             * gf_client_unref will be done while handling
             * RPC_EVENT_TRANSPORT_DESTROY
             */

        unref_transport:
            /* rpc_transport_unref() causes a RPCSVC_EVENT_TRANSPORT_DESTROY
             * to be called in blocking manner
             * So no code should ideally be after this unref, Call
             * rpc_transport_unref only while cleanup_starting flag is not set
             * otherwise transport_unref will be call by either
             * server_connection_cleanup_flush_cbk or server_submit_reply at the
             * time of freeing state
             */
            if (!client || !detached || !fd_exist)
                rpc_transport_unref(trans);
            break;
        case RPCSVC_EVENT_TRANSPORT_DESTROY:
            pthread_mutex_lock(&conf->mutex);
            client = trans->xl_private;
            list_del_init(&trans->list);
            pthread_mutex_unlock(&conf->mutex);
            if (!client)
                break;

            if (client->bound_xl && client->bound_xl->cleanup_starting) {
                xprtrefcount = GF_ATOMIC_GET(client->bound_xl->xprtrefcnt);
                if (xprtrefcount > 0) {
                    xprtrefcount = GF_ATOMIC_DEC(client->bound_xl->xprtrefcnt);
                    if (xprtrefcount == 0)
                        xlator_name = gf_strdup(client->bound_xl->name);
                }
            }

            gf_client_unref(client);

            if (xlator_name) {
                server_call_xlator_mem_cleanup(this, xlator_name);
                GF_FREE(xlator_name);
            }

            trans->xl_private = NULL;
            break;
        default:
            break;
    }
    ret = 0;
out:
    /* Only case when ret == -1 is the initial validation */
    return ret;
}

void *
server_graph_janitor_threads(void *data)
{
    xlator_t *victim = NULL;
    xlator_t *this = NULL;
    server_conf_t *conf = NULL;
    glusterfs_ctx_t *ctx = NULL;
    char *victim_name = NULL;
    server_cleanup_xprt_arg_t *arg = NULL;
    gf_boolean_t victim_found = _gf_false;
    xlator_list_t **trav_p = NULL;
    xlator_t *top = NULL;
    uint32_t parent_down = 0;

    GF_ASSERT(data);

    arg = data;
    this = arg->this;
    victim_name = arg->victim_name;
    THIS = arg->this;
    conf = this->private;

    ctx = THIS->ctx;
    GF_VALIDATE_OR_GOTO(this->name, ctx, out);

    top = this->ctx->active->first;
    LOCK(&ctx->volfile_lock);
    for (trav_p = &top->children; *trav_p; trav_p = &(*trav_p)->next) {
        victim = (*trav_p)->xlator;
        if (victim->cleanup_starting &&
            strcmp(victim->name, victim_name) == 0) {
            parent_down = victim->parent_down;
            victim->parent_down = 1;
            if (!parent_down)
                victim_found = _gf_true;
            break;
        }
    }
    if (victim_found)
        glusterfs_delete_volfile_checksum(ctx, victim->volfile_id);
    UNLOCK(&ctx->volfile_lock);
    if (!victim_found) {
        gf_log(this->name, GF_LOG_ERROR,
               "victim brick %s is not"
               " found in graph",
               victim_name);
        goto out;
    }

    default_notify(victim, GF_EVENT_PARENT_DOWN, victim);
    if (victim->notify_down) {
        gf_log(THIS->name, GF_LOG_INFO,
               "Start call fini for brick"
               " %s stack",
               victim->name);
        xlator_mem_cleanup(victim);
        rpcsvc_autoscale_threads(ctx, conf->rpc, -1);
    }

out:
    free(arg->victim_name);
    free(arg);
    return NULL;
}

int32_t
server_mem_acct_init(xlator_t *this)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("server", this, out);

    ret = xlator_mem_acct_init(this, gf_server_mt_end + 1);

    if (ret != 0) {
        gf_smsg(this->name, GF_LOG_ERROR, ENOMEM, PS_MSG_NO_MEMORY, NULL);
        return ret;
    }
out:
    return ret;
}

static int
_delete_auth_opt(dict_t *this, char *key, data_t *value, void *data)
{
    char *auth_option_pattern[] = {
        "auth.addr.*.allow",     "auth.addr.*.reject",     "auth.login.*.allow",
        "auth.login.*.password", "auth.login.*.ssl-allow", NULL};
    int i = 0;

    for (i = 0; auth_option_pattern[i]; i++) {
        if (fnmatch(auth_option_pattern[i], key, 0) == 0) {
            dict_del(this, key);
            break;
        }
    }

    return 0;
}

static int
_copy_auth_opt(dict_t *unused, char *key, data_t *value, void *xl_dict)
{
    char *auth_option_pattern[] = {
        "auth.addr.*.allow",     "auth.addr.*.reject",     "auth.login.*.allow",
        "auth.login.*.password", "auth.login.*.ssl-allow", NULL};
    int i = 0;

    for (i = 0; auth_option_pattern[i]; i++) {
        if (fnmatch(auth_option_pattern[i], key, 0) == 0) {
            dict_set((dict_t *)xl_dict, key, value);
            break;
        }
    }

    return 0;
}

int
server_check_event_threads(xlator_t *this, server_conf_t *conf, int32_t new)
{
    struct event_pool *pool = this->ctx->event_pool;
    int target;

    target = new + pool->auto_thread_count;
    conf->event_threads = new;

    if (target == pool->eventthreadcount) {
        return 0;
    }

    return gf_event_reconfigure_threads(pool, target);
}

int
server_reconfigure(xlator_t *this, dict_t *options)
{
    server_conf_t *conf = NULL;
    rpcsvc_t *rpc_conf;
    rpcsvc_listener_t *listeners;
    rpc_transport_t *xprt = NULL;
    rpc_transport_t *xp_next = NULL;
    int inode_lru_limit;
    gf_boolean_t trace;
    data_t *data;
    int ret = 0;
    char *statedump_path = NULL;
    int32_t new_nthread = 0;
    char *auth_path = NULL;
    char *xprt_path = NULL;
    xlator_t *oldTHIS;
    xlator_t *kid;

    /*
     * Since we're not a fop, we can't really count on THIS being set
     * correctly, and it needs to be or else GF_OPTION_RECONF won't work
     * (because it won't find our options list).  This is another thing
     * that "just happened" to work before multiplexing, but now we need to
     * handle it more explicitly.
     */
    oldTHIS = THIS;
    THIS = this;

    conf = this->private;

    if (!conf) {
        gf_msg_callingfn(this->name, GF_LOG_DEBUG, EINVAL, PS_MSG_INVALID_ENTRY,
                         "conf == null!!!");
        goto out;
    }

    /*
     * For some of the auth/rpc stuff, we need to operate on the correct
     * child, but for other stuff we need to operate on the server
     * translator itself.
     */
    kid = NULL;
    if (dict_get_str_sizen(options, "auth-path", &auth_path) == 0) {
        kid = get_xlator_by_name(this, auth_path);
    }
    if (!kid) {
        kid = this;
    }

    if (dict_get_int32_sizen(options, "inode-lru-limit", &inode_lru_limit) ==
        0) {
        conf->inode_lru_limit = inode_lru_limit;
        gf_msg_trace(this->name, 0,
                     "Reconfigured inode-lru-limit to "
                     "%d",
                     conf->inode_lru_limit);

        /* traverse through the xlator graph. For each xlator in the
           graph check whether it is a bound_xl or not (bound_xl means
           the xlator will have its itable pointer set). If so, then
           set the lru limit for the itable.
        */
        xlator_foreach(this, xlator_set_inode_lru_limit, &inode_lru_limit);
    }

    data = dict_get_sizen(options, "trace");
    if (data) {
        ret = gf_string2boolean(data->data, &trace);
        if (ret != 0) {
            gf_smsg(this->name, GF_LOG_WARNING, EINVAL, PS_MSG_INVALID_ENTRY,
                    NULL);
            ret = -1;
            goto out;
        }
        conf->trace = trace;
        gf_msg_trace(this->name, 0, "Reconfigured trace to %d", conf->trace);
    }

    GF_OPTION_RECONF("statedump-path", statedump_path, options, path, do_auth);
    if (!statedump_path) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_STATEDUMP_PATH_ERROR, NULL);
        goto do_auth;
    }
    gf_path_strip_trailing_slashes(statedump_path);
    GF_FREE(this->ctx->statedump_path);
    this->ctx->statedump_path = gf_strdup(statedump_path);

do_auth:
    if (!conf->auth_modules)
        conf->auth_modules = dict_new();

    dict_foreach(options, get_auth_types, conf->auth_modules);
    ret = validate_auth_options(kid, options);
    if (ret == -1) {
        /* logging already done in validate_auth_options function. */
        goto out;
    }

    dict_foreach(kid->options, _delete_auth_opt, NULL);
    dict_foreach(options, _copy_auth_opt, kid->options);

    ret = gf_auth_init(kid, conf->auth_modules);
    if (ret) {
        dict_unref(conf->auth_modules);
        goto out;
    }

    GF_OPTION_RECONF("manage-gids", conf->server_manage_gids, options, bool,
                     do_rpc);

    GF_OPTION_RECONF("gid-timeout", conf->gid_cache_timeout, options, int32,
                     do_rpc);
    if (gid_cache_reconf(&conf->gid_cache, conf->gid_cache_timeout) < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_GRP_CACHE_ERROR, NULL);
        goto do_rpc;
    }

do_rpc:
    rpc_conf = conf->rpc;
    if (!rpc_conf) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_RPC_CONF_ERROR, NULL);
        goto out;
    }

    ret = rpcsvc_auth_reconf(rpc_conf, options);
    if (ret == -1) {
        gf_log(GF_RPCSVC, GF_LOG_ERROR, "Failed to reconfigure authentication");
        goto out;
    }

    GF_OPTION_RECONF("strict-auth-accept", conf->strict_auth_enabled, options,
                     bool, out);

    GF_OPTION_RECONF("dynamic-auth", conf->dync_auth, options, bool, out);

    if (conf->dync_auth) {
        pthread_mutex_lock(&conf->mutex);
        {
            /*
             * Disconnecting will (usually) drop the last ref,
             * which will cause the transport to be unlinked and
             * freed while we're still traversing, which will cause
             * us to crash unless we use list_for_each_entry_safe.
             */
            list_for_each_entry_safe(xprt, xp_next, &conf->xprt_list, list)
            {
                /* check for client authorization */
                if (!xprt->clnt_options) {
                    /* If clnt_options dictionary is null,
                     * which means for this transport
                     * server_setvolume was not called.
                     *
                     * So here we can skip authentication
                     * because server_setvolume will do
                     * gf_authenticate.
                     *
                     */
                    continue;
                }
                /*
                 * Make sure we're only operating on
                 * connections that are relevant to the brick
                 * we're reconfiguring.
                 */
                if (dict_get_str_sizen(xprt->clnt_options, "remote-subvolume",
                                       &xprt_path) != 0) {
                    continue;
                }
                if (strcmp(xprt_path, auth_path) != 0) {
                    continue;
                }
                ret = gf_authenticate(xprt->clnt_options, options,
                                      conf->auth_modules);
                if (ret == AUTH_ACCEPT) {
                    gf_smsg(kid->name, GF_LOG_TRACE, 0, PS_MSG_CLIENT_ACCEPTED,
                            NULL);
                } else {
                    gf_event(EVENT_CLIENT_AUTH_REJECT,
                             "client_uid=%s;"
                             "client_identifier=%s;"
                             "server_identifier=%s;"
                             "brick_path=%s",
                             xprt->xl_private->client_uid,
                             xprt->peerinfo.identifier, xprt->myinfo.identifier,
                             auth_path);
                    gf_smsg(this->name, GF_LOG_INFO, EACCES,
                            PS_MSG_UNAUTHORIZED_CLIENT,
                            "peerinfo-identifier=%s", xprt->peerinfo.identifier,
                            NULL);
                    rpc_transport_disconnect(xprt, _gf_false);
                }
            }
        }
        pthread_mutex_unlock(&conf->mutex);
    }

    ret = rpcsvc_set_outstanding_rpc_limit(
        rpc_conf, options, RPCSVC_DEFAULT_OUTSTANDING_RPC_LIMIT);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_RECONFIGURE_FAILED, NULL);
        goto out;
    }

    list_for_each_entry(listeners, &(rpc_conf->listeners), list)
    {
        if (listeners->trans != NULL) {
            if (listeners->trans->reconfigure)
                listeners->trans->reconfigure(listeners->trans, options);
            else
                gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_TRANSPORT_ERROR,
                        NULL);
        }
    }

    /*
     * Update:
     * We don't need to reset auto_thread_count since it has been derived
     * out of the total bricks attached. We can reconfigure event threads
     * but not auto threads.
     */

    GF_OPTION_RECONF("event-threads", new_nthread, options, int32, out);
    ret = server_check_event_threads(this, conf, new_nthread);
    if (ret)
        goto out;

out:
    THIS = oldTHIS;
    gf_msg_debug("", 0, "returning %d", ret);
    return ret;
}

static int32_t
client_destroy_cbk(xlator_t *this, client_t *client)
{
    void *tmp = NULL;
    server_ctx_t *ctx = NULL;

    client_ctx_del(client, this, &tmp);

    ctx = tmp;

    if (ctx == NULL)
        return 0;

    gf_fd_fdtable_destroy(ctx->fdtable);
    LOCK_DESTROY(&ctx->fdtable_lock);
    GF_FREE(ctx);

    return 0;
}

int32_t
server_dump_metrics(xlator_t *this, int fd)
{
    rpc_transport_t *xprt = NULL;
    server_conf_t *conf = NULL;
    client_t *client = NULL;

    conf = this->private;

    pthread_mutex_lock(&conf->mutex);

    list_for_each_entry(xprt, &conf->xprt_list, list)
    {
        client = xprt->xl_private;

        if (!client)
            continue;

        dprintf(fd, "%s.total.rpc.%s.bytes_read %" PRIu64 "\n", this->name,
                client->client_uid, xprt->total_bytes_read);
        dprintf(fd, "%s.total.rpc.%s.bytes_write %" PRIu64 "\n", this->name,
                client->client_uid, xprt->total_bytes_write);
        dprintf(fd, "%s.total.rpc.%s.outstanding %d\n", this->name,
                client->client_uid, xprt->outstanding_rpc_count);
    }

    pthread_mutex_unlock(&conf->mutex);

    return 0;
}

void
server_cleanup(xlator_t *this, server_conf_t *conf)
{
    if (!this || !conf)
        return;

    LOCK_DESTROY(&conf->itable_lock);
    pthread_mutex_destroy(&conf->mutex);

    if (this->ctx->event_pool) {
        /* Free the event pool */
        (void)gf_event_pool_destroy(this->ctx->event_pool);
    }

    if (dict_get_sizen(this->options, "config-directory")) {
        GF_FREE(conf->conf_dir);
        conf->conf_dir = NULL;
    }

    if (conf->child_status) {
        GF_FREE(conf->child_status);
        conf->child_status = NULL;
    }

    if (this->ctx->statedump_path) {
        GF_FREE(this->ctx->statedump_path);
        this->ctx->statedump_path = NULL;
    }

    if (conf->auth_modules) {
        gf_auth_fini(conf->auth_modules);
        dict_unref(conf->auth_modules);
    }

    if (conf->rpc) {
        (void)rpcsvc_destroy(conf->rpc);
        conf->rpc = NULL;
    }

    GF_FREE(conf);
    this->private = NULL;
}

int
server_init(xlator_t *this)
{
    int32_t ret = -1;
    server_conf_t *conf = NULL;
    char *transport_type = NULL;
    char *statedump_path = NULL;
    int total_transport = 0;

    GF_VALIDATE_OR_GOTO("init", this, out);

    if (this->children == NULL) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_SUBVOL_NULL, NULL);
        goto out;
    }

    if (this->parents != NULL) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_PARENT_VOL_ERROR, NULL);
        goto out;
    }

    conf = GF_CALLOC(1, sizeof(server_conf_t), gf_server_mt_server_conf_t);

    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    INIT_LIST_HEAD(&conf->xprt_list);
    pthread_mutex_init(&conf->mutex, NULL);

    LOCK_INIT(&conf->itable_lock);

    /* Set event threads to the configured default */
    GF_OPTION_INIT("event-threads", conf->event_threads, int32, out);
    ret = server_check_event_threads(this, conf, conf->event_threads);
    if (ret)
        goto out;

    ret = server_build_config(this, conf);
    if (ret)
        goto out;

    ret = dict_get_str_sizen(this->options, "config-directory",
                             &conf->conf_dir);
    if (ret)
        conf->conf_dir = CONFDIR;

    conf->child_status = GF_CALLOC(1, sizeof(struct _child_status),
                                   gf_server_mt_child_status);
    INIT_LIST_HEAD(&conf->child_status->status_list);

    GF_OPTION_INIT("statedump-path", statedump_path, path, out);
    if (statedump_path) {
        gf_path_strip_trailing_slashes(statedump_path);
        this->ctx->statedump_path = gf_strdup(statedump_path);
    } else {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_SET_STATEDUMP_PATH_ERROR,
                NULL);
        ret = -1;
        goto out;
    }

    /* Authentication modules */
    conf->auth_modules = dict_new();
    GF_VALIDATE_OR_GOTO(this->name, conf->auth_modules, out);

    dict_foreach(this->options, get_auth_types, conf->auth_modules);
    ret = validate_auth_options(this, this->options);
    if (ret == -1) {
        /* logging already done in validate_auth_options function. */
        goto out;
    }

    ret = gf_auth_init(this, conf->auth_modules);
    if (ret) {
        dict_unref(conf->auth_modules);
        conf->auth_modules = NULL;
        goto out;
    }

    ret = dict_get_str_boolean(this->options, "manage-gids", _gf_false);
    if (ret == -1)
        conf->server_manage_gids = _gf_false;
    else
        conf->server_manage_gids = ret;

    GF_OPTION_INIT("gid-timeout", conf->gid_cache_timeout, int32, out);
    if (gid_cache_init(&conf->gid_cache, conf->gid_cache_timeout) < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_INIT_GRP_CACHE_ERROR, NULL);
        goto out;
    }

    ret = dict_get_str_boolean(this->options, "strict-auth-accept", _gf_false);
    if (ret == -1)
        conf->strict_auth_enabled = _gf_false;
    else
        conf->strict_auth_enabled = ret;

    ret = dict_get_str_boolean(this->options, "dynamic-auth", _gf_true);
    if (ret == -1)
        conf->dync_auth = _gf_true;
    else
        conf->dync_auth = ret;

    /* RPC related */
    conf->rpc = rpcsvc_init(this, this->ctx, this->options, 0);
    if (conf->rpc == NULL) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_RPCSVC_CREATE_FAILED, NULL);
        ret = -1;
        goto out;
    }

    ret = rpcsvc_set_outstanding_rpc_limit(
        conf->rpc, this->options, RPCSVC_DEFAULT_OUTSTANDING_RPC_LIMIT);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_RPC_CONFIGURE_FAILED, NULL);
        goto out;
    }

    /*
     * This is the only place where we want secure_srvr to reflect
     * the data-plane setting.
     */
    this->ctx->secure_srvr = MGMT_SSL_COPY_IO;

    ret = dict_get_str_sizen(this->options, "transport-type", &transport_type);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_TRANSPORT_TYPE_NOT_SET,
                NULL);
        ret = -1;
        goto out;
    }
    total_transport = rpc_transport_count(transport_type);
    if (total_transport <= 0) {
        gf_smsg(this->name, GF_LOG_ERROR, 0,
                PS_MSG_GET_TOTAL_AVAIL_TRANSPORT_FAILED, NULL);
        ret = -1;
        goto out;
    }

    ret = dict_set_int32_sizen(this->options, "notify-poller-death", 1);

    ret = rpcsvc_create_listeners(conf->rpc, this->options, this->name);
    if (ret < 1) {
        gf_smsg(this->name, GF_LOG_WARNING, 0,
                PS_MSG_RPCSVC_LISTENER_CREATE_FAILED, NULL);
        if (ret != -EADDRINUSE)
            ret = -1;
        goto out;
    } else if (ret < total_transport) {
        gf_smsg(this->name, GF_LOG_ERROR, 0,
                PS_MSG_RPCSVC_LISTENER_CREATE_FAILED, "number=%d",
                "continuing with succeeded transport", (total_transport - ret),
                NULL);
    }

    ret = rpcsvc_register_notify(conf->rpc, server_rpc_notify, this);
    if (ret) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, PS_MSG_RPCSVC_NOTIFY, NULL);
        goto out;
    }

    glusterfs3_3_fop_prog.options = this->options;
    /* make sure we register the fop program at the head to optimize
     * lookup
     */
    ret = rpcsvc_program_register(conf->rpc, &glusterfs3_3_fop_prog, _gf_true);
    if (ret) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, PS_MSG_PGM_REG_FAILED, "name=%s",
                glusterfs3_3_fop_prog.progname, "prognum=%d",
                glusterfs3_3_fop_prog.prognum, "progver=%d",
                glusterfs3_3_fop_prog.progver, NULL);
        goto out;
    }

    glusterfs4_0_fop_prog.options = this->options;
    ret = rpcsvc_program_register(conf->rpc, &glusterfs4_0_fop_prog, _gf_true);
    if (ret) {
        gf_log(this->name, GF_LOG_WARNING,
               "registration of program (name:%s, prognum:%d, "
               "progver:%d) failed",
               glusterfs4_0_fop_prog.progname, glusterfs4_0_fop_prog.prognum,
               glusterfs4_0_fop_prog.progver);
        rpcsvc_program_unregister(conf->rpc, &glusterfs3_3_fop_prog);
        goto out;
    }

    gluster_handshake_prog.options = this->options;
    ret = rpcsvc_program_register(conf->rpc, &gluster_handshake_prog,
                                  _gf_false);
    if (ret) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, PS_MSG_PGM_REG_FAILED, "name=%s",
                gluster_handshake_prog.progname, "prognum=%d",
                gluster_handshake_prog.prognum, "progver=%d",
                gluster_handshake_prog.progver, NULL);
        rpcsvc_program_unregister(conf->rpc, &glusterfs3_3_fop_prog);
        rpcsvc_program_unregister(conf->rpc, &glusterfs4_0_fop_prog);
        goto out;
    }

#ifndef GF_DARWIN_HOST_OS
    {
        struct rlimit lim;

        lim.rlim_cur = 1048576;
        lim.rlim_max = 1048576;

        if (setrlimit(RLIMIT_NOFILE, &lim) == -1) {
            gf_smsg(this->name, GF_LOG_WARNING, errno, PS_MSG_ULIMIT_SET_FAILED,
                    "errno=%s", strerror(errno), NULL);
            lim.rlim_cur = 65536;
            lim.rlim_max = 65536;

            if (setrlimit(RLIMIT_NOFILE, &lim) == -1) {
                gf_smsg(this->name, GF_LOG_WARNING, errno, PS_MSG_FD_NOT_FOUND,
                        "errno=%s", strerror(errno), NULL);
            } else {
                gf_msg_trace(this->name, 0,
                             "max open fd set "
                             "to 64k");
            }
        }
    }
#endif
    if (!this->ctx->cmd_args.volfile_id) {
        /* In some use cases this is a valid case, but
           document this to be annoying log in that case */
        gf_smsg(this->name, GF_LOG_WARNING, EINVAL, PS_MSG_VOL_FILE_OPEN_FAILED,
                NULL);
        this->ctx->cmd_args.volfile_id = gf_strdup("gluster");
    }
    FIRST_CHILD(this)->volfile_id = gf_strdup(this->ctx->cmd_args.volfile_id);

    this->private = conf;
    ret = 0;

out:
    if (ret) {
        if (this != NULL) {
            this->fini(this);
        }
        server_cleanup(this, conf);
    }

    return ret;
}

void
server_fini(xlator_t *this)
{
#if 0
        server_conf_t *conf = NULL;

        conf = this->private;

        if (conf) {
                if (conf->rpc) {
                        /* TODO: memory leak here, have to free RPC */
                        /*
                          if (conf->rpc->conn) {
                          rpcsvc_conn_destroy (conf->rpc->conn);
                          }
                          rpcsvc_fini (conf->rpc);
                        */
                        ;
                }

                if (conf->auth_modules)
                        dict_unref (conf->auth_modules);

                GF_FREE (conf);
        }

        this->private = NULL;
#endif
    return;
}

int
server_process_event_upcall(xlator_t *this, void *data)
{
    int ret = -1;
    server_conf_t *conf = NULL;
    client_t *client = NULL;
    char *client_uid = NULL;
    struct gf_upcall *upcall_data = NULL;
    void *up_req = NULL;
    rpc_transport_t *xprt = NULL;
    enum gf_cbk_procnum cbk_procnum = GF_CBK_NULL;
    gfs3_cbk_cache_invalidation_req gf_c_req = {
        0,
    };
    gfs3_recall_lease_req gf_recall_lease = {
        {
            0,
        },
    };
    gfs4_inodelk_contention_req gf_inodelk_contention = {
        {0},
    };
    gfs4_entrylk_contention_req gf_entrylk_contention = {
        {0},
    };
    xdrproc_t xdrproc;

    GF_VALIDATE_OR_GOTO(this->name, data, out);

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    upcall_data = (struct gf_upcall *)data;
    client_uid = upcall_data->client_uid;
    /* client_uid could be NULL if the upcall was intended for a server's
     * child xlator (so no client_uid available) but it hasn't handled
     * the notification. For this reason we silently ignore any upcall
     * request with a NULL client_uid, but -1 will be returned.
     */
    if (client_uid == NULL) {
        gf_msg_debug(this->name, 0, "NULL client_uid for an upcall request");
        goto out;
    }

    switch (upcall_data->event_type) {
        case GF_UPCALL_CACHE_INVALIDATION:
            ret = gf_proto_cache_invalidation_from_upcall(this, &gf_c_req,
                                                          upcall_data);
            if (ret < 0)
                goto out;

            up_req = &gf_c_req;
            cbk_procnum = GF_CBK_CACHE_INVALIDATION;
            xdrproc = (xdrproc_t)xdr_gfs3_cbk_cache_invalidation_req;
            break;
        case GF_UPCALL_RECALL_LEASE:
            ret = gf_proto_recall_lease_from_upcall(this, &gf_recall_lease,
                                                    upcall_data);
            if (ret < 0)
                goto out;

            up_req = &gf_recall_lease;
            cbk_procnum = GF_CBK_RECALL_LEASE;
            xdrproc = (xdrproc_t)xdr_gfs3_recall_lease_req;
            break;
        case GF_UPCALL_INODELK_CONTENTION:
            ret = gf_proto_inodelk_contention_from_upcall(
                this, &gf_inodelk_contention, upcall_data);
            if (ret < 0)
                goto out;

            up_req = &gf_inodelk_contention;
            cbk_procnum = GF_CBK_INODELK_CONTENTION;
            xdrproc = (xdrproc_t)xdr_gfs4_inodelk_contention_req;
            break;
        case GF_UPCALL_ENTRYLK_CONTENTION:
            ret = gf_proto_entrylk_contention_from_upcall(
                this, &gf_entrylk_contention, upcall_data);
            if (ret < 0)
                goto out;

            up_req = &gf_entrylk_contention;
            cbk_procnum = GF_CBK_ENTRYLK_CONTENTION;
            xdrproc = (xdrproc_t)xdr_gfs4_entrylk_contention_req;
            break;
        default:
            gf_smsg(this->name, GF_LOG_WARNING, EINVAL,
                    PS_MSG_INVLAID_UPCALL_EVENT, "event-type=%d",
                    upcall_data->event_type, NULL);
            goto out;
    }

    pthread_mutex_lock(&conf->mutex);
    {
        list_for_each_entry(xprt, &conf->xprt_list, list)
        {
            client = xprt->xl_private;

            /* 'client' is not atomically added during xprt entry
             * addition to the list. */
            if (!client || strcmp(client->client_uid, client_uid))
                continue;

            ret = rpcsvc_request_submit(conf->rpc, xprt, &server_cbk_prog,
                                        cbk_procnum, up_req, this->ctx,
                                        xdrproc);
            if (ret < 0) {
                gf_msg_debug(this->name, 0,
                             "Failed to send "
                             "upcall to client:%s upcall "
                             "event:%d",
                             client_uid, upcall_data->event_type);
            }
            break;
        }
    }
    pthread_mutex_unlock(&conf->mutex);
    ret = 0;
out:
    GF_FREE((gf_c_req.xdata).xdata_val);
    GF_FREE((gf_recall_lease.xdata).xdata_val);
    GF_FREE((gf_inodelk_contention.xdata).xdata_val);
    GF_FREE((gf_entrylk_contention.xdata).xdata_val);

    return ret;
}

int
server_process_child_event(xlator_t *this, int32_t event, void *data,
                           enum gf_cbk_procnum cbk_procnum)
{
    int ret = -1;
    server_conf_t *conf = NULL;
    rpc_transport_t *xprt = NULL;
    xlator_t *victim = NULL;
    struct _child_status *tmp = NULL;

    GF_VALIDATE_OR_GOTO(this->name, data, out);

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    victim = data;
    pthread_mutex_lock(&conf->mutex);
    {
        if (cbk_procnum == GF_CBK_CHILD_UP) {
            list_for_each_entry(tmp, &conf->child_status->status_list,
                                status_list)
            {
                if (tmp->name == NULL)
                    break;
                if (strcmp(tmp->name, victim->name) == 0)
                    break;
            }
            if (tmp->name) {
                tmp->child_up = _gf_true;
            } else {
                tmp = GF_CALLOC(1, sizeof(struct _child_status),
                                gf_server_mt_child_status);
                INIT_LIST_HEAD(&tmp->status_list);
                tmp->name = gf_strdup(victim->name);
                tmp->child_up = _gf_true;
                memcpy(tmp->volume_id, victim->graph->volume_id,
                       GF_UUID_BUF_SIZE);
                list_add_tail(&tmp->status_list,
                              &conf->child_status->status_list);
            }
        }

        if (cbk_procnum == GF_CBK_CHILD_DOWN) {
            list_for_each_entry(tmp, &conf->child_status->status_list,
                                status_list)
            {
                if (strcmp(tmp->name, victim->name) == 0) {
                    tmp->child_up = _gf_false;
                    break;
                }
            }

            if (!tmp->name)
                gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_CHILD_STATUS_FAILED,
                        "name=%s", victim->name, NULL);
        }
        list_for_each_entry(xprt, &conf->xprt_list, list)
        {
            if (!xprt->xl_private) {
                continue;
            }
            if (xprt->xl_private->bound_xl == data) {
                rpcsvc_callback_submit(conf->rpc, xprt, &server_cbk_prog,
                                       cbk_procnum, NULL, 0, NULL);
            }
        }
    }
    pthread_mutex_unlock(&conf->mutex);
    ret = 0;
out:
    return ret;
}

int
server_notify(xlator_t *this, int32_t event, void *data, ...)
{
    int ret = -1;
    server_conf_t *conf = NULL;
    rpc_transport_t *xprt = NULL;
    rpc_transport_t *xp_next = NULL;
    xlator_t *victim = NULL;
    xlator_t *top = NULL;
    xlator_t *travxl = NULL;
    xlator_list_t **trav_p = NULL;
    struct _child_status *tmp = NULL;
    gf_boolean_t victim_found = _gf_false;
    glusterfs_ctx_t *ctx = NULL;
    gf_boolean_t xprt_found = _gf_false;
    uint64_t totxprt = 0;
    uint64_t totdisconnect = 0;

    GF_VALIDATE_OR_GOTO(THIS->name, this, out);
    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);
    victim = data;
    ctx = THIS->ctx;

    switch (event) {
        case GF_EVENT_UPCALL: {
            GF_VALIDATE_OR_GOTO(this->name, data, out);

            ret = server_process_event_upcall(this, data);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, 0,
                        PS_MSG_SERVER_EVENT_UPCALL_FAILED, NULL);
                goto out;
            }
            break;
        }

        case GF_EVENT_PARENT_UP: {
            conf = this->private;

            conf->parent_up = _gf_true;

            default_notify(this, event, data);
            break;
        }

        case GF_EVENT_CHILD_UP: {
            ret = server_process_child_event(this, event, data,
                                             GF_CBK_CHILD_UP);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, 0,
                        PS_MSG_SERVER_CHILD_EVENT_FAILED, NULL);
                goto out;
            }
            default_notify(this, event, data);
            break;
        }

        case GF_EVENT_CHILD_DOWN: {
            if (victim->cleanup_starting) {
                victim->notify_down = 1;
                gf_log(this->name, GF_LOG_INFO,
                       "Getting CHILD_DOWN event for brick %s", victim->name);
            }

            ret = server_process_child_event(this, event, data,
                                             GF_CBK_CHILD_DOWN);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, 0,
                        PS_MSG_SERVER_CHILD_EVENT_FAILED, NULL);
                goto out;
            }
            default_notify(this, event, data);
            break;
        }

        case GF_EVENT_CLEANUP:
            conf = this->private;
            pthread_mutex_lock(&conf->mutex);
            /* Calculate total no. of xprt available in list for this
               brick xlator
             */
            list_for_each_entry_safe(xprt, xp_next, &conf->xprt_list, list)
            {
                if (!xprt->xl_private) {
                    continue;
                }

                if (GF_ATOMIC_GET(xprt->disconnect_progress))
                    continue;

                if (xprt->xl_private->bound_xl == data) {
                    totxprt++;
                }
            }

            list_for_each_entry(tmp, &conf->child_status->status_list,
                                status_list)
            {
                if (strcmp(tmp->name, victim->name) == 0) {
                    tmp->child_up = _gf_false;
                    GF_ATOMIC_INIT(victim->xprtrefcnt, totxprt);
                    break;
                }
            }

            /*
             * Disconnecting will (usually) drop the last ref, which will
             * cause the transport to be unlinked and freed while we're
             * still traversing, which will cause us to crash unless we use
             * list_for_each_entry_safe.
             */
            list_for_each_entry_safe(xprt, xp_next, &conf->xprt_list, list)
            {
                if (!xprt->xl_private) {
                    continue;
                }

                if (GF_ATOMIC_GET(xprt->disconnect_progress))
                    continue;

                if (xprt->xl_private->bound_xl == data) {
                    gf_log(this->name, GF_LOG_INFO, "disconnecting %s",
                           xprt->peerinfo.identifier);
                    xprt_found = _gf_true;
                    totdisconnect++;
                    rpc_transport_disconnect(xprt, _gf_false);
                }
            }

            if (totxprt > totdisconnect)
                GF_ATOMIC_SUB(victim->xprtrefcnt, (totxprt - totdisconnect));

            pthread_mutex_unlock(&conf->mutex);
            if (this->ctx->active) {
                top = this->ctx->active->first;
                LOCK(&ctx->volfile_lock);
                for (trav_p = &top->children; *trav_p;
                     trav_p = &(*trav_p)->next) {
                    travxl = (*trav_p)->xlator;
                    if (!travxl->call_cleanup &&
                        strcmp(travxl->name, victim->name) == 0) {
                        victim_found = _gf_true;
                        break;
                    }
                }
                if (victim_found)
                    glusterfs_delete_volfile_checksum(ctx, victim->volfile_id);
                UNLOCK(&ctx->volfile_lock);

                rpc_clnt_mgmt_pmap_signout(ctx, victim->name);

                if (!xprt_found && victim_found) {
                    server_call_xlator_mem_cleanup(this, victim->name);
                }
            }
            break;

        default:
            default_notify(this, event, data);
            break;
    }
    ret = 0;
out:
    return ret;
}

struct xlator_fops server_fops;

struct xlator_cbks server_cbks = {
    .client_destroy = client_destroy_cbk,
};

struct xlator_dumpops server_dumpops = {
    .priv = server_priv,
    .fd = gf_client_dump_fdtables,
    .inode = gf_client_dump_inodes,
    .priv_to_dict = server_priv_to_dict,
    .fd_to_dict = gf_client_dump_fdtables_to_dict,
    .inode_to_dict = gf_client_dump_inodes_to_dict,
};

struct volume_options server_options[] = {
    {.key = {"transport-type"},
     .value = {"rpc", "rpc-over-rdma", "tcp", "socket", "ib-verbs", "unix",
               "ib-sdp", "tcp/server", "ib-verbs/server", "rdma",
               "rdma*([ \t]),*([ \t])socket", "rdma*([ \t]),*([ \t])tcp",
               "tcp*([ \t]),*([ \t])rdma", "socket*([ \t]),*([ \t])rdma"},
     .type = GF_OPTION_TYPE_STR,
     .default_value = "{{ volume.transport }}"},
    {
        .key = {"transport.listen-backlog"},
        .type = GF_OPTION_TYPE_INT,
        .default_value = "10",
    },
    {
        .key = {"volume-filename.*"},
        .type = GF_OPTION_TYPE_PATH,
    },
    {
        .key = {"transport.tcp-user-timeout"},
        .type = GF_OPTION_TYPE_TIME,
        .min = 0,
        .max = 1013,
        .default_value = TOSTRING(GF_NETWORK_TIMEOUT),
    },
    {
        .key = {"transport.*"},
        .type = GF_OPTION_TYPE_ANY,
    },
    {.key = {"inode-lru-limit"},
     .type = GF_OPTION_TYPE_INT,
     .min = 0,
     .max = 1048576,
     .default_value = "16384",
     .description = "Specifies the limit on the number of inodes "
                    "in the lru list of the inode cache.",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"trace"}, .type = GF_OPTION_TYPE_BOOL},
    {
        .key = {"config-directory", "conf-dir"},
        .type = GF_OPTION_TYPE_PATH,
    },
    {.key = {"rpc-auth-allow-insecure", "allow-insecure"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "on",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"root-squash"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     .description = "Map requests from uid/gid 0 to the anonymous "
                    "uid/gid. Note that this does not apply to any other "
                    "uids or gids that might be equally sensitive, such "
                    "as user bin or group staff.",
     .op_version = {2},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"all-squash"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     .description = "Map requests from any uid/gid to the anonymous "
                    "uid/gid. Note that this does not apply to any other "
                    "uids or gids that might be equally sensitive, such "
                    "as user bin or group staff.",
     .op_version = {GD_OP_VERSION_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"anonuid"},
     .type = GF_OPTION_TYPE_INT,
     .default_value = "65534", /* RPC_NOBODY_UID */
     .min = 0,
     .max = (uint32_t)-1,
     .description = "value of the uid used for the anonymous "
                    "user/nfsnobody when root-squash/all-squash is enabled.",
     .op_version = {3},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"anongid"},
     .type = GF_OPTION_TYPE_INT,
     .default_value = "65534", /* RPC_NOBODY_GID */
     .min = 0,
     .max = (uint32_t)-1,
     .description = "value of the gid used for the anonymous "
                    "user/nfsnobody when root-squash/all-squash is enabled.",
     .op_version = {3},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"statedump-path"},
     .type = GF_OPTION_TYPE_PATH,
     .default_value = DEFAULT_VAR_RUN_DIRECTORY,
     .description = "Specifies directory in which gluster should save its"
                    " statedumps.",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"tcp-window-size"},
     .type = GF_OPTION_TYPE_SIZET,
     .min = GF_MIN_SOCKET_WINDOW_SIZE,
     .max = GF_MAX_SOCKET_WINDOW_SIZE,
     .description = "Specifies the window size for tcp socket.",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE},

    /*  The following two options are defined in addr.c, redifined here *
     * for the sake of validation during volume set from cli            */

    {.key = {"auth.addr.*.allow", "auth.allow"},
     .setkey = "auth.addr.{{ brick.path }}.allow",
     .default_value = "*",
     .type = GF_OPTION_TYPE_INTERNET_ADDRESS_LIST,
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .description = "Allow a comma separated list of addresses and/or "
                    "hostnames to connect to the server. Option "
                    "auth.reject overrides this option. By default, all "
                    "connections are allowed."},
    {.key = {"auth.addr.*.reject", "auth.reject"},
     .setkey = "auth.addr.{{ brick.path }}.reject",
     .type = GF_OPTION_TYPE_INTERNET_ADDRESS_LIST,
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .description = "Reject a comma separated list of addresses and/or "
                    "hostnames to connect to the server. This option "
                    "overrides the auth.allow option. By default, all"
                    " connections are allowed."},
    {.key = {"ssl-allow"},
     .setkey = "auth.login.{{ brick.path }}.ssl-allow",
     .default_value = "*",
     .type = GF_OPTION_TYPE_INTERNET_ADDRESS_LIST,
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .description = "Allow a comma separated list of common names (CN) of"
                    "the clients that are allowed to access the server."
                    "By default, all TLS authenticated clients are"
                    "allowed to access the server."},
    /* This is not a valid path w.r.t daemons, hence it's string */
    {.key = {"auth-path"},
     .type = GF_OPTION_TYPE_STR,
     .default_value = "{{ brick.path }}"},
    {.key = {"rpc.outstanding-rpc-limit"},
     .type = GF_OPTION_TYPE_INT,
     .min = RPCSVC_MIN_OUTSTANDING_RPC_LIMIT,
     .max = RPCSVC_MAX_OUTSTANDING_RPC_LIMIT,
     .default_value = TOSTRING(RPCSVC_DEFAULT_OUTSTANDING_RPC_LIMIT),
     .description = "Parameter to throttle the number of incoming RPC "
                    "requests from a client. 0 means no limit (can "
                    "potentially run out of memory)",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC | OPT_FLAG_GLOBAL},
    {.key = {"manage-gids"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     .description = "Resolve groups on the server-side.",
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"gid-timeout"},
     .type = GF_OPTION_TYPE_INT,
     .default_value = "300",
     .description = "Timeout in seconds for the cached groups to expire.",
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"event-threads"},
     .type = GF_OPTION_TYPE_INT,
     .min = 1,
     .max = 1024,
     .default_value = "2",
     .description = "Specifies the number of event threads to execute "
                    "in parallel. Larger values would help process"
                    " responses faster, depending on available processing"
                    " power.",
     .op_version = {GD_OP_VERSION_3_7_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"dynamic-auth"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "on",
     .description = "When 'on' perform dynamic authentication of volume "
                    "options in order to allow/terminate client "
                    "transport connection immediately in response to "
                    "*.allow | *.reject volume set options.",
     .op_version = {GD_OP_VERSION_3_7_5},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"strict-auth-accept"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     .description = "strict-auth-accept reject connection with out"
                    "a valid username and password."},
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = server_init,
    .fini = server_fini,
    .notify = server_notify,
    .reconfigure = server_reconfigure,
    .mem_acct_init = server_mem_acct_init,
    .dump_metrics = server_dump_metrics,
    .op_version = {1}, /* Present from the initial version */
    .dumpops = &server_dumpops,
    .fops = &server_fops,
    .cbks = &server_cbks,
    .options = server_options,
    .identifier = "server",
    .category = GF_MAINTAINED,
};
