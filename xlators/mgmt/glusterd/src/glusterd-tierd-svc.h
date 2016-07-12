/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_TIERD_SVC_H_
#define _GLUSTERD_TIERD_SVC_H_

#include "glusterd-svc-mgmt.h"


typedef struct glusterd_tierdsvc_ glusterd_tierdsvc_t;

struct glusterd_tierdsvc_ {
        glusterd_svc_t          svc;
        int                     port;
        gf_store_handle_t      *handle;
};

void
glusterd_tierdsvc_build (glusterd_svc_t *svc);

int
glusterd_tierdsvc_init (void *data);

int
glusterd_tierdsvc_manager (glusterd_svc_t *svc, void *data, int flags);

int
glusterd_tierdsvc_start (glusterd_svc_t *svc, int flags);

int
glusterd_tierdsvc_reconfigure (void *data);

int
glusterd_tierdsvc_restart ();

#endif
