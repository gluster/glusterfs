/*
   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_SHD_SVC_HELPER_H_
#define _GLUSTERD_SHD_SVC_HELPER_H_

#include "glusterd.h"
#include "glusterd-svc-mgmt.h"

void
glusterd_svc_build_shd_socket_filepath(glusterd_volinfo_t *volinfo, char *path,
                                       int path_len);

void
glusterd_svc_build_shd_pidfile(glusterd_volinfo_t *volinfo, char *path,
                               int path_len);

void
glusterd_svc_build_shd_volfile_path(glusterd_volinfo_t *volinfo, char *path,
                                    int path_len);

void
glusterd_shd_svcproc_cleanup(glusterd_shdsvc_t *shd);

int
glusterd_recover_shd_attach_failure(glusterd_volinfo_t *volinfo,
                                    glusterd_svc_t *svc, int flags);

int
glusterd_shdsvc_create_volfile(glusterd_volinfo_t *volinfo);

int
glusterd_svc_set_shd_pidfile(glusterd_volinfo_t *volinfo, dict_t *dict);

#endif
