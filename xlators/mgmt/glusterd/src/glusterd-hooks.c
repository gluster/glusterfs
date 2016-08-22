/*
   Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "globals.h"
#include "glusterfs.h"
#include "dict.h"
#include "xlator.h"
#include "logging.h"
#include "run.h"
#include "defaults.h"
#include "syscall.h"
#include "compat.h"
#include "compat-errno.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include "glusterd-hooks.h"
#include "glusterd-messages.h"

#include <fnmatch.h>

#define EMPTY ""
char glusterd_hook_dirnames[GD_OP_MAX][256] =
{
        [GD_OP_NONE]                    = EMPTY,
        [GD_OP_CREATE_VOLUME]           = "create",
        [GD_OP_START_BRICK]             = EMPTY,
        [GD_OP_STOP_BRICK]              = EMPTY,
        [GD_OP_DELETE_VOLUME]           = "delete",
        [GD_OP_START_VOLUME]            = "start",
        [GD_OP_STOP_VOLUME]             = "stop",
        [GD_OP_DEFRAG_VOLUME]           = EMPTY,
        [GD_OP_ADD_BRICK]               = "add-brick",
        [GD_OP_REMOVE_BRICK]            = "remove-brick",
        [GD_OP_REPLACE_BRICK]           = EMPTY,
        [GD_OP_SET_VOLUME]              = "set",
        [GD_OP_RESET_VOLUME]            = "reset",
        [GD_OP_SYNC_VOLUME]             = EMPTY,
        [GD_OP_LOG_ROTATE]              = EMPTY,
        [GD_OP_GSYNC_CREATE]            = "gsync-create",
        [GD_OP_GSYNC_SET]               = EMPTY,
        [GD_OP_PROFILE_VOLUME]          = EMPTY,
        [GD_OP_QUOTA]                   = EMPTY,
        [GD_OP_STATUS_VOLUME]           = EMPTY,
        [GD_OP_REBALANCE]               = EMPTY,
        [GD_OP_HEAL_VOLUME]             = EMPTY,
        [GD_OP_STATEDUMP_VOLUME]        = EMPTY,
        [GD_OP_LIST_VOLUME]             = EMPTY,
        [GD_OP_CLEARLOCKS_VOLUME]       = EMPTY,
        [GD_OP_DEFRAG_BRICK_VOLUME]     = EMPTY,
        [GD_OP_RESET_BRICK]             = EMPTY,
};
#undef EMPTY

static gf_boolean_t
glusterd_is_hook_enabled (char *script)
{
        return (script[0] == 'S' && (fnmatch ("*.rpmsave", script, 0) != 0)
                                 && (fnmatch ("*.rpmnew", script, 0) != 0));
}

int
glusterd_hooks_create_hooks_directory (char *basedir)
{
        int  ret                                        = -1;
        int  op                                         = GD_OP_NONE;
        int  type                                       = GD_COMMIT_HOOK_NONE;
        char version_dir[PATH_MAX]                      = {0, };
        char path[PATH_MAX]                             = {0, };
        char *cmd_subdir                                = NULL;
        char type_subdir[GD_COMMIT_HOOK_MAX][256]       = {{0, },
                                                           "pre",
                                                           "post"};
        glusterd_conf_t *priv                           = NULL;

        priv = THIS->private;

        snprintf (path, sizeof (path), "%s/hooks", basedir);
        ret = mkdir_p (path, 0777, _gf_true);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED, "Unable to create %s",
                        path);
                goto out;
        }

        GLUSTERD_GET_HOOKS_DIR (version_dir, GLUSTERD_HOOK_VER, priv);
        ret = mkdir_p (version_dir, 0777, _gf_true);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED, "Unable to create %s",
                        version_dir);
                goto out;
        }

        for (op = GD_OP_NONE+1; op < GD_OP_MAX; op++) {
                cmd_subdir = glusterd_hooks_get_hooks_cmd_subdir (op);
                if (strlen (cmd_subdir) == 0)
                        continue;

                snprintf (path, sizeof (path), "%s/%s", version_dir,
                          cmd_subdir);
                ret = mkdir_p (path, 0777, _gf_true);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_CRITICAL, errno,
                                GD_MSG_CREATE_DIR_FAILED,
                                "Unable to create %s",
                                path);
                        goto out;
                }

                for (type = GD_COMMIT_HOOK_PRE; type < GD_COMMIT_HOOK_MAX;
                     type++) {
                        snprintf (path, sizeof (path), "%s/%s/%s",
                                  version_dir, cmd_subdir, type_subdir[type]);
                        ret = mkdir_p (path, 0777, _gf_true);
                        if (ret) {
                                gf_msg (THIS->name, GF_LOG_CRITICAL, errno,
                                        GD_MSG_CREATE_DIR_FAILED,
                                        "Unable to create %s",
                                        path);
                                goto out;
                        }
                }
        }

        ret = 0;
out:
        return ret;
}

char*
glusterd_hooks_get_hooks_cmd_subdir (glusterd_op_t op)
{
        GF_ASSERT ((op > GD_OP_NONE) && (op < GD_OP_MAX));

        return glusterd_hook_dirnames[op];
}

void
glusterd_hooks_add_working_dir (runner_t *runner, glusterd_conf_t *priv)
{
        runner_argprintf (runner, "--gd-workdir=%s", priv->workdir);
}

void
glusterd_hooks_add_op (runner_t *runner, char *op)
{
        runner_argprintf (runner, "--volume-op=%s", op);
}

void
glusterd_hooks_add_hooks_version (runner_t* runner)
{
        runner_argprintf (runner, "--version=%d", GLUSTERD_HOOK_VER);
}

static void
glusterd_hooks_add_custom_args (dict_t *dict, runner_t *runner)
{
        char      *hooks_args     = NULL;
        int32_t    ret            = -1;
        xlator_t  *this           = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);
        GF_VALIDATE_OR_GOTO (this->name, runner, out);

        ret = dict_get_str (dict, "hooks_args", &hooks_args);
        if (ret)
                gf_msg_debug (this->name, 0,
                        "No Hooks Arguments.");
        else
                gf_msg_debug (this->name, 0,
                        "Hooks Args = %s", hooks_args);

        if (hooks_args)
                runner_argprintf (runner, "%s", hooks_args);

out:
        return;
}


int
glusterd_hooks_set_volume_args (dict_t *dict, runner_t *runner)
{
        int     i               = 0;
        int     count           = 0;
        int     ret             = -1;
        char    query[1024]     = {0,};
        char    *key            = NULL;
        char    *value          = NULL;

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                goto out;

        /* This will not happen unless op_ctx
         * is corrupted*/
        if (!count)
                goto out;

        runner_add_arg (runner, "-o");
        for (i = 1; ret == 0; i++) {
                snprintf (query, sizeof (query), "key%d", i);
                ret = dict_get_str (dict, query, &key);
                if (ret)
                       continue;

                snprintf (query, sizeof (query), "value%d", i);
                ret = dict_get_str (dict, query, &value);
                if (ret)
                       continue;

                runner_argprintf (runner, "%s=%s", key, value);
        }

        glusterd_hooks_add_custom_args (dict, runner);

        ret = 0;
out:
        return ret;
}

static int
glusterd_hooks_add_op_args (runner_t *runner, glusterd_op_t op,
                            dict_t *op_ctx, glusterd_commit_hook_type_t type)
{
        int                     vol_count        = 0;
        gf_boolean_t            truth            = _gf_false;
        glusterd_volinfo_t      *voliter         = NULL;
        glusterd_conf_t         *priv            = NULL;
        int                     ret              = -1;

        priv = THIS->private;
        cds_list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (glusterd_is_volume_started (voliter))
                        vol_count++;
        }

        ret = 0;
        switch (op) {
                case GD_OP_START_VOLUME:
                        if (type == GD_COMMIT_HOOK_PRE &&
                            vol_count == 0)
                                truth = _gf_true;

                        else if (type == GD_COMMIT_HOOK_POST &&
                                 vol_count == 1)
                                truth = _gf_true;

                        else
                                truth = _gf_false;

                        runner_argprintf (runner, "--first=%s",
                                          truth? "yes":"no");

                        glusterd_hooks_add_hooks_version (runner);
                        glusterd_hooks_add_op (runner, "start");
                        glusterd_hooks_add_working_dir (runner, priv);

                        break;

                case GD_OP_STOP_VOLUME:
                        if (type == GD_COMMIT_HOOK_PRE &&
                            vol_count == 1)
                                truth = _gf_true;

                        else if (type == GD_COMMIT_HOOK_POST &&
                                 vol_count == 0)
                                truth = _gf_true;

                        else
                                truth = _gf_false;

                        runner_argprintf (runner, "--last=%s",
                                          truth? "yes":"no");
                        break;

                case GD_OP_SET_VOLUME:
                        ret = glusterd_hooks_set_volume_args (op_ctx, runner);
                        glusterd_hooks_add_working_dir (runner, priv);
                        break;

                case GD_OP_GSYNC_CREATE:
                        glusterd_hooks_add_custom_args (op_ctx, runner);
                        break;

                case GD_OP_ADD_BRICK:
                        glusterd_hooks_add_hooks_version (runner);
                        glusterd_hooks_add_op (runner, "add-brick");
                        glusterd_hooks_add_working_dir (runner, priv);
                        break;

               case GD_OP_RESET_VOLUME:
                        glusterd_hooks_add_hooks_version (runner);
                        glusterd_hooks_add_op (runner, "reset");
                        glusterd_hooks_add_working_dir (runner, priv);
                        break;

                default:
                        break;

        }

        return ret;
}

int
glusterd_hooks_run_hooks (char *hooks_path, glusterd_op_t op, dict_t *op_ctx,
                          glusterd_commit_hook_type_t type)
{
        xlator_t        *this                   = NULL;
        runner_t         runner                 = {0,};
        DIR             *hookdir                = NULL;
        struct dirent   *entry                  = NULL;
        struct dirent    scratch[2]             = {{0,},};
        char            *volname                = NULL;
        char           **lines                  = NULL;
        int              N                      = 8; /*arbitrary*/
        int              lineno                 = 0;
        int              line_count             = 0;
        int              ret                    = -1;

        this = THIS;

        ret = dict_get_str (op_ctx, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_DICT_GET_FAILED, "Failed to get volname "
                        "from operation context");
                goto out;
        }

        hookdir = sys_opendir (hooks_path);
        if (!hookdir) {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DIR_OP_FAILED,
                        "Failed to open dir %s",
                        hooks_path);
                goto out;
        }

        lines = GF_CALLOC (1, N * sizeof (*lines), gf_gld_mt_charptr);
        if (!lines) {
                ret = -1;
                goto out;
        }

        ret = -1;
        line_count = 0;
        GF_FOR_EACH_ENTRY_IN_DIR (entry, hookdir, scratch);
        while (entry) {
                if (line_count == N-1) {
                        N *= 2;
                        lines = GF_REALLOC (lines, N * sizeof (char *));
                        if (!lines)
                                goto out;
                }

                if (glusterd_is_hook_enabled (entry->d_name)) {
                        lines[line_count] = gf_strdup (entry->d_name);
                        line_count++;
                }

                GF_FOR_EACH_ENTRY_IN_DIR (entry, hookdir, scratch);
        }

        lines[line_count] = NULL;
        lines = GF_REALLOC (lines, (line_count + 1) * sizeof (char *));
        if (!lines)
                goto out;

        qsort (lines, line_count, sizeof (*lines), glusterd_compare_lines);

        for (lineno = 0; lineno < line_count; lineno++) {

                runinit (&runner);
                runner_argprintf (&runner, "%s/%s", hooks_path, lines[lineno]);
                /*Add future command line arguments to hook scripts below*/
                runner_argprintf (&runner, "--volname=%s", volname);
                ret = glusterd_hooks_add_op_args (&runner, op, op_ctx, type);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_ADD_OP_ARGS_FAIL, "Failed to add "
                                "command specific arguments");
                        goto out;
                }

                ret = runner_run_reuse (&runner);
                if (ret) {
                        runner_log (&runner, this->name, GF_LOG_ERROR,
                                    "Failed to execute script");
                } else {
                        runner_log (&runner, this->name, GF_LOG_INFO,
                                    "Ran script");
                }
                runner_end (&runner);
        }

        ret = 0;
out:
        if (lines) {
                for (lineno = 0; lineno < line_count+1; lineno++)
                        GF_FREE (lines[lineno]);

                GF_FREE (lines);
        }

        if (hookdir)
                sys_closedir (hookdir);

        return ret;
}

int
glusterd_hooks_post_stub_enqueue (char *scriptdir, glusterd_op_t op,
                                  dict_t *op_ctx)
{
        int                     ret     = -1;
        glusterd_hooks_stub_t   *stub   = NULL;
        glusterd_hooks_private_t *hooks_priv = NULL;
        glusterd_conf_t         *conf   = NULL;

        conf = THIS->private;
        hooks_priv = conf->hooks_priv;

        ret = glusterd_hooks_stub_init (&stub, scriptdir, op, op_ctx);
        if (ret)
                goto out;

        pthread_mutex_lock (&hooks_priv->mutex);
        {
                hooks_priv->waitcount++;
                cds_list_add_tail (&stub->all_hooks, &hooks_priv->list);
                pthread_cond_signal (&hooks_priv->cond);
        }
        pthread_mutex_unlock (&hooks_priv->mutex);

        ret = 0;
out:
        return ret;
}

int
glusterd_hooks_stub_init (glusterd_hooks_stub_t **stub, char *scriptdir,
                          glusterd_op_t op, dict_t *op_ctx)
{
        int                     ret             = -1;
        glusterd_hooks_stub_t   *hooks_stub     = NULL;

        GF_ASSERT (stub);
        if (!stub)
                goto out;

        hooks_stub = GF_CALLOC (1, sizeof (*hooks_stub),
                                gf_gld_mt_hooks_stub_t);
        if (!hooks_stub)
                goto out;

        CDS_INIT_LIST_HEAD (&hooks_stub->all_hooks);
        hooks_stub->op = op;
        hooks_stub->scriptdir = gf_strdup (scriptdir);
        if (!hooks_stub->scriptdir)
                goto out;

        hooks_stub->op_ctx = dict_copy_with_ref (op_ctx, hooks_stub->op_ctx);
        if (!hooks_stub->op_ctx)
                goto out;

        *stub = hooks_stub;
        ret = 0;
out:
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_POST_HOOK_STUB_INIT_FAIL, "Failed to initialize "
                        "post hooks stub");
                glusterd_hooks_stub_cleanup (hooks_stub);
        }

        return ret;
}

void
glusterd_hooks_stub_cleanup (glusterd_hooks_stub_t *stub)
{
        if (!stub) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  GD_MSG_HOOK_STUB_NULL,
                                  "hooks_stub is NULL");
                return;
        }

        if (stub->op_ctx)
                dict_unref (stub->op_ctx);

        GF_FREE (stub->scriptdir);

        GF_FREE (stub);
}

static void*
hooks_worker (void *args)
{
        glusterd_conf_t *conf = NULL;
        glusterd_hooks_private_t *hooks_priv = NULL;
        glusterd_hooks_stub_t *stub = NULL;

        THIS = args;
        conf = THIS->private;
        hooks_priv = conf->hooks_priv;

        for (;;) {
                pthread_mutex_lock (&hooks_priv->mutex);
                {
                        while (cds_list_empty (&hooks_priv->list)) {
                                pthread_cond_wait (&hooks_priv->cond,
                                                   &hooks_priv->mutex);
                        }
                        stub = cds_list_entry (hooks_priv->list.next,
                                               glusterd_hooks_stub_t,
                                               all_hooks);
                        cds_list_del_init (&stub->all_hooks);
                        hooks_priv->waitcount--;

                }
                pthread_mutex_unlock (&hooks_priv->mutex);

                glusterd_hooks_run_hooks (stub->scriptdir, stub->op,
                                          stub->op_ctx, GD_COMMIT_HOOK_POST);
                glusterd_hooks_stub_cleanup (stub);
        }

        return NULL;
}

int
glusterd_hooks_priv_init (glusterd_hooks_private_t **new)
{
        int                      ret            = -1;
        glusterd_hooks_private_t *hooks_priv    = NULL;

        if (!new)
                goto out;

        hooks_priv = GF_CALLOC (1, sizeof (*hooks_priv),
                                gf_gld_mt_hooks_priv_t);
        if (!hooks_priv)
                goto out;

        pthread_mutex_init (&hooks_priv->mutex, NULL);
        pthread_cond_init (&hooks_priv->cond, NULL);
        CDS_INIT_LIST_HEAD (&hooks_priv->list);
        hooks_priv->waitcount = 0;

        *new = hooks_priv;
        ret = 0;
out:
        return ret;
}

int
glusterd_hooks_spawn_worker (xlator_t *this)
{
        int                       ret          = -1;
        glusterd_conf_t           *conf        = NULL;
        glusterd_hooks_private_t  *hooks_priv  = NULL;


        ret = glusterd_hooks_priv_init (&hooks_priv);
        if (ret)
                goto out;

        conf = this->private;
        conf->hooks_priv = hooks_priv;
        ret = pthread_create (&hooks_priv->worker, NULL, hooks_worker,
                              (void *)this);
        if (ret)
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_SPAWN_THREADS_FAIL, "Failed to spawn post "
                        "hooks worker thread");
out:
        return ret;
}
