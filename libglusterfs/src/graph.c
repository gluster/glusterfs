/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
        char         timestr[64];
        time_t       utime = 0;
        pid_t        mypid = 0;
        struct utsname uname_buf = {{0, }, };
        int            uname_ret = -1;

        mypid = getpid ();
        uname_ret   = uname (&uname_buf);

        utime = time (NULL);
        gf_time_fmt (timestr, sizeof timestr, utime, gf_timefmt_FT);
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
                        const char *type, const char *name,
                        gf_boolean_t autoload)
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

        ixl->is_autoloaded = autoload;

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
glusterfs_graph_acl (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
        int ret = 0;
        cmd_args_t      *cmd_args = NULL;

        cmd_args = &ctx->cmd_args;

        if (!cmd_args->acl)
                return 0;

        ret = glusterfs_graph_insert (graph, ctx, "system/posix-acl",
                                      "posix-acl-autoload", 1);
        return ret;
}

int
glusterfs_graph_worm (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
        int ret = 0;
        cmd_args_t      *cmd_args = NULL;

        cmd_args = &ctx->cmd_args;

        if (!cmd_args->worm)
                return 0;

        ret = glusterfs_graph_insert (graph, ctx, "features/worm",
                                      "worm-autoload", 1);
        return ret;
}


int
glusterfs_graph_meta (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
        int ret = 0;

	if (!ctx->master)
		return 0;

        ret = glusterfs_graph_insert (graph, ctx, "meta",
                                      "meta-autoload", 1);
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
                                      "mac-compat-autoload", 1);

        return ret;
}

int
glusterfs_graph_gfid_access (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
        int ret = 0;
        cmd_args_t      *cmd_args = NULL;

        cmd_args = &ctx->cmd_args;

        if (!cmd_args->aux_gfid_mount)
                return 0;

        ret = glusterfs_graph_insert (graph, ctx, "features/gfid-access",
                                      "gfid-access-autoload", 1);
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
                                        gf_log (trav->name, GF_LOG_INFO,
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
        xlator_t           *trav = NULL;
        int                 ret = -1;
        char               *errstr = NULL;

        trav = graph->first;

        while (trav) {
                if (list_empty (&trav->volume_options))
                        continue;

                ret = xlator_options_validate (trav, trav->options, &errstr);
                if (ret) {
                        gf_log (trav->name, GF_LOG_ERROR,
                                "validation failed: %s", errstr);
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


static int
_log_if_unknown_option (dict_t *dict, char *key, data_t *value, void *data)
{
        volume_option_t   *found = NULL;
        xlator_t          *xl = NULL;

        xl = data;

        found = xlator_volume_option_get (xl, key);

        if (!found) {
                gf_log (xl->name, GF_LOG_WARNING,
                        "option '%s' is not recognized", key);
        }

        return 0;
}


static void
_xlator_check_unknown_options (xlator_t *xl, void *data)
{
        dict_foreach (xl->options, _log_if_unknown_option, xl);
}


int
glusterfs_graph_unknown_options (glusterfs_graph_t *graph)
{
        xlator_foreach (graph->first, _xlator_check_unknown_options, NULL);
        return 0;
}


void
fill_uuid (char *uuid, int size)
{
        char           hostname[256] = {0,};
        struct timeval tv = {0,};
        char           now_str[64];

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

        gf_time_fmt (now_str, sizeof now_str, tv.tv_sec, gf_timefmt_dirent);
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
        if (ret) {
                gf_log ("graph", GF_LOG_ERROR, "glusterfs graph settop failed");
                return -1;
        }

        /* XXX: WORM VOLUME */
        ret = glusterfs_graph_worm (graph, ctx);
        if (ret) {
                gf_log ("graph", GF_LOG_ERROR, "glusterfs graph worm failed");
                return -1;
        }
        ret = glusterfs_graph_acl (graph, ctx);
        if (ret) {
                gf_log ("graph", GF_LOG_ERROR, "glusterfs graph ACL failed");
                return -1;
        }

        /* XXX: MAC COMPAT */
        ret = glusterfs_graph_mac_compat (graph, ctx);
        if (ret) {
                gf_log ("graph", GF_LOG_ERROR, "glusterfs graph mac compat failed");
                return -1;
        }

        /* XXX: gfid-access */
        ret = glusterfs_graph_gfid_access (graph, ctx);
        if (ret) {
                gf_log ("graph", GF_LOG_ERROR,
                        "glusterfs graph 'gfid-access' failed");
                return -1;
        }

	/* XXX: topmost xlator */
	ret = glusterfs_graph_meta (graph, ctx);
	if (ret) {
		gf_log ("graph", GF_LOG_ERROR,
			"glusterfs graph meta failed");
		return -1;
	}

        /* XXX: this->ctx setting */
        for (trav = graph->first; trav; trav = trav->next) {
                trav->ctx = ctx;
        }

        /* XXX: DOB setting */
        gettimeofday (&graph->dob, NULL);

        fill_uuid (graph->graph_uuid, 128);

        graph->id = ctx->graph_id++;

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
        if (ret) {
                gf_log ("graph", GF_LOG_ERROR, "validate options failed");
                return ret;
        }

        /* XXX: perform init () */
        ret = glusterfs_graph_init (graph);
        if (ret) {
                gf_log ("graph", GF_LOG_ERROR, "init failed");
                return ret;
        }

        ret = glusterfs_graph_unknown_options (graph);
        if (ret) {
                gf_log ("graph", GF_LOG_ERROR, "unknown options failed");
                return ret;
        }

        /* XXX: log full graph (_gf_dump_details) */

        list_add (&graph->list, &ctx->graphs);
        ctx->active = graph;

        /* XXX: attach to master and set active pointer */
        if (ctx->master) {
                ret = xlator_notify (ctx->master, GF_EVENT_GRAPH_NEW, graph);
                if (ret) {
                        gf_log ("graph", GF_LOG_ERROR,
                                "graph new notification failed");
                        return ret;
                }
                ((xlator_t *)ctx->master)->next = graph->top;
        }

        /* XXX: perform parent up */
        ret = glusterfs_graph_parent_up (graph);
        if (ret) {
                gf_log ("graph", GF_LOG_ERROR, "parent up notification failed");
                return ret;
        }

        return 0;
}


int
xlator_equal_rec (xlator_t *xl1, xlator_t *xl2)
{
        xlator_list_t *trav1 = NULL;
        xlator_list_t *trav2 = NULL;
        int            ret   = 0;

        if (xl1 == NULL || xl2 == NULL) {
                gf_log ("xlator", GF_LOG_DEBUG, "invalid argument");
                return -1;
        }

        trav1 = xl1->children;
        trav2 = xl2->children;

        while (trav1 && trav2) {
                ret = xlator_equal_rec (trav1->xlator, trav2->xlator);
                if (ret) {
                        gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                                "xlators children not equal");
                        goto out;
                }

                trav1 = trav1->next;
                trav2 = trav2->next;
        }

        if (trav1 || trav2) {
                ret = -1;
                goto out;
        }

        if (strcmp (xl1->name, xl2->name)) {
                ret = -1;
                goto out;
        }

	/* type could have changed even if xlator names match,
	   e.g cluster/distrubte and cluster/nufa share the same
	   xlator name
	*/
        if (strcmp (xl1->type, xl2->type)) {
                ret = -1;
                goto out;
        }
out :
        return ret;
}


gf_boolean_t
is_graph_topology_equal (glusterfs_graph_t *graph1, glusterfs_graph_t *graph2)
{
        xlator_t    *trav1    = NULL;
        xlator_t    *trav2    = NULL;
        gf_boolean_t ret      = _gf_true;

        trav1 = graph1->first;
        trav2 = graph2->first;

        ret = xlator_equal_rec (trav1, trav2);

        if (ret) {
                gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                        "graphs are not equal");
                ret = _gf_false;
                goto out;
        }

        ret = _gf_true;
        gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                "graphs are equal");

out:
        return ret;
}


/* Function has 3types of return value 0, -ve , 1
 *   return 0          =======> reconfiguration of options has succeeded
 *   return 1          =======> the graph has to be reconstructed and all the xlators should be inited
 *   return -1(or -ve) =======> Some Internal Error occurred during the operation
 */
int
glusterfs_volfile_reconfigure (int oldvollen, FILE *newvolfile_fp,
                               glusterfs_ctx_t *ctx, const char *oldvolfile)
{
        glusterfs_graph_t *oldvolfile_graph = NULL;
        glusterfs_graph_t *newvolfile_graph = NULL;
        FILE              *oldvolfile_fp    = NULL;
        gf_boolean_t      active_graph_found = _gf_true;

        int ret = -1;

        if (!oldvollen) {
                ret = 1; // Has to call INIT for the whole graph
                goto out;
        }

        if (!ctx) {
                gf_log ("glusterfsd-mgmt", GF_LOG_ERROR,
			"ctx is NULL");
		goto out;
	}

        oldvolfile_graph = ctx->active;
        if (!oldvolfile_graph) {
                active_graph_found = _gf_false;
                gf_log ("glusterfsd-mgmt", GF_LOG_ERROR,
                        "glusterfs_ctx->active is NULL");

                oldvolfile_fp = tmpfile ();
                if (!oldvolfile_fp) {
                        gf_log ("glusterfsd-mgmt", GF_LOG_ERROR, "Unable to "
                                "create temporary volfile: (%s)",
                                strerror (errno));
                        goto out;
                }

                fwrite (oldvolfile, oldvollen, 1, oldvolfile_fp);
                fflush (oldvolfile_fp);
                if (ferror (oldvolfile_fp)) {
                        goto out;
                }

                oldvolfile_graph = glusterfs_graph_construct (oldvolfile_fp);
                if (!oldvolfile_graph)
                        goto out;
        }

        newvolfile_graph = glusterfs_graph_construct (newvolfile_fp);
        if (!newvolfile_graph) {
                goto out;
        }

	glusterfs_graph_prepare (newvolfile_graph, ctx);

        if (!is_graph_topology_equal (oldvolfile_graph,
                                      newvolfile_graph)) {

                ret = 1;
                gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                        "Graph topology not equal(should call INIT)");
                goto out;
        }

        gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                "Only options have changed in the new "
                "graph");

        /* */
        ret = glusterfs_graph_reconfigure (oldvolfile_graph,
                                           newvolfile_graph);
        if (ret) {
                gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                        "Could not reconfigure new options in old graph");
                goto out;
        }

        ret = 0;
out:
        if (oldvolfile_fp)
                fclose (oldvolfile_fp);

        /*  Do not simply destroy the old graph here. If the oldgraph
            is constructed here in this function itself instead of getting
            it from ctx->active (which happens only of ctx->active is NULL),
            then destroy the old graph. If some i/o is still happening in
            the old graph and the old graph is obtained from ctx->active,
            then destroying the graph will cause problems.
        */
        if (!active_graph_found && oldvolfile_graph)
                glusterfs_graph_destroy (oldvolfile_graph);
        if (newvolfile_graph)
                glusterfs_graph_destroy (newvolfile_graph);

        return ret;
}


int
glusterfs_graph_reconfigure (glusterfs_graph_t *oldgraph,
                             glusterfs_graph_t *newgraph)
{
        xlator_t   *old_xl   = NULL;
        xlator_t   *new_xl   = NULL;

        GF_ASSERT (oldgraph);
        GF_ASSERT (newgraph);

        old_xl   = oldgraph->first;
        while (old_xl->is_autoloaded) {
                old_xl = old_xl->children->xlator;
        }

        new_xl   = newgraph->first;
        while (new_xl->is_autoloaded) {
                new_xl = new_xl->children->xlator;
        }

        return xlator_tree_reconfigure (old_xl, new_xl);
}

int
glusterfs_graph_destroy (glusterfs_graph_t *graph)
{
        GF_VALIDATE_OR_GOTO ("graph", graph, out);

        xlator_tree_free (graph->first);

        list_del_init (&graph->list);
        GF_FREE (graph);
out:
        return 0;
}
