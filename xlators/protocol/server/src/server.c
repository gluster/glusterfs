/*
  Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <sys/time.h>
#include <sys/resource.h>


#include "server.h"
#include "server-helpers.h"
#include "glusterfs3-xdr.h"
#include "call-stub.h"
#include "statedump.h"
#include "defaults.h"
#include "authenticate.h"

void
grace_time_handler (void *data)
{
        client_t      *client    = NULL;
        xlator_t      *this      = NULL;
        gf_timer_t    *timer     = NULL;
        server_ctx_t  *serv_ctx  = NULL;
        gf_boolean_t   cancelled = _gf_false;
        gf_boolean_t   detached  = _gf_false;

        client = data;
        this = client->this;

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);

        gf_log (this->name, GF_LOG_INFO, "grace timer expired for %s",
                client->client_uid);

        serv_ctx = server_ctx_get (client, this);

        if (serv_ctx == NULL) {
                gf_log (this->name, GF_LOG_INFO, "server_ctx_get() failed");
                goto out;
        }

        LOCK (&serv_ctx->fdtable_lock);
        {
                if (serv_ctx->grace_timer) {
                        timer = serv_ctx->grace_timer;
                        serv_ctx->grace_timer = NULL;
                }
        }
        UNLOCK (&serv_ctx->fdtable_lock);
        if (timer) {
                gf_timer_call_cancel (this->ctx, timer);
                cancelled = _gf_true;
        }
        if (cancelled) {

                /*
                 * client must not be destroyed in gf_client_put(),
                 * so take a ref.
                 */
                gf_client_ref (client);
                gf_client_put (client, &detached);
                if (detached)//reconnection did not happen :-(
                        server_connection_cleanup (this, client,
                                                   INTERNAL_LOCKS | POSIX_LOCKS);
                gf_client_unref (client);
        }
out:
        return;
}

struct iobuf *
gfs_serialize_reply (rpcsvc_request_t *req, void *arg, struct iovec *outmsg,
                     xdrproc_t xdrproc)
{
        struct iobuf *iob      = NULL;
        ssize_t       retlen   = 0;
        ssize_t       xdr_size = 0;

        GF_VALIDATE_OR_GOTO ("server", req, ret);

        /* First, get the io buffer into which the reply in arg will
         * be serialized.
         */
        if (arg && xdrproc) {
                xdr_size = xdr_sizeof (xdrproc, arg);
                iob = iobuf_get2 (req->svc->ctx->iobuf_pool, xdr_size);
                if (!iob) {
                        gf_log_callingfn (THIS->name, GF_LOG_ERROR,
                                          "Failed to get iobuf");
                        goto ret;
                };

                iobuf_to_iovec (iob, outmsg);
                /* Use the given serializer to translate the give C structure in arg
                 * to XDR format which will be written into the buffer in outmsg.
                 */
                /* retlen is used to received the error since size_t is unsigned and we
                 * need -1 for error notification during encoding.
                 */

                retlen = xdr_serialize_generic (*outmsg, arg, xdrproc);
                if (retlen == -1) {
                        /* Failed to Encode 'GlusterFS' msg in RPC is not exactly
                           failure of RPC return values.. client should get
                           notified about this, so there are no missing frames */
                        gf_log_callingfn ("", GF_LOG_ERROR, "Failed to encode message");
                        req->rpc_err = GARBAGE_ARGS;
                        retlen = 0;
                }
        }
        outmsg->iov_len = retlen;
ret:
        if (retlen == -1) {
                iobuf_unref (iob);
                iob = NULL;
        }

        return iob;
}



int
server_submit_reply (call_frame_t *frame, rpcsvc_request_t *req, void *arg,
                     struct iovec *payload, int payloadcount,
                     struct iobref *iobref, xdrproc_t xdrproc)
{
        struct iobuf           *iob        = NULL;
        int                     ret        = -1;
        struct iovec            rsp        = {0,};
        server_state_t         *state      = NULL;
        char                    new_iobref = 0;
        client_t               *client     = NULL;
        gf_boolean_t            lk_heal    = _gf_false;

        GF_VALIDATE_OR_GOTO ("server", req, ret);

        if (frame) {
                state = CALL_STATE (frame);
                frame->local = NULL;
                client = frame->root->client;
        }

        if (client)
                lk_heal = ((server_conf_t *) client->this->private)->lk_heal;

        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref) {
                        goto ret;
                }

                new_iobref = 1;
        }

        iob = gfs_serialize_reply (req, arg, &rsp, xdrproc);
        if (!iob) {
                gf_log ("", GF_LOG_ERROR, "Failed to serialize reply");
                goto ret;
        }

        iobref_add (iobref, iob);

        /* Then, submit the message for transmission. */
        ret = rpcsvc_submit_generic (req, &rsp, 1, payload, payloadcount,
                                     iobref);

        /* TODO: this is demo purpose only */
        /* ret = rpcsvc_callback_submit (req->svc, req->trans, req->prog,
           GF_CBK_NULL, &rsp, 1);
        */
        /* Now that we've done our job of handing the message to the RPC layer
         * we can safely unref the iob in the hope that RPC layer must have
         * ref'ed the iob on receiving into the txlist.
         */
        iobuf_unref (iob);
        if (ret == -1) {
                gf_log_callingfn ("", GF_LOG_ERROR, "Reply submission failed");
                if (frame && client && !lk_heal) {
                        server_connection_cleanup (frame->this, client,
                                                  INTERNAL_LOCKS | POSIX_LOCKS);
                } else {
                        gf_log_callingfn ("", GF_LOG_ERROR,
                                          "Reply submission failed");
                        /* TODO: Failure of open(dir), create, inodelk, entrylk
                           or lk fops send failure must be handled specially. */
                }
                goto ret;
        }

        ret = 0;
ret:
        if (state) {
                free_state (state);
        }

        if (frame) {
                gf_client_unref (client);
                STACK_DESTROY (frame->root);
        }

        if (new_iobref) {
                iobref_unref (iobref);
        }

        return ret;
}


int
server_priv_to_dict (xlator_t *this, dict_t *dict)
{
        server_conf_t   *conf = NULL;
        rpc_transport_t *xprt = NULL;
        peer_info_t     *peerinfo = NULL;
        char            key[32] = {0,};
        int             count = 0;
        int             ret = -1;

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (THIS->name, dict, out);

        conf = this->private;
        if (!conf)
                return 0;
        //TODO: Dump only specific info to dict

        pthread_mutex_lock (&conf->mutex);
        {
                list_for_each_entry (xprt, &conf->xprt_list, list) {
                        peerinfo = &xprt->peerinfo;
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "client%d.hostname",
                                  count);
                        ret = dict_set_str (dict, key, peerinfo->identifier);
                        if (ret)
                                goto unlock;

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "client%d.bytesread",
                                  count);
                        ret = dict_set_uint64 (dict, key,
                                               xprt->total_bytes_read);
                        if (ret)
                                goto unlock;

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "client%d.byteswrite",
                                  count);
                        ret = dict_set_uint64 (dict, key,
                                               xprt->total_bytes_write);
                        if (ret)
                                goto unlock;

                        count++;
                }
        }
unlock:
        pthread_mutex_unlock (&conf->mutex);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "clientcount", count);

out:
        return ret;
}

int
server_priv (xlator_t *this)
{
        server_conf_t    *conf = NULL;
        rpc_transport_t  *xprt = NULL;
        char              key[GF_DUMP_MAX_BUF_LEN] = {0,};
        uint64_t          total_read = 0;
        uint64_t          total_write = 0;
        int32_t           ret  = -1;

        GF_VALIDATE_OR_GOTO ("server", this, out);

        conf = this->private;
        if (!conf)
                return 0;

        gf_proc_dump_build_key (key, "xlator.protocol.server", "priv");
        gf_proc_dump_add_section (key);

        ret = pthread_mutex_trylock (&conf->mutex);
        if (ret != 0)
                goto out;
        {
                list_for_each_entry (xprt, &conf->xprt_list, list) {
                        total_read  += xprt->total_bytes_read;
                        total_write += xprt->total_bytes_write;
                }
        }
        pthread_mutex_unlock (&conf->mutex);

        gf_proc_dump_build_key(key, "server", "total-bytes-read");
        gf_proc_dump_write(key, "%"PRIu64, total_read);

        gf_proc_dump_build_key(key, "server", "total-bytes-write");
        gf_proc_dump_write(key, "%"PRIu64, total_write);

        ret = 0;
out:
        if (ret)
                gf_proc_dump_write ("Unable to print priv",
                                    "(Lock acquisition failed) %s",
                                    this?this->name:"server");

        return ret;
}


static int
get_auth_types (dict_t *this, char *key, data_t *value, void *data)
{
        dict_t   *auth_dict = NULL;
        char     *saveptr = NULL;
        char     *tmp = NULL;
        char     *key_cpy = NULL;
        int32_t   ret = -1;

        GF_VALIDATE_OR_GOTO ("server", this, out);
        GF_VALIDATE_OR_GOTO ("server", key, out);
        GF_VALIDATE_OR_GOTO ("server", data, out);

        auth_dict = data;
        key_cpy = gf_strdup (key);
        GF_VALIDATE_OR_GOTO("server", key_cpy, out);

        tmp = strtok_r (key_cpy, ".", &saveptr);
        ret = strcmp (tmp, "auth");
        if (ret == 0) {
                tmp = strtok_r (NULL, ".", &saveptr);
                if (strcmp (tmp, "ip") == 0) {
                        /* TODO: backward compatibility, remove when
                           newer versions are available */
                        tmp = "addr";
                        gf_log ("server", GF_LOG_WARNING,
                                "assuming 'auth.ip' to be 'auth.addr'");
                }
                ret = dict_set_dynptr (auth_dict, tmp, NULL, 0);
                if (ret < 0) {
                        gf_log ("server", GF_LOG_DEBUG,
                                "failed to dict_set_dynptr");
                }
        }

        GF_FREE (key_cpy);
out:
        return 0;
}

int
_check_for_auth_option (dict_t *d, char *k, data_t *v,
                        void *tmp)
{
        int       ret           = 0;
        xlator_t *xl            = NULL;
        char     *tail          = NULL;
        char     *tmp_addr_list = NULL;
        char     *addr          = NULL;
        char     *tmp_str       = NULL;

        xl = tmp;

        tail = strtail (k, "auth.");
        if (!tail)
                goto out;

        /* fast fwd thru module type */
        tail = strchr (tail, '.');
        if (!tail)
                goto out;
        tail++;

        tail = strtail (tail, xl->name);
        if (!tail)
                goto out;

        if (*tail == '.') {
                /* when we are here, the key is checked for
                 * valid auth.allow.<xlator>
                 * Now we verify the ip address
                 */
                if (!strcmp (v->data, "*")) {
                        ret = 0;
                        goto out;
                }

                tmp_addr_list = gf_strdup (v->data);
                addr = strtok_r (tmp_addr_list, ",", &tmp_str);
                if (!addr)
                        addr = v->data;

                while (addr) {
                        if (valid_internet_address (addr, _gf_true)) {
                                ret = 0;
                        } else {
                                ret = -1;
                                gf_log (xl->name, GF_LOG_ERROR,
                                        "internet address '%s'"
                                        " does not conform to"
                                        " standards.", addr);
                                goto out;
                        }
                        if (tmp_str)
                                addr = strtok_r (NULL, ",", &tmp_str);
                        else
                                addr = NULL;
                }

                GF_FREE (tmp_addr_list);
                tmp_addr_list = NULL;
        }
out:
        return ret;
}

int
validate_auth_options (xlator_t *this, dict_t *dict)
{
        int            error = -1;
        xlator_list_t *trav = NULL;

        GF_VALIDATE_OR_GOTO ("server", this, out);
        GF_VALIDATE_OR_GOTO ("server", dict, out);

        trav = this->children;
        while (trav) {
                error = dict_foreach (dict, _check_for_auth_option,
                                      trav->xlator);

                if (-1 == error) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "volume '%s' defined as subvolume, but no "
                                "authentication defined for the same",
                                trav->xlator->name);
                        break;
                }
                trav = trav->next;
        }

out:
        return error;
}


int
server_rpc_notify (rpcsvc_t *rpc, void *xl, rpcsvc_event_t event,
                   void *data)
{
        gf_boolean_t         detached   = _gf_false;
        xlator_t            *this       = NULL;
        rpc_transport_t     *trans      = NULL;
        server_conf_t       *conf       = NULL;
        client_t            *client     = NULL;
        server_ctx_t        *serv_ctx   = NULL;

        if (!xl || !data) {
                gf_log_callingfn ("server", GF_LOG_WARNING,
                                  "Calling rpc_notify without initializing");
                goto out;
        }

        this = xl;
        trans = data;
        conf = this->private;

        switch (event) {
        case RPCSVC_EVENT_ACCEPT:
        {
                /* Have a structure per new connection */
                /* TODO: Should we create anything here at all ? * /
                   client->conn = create_server_conn_state (this, trans);
                   if (!client->conn)
                   goto out;

                   trans->protocol_private = client->conn;
                */
                INIT_LIST_HEAD (&trans->list);

                pthread_mutex_lock (&conf->mutex);
                {
                        list_add_tail (&trans->list, &conf->xprt_list);
                }
                pthread_mutex_unlock (&conf->mutex);

                break;
        }
        case RPCSVC_EVENT_DISCONNECT:
                /* transport has to be removed from the list upon disconnect
                 * irrespective of whether lock self heal is off or on, since
                 * new transport will be created upon reconnect.
                 */
                pthread_mutex_lock (&conf->mutex);
                {
                        list_del_init (&trans->list);
                }
                pthread_mutex_unlock (&conf->mutex);

                client = trans->xl_private;
                if (!client)
                        break;

                gf_log (this->name, GF_LOG_INFO, "disconnecting connection"
                        "from %s", client->client_uid);

                /* If lock self heal is off, then destroy the
                   conn object, else register a grace timer event */
                if (!conf->lk_heal) {
                        gf_client_ref (client);
                        gf_client_put (client, &detached);
                        if (detached)
                                server_connection_cleanup (this, client,
                                                           INTERNAL_LOCKS | POSIX_LOCKS);
                        gf_client_unref (client);
                        break;
                }
                trans->xl_private = NULL;
                server_connection_cleanup (this, client, INTERNAL_LOCKS);

                serv_ctx = server_ctx_get (client, this);

                if (serv_ctx == NULL) {
                        gf_log (this->name, GF_LOG_INFO,
                                "server_ctx_get() failed");
                        goto out;
                }

                LOCK (&serv_ctx->fdtable_lock);
                {
                        if (!serv_ctx->grace_timer) {

                                gf_log (this->name, GF_LOG_INFO,
                                        "starting a grace timer for %s",
                                        client->client_uid);

                                serv_ctx->grace_timer =
                                        gf_timer_call_after (this->ctx,
                                                             conf->grace_ts,
                                                             grace_time_handler,
                                                             client);
                        }
                }
                UNLOCK (&serv_ctx->fdtable_lock);
                break;
        case RPCSVC_EVENT_TRANSPORT_DESTROY:
                /*- conn obj has been disassociated from trans on first
                 *  disconnect.
                 *  conn cleanup and destruction is handed over to
                 *  grace_time_handler or the subsequent handler that 'owns'
                 *  the conn. Nothing left to be done here. */
                break;
        default:
                break;
        }

out:
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        GF_VALIDATE_OR_GOTO ("server", this, out);

        ret = xlator_mem_acct_init (this, gf_server_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        "failed");
                return ret;
        }
out:
        return ret;
}


static int
_delete_auth_opt (dict_t *this, char *key, data_t *value, void *data)
{
        char *auth_option_pattern[] = { "auth.addr.*.allow",
                                        "auth.addr.*.reject",
                                        NULL};
        int i = 0;

        for (i = 0; auth_option_pattern[i]; i++) {
                if (fnmatch (auth_option_pattern[i], key, 0) == 0) {
                        dict_del (this, key);
                        break;
                }
        }

        return 0;
}


static int
_copy_auth_opt (dict_t *unused, char *key, data_t *value, void *xl_dict)
{
        char *auth_option_pattern[] = { "auth.addr.*.allow",
                                        "auth.addr.*.reject",
                                        NULL};
        int i = 0;

        for (i = 0; auth_option_pattern [i]; i++) {
                if (fnmatch (auth_option_pattern[i], key, 0) == 0) {
                        dict_set ((dict_t *)xl_dict, key, value);
                        break;
                }
        }

        return 0;
}


int
server_init_grace_timer (xlator_t *this, dict_t *options,
                         server_conf_t *conf)
{
        int32_t   ret            = -1;
        int32_t   grace_timeout  = -1;
        char     *lk_heal        = NULL;

        GF_VALIDATE_OR_GOTO ("server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, options, out);
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        conf->lk_heal = _gf_false;

        ret = dict_get_str (options, "lk-heal", &lk_heal);
        if (!ret)
                gf_string2boolean (lk_heal, &conf->lk_heal);

        gf_log (this->name, GF_LOG_DEBUG, "lk-heal = %s",
                (conf->lk_heal) ? "on" : "off");

        ret = dict_get_int32 (options, "grace-timeout", &grace_timeout);
        if (!ret)
                conf->grace_ts.tv_sec = grace_timeout;
        else
                conf->grace_ts.tv_sec = 10;

        gf_log (this->name, GF_LOG_DEBUG, "Server grace timeout "
                "value = %"PRIu64, conf->grace_ts.tv_sec);

        conf->grace_ts.tv_nsec  = 0;

        ret = 0;
out:
        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{

        server_conf_t            *conf =NULL;
        rpcsvc_t                 *rpc_conf;
        rpcsvc_listener_t        *listeners;
        int                       inode_lru_limit;
        gf_boolean_t              trace;
        data_t                   *data;
        int                       ret = 0;
        char                     *statedump_path = NULL;
        conf = this->private;

        if (!conf) {
                gf_log_callingfn (this->name, GF_LOG_DEBUG, "conf == null!!!");
                goto out;
        }
        if (dict_get_int32 ( options, "inode-lru-limit", &inode_lru_limit) == 0){
                conf->inode_lru_limit = inode_lru_limit;
                gf_log (this->name, GF_LOG_TRACE, "Reconfigured inode-lru-limit"
                        " to %d", conf->inode_lru_limit);
        }

        data = dict_get (options, "trace");
        if (data) {
                ret = gf_string2boolean (data->data, &trace);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "'trace' takes on only boolean values. "
                                "Neglecting option");
                        ret = -1;
                        goto out;
                }
                conf->trace = trace;
                gf_log (this->name, GF_LOG_TRACE, "Reconfigured trace"
                        " to %d", conf->trace);

        }

        GF_OPTION_RECONF ("statedump-path", statedump_path,
                          options, path, out);
        if (!statedump_path) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error while reconfiguring statedump path");
                ret = -1;
                goto out;
        }
        gf_path_strip_trailing_slashes (statedump_path);
        GF_FREE (this->ctx->statedump_path);
        this->ctx->statedump_path = gf_strdup (statedump_path);

        if (!conf->auth_modules)
                conf->auth_modules = dict_new ();

        dict_foreach (options, get_auth_types, conf->auth_modules);
        ret = validate_auth_options (this, options);
        if (ret == -1) {
                /* logging already done in validate_auth_options function. */
                goto out;
        }
        dict_foreach (this->options, _delete_auth_opt, this->options);
        dict_foreach (options, _copy_auth_opt, this->options);

        ret = gf_auth_init (this, conf->auth_modules);
        if (ret) {
                dict_unref (conf->auth_modules);
                goto out;
        }

        rpc_conf = conf->rpc;
        if (!rpc_conf) {
                gf_log (this->name, GF_LOG_ERROR, "No rpc_conf !!!!");
                goto out;
        }

        (void) rpcsvc_set_allow_insecure (rpc_conf, options);
        (void) rpcsvc_set_root_squash (rpc_conf, options);

        ret = rpcsvc_set_outstanding_rpc_limit (rpc_conf, options,
                                         RPCSVC_DEFAULT_OUTSTANDING_RPC_LIMIT);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to reconfigure outstanding-rpc-limit");
                goto out;
        }

        list_for_each_entry (listeners, &(rpc_conf->listeners), list) {
                if (listeners->trans != NULL) {
                        if (listeners->trans->reconfigure )
                                listeners->trans->reconfigure (listeners->trans, options);
                        else
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Reconfigure not found for transport" );
                }
        }
        ret = server_init_grace_timer (this, options, conf);

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

static int32_t
client_destroy_cbk (xlator_t *this, client_t *client)
{
        void         *tmp = NULL;
        server_ctx_t *ctx = NULL;

        client_ctx_del (client, this, &tmp);

        ctx = tmp;

        if (ctx == NULL)
                return 0;

        gf_fd_fdtable_destroy (ctx->fdtable);
        LOCK_DESTROY (&ctx->fdtable_lock);
        GF_FREE (ctx);

        return 0;
}

int
init (xlator_t *this)
{
        int32_t            ret      = -1;
        server_conf_t     *conf     = NULL;
        rpcsvc_listener_t *listener = NULL;
        char              *statedump_path = NULL;
        GF_VALIDATE_OR_GOTO ("init", this, out);

        if (this->children == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "protocol/server should have subvolume");
                goto out;
        }

        if (this->parents != NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "protocol/server should not have parent volumes");
                goto out;
        }

        conf = GF_CALLOC (1, sizeof (server_conf_t),
                          gf_server_mt_server_conf_t);

        GF_VALIDATE_OR_GOTO(this->name, conf, out);

        INIT_LIST_HEAD (&conf->xprt_list);
        pthread_mutex_init (&conf->mutex, NULL);

        ret = server_init_grace_timer (this, this->options, conf);
        if (ret)
                goto out;

        ret = server_build_config (this, conf);
        if (ret)
                goto out;

        ret = dict_get_str (this->options, "config-directory", &conf->conf_dir);
        if (ret)
                conf->conf_dir = CONFDIR;

        /*ret = dict_get_str (this->options, "statedump-path", &statedump_path);
        if (!ret) {
                gf_path_strip_trailing_slashes (statedump_path);
                this->ctx->statedump_path = statedump_path;
        }*/
        GF_OPTION_INIT ("statedump-path", statedump_path, path, out);
        if (statedump_path) {
                gf_path_strip_trailing_slashes (statedump_path);
                this->ctx->statedump_path = gf_strdup (statedump_path);
        } else {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error setting statedump path");
                ret = -1;
                goto out;
        }

        /* Authentication modules */
        conf->auth_modules = dict_new ();
        GF_VALIDATE_OR_GOTO(this->name, conf->auth_modules, out);

        dict_foreach (this->options, get_auth_types, conf->auth_modules);
        ret = validate_auth_options (this, this->options);
        if (ret == -1) {
                /* logging already done in validate_auth_options function. */
                goto out;
        }

        ret = gf_auth_init (this, conf->auth_modules);
        if (ret) {
                dict_unref (conf->auth_modules);
                goto out;
        }

        /* RPC related */
        conf->rpc = rpcsvc_init (this, this->ctx, this->options, 0);
        if (conf->rpc == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "creation of rpcsvc failed");
                ret = -1;
                goto out;
        }

        ret = rpcsvc_set_outstanding_rpc_limit (conf->rpc, this->options,
                                         RPCSVC_DEFAULT_OUTSTANDING_RPC_LIMIT);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to configure outstanding-rpc-limit");
                goto out;
        }

        ret = rpcsvc_create_listeners (conf->rpc, this->options,
                                       this->name);
        if (ret < 1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "creation of listener failed");
                ret = -1;
                goto out;
        }

        ret = rpcsvc_register_notify (conf->rpc, server_rpc_notify, this);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "registration of notify with rpcsvc failed");
                goto out;
        }

        glusterfs3_3_fop_prog.options = this->options;
        ret = rpcsvc_program_register (conf->rpc, &glusterfs3_3_fop_prog);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "registration of program (name:%s, prognum:%d, "
                        "progver:%d) failed", glusterfs3_3_fop_prog.progname,
                        glusterfs3_3_fop_prog.prognum,
                        glusterfs3_3_fop_prog.progver);
                goto out;
        }

        gluster_handshake_prog.options = this->options;
        ret = rpcsvc_program_register (conf->rpc, &gluster_handshake_prog);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "registration of program (name:%s, prognum:%d, "
                        "progver:%d) failed", gluster_handshake_prog.progname,
                        gluster_handshake_prog.prognum,
                        gluster_handshake_prog.progver);
                rpcsvc_program_unregister (conf->rpc, &glusterfs3_3_fop_prog);
                goto out;
        }

#ifndef GF_DARWIN_HOST_OS
        {
                struct rlimit lim;

                lim.rlim_cur = 1048576;
                lim.rlim_max = 1048576;

                if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "WARNING: Failed to set 'ulimit -n 1M': %s",
                                strerror(errno));
                        lim.rlim_cur = 65536;
                        lim.rlim_max = 65536;

                        if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Failed to set max open fd to 64k: %s",
                                        strerror(errno));
                        } else {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "max open fd set to 64k");
                        }
                }
        }
#endif
        this->private = conf;

        ret = 0;
out:
        if (ret) {
                if (this != NULL) {
                        this->fini (this);
                }

                if (listener != NULL) {
                        rpcsvc_listener_destroy (listener);
                }
        }

        return ret;
}


void
fini (xlator_t *this)
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
notify (xlator_t *this, int32_t event, void *data, ...)
{
        int          ret = 0;
        switch (event) {
        default:
                default_notify (this, event, data);
                break;
        }

        return ret;
}


struct xlator_fops fops;

struct xlator_cbks cbks = {
        .client_destroy = client_destroy_cbk,
};

struct xlator_dumpops dumpops = {
        .priv           = server_priv,
        .fd             = gf_client_dump_fdtables,
        .inode          = gf_client_dump_inodes,
        .priv_to_dict   = server_priv_to_dict,
        .fd_to_dict     = gf_client_dump_fdtables_to_dict,
        .inode_to_dict  = gf_client_dump_inodes_to_dict,
};


struct volume_options options[] = {
        { .key   = {"transport-type"},
          .value = {"rpc", "rpc-over-rdma", "tcp", "socket", "ib-verbs",
                    "unix", "ib-sdp", "tcp/server", "ib-verbs/server", "rdma",
                    "rdma*([ \t]),*([ \t])socket",
                    "rdma*([ \t]),*([ \t])tcp",
                    "tcp*([ \t]),*([ \t])rdma",
                    "socket*([ \t]),*([ \t])rdma"},
          .type  = GF_OPTION_TYPE_STR
        },
        { .key   = {"volume-filename.*"},
          .type  = GF_OPTION_TYPE_PATH,
        },
        { .key   = {"transport.*"},
          .type  = GF_OPTION_TYPE_ANY,
        },
        { .key   = {"rpc*"},
          .type  = GF_OPTION_TYPE_ANY,
        },
        { .key   = {"inode-lru-limit"},
          .type  = GF_OPTION_TYPE_INT,
          .min   = 0,
          .max   = (1 * GF_UNIT_MB),
          .default_value = "16384",
          .description = "Specifies the maximum megabytes of memory to be "
          "used in the inode cache."
        },
        { .key   = {"verify-volfile-checksum"},
          .type  = GF_OPTION_TYPE_BOOL
        },
        { .key   = {"trace"},
          .type  = GF_OPTION_TYPE_BOOL
        },
        { .key   = {"config-directory",
                    "conf-dir"},
          .type  = GF_OPTION_TYPE_PATH,
        },
        { .key   = {"rpc-auth-allow-insecure"},
          .type  = GF_OPTION_TYPE_BOOL,
        },
        { .key   = {"root-squash"},
          .type  = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Map requests from uid/gid 0 to the anonymous "
                         "uid/gid. Note that this does not apply to any other "
                         "uids or gids that might be equally sensitive, such "
                         "as user bin or group staff."
        },
        { .key           = {"anonuid"},
          .type          = GF_OPTION_TYPE_INT,
          .default_value = "65534", /* RPC_NOBODY_UID */
          .min           = 0,
          .max           = (uint32_t) -1,
          .description   = "value of the uid used for the anonymous "
                           "user/nfsnobody when root-squash is enabled."
        },
        { .key           = {"anongid"},
          .type          = GF_OPTION_TYPE_INT,
          .default_value = "65534", /* RPC_NOBODY_GID */
          .min           = 0,
          .max           = (uint32_t) -1,
          .description   = "value of the gid used for the anonymous "
                           "user/nfsnobody when root-squash is enabled."
        },
        { .key           = {"statedump-path"},
          .type          = GF_OPTION_TYPE_PATH,
          .default_value = DEFAULT_VAR_RUN_DIRECTORY,
          .description = "Specifies directory in which gluster should save its"
                         " statedumps. By default it is the /tmp directory"
        },
        { .key   = {"lk-heal"},
          .type  = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
        },
        {.key  = {"grace-timeout"},
         .type = GF_OPTION_TYPE_INT,
         .min  = 10,
         .max  = 1800,
        },
        {.key  = {"tcp-window-size"},
         .type = GF_OPTION_TYPE_SIZET,
         .min  = GF_MIN_SOCKET_WINDOW_SIZE,
         .max  = GF_MAX_SOCKET_WINDOW_SIZE,
         .description = "Specifies the window size for tcp socket."
        },

        /*  The following two options are defined in addr.c, redifined here *
         * for the sake of validation during volume set from cli            */

        { .key   = {"auth.addr.*.allow"},
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS_LIST,
          .description = "Allow a comma separated list of addresses and/or "
                         "hostnames to connect to the server. Option "
                         "auth.reject overrides this option. By default, all "
                         "connections are allowed."
        },
        { .key   = {"auth.addr.*.reject"},
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS_LIST,
          .description = "Reject a comma separated list of addresses and/or "
                         "hostnames to connect to the server. This option "
                         "overrides the auth.allow option. By default, all"
                         " connections are allowed."
        },

        { .key   = {NULL} },
};
