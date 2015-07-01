/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_SNAPD_SVC_H_
#define _GLUSTERD_SNAPD_SVC_H_

#include "glusterd-svc-mgmt.h"

typedef struct glusterd_snapdsvc_ glusterd_snapdsvc_t;

struct glusterd_snapdsvc_{
        glusterd_svc_t     svc;
        int                port;
        gf_store_handle_t *handle;
};

void
glusterd_snapdsvc_build (glusterd_svc_t *svc);

int
glusterd_snapdsvc_init (void *data);

int
glusterd_snapdsvc_manager (glusterd_svc_t *svc, void *data, int flags);

int
glusterd_snapdsvc_start (glusterd_svc_t *svc, int flags);

int
glusterd_snapdsvc_restart ();

int
glusterd_snapdsvc_rpc_notify (glusterd_conn_t *conn, rpc_clnt_event_t event);

#endif
