/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is GF_FREE software; you can redistribute it and/or modify
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
#include "glusterd.h"
#include "defaults.h"
#include "logging.h"
#include "dict.h"
#include "graph-utils.h"
#include "glusterd-mem-types.h"
#include "cli1.h"
#include "glusterd-volgen.h"


/* dispatch table for VOLUME SET
 *
 * First field is the <key>, for the purpose of looking it up
 * in volume dictionary.
 *
 * Second field is of the format "<xlator-type>:<action-specifier>".
 * The ":<action-specifier>" part can be omitted, which is handled
 * as if <action-specifier> is same as <key>.
 *
 * There are two type of entries: basic and special.
 *
 * - Basic entries are the ones where the <action-specifier>
 *   does _not_ start with the bang! character ('!').
 *   In their case, <action-specifier> is understood as
 *   an option for <xlator-type>. Their effect is to copy over
 *   the volinfo->dict[<key>] value to the graph nodes of
 *   type <xlator-type> (if such a value is set). You are free to
 *   add entries of this type, they will become functional just
 *   by being present in the table.
 *
 * - Special entries where the <action-specifier> starts
 *   with the bang!. They are not applied to all graphs
 *   during generation, and you cannot extend them in a
 *   trivial way which could be just picked up. Better
 *   not touch them unless you know what you do.
 */

struct volopt_map_entry glusterd_volopt_map[] = {
        {"lookup-unhashed",             "cluster/distribute"},
        {"min-free-disk",               "cluster/distribute"},

        {"entry-change-log",            "cluster/replicate"},
        {"read-subvolume",              "cluster/replicate"},
        {"background-self-heal-count",  "cluster/replicate"},
        {"metadata-self-heal",          "cluster/replicate"},
        {"data-self-heal",              "cluster/replicate"},
        {"entry-self-heal",             "cluster/replicate"},
        {"strict-readdir",              "cluster/replicate"},
        {"data-self-heal-window-size",  "cluster/replicate"},
        {"data-change-log",             "cluster/replicate"},
        {"metadata-change-log",         "cluster/replicate"},

        {"block-size",                  "cluster/stripe"},

        {"latency-measurement",         "debug/io-stats"},
        {"dump-fd-stats",               "debug/io-stats"},

        {"max-file-size",               "performance/io-cache"},
        {"min-file-size",               "performance/io-cache"},
        {"cache-timeout",               "performance/io-cache"},
        {"cache-size",                  "performance/io-cache"},
        {"priority",                    "performance/io-cache"},

        {"thread-count",                "performance/io-threads"},

        {"disk-usage-limit",            "performance/quota"},
        {"min-free-disk-limit",         "performance/quota"},

        {"window-size",                 "performance/write-behind:cache-size"},

        {"frame-timeout",               "protocol/client"},
        {"ping-timeout",                "protocol/client"},

        {"inode-lru-limit",             "protocol/server"},

        {"write-behind",                "performance/write-behind:!perf"},
        {"read-ahead",                  "performance/read-ahead:!perf"},
        {"io-cache",                    "performance/io-cache:!perf"},
        {"quick-read",                  "performance/quick-read:!perf"},
        {"stat-prefetch",               "performance/stat-prefetch:!perf"},

        {NULL,                          }
};


/* Default entries (as of now, only for special volopts). */

struct volopt_map_entry2 {
        char *key;
        char *voltype;
        char *option;
        char *value;
};

static struct volopt_map_entry2 default_volopt_map2[] = {
        {"write-behind",  NULL, NULL, "on"},
        {"read-ahead",    NULL, NULL, "on"},
        {"io-cache",      NULL, NULL, "on"},
        {"quick-read",    NULL, NULL, "on"},
        {NULL,                            }
};

#define VOLGEN_GET_NFS_DIR(path)                                        \
        do {                                                            \
                glusterd_conf_t *priv = THIS->private;                  \
                snprintf (path, PATH_MAX, "%s/nfs", priv->workdir);     \
        } while (0);                                                    \

#define VOLGEN_GET_VOLUME_DIR(path, volinfo)                            \
        do {                                                            \
                glusterd_conf_t *priv = THIS->private;                  \
                snprintf (path, PATH_MAX, "%s/vols/%s", priv->workdir,  \
                          volinfo->volname);                            \
        } while (0);                                                    \




/*********************************************
 *
 * xlator generation / graph manipulation API
 *
 *********************************************/



static xlator_t *
xlator_instantiate_va (const char *type, const char *format, va_list arg)
{
        xlator_t *xl = NULL;
        char *volname = NULL;
        int ret = 0;

        ret = gf_vasprintf (&volname, format, arg);
        if (ret < 0) {
                volname = NULL;

                goto error;
        }

        xl = GF_CALLOC (1, sizeof (*xl), gf_common_mt_xlator_t);
        if (!xl)
                goto error;
        ret = xlator_set_type_virtual (xl, type);
        if (ret)
                goto error;
        xl->options = get_new_dict();
        if (!xl->options)
                goto error;
        xl->name = volname;

        return xl;

 error:
        gf_log ("", GF_LOG_ERROR, "creating xlator of type %s failed",
                type);
        if (volname)
                GF_FREE (volname);
        if (xl)
                xlator_destroy (xl);

        return NULL;
}

#ifdef __not_used_as_of_now_
static xlator_t *
xlator_instantiate (const char *type, const char *format, ...)
{
        va_list arg;
        xlator_t *xl;

        va_start (arg, format);
        xl = xlator_instantiate_va (type, format, arg);
        va_end (arg);

        return xl;
}
#endif

static int
volgen_xlator_link (xlator_t *pxl, xlator_t *cxl)
{
        int ret = 0;

        ret = glusterfs_xlator_link (pxl, cxl);
        if (ret == -1) {
                gf_log ("", GF_LOG_ERROR,
                        "Out of memory, cannot link xlators %s <- %s",
                        pxl->name, cxl->name);
        }

        return ret;
}

static int
volgen_graph_link (glusterfs_graph_t *graph, xlator_t *xl)
{
        int ret = 0;

        /* no need to care about graph->top here */
        if (graph->first)
                ret = volgen_xlator_link (xl, graph->first);
        if (ret == -1) {
                gf_log ("", GF_LOG_ERROR, "failed to add graph entry %s",
                        xl->name);

                return -1;
        }

        return 0;
}

static xlator_t *
volgen_graph_add_as (glusterfs_graph_t *graph, const char *type,
                     const char *format, ...)
{
        va_list arg;
        xlator_t *xl = NULL;

        va_start (arg, format);
        xl = xlator_instantiate_va (type, format, arg);
        va_end (arg);

        if (!xl)
                return NULL;

        if (volgen_graph_link (graph, xl)) {
                xlator_destroy (xl);

                return NULL;
        } else
                glusterfs_graph_set_first (graph, xl);

        return xl;
}

static xlator_t *
volgen_graph_add_nolink (glusterfs_graph_t *graph, const char *type,
                         const char *format, ...)
{
        va_list arg;
        xlator_t *xl = NULL;

        va_start (arg, format);
        xl = xlator_instantiate_va (type, format, arg);
        va_end (arg);

        if (!xl)
                return NULL;

        glusterfs_graph_set_first (graph, xl);

        return xl;
}

static xlator_t *
volgen_graph_add (glusterfs_graph_t *graph, char *type, char *volname)
{
        char *shorttype = NULL;

        shorttype = strrchr (type, '/');
        GF_ASSERT (shorttype);
        shorttype++;
        GF_ASSERT (*shorttype);

        return volgen_graph_add_as (graph, type, "%s-%s", volname, shorttype);
}

/* XXX Seems there is no such generic routine?
 * Maybe should put to xlator.c ??
 */
static int
xlator_set_option (xlator_t *xl, char *key, char *value)
{
        char *dval     = NULL;

        dval = gf_strdup (value);
        if (!dval) {
                gf_log ("", GF_LOG_ERROR,
                        "failed to set xlator opt: %s[%s] = %s",
                        xl->name, key, value);

                return -1;
        }

        return dict_set_dynstr (xl->options, key, dval);
}

static inline xlator_t *
first_of (glusterfs_graph_t *graph)
{
        return (xlator_t *)graph->first;
}




/**************************
 *
 * Volume generation engine
 *
 **************************/


static int
volgen_graph_set_options_generic (glusterfs_graph_t *graph, dict_t *dict,
                                  void *param,
                                  int (*handler) (glusterfs_graph_t *graph,
                                                  struct volopt_map_entry2 *vme2,
                                                  void *param))
{
        struct volopt_map_entry *vme = NULL;
        struct volopt_map_entry2 vme2 = {0,};
        struct volopt_map_entry2 *vme2x = NULL;
        char *value = NULL;
        int ret = 0;

        for (vme = glusterd_volopt_map; vme->key; vme++) {
                ret = dict_get_str (dict, vme->key, &value);
                if (ret) {
                        for (vme2x = default_volopt_map2; vme2x->key;
                             vme2x++) {
                                if (strcmp (vme2x->key, vme->key) == 0) {
                                        value = vme2x->value;
                                        ret = 0;
                                        break;
                                }
                        }
                }
                if (ret)
                        continue;

                vme2.key = vme->key;
                vme2.voltype = gf_strdup (vme->voltype);
                if (!vme2.voltype) {
                        gf_log ("", GF_LOG_ERROR, "Out of memory");

                        return -1;
                }
                vme2.option = strchr (vme2.voltype, ':');
                if (vme2.option) {
                        *vme2.option = '\0';
                        vme2.option++;
                } else
                        vme2.option = vme->key;
                vme2.value = value;

                ret = handler (graph, &vme2, param);

                GF_FREE (vme2.voltype);
                if (ret)
                        return -1;
        }

        return 0;
}

static int
basic_option_handler (glusterfs_graph_t *graph, struct volopt_map_entry2 *vme2,
                      void *param)
{
        xlator_t *trav;
        int ret = 0;

        if (vme2->option[0] == '!')
                return 0;

        for (trav = first_of (graph); trav; trav = trav->next) {
                if (strcmp (trav->type, vme2->voltype) != 0)
                        continue;

                ret = xlator_set_option (trav, vme2->option, vme2->value);
                if (ret)
                        return -1;
        }

        return 0;
}

static int
volgen_graph_set_options (glusterfs_graph_t *graph, dict_t *dict)
{
        return volgen_graph_set_options_generic (graph, dict, NULL,
                                                 &basic_option_handler);
}

static int
volgen_graph_merge_sub (glusterfs_graph_t *dgraph, glusterfs_graph_t *sgraph)
{
        xlator_t *trav = NULL;

        GF_ASSERT (dgraph->first);

        if (volgen_xlator_link (first_of (dgraph), first_of (sgraph)) == -1)
                return -1;

        for (trav = first_of (dgraph); trav->next; trav = trav->next);

        trav->next = sgraph->first;
        trav->next->prev = trav;
        dgraph->xl_count += sgraph->xl_count;

        return 0;
}

static int
volgen_write_volfile (glusterfs_graph_t *graph, char *filename)
{
        char *ftmp = NULL;
        FILE *f = NULL;

        if (gf_asprintf (&ftmp, "%s.tmp", filename) == -1) {
                ftmp = NULL;

                goto error;
        }

        f = fopen (ftmp, "w");
        if (!f)
                goto error;

        if (glusterfs_graph_print_file (f, graph) == -1)
                goto error;

        if (fclose (f) == -1)
                goto error;

        if (rename (ftmp, filename) == -1)
                goto error;

        GF_FREE (ftmp);

        return 0;

 error:

        if (ftmp)
                GF_FREE (ftmp);
        if (f)
                fclose (f);

        gf_log ("", GF_LOG_ERROR, "failed to create volfile %s", filename);

        return -1;
}

static void
volgen_graph_free (glusterfs_graph_t *graph)
{
        xlator_t *trav = NULL;
        xlator_t *trav_old = NULL;

        for (trav = first_of (graph) ;; trav = trav->next) {
                if (trav_old)
                        xlator_destroy (trav_old);

                trav_old = trav;

                if (!trav)
                        break;
        }
}

static int
build_graph_generic (glusterfs_graph_t *graph, glusterd_volinfo_t *volinfo,
                     dict_t *mod_dict, void *param,
                     int (*builder) (glusterfs_graph_t *graph,
                                     glusterd_volinfo_t *volinfo,
                                     dict_t *set_dict, void *param))
{
        dict_t *set_dict = NULL;
        int ret = 0;

        if (mod_dict) {
                set_dict = dict_copy (volinfo->dict, NULL);
                if (!set_dict)
                        return -1;
                dict_copy (mod_dict, set_dict);
                /* XXX dict_copy swallows errors */
        } else
                set_dict = volinfo->dict;

        ret = builder (graph, volinfo, set_dict, param);
        if (!ret)
                ret = volgen_graph_set_options (graph, set_dict);

        if (mod_dict)
                dict_destroy (set_dict);

        return ret;
}

static void
get_vol_transport_type (glusterd_volinfo_t *volinfo, char *tt)
{
        volinfo->transport_type == GF_TRANSPORT_RDMA ?
        strcpy (tt, "rdma"):
        strcpy (tt, "tcp");
}

static int
server_graph_builder (glusterfs_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, void *param)
{
        char     *volname = NULL;
        char     *path = NULL;
        int       pump = 0;
        xlator_t *xl = NULL;
        xlator_t *txl = NULL;
        char     *aaa = NULL;
        int       ret = 0;
        char      transt[16] = {0,};

        path = param;
        volname = volinfo->volname;
        get_vol_transport_type (volinfo, transt);

        xl = volgen_graph_add (graph, "storage/posix", volname);
        if (!xl)
                return -1;

        ret = xlator_set_option (xl, "directory", path);
        if (ret)
                return -1;

        xl = volgen_graph_add (graph, "features/access-control", volname);
        if (!xl)
                return -1;

        xl = volgen_graph_add (graph, "features/locks", volname);
        if (!xl)
                return -1;

        ret = dict_get_int32 (volinfo->dict, "enable-pump", &pump);
        if (ret == -ENOENT)
                pump = 0;
        if (pump) {
                txl = first_of (graph);

                xl = volgen_graph_add_as (graph, "protocol/client", "%s-%s",
                                          volname, "replace-brick");
                if (!xl)
                        return -1;
                ret = xlator_set_option (xl, "transport-type", transt);
                if (ret)
                        return -1;
                ret = xlator_set_option (xl, "remote-port", "34034");
                if (ret)
                        return -1;

                xl = volgen_graph_add (graph, "cluster/pump", volname);
                if (!xl)
                        return -1;

                ret = volgen_xlator_link (xl, txl);
                if (ret)
                        return -1;
        }

        xl = volgen_graph_add (graph, "performance/io-threads", volname);
        if (!xl)
                return -1;
        ret = xlator_set_option (xl, "thread-count", "16");
        if (ret)
                return -1;

        xl = volgen_graph_add_as (graph, "debug/io-stats", path);
        if (!xl)
                return -1;

        xl = volgen_graph_add (graph, "protocol/server", volname);
        if (!xl)
                return -1;
        ret = xlator_set_option (xl, "transport-type", transt);
        if (ret)
                return -1;

        ret = gf_asprintf (&aaa, "auth.addr.%s.allow", path);
        if (ret == -1) {
                gf_log ("", GF_LOG_ERROR, "Out of memory");

                return -1;
        }
        ret = xlator_set_option (xl, aaa, "*");
        GF_FREE (aaa);

        return ret;
}


/* builds a graph for server role , with option overrides in mod_dict */
static int
build_server_graph (glusterfs_graph_t *graph, glusterd_volinfo_t *volinfo,
                    dict_t *mod_dict, char *path)
{
        return build_graph_generic (graph, volinfo, mod_dict, path,
                                    &server_graph_builder);
}

static int
perfxl_option_handler (glusterfs_graph_t *graph, struct volopt_map_entry2 *vme2,
                       void *param)
{
        char *volname = NULL;
        gf_boolean_t enabled = _gf_false;

        volname = param;

        if (strcmp (vme2->option, "!perf") != 0)
                return 0;

        if (gf_string2boolean (vme2->value, &enabled) == -1)
                return -1;
        if (!enabled)
                return 0;

        if (volgen_graph_add (graph, vme2->voltype, volname))
                return 0;
        else
                return -1;
}

static int
client_graph_builder (glusterfs_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, void *param)
{
        int                      replicate_count    = 0;
        int                      stripe_count       = 0;
        int                      dist_count         = 0;
        int                      num_bricks         = 0;
        char                     transt[16]         = {0,};
        int                      cluster_count      = 0;
        char                    *volname            = NULL;
        dict_t                  *dict               = NULL;
        glusterd_brickinfo_t    *brick = NULL;
        char                    *replicate_args[]   = {"cluster/replicate",
                                                       "%s-replicate-%d"};
        char                    *stripe_args[]      = {"cluster/stripe",
                                                       "%s-stripe-%d"};
        char                   **cluster_args       = NULL;
        int                      i                  = 0;
        int                      j                  = 0;
        int                      ret                = 0;
        xlator_t                *xl                 = NULL;
        xlator_t                *txl                = NULL;
        xlator_t                *trav               = NULL;

        volname = volinfo->volname;
        dict    = volinfo->dict;
        GF_ASSERT (dict);
        get_vol_transport_type (volinfo, transt);

        list_for_each_entry (brick, &volinfo->bricks, brick_list)
                num_bricks++;

        if (GF_CLUSTER_TYPE_REPLICATE == volinfo->type) {
                if (volinfo->brick_count <= volinfo->sub_count) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Volfile is plain replicated");
                        replicate_count = volinfo->sub_count;
                        dist_count = num_bricks / replicate_count;
                        if (!dist_count) {
                                replicate_count = num_bricks;
                                dist_count = num_bricks / replicate_count;
                        }
                } else {
                        gf_log ("", GF_LOG_DEBUG,
                                "Volfile is distributed-replicated");
                        replicate_count = volinfo->sub_count;
                        dist_count = num_bricks / replicate_count;
                }

        } else if (GF_CLUSTER_TYPE_STRIPE == volinfo->type) {
                if (volinfo->brick_count == volinfo->sub_count) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Volfile is plain striped");
                        stripe_count = volinfo->sub_count;
                        dist_count = num_bricks / stripe_count;
                } else {
                        gf_log ("", GF_LOG_DEBUG,
                                "Volfile is distributed-striped");
                        stripe_count = volinfo->sub_count;
                        dist_count = num_bricks / stripe_count;
                }
        } else {
                gf_log ("", GF_LOG_DEBUG,
                        "Volfile is plain distributed");
                dist_count = num_bricks;
        }

        if (stripe_count && replicate_count) {
                gf_log ("", GF_LOG_DEBUG,
                        "Striped Replicate config not allowed");
                return -1;
        }
        if (replicate_count > 1) {
                cluster_count = replicate_count;
                cluster_args = replicate_args;
        }
        if (stripe_count > 1) {
                cluster_count = stripe_count;
                cluster_args = stripe_args;
        }

        i = 0;
        list_for_each_entry (brick, &volinfo->bricks, brick_list) {
                xl = volgen_graph_add_nolink (graph, "protocol/client",
                                              "%s-client-%d", volname, i);
                if (!xl)
                        return -1;
                ret = xlator_set_option (xl, "remote-host", brick->hostname);
                if (ret)
                        return -1;
                ret = xlator_set_option (xl, "remote-subvolume", brick->path);
                if (ret)
                        return -1;
                ret = xlator_set_option (xl, "transport-type", transt);
                if (ret)
                        return -1;

                i++;
        }

        if (cluster_count > 1) {
                j = 0;
                i = 0;
                txl = first_of (graph);
                for (trav = txl; trav->next; trav = trav->next);
                for (;; trav = trav->prev) {
                        if (i % cluster_count == 0) {
                                xl = volgen_graph_add_nolink (graph,
                                                              cluster_args[0],
                                                              cluster_args[1],
                                                              volname, j);
                                if (!xl)
                                        return -1;
                                j++;
                        }

                        ret = volgen_xlator_link (xl, trav);
                        if (ret)
                                return -1;

                        if (trav == txl)
                                break;
                        i++;
                }
        }

        if (dist_count > 1) {
                xl = volgen_graph_add_nolink (graph, "cluster/distribute",
                                              "%s-dht", volname);
                if (!xl)
                        return -1;

                trav = xl;
                for (i = 0; i < dist_count; i++)
                        trav = trav->next;
                for (; trav != xl; trav = trav->prev) {
                        ret = volgen_xlator_link (xl, trav);
                        if (ret)
                                return -1;
                }
        }

        ret = volgen_graph_set_options_generic (graph, set_dict, volname,
                                                &perfxl_option_handler);
        if (ret)
                return -1;

        xl = volgen_graph_add_as (graph, "debug/io-stats", volname);
        if (!xl)
                return -1;

        return 0;
}


/* builds a graph for client role , with option overrides in mod_dict */
static int
build_client_graph (glusterfs_graph_t *graph, glusterd_volinfo_t *volinfo,
                    dict_t *mod_dict)
{
        return build_graph_generic (graph, volinfo, mod_dict, NULL,
                                    &client_graph_builder);
}


/* builds a graph for nfs server role */
static int
build_nfs_graph (glusterfs_graph_t *graph)
{
        glusterfs_graph_t   cgraph        = {{0,},};
        glusterd_volinfo_t *voliter       = NULL;
        xlator_t           *this          = NULL;
        glusterd_conf_t    *priv          = NULL;
        xlator_t           *nfsxl         = NULL;
        char               *skey          = NULL;
        char                volume_id[64] = {0,};
        int                 ret           = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        nfsxl = volgen_graph_add_as (graph, "nfs/server", "nfs-server");
        if (!nfsxl)
                return -1;
        ret = xlator_set_option (nfsxl, "nfs.dynamic-volumes", "on");
        if (ret)
                return -1;

        list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (voliter->status != GLUSTERD_STATUS_STARTED)
                        continue;

                ret = gf_asprintf (&skey, "rpc-auth.addr.%s.allow",
                                   voliter->volname);
                if (ret == -1)
                        goto oom;
                ret = xlator_set_option (nfsxl, skey, "*");
                GF_FREE (skey);
                if (ret)
                        return -1;

                ret = gf_asprintf (&skey, "nfs3.%s.volume-id",
                                   voliter->volname);
                if (ret == -1)
                        goto oom;
                uuid_unparse (voliter->volume_id, volume_id);
                ret = xlator_set_option (nfsxl, skey, volume_id);
                GF_FREE (skey);
                if (ret)
                        return -1;

                memset (&cgraph, 0, sizeof (cgraph));
                ret = build_client_graph (&cgraph, voliter, NULL);
                if (ret)
                        return -1;
                ret = volgen_graph_merge_sub (graph, &cgraph);
        }

        return ret;

 oom:
        gf_log ("", GF_LOG_ERROR, "Out of memory");

        return -1;
}




/****************************
 *
 * Volume generation interface
 *
 ****************************/


static void
get_brick_filepath (char *filename, glusterd_volinfo_t *volinfo,
                    glusterd_brickinfo_t *brickinfo)
{
        char  path[PATH_MAX]   = {0,};
        char  brick[PATH_MAX]  = {0,};

        GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, brick);
        VOLGEN_GET_VOLUME_DIR (path, volinfo);

        snprintf (filename, PATH_MAX, "%s/%s.%s.%s.vol",
                  path, volinfo->volname,
                  brickinfo->hostname,
                  brick);
}

static int
glusterd_generate_brick_volfile (glusterd_volinfo_t *volinfo,
                                 glusterd_brickinfo_t *brickinfo)
{
        glusterfs_graph_t graph = {{0,},};
        char    filename[PATH_MAX] = {0,};
        int     ret = -1;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        get_brick_filepath (filename, volinfo, brickinfo);

        ret = build_server_graph (&graph, volinfo, NULL, brickinfo->path);
        if (!ret)
                ret = volgen_write_volfile (&graph, filename);

        volgen_graph_free (&graph);

        return ret;
}

static int
generate_brick_volfiles (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t    *brickinfo = NULL;
        int                     ret = -1;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                gf_log ("", GF_LOG_DEBUG,
                        "Found a brick - %s:%s", brickinfo->hostname,
                        brickinfo->path);

                ret = glusterd_generate_brick_volfile (volinfo, brickinfo);
                if (ret)
                        goto out;

        }

        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static void
get_client_filepath (char *filename, glusterd_volinfo_t *volinfo)
{
        char  path[PATH_MAX] = {0,};

        VOLGEN_GET_VOLUME_DIR (path, volinfo);

        snprintf (filename, PATH_MAX, "%s/%s-fuse.vol",
                  path, volinfo->volname);
}

static int
generate_client_volfile (glusterd_volinfo_t *volinfo)
{
        glusterfs_graph_t graph = {{0,},};
        char    filename[PATH_MAX] = {0,};
        int     ret = -1;

        get_client_filepath (filename, volinfo);

        ret = build_client_graph (&graph, volinfo, NULL);
        if (!ret)
                ret = volgen_write_volfile (&graph, filename);

        volgen_graph_free (&graph);

        return ret;
}

int
glusterd_create_rb_volfiles (glusterd_volinfo_t *volinfo,
                             glusterd_brickinfo_t *brickinfo)
{
        int ret = -1;

        ret = glusterd_generate_brick_volfile (volinfo, brickinfo);
        if (!ret)
                ret = generate_client_volfile (volinfo);
        if (!ret)
                ret = glusterd_fetchspec_notify (THIS);

        return ret;
}

int
glusterd_create_volfiles (glusterd_volinfo_t *volinfo)
{
        int ret = -1;

        ret = generate_brick_volfiles (volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR,
                        "Could not generate volfiles for bricks");
                goto out;
        }

        ret = generate_client_volfile (volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR,
                        "Could not generate volfile for client");
                goto out;
        }

        ret = glusterd_fetchspec_notify (THIS);

out:
        return ret;
}

void
glusterd_get_nfs_filepath (char *filename)
{
        char  path[PATH_MAX] = {0,};

        VOLGEN_GET_NFS_DIR (path);

        snprintf (filename, PATH_MAX, "%s/nfs-server.vol", path);
}

int
glusterd_create_nfs_volfile ()
{
        glusterfs_graph_t graph = {{0,},};
        char    filename[PATH_MAX] = {0,};
        int     ret = -1;

        glusterd_get_nfs_filepath (filename);

        ret = build_nfs_graph (&graph);
        if (!ret)
                ret = volgen_write_volfile (&graph, filename);

        volgen_graph_free (&graph);

        return ret;
}

int
glusterd_delete_volfile (glusterd_volinfo_t *volinfo,
                         glusterd_brickinfo_t *brickinfo)
{
        char filename[PATH_MAX] = {0,};

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        get_brick_filepath (filename, volinfo, brickinfo);
        return unlink (filename);
}
