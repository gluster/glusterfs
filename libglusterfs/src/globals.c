/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <pthread.h>

#include "glusterfs/syncop.h"
#include "glusterfs/libglusterfs-messages.h"

const char *gf_fop_list[GF_FOP_MAXVALUE] = {
    [GF_FOP_NULL] = "NULL",
    [GF_FOP_STAT] = "STAT",
    [GF_FOP_READLINK] = "READLINK",
    [GF_FOP_MKNOD] = "MKNOD",
    [GF_FOP_MKDIR] = "MKDIR",
    [GF_FOP_UNLINK] = "UNLINK",
    [GF_FOP_RMDIR] = "RMDIR",
    [GF_FOP_SYMLINK] = "SYMLINK",
    [GF_FOP_RENAME] = "RENAME",
    [GF_FOP_LINK] = "LINK",
    [GF_FOP_TRUNCATE] = "TRUNCATE",
    [GF_FOP_OPEN] = "OPEN",
    [GF_FOP_READ] = "READ",
    [GF_FOP_WRITE] = "WRITE",
    [GF_FOP_STATFS] = "STATFS",
    [GF_FOP_FLUSH] = "FLUSH",
    [GF_FOP_FSYNC] = "FSYNC",
    [GF_FOP_SETXATTR] = "SETXATTR",
    [GF_FOP_GETXATTR] = "GETXATTR",
    [GF_FOP_REMOVEXATTR] = "REMOVEXATTR",
    [GF_FOP_OPENDIR] = "OPENDIR",
    [GF_FOP_FSYNCDIR] = "FSYNCDIR",
    [GF_FOP_ACCESS] = "ACCESS",
    [GF_FOP_CREATE] = "CREATE",
    [GF_FOP_FTRUNCATE] = "FTRUNCATE",
    [GF_FOP_FSTAT] = "FSTAT",
    [GF_FOP_LK] = "LK",
    [GF_FOP_LOOKUP] = "LOOKUP",
    [GF_FOP_READDIR] = "READDIR",
    [GF_FOP_INODELK] = "INODELK",
    [GF_FOP_FINODELK] = "FINODELK",
    [GF_FOP_ENTRYLK] = "ENTRYLK",
    [GF_FOP_FENTRYLK] = "FENTRYLK",
    [GF_FOP_XATTROP] = "XATTROP",
    [GF_FOP_FXATTROP] = "FXATTROP",
    [GF_FOP_FSETXATTR] = "FSETXATTR",
    [GF_FOP_FGETXATTR] = "FGETXATTR",
    [GF_FOP_RCHECKSUM] = "RCHECKSUM",
    [GF_FOP_SETATTR] = "SETATTR",
    [GF_FOP_FSETATTR] = "FSETATTR",
    [GF_FOP_READDIRP] = "READDIRP",
    [GF_FOP_GETSPEC] = "GETSPEC",
    [GF_FOP_FORGET] = "FORGET",
    [GF_FOP_RELEASE] = "RELEASE",
    [GF_FOP_RELEASEDIR] = "RELEASEDIR",
    [GF_FOP_FREMOVEXATTR] = "FREMOVEXATTR",
    [GF_FOP_FALLOCATE] = "FALLOCATE",
    [GF_FOP_DISCARD] = "DISCARD",
    [GF_FOP_ZEROFILL] = "ZEROFILL",
    [GF_FOP_IPC] = "IPC",
    [GF_FOP_SEEK] = "SEEK",
    [GF_FOP_LEASE] = "LEASE",
    [GF_FOP_COMPOUND] = "COMPOUND",
    [GF_FOP_GETACTIVELK] = "GETACTIVELK",
    [GF_FOP_SETACTIVELK] = "SETACTIVELK",
    [GF_FOP_PUT] = "PUT",
    [GF_FOP_ICREATE] = "ICREATE",
    [GF_FOP_NAMELINK] = "NAMELINK",
    [GF_FOP_COPY_FILE_RANGE] = "COPY_FILE_RANGE",
};

const char *gf_upcall_list[GF_UPCALL_FLAGS_MAXVALUE] = {
    [GF_UPCALL_NULL] = "NULL",
    [GF_UPCALL] = "UPCALL",
    [GF_UPCALL_CI_STAT] = "CI_IATT",
    [GF_UPCALL_CI_XATTR] = "CI_XATTR",
    [GF_UPCALL_CI_RENAME] = "CI_RENAME",
    [GF_UPCALL_CI_NLINK] = "CI_UNLINK",
    [GF_UPCALL_CI_FORGET] = "CI_FORGET",
    [GF_UPCALL_LEASE_RECALL] = "LEASE_RECALL",
};

/* THIS */

/* This global ctx is a bad hack to prevent some of the libgfapi crashes.
 * This should be removed once the patch on resource pool is accepted
 */
glusterfs_ctx_t *global_ctx = NULL;
pthread_mutex_t global_ctx_mutex = PTHREAD_MUTEX_INITIALIZER;
xlator_t global_xlator;
static int gf_global_mem_acct_enable = 1;
static pthread_once_t globals_inited = PTHREAD_ONCE_INIT;

static pthread_key_t free_key;

static __thread xlator_t *thread_xlator = NULL;
static __thread void *thread_synctask = NULL;
static __thread void *thread_leaseid = NULL;
static __thread struct syncopctx thread_syncopctx = {};
static __thread char thread_uuid_buf[GF_UUID_BUF_SIZE] = {};
static __thread char thread_lkowner_buf[GF_LKOWNER_BUF_SIZE] = {};
static __thread char thread_leaseid_buf[GF_LEASE_ID_BUF_SIZE] = {};

int
gf_global_mem_acct_enable_get(void)
{
    return gf_global_mem_acct_enable;
}

int
gf_global_mem_acct_enable_set(int val)
{
    gf_global_mem_acct_enable = val;
    return 0;
}

static struct xlator_cbks global_cbks = {
    .forget = NULL,
    .release = NULL,
    .releasedir = NULL,
    .invalidate = NULL,
    .client_destroy = NULL,
    .client_disconnect = NULL,
    .ictxmerge = NULL,
    .ictxsize = NULL,
    .fdctxsize = NULL,
};

/* This is required to get through the check in graph.c */
static struct xlator_fops global_fops = {};

static int
global_xl_reconfigure(xlator_t *this, dict_t *options)
{
    int ret = -1;
    gf_boolean_t bool_opt = _gf_false;

    /* This is not added in volume dump, hence adding the options in log
       would be helpful for debugging later */
    dict_dump_to_log(options);

    GF_OPTION_RECONF("measure-latency", bool_opt, options, bool, out);
    this->ctx->measure_latency = bool_opt;

    GF_OPTION_RECONF("metrics-dump-path", this->ctx->config.metrics_dumppath,
                     options, str, out);

    /* TODO: add more things here */
    ret = 0;
out:
    return ret;
}

static int
global_xl_init(xlator_t *this)
{
    int ret = -1;
    gf_boolean_t bool_opt = false;

    GF_OPTION_INIT("measure-latency", bool_opt, bool, out);
    this->ctx->measure_latency = bool_opt;

    GF_OPTION_INIT("metrics-dump-path", this->ctx->config.metrics_dumppath, str,
                   out);

    ret = 0;

out:
    return ret;
}

static void
global_xl_fini(xlator_t *this)
{
    return;
}

struct volume_options global_xl_options[] = {
    {.key = {"measure-latency"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "no",
     .op_version = {GD_OP_VERSION_4_0_0},
     .flags = OPT_FLAG_SETTABLE,
     .tags = {"global", "context"},
     .description = "Use this option to toggle measuring latency"},
    {.key = {"metrics-dump-path"},
     .type = GF_OPTION_TYPE_STR,
     .default_value = "{{gluster_workdir}}/metrics",
     .op_version = {GD_OP_VERSION_4_0_0},
     .flags = OPT_FLAG_SETTABLE,
     .tags = {"global", "context"},
     .description = "Use this option to set the metrics dump path"},

    {
        .key = {NULL},
    },
};

static volume_opt_list_t global_xl_opt_list;

void
glusterfs_this_init()
{
    global_xlator.name = "glusterfs";
    global_xlator.type = GF_GLOBAL_XLATOR_NAME;
    global_xlator.cbks = &global_cbks;
    global_xlator.fops = &global_fops;
    global_xlator.reconfigure = global_xl_reconfigure;
    global_xlator.init = global_xl_init;
    global_xlator.fini = global_xl_fini;

    INIT_LIST_HEAD(&global_xlator.volume_options);
    INIT_LIST_HEAD(&global_xl_opt_list.list);
    global_xl_opt_list.given_opt = global_xl_options;

    list_add_tail(&global_xl_opt_list.list, &global_xlator.volume_options);
}

xlator_t **
__glusterfs_this_location()
{
    xlator_t **this_location;

    this_location = &thread_xlator;
    if (*this_location == NULL) {
        thread_xlator = &global_xlator;
    }

    return this_location;
}

xlator_t *
glusterfs_this_get()
{
    return *__glusterfs_this_location();
}

void
glusterfs_this_set(xlator_t *this)
{
    thread_xlator = this;
}

/* SYNCOPCTX */

void *
syncopctx_getctx()
{
    return &thread_syncopctx;
}

/* SYNCTASK */

void *
synctask_get()
{
    return thread_synctask;
}

void
synctask_set(void *synctask)
{
    thread_synctask = synctask;
}

// UUID_BUFFER

char *
glusterfs_uuid_buf_get()
{
    return thread_uuid_buf;
}

/* LKOWNER_BUFFER */

char *
glusterfs_lkowner_buf_get()
{
    return thread_lkowner_buf;
}

/* Leaseid buffer */

char *
glusterfs_leaseid_buf_get()
{
    char *buf = NULL;

    buf = thread_leaseid;
    if (buf == NULL) {
        buf = thread_leaseid_buf;
        thread_leaseid = buf;
    }

    return buf;
}

char *
glusterfs_leaseid_exist()
{
    return thread_leaseid;
}

static void
glusterfs_cleanup(void *ptr)
{
    if (thread_syncopctx.groups != NULL) {
        GF_FREE(thread_syncopctx.groups);
    }

    mem_pool_thread_destructor(NULL);
}

void
gf_thread_needs_cleanup(void)
{
    /* The value stored in free_key TLS is not really used for anything, but
     * pthread implementation doesn't call the TLS destruction function unless
     * it's != NULL. This function must be called whenever something is
     * allocated for this thread so that glusterfs_cleanup() will be called
     * and resources can be released. */
    (void)pthread_setspecific(free_key, (void *)1);
}

static void
gf_globals_init_once()
{
    int ret = 0;

    glusterfs_this_init();

    /* This is needed only to cleanup the potential allocation of
     * thread_syncopctx.groups. */
    ret = pthread_key_create(&free_key, glusterfs_cleanup);
    if (ret != 0) {
        gf_msg("", GF_LOG_ERROR, ret, LG_MSG_PTHREAD_KEY_CREATE_FAILED,
               "failed to create the pthread key");

        gf_msg("", GF_LOG_CRITICAL, 0, LG_MSG_GLOBAL_INIT_FAILED,
               "Exiting as global initialization failed");

        exit(ret);
    }
}

int
glusterfs_globals_init(glusterfs_ctx_t *ctx)
{
    int ret = 0;

    gf_log_globals_init(ctx, GF_LOG_INFO);

    ret = pthread_once(&globals_inited, gf_globals_init_once);

    if (ret)
        gf_msg("", GF_LOG_CRITICAL, ret, LG_MSG_PTHREAD_FAILED,
               "pthread_once failed");

    return ret;
}
