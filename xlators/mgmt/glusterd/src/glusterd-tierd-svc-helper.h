/*
   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_TIERD_SVC_HELPER_H_
#define _GLUSTERD_TIERD_SVC_HELPER_H_

#include "glusterd.h"

void
glusterd_svc_build_tierd_rundir (glusterd_volinfo_t *volinfo,
                                 char *path, int path_len);

void
glusterd_svc_build_tierd_socket_filepath (glusterd_volinfo_t *volinfo,
                                          char *path, int path_len);

void
glusterd_svc_build_tierd_pidfile (glusterd_volinfo_t *volinfo,
                                  char *path, int path_len);

void
glusterd_svc_build_tierd_volfile_path (glusterd_volinfo_t *volinfo,
                                       char *path, int path_len);

void
glusterd_svc_build_tierd_logdir (char *logdir, char *volname, size_t len);

void
glusterd_svc_build_tierd_logfile (char *logfile, char *logdir, size_t len);
#endif
