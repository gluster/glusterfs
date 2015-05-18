/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_CONN_MGMT_H_
#define _GLUSTERD_CONN_MGMT_H_

#include "rpc-clnt.h"

typedef struct glusterd_conn_ glusterd_conn_t;

typedef int (*glusterd_conn_notify_t)
                (glusterd_conn_t *conn, rpc_clnt_event_t event);

struct glusterd_conn_ {
        struct rpc_clnt *rpc;
        char sockpath[PATH_MAX];
        int frame_timeout;
        /* Existing daemons tend to specialize their respective
         * notify implementations, so ... */
        glusterd_conn_notify_t notify;
};

int
glusterd_conn_init (glusterd_conn_t *conn, char *sockpath,
                    int frame_timeout, glusterd_conn_notify_t notify);

int
glusterd_conn_term (glusterd_conn_t *conn);

int
glusterd_conn_connect (glusterd_conn_t *conn);

int
glusterd_conn_disconnect (glusterd_conn_t *conn);

int
glusterd_conn_common_notify (struct rpc_clnt *rpc, void *mydata,
                             rpc_clnt_event_t event, void *data);

int32_t
glusterd_conn_build_socket_filepath (char *rundir, uuid_t uuid,
                                     char *socketpath, int len);

#endif
