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
glusterd_svcs_reconfigure(glusterd_volinfo_t *volinfo);

int
glusterd_svcs_stop(glusterd_volinfo_t *vol);

int
glusterd_svcs_manager(glusterd_volinfo_t *volinfo);

int
glusterd_svc_check_volfile_identical(char *svc_name,
                                     glusterd_graph_builder_t builder,
                                     gf_boolean_t *identical);
int
glusterd_svc_check_topology_identical(char *svc_name,
                                      glusterd_graph_builder_t builder,
                                      gf_boolean_t *identical);
int
glusterd_volume_svc_check_volfile_identical(char *svc_name, dict_t *mode_dict,
                                            glusterd_volinfo_t *volinfo,
                                            glusterd_vol_graph_builder_t,
                                            gf_boolean_t *identical);
int
glusterd_volume_svc_check_topology_identical(char *svc_name, dict_t *mode_dict,
                                             glusterd_volinfo_t *volinfo,
                                             glusterd_vol_graph_builder_t,
                                             gf_boolean_t *identical);
void
glusterd_volume_svc_build_volfile_path(char *server, glusterd_volinfo_t *vol,
                                       char *volfile, size_t len);
void *
__gf_find_compatible_svc(gd_node_type daemon);

glusterd_svc_proc_t *
glusterd_svcprocess_new();

int
glusterd_shd_svc_mux_init(glusterd_volinfo_t *volinfo, glusterd_svc_t *svc);

void *
__gf_find_compatible_svc_from_pid(gd_node_type daemon, pid_t pid);

int
glusterd_attach_svc(glusterd_svc_t *svc, glusterd_volinfo_t *volinfo,
                    int flags);

int
glusterd_detach_svc(glusterd_svc_t *svc, glusterd_volinfo_t *volinfo, int sig);

int
__glusterd_send_svc_configure_req(glusterd_svc_t *svc, int flag,
                                  struct rpc_clnt *rpc, char *volfile_id,
                                  int op);

#endif
