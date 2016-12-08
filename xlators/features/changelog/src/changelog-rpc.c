/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "syscall.h"
#include "changelog-rpc.h"
#include "changelog-mem-types.h"
#include "changelog-ev-handle.h"

struct rpcsvc_program *changelog_programs[];

static void
changelog_cleanup_dispatchers (xlator_t *this,
                               changelog_priv_t *priv, int count)
{
        for (count--; count >= 0; count--) {
                (void) changelog_thread_cleanup
                        (this, priv->ev_dispatcher[count]);
        }
}

static int
changelog_cleanup_rpc_threads (xlator_t *this, changelog_priv_t *priv)
{
        int ret = 0;
        changelog_clnt_t *conn = NULL;

        conn = &priv->connections;
        if (!conn)
                return 0;

        /** terminate RPC thread(s) */
        ret = changelog_thread_cleanup (this, priv->connector);
        if (ret != 0)
                goto error_return;
        /** terminate dispatcher thread(s) */
        changelog_cleanup_dispatchers (this, priv, priv->nr_dispatchers);

        /* TODO: what about pending and waiting connections? */
        changelog_ev_cleanup_connections (this, conn);

        /* destroy locks */
        ret = pthread_mutex_destroy (&conn->pending_lock);
        if (ret != 0)
                goto error_return;
        ret = pthread_cond_destroy (&conn->pending_cond);
        if (ret != 0)
                goto error_return;
        ret = LOCK_DESTROY (&conn->active_lock);
        if (ret != 0)
                goto error_return;
        ret = LOCK_DESTROY (&conn->wait_lock);
        if (ret != 0)
                goto error_return;
        return 0;

 error_return:
        return -1;
}

static int
changelog_init_rpc_threads (xlator_t *this, changelog_priv_t *priv,
                            rbuf_t *rbuf, int nr_dispatchers)
{
        int               j    = 0;
        int               ret  = 0;
        changelog_clnt_t *conn = NULL;

        conn = &priv->connections;

        conn->this = this;
        conn->rbuf = rbuf;
        conn->sequence = 1; /* start with sequence number one */

        INIT_LIST_HEAD (&conn->pending);
        INIT_LIST_HEAD (&conn->active);
        INIT_LIST_HEAD (&conn->waitq);

        ret = pthread_mutex_init (&conn->pending_lock, NULL);
        if (ret)
                goto error_return;
        ret = pthread_cond_init (&conn->pending_cond, NULL);
        if (ret)
                goto cleanup_pending_lock;

        ret = LOCK_INIT (&conn->active_lock);
        if (ret)
                goto cleanup_pending_cond;
        ret = LOCK_INIT (&conn->wait_lock);
        if (ret)
                goto cleanup_active_lock;

        /* spawn reverse connection thread */
        ret = pthread_create (&priv->connector,
                              NULL, changelog_ev_connector, conn);
        if (ret != 0)
                goto cleanup_wait_lock;

        /* spawn dispatcher thread(s) */
        priv->ev_dispatcher = GF_CALLOC (nr_dispatchers, sizeof(pthread_t),
                                         gf_changelog_mt_ev_dispatcher_t);
        if (!priv->ev_dispatcher)
                goto cleanup_connector;

        /* spawn dispatcher threads */
        for (; j < nr_dispatchers; j++) {
                ret = pthread_create (&priv->ev_dispatcher[j],
                                      NULL, changelog_ev_dispatch, conn);
                if (ret != 0) {
                        changelog_cleanup_dispatchers (this, priv, j);
                        break;
                }
        }

        if (ret != 0)
                goto cleanup_connector;

        priv->nr_dispatchers = nr_dispatchers;
        return 0;

 cleanup_connector:
        (void) pthread_cancel (priv->connector);
 cleanup_wait_lock:
        LOCK_DESTROY (&conn->wait_lock);
 cleanup_active_lock:
        LOCK_DESTROY (&conn->active_lock);
 cleanup_pending_cond:
        (void) pthread_cond_destroy (&conn->pending_cond);
 cleanup_pending_lock:
        (void) pthread_mutex_destroy (&conn->pending_lock);
 error_return:
        return -1;
}

int
changelog_rpcsvc_notify (rpcsvc_t *rpc,
                         void *xl, rpcsvc_event_t event, void *data)
{
        return 0;
}

void
changelog_destroy_rpc_listner (xlator_t *this, changelog_priv_t *priv)
{
        char sockfile[UNIX_PATH_MAX] = {0,};

        /* sockfile path could have been saved to avoid this */
        CHANGELOG_MAKE_SOCKET_PATH (priv->changelog_brick,
                                    sockfile, UNIX_PATH_MAX);
        changelog_rpc_server_destroy (this,
                                      priv->rpc, sockfile,
                                      changelog_rpcsvc_notify,
                                      changelog_programs);
        (void) changelog_cleanup_rpc_threads (this, priv);
}

rpcsvc_t *
changelog_init_rpc_listener (xlator_t *this, changelog_priv_t *priv,
                            rbuf_t *rbuf, int nr_dispatchers)
{
        int ret = 0;
        char sockfile[UNIX_PATH_MAX] = {0,};
        rpcsvc_t *svcp;

        ret = changelog_init_rpc_threads (this, priv, rbuf, nr_dispatchers);
        if (ret)
                return NULL;

        CHANGELOG_MAKE_SOCKET_PATH (priv->changelog_brick,
                                    sockfile, UNIX_PATH_MAX);
        (void) sys_unlink (sockfile);
        svcp = changelog_rpc_server_init (this, sockfile, NULL,
                                          changelog_rpcsvc_notify,
                                          changelog_programs);
        return svcp;
}

void
changelog_rpc_clnt_cleanup (changelog_rpc_clnt_t *crpc)
{
        if (!crpc)
                return;
        crpc->c_clnt = NULL;
        LOCK_DESTROY (&crpc->lock);
        GF_FREE (crpc);
}

static changelog_rpc_clnt_t *
changelog_rpc_clnt_init (xlator_t *this,
                         changelog_probe_req *rpc_req, changelog_clnt_t *c_clnt)
{
        int ret = 0;
        changelog_rpc_clnt_t *crpc = NULL;

        crpc = GF_CALLOC (1, sizeof (*crpc), gf_changelog_mt_rpc_clnt_t);
        if (!crpc)
                goto error_return;
        INIT_LIST_HEAD (&crpc->list);

        /* Take a ref, the last unref will be on RPC_CLNT_DESTROY
         * which comes as a result of last rpc_clnt_unref.
         */
        crpc->ref = 1;
        changelog_set_disconnect_flag (crpc, _gf_false);

        crpc->filter = rpc_req->filter;
        (void) memcpy (crpc->sock, rpc_req->sock, strlen (rpc_req->sock));

        crpc->this = this;
        crpc->c_clnt = c_clnt;
        crpc->cleanup = changelog_rpc_clnt_cleanup;

        ret = LOCK_INIT (&crpc->lock);
        if (ret != 0)
                goto dealloc_crpc;
        return crpc;

 dealloc_crpc:
        GF_FREE (crpc);
 error_return:
        return NULL;
}

/**
 * Actor declarations
 */

/**
 * @probe_handler
 * A probe RPC call spawns a connect back to the caller. Caller also
 * passes an hint which acts as a filter for selecting updates.
 */

int
changelog_handle_probe (rpcsvc_request_t *req)
{
        int                   ret     = 0;
        xlator_t             *this    = NULL;
        rpcsvc_t             *svc     = NULL;
        changelog_priv_t     *priv    = NULL;
        changelog_clnt_t     *c_clnt  = NULL;
        changelog_rpc_clnt_t *crpc    = NULL;

        changelog_probe_req   rpc_req = {0,};
        changelog_probe_rsp   rpc_rsp = {0,};

        ret = xdr_to_generic (req->msg[0],
                              &rpc_req, (xdrproc_t)xdr_changelog_probe_req);
        if (ret < 0) {
                gf_msg ("", GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_HANDLE_PROBE_ERROR,
                        "xdr decoding error");
                req->rpc_err = GARBAGE_ARGS;
                goto handle_xdr_error;
        }

        /* ->xl hidden in rpcsvc */
        svc    = rpcsvc_request_service (req);
        this   = svc->xl;
        priv   = this->private;
        c_clnt = &priv->connections;

        crpc = changelog_rpc_clnt_init (this, &rpc_req, c_clnt);
        if (!crpc)
                goto handle_xdr_error;

        changelog_ev_queue_connection (c_clnt, crpc);
        rpc_rsp.op_ret = 0;

        goto submit_rpc;

 handle_xdr_error:
        rpc_rsp.op_ret = -1;
 submit_rpc:
        (void) changelog_rpc_sumbit_reply (req, &rpc_rsp, NULL, 0, NULL,
                                           (xdrproc_t)xdr_changelog_probe_rsp);
        return 0;
}

/**
 * RPC declarations
 */

rpcsvc_actor_t changelog_svc_actors[CHANGELOG_RPC_PROC_MAX] = {
        [CHANGELOG_RPC_PROBE_FILTER] = {
                "CHANGELOG PROBE FILTER", CHANGELOG_RPC_PROBE_FILTER,
                changelog_handle_probe, NULL, 0, DRC_NA
        },
};

struct rpcsvc_program changelog_svc_prog = {
        .progname  = CHANGELOG_RPC_PROGNAME,
        .prognum   = CHANGELOG_RPC_PROGNUM,
        .progver   = CHANGELOG_RPC_PROGVER,
        .numactors = CHANGELOG_RPC_PROC_MAX,
        .actors    = changelog_svc_actors,
        .synctask  = _gf_true,
};

struct rpcsvc_program *changelog_programs[] = {
        &changelog_svc_prog,
        NULL,
};
