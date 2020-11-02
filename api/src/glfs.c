/*
  Copyright (c) 2012-2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/*
  TODO:
  - set proper pid/lk_owner to call frames (currently buried in syncop)
  - fix logging.c/h to store logfp and loglevel in glusterfs_ctx_t and
    reach it via THIS.
  - update syncop functions to accept/return xdata. ???
  - protocol/client to reconnect immediately after portmap disconnect.
  - handle SEEK_END failure in _lseek()
  - handle umask (per filesystem?)
  - make itables LRU based
  - 0-copy for readv/writev
  - reconcile the open/creat mess
*/

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#ifdef GF_LINUX_HOST_OS
#include <sys/prctl.h>
#endif

#include <glusterfs/glusterfs.h>
#include <glusterfs/logging.h>
#include <glusterfs/stack.h>
#include <glusterfs/gf-event.h>
#include "glfs-mem-types.h"
#include <glusterfs/common-utils.h>
#include <glusterfs/syncop.h>
#include <glusterfs/call-stub.h>
#include <glusterfs/hashfn.h>
#include "rpc-clnt.h"
#include <glusterfs/statedump.h>
#include <glusterfs/syscall.h>

#include "gfapi-messages.h"
#include "glfs.h"
#include "glfs-internal.h"

static gf_boolean_t
vol_assigned(cmd_args_t *args)
{
    return args->volfile || args->volfile_server;
}

static int
glusterfs_ctx_defaults_init(glusterfs_ctx_t *ctx)
{
    call_pool_t *pool = NULL;
    int ret = -1;

    if (!ctx) {
        goto err;
    }

    ret = xlator_mem_acct_init(THIS, glfs_mt_end + 1);
    if (ret != 0) {
        gf_smsg(THIS->name, GF_LOG_ERROR, ENOMEM, API_MSG_MEM_ACCT_INIT_FAILED,
                NULL);
        return ret;
    }

    /* reset ret to -1 so that we don't need to explicitly
     * set it in all error paths before "goto err"
     */

    ret = -1;

    ctx->process_uuid = generate_glusterfs_ctx_id();
    if (!ctx->process_uuid) {
        goto err;
    }

    ctx->page_size = 128 * GF_UNIT_KB;

    ctx->iobuf_pool = iobuf_pool_new();
    if (!ctx->iobuf_pool) {
        goto err;
    }

    ctx->event_pool = gf_event_pool_new(DEFAULT_EVENT_POOL_SIZE,
                                        STARTING_EVENT_THREADS);
    if (!ctx->event_pool) {
        goto err;
    }

    ctx->env = syncenv_new(0, 0, 0);
    if (!ctx->env) {
        goto err;
    }

    pool = GF_CALLOC(1, sizeof(call_pool_t), glfs_mt_call_pool_t);
    if (!pool) {
        goto err;
    }

    /* frame_mem_pool size 112 * 4k */
    pool->frame_mem_pool = mem_pool_new(call_frame_t, 4096);
    if (!pool->frame_mem_pool) {
        goto err;
    }
    /* stack_mem_pool size 256 * 1024 */
    pool->stack_mem_pool = mem_pool_new(call_stack_t, 1024);
    if (!pool->stack_mem_pool) {
        goto err;
    }

    ctx->stub_mem_pool = mem_pool_new(call_stub_t, 1024);
    if (!ctx->stub_mem_pool) {
        goto err;
    }

    ctx->dict_pool = mem_pool_new(dict_t, GF_MEMPOOL_COUNT_OF_DICT_T);
    if (!ctx->dict_pool)
        goto err;

    ctx->dict_pair_pool = mem_pool_new(data_pair_t,
                                       GF_MEMPOOL_COUNT_OF_DATA_PAIR_T);
    if (!ctx->dict_pair_pool)
        goto err;

    ctx->dict_data_pool = mem_pool_new(data_t, GF_MEMPOOL_COUNT_OF_DATA_T);
    if (!ctx->dict_data_pool)
        goto err;

    ctx->logbuf_pool = mem_pool_new(log_buf_t, GF_MEMPOOL_COUNT_OF_LRU_BUF_T);
    if (!ctx->logbuf_pool)
        goto err;

    INIT_LIST_HEAD(&pool->all_frames);
    INIT_LIST_HEAD(&ctx->cmd_args.xlator_options);
    INIT_LIST_HEAD(&ctx->cmd_args.volfile_servers);

    LOCK_INIT(&pool->lock);
    ctx->pool = pool;

    ret = 0;
err:
    if (ret && pool) {
        if (pool->frame_mem_pool)
            mem_pool_destroy(pool->frame_mem_pool);
        if (pool->stack_mem_pool)
            mem_pool_destroy(pool->stack_mem_pool);
        GF_FREE(pool);
    }

    if (ret && ctx) {
        if (ctx->stub_mem_pool)
            mem_pool_destroy(ctx->stub_mem_pool);
        if (ctx->dict_pool)
            mem_pool_destroy(ctx->dict_pool);
        if (ctx->dict_data_pool)
            mem_pool_destroy(ctx->dict_data_pool);
        if (ctx->dict_pair_pool)
            mem_pool_destroy(ctx->dict_pair_pool);
        if (ctx->logbuf_pool)
            mem_pool_destroy(ctx->logbuf_pool);
    }

    return ret;
}

static int
create_primary(struct glfs *fs)
{
    int ret = 0;
    xlator_t *primary = NULL;

    primary = GF_CALLOC(1, sizeof(*primary), glfs_mt_xlator_t);
    if (!primary)
        goto err;

    primary->name = gf_strdup("gfapi");
    if (!primary->name)
        goto err;

    if (xlator_set_type(primary, "mount/api") == -1) {
        gf_smsg("glfs", GF_LOG_ERROR, 0, API_MSG_PRIMARY_XLATOR_INIT_FAILED,
                "name=%s", fs->volname, NULL);
        goto err;
    }

    primary->ctx = fs->ctx;
    primary->private = fs;
    primary->options = dict_new();
    if (!primary->options)
        goto err;

    ret = xlator_init(primary);
    if (ret) {
        gf_smsg("glfs", GF_LOG_ERROR, 0, API_MSG_GFAPI_XLATOR_INIT_FAILED,
                NULL);
        goto err;
    }

    fs->ctx->primary = primary;
    THIS = primary;

    return 0;

err:
    if (primary) {
        xlator_destroy(primary);
    }

    return -1;
}

static FILE *
get_volfp(struct glfs *fs)
{
    cmd_args_t *cmd_args = NULL;
    FILE *specfp = NULL;

    cmd_args = &fs->ctx->cmd_args;

    if ((specfp = fopen(cmd_args->volfile, "r")) == NULL) {
        gf_smsg("glfs", GF_LOG_ERROR, errno, API_MSG_VOLFILE_OPEN_FAILED,
                "file=%s", cmd_args->volfile, "err=%s", strerror(errno), NULL);
        return NULL;
    }

    gf_msg_debug("glfs", 0, "loading volume file %s", cmd_args->volfile);

    return specfp;
}

int
glfs_volumes_init(struct glfs *fs)
{
    FILE *fp = NULL;
    cmd_args_t *cmd_args = NULL;
    int ret = 0;

    cmd_args = &fs->ctx->cmd_args;

    if (!vol_assigned(cmd_args))
        return -1;

    if (sys_access(SECURE_ACCESS_FILE, F_OK) == 0) {
        fs->ctx->secure_mgmt = 1;
        fs->ctx->ssl_cert_depth = glusterfs_read_secure_access_file();
    }

    if (cmd_args->volfile_server) {
        ret = glfs_mgmt_init(fs);
        goto out;
    }

    fp = get_volfp(fs);

    if (!fp) {
        gf_smsg("glfs", GF_LOG_ERROR, ENOENT, API_MSG_VOL_SPEC_FILE_ERROR,
                NULL);
        ret = -1;
        goto out;
    }

    ret = glfs_process_volfp(fs, fp);
    if (ret)
        goto out;

out:
    return ret;
}

///////////////////////////////////////////////////////////////////////////////

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_set_xlator_option, 3.4.0)
int
pub_glfs_set_xlator_option(struct glfs *fs, const char *xlator, const char *key,
                           const char *value)
{
    xlator_cmdline_option_t *option = NULL;

    DECLARE_OLD_THIS;
    __GLFS_ENTRY_VALIDATE_FS(fs, invalid_fs);

    option = GF_CALLOC(1, sizeof(*option), glfs_mt_xlator_cmdline_option_t);
    if (!option)
        goto enomem;

    INIT_LIST_HEAD(&option->cmd_args);

    option->volume = gf_strdup(xlator);
    if (!option->volume)
        goto enomem;
    option->key = gf_strdup(key);
    if (!option->key)
        goto enomem;
    option->value = gf_strdup(value);
    if (!option->value)
        goto enomem;

    list_add(&option->cmd_args, &fs->ctx->cmd_args.xlator_options);

    __GLFS_EXIT_FS;

    return 0;
enomem:
    errno = ENOMEM;

    if (!option) {
        __GLFS_EXIT_FS;
        return -1;
    }

    GF_FREE(option->volume);
    GF_FREE(option->key);
    GF_FREE(option->value);
    GF_FREE(option);

    __GLFS_EXIT_FS;

invalid_fs:
    return -1;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_unset_volfile_server, 3.5.1)
int
pub_glfs_unset_volfile_server(struct glfs *fs, const char *transport,
                              const char *host, const int port)
{
    cmd_args_t *cmd_args = NULL;
    server_cmdline_t *server = NULL;
    server_cmdline_t *tmp = NULL;
    char *transport_val = NULL;
    int port_val = 0;
    int ret = -1;

    if (!fs || !host) {
        errno = EINVAL;
        return ret;
    }

    DECLARE_OLD_THIS;
    __GLFS_ENTRY_VALIDATE_FS(fs, invalid_fs);

    cmd_args = &fs->ctx->cmd_args;

    if (transport) {
        transport_val = gf_strdup(transport);
    } else {
        transport_val = gf_strdup(GF_DEFAULT_VOLFILE_TRANSPORT);
    }

    if (!transport_val) {
        errno = ENOMEM;
        goto out;
    }

    if (port) {
        port_val = port;
    } else {
        port_val = GF_DEFAULT_BASE_PORT;
    }

    list_for_each_entry_safe(server, tmp, &cmd_args->curr_server->list, list)
    {
        if (!server->volfile_server || !server->transport)
            continue;
        if ((!strcmp(server->volfile_server, host) &&
             !strcmp(server->transport, transport_val) &&
             (server->port == port_val))) {
            list_del(&server->list);
            ret = 0;
            goto out;
        }
    }

out:
    GF_FREE(transport_val);
    __GLFS_EXIT_FS;

invalid_fs:
    return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_set_volfile_server, 3.4.0)
int
pub_glfs_set_volfile_server(struct glfs *fs, const char *transport,
                            const char *host, int port)
{
    cmd_args_t *cmd_args = NULL;
    int ret = -1;
    char *server_host = NULL;
    char *server_transport = NULL;

    if (!fs || !host) {
        errno = EINVAL;
        return ret;
    }

    DECLARE_OLD_THIS;
    __GLFS_ENTRY_VALIDATE_FS(fs, invalid_fs);

    cmd_args = &fs->ctx->cmd_args;
    cmd_args->max_connect_attempts = 1;

    server_host = gf_strdup(host);
    if (!server_host) {
        errno = ENOMEM;
        goto out;
    }

    if (transport) {
        /* volfile fetch support over tcp|unix only */
        if (!strcmp(transport, "tcp") || !strcmp(transport, "unix")) {
            server_transport = gf_strdup(transport);
        } else if (!strcmp(transport, "rdma")) {
            server_transport = gf_strdup(GF_DEFAULT_VOLFILE_TRANSPORT);
            gf_smsg("glfs", GF_LOG_WARNING, EINVAL, API_MSG_TRANS_RDMA_DEP,
                    NULL);
        } else {
            gf_smsg("glfs", GF_LOG_TRACE, EINVAL, API_MSG_TRANS_NOT_SUPPORTED,
                    "transport=%s", transport, NULL);
            goto out;
        }
    } else {
        server_transport = gf_strdup(GF_DEFAULT_VOLFILE_TRANSPORT);
    }

    if (!server_transport) {
        errno = ENOMEM;
        goto out;
    }

    if (!port) {
        port = GF_DEFAULT_BASE_PORT;
    }

    if (!strcmp(server_transport, "unix")) {
        port = 0;
    }

    ret = gf_set_volfile_server_common(cmd_args, server_host, server_transport,
                                       port);
    if (ret) {
        gf_log("glfs", GF_LOG_ERROR, "failed to set volfile server: %s",
               strerror(errno));
    }

out:
    if (server_host) {
        GF_FREE(server_host);
    }

    if (server_transport) {
        GF_FREE(server_transport);
    }

    __GLFS_EXIT_FS;

invalid_fs:
    return ret;
}

/* *
 * Used to free the arguments allocated by glfs_set_volfile_server()
 */
static void
glfs_free_volfile_servers(cmd_args_t *cmd_args)
{
    server_cmdline_t *server = NULL;
    server_cmdline_t *tmp = NULL;

    GF_VALIDATE_OR_GOTO(THIS->name, cmd_args, out);

    list_for_each_entry_safe(server, tmp, &cmd_args->volfile_servers, list)
    {
        list_del_init(&server->list);
        GF_FREE(server->volfile_server);
        GF_FREE(server->transport);
        GF_FREE(server);
    }
    cmd_args->curr_server = NULL;
out:
    return;
}

static void
glfs_free_xlator_options(cmd_args_t *cmd_args)
{
    xlator_cmdline_option_t *xo = NULL;
    xlator_cmdline_option_t *tmp_xo = NULL;

    if (!&(cmd_args->xlator_options))
        return;

    list_for_each_entry_safe(xo, tmp_xo, &cmd_args->xlator_options, cmd_args)
    {
        list_del_init(&xo->cmd_args);
        GF_FREE(xo->volume);
        GF_FREE(xo->key);
        GF_FREE(xo->value);
        GF_FREE(xo);
    }
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_setfsuid, 3.4.2)
int
pub_glfs_setfsuid(uid_t fsuid)
{
    /* TODO:
     * - Set the THIS and restore it appropriately
     */
    return syncopctx_setfsuid(&fsuid);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_setfsgid, 3.4.2)
int
pub_glfs_setfsgid(gid_t fsgid)
{
    /* TODO:
     * - Set the THIS and restore it appropriately
     */
    return syncopctx_setfsgid(&fsgid);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_setfsgroups, 3.4.2)
int
pub_glfs_setfsgroups(size_t size, const gid_t *list)
{
    /* TODO:
     * - Set the THIS and restore it appropriately
     */
    return syncopctx_setfsgroups(size, list);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_setfsleaseid, 4.0.0)
int
pub_glfs_setfsleaseid(glfs_leaseid_t leaseid)
{
    int ret = -1;
    char *gleaseid = NULL;

    gleaseid = gf_leaseid_get();
    if (gleaseid) {
        if (leaseid)
            memcpy(gleaseid, leaseid, LEASE_ID_SIZE);
        else /* reset leaseid */
            memset(gleaseid, 0, LEASE_ID_SIZE);
        ret = 0;
    }

    if (ret)
        gf_log("glfs", GF_LOG_ERROR, "failed to set leaseid: %s",
               strerror(errno));
    return ret;
}

int
get_fop_attr_glfd(dict_t **fop_attr, struct glfs_fd *glfd)
{
    char *leaseid = NULL;
    int ret = 0;
    gf_boolean_t dict_create = _gf_false;

    leaseid = GF_MALLOC(LEASE_ID_SIZE, gf_common_mt_char);
    GF_CHECK_ALLOC_AND_LOG("gfapi", leaseid, ret, "lease id alloc failed", out);
    memcpy(leaseid, glfd->lease_id, LEASE_ID_SIZE);
    if (*fop_attr == NULL) {
        *fop_attr = dict_new();
        dict_create = _gf_true;
    }
    GF_CHECK_ALLOC_AND_LOG("gfapi", *fop_attr, ret, "dict_new failed", out);
    ret = dict_set_bin(*fop_attr, "lease-id", leaseid, LEASE_ID_SIZE);
out:
    if (ret) {
        GF_FREE(leaseid);
        if (dict_create) {
            if (*fop_attr)
                dict_unref(*fop_attr);
            *fop_attr = NULL;
        }
    }
    return ret;
}

int
set_fop_attr_glfd(struct glfs_fd *glfd)
{
    char *lease_id = NULL;
    int ret = -1;

    lease_id = gf_existing_leaseid();
    if (lease_id) {
        memcpy(glfd->lease_id, lease_id, LEASE_ID_SIZE);
        ret = 0;
    }
    return ret;
}

int
get_fop_attr_thrd_key(dict_t **fop_attr)
{
    char *existing_leaseid = NULL, *leaseid = NULL;
    int ret = 0;
    gf_boolean_t dict_create = _gf_false;

    existing_leaseid = gf_existing_leaseid();
    if (existing_leaseid) {
        leaseid = GF_MALLOC(LEASE_ID_SIZE, gf_common_mt_char);
        GF_CHECK_ALLOC_AND_LOG("gfapi", leaseid, ret, "lease id alloc failed",
                               out);
        memcpy(leaseid, existing_leaseid, LEASE_ID_SIZE);
        if (*fop_attr == NULL) {
            *fop_attr = dict_new();
            dict_create = _gf_true;
        }
        GF_CHECK_ALLOC_AND_LOG("gfapi", *fop_attr, ret, "dict_new failed", out);
        ret = dict_set_bin(*fop_attr, "lease-id", leaseid, LEASE_ID_SIZE);
    }

out:
    if (ret) {
        GF_FREE(leaseid);
        if (dict_create) {
            if (*fop_attr)
                dict_unref(*fop_attr);
            *fop_attr = NULL;
        }
    }
    return ret;
}

void
unset_fop_attr(dict_t **fop_attr)
{
    char *lease_id = NULL;
    lease_id = gf_existing_leaseid();
    if (lease_id)
        memset(lease_id, 0, LEASE_ID_SIZE);
    if (*fop_attr) {
        dict_unref(*fop_attr);
        *fop_attr = NULL;
    }
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_from_glfd, 3.4.0)
struct glfs *
pub_glfs_from_glfd(struct glfs_fd *glfd)
{
    if (glfd == NULL) {
        errno = EBADF;
        return NULL;
    }

    return glfd->fs;
}

static void
glfs_fd_destroy(struct glfs_fd *glfd)
{
    if (!glfd)
        return;

    glfs_lock(glfd->fs, _gf_true);
    {
        list_del_init(&glfd->openfds);
    }
    glfs_unlock(glfd->fs);

    if (glfd->fd) {
        fd_unref(glfd->fd);
        glfd->fd = NULL;
    }

    GF_FREE(glfd->readdirbuf);

    GF_FREE(glfd);
}

struct glfs_fd *
glfs_fd_new(struct glfs *fs)
{
    struct glfs_fd *glfd = NULL;

    glfd = GF_CALLOC(1, sizeof(*glfd), glfs_mt_glfs_fd_t);
    if (!glfd)
        return NULL;

    glfd->fs = fs;

    INIT_LIST_HEAD(&glfd->openfds);

    GF_REF_INIT(glfd, glfs_fd_destroy);

    return glfd;
}

void
glfs_fd_bind(struct glfs_fd *glfd)
{
    struct glfs *fs = NULL;

    fs = glfd->fs;

    glfs_lock(fs, _gf_true);
    {
        list_add_tail(&glfd->openfds, &fs->openfds);
    }
    glfs_unlock(fs);
}

static void *
glfs_poller(void *data)
{
    struct glfs *fs = NULL;

    fs = data;

    gf_event_dispatch(fs->ctx->event_pool);

    return NULL;
}

static struct glfs *
glfs_new_fs(const char *volname)
{
    struct glfs *fs = NULL;

    fs = CALLOC(1, sizeof(*fs));
    if (!fs)
        return NULL;

    INIT_LIST_HEAD(&fs->openfds);
    INIT_LIST_HEAD(&fs->upcall_list);
    INIT_LIST_HEAD(&fs->waitq);

    PTHREAD_MUTEX_INIT(&fs->mutex, NULL, fs->pthread_flags, GLFS_INIT_MUTEX,
                       err);

    PTHREAD_COND_INIT(&fs->cond, NULL, fs->pthread_flags, GLFS_INIT_COND, err);

    PTHREAD_COND_INIT(&fs->child_down_cond, NULL, fs->pthread_flags,
                      GLFS_INIT_COND_CHILD, err);

    PTHREAD_MUTEX_INIT(&fs->upcall_list_mutex, NULL, fs->pthread_flags,
                       GLFS_INIT_MUTEX_UPCALL, err);

    fs->volname = strdup(volname);
    if (!fs->volname)
        goto err;

    fs->pin_refcnt = 0;
    fs->upcall_events = 0;
    fs->up_cbk = NULL;
    fs->up_data = NULL;

    return fs;

err:
    glfs_free_from_ctx(fs);
    return NULL;
}

extern xlator_t global_xlator;
extern glusterfs_ctx_t *global_ctx;
extern pthread_mutex_t global_ctx_mutex;

static int
glfs_init_global_ctx()
{
    int ret = 0;
    glusterfs_ctx_t *ctx = NULL;

    pthread_mutex_lock(&global_ctx_mutex);
    {
        if (global_xlator.ctx)
            goto unlock;

        ctx = glusterfs_ctx_new();
        if (!ctx) {
            ret = -1;
            goto unlock;
        }

        gf_log_globals_init(ctx, GF_LOG_NONE);

        global_ctx = ctx;
        global_xlator.ctx = global_ctx;

        ret = glusterfs_ctx_defaults_init(ctx);
        if (ret) {
            global_ctx = NULL;
            global_xlator.ctx = NULL;
            goto unlock;
        }
    }
unlock:
    pthread_mutex_unlock(&global_ctx_mutex);

    if (ret)
        FREE(ctx);

    return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_new, 3.4.0)
struct glfs *
pub_glfs_new(const char *volname)
{
    if (!volname) {
        errno = EINVAL;
        return NULL;
    }

    struct glfs *fs = NULL;
    int i = 0;
    int ret = -1;
    glusterfs_ctx_t *ctx = NULL;
    xlator_t *old_THIS = NULL;
    char pname[16] = "";
    char msg[32] = "";

    if (volname[0] == '/' || volname[0] == '-') {
        if (strncmp(volname, "/snaps/", 7) == 0) {
            goto label;
        }
        errno = EINVAL;
        return NULL;
    }

    for (i = 0; i < strlen(volname); i++) {
        if (!isalnum(volname[i]) && (volname[i] != '_') &&
            (volname[i] != '-')) {
            errno = EINVAL;
            return NULL;
        }
    }

label:
    /*
     * Do this as soon as possible in case something else depends on
     * pool allocations.
     */
    mem_pools_init();

    fs = glfs_new_fs(volname);
    if (!fs)
        goto out;

    ctx = glusterfs_ctx_new();
    if (!ctx)
        goto out;

    /* first globals init, for gf_mem_acct_enable_set () */

    ret = glusterfs_globals_init(ctx);
    if (ret)
        goto out;

    old_THIS = THIS;
    ret = glfs_init_global_ctx();
    if (ret)
        goto out;

    /* then ctx_defaults_init, for xlator_mem_acct_init(THIS) */

    ret = glusterfs_ctx_defaults_init(ctx);
    if (ret)
        goto out;

    fs->ctx = ctx;
    fs->ctx->process_mode = GF_CLIENT_PROCESS;

    ret = glfs_set_logging(fs, "/dev/null", 0);
    if (ret)
        goto out;

    fs->ctx->cmd_args.volfile_id = gf_strdup(volname);
    if (!(fs->ctx->cmd_args.volfile_id)) {
        ret = -1;
        goto out;
    }

    ret = -1;
#ifdef GF_LINUX_HOST_OS
    ret = prctl(PR_GET_NAME, (unsigned long)pname, 0, 0, 0);
#endif
    if (ret)
        fs->ctx->cmd_args.process_name = gf_strdup("gfapi");
    else {
        snprintf(msg, sizeof(msg), "gfapi.%s", pname);
        fs->ctx->cmd_args.process_name = gf_strdup(msg);
    }
    ret = 0;

out:
    if (ret) {
        if (fs) {
            glfs_fini(fs);
            fs = NULL;
        } else {
            /* glfs_fini() calls mem_pools_fini() too */
            mem_pools_fini();
        }
    }

    if (old_THIS)
        THIS = old_THIS;

    return fs;
}

GFAPI_SYMVER_PRIVATE_DEFAULT(glfs_new_from_ctx, 3.7.0)
struct glfs *
priv_glfs_new_from_ctx(glusterfs_ctx_t *ctx)
{
    struct glfs *fs = NULL;

    if (!ctx)
        goto out;

    fs = glfs_new_fs("");
    if (!fs)
        goto out;

    fs->ctx = ctx;

out:
    return fs;
}

GFAPI_SYMVER_PRIVATE_DEFAULT(glfs_free_from_ctx, 3.7.0)
void
priv_glfs_free_from_ctx(struct glfs *fs)
{
    upcall_entry *u_list = NULL;
    upcall_entry *tmp = NULL;

    if (!fs)
        return;

    /* cleanup upcall structures */
    list_for_each_entry_safe(u_list, tmp, &fs->upcall_list, upcall_list)
    {
        list_del_init(&u_list->upcall_list);
        GF_FREE(u_list->upcall_data.data);
        GF_FREE(u_list);
    }

    PTHREAD_MUTEX_DESTROY(&fs->mutex, fs->pthread_flags, GLFS_INIT_MUTEX);

    PTHREAD_COND_DESTROY(&fs->cond, fs->pthread_flags, GLFS_INIT_COND);

    PTHREAD_COND_DESTROY(&fs->child_down_cond, fs->pthread_flags,
                         GLFS_INIT_COND_CHILD);

    PTHREAD_MUTEX_DESTROY(&fs->upcall_list_mutex, fs->pthread_flags,
                          GLFS_INIT_MUTEX_UPCALL);

    if (fs->oldvolfile)
        FREE(fs->oldvolfile);

    FREE(fs->volname);

    FREE(fs);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_set_volfile, 3.4.0)
int
pub_glfs_set_volfile(struct glfs *fs, const char *volfile)
{
    cmd_args_t *cmd_args = NULL;

    cmd_args = &fs->ctx->cmd_args;

    if (vol_assigned(cmd_args))
        return -1;

    cmd_args->volfile = gf_strdup(volfile);
    if (!cmd_args->volfile)
        return -1;
    return 0;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_set_logging, 3.4.0)
int
pub_glfs_set_logging(struct glfs *fs, const char *logfile, int loglevel)
{
    int ret = -1;
    char *tmplog = NULL;

    DECLARE_OLD_THIS;
    __GLFS_ENTRY_VALIDATE_FS(fs, invalid_fs);

    if (!logfile) {
        ret = gf_set_log_file_path(&fs->ctx->cmd_args, fs->ctx);
        if (ret)
            goto out;
        tmplog = fs->ctx->cmd_args.log_file;
    } else {
        tmplog = (char *)logfile;
    }

    /* finish log set parameters before init */
    if (loglevel >= 0)
        gf_log_set_loglevel(fs->ctx, loglevel);

    ret = gf_log_init(fs->ctx, tmplog, NULL);
    if (ret)
        goto out;

    ret = gf_log_inject_timer_event(fs->ctx);
    if (ret)
        goto out;

out:
    __GLFS_EXIT_FS;

invalid_fs:
    return ret;
}

int
glfs_init_wait(struct glfs *fs)
{
    int ret = -1;

    /* Always a top-down call, use glfs_lock() */
    glfs_lock(fs, _gf_true);
    {
        while (!fs->init)
            pthread_cond_wait(&fs->cond, &fs->mutex);
        ret = fs->ret;
        errno = fs->err;
    }
    glfs_unlock(fs);

    return ret;
}

GFAPI_SYMVER_PRIVATE_DEFAULT(glfs_init_done, 3.4.0)
void
priv_glfs_init_done(struct glfs *fs, int ret)
{
    glfs_init_cbk init_cbk;

    if (!fs) {
        gf_smsg("glfs", GF_LOG_ERROR, EINVAL, API_MSG_GLFS_FSOBJ_NULL, NULL);
        goto out;
    }

    init_cbk = fs->init_cbk;

    /* Always a bottom-up call, use mutex_lock() */
    pthread_mutex_lock(&fs->mutex);
    {
        fs->init = 1;
        fs->ret = ret;
        fs->err = errno;

        if (!init_cbk)
            pthread_cond_broadcast(&fs->cond);
    }
    pthread_mutex_unlock(&fs->mutex);

    if (init_cbk)
        init_cbk(fs, ret);
out:
    return;
}

int
glfs_init_common(struct glfs *fs)
{
    int ret = -1;

    ret = create_primary(fs);
    if (ret)
        return ret;

    ret = gf_thread_create(&fs->poller, NULL, glfs_poller, fs, "glfspoll");
    if (ret)
        return ret;

    ret = glfs_volumes_init(fs);
    if (ret)
        return ret;

    fs->dev_id = gf_dm_hashfn(fs->volname, strlen(fs->volname));
    return ret;
}

int
glfs_init_async(struct glfs *fs, glfs_init_cbk cbk)
{
    int ret = -1;

    if (!fs || !fs->ctx) {
        gf_smsg("glfs", GF_LOG_ERROR, EINVAL, API_MSG_FS_NOT_INIT, NULL);
        errno = EINVAL;
        return ret;
    }

    fs->init_cbk = cbk;

    ret = glfs_init_common(fs);

    return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_init, 3.4.0)
int
pub_glfs_init(struct glfs *fs)
{
    int ret = -1;

    DECLARE_OLD_THIS;

    if (!fs || !fs->ctx) {
        gf_smsg("glfs", GF_LOG_ERROR, EINVAL, API_MSG_FS_NOT_INIT, NULL);
        errno = EINVAL;
        return ret;
    }

    __GLFS_ENTRY_VALIDATE_FS(fs, invalid_fs);

    ret = glfs_init_common(fs);
    if (ret)
        goto out;

    ret = glfs_init_wait(fs);
out:
    __GLFS_EXIT_FS;

    /* Set the initial current working directory to "/" */
    if (ret >= 0) {
        ret = glfs_chdir(fs, "/");
    }

invalid_fs:
    return ret;
}

static int
glusterfs_ctx_destroy(glusterfs_ctx_t *ctx)
{
    call_pool_t *pool = NULL;
    int ret = 0;
    glusterfs_graph_t *trav_graph = NULL;
    glusterfs_graph_t *tmp = NULL;

    if (ctx == NULL)
        return 0;

    if (ctx->cmd_args.curr_server)
        glfs_free_volfile_servers(&ctx->cmd_args);

    glfs_free_xlator_options(&ctx->cmd_args);

    /* For all the graphs, crawl through the xlator_t structs and free
     * all its members except for the mem_acct member,
     * as GF_FREE will be referencing it.
     */
    list_for_each_entry_safe(trav_graph, tmp, &ctx->graphs, list)
    {
        xlator_tree_free_members(trav_graph->first);
    }

    /* Free the memory pool */
    if (ctx->stub_mem_pool)
        mem_pool_destroy(ctx->stub_mem_pool);
    if (ctx->dict_pool)
        mem_pool_destroy(ctx->dict_pool);
    if (ctx->dict_data_pool)
        mem_pool_destroy(ctx->dict_data_pool);
    if (ctx->dict_pair_pool)
        mem_pool_destroy(ctx->dict_pair_pool);
    if (ctx->logbuf_pool)
        mem_pool_destroy(ctx->logbuf_pool);

    pool = ctx->pool;
    if (pool) {
        if (pool->frame_mem_pool)
            mem_pool_destroy(pool->frame_mem_pool);
        if (pool->stack_mem_pool)
            mem_pool_destroy(pool->stack_mem_pool);
        LOCK_DESTROY(&pool->lock);
        GF_FREE(pool);
    }

    /* Free the event pool */
    ret = gf_event_pool_destroy(ctx->event_pool);

    /* Free the iobuf pool */
    iobuf_pool_destroy(ctx->iobuf_pool);

    GF_FREE(ctx->process_uuid);
    GF_FREE(ctx->cmd_args.volfile_id);
    GF_FREE(ctx->cmd_args.process_name);

    LOCK_DESTROY(&ctx->lock);
    pthread_mutex_destroy(&ctx->notify_lock);
    pthread_cond_destroy(&ctx->notify_cond);

    /* Free all the graph structs and its containing xlator_t structs
     * from this point there should be no reference to GF_FREE/GF_CALLOC
     * as it will try to access mem_acct and the below function would
     * have freed the same.
     */
    list_for_each_entry_safe(trav_graph, tmp, &ctx->graphs, list)
    {
        glusterfs_graph_destroy_residual(trav_graph);
    }

    GF_FREE(ctx->statedump_path);
    FREE(ctx);

    return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fini, 3.4.0)
int
pub_glfs_fini(struct glfs *fs)
{
    int ret = -1;
    int countdown = 100;
    xlator_t *subvol = NULL;
    glusterfs_ctx_t *ctx = NULL;
    glusterfs_graph_t *graph = NULL;
    call_pool_t *call_pool = NULL;
    int fs_init = 0;
    int err = -1;
    struct synctask *waittask = NULL;

    DECLARE_OLD_THIS;

    if (!fs) {
        errno = EINVAL;
        goto invalid_fs;
    }

    ctx = fs->ctx;
    if (!ctx) {
        goto free_fs;
    }

    THIS = fs->ctx->primary;

    if (ctx->mgmt) {
        rpc_clnt_disable(ctx->mgmt);
    }

    call_pool = fs->ctx->pool;

    /* Wake up any suspended synctasks */
    while (!list_empty(&fs->waitq)) {
        waittask = list_entry(fs->waitq.next, struct synctask, waitq);
        list_del_init(&waittask->waitq);
        synctask_wake(waittask);
    }

    while (countdown--) {
        /* give some time for background frames to finish */
        pthread_mutex_lock(&fs->mutex);
        {
            /* Do we need to increase countdown? */
            if ((!call_pool->cnt) && (!fs->pin_refcnt)) {
                gf_msg_trace("glfs", 0,
                             "call_pool_cnt - %" PRId64
                             ","
                             "pin_refcnt - %d",
                             call_pool->cnt, fs->pin_refcnt);

                ctx->cleanup_started = 1;
                pthread_mutex_unlock(&fs->mutex);
                break;
            }
        }
        pthread_mutex_unlock(&fs->mutex);
        gf_nanosleep(100000 * GF_US_IN_NS);
    }

    /* leaked frames may exist, we ignore */

    /*We deem glfs_fini as successful if there are no pending frames in the call
     *pool*/
    ret = (call_pool->cnt == 0) ? 0 : -1;

    pthread_mutex_lock(&fs->mutex);
    {
        fs_init = fs->init;
    }
    pthread_mutex_unlock(&fs->mutex);

    if (fs_init != 0) {
        subvol = glfs_active_subvol(fs);
        if (subvol) {
            /* PARENT_DOWN within glfs_subvol_done() is issued
               only on graph switch (new graph should activiate
               and decrement the extra @winds count taken in
               glfs_graph_setup()

               Since we are explicitly destroying,
               PARENT_DOWN is necessary
            */
            xlator_notify(subvol, GF_EVENT_PARENT_DOWN, subvol, 0);
            /* Here we wait for GF_EVENT_CHILD_DOWN before exiting,
               in case of asynchrnous cleanup
            */
            graph = subvol->graph;
            err = pthread_mutex_lock(&fs->mutex);
            if (err != 0) {
                gf_smsg("glfs", GF_LOG_ERROR, err, API_MSG_FSMUTEX_LOCK_FAILED,
                        "error=%s", strerror(err), NULL);
                goto fail;
            }
            /* check and wait for CHILD_DOWN for active subvol*/
            {
                while (graph->used) {
                    err = pthread_cond_wait(&fs->child_down_cond, &fs->mutex);
                    if (err != 0)
                        gf_smsg("glfs", GF_LOG_INFO, err,
                                API_MSG_COND_WAIT_FAILED, "name=%s",
                                subvol->name, "err=%s", strerror(err), NULL);
                }
            }

            err = pthread_mutex_unlock(&fs->mutex);
            if (err != 0) {
                gf_smsg("glfs", GF_LOG_ERROR, err,
                        API_MSG_FSMUTEX_UNLOCK_FAILED, "error=%s",
                        strerror(err), NULL);
                goto fail;
            }
        }
        glfs_subvol_done(fs, subvol);
    }

    ctx->cleanup_started = 1;

    if (fs_init != 0) {
        /* Destroy all the inode tables of all the graphs.
         * NOTE:
         * - inode objects should be destroyed before calling fini()
         *   of each xlator, as fini() and forget() of the xlators
         *   can share few common locks or data structures, calling
         *   fini first might destroy those required by forget
         *   ( eg: in quick-read)
         * - The call to inode_table_destroy_all is not required when
         *   the cleanup during graph switch is implemented to perform
         *   inode table destroy.
         */
        inode_table_destroy_all(ctx);

        /* Call fini() of all the xlators in the active graph
         * NOTE:
         * - xlator fini() should be called before destroying any of
         *   the threads. (eg: fini() in protocol-client uses timer
         *   thread) */
        glusterfs_graph_deactivate(ctx->active);

        /* Join the syncenv_processor threads and cleanup
         * syncenv resources*/
        syncenv_destroy(ctx->env);

        /* Join the poller thread */
        if (gf_event_dispatch_destroy(ctx->event_pool) < 0)
            ret = -1;
    }

    /* Avoid dispatching events to mgmt after freed,
     * unreference mgmt after the event_dispatch_destroy */
    if (ctx->mgmt) {
        rpc_clnt_unref(ctx->mgmt);
        ctx->mgmt = NULL;
    }

    /* log infra has to be brought down before destroying
     * timer registry, as logging uses timer infra
     */
    if (gf_log_fini(ctx) != 0)
        ret = -1;

    /* Join the timer thread */
    if (fs_init != 0) {
        gf_timer_registry_destroy(ctx);
    }

    /* Destroy the context and the global pools */
    if (glusterfs_ctx_destroy(ctx) != 0)
        ret = -1;

free_fs:
    glfs_free_from_ctx(fs);

    /*
     * Do this as late as possible in case anything else has (or
     * grows) a dependency on mem-pool allocations.
     */
    mem_pools_fini();

fail:
    if (!ret)
        ret = err;

    __GLFS_EXIT_FS;

invalid_fs:
    return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_get_volfile, 3.6.0)
ssize_t
pub_glfs_get_volfile(struct glfs *fs, void *buf, size_t len)
{
    ssize_t res = -1;

    DECLARE_OLD_THIS;
    __GLFS_ENTRY_VALIDATE_FS(fs, invalid_fs);

    glfs_lock(fs, _gf_true);
    if (len >= fs->oldvollen) {
        gf_msg_trace("glfs", 0, "copying %zu to %p", len, buf);
        memcpy(buf, fs->oldvolfile, len);
        res = len;
    } else {
        res = len - fs->oldvollen;
        gf_msg_trace("glfs", 0, "buffer is %zd too short", -res);
    }
    glfs_unlock(fs);

    __GLFS_EXIT_FS;

invalid_fs:
    return res;
}

GFAPI_SYMVER_PRIVATE_DEFAULT(glfs_ipc, 3.12.0)
int
priv_glfs_ipc(struct glfs *fs, int opcode, void *xd_in, void **xd_out)
{
    xlator_t *subvol = NULL;
    int ret = -1;

    DECLARE_OLD_THIS;
    __GLFS_ENTRY_VALIDATE_FS(fs, invalid_fs);

    subvol = glfs_active_subvol(fs);
    if (!subvol) {
        ret = -1;
        errno = EIO;
        goto out;
    }

    ret = syncop_ipc(subvol, opcode, (dict_t *)xd_in, (dict_t **)xd_out);
    DECODE_SYNCOP_ERR(ret);

out:
    glfs_subvol_done(fs, subvol);
    __GLFS_EXIT_FS;

invalid_fs:
    return ret;
}

GFAPI_SYMVER_PRIVATE_DEFAULT(glfs_setfspid, 6.1)
int
priv_glfs_setfspid(struct glfs *fs, pid_t pid)
{
    cmd_args_t *cmd_args = NULL;
    int ret = 0;

    cmd_args = &fs->ctx->cmd_args;
    cmd_args->client_pid = pid;
    cmd_args->client_pid_set = 1;
    ret = syncopctx_setfspid(&pid);

    return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_free, 3.7.16)
void
pub_glfs_free(void *ptr)
{
    GLFS_FREE(ptr);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_get_fs, 3.7.16)
struct glfs *
pub_glfs_upcall_get_fs(struct glfs_upcall *arg)
{
    return arg->fs;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_get_reason, 3.7.16)
enum glfs_upcall_reason
pub_glfs_upcall_get_reason(struct glfs_upcall *arg)
{
    return arg->reason;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_get_event, 3.7.16)
void *
pub_glfs_upcall_get_event(struct glfs_upcall *arg)
{
    return arg->event;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_inode_get_object, 3.7.16)
struct glfs_object *
pub_glfs_upcall_inode_get_object(struct glfs_upcall_inode *arg)
{
    return arg->object;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_inode_get_flags, 3.7.16)
uint64_t
pub_glfs_upcall_inode_get_flags(struct glfs_upcall_inode *arg)
{
    return arg->flags;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_inode_get_stat, 3.7.16)
struct stat *
pub_glfs_upcall_inode_get_stat(struct glfs_upcall_inode *arg)
{
    return &arg->buf;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_inode_get_expire, 3.7.16)
uint64_t
pub_glfs_upcall_inode_get_expire(struct glfs_upcall_inode *arg)
{
    return arg->expire_time_attr;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_inode_get_pobject, 3.7.16)
struct glfs_object *
pub_glfs_upcall_inode_get_pobject(struct glfs_upcall_inode *arg)
{
    return arg->p_object;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_inode_get_pstat, 3.7.16)
struct stat *
pub_glfs_upcall_inode_get_pstat(struct glfs_upcall_inode *arg)
{
    return &arg->p_buf;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_inode_get_oldpobject, 3.7.16)
struct glfs_object *
pub_glfs_upcall_inode_get_oldpobject(struct glfs_upcall_inode *arg)
{
    return arg->oldp_object;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_inode_get_oldpstat, 3.7.16)
struct stat *
pub_glfs_upcall_inode_get_oldpstat(struct glfs_upcall_inode *arg)
{
    return &arg->oldp_buf;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_lease_get_object, 4.1.6)
struct glfs_object *
pub_glfs_upcall_lease_get_object(struct glfs_upcall_lease *arg)
{
    return arg->object;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_lease_get_lease_type, 4.1.6)
uint32_t
pub_glfs_upcall_lease_get_lease_type(struct glfs_upcall_lease *arg)
{
    return arg->lease_type;
}

/* definitions of the GLFS_SYSRQ_* chars are in glfs.h */
static struct glfs_sysrq_help {
    char sysrq;
    char *msg;
} glfs_sysrq_help[] = {{GLFS_SYSRQ_HELP, "(H)elp"},
                       {GLFS_SYSRQ_STATEDUMP, "(S)tatedump"},
                       {0, NULL}};

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_sysrq, 3.10.0)
int
pub_glfs_sysrq(struct glfs *fs, char sysrq)
{
    glusterfs_ctx_t *ctx = NULL;
    int ret = 0;
    int msg_len;
    char msg[1024] = {
        0,
    }; /* should not exceed 1024 chars */

    if (!fs || !fs->ctx) {
        ret = -1;
        errno = EINVAL;
        goto out;
    }

    ctx = fs->ctx;

    switch (sysrq) {
        case GLFS_SYSRQ_HELP: {
            struct glfs_sysrq_help *usage = NULL;

            for (usage = glfs_sysrq_help; usage->sysrq; usage++) {
                msg_len = strlen(msg);
                snprintf(msg + msg_len, /* append to msg */
                         sizeof(msg) - msg_len - 2,
                         /* - 2 for the " " + terminating \0 */
                         " %s", usage->msg);
            }

            /* not really an 'error', but make sure it gets logged */
            gf_log("glfs", GF_LOG_ERROR, "available events: %s", msg);

            break;
        }
        case GLFS_SYSRQ_STATEDUMP:
            gf_proc_dump_info(SIGUSR1, ctx);
            break;
        default:
            gf_smsg("glfs", GF_LOG_ERROR, ENOTSUP, API_MSG_INVALID_SYSRQ,
                    "sysrq=%c", sysrq, NULL);
            errno = ENOTSUP;
            ret = -1;
    }
out:
    return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_register, 3.13.0)
int
pub_glfs_upcall_register(struct glfs *fs, uint32_t event_list,
                         glfs_upcall_cbk cbk, void *data)
{
    int ret = 0;

    /* list of supported upcall events */
    uint32_t up_events = (GLFS_EVENT_INODE_INVALIDATE |
                          GLFS_EVENT_RECALL_LEASE);

    DECLARE_OLD_THIS;
    __GLFS_ENTRY_VALIDATE_FS(fs, invalid_fs);

    GF_VALIDATE_OR_GOTO(THIS->name, cbk, out);

    /* Event list should be either GLFS_EVENT_ANY
     * or list of supported individual events (up_events)
     */
    if ((event_list != GLFS_EVENT_ANY) && (event_list & ~up_events)) {
        errno = EINVAL;
        ret = -1;
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, API_MSG_INVALID_ARG,
                "event_list=(0x%08x)", event_list, NULL);
        goto out;
    }

    /* in case other thread does unregister */
    pthread_mutex_lock(&fs->mutex);
    {
        if (event_list & GLFS_EVENT_INODE_INVALIDATE) {
            /* @todo: Check if features.cache-invalidation is
             * enabled.
             */
            fs->upcall_events |= GF_UPCALL_CACHE_INVALIDATION;
            ret |= GLFS_EVENT_INODE_INVALIDATE;
        }
        if (event_list & GLFS_EVENT_RECALL_LEASE) {
            /* @todo: Check if features.leases is enabled */
            fs->upcall_events |= GF_UPCALL_RECALL_LEASE;
            ret |= GLFS_EVENT_RECALL_LEASE;
        }
        /* Override cbk function if existing */
        fs->up_cbk = cbk;
        fs->up_data = data;
        fs->cache_upcalls = _gf_true;
    }
    pthread_mutex_unlock(&fs->mutex);

out:
    __GLFS_EXIT_FS;

invalid_fs:
    return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_upcall_unregister, 3.13.0)
int
pub_glfs_upcall_unregister(struct glfs *fs, uint32_t event_list)
{
    int ret = 0;
    /* list of supported upcall events */
    uint32_t up_events = (GLFS_EVENT_INODE_INVALIDATE |
                          GLFS_EVENT_RECALL_LEASE);

    DECLARE_OLD_THIS;
    __GLFS_ENTRY_VALIDATE_FS(fs, invalid_fs);

    /* Event list should be either GLFS_EVENT_ANY
     * or list of supported individual events (up_events)
     */
    if ((event_list != GLFS_EVENT_ANY) && (event_list & ~up_events)) {
        errno = EINVAL;
        ret = -1;
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, API_MSG_INVALID_ARG,
                "event_list=(0x%08x)", event_list, NULL);
        goto out;
    }

    pthread_mutex_lock(&fs->mutex);
    {
        /* We already checked if event_list contains list of supported
         * upcall events. No other specific checks needed as of now for
         * unregister */
        fs->upcall_events &= ~(event_list);
        ret |= ((event_list == GLFS_EVENT_ANY) ? up_events : event_list);

        /* If there are no upcall events registered, reset cbk */
        if (fs->upcall_events == 0) {
            fs->up_cbk = NULL;
            fs->up_data = NULL;
            fs->cache_upcalls = _gf_false;
        }
    }
    pthread_mutex_unlock(&fs->mutex);

out:
    __GLFS_EXIT_FS;

invalid_fs:
    return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_set_statedump_path, 7.0)
int
pub_glfs_set_statedump_path(struct glfs *fs, const char *path)
{
    struct stat st;
    int ret;
    DECLARE_OLD_THIS;
    __GLFS_ENTRY_VALIDATE_FS(fs, invalid_fs);

    if (!path) {
        gf_log("glfs", GF_LOG_ERROR, "path is NULL");
        errno = EINVAL;
        goto err;
    }

    /* If path is not present OR, if it is directory AND has enough permission
     * to create files, then proceed */
    ret = sys_stat(path, &st);
    if (ret && errno != ENOENT) {
        gf_log("glfs", GF_LOG_ERROR, "%s: not a valid path (%s)", path,
               strerror(errno));
        errno = EINVAL;
        goto err;
    }

    if (!ret) {
        /* file is present, now check other things */
        if (!S_ISDIR(st.st_mode)) {
            gf_log("glfs", GF_LOG_ERROR, "%s: path is not directory", path);
            errno = EINVAL;
            goto err;
        }
        if (sys_access(path, W_OK | X_OK) < 0) {
            gf_log("glfs", GF_LOG_ERROR,
                   "%s: path doesn't have write permission", path);
            errno = EPERM;
            goto err;
        }
    }

    /* If set, it needs to be freed, so we don't have leak */
    GF_FREE(fs->ctx->statedump_path);

    fs->ctx->statedump_path = gf_strdup(path);
    if (!fs->ctx->statedump_path) {
        gf_log("glfs", GF_LOG_ERROR,
               "%s: failed to set statedump path, no memory", path);
        errno = ENOMEM;
        goto err;
    }

    __GLFS_EXIT_FS;

    return 0;
err:
    __GLFS_EXIT_FS;

invalid_fs:
    return -1;
}
