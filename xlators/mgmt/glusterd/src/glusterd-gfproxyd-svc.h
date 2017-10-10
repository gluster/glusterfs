/*
  Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_GFPROXYD_SVC_H_
#define _GLUSTERD_GFPROXYD_SVC_H_

#include "glusterd-svc-mgmt.h"

#define gfproxyd_svc_name "gfproxyd"

struct glusterd_gfproxydsvc_ {
        glusterd_svc_t          svc;
        int                     port;
        gf_store_handle_t      *handle;
};

typedef struct glusterd_gfproxydsvc_ glusterd_gfproxydsvc_t;

void
glusterd_gfproxydsvc_build (glusterd_svc_t *svc);

int
glusterd_gfproxydsvc_manager (glusterd_svc_t *svc, void *data, int flags);

int
glusterd_gfproxydsvc_start (glusterd_svc_t *svc, int flags);

int
glusterd_gfproxydsvc_stop (glusterd_svc_t *svc, int sig);

int
glusterd_gfproxydsvc_reconfigure ();

void
glusterd_gfproxydsvc_build_volfile_path (char *server, char *workdir,
                                         char *volfile, size_t len);

int
glusterd_gfproxydsvc_restart ();
#endif
