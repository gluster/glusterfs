/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "call-stub.h"
#include "defaults.h"
#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-threads.h"
#include <stdlib.h>
#include "locking.h"
#include "iot-mem-types.h"
#include "io-threads-messages.h"
#include "timespec.h"

struct volume_options options[];

int
iot_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    IOT_FOP(lookup, frame, this, loc, xdata);
    return 0;
}

int
iot_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc, struct iatt *stbuf,
            int32_t valid, dict_t *xdata)
{
    IOT_FOP(setattr, frame, this, loc, stbuf, valid, xdata);
    return 0;
}

int
iot_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iatt *stbuf,
             int32_t valid, dict_t *xdata)
{
    IOT_FOP(fsetattr, frame, this, fd, stbuf, valid, xdata);
    return 0;
}

int
iot_access(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
           dict_t *xdata)
{
    IOT_FOP(access, frame, this, loc, mask, xdata);
    return 0;
}

int
iot_readlink(call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
             dict_t *xdata)
{
    IOT_FOP(readlink, frame, this, loc, size, xdata);
    return 0;
}

int
iot_mknod(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev, mode_t umask, dict_t *xdata)
{
    IOT_FOP(mknod, frame, this, loc, mode, rdev, umask, xdata);
    return 0;
}

int
iot_mkdir(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          mode_t umask, dict_t *xdata)
{
    IOT_FOP(mkdir, frame, this, loc, mode, umask, xdata);
    return 0;
}

int
iot_rmdir(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
          dict_t *xdata)
{
    IOT_FOP(rmdir, frame, this, loc, flags, xdata);
    return 0;
}

int
iot_symlink(call_frame_t *frame, xlator_t *this, const char *linkname,
            loc_t *loc, mode_t umask, dict_t *xdata)
{
    IOT_FOP(symlink, frame, this, linkname, loc, umask, xdata);
    return 0;
}

int
iot_rename(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
           dict_t *xdata)
{
    IOT_FOP(rename, frame, this, oldloc, newloc, xdata);
    return 0;
}

int
iot_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
    IOT_FOP(open, frame, this, loc, flags, fd, xdata);
    return 0;
}

int
iot_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
    IOT_FOP(create, frame, this, loc, flags, mode, umask, fd, xdata);
    return 0;
}

int
iot_put(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
        mode_t umask, uint32_t flags, struct iovec *vector, int32_t count,
        off_t offset, struct iobref *iobref, dict_t *xattr, dict_t *xdata)
{
    IOT_FOP(put, frame, this, loc, mode, umask, flags, vector, count, offset,
            iobref, xattr, xdata);
    return 0;
}

int
iot_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t flags, dict_t *xdata)
{
    IOT_FOP(readv, frame, this, fd, size, offset, flags, xdata);
    return 0;
}

int
iot_flush(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    IOT_FOP(flush, frame, this, fd, xdata);
    return 0;
}

int
iot_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
          dict_t *xdata)
{
    IOT_FOP(fsync, frame, this, fd, datasync, xdata);
    return 0;
}

int
iot_writev(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t offset, uint32_t flags, struct iobref *iobref,
           dict_t *xdata)
{
    IOT_FOP(writev, frame, this, fd, vector, count, offset, flags, iobref,
            xdata);
    return 0;
}

int
iot_lk(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
       struct gf_flock *flock, dict_t *xdata)
{
    IOT_FOP(lk, frame, this, fd, cmd, flock, xdata);
    return 0;
}

int
iot_stat(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    IOT_FOP(stat, frame, this, loc, xdata);
    return 0;
}

int
iot_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    IOT_FOP(fstat, frame, this, fd, xdata);
    return 0;
}

int
iot_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
             dict_t *xdata)
{
    IOT_FOP(truncate, frame, this, loc, offset, xdata);
    return 0;
}

int
iot_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              dict_t *xdata)
{
    IOT_FOP(ftruncate, frame, this, fd, offset, xdata);
    return 0;
}

int
iot_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t xflag,
           dict_t *xdata)
{
    IOT_FOP(unlink, frame, this, loc, xflag, xdata);
    return 0;
}

int
iot_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
         dict_t *xdata)
{
    IOT_FOP(link, frame, this, oldloc, newloc, xdata);
    return 0;
}

int
iot_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
            dict_t *xdata)
{
    IOT_FOP(opendir, frame, this, loc, fd, xdata);
    return 0;
}

int
iot_fsyncdir(call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync,
             dict_t *xdata)
{
    IOT_FOP(fsyncdir, frame, this, fd, datasync, xdata);
    return 0;
}

int
iot_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    IOT_FOP(statfs, frame, this, loc, xdata);
    return 0;
}

int
iot_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags, dict_t *xdata)
{
    IOT_FOP(setxattr, frame, this, loc, dict, flags, xdata);
    return 0;
}

int
iot_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, const char *name,
             dict_t *xdata)
{
    gf_iot_t *iot = NULL;
    dict_t *depths = NULL;
    int i = 0;
    int32_t op_ret = 0;
    int32_t op_errno = 0;

    iot = this->ctx->iot;

    if (iot && name && strcmp(name, IO_THREADS_QUEUE_SIZE_KEY) == 0) {
        /*
         * We explicitly do not want a reference count
         * for this dict in this translator
         */
        depths = dict_new();
        if (!depths) {
            op_ret = -1;
            op_errno = ENOMEM;
            goto unwind_special_getxattr;
        }

        for (i = 0; i < GF_FOP_PRI_MAX; i++) {
            if (dict_set_int32(depths, (char *)fop_pri_to_string(i),
                               iot->queue_sizes[i]) != 0) {
                dict_unref(depths);
                depths = NULL;
                goto unwind_special_getxattr;
            }
        }

    unwind_special_getxattr:
        STACK_UNWIND_STRICT(getxattr, frame, op_ret, op_errno, depths, xdata);
        if (depths)
            dict_unref(depths);
        return 0;
    }

    IOT_FOP(getxattr, frame, this, loc, name, xdata);
    return 0;
}

int
iot_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
              dict_t *xdata)
{
    IOT_FOP(fgetxattr, frame, this, fd, name, xdata);
    return 0;
}

int
iot_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int32_t flags, dict_t *xdata)
{
    IOT_FOP(fsetxattr, frame, this, fd, dict, flags, xdata);
    return 0;
}

int
iot_removexattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
    IOT_FOP(removexattr, frame, this, loc, name, xdata);
    return 0;
}

int
iot_fremovexattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
    IOT_FOP(fremovexattr, frame, this, fd, name, xdata);
    return 0;
}

int
iot_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, dict_t *xdata)
{
    IOT_FOP(readdirp, frame, this, fd, size, offset, xdata);
    return 0;
}

int
iot_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t offset, dict_t *xdata)
{
    IOT_FOP(readdir, frame, this, fd, size, offset, xdata);
    return 0;
}

int
iot_inodelk(call_frame_t *frame, xlator_t *this, const char *volume, loc_t *loc,
            int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
    IOT_FOP(inodelk, frame, this, volume, loc, cmd, lock, xdata);
    return 0;
}

int
iot_finodelk(call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
             int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
    IOT_FOP(finodelk, frame, this, volume, fd, cmd, lock, xdata);
    return 0;
}

int
iot_entrylk(call_frame_t *frame, xlator_t *this, const char *volume, loc_t *loc,
            const char *basename, entrylk_cmd cmd, entrylk_type type,
            dict_t *xdata)
{
    IOT_FOP(entrylk, frame, this, volume, loc, basename, cmd, type, xdata);
    return 0;
}

int
iot_fentrylk(call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
             const char *basename, entrylk_cmd cmd, entrylk_type type,
             dict_t *xdata)
{
    IOT_FOP(fentrylk, frame, this, volume, fd, basename, cmd, type, xdata);
    return 0;
}

int
iot_xattrop(call_frame_t *frame, xlator_t *this, loc_t *loc,
            gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
    IOT_FOP(xattrop, frame, this, loc, optype, xattr, xdata);
    return 0;
}

int
iot_fxattrop(call_frame_t *frame, xlator_t *this, fd_t *fd,
             gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
    IOT_FOP(fxattrop, frame, this, fd, optype, xattr, xdata);
    return 0;
}

int32_t
iot_rchecksum(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              int32_t len, dict_t *xdata)
{
    IOT_FOP(rchecksum, frame, this, fd, offset, len, xdata);
    return 0;
}

int
iot_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
              off_t offset, size_t len, dict_t *xdata)
{
    IOT_FOP(fallocate, frame, this, fd, mode, offset, len, xdata);
    return 0;
}

int
iot_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            size_t len, dict_t *xdata)
{
    IOT_FOP(discard, frame, this, fd, offset, len, xdata);
    return 0;
}

int
iot_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             off_t len, dict_t *xdata)
{
    IOT_FOP(zerofill, frame, this, fd, offset, len, xdata);
    return 0;
}

int
iot_seek(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
         gf_seek_what_t what, dict_t *xdata)
{
    IOT_FOP(seek, frame, this, fd, offset, what, xdata);
    return 0;
}

int
iot_lease(call_frame_t *frame, xlator_t *this, loc_t *loc,
          struct gf_lease *lease, dict_t *xdata)
{
    IOT_FOP(lease, frame, this, loc, lease, xdata);
    return 0;
}

int
iot_getactivelk(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    IOT_FOP(getactivelk, frame, this, loc, xdata);
    return 0;
}

int
iot_setactivelk(call_frame_t *frame, xlator_t *this, loc_t *loc,
                lock_migration_info_t *locklist, dict_t *xdata)
{
    IOT_FOP(setactivelk, frame, this, loc, locklist, xdata);
    return 0;
}

int32_t
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    if (!this)
        return ret;

    ret = xlator_mem_acct_init(this, gf_iot_mt_end + 1);

    if (ret != 0) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, IO_THREADS_MSG_NO_MEMORY,
               "Memory accounting init failed");
        return ret;
    }

    return ret;
}

int
reconfigure(xlator_t *this, dict_t *options)
{
    gf_iot_t *iot = NULL;
    int ret = -1;

    iot = this->ctx->iot;
    if (!iot)
        goto out;

    GF_OPTION_RECONF("thread-count", iot->max_count, options, int32, out);

    GF_OPTION_RECONF("high-prio-threads", iot->ac_iot_limit[GF_FOP_PRI_HI],
                     options, int32, out);

    GF_OPTION_RECONF("normal-prio-threads",
                     iot->ac_iot_limit[GF_FOP_PRI_NORMAL], options, int32, out);

    GF_OPTION_RECONF("low-prio-threads", iot->ac_iot_limit[GF_FOP_PRI_LO],
                     options, int32, out);

    GF_OPTION_RECONF("least-prio-threads", iot->ac_iot_limit[GF_FOP_PRI_LEAST],
                     options, int32, out);

    GF_OPTION_RECONF("enable-least-priority", iot->least_priority, options,
                     bool, out);

    GF_OPTION_RECONF("cleanup-disconnected-reqs",
                     iot->cleanup_disconnected_reqs, options, bool, out);

    GF_OPTION_RECONF("watchdog-secs", iot->watchdog_secs, options, int32, out);

    GF_OPTION_RECONF("pass-through", this->pass_through, options, bool, out);

    ret = gf_iot_reconf(this);
out:
    return ret;
}

int
init(xlator_t *this)
{
    gf_iot_t *iot = NULL;
    int ret = -1;

    if (!this->children || this->children->next) {
        gf_msg("io-threads", GF_LOG_ERROR, 0,
               IO_THREADS_MSG_XLATOR_CHILD_MISCONFIGURED,
               "FATAL: iot not configured "
               "with exactly one child");
        goto out;
    }

    if (!this->parents) {
        gf_msg(this->name, GF_LOG_WARNING, 0, IO_THREADS_MSG_VOL_MISCONFIGURED,
               "dangling volume. check volfile ");
    }

    iot = this->ctx->iot;
    if (!iot)
        goto out;

    GF_OPTION_INIT("thread-count", iot->max_count, int32, out);

    GF_OPTION_INIT("high-prio-threads", iot->ac_iot_limit[GF_FOP_PRI_HI], int32,
                   out);

    GF_OPTION_INIT("normal-prio-threads", iot->ac_iot_limit[GF_FOP_PRI_NORMAL],
                   int32, out);

    GF_OPTION_INIT("low-prio-threads", iot->ac_iot_limit[GF_FOP_PRI_LO], int32,
                   out);

    GF_OPTION_INIT("least-prio-threads", iot->ac_iot_limit[GF_FOP_PRI_LEAST],
                   int32, out);

    GF_OPTION_INIT("idle-time", iot->idle_time, int32, out);

    GF_OPTION_INIT("enable-least-priority", iot->least_priority, bool, out);

    GF_OPTION_INIT("cleanup-disconnected-reqs", iot->cleanup_disconnected_reqs,
                   bool, out);

    GF_OPTION_INIT("pass-through", this->pass_through, bool, out);

    GF_OPTION_INIT("watchdog-secs", iot->watchdog_secs, int32, out);

    ret = gf_iot_get(this);
out:
    return ret;
}

int
notify(xlator_t *this, int32_t event, void *data, ...)
{
    /* TODO: Ensure all the fops submitted by this xl to iot, to be
     * complete? may be not required, as protocol-server or master
     * takes care of this
     if (GF_EVENT_PARENT_DOWN == event)
        gf_iot_wait_queue_empty(); */

    default_notify(this, event, data);

    return 0;
}

void
fini(xlator_t *this)
{
    gf_iot_put(this);
    return;
}

struct xlator_fops fops = {
    .open = iot_open,
    .create = iot_create,
    .readv = iot_readv,
    .writev = iot_writev,
    .flush = iot_flush,
    .fsync = iot_fsync,
    .lk = iot_lk,
    .stat = iot_stat,
    .fstat = iot_fstat,
    .truncate = iot_truncate,
    .ftruncate = iot_ftruncate,
    .unlink = iot_unlink,
    .lookup = iot_lookup,
    .setattr = iot_setattr,
    .fsetattr = iot_fsetattr,
    .access = iot_access,
    .readlink = iot_readlink,
    .mknod = iot_mknod,
    .mkdir = iot_mkdir,
    .rmdir = iot_rmdir,
    .symlink = iot_symlink,
    .rename = iot_rename,
    .link = iot_link,
    .opendir = iot_opendir,
    .fsyncdir = iot_fsyncdir,
    .statfs = iot_statfs,
    .setxattr = iot_setxattr,
    .getxattr = iot_getxattr,
    .fgetxattr = iot_fgetxattr,
    .fsetxattr = iot_fsetxattr,
    .removexattr = iot_removexattr,
    .fremovexattr = iot_fremovexattr,
    .readdir = iot_readdir,
    .readdirp = iot_readdirp,
    .inodelk = iot_inodelk,
    .finodelk = iot_finodelk,
    .entrylk = iot_entrylk,
    .fentrylk = iot_fentrylk,
    .xattrop = iot_xattrop,
    .fxattrop = iot_fxattrop,
    .rchecksum = iot_rchecksum,
    .fallocate = iot_fallocate,
    .discard = iot_discard,
    .zerofill = iot_zerofill,
    .seek = iot_seek,
    .lease = iot_lease,
    .getactivelk = iot_getactivelk,
    .setactivelk = iot_setactivelk,
    .put = iot_put,
};

struct xlator_cbks cbks = {
    .client_destroy = gf_iot_client_destroy,
    .client_disconnect = gf_iot_disconnect_cbk,
};

struct volume_options options[] = {
    {.key = {"thread-count"},
     .type = GF_OPTION_TYPE_INT,
     .min = IOT_MIN_THREADS,
     .max = IOT_MAX_THREADS,
     .default_value = "16",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-threads"},
     /*.option = "thread-count"*/
     .description = "Number of threads in IO threads translator which "
                    "perform concurrent IO operations"

    },
    {.key = {"high-prio-threads"},
     .type = GF_OPTION_TYPE_INT,
     .min = IOT_MIN_THREADS,
     .max = IOT_MAX_THREADS,
     .default_value = "16",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-threads"},
     .description = "Max number of threads in IO threads translator which "
                    "perform high priority IO operations at a given time"

    },
    {.key = {"normal-prio-threads"},
     .type = GF_OPTION_TYPE_INT,
     .min = IOT_MIN_THREADS,
     .max = IOT_MAX_THREADS,
     .default_value = "16",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-threads"},
     .description = "Max number of threads in IO threads translator which "
                    "perform normal priority IO operations at a given time"

    },
    {.key = {"low-prio-threads"},
     .type = GF_OPTION_TYPE_INT,
     .min = IOT_MIN_THREADS,
     .max = IOT_MAX_THREADS,
     .default_value = "16",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-threads"},
     .description = "Max number of threads in IO threads translator which "
                    "perform low priority IO operations at a given time"

    },
    {.key = {"least-prio-threads"},
     .type = GF_OPTION_TYPE_INT,
     .min = IOT_MIN_THREADS,
     .max = IOT_MAX_THREADS,
     .default_value = "1",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-threads"},
     .description = "Max number of threads in IO threads translator which "
                    "perform least priority IO operations at a given time"},
    {.key = {"enable-least-priority"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = SITE_H_ENABLE_LEAST_PRIORITY,
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-threads"},
     .description = "Enable/Disable least priority"},
    {
        .key = {"idle-time"},
        .type = GF_OPTION_TYPE_INT,
        .min = 1,
        .max = 0x7fffffff,
        .default_value = "120",
    },
    {.key = {"watchdog-secs"},
     .type = GF_OPTION_TYPE_INT,
     .min = 0,
     .default_value = 0,
     .op_version = {GD_OP_VERSION_4_1_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-threads"},
     .description = "Number of seconds a queue must be stalled before "
                    "starting an 'emergency' thread."},
    {.key = {"cleanup-disconnected-reqs"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     .op_version = {GD_OP_VERSION_4_1_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC | OPT_FLAG_CLIENT_OPT,
     .tags = {"io-threads"},
     .description = "'Poison' queued requests when a client disconnects"},
    {.key = {"pass-through"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "false",
     .op_version = {GD_OP_VERSION_4_1_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC | OPT_FLAG_CLIENT_OPT,
     .tags = {"io-threads"},
     .description = "Enable/Disable io threads translator"},
    {
        .key = {NULL},
    },
};
