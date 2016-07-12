/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_SVC_HELPER_H_
#define _GLUSTERD_SVC_HELPER_H_

#include "glusterd.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-volgen.h"

int
glusterd_svcs_reconfigure ();

int
glusterd_svcs_stop ();

int
glusterd_svcs_manager (glusterd_volinfo_t *volinfo);

int
glusterd_svc_check_volfile_identical (char *svc_name,
                                      glusterd_graph_builder_t builder,
                                      gf_boolean_t *identical);
int
glusterd_svc_check_topology_identical (char *svc_name,
                                       glusterd_graph_builder_t builder,
                                       gf_boolean_t *identical);

int
glusterd_svc_check_tier_volfile_identical (char *svc_name,
                                           glusterd_volinfo_t *volinfo,
                                           gf_boolean_t *identical);
int
glusterd_svc_check_tier_topology_identical (char *svc_name,
                                            glusterd_volinfo_t *volinfo,
                                            gf_boolean_t *identical);

#endif
