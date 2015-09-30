/*
  Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_SCRUB_SVC_H_
#define _GLUSTERD_SCRUB_SVC_H_

#include "glusterd-svc-mgmt.h"

typedef struct glusterd_scrubsvc_ glusterd_scrubsvc_t;

struct glusterd_scrubsvc_{
        glusterd_svc_t      svc;
        gf_store_handle_t   *handle;
};

void
glusterd_scrubsvc_build (glusterd_svc_t *svc);

int
glusterd_scrubsvc_init (glusterd_svc_t *svc);

int
glusterd_scrubsvc_manager (glusterd_svc_t *svc, void *data, int flags);

int
glusterd_scrubsvc_start (glusterd_svc_t *svc, int flags);

int
glusterd_scrubsvc_stop (glusterd_svc_t *svc, int sig);

int
glusterd_scrubsvc_reconfigure ();

void
glusterd_scrubsvc_build_volfile_path (char *server, char *workdir,
                                     char *volfile, size_t len);

#endif
