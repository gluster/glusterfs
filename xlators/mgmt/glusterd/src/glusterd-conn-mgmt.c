/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "rpc-clnt.h"
#include "glusterd.h"
#include "glusterd-conn-mgmt.h"
#include "glusterd-conn-helper.h"
#include "glusterd-utils.h"
#include "glusterd-messages.h"

int
glusterd_conn_init (glusterd_conn_t *conn, char *sockpath,
                    int frame_timeout, glusterd_conn_notify_t notify)
{
        int                  ret      = -1;
        dict_t              *options  = NULL;
        struct rpc_clnt     *rpc      = NULL;
        xlator_t            *this     = THIS;
        glusterd_svc_t      *svc      = NULL;

        if (!this)
                goto out;

        svc = glusterd_conn_get_svc_object (conn);
        if (!svc) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SVC_GET_FAIL, "Failed to get the service");
                goto out;
        }

        ret = rpc_transport_unix_options_build (&options, sockpath,
                                                frame_timeout);
        if (ret)
                goto out;

        ret = dict_set_str (options, "transport.socket.ignore-enoent", "on");
        if (ret)
                goto out;

        /* @options is free'd by rpc_transport when destroyed */
        rpc = rpc_clnt_new (options, this, (char *)svc->name, 16);
        if (!rpc) {
                ret = -1;
                goto out;
        }

        ret = rpc_clnt_register_notify (rpc, glusterd_conn_common_notify,
                                        conn);
        if (ret)
                goto out;

        ret = snprintf (conn->sockpath, sizeof (conn->sockpath), "%s",
                        sockpath);
        if (ret < 0)
                goto out;
        else
                ret = 0;

        conn->frame_timeout = frame_timeout;
        conn->rpc = rpc;
        conn->notify = notify;
out:
        if (ret) {
                if (rpc) {
                        rpc_clnt_unref (rpc);
                        rpc = NULL;
                }
        }
        return ret;
}

int
glusterd_conn_term (glusterd_conn_t *conn)
{
        rpc_clnt_unref (conn->rpc);
        return 0;
}

int
glusterd_conn_connect (glusterd_conn_t *conn)
{
        return rpc_clnt_start (conn->rpc);
}

int
glusterd_conn_disconnect (glusterd_conn_t *conn)
{
        rpc_clnt_disconnect (conn->rpc);

        return 0;
}


int
__glusterd_conn_common_notify (struct rpc_clnt *rpc, void *mydata,
                               rpc_clnt_event_t event, void *data)
{
        glusterd_conn_t *conn = mydata;

        /* Silently ignoring this error, exactly like the current
         * implementation */
        if (!conn)
                return 0;

        return conn->notify (conn, event);
}

int
glusterd_conn_common_notify (struct rpc_clnt *rpc, void *mydata,
                             rpc_clnt_event_t event, void *data)
{
        return glusterd_big_locked_notify
                                (rpc, mydata, event, data,
                                 __glusterd_conn_common_notify);
}

int32_t
glusterd_conn_build_socket_filepath (char *rundir, uuid_t uuid,
                                     char *socketpath, int len)
{
        char sockfilepath[PATH_MAX] = {0,};

        snprintf (sockfilepath, sizeof (sockfilepath), "%s/run-%s",
                  rundir, uuid_utoa (uuid));

        glusterd_set_socket_filepath (sockfilepath, socketpath, len);
        return 0;
}
