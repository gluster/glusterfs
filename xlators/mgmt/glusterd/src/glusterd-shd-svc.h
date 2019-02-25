/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_SHD_SVC_H_
#define _GLUSTERD_SHD_SVC_H_

#include "glusterd-svc-mgmt.h"
#include "glusterd.h"

typedef struct glusterd_shdsvc_ glusterd_shdsvc_t;
struct glusterd_shdsvc_ {
    glusterd_svc_t svc;
    gf_boolean_t attached;
};

void
glusterd_shdsvc_build(glusterd_svc_t *svc);

int
glusterd_shdsvc_init(void *data, glusterd_conn_t *mux_conn,
                     glusterd_svc_proc_t *svc_proc);

int
glusterd_shdsvc_manager(glusterd_svc_t *svc, void *data, int flags);

int
glusterd_shdsvc_start(glusterd_svc_t *svc, int flags);

int
glusterd_shdsvc_reconfigure();

int
glusterd_shdsvc_restart();

int
glusterd_shdsvc_stop(glusterd_svc_t *svc, int sig);

#endif
