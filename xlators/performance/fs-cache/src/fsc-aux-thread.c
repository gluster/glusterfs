/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#include <glusterfs/call-stub.h>
#include <glusterfs/defaults.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/logging.h>
#include <glusterfs/dict.h>
#include <glusterfs/xlator.h>
#include "fs-cache.h"
#include "fsc-mem-types.h"
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <glusterfs/locking.h>
#include <glusterfs/timespec.h>



void
fsc_disk_space_check(xlator_t *this)
{
    fsc_conf_t *conf = NULL;
    char *subvol_path = NULL;
    int op_ret = 0;
    int percent = 0;
    struct statvfs buf = {0};
    struct stat fstatbuf = {
        0,
    };
    uint64_t totsz = 0;
    uint64_t freesz = 0;

    GF_VALIDATE_OR_GOTO(this->name, this, out);
    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    subvol_path = conf->cache_dir;
    percent = conf->disk_reserve;

    op_ret = sys_stat(subvol_path, &fstatbuf);
    if (op_ret == -1) {
        gf_msg("fs-cache", GF_LOG_ERROR, 0, FS_CACHE_MSG_ERROR,
               "xl=%p,conf=%p,check failed path=(%s)", this, conf, subvol_path);
        conf->is_enable = _gf_false;
        goto out;
    } else {
        conf->is_enable = _gf_true;
    }

    op_ret = sys_statvfs(subvol_path, &buf);
    if (op_ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
               "statvfs failed on %s", subvol_path);
        goto out;
    }
    totsz = (buf.f_blocks * buf.f_bsize);
    freesz = (buf.f_bfree * buf.f_bsize);

    gf_msg(this->name, GF_LOG_TRACE, 0, FS_CACHE_MSG_TRACE,
           "xl=%p,conf=%p,free size=%" PRIu64 "MB", this, conf, freesz / 1024 / 1024);

    if (freesz <= ((totsz * percent) / 100)) {
        conf->disk_space_full = 1;
        gf_msg(this->name, GF_LOG_WARNING, 0, FS_CACHE_MSG_WARNING,
               "dir=%s is full ", subvol_path);
    } else {
        conf->disk_space_full = 0;
    }
out:
    return;
}

void
fsc_clear_idle_node(xlator_t *this)
{
    fsc_conf_t *conf = NULL;
    fsc_inode_t *curr = NULL, *tmp = NULL;
    int32_t del_cnt = 0;
    conf = this->private;

    if (conf->resycle_idle_inode == 0) {
        return;
    }

    gf_msg(this->name, GF_LOG_TRACE, 0, FS_CACHE_MSG_TRACE,
           "clear idle fsc inode start %d", conf->inodes_count);

    fsc_inodes_list_lock(conf);
    list_for_each_entry_safe(curr, tmp, &conf->inodes, inode_list)
    {
        if (fsc_inode_is_idle(curr)) {
            conf->inodes_count--;
            list_del_init(&curr->inode_list);
            fsc_inode_destroy(curr, 1);
            del_cnt++;
            if (del_cnt >= 100) {
                goto unlock;
            }
        }
    }
unlock:
    fsc_inodes_list_unlock(conf);

    gf_msg(this->name, GF_LOG_TRACE, 0, FS_CACHE_MSG_TRACE,
           "clear idle fsc inode end %d", conf->inodes_count);
}

static void *
fsc_aux_thread_proc(void *data)
{
    xlator_t *this = NULL;
    fsc_conf_t *conf = NULL;
    uint32_t interval = 0;
    uint32_t clear_count = 0;
    uint32_t clear_interval = 0;
    int ret = -1;

    this = data;
    conf = this->private;

    interval = 15;
    clear_interval = 600 / interval;

    gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
           "fsc_aux thread started xlator=%p,interval = %d seconds",
           this, interval);
    while (1) {
        /* aborting sleep() is a request to exit this thread, sleep()
         * will normally not return when cancelled */
        ret = sleep(interval);
        if (ret > 0)
            break;
        clear_count++;
        /* prevent thread errors while doing the health-check(s) */
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        /* Do the disk-check.*/
        fsc_disk_space_check(this);

        if (!conf->aux_thread_active)
            goto out;

        if ( (clear_count % clear_interval) == 0 ) {
            fsc_clear_idle_node(this);
        }
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }

out:
    gf_msg(this->name, GF_LOG_INFO, errno, FS_CACHE_MSG_INFO,
           "fsc_aux thread exiting xlator=%p.", this);

    pthread_mutex_lock(&conf->aux_lock);
    {
        conf->aux_thread_active = _gf_false;
    }
    pthread_mutex_unlock(&conf->aux_lock);

    return NULL;
}

int
fsc_spawn_aux_thread(xlator_t *xl)
{
    fsc_conf_t *conf = NULL;
    int ret = -1;

    conf = xl->private;

    pthread_mutex_lock(&conf->aux_lock);
    {
        /* cancel the running thread  */
        if (conf->aux_thread_active == _gf_true) {
            pthread_cancel(conf->aux_thread);
            conf->aux_thread_active = _gf_false;
        }

        ret = gf_thread_create(&conf->aux_thread, NULL,
                               fsc_aux_thread_proc, xl,
                               "fsc_aux");
        if (ret) {
            conf->aux_thread_active = _gf_false;
            gf_msg(xl->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
                   "unable to spanw fsc_aux  thread");
            goto unlock;
        }

        conf->aux_thread_active = _gf_true;
    }
unlock:
    pthread_mutex_unlock(&conf->aux_lock);
    return ret;
}
