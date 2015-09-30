/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_SNAPD_SVC_HELPER_H_
#define _GLUSTERD_SNAPD_SVC_HELPER_H_

#include "glusterd.h"

void
glusterd_svc_build_snapd_rundir (glusterd_volinfo_t *volinfo,
                                 char *path, int path_len);

void
glusterd_svc_build_snapd_socket_filepath (glusterd_volinfo_t *volinfo,
                                          char *path, int path_len);

void
glusterd_svc_build_snapd_pidfile (glusterd_volinfo_t *volinfo,
                                  char *path, int path_len);

void
glusterd_svc_build_snapd_volfile (glusterd_volinfo_t *volinfo,
                                  char *path, int path_len);

#endif
