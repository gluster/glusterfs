/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_SVC_MGMT_H_
#define _GLUSTERD_SVC_MGMT_H_

#include "glusterd-proc-mgmt.h"
#include "glusterd-conn-mgmt.h"

struct glusterd_svc_;
typedef struct glusterd_svc_ glusterd_svc_t;

typedef void (*glusterd_svc_build_t) (glusterd_svc_t *svc);

typedef int (*glusterd_svc_manager_t) (glusterd_svc_t *svc,
                                       void *data, int flags);
typedef int (*glusterd_svc_start_t) (glusterd_svc_t *svc, int flags);
typedef int (*glusterd_svc_stop_t) (glusterd_svc_t *svc, int sig);
typedef int (*glusterd_svc_reconfigure_t) (void *data);

struct glusterd_svc_ {
        char                      name[PATH_MAX];
        glusterd_conn_t           conn;
        glusterd_proc_t           proc;
        glusterd_svc_build_t      build;
        glusterd_svc_manager_t    manager;
        glusterd_svc_start_t      start;
        glusterd_svc_stop_t       stop;
        gf_boolean_t              online;
        gf_boolean_t              inited;
        glusterd_svc_reconfigure_t    reconfigure;
};

int
glusterd_svc_create_rundir (char *rundir);

int
glusterd_svc_init (glusterd_svc_t *svc, char *svc_name);

int
glusterd_svc_start (glusterd_svc_t *svc, int flags, dict_t *cmdline);

int
glusterd_svc_stop (glusterd_svc_t *svc, int sig);

void
glusterd_svc_build_pidfile_path (char *server, char *workdir,
                                 char *path, size_t len);

void
glusterd_svc_build_volfile_path (char *server, char *workdir,
                                 char *volfile, size_t len);

void
glusterd_svc_build_svcdir (char *server, char *workdir,
                           char *path, size_t len);

void
glusterd_svc_build_rundir (char *server, char *workdir,
                           char *path, size_t len);

int
glusterd_svc_reconfigure (int (*create_volfile) ());

int
glusterd_svc_common_rpc_notify (glusterd_conn_t *conn,
                                rpc_clnt_event_t event);

#endif
