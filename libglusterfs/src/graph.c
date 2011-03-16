/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include <dlfcn.h>
#include <netdb.h>
#include <fnmatch.h>
#include "defaults.h"




#if 0
static void
_gf_dump_details (int argc, char **argv)
{
        extern FILE *gf_log_logfile;
        int          i = 0;
        char         timestr[256];
        time_t       utime = 0;
        struct tm   *tm = NULL;
        pid_t        mypid = 0;
        struct utsname uname_buf = {{0, }, };
        int            uname_ret = -1;

        utime = time (NULL);
        tm    = localtime (&utime);
        mypid = getpid ();
        uname_ret   = uname (&uname_buf);

        /* Which git? What time? */
        strftime (timestr, 256, "%Y-%m-%d %H:%M:%S", tm);
        fprintf (gf_log_logfile,
                 "========================================"
                 "========================================\n");
        fprintf (gf_log_logfile, "Version      : %s %s built on %s %s\n",
                 PACKAGE_NAME, PACKAGE_VERSION, __DATE__, __TIME__);
        fprintf (gf_log_logfile, "git: %s\n",
                 GLUSTERFS_REPOSITORY_REVISION);
        fprintf (gf_log_logfile, "Starting Time: %s\n", timestr);
        fprintf (gf_log_logfile, "Command line : ");
        for (i = 0; i < argc; i++) {
                fprintf (gf_log_logfile, "%s ", argv[i]);
        }

        fprintf (gf_log_logfile, "\nPID          : %d\n", mypid);

        if (uname_ret == 0) {
                fprintf (gf_log_logfile, "System name  : %s\n",
                         uname_buf.sysname);
                fprintf (gf_log_logfile, "Nodename     : %s\n",
                         uname_buf.nodename);
                fprintf (gf_log_logfile, "Kernel Release : %s\n",
                         uname_buf.release);
                fprintf (gf_log_logfile, "Hardware Identifier: %s\n",
                         uname_buf.machine);
        }


        fprintf (gf_log_logfile, "\n");
        fflush (gf_log_logfile);
}
#endif


static int
_log_if_option_is_invalid (xlator_t *xl, data_pair_t *pair)
{
        volume_opt_list_t *vol_opt = NULL;
        volume_option_t   *opt     = NULL;
        int                i     = 0;
        int                index = 0;
        int                found = 0;

        /* Get the first volume_option */
        list_for_each_entry (vol_opt, &xl->volume_options, list) {
                /* Warn for extra option */
                if (!vol_opt->given_opt)
                        break;

                opt = vol_opt->given_opt;
                for (index = 0;
                     ((index < ZR_OPTION_MAX_ARRAY_SIZE) &&
                      (opt[index].key && opt[index].key[0]));  index++)
                        for (i = 0; (i < ZR_VOLUME_MAX_NUM_KEY) &&
                                     opt[index].key[i]; i++) {
                                if (fnmatch (opt[index].key[i],
                                             pair->key,
                                             FNM_NOESCAPE) == 0) {
                                        found = 1;
                                        break;
                                }
                        }
        }

        if (!found) {
                gf_log (xl->name, GF_LOG_WARNING,
                        "option '%s' is not recognized",
                        pair->key);
        }
        return 0;
}


int
glusterfs_xlator_link (xlator_t *pxl, xlator_t *cxl)
{
        xlator_list_t   *xlchild = NULL;
        xlator_list_t   *xlparent = NULL;
        xlator_list_t  **tmp = NULL;

        xlparent = (void *) GF_CALLOC (1, sizeof (*xlparent),
                                       gf_common_mt_xlator_list_t);
        if (!xlparent)
                return -1;

        xlchild = (void *) GF_CALLOC (1, sizeof (*xlchild),
                                      gf_common_mt_xlator_list_t);
        if (!xlchild) {
                GF_FREE (xlparent);

                return -1;
        }

        xlparent->xlator = pxl;
        for (tmp = &cxl->parents; *tmp; tmp = &(*tmp)->next);
        *tmp = xlparent;

        xlchild->xlator = cxl;
        for (tmp = &pxl->children; *tmp; tmp = &(*tmp)->next);
        *tmp = xlchild;

        return 0;
}


void
glusterfs_graph_set_first (glusterfs_graph_t *graph, xlator_t *xl)
{
        xl->next = graph->first;
        if (graph->first)
                ((xlator_t *)graph->first)->prev = xl;
        graph->first = xl;

        graph->xl_count++;
}


int
glusterfs_graph_insert (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx,
                        const char *type, const char *name)
{
        xlator_t        *ixl = NULL;

        if (!ctx->master) {
                gf_log ("glusterfs", GF_LOG_ERROR,
                        "volume \"%s\" can be added from command line only "
                        "on client side", type);

                return -1;
        }

        ixl = GF_CALLOC (1, sizeof (*ixl), gf_common_mt_xlator_t);
        if (!ixl)
                return -1;

        ixl->ctx      = ctx;
        ixl->graph    = graph;
        ixl->options  = get_new_dict ();
        if (!ixl->options)
                goto err;

        ixl->name  = gf_strdup (name);
        if (!ixl->name)
                goto err;

        if (xlator_set_type (ixl, type) == -1) {
                gf_log ("glusterfs", GF_LOG_ERROR,
                        "%s (%s) initialization failed",
                        name, type);
                return -1;
        }

        if (glusterfs_xlator_link (ixl, graph->top) == -1)
                goto err;
        glusterfs_graph_set_first (graph, ixl);
        graph->top = ixl;

        return 0;
err:
        xlator_destroy (ixl);
        return -1;
}


int
glusterfs_graph_readonly (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
        int ret = 0;
        cmd_args_t      *cmd_args = NULL;

        cmd_args = &ctx->cmd_args;

        if (!cmd_args->read_only)
                return 0;

        ret = glusterfs_graph_insert (graph, ctx, "features/read-only",
                                      "readonly-autoload");
        return ret;
}


int
glusterfs_graph_mac_compat (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
        int ret = 0;
        cmd_args_t      *cmd_args = NULL;

        cmd_args = &ctx->cmd_args;

        if (cmd_args->mac_compat == GF_OPTION_DISABLE)
                return 0;

        ret = glusterfs_graph_insert (graph, ctx, "features/mac-compat",
                                      "mac-compat-autoload");

        return ret;
}


static void
gf_add_cmdline_options (glusterfs_graph_t *graph, cmd_args_t *cmd_args)
{
        int                      ret = 0;
        xlator_t                *trav = NULL;
        xlator_cmdline_option_t *cmd_option = NULL;

        trav = graph->first;

        while (trav) {
                list_for_each_entry (cmd_option,
                                     &cmd_args->xlator_options, cmd_args) {
                        if (!fnmatch (cmd_option->volume,
                                      trav->name, FNM_NOESCAPE)) {
                                ret = dict_set_str (trav->options,
                                                    cmd_option->key,
                                                    cmd_option->value);
                                if (ret == 0) {
                                        gf_log (trav->name, GF_LOG_WARNING,
                                                "adding option '%s' for "
                                                "volume '%s' with value '%s'",
                                                cmd_option->key, trav->name,
                                                cmd_option->value);
                                } else {
                                        gf_log (trav->name, GF_LOG_WARNING,
                                                "adding option '%s' for "
                                                "volume '%s' failed: %s",
                                                cmd_option->key, trav->name,
                                                strerror (-ret));
                                }
                        }
                }
                trav = trav->next;
        }
}


int
glusterfs_graph_validate_options (glusterfs_graph_t *graph)
{
        volume_opt_list_t  *vol_opt = NULL;
        xlator_t           *trav = NULL;
        int                 ret = -1;

        trav = graph->first;

        while (trav) {
                if (list_empty (&trav->volume_options))
                        continue;

                vol_opt = list_entry (trav->volume_options.next,
                                      volume_opt_list_t, list);

                ret = validate_xlator_volume_options (trav,
                                                      vol_opt->given_opt);
                if (ret) {
                        gf_log (trav->name, GF_LOG_ERROR,
                                "validating translator failed");
                        return ret;
                }
                trav = trav->next;
        }

        return 0;
}


int
glusterfs_graph_init (glusterfs_graph_t *graph)
{
        xlator_t           *trav = NULL;
        int                 ret = -1;

        trav = graph->first;

        while (trav) {
                ret = xlator_init (trav);
                if (ret) {
                        gf_log (trav->name, GF_LOG_ERROR,
                                "initializing translator failed");
                        return ret;
                }
                trav = trav->next;
        }

        return 0;
}


int
glusterfs_graph_unknown_options (glusterfs_graph_t *graph)
{
        data_pair_t        *pair = NULL;
        xlator_t           *trav = NULL;

        trav = graph->first;

        /* Validate again phase */
        while (trav) {
                pair = trav->options->members_list;
                while (pair) {
                        _log_if_option_is_invalid (trav, pair);
                        pair = pair->next;
                }
                trav = trav->next;
        }

        return 0;
}


void
fill_uuid (char *uuid, int size)
{
        char           hostname[256] = {0,};
        struct timeval tv = {0,};
        struct tm      now = {0, };
        char           now_str[32];

        if (gettimeofday (&tv, NULL) == -1) {
                gf_log ("graph", GF_LOG_ERROR,
                        "gettimeofday: failed %s",
                        strerror (errno));
        }

        if (gethostname (hostname, 256) == -1) {
                gf_log ("graph", GF_LOG_ERROR,
                        "gethostname: failed %s",
                        strerror (errno));
        }

        localtime_r (&tv.tv_sec, &now);
        strftime (now_str, 32, "%Y/%m/%d-%H:%M:%S", &now);
        snprintf (uuid, size, "%s-%d-%s:%"GF_PRI_SUSECONDS,
                  hostname, getpid(), now_str, tv.tv_usec);

        return;
}


int
glusterfs_graph_settop (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
        const char *volume_name = NULL;
        xlator_t   *trav = NULL;

        volume_name = ctx->cmd_args.volume_name;

        if (!volume_name) {
                graph->top = graph->first;
                return 0;
        }

        for (trav = graph->first; trav; trav = trav->next) {
                if (strcmp (trav->name, volume_name) == 0) {
                        graph->top = trav;
                        return 0;
                }
        }

        return -1;
}


int
glusterfs_graph_parent_up (glusterfs_graph_t *graph)
{
        xlator_t *trav = NULL;
        int       ret = -1;

        trav = graph->first;

        while (trav) {
                if (!xlator_has_parent (trav)) {
                        ret = xlator_notify (trav, GF_EVENT_PARENT_UP, trav);
                }

                if (ret)
                        break;

                trav = trav->next;
        }

        return ret;
}


int
glusterfs_graph_prepare (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
        xlator_t    *trav = NULL;
        int          ret = 0;

        /* XXX: CHECKSUM */

        /* XXX: attach to -n volname */
        ret = glusterfs_graph_settop (graph, ctx);
        if (ret)
                return -1;

        /* XXX: RO VOLUME */
        ret = glusterfs_graph_readonly (graph, ctx);
        if (ret)
                return -1;

        /* XXX: MAC COMPAT */
        ret = glusterfs_graph_mac_compat (graph, ctx);
        if (ret)
                return -1;

        /* XXX: this->ctx setting */
        for (trav = graph->first; trav; trav = trav->next) {
                trav->ctx = ctx;
        }

        /* XXX: DOB setting */
        gettimeofday (&graph->dob, NULL);

        fill_uuid (graph->graph_uuid, 128);

        /* XXX: --xlator-option additions */
        gf_add_cmdline_options (graph, &ctx->cmd_args);


        return 0;
}


int
glusterfs_graph_activate (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
        int ret = 0;

        /* XXX: all xlator options validation */
        ret = glusterfs_graph_validate_options (graph);
        if (ret)
                return ret;

        /* XXX: perform init () */
        ret = glusterfs_graph_init (graph);
        if (ret)
                return ret;

        ret = glusterfs_graph_unknown_options (graph);
        if (ret)
                return ret;

        /* XXX: log full graph (_gf_dump_details) */

        list_add (&graph->list, &ctx->graphs);
        ctx->active = graph;

        /* XXX: attach to master and set active pointer */
        if (ctx->master)
                ret = xlator_notify (ctx->master, GF_EVENT_GRAPH_NEW, graph);
        if (ret)
                return ret;

        /* XXX: perform parent up */
        ret = glusterfs_graph_parent_up (graph);
        if (ret)
                return ret;


        return 0;
}

int
glusterfs_graph_reconfigure (glusterfs_graph_t *oldgraph,
                             glusterfs_graph_t *newgraph)
{
        xlator_t *old_xl = NULL;
        xlator_t *new_xl = NULL;

        GF_ASSERT (oldgraph);
        GF_ASSERT (newgraph);

        old_xl   = oldgraph->first;
        new_xl   = newgraph->first;

        return xlator_tree_reconfigure (old_xl, new_xl);
}

int
glusterfs_graph_destroy (glusterfs_graph_t *graph)
{
        return 0;
}
