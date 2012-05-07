/*
  Copyright (c) 2007-2012 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "globals.h"
#include "glusterfs.h"
#include "dict.h"
#include "xlator.h"
#include "logging.h"
#include "run.h"
#include "defaults.h"
#include "compat.h"
#include "compat-errno.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include "glusterd-hooks.h"

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
        [GD_OP_RESET_VOLUME]            = EMPTY,
        [GD_OP_SYNC_VOLUME]             = EMPTY,
        [GD_OP_LOG_ROTATE]              = EMPTY,
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
};
#undef EMPTY

static inline gf_boolean_t
glusterd_is_hook_enabled (char *script)
{
        return (script[0] == 'S');
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
        ret = mkdir_if_missing (path, NULL);
        if (ret) {
                gf_log (THIS->name, GF_LOG_CRITICAL, "Unable to create %s due"
                         "to %s", path, strerror (errno));
                goto out;
        }

        GLUSTERD_GET_HOOKS_DIR (version_dir, GLUSTERD_HOOK_VER, priv);
        ret = mkdir_if_missing (version_dir, NULL);
        if (ret) {
                gf_log (THIS->name, GF_LOG_CRITICAL, "Unable to create %s due "
                        "to %s", version_dir, strerror (errno));
                goto out;
        }

        for (op = GD_OP_NONE+1; op < GD_OP_MAX; op++) {
                cmd_subdir = glusterd_hooks_get_hooks_cmd_subdir (op);
                if (strlen (cmd_subdir) == 0)
                        continue;

                snprintf (path, sizeof (path), "%s/%s", version_dir,
                          cmd_subdir);
                ret = mkdir_if_missing (path, NULL);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_CRITICAL,
                                "Unable to create %s due to %s",
                                path, strerror (errno));
                        goto out;
                }

                for (type = GD_COMMIT_HOOK_PRE; type < GD_COMMIT_HOOK_MAX;
                     type++) {
                        snprintf (path, sizeof (path), "%s/%s/%s",
                                  version_dir, cmd_subdir, type_subdir[type]);
                        ret = mkdir_if_missing (path, NULL);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_CRITICAL,
                                        "Unable to create %s due to %s",
                                        path, strerror (errno));
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
        for (i = 1; (ret == 0); i++) {
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
        list_for_each_entry (voliter, &priv->volumes,
                             vol_list) {
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
        glusterd_conf_t *priv                   = NULL;
        runner_t        runner                  = {0, };
        struct dirent   *entry                  = NULL;
        DIR             *hookdir                = NULL;
        char            *volname                = NULL;
        char            **lines                 = NULL;
        int             N                       = 8; /*arbitrary*/
        int             lineno                  = 0;
        int             line_count              = 0;
        int             ret                     = -1;

        this = THIS;
        priv = this->private;

        ret = dict_get_str (op_ctx, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_CRITICAL, "Failed to get volname "
                        "from operation context");
                goto out;
        }

        hookdir = opendir (hooks_path);
        if (!hookdir) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "Failed to open dir %s, due "
                        "to %s", hooks_path, strerror (errno));
                goto out;
        }

        lines = GF_CALLOC (1, N * sizeof (*lines), gf_gld_mt_charptr);
        if (!lines) {
                ret = -1;
                goto out;
        }

        ret = -1;
        line_count = 0;
        glusterd_for_each_entry (entry, hookdir);
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

                glusterd_for_each_entry (entry, hookdir);
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
                        gf_log (this->name, GF_LOG_ERROR, "Failed to add "
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
                        if (lines[lineno])
                                GF_FREE (lines[lineno]);

                GF_FREE (lines);
        }

        if (hookdir)
                closedir (hookdir);

        return ret;
}

