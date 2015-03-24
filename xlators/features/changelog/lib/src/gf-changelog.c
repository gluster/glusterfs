/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <errno.h>
#include <dirent.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>

#include "globals.h"
#include "glusterfs.h"
#include "logging.h"
#include "defaults.h"
#include "syncop.h"

#include "gf-changelog-rpc.h"
#include "gf-changelog-helpers.h"

/* from the changelog translator */
#include "changelog-misc.h"
#include "changelog-mem-types.h"

/**
 * Global singleton xlator pointer for the library, initialized
 * during library load. This should probably be hidden inside
 * an initialized object which is an handle for the consumer.
 *
 * TODO: do away with the global..
 */
xlator_t *master = NULL;

static inline
gf_private_t *gf_changelog_alloc_priv ()
{
        int ret = 0;
        gf_private_t *priv = NULL;

        priv = calloc (1, sizeof (gf_private_t));
        if (!priv)
                goto error_return;
        INIT_LIST_HEAD (&priv->connections);

        ret = LOCK_INIT (&priv->lock);
        if (ret != 0)
                goto free_priv;
        priv->api = NULL;

        return priv;

 free_priv:
        free (priv);
 error_return:
        return NULL;
}

#define GF_CHANGELOG_EVENT_POOL_SIZE   16384
#define GF_CHANGELOG_EVENT_THREAD_COUNT 4

static int
gf_changelog_ctx_defaults_init (glusterfs_ctx_t *ctx)
{
        cmd_args_t    *cmd_args = NULL;
        struct rlimit  lim = {0, };
        call_pool_t   *pool = NULL;
        int            ret         = -1;

        ret = xlator_mem_acct_init (THIS, gf_changelog_mt_end);
        if (ret != 0) {
                return ret;
        }

        ctx->process_uuid = generate_glusterfs_ctx_id ();
        if (!ctx->process_uuid)
                return -1;

        ctx->page_size  = 128 * GF_UNIT_KB;

        ctx->iobuf_pool = iobuf_pool_new ();
        if (!ctx->iobuf_pool)
                return -1;

        ctx->event_pool = event_pool_new (GF_CHANGELOG_EVENT_POOL_SIZE,
                                          GF_CHANGELOG_EVENT_THREAD_COUNT);
        if (!ctx->event_pool)
                return -1;

        pool = GF_CALLOC (1, sizeof (call_pool_t),
                          gf_changelog_mt_libgfchangelog_call_pool_t);
        if (!pool)
                return -1;

        /* frame_mem_pool size 112 * 64 */
        pool->frame_mem_pool = mem_pool_new (call_frame_t, 32);
        if (!pool->frame_mem_pool)
                return -1;

        /* stack_mem_pool size 256 * 128 */
        pool->stack_mem_pool = mem_pool_new (call_stack_t, 16);

        if (!pool->stack_mem_pool)
                return -1;

        ctx->stub_mem_pool = mem_pool_new (call_stub_t, 16);
        if (!ctx->stub_mem_pool)
                return -1;

        ctx->dict_pool = mem_pool_new (dict_t, 32);
        if (!ctx->dict_pool)
                return -1;

        ctx->dict_pair_pool = mem_pool_new (data_pair_t, 512);
        if (!ctx->dict_pair_pool)
                return -1;

        ctx->dict_data_pool = mem_pool_new (data_t, 512);
        if (!ctx->dict_data_pool)
                return -1;

        INIT_LIST_HEAD (&pool->all_frames);
        LOCK_INIT (&pool->lock);
        ctx->pool = pool;

        pthread_mutex_init (&(ctx->lock), NULL);

        cmd_args = &ctx->cmd_args;

        INIT_LIST_HEAD (&cmd_args->xlator_options);

        lim.rlim_cur = RLIM_INFINITY;
        lim.rlim_max = RLIM_INFINITY;
        setrlimit (RLIMIT_CORE, &lim);

        return 0;
}

/* TODO: cleanup ctx defaults */
static void
gf_changelog_cleanup_this (xlator_t *this)
{
        glusterfs_ctx_t *ctx = NULL;

        if (!this)
                return;

        ctx = this->ctx;
        syncenv_destroy (ctx->env);
        free (ctx);

        if (this->private)
                free (this->private);

        this->private = NULL;
        this->ctx = NULL;
}

static int
gf_changelog_init_this ()
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = glusterfs_ctx_new ();
        if (!ctx)
                goto error_return;

        if (glusterfs_globals_init (ctx))
                goto free_ctx;

        THIS->ctx = ctx;
        if (gf_changelog_ctx_defaults_init (ctx))
                goto free_ctx;

        ctx->env = syncenv_new (0, 0, 0);
        if (!ctx->env)
                goto free_ctx;
        return 0;

 free_ctx:
        free (ctx);
        THIS->ctx = NULL;
 error_return:
        return -1;
}

static int
gf_changelog_init_master ()
{
        int              ret  = 0;
        gf_private_t    *priv = NULL;
        glusterfs_ctx_t *ctx  = NULL;

        ret = gf_changelog_init_this ();
        if (ret != 0)
                goto error_return;
        master = THIS;

        priv = gf_changelog_alloc_priv ();
        if (!priv)
                goto cleanup_master;
        master->private = priv;

        /* poller thread */
        ret = pthread_create (&priv->poller,
                              NULL, changelog_rpc_poller, master);
        if (ret != 0) {
                gf_log (master->name, GF_LOG_ERROR,
                        "failed to spawn poller thread");
                goto cleanup_master;
        }

        return 0;

 cleanup_master:
        master->private = NULL;
        gf_changelog_cleanup_this (master);
 error_return:
        return -1;
}

/* ctor/dtor */

void
__attribute__ ((constructor)) gf_changelog_ctor (void)
{
        (void) gf_changelog_init_master ();
}

void
__attribute__ ((destructor)) gf_changelog_dtor (void)
{
        gf_changelog_cleanup_this (master);
}

/* TODO: cleanup clnt/svc on failure */
int
gf_changelog_setup_rpc (xlator_t *this,
                        gf_changelog_t *entry, int proc)
{
        int              ret = 0;
        rpcsvc_t        *svc = NULL;
        struct rpc_clnt *rpc = NULL;

        /**
         * Initialize a connect back socket. A probe() RPC call to the server
         * triggers a reverse connect.
         */
        svc = gf_changelog_reborp_init_rpc_listner (this, entry->brick,
                                                    RPC_SOCK (entry), entry);
        if (!svc)
                goto error_return;
        RPC_REBORP (entry) = svc;

        /* Initialize an RPC client */
        rpc = gf_changelog_rpc_init (this, entry);
        if (!rpc)
                goto error_return;
        RPC_PROBER (entry) = rpc;

        /**
         * @FIXME
         * till we have connection state machine, let's delay the RPC call
         * for now..
         */
        sleep (2);

        /**
         * Probe changelog translator for reverse connection. After a successful
         * call, there's less use of the client and can be disconnected, but
         * let's leave the connection active for any future RPC calls.
         */
        ret = gf_changelog_invoke_rpc (this, entry, proc);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not initiate probe RPC, bailing out!!!");
                goto error_return;
        }

        return 0;

 error_return:
        return -1;
}

static void
gf_cleanup_event (gf_changelog_t *entry)
{
        xlator_t             *this = NULL;
        struct gf_event_list *ev   = NULL;

        this = entry->this;
        ev = &entry->event;

        (void) gf_thread_cleanup (this, ev->invoker);

        (void) pthread_mutex_destroy (&ev->lock);
        (void) pthread_cond_destroy (&ev->cond);

        ev->entry = NULL;
}

static int
gf_init_event (gf_changelog_t *entry)
{
        int ret = 0;
        struct gf_event_list *ev = NULL;

        ev = &entry->event;
        ev->entry = entry;

        ret = pthread_mutex_init (&ev->lock, NULL);
        if (ret != 0)
                goto error_return;
        ret = pthread_cond_init (&ev->cond, NULL);
        if (ret != 0)
                goto cleanup_mutex;
        INIT_LIST_HEAD (&ev->events);

        ev->next_seq = 0;  /* bootstrap sequencing */

        if (entry->ordered) {
                ret = pthread_create (&ev->invoker, NULL,
                                      gf_changelog_callback_invoker, ev);
                if (ret != 0)
                        goto cleanup_cond;
        }

        return 0;

 cleanup_cond:
        (void) pthread_cond_destroy (&ev->cond);
 cleanup_mutex:
        (void) pthread_mutex_destroy (&ev->lock);
 error_return:
        return -1;
}

/**
 * TODO:
 *  - cleanup invoker thread (if ordered mode)
 *  - cleanup event list
 *  - destroy rpc{-clnt, svc}
 */
int
gf_cleanup_brick_connection (xlator_t *this, gf_changelog_t *entry)
{
        return 0;
}

int
gf_cleanup_connections (xlator_t *this)
{
        return 0;
}

static int
gf_setup_brick_connection (xlator_t *this,
                           struct gf_brick_spec *brick,
                           gf_boolean_t ordered, void *xl)
{
        int ret = 0;
        gf_private_t *priv = NULL;
        gf_changelog_t *entry = NULL;

        priv = this->private;

        if (!brick->callback || !brick->init || !brick->fini)
                goto error_return;

        entry = GF_CALLOC (1, sizeof (*entry),
                           gf_changelog_mt_libgfchangelog_t);
        if (!entry)
                goto error_return;
        INIT_LIST_HEAD (&entry->list);

        entry->notify = brick->filter;
        (void) strncpy (entry->brick, brick->brick_path, PATH_MAX);

        entry->this = this;
        entry->invokerxl = xl;

        entry->ordered = ordered;
        if (ordered) {
                ret = gf_init_event (entry);
                if (ret)
                        goto free_entry;
        }

        entry->fini         = brick->fini;
        entry->callback     = brick->callback;
        entry->connected    = brick->connected;
        entry->disconnected = brick->disconnected;

        entry->ptr = brick->init (this, brick);
        if (!entry->ptr)
                goto cleanup_event;
        priv->api = entry->ptr;  /* pointer to API, if required */

        LOCK (&priv->lock);
        {
                list_add_tail (&entry->list, &priv->connections);
        }
        UNLOCK (&priv->lock);

        ret = gf_changelog_setup_rpc (this, entry, CHANGELOG_RPC_PROBE_FILTER);
        if (ret)
                goto cleanup_event;
        return 0;

 cleanup_event:
        if (ordered)
                gf_cleanup_event (entry);
 free_entry:
        list_del (&entry->list); /* FIXME: kludge for now */
        GF_FREE (entry);
 error_return:
        return -1;
}

int
gf_changelog_register_brick (xlator_t *this,
                             struct gf_brick_spec *brick,
                             gf_boolean_t ordered, void *xl)
{
        return gf_setup_brick_connection (this, brick, ordered, xl);
}

static int
gf_changelog_setup_logging (xlator_t *this, char *logfile, int loglevel)
{
        /* passing ident as NULL means to use default ident for syslog */
        if (gf_log_init (this->ctx, logfile, NULL))
                return -1;

        gf_log_set_loglevel ((loglevel == -1) ? GF_LOG_INFO :
                             loglevel);
        return 0;
}

int
gf_changelog_register_generic (struct gf_brick_spec *bricks, int count,
                               int ordered, char *logfile, int lvl, void *xl)
{
        int                   ret        = 0;
        xlator_t             *this       = NULL;
        xlator_t             *old_this   = NULL;
        struct gf_brick_spec *brick      = NULL;
        gf_boolean_t          need_order = _gf_false;

        SAVE_THIS (xl);

        this = THIS;
        if (!this)
                goto error_return;

        ret = gf_changelog_setup_logging (this, logfile, lvl);
        if (ret)
                goto error_return;

        need_order = (ordered) ? _gf_true : _gf_false;

        brick = bricks;
        while (count--) {
                gf_log (this->name, GF_LOG_INFO,
                        "Registering brick: %s [notify filter: %d]",
                        brick->brick_path, brick->filter);

                ret = gf_changelog_register_brick (this, brick, need_order, xl);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Error registering with changelog xlator");
                        break;
                }

                brick++;
        }

        if (ret != 0)
                goto cleanup_inited_bricks;

        RESTORE_THIS();
        return 0;

 cleanup_inited_bricks:
        gf_cleanup_connections (this);
 error_return:
        RESTORE_THIS();
        return -1;
}

/**
 * @API
 *  gf_changelog_register()
 *
 * This is _NOT_ a generic register API. It's a special API to handle
 * updates at a journal granulality. This is used by consumers wanting
 * to process persistent journal such as geo-replication via a set of
 * APIs. All of this is required to maintain backward compatibility.
 * Owner specific private data is stored in ->api (in gf_private_t),
 * which is used by APIs to access it's private data. This limits
 * the library access to a single brick, but that's how it used to
 * be anyway. Furthermore, this API solely _owns_ "this", therefore
 * callers already having a notion of "this" are expected to use the
 * newer API.
 *
 * Newer applications wanting to use this library need not face this
 * limitation and reply of the much more feature rich generic register
 * API, which is purely callback based.
 *
 * NOTE: @max_reconnects is not used but required for backward compat.
 *
 * For generic API, refer gf_changelog_register_generic().
 */
int
gf_changelog_register (char *brick_path, char *scratch_dir,
                       char *log_file, int log_level, int max_reconnects)
{
        struct gf_brick_spec brick = {0,};

        THIS = master;

        brick.brick_path = brick_path;
        brick.filter     = CHANGELOG_OP_TYPE_JOURNAL;

        brick.init         = gf_changelog_journal_init;
        brick.fini         = gf_changelog_journal_fini;
        brick.callback     = gf_changelog_handle_journal;
        brick.connected    = gf_changelog_journal_connect;
        brick.disconnected = gf_changelog_journal_disconnect;

        brick.ptr = scratch_dir;

        return gf_changelog_register_generic (&brick, 1, 1,
                                              log_file, log_level, NULL);
}
