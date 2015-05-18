/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_PROC_MGMT_H_
#define _GLUSTERD_PROC_MGMT_H_

typedef struct glusterd_proc_ glusterd_proc_t;

enum proc_flags {
        PROC_NONE = 0,
        PROC_START,
        PROC_START_NO_WAIT,
        PROC_STOP,
        PROC_STOP_FORCE
};

struct glusterd_proc_ {
        char name[PATH_MAX];
        char pidfile[PATH_MAX];
        char logdir[PATH_MAX];
        char logfile[PATH_MAX];
        char volfile[PATH_MAX];
        char volfileserver[PATH_MAX];
        char volfileid[256];
};

int
glusterd_proc_init (glusterd_proc_t *proc, char *name, char *pidfile,
                    char *logdir, char *logfile, char *volfile, char *volfileid,
                    char *volfileserver);

int
glusterd_proc_stop (glusterd_proc_t *proc, int sig, int flags);

int
glusterd_proc_is_running (glusterd_proc_t *proc);
#endif
