/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <stdio.h>
#include <limits.h>
#include <signal.h>

#include "glusterd.h"
#include "glusterd-utils.h"
#include <glusterfs/common-utils.h>
#include <glusterfs/xlator.h>
#include <glusterfs/logging.h>
#include "glusterd-messages.h"
#include "glusterd-proc-mgmt.h"

int
glusterd_proc_init(glusterd_proc_t *proc, char *name, char *pidfile,
                   char *logdir, char *logfile, char *volfile, char *volfileid,
                   char *volfileserver)
{
    int ret = -1;

    ret = snprintf(proc->name, sizeof(proc->name), "%s", name);
    if (ret < 0)
        goto out;

    ret = snprintf(proc->pidfile, sizeof(proc->pidfile), "%s", pidfile);
    if (ret < 0)
        goto out;

    ret = snprintf(proc->logdir, sizeof(proc->logdir), "%s", logdir);
    if (ret < 0)
        goto out;

    ret = snprintf(proc->logfile, sizeof(proc->logfile), "%s", logfile);
    if (ret < 0)
        goto out;

    ret = snprintf(proc->volfile, sizeof(proc->volfile), "%s", volfile);
    if (ret < 0)
        goto out;

    ret = snprintf(proc->volfileid, sizeof(proc->volfileid), "%s", volfileid);
    if (ret < 0)
        goto out;

    ret = snprintf(proc->volfileserver, sizeof(proc->volfileserver), "%s",
                   volfileserver);
    if (ret < 0)
        goto out;

out:
    if (ret > 0)
        ret = 0;

    return ret;
}

int
glusterd_proc_stop(glusterd_proc_t *proc, int sig, int flags)
{
    /* NB: Copy-paste code from glusterd_service_stop, the source may be
     * removed once all daemon management use proc */

    int32_t ret = -1;
    pid_t pid = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    int tries;

    conf = this->private;
    GF_ASSERT(conf);

    if (!gf_is_service_running(proc->pidfile, &pid)) {
        ret = 0;
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_ALREADY_STOPPED,
               "%s already stopped", proc->name);
        goto out;
    }
    gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_SVC_STOP_SUCCESS,
           "Stopping %s daemon running in pid: "
           "%d",
           proc->name, pid);

    ret = kill(pid, sig);
    if (ret) {
        switch (errno) {
            case ESRCH:
                gf_msg_debug(this->name, 0,
                             "%s is already "
                             "stopped",
                             proc->name);
                ret = 0;
                goto out;
            default:
                gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_SVC_KILL_FAIL,
                       "Unable to kill %s "
                       "service, reason:%s",
                       proc->name, strerror(errno));
        }
    } else {
        (void)glusterd_unlink_file(proc->pidfile);
    }
    if (flags != PROC_STOP_FORCE)
        goto out;

    for (tries = 10; tries > 0; --tries) {
        if (gf_is_service_running(proc->pidfile, &pid)) {
            synclock_unlock(&conf->big_lock);
            synctask_usleep(100000);
            synclock_lock(&conf->big_lock);
        } else {
            ret = 0;
            goto out;
        }
    }

    if (gf_is_service_running(proc->pidfile, &pid)) {
        ret = kill(pid, SIGKILL);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_PID_KILL_FAIL,
                   "Unable to kill pid:%d, "
                   "reason:%s",
                   pid, strerror(errno));
            goto out;
        }
        ret = glusterd_unlink_file(proc->pidfile);
        if (ret)
            goto out;
    }

    ret = 0;
out:
    return ret;
}

int
glusterd_proc_get_pid(glusterd_proc_t *proc)
{
    int pid = -1;
    (void)gf_is_service_running(proc->pidfile, &pid);
    return pid;
}

int
glusterd_proc_is_running(glusterd_proc_t *proc)
{
    int pid = -1;

    return gf_is_service_running(proc->pidfile, &pid);
}
