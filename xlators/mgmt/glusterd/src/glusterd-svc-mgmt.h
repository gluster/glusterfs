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
#include "glusterd-rcu.h"

struct glusterd_svc_;

typedef struct glusterd_svc_ glusterd_svc_t;
typedef struct glusterd_svc_proc_ glusterd_svc_proc_t;

typedef void (*glusterd_svc_build_t)(glusterd_svc_t *svc);

typedef int (*glusterd_svc_manager_t)(glusterd_svc_t *svc, void *data,
                                      int flags);
typedef int (*glusterd_svc_start_t)(glusterd_svc_t *svc, int flags);
typedef int (*glusterd_svc_stop_t)(glusterd_svc_t *svc, int sig);
typedef int (*glusterd_svc_reconfigure_t)(void *data);

typedef int (*glusterd_muxsvc_conn_notify_t)(glusterd_svc_proc_t *mux_proc,
                                             rpc_clnt_event_t event);

typedef enum gf_svc_status {
    GF_SVC_STARTING,
    GF_SVC_STARTED,
    GF_SVC_STOPPING,
    GF_SVC_DISCONNECTED,
    GF_SVC_DIED,
} gf_svc_status_t;

struct glusterd_svc_proc_ {
    struct cds_list_head svc_proc_list;
    struct cds_list_head svcs;
    glusterd_muxsvc_conn_notify_t notify;
    rpc_clnt_t *rpc;
    void *data;
    gf_svc_status_t status;
};

struct glusterd_svc_ {
    glusterd_conn_t conn;
    glusterd_svc_manager_t manager;
    glusterd_svc_start_t start;
    glusterd_svc_stop_t stop;
    glusterd_svc_reconfigure_t reconfigure;
    glusterd_svc_proc_t *svc_proc;
    struct cds_list_head mux_svc;
    glusterd_proc_t proc;
    char name[NAME_MAX];
    gf_boolean_t online;
    gf_boolean_t inited;
};

int
glusterd_svc_create_rundir(char *rundir);

int
glusterd_svc_init(glusterd_svc_t *svc, char *svc_name);

int
glusterd_svc_start(glusterd_svc_t *svc, int flags, dict_t *cmdline);

int
glusterd_svc_stop(glusterd_svc_t *svc, int sig);

void
glusterd_svc_build_pidfile_path(char *server, char *workdir, char *path,
                                size_t len);

void
glusterd_svc_build_volfile_path(char *server, char *workdir, char *volfile,
                                size_t len);

void
glusterd_svc_build_logfile_path(char *server, char *logdir, char *logfile,
                                size_t len);

void
glusterd_svc_build_svcdir(char *server, char *workdir, char *path, size_t len);

void
glusterd_svc_build_rundir(char *server, char *workdir, char *path, size_t len);

int
glusterd_svc_reconfigure(int (*create_volfile)());

int
glusterd_svc_common_rpc_notify(glusterd_conn_t *conn, rpc_clnt_event_t event);

int
glusterd_muxsvc_common_rpc_notify(glusterd_svc_proc_t *conn,
                                  rpc_clnt_event_t event);

int
glusterd_proc_get_pid(glusterd_proc_t *proc);

int
glusterd_muxsvc_conn_init(glusterd_conn_t *conn, glusterd_svc_proc_t *mux_proc,
                          char *sockpath, int frame_timeout,
                          glusterd_muxsvc_conn_notify_t notify);
#endif
