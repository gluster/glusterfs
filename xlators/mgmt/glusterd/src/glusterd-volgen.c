/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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

#include <fnmatch.h>
#include <sys/wait.h>

#if (HAVE_LIB_XML)
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#endif

#include "xlator.h"
#include "glusterd.h"
#include "defaults.h"
#include "logging.h"
#include "dict.h"
#include "graph-utils.h"
#include "trie.h"
#include "glusterd-mem-types.h"
#include "cli1-xdr.h"
#include "glusterd-volgen.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "run.h"


/* dispatch table for VOLUME SET
 * -----------------------------
 *
 * Format of entries:
 *
 * First field is the <key>, for the purpose of looking it up
 * in volume dictionary. Each <key> is of the format "<domain>.<specifier>".
 *
 * Second field is <voltype>.
 *
 * Third field is <option>, if its unset, it's assumed to be
 * the same as <specifier>.
 *
 * Fourth field is <value>. In this context they are used to specify
 * a default. That is, even the volume dict doesn't have a value,
 * we procced as if the default value were set for it.
 *
 * There are two type of entries: basic and special.
 *
 * - Basic entries are the ones where the <option> does _not_ start with
 *   the bang! character ('!').
 *
 *   In their case, <option> is understood as an option for an xlator of
 *   type <voltype>. Their effect is to copy over the volinfo->dict[<key>]
 *   value to all graph nodes of type <voltype> (if such a value is set).
 *
 *   You are free to add entries of this type, they will become functional
 *   just by being present in the table.
 *
 * - Special entries where the <option> starts with the bang!.
 *
 *   They are not applied to all graphs during generation, and you cannot
 *   extend them in a trivial way which could be just picked up. Better
 *   not touch them unless you know what you do.
 *
 * "NODOC" entries are not part of the public interface and are subject
 * to change at any time.
 *
 * Another kind of grouping for options, according to visibility:
 *
 * - Exported: one which is used in the code. These are characterized by
 *   being used a macro as <key> (of the format VKEY_..., defined in
 *   glusterd-volgen.h
 *
 * - Non-exported: the rest; these have string literal <keys>.
 *
 * Adhering to this policy, option name changes shall be one-liners.
 *
 */
typedef enum  { DOC, NO_DOC, GLOBAL_DOC, GLOBAL_NO_DOC } option_type_t;


struct volopt_map_entry {
        char *key;
        char *voltype;
        char *option;
        char *value;
        option_type_t type;
        uint32_t flags;
};

static struct volopt_map_entry glusterd_volopt_map[] = {

        {"cluster.lookup-unhashed",              "cluster/distribute", NULL, NULL, NO_DOC, 0    },
        {"cluster.min-free-disk",                "cluster/distribute", NULL, NULL, NO_DOC, 0    },
	{"cluster.min-free-inodes",              "cluster/distribute", NULL, NULL, NO_DOC, 0    },

        {"cluster.entry-change-log",             "cluster/replicate",  NULL, NULL, NO_DOC, 0     },
        {"cluster.read-subvolume",               "cluster/replicate",  NULL, NULL, NO_DOC, 0    },
        {"cluster.background-self-heal-count",   "cluster/replicate",  NULL, NULL, NO_DOC, 0    },
        {"cluster.metadata-self-heal",           "cluster/replicate",  NULL, NULL, NO_DOC, 0     },
        {"cluster.data-self-heal",               "cluster/replicate",  NULL, NULL, NO_DOC, 0     },
        {"cluster.entry-self-heal",              "cluster/replicate",  NULL, NULL, NO_DOC, 0     },
        {"cluster.self-heal-daemon",             "cluster/replicate",  "!self-heal-daemon" , NULL, NO_DOC, 0     },
        {"cluster.strict-readdir",               "cluster/replicate",  NULL, NULL, NO_DOC, 0     },
        {"cluster.self-heal-window-size",        "cluster/replicate",         "data-self-heal-window-size", NULL, DOC, 0},
        {"cluster.data-change-log",              "cluster/replicate",  NULL, NULL, NO_DOC, 0     },
        {"cluster.metadata-change-log",          "cluster/replicate",  NULL, NULL, NO_DOC, 0     },
        {"cluster.data-self-heal-algorithm",     "cluster/replicate",         "data-self-heal-algorithm", NULL,DOC, 0},

        {"cluster.stripe-block-size",            "cluster/stripe",            "block-size", NULL, DOC, 0},

        {VKEY_DIAG_LAT_MEASUREMENT,              "debug/io-stats",     "latency-measurement", "off", NO_DOC, 0      },
        {"diagnostics.dump-fd-stats",            "debug/io-stats",     NULL, NULL, NO_DOC, 0     },
        {VKEY_DIAG_CNT_FOP_HITS,                 "debug/io-stats",     "count-fop-hits", "off", NO_DOC, 0     },

        {"diagnostics.brick-log-level",          "debug/io-stats",     "!brick-log-level", NULL, DOC, 0},
        {"diagnostics.client-log-level",         "debug/io-stats",     "!client-log-level", NULL, DOC, 0},
        {"diagnostics.brick-sys-log-level",      "debug/io-stats",     "!sys-log-level", NULL, DOC, 0},
        {"diagnostics.client-sys-log-level",     "debug/io-stats",     "!sys-log-level", NULL, DOC, 0},

        {"performance.cache-max-file-size",      "performance/io-cache",      "max-file-size", NULL, DOC, 0},
        {"performance.cache-min-file-size",      "performance/io-cache",      "min-file-size", NULL, DOC, 0},
        {"performance.cache-refresh-timeout",    "performance/io-cache",      "cache-timeout", NULL, DOC, 0},
        {"performance.cache-priority",           "performance/io-cache",      "priority", NULL, DOC, 0},
        {"performance.cache-size",               "performance/io-cache",   NULL, NULL, NO_DOC, 0 },
        {"performance.cache-size",               "performance/quick-read", NULL, NULL, NO_DOC, 0 },
        {"performance.flush-behind",             "performance/write-behind",      "flush-behind", NULL, DOC, 0},

        {"performance.io-thread-count",          "performance/io-threads",    "thread-count", DOC, 0},

        {"performance.disk-usage-limit",         "performance/quota",   NULL, NULL, NO_DOC, 0    },
        {"performance.min-free-disk-limit",      "performance/quota",   NULL, NULL, NO_DOC, 0    },

        {"performance.write-behind-window-size", "performance/write-behind",  "cache-size", NULL, DOC},

        {"network.frame-timeout",                "protocol/client",    NULL, NULL, NO_DOC, 0     },
        {"network.ping-timeout",                 "protocol/client",    NULL, NULL, NO_DOC, 0     },
        {"network.inode-lru-limit",              "protocol/server",    NULL, NULL, NO_DOC, 0     },

        {"auth.allow",                           "protocol/server",           "!server-auth", "*", DOC, 0},
        {"auth.reject",                          "protocol/server",           "!server-auth", NULL, DOC, 0},

        {"transport.keepalive",                   "protocol/server",           "transport.socket.keepalive", NULL, NO_DOC, 0},
        {"server.allow-insecure",                 "protocol/server",          "rpc-auth-allow-insecure", NULL, NO_DOC, 0},

        {"performance.write-behind",             "performance/write-behind",  "!perf", "on", NO_DOC, 0},
        {"performance.read-ahead",               "performance/read-ahead",    "!perf", "on", NO_DOC, 0},
        {"performance.io-cache",                 "performance/io-cache",      "!perf", "on", NO_DOC, 0},
        {"performance.quick-read",               "performance/quick-read",    "!perf", "on", NO_DOC, 0},
        {VKEY_PERF_STAT_PREFETCH,                "performance/stat-prefetch", "!perf", "on", NO_DOC, 0},
        {"performance.client-io-threads",        "performance/io-threads",    "!perf", "off", NO_DOC, 0},
        {VKEY_MARKER_XTIME,                      "features/marker",           "xtime", "off", NO_DOC, OPT_FLAG_FORCE},
        {VKEY_MARKER_XTIME,                      "features/marker",           "!xtime", "off", NO_DOC, OPT_FLAG_FORCE},

        {"nfs.enable-ino32",                     "nfs/server",                "nfs.enable-ino32", NULL, GLOBAL_DOC, 0},
        {"nfs.mem-factor",                       "nfs/server",                "nfs.mem-factor", NULL, GLOBAL_DOC, 0},
        {"nfs.export-dirs",                      "nfs/server",                "nfs3.export-dirs", NULL, GLOBAL_DOC, 0},
        {"nfs.export-volumes",                   "nfs/server",                "nfs3.export-volumes", NULL, GLOBAL_DOC, 0},
        {"nfs.addr-namelookup",                  "nfs/server",                "rpc-auth.addr.namelookup", NULL, GLOBAL_DOC, 0},
        {"nfs.dynamic-volumes",                  "nfs/server",                "nfs.dynamic-volumes", NULL, GLOBAL_NO_DOC, 0},
        {"nfs.register-with-portmap",            "nfs/server",                "rpc.register-with-portmap", NULL, GLOBAL_DOC, 0},
        {"nfs.port",                             "nfs/server",                "nfs.port", NULL, GLOBAL_DOC, 0},

        {"nfs.rpc-auth-unix",                    "nfs/server",                "!rpc-auth.auth-unix.*", NULL, DOC, 0},
        {"nfs.rpc-auth-null",                    "nfs/server",                "!rpc-auth.auth.null.*", NULL, DOC, 0},
        {"nfs.rpc-auth-allow",                   "nfs/server",                "!rpc-auth.addr.*.allow", NULL, DOC, 0},
        {"nfs.rpc-auth-reject",                  "nfs/server",                "!rpc-auth.addr.*.reject", NULL, DOC, 0},
        {"nfs.ports-insecure",                   "nfs/server",                "!rpc-auth.ports.*.insecure", NULL, DOC, 0},
        {"nfs.transport-type",                   "nfs/server",                "!nfs.transport-type", NULL, DOC, 0},

        {"nfs.trusted-sync",                     "nfs/server",                "!nfs3.*.trusted-sync", NULL, DOC, 0},
        {"nfs.trusted-write",                    "nfs/server",                "!nfs3.*.trusted-write", NULL, DOC, 0},
        {"nfs.volume-access",                    "nfs/server",                "!nfs3.*.volume-access", NULL, DOC, 0},
        {"nfs.export-dir",                       "nfs/server",                "!nfs3.*.export-dir", NULL, DOC, 0},
        {"nfs.disable",                          "nfs/server",                "!nfs-disable", NULL, DOC, 0},

        {VKEY_FEATURES_QUOTA,                    "features/marker",           "quota", "off", NO_DOC, OPT_FLAG_FORCE},
        {VKEY_FEATURES_LIMIT_USAGE,              "features/quota",            "limit-set", NULL, NO_DOC, 0},
        {"features.quota-timeout",               "features/quota",            "timeout", "0", DOC, 0},
        {"server.statedump-path",                "protocol/server",           "statedump-path", NULL, NO_DOC, 0},
        {NULL,                                                                }
};



/*********************************************
 *
 * xlator generation / graph manipulation API
 *
 *********************************************/


struct volgen_graph {
        char **errstr;
        glusterfs_graph_t graph;
};
typedef struct volgen_graph volgen_graph_t;

static void
set_graph_errstr (volgen_graph_t *graph, const char *str)
{
        if (!graph->errstr)
                return;

        *graph->errstr = gf_strdup (str);
}

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
        INIT_LIST_HEAD (&xl->volume_options);

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
volgen_graph_link (volgen_graph_t *graph, xlator_t *xl)
{
        int ret = 0;

        /* no need to care about graph->top here */
        if (graph->graph.first)
                ret = volgen_xlator_link (xl, graph->graph.first);
        if (ret == -1) {
                gf_log ("", GF_LOG_ERROR, "failed to add graph entry %s",
                        xl->name);

                return -1;
        }

        return 0;
}

static xlator_t *
volgen_graph_add_as (volgen_graph_t *graph, const char *type,
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
                glusterfs_graph_set_first (&graph->graph, xl);

        return xl;
}

static xlator_t *
volgen_graph_add_nolink (volgen_graph_t *graph, const char *type,
                         const char *format, ...)
{
        va_list arg;
        xlator_t *xl = NULL;

        va_start (arg, format);
        xl = xlator_instantiate_va (type, format, arg);
        va_end (arg);

        if (!xl)
                return NULL;

        glusterfs_graph_set_first (&graph->graph, xl);

        return xl;
}

static xlator_t *
volgen_graph_add (volgen_graph_t *graph, char *type, char *volname)
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

static int
xlator_get_option (xlator_t *xl, char *key, char **value)
{
        GF_ASSERT (xl);
        return dict_get_str (xl->options, key, value);
}

static inline xlator_t *
first_of (volgen_graph_t *graph)
{
        return (xlator_t *)graph->graph.first;
}




/**************************
 *
 * Trie glue
 *
 *************************/


static int
volopt_selector (int lvl, char **patt, void *param,
                     int (*optcbk)(char *word, void *param))
{
        struct volopt_map_entry *vme = NULL;
        char *w = NULL;
        int i = 0;
        int len = 0;
        int ret = 0;
        char *dot = NULL;

        for (vme = glusterd_volopt_map; vme->key; vme++) {
                w = vme->key;

                for (i = 0; i < lvl; i++) {
                        if (patt[i]) {
                                w = strtail (w, patt[i]);
                                GF_ASSERT (!w || *w);
                                if (!w || *w != '.')
                                        goto next;
                        } else {
                                w = strchr (w, '.');
                                GF_ASSERT (w);
                        }
                        w++;
                }

                dot = strchr (w, '.');
                if (dot) {
                        len = dot - w;
                        w = gf_strdup (w);
                        if (!w)
                                return -1;
                        w[len] = '\0';
                }
                ret = optcbk (w, param);
                if (dot)
                        GF_FREE (w);
                if (ret)
                        return -1;
 next:
                continue;
        }

        return 0;
}

static int
volopt_trie_cbk (char *word, void *param)
{
        return trie_add ((trie_t *)param, word);
}

static int
process_nodevec (struct trienodevec *nodevec, char **hint)
{
        int ret = 0;
        char *hint1 = NULL;
        char *hint2 = NULL;
        char *hintinfx = "";
        trienode_t **nodes = nodevec->nodes;

        if (!nodes[0]) {
                *hint = NULL;
                return 0;
        }

#if 0
        /* Limit as in git */
        if (trienode_get_dist (nodes[0]) >= 6) {
                *hint = NULL;
                return 0;
        }
#endif

        if (trienode_get_word (nodes[0], &hint1))
                return -1;

        if (nodevec->cnt < 2 || !nodes[1]) {
                *hint = hint1;
                return 0;
        }

        if (trienode_get_word (nodes[1], &hint2))
                return -1;

        if (*hint)
                hintinfx = *hint;
        ret = gf_asprintf (hint, "%s or %s%s", hint1, hintinfx, hint2);
        if (ret > 0)
                ret = 0;
        return ret;
}

static int
volopt_trie_section (int lvl, char **patt, char *word, char **hint, int hints)
{
        trienode_t *nodes[] = { NULL, NULL };
        struct trienodevec nodevec = { nodes, 2};
        trie_t *trie = NULL;
        int ret = 0;

        trie = trie_new ();
        if (!trie)
                return -1;

        if (volopt_selector (lvl, patt, trie, &volopt_trie_cbk)) {
                trie_destroy (trie);

                return -1;
        }

        GF_ASSERT (hints <= 2);
        nodevec.cnt = hints;
        ret = trie_measure_vec (trie, word, &nodevec);
        if (ret || !nodevec.nodes[0])
                trie_destroy (trie);

        ret = process_nodevec (&nodevec, hint);
        trie_destroy (trie);

        return ret;
}

static int
volopt_trie (char *key, char **hint)
{
        char *patt[] = { NULL };
        char *fullhint = NULL;
        char *dot = NULL;
        char *dom = NULL;
        int   len = 0;
        int   ret = 0;

        *hint = NULL;

        dot = strchr (key, '.');
        if (!dot)
                return volopt_trie_section (1, patt, key, hint, 2);

        len = dot - key;
        dom = gf_strdup (key);
        if (!dom)
                return -1;
        dom[len] = '\0';

        ret = volopt_trie_section (0, NULL, dom, patt, 1);
        GF_FREE (dom);
        if (ret) {
                patt[0] = NULL;
                goto out;
        }
        if (!patt[0])
                goto out;

        *hint = "...";
        ret = volopt_trie_section (1, patt, dot + 1, hint, 2);
        if (ret)
                goto out;
        if (*hint) {
                ret = gf_asprintf (&fullhint, "%s.%s", patt[0], *hint);
                GF_FREE (*hint);
                if (ret >= 0) {
                        ret = 0;
                        *hint = fullhint;
                }
        }

 out:
        if (patt[0])
                GF_FREE (patt[0]);
        if (ret)
                *hint = NULL;

        return ret;
}




/**************************
 *
 * Volume generation engine
 *
 **************************/


typedef int (*volgen_opthandler_t) (volgen_graph_t *graph,
                                    struct volopt_map_entry *vme,
                                    void *param);

struct opthandler_data {
        volgen_graph_t *graph;
        volgen_opthandler_t handler;
        struct volopt_map_entry *vme;
        gf_boolean_t found;
        gf_boolean_t data_t_fake;
        int rv;
        char *volname;
        void *param;
};

#define pattern_match_options 0


static void
process_option (dict_t *dict, char *key, data_t *value, void *param)
{
        struct opthandler_data *odt = param;
        struct volopt_map_entry vme = {0,};

        if (odt->rv)
                return;
#if pattern_match_options
        if (fnmatch (odt->vme->key, key, 0) != 0)
                return;
#endif
        odt->found = _gf_true;

        vme.key = key;
        vme.voltype = odt->vme->voltype;
        vme.option = odt->vme->option;
        if (!vme.option) {
                vme.option = strrchr (key, '.');
                if (vme.option)
                        vme.option++;
                else
                        vme.option = key;
        }
        if (odt->data_t_fake)
                vme.value = (char *)value;
        else
                vme.value = value->data;

        odt->rv = odt->handler (odt->graph, &vme, odt->param);
}

static int
volgen_graph_set_options_generic (volgen_graph_t *graph, dict_t *dict,
                                  void *param, volgen_opthandler_t handler)
{
        struct volopt_map_entry *vme = NULL;
        struct opthandler_data odt = {0,};
        data_t *data = NULL;

        odt.graph = graph;
        odt.handler = handler;
        odt.param = param;
        (void)data;

        for (vme = glusterd_volopt_map; vme->key; vme++) {
                odt.vme = vme;
                odt.found = _gf_false;
                odt.data_t_fake = _gf_false;

#if pattern_match_options
                dict_foreach (dict, process_option, &odt);
#else
                data = dict_get (dict, vme->key);

                if (data)
                        process_option (dict, vme->key, data, &odt);
#endif
                if (odt.rv)
                        return odt.rv;

                if (odt.found)
                        continue;

                /* check for default value */

                if (vme->value) {
                        /* stupid hack to be able to reuse dict iterator
                         * in this context
                         */
                        odt.data_t_fake = _gf_true;
                        process_option (NULL, vme->key, (data_t *)vme->value,
                                        &odt);
                        if (odt.rv)
                                return odt.rv;
                }
        }

        return 0;
}

static int
no_filter_option_handler (volgen_graph_t *graph, struct volopt_map_entry *vme,
                          void *param)
{
        xlator_t *trav;
        int ret = 0;

        for (trav = first_of (graph); trav; trav = trav->next) {
                if (strcmp (trav->type, vme->voltype) != 0)
                        continue;

                ret = xlator_set_option (trav, vme->option, vme->value);
                if (ret)
                        break;
        }
        return ret;
}

static int
basic_option_handler (volgen_graph_t *graph, struct volopt_map_entry *vme,
                      void *param)
{
        int     ret = 0;

        if (vme->option[0] == '!')
                goto out;

        ret = no_filter_option_handler (graph, vme, param);
out:
        return ret;
}

static int
volgen_graph_set_options (volgen_graph_t *graph, dict_t *dict)
{
        return volgen_graph_set_options_generic (graph, dict, NULL,
                                                 &basic_option_handler);
}

static int
optget_option_handler (volgen_graph_t *graph, struct volopt_map_entry *vme,
                       void *param)
{
        struct volopt_map_entry *vme2 = param;

        if (strcmp (vme->key, vme2->key) == 0)
                vme2->value = vme->value;

        return 0;
}

/* This getter considers defaults also. */
static int
volgen_dict_get (dict_t *dict, char *key, char **value)
{
        struct volopt_map_entry vme = {0,};
        int ret = 0;

        vme.key = key;

        ret = volgen_graph_set_options_generic (NULL, dict, &vme,
                                                &optget_option_handler);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Out of memory");

                return -1;
        }

        *value = vme.value;

        return 0;
}

static int
option_complete (char *key, char **completion)
{
        struct volopt_map_entry *vme = NULL;

        *completion = NULL;
        for (vme = glusterd_volopt_map; vme->key; vme++) {
                if (strcmp (strchr (vme->key, '.') + 1, key) != 0)
                        continue;

                if (*completion && strcmp (*completion, vme->key) != 0) {
                        /* cancel on non-unique match */
                        *completion = NULL;

                        return 0;
                } else
                        *completion = vme->key;
        }

        if (*completion) {
                /* For sake of unified API we want
                 * have the completion to be a to-be-freed
                 * string.
                 */
                *completion = gf_strdup (*completion);
                return -!*completion;
        }

        return 0;
}

int
glusterd_volinfo_get (glusterd_volinfo_t *volinfo, char *key, char **value)
{
        return volgen_dict_get (volinfo->dict, key, value);
}

int
glusterd_volinfo_get_boolean (glusterd_volinfo_t *volinfo, char *key)
{
        char *val = NULL;
        gf_boolean_t  boo = _gf_false;
        int ret = 0;

        ret = glusterd_volinfo_get (volinfo, key, &val);
        if (ret)
                return -1;

        if (val)
                ret = gf_string2boolean (val, &boo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "value for %s option is not valid", key);

                return -1;
        }

        return boo;
}

gf_boolean_t
glusterd_check_voloption_flags (char *key, int32_t flags)
{
        char *completion = NULL;
        struct volopt_map_entry *vmep = NULL;
        int   ret = 0;

        COMPLETE_OPTION(key, completion, ret);
        for (vmep = glusterd_volopt_map; vmep->key; vmep++) {
                if (strcmp (vmep->key, key) == 0) {
                        if (vmep->flags & flags)
                                return _gf_true;
                        else
                                return _gf_false;
                }
        }

        return _gf_false;
}

gf_boolean_t
glusterd_check_globaloption (char *key)
{
        char *completion = NULL;
        struct volopt_map_entry *vmep = NULL;
        int   ret = 0;

        COMPLETE_OPTION(key, completion, ret);
        for (vmep = glusterd_volopt_map; vmep->key; vmep++) {
                if (strcmp (vmep->key, key) == 0) {
                        if ((vmep->type == GLOBAL_DOC) ||
                            (vmep->type == GLOBAL_NO_DOC))
                                return _gf_true;
                        else
                                return _gf_false;
                }
        }

        return _gf_false;
}

gf_boolean_t
glusterd_check_localoption (char *key)
{
        char *completion = NULL;
        struct volopt_map_entry *vmep = NULL;
        int   ret = 0;

        COMPLETE_OPTION(key, completion, ret);
        for (vmep = glusterd_volopt_map; vmep->key; vmep++) {
                if (strcmp (vmep->key, key) == 0) {
                        if ((vmep->type == DOC) ||
                            (vmep->type == NO_DOC))
                                return _gf_true;
                        else
                                return _gf_false;
                }
        }

        return _gf_false;
}

int
glusterd_check_voloption (char *key)
{
        char *completion = NULL;
        struct volopt_map_entry *vmep = NULL;
        int ret = 0;

        COMPLETE_OPTION(key, completion, ret);
        for (vmep = glusterd_volopt_map; vmep->key; vmep++) {
                if (strcmp (vmep->key, key) == 0) {
                        if ((vmep->type == DOC) ||
                            (vmep->type == DOC))
                                return _gf_true;
                        else
                                return _gf_false;
                }
        }

        return _gf_false;

}

int
glusterd_check_option_exists (char *key, char **completion)
{
        dict_t *dict = NULL;
        struct volopt_map_entry vme = {0,};
        struct volopt_map_entry *vmep = NULL;
        int ret = 0;

        (void)vme;
        (void)vmep;
        (void)dict;

        if (!strchr (key, '.')) {
                if (completion) {
                        ret = option_complete (key, completion);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, "Out of memory");
                                return -1;
                        }

                        ret = !!*completion;
                        if (ret)
                                return ret;
                        else
                                goto trie;
                } else
                        return 0;
        }

#if !pattern_match_options
        for (vmep = glusterd_volopt_map; vmep->key; vmep++) {
                if (strcmp (vmep->key, key) == 0) {
                        ret = 1;
                        break;
                }
        }
#else
        vme.key = key;

        /* We are getting a bit anal here to avoid typing
         * fnmatch one more time. Orthogonality foremost!
         * The internal logic of looking up in the volopt_map table
         * should be coded exactly once.
         *
         * [[Ha-ha-ha, so now if I ever change the internals then I'll
         * have to update the fnmatch in this comment also :P ]]
         */
        dict = get_new_dict ();
        if (!dict || dict_set_str (dict, key, "")) {
                gf_log ("", GF_LOG_ERROR, "Out of memory");

                return -1;
        }

        ret = volgen_graph_set_options_generic (NULL, dict, &vme,
                                                &optget_option_handler);
        dict_destroy (dict);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Out of memory");

                return -1;
        }

        ret = !!vme.value;
#endif

        if (ret || !completion)
                return ret;

 trie:
        ret = volopt_trie (key, completion);
        if (ret) {
                gf_log ("", GF_LOG_ERROR,
                        "Some error occurred during keyword hinting");
        }

        return ret;
}

char*
glusterd_get_trans_type_rb (gf_transport_type ttype)
{
        char *trans_type = NULL;

        switch (ttype) {
        case GF_TRANSPORT_RDMA:
                gf_asprintf (&trans_type, "rdma");
                break;
        case GF_TRANSPORT_TCP:
        case GF_TRANSPORT_BOTH_TCP_RDMA:
                gf_asprintf (&trans_type, "tcp");
                break;
        default:
                gf_log (THIS->name, GF_LOG_ERROR, "Unknown "
                        "transport type");
        }

        return trans_type;
}

static int
_xl_link_children (xlator_t *parent, xlator_t *children, size_t child_count)
{
        xlator_t        *trav = NULL;
        size_t          seek = 0;
        int             ret = -1;

        if (child_count == 0)
                goto out;
        seek = child_count;
        for (trav = children; --seek; trav = trav->next);
        for (; child_count--; trav = trav->prev) {
                ret = volgen_xlator_link (parent, trav);
                if (ret)
                        goto out;
        }
        ret = 0;
out:
        return ret;
}

static int
volgen_graph_merge_sub (volgen_graph_t *dgraph, volgen_graph_t *sgraph,
                        size_t child_count)
{
        xlator_t *trav = NULL;
        int      ret   = 0;

        GF_ASSERT (dgraph->graph.first);

        ret = _xl_link_children (first_of (dgraph), first_of (sgraph),
                                 child_count);
        if (ret)
                goto out;

        for (trav = first_of (dgraph); trav->next; trav = trav->next);

        trav->next = first_of (sgraph);
        trav->next->prev = trav;
        dgraph->graph.xl_count += sgraph->graph.xl_count;

out:
        return ret;
}

static void
volgen_apply_filters (char *orig_volfile)
{
	DIR           *filterdir = NULL;
	struct dirent  entry = {0,};
	struct dirent *next = NULL;
	char          *filterpath = NULL;
	struct stat    statbuf = {0,};

	filterdir = opendir(FILTERDIR);
	if (!filterdir) {
		return;
	}

	while ((readdir_r(filterdir,&entry,&next) == 0) && next) {
		if (!strncmp(entry.d_name,".",sizeof(entry.d_name))) {
			continue;
		}
		if (!strncmp(entry.d_name,"..",sizeof(entry.d_name))) {
			continue;
		}
		/*
		 * d_type isn't guaranteed to be present/valid on all systems,
		 * so do an explicit stat instead.
		 */
		if (gf_asprintf(&filterpath,"%s/%.*s",FILTERDIR,
				sizeof(entry.d_name), entry.d_name) == (-1)) {
			continue;
		}
		/* Deliberately use stat instead of lstat to allow symlinks. */
		if (stat(filterpath,&statbuf) == (-1)) {
			goto free_fp;
		}
		if (!S_ISREG(statbuf.st_mode)) {
			goto free_fp;
		}
		/*
		 * We could check the mode in statbuf directly, or just skip
		 * this entirely and check for EPERM after exec fails, but this
		 * is cleaner.
		 */
		if (access(filterpath,X_OK) != 0) {
			goto free_fp;
		}
		if (runcmd(filterpath,orig_volfile,NULL)) {
			gf_log("",GF_LOG_ERROR,"failed to run filter %.*s",
			       (int)sizeof(entry.d_name), entry.d_name);
		}
free_fp:
		GF_FREE(filterpath);
	}
}

static int
volgen_write_volfile (volgen_graph_t *graph, char *filename)
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

        if (glusterfs_graph_print_file (f, &graph->graph) == -1)
	goto error;

        if (fclose (f) == -1)
                goto error;
        f = NULL;

        if (rename (ftmp, filename) == -1)
                goto error;

        GF_FREE (ftmp);

	volgen_apply_filters(filename);

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
volgen_graph_free (volgen_graph_t *graph)
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
build_graph_generic (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                     dict_t *mod_dict, void *param,
                     int (*builder) (volgen_graph_t *graph,
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
        } else {
                set_dict = volinfo->dict;
        }

        ret = builder (graph, volinfo, set_dict, param);
        if (!ret)
                ret = volgen_graph_set_options (graph, set_dict);

        if (mod_dict)
                dict_destroy (set_dict);

        return ret;
}

static gf_transport_type
transport_str_to_type (char *tt)
{
        gf_transport_type type = GF_TRANSPORT_TCP;

        if (!strcmp ("tcp", tt))
                type = GF_TRANSPORT_TCP;
        else if (!strcmp ("rdma", tt))
                type = GF_TRANSPORT_RDMA;
        else if (!strcmp ("tcp,rdma", tt))
                type = GF_TRANSPORT_BOTH_TCP_RDMA;
        return type;
}

static void
transport_type_to_str (gf_transport_type type, char *tt)
{
        switch (type) {
        case GF_TRANSPORT_RDMA:
                strcpy (tt, "rdma");
                break;
        case GF_TRANSPORT_TCP:
                strcpy (tt, "tcp");
                break;
        case GF_TRANSPORT_BOTH_TCP_RDMA:
                strcpy (tt, "tcp,rdma");
                break;
        }
}

static void
get_vol_transport_type (glusterd_volinfo_t *volinfo, char *tt)
{
        transport_type_to_str (volinfo->transport_type, tt);
}

static void
get_vol_nfs_transport_type (glusterd_volinfo_t *volinfo, char *tt)
{
        if (volinfo->nfs_transport_type == GF_TRANSPORT_BOTH_TCP_RDMA) {
                gf_log ("", GF_LOG_ERROR, "%s:nfs transport cannot be both"
                        " tcp and rdma", volinfo->volname);
                GF_ASSERT (0);
        }
        transport_type_to_str (volinfo->nfs_transport_type, tt);
}

/*  gets the volinfo, dict, a character array for filling in
 *  the transport type and a boolean option which says whether
 *  the transport type is required for nfs or not. If its not
 *  for nfs, then it is considered as the client transport
 *  and client transport type is filled in the character array
 */
static void
get_transport_type (glusterd_volinfo_t *volinfo, dict_t *set_dict,
                    char *transt, gf_boolean_t is_nfs)
{
        int    ret   = -1;
        char  *tt    = NULL;
        char  *key   = NULL;
        typedef void (*transport_type) (glusterd_volinfo_t *volinfo, char *tt);
        transport_type get_transport;

        if (is_nfs == _gf_false) {
                key = "client-transport-type";
                get_transport = get_vol_transport_type;
        } else {
                key = "nfs.transport-type";
                get_transport = get_vol_nfs_transport_type;
        }

        ret = dict_get_str (set_dict, key, &tt);
        if (ret)
                get_transport (volinfo, transt);
        if (!ret)
                strcpy (transt, tt);
}

static int
server_auth_option_handler (volgen_graph_t *graph,
                            struct volopt_map_entry *vme, void *param)
{
        xlator_t *xl = NULL;
        xlator_list_t *trav = NULL;
        char *aa = NULL;
        int   ret   = 0;
        char *key = NULL;

        if (strcmp (vme->option, "!server-auth") != 0)
                return 0;

        xl = first_of (graph);

        /* from 'auth.allow' -> 'allow', and 'auth.reject' -> 'reject' */
        key = strchr (vme->key, '.') + 1;

        for (trav = xl->children; trav; trav = trav->next) {
                ret = gf_asprintf (&aa, "auth.addr.%s.%s", trav->xlator->name,
                                   key);
                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }
                if (ret)
                        return -1;
        }

        return 0;
}

static int
loglevel_option_handler (volgen_graph_t *graph,
                         struct volopt_map_entry *vme, void *param)
{
        char *role = param;
        struct volopt_map_entry vme2 = {0,};

        if ( (strcmp (vme->option, "!client-log-level") != 0 &&
               strcmp (vme->option, "!brick-log-level") != 0)
            || !strstr (vme->key, role))
                return 0;

        memcpy (&vme2, vme, sizeof (vme2));
        vme2.option = "log-level";

        return basic_option_handler (graph, &vme2, NULL);
}

static int
server_check_marker_off (volgen_graph_t *graph, struct volopt_map_entry *vme,
                         glusterd_volinfo_t *volinfo)
{
        gf_boolean_t           bool = _gf_false;
        int                    ret = 0;

        GF_ASSERT (volinfo);
        GF_ASSERT (vme);

        if (strcmp (vme->option, "!xtime") != 0)
                return 0;

        ret = gf_string2boolean (vme->value, &bool);
        if (ret || bool)
                goto out;

        ret = glusterd_volinfo_get_boolean (volinfo, VKEY_MARKER_XTIME);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "failed to get the marker status");
                ret = -1;
                goto out;
        }

        if (ret) {
                bool = _gf_false;
                ret = glusterd_check_gsync_running (volinfo, &bool);

                if (bool) {
                        gf_log ("", GF_LOG_WARNING, GEOREP" sessions active"
                                "for the volume %s, cannot disable marker "
                                ,volinfo->volname);
                        set_graph_errstr (graph,
                                          VKEY_MARKER_XTIME" cannot be disabled "
                                          "while "GEOREP" sessions exist");
                        ret = -1;
                        goto out;
                }

                if (ret) {
                        gf_log ("", GF_LOG_WARNING, "Unable to get the status"
                                 " of active gsync session");
                        goto out;
                }
        }

        ret = 0;
 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

static int
sys_loglevel_option_handler (volgen_graph_t *graph,
                             struct volopt_map_entry *vme,
                             void *param)
{
        char  *role = NULL;
        struct volopt_map_entry vme2 = {0,};

        role = (char *) param;

        if (strcmp (vme->option, "!sys-log-level") != 0 ||
            !strstr (vme->key, role))
                return 0;

        memcpy (&vme2, vme, sizeof (vme2));
        vme2.option = "sys-log-level";

        return basic_option_handler (graph, &vme2, NULL);
}

static int
volgen_graph_set_xl_options (volgen_graph_t *graph, dict_t *dict)
{
        int32_t   ret               = -1;
        char     *xlator            = NULL;
        char     xlator_match[1024] = {0,}; /* for posix* -> *posix* */
        char     *loglevel          = NULL;
        xlator_t *trav              = NULL;

        ret = dict_get_str (dict, "xlator", &xlator);
        if (ret)
                goto out;

        ret = dict_get_str (dict, "loglevel", &loglevel);
        if (ret)
                goto out;

        snprintf (xlator_match, 1024, "*%s", xlator);

        for (trav = first_of (graph); trav; trav = trav->next) {
                if (fnmatch(xlator_match, trav->type, FNM_NOESCAPE) == 0) {
                        gf_log ("glusterd", GF_LOG_DEBUG, "Setting log level for xlator: %s",
                                trav->type);
                        ret = xlator_set_option (trav, "log-level", loglevel);
                        if (ret)
                                break;
                }
        }

 out:
        return ret;
}

static int
server_spec_option_handler (volgen_graph_t *graph,
                            struct volopt_map_entry *vme, void *param)
{
        int                     ret = 0;
        glusterd_volinfo_t      *volinfo = NULL;

        volinfo = param;

        ret = server_auth_option_handler (graph, vme, NULL);
        if (!ret)
                ret = server_check_marker_off (graph, vme, volinfo);

        if (!ret)
                ret = loglevel_option_handler (graph, vme, "brick");

        if (!ret)
                ret = sys_loglevel_option_handler (graph, vme, "brick");

        return ret;
}

static int
server_spec_extended_option_handler (volgen_graph_t *graph,
                                     struct volopt_map_entry *vme, void *param)
{
        int     ret           = 0;
        dict_t *dict          = NULL;

        GF_ASSERT (param);
        dict = (dict_t *)param;

        ret = server_auth_option_handler (graph, vme, NULL);
        if (!ret)
                ret = volgen_graph_set_xl_options (graph, dict);

        return ret;
}

static void get_vol_tstamp_file (char *filename, glusterd_volinfo_t *volinfo);

static int
server_graph_builder (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, void *param)
{
        char     *volname               = NULL;
        char     *path                  = NULL;
        int       pump                  = 0;
        xlator_t *xl                    = NULL;
        xlator_t *txl                   = NULL;
        xlator_t *rbxl                  = NULL;
        char      transt[16]            = {0,};
        char     *ptranst               = NULL;
        char      volume_id[64]         = {0,};
        char      tstamp_file[PATH_MAX] = {0,};
        int       ret                   = 0;
        char     *xlator                = NULL;
        char     *loglevel              = NULL;

        path = param;
        volname = volinfo->volname;
        get_vol_transport_type (volinfo, transt);

        ret = dict_get_str (set_dict, "xlator", &xlator);

        /* got a cli log level request */
        if (!ret) {
                ret = dict_get_str (set_dict, "loglevel", &loglevel);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "could not get both"
                                " translator name and loglevel for log level request");
                        goto out;
                }
        }

        xl = volgen_graph_add (graph, "storage/posix", volname);
        if (!xl)
                return -1;

        ret = xlator_set_option (xl, "directory", path);
        if (ret)
                return -1;

        ret = xlator_set_option (xl, "volume-id",
                                 uuid_utoa (volinfo->volume_id));
        if (ret)
                return -1;

        xl = volgen_graph_add (graph, "features/access-control", volname);
        if (!xl)
                return -1;

        xl = volgen_graph_add (graph, "features/locks", volname);
        if (!xl)
                return -1;

        xl = volgen_graph_add (graph, "performance/io-threads", volname);
        if (!xl)
                return -1;

        ret = dict_get_int32 (volinfo->dict, "enable-pump", &pump);
        if (ret == -ENOENT)
                ret = pump = 0;
        if (ret)
                return -1;
        if (pump) {
                txl = first_of (graph);

                rbxl = volgen_graph_add_nolink (graph, "protocol/client",
                                                "%s-replace-brick", volname);
                if (!rbxl)
                        return -1;

		ptranst = glusterd_get_trans_type_rb (volinfo->transport_type);
		if (NULL == ptranst)
			return -1;

                ret = xlator_set_option (rbxl, "transport-type", ptranst);
                GF_FREE (ptranst);
                if (ret)
                        return -1;

                xl = volgen_graph_add_nolink (graph, "cluster/pump", "%s-pump",
                                              volname);
                if (!xl)
                        return -1;
                ret = volgen_xlator_link (xl, txl);
                if (ret)
                        return -1;
                ret = volgen_xlator_link (xl, rbxl);
                if (ret)
                        return -1;
        }

        xl = volgen_graph_add (graph, "features/marker", volname);
        if (!xl)
                return -1;
        uuid_unparse (volinfo->volume_id, volume_id);
        ret = xlator_set_option (xl, "volume-uuid", volume_id);
        if (ret)
                return -1;
        get_vol_tstamp_file (tstamp_file, volinfo);
        ret = xlator_set_option (xl, "timestamp-file", tstamp_file);
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

        ret = volgen_graph_set_options_generic (graph, set_dict,
                                                (xlator && loglevel) ? (void *)set_dict : volinfo,
                                                (xlator && loglevel) ?  &server_spec_extended_option_handler :
                                                 &server_spec_option_handler);

 out:
        return ret;
}


/* builds a graph for server role , with option overrides in mod_dict */
static int
build_server_graph (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                    dict_t *mod_dict, char *path)
{
        return build_graph_generic (graph, volinfo, mod_dict, path,
                                    &server_graph_builder);
}

static int
perfxl_option_handler (volgen_graph_t *graph, struct volopt_map_entry *vme,
                       void *param)
{
        char *volname = NULL;
        gf_boolean_t enabled = _gf_false;

        volname = param;

        if (strcmp (vme->option, "!perf") != 0)
                return 0;

        if (gf_string2boolean (vme->value, &enabled) == -1)
                return -1;
        if (!enabled)
                return 0;

        if (volgen_graph_add (graph, vme->voltype, volname))
                return 0;
        else
                return -1;
}

#if (HAVE_LIB_XML)
static int
end_sethelp_xml_doc (xmlTextWriterPtr writer)
{
        int             ret = -1;

        ret = xmlTextWriterEndElement(writer);
        if (ret < 0) {
                gf_log ("glusterd", GF_LOG_ERROR, "Could not end an "
                        "xmlElemetnt");
                ret = -1;
                goto out;
        }
        ret = xmlTextWriterEndDocument (writer);
        if (ret < 0) {
                gf_log ("glusterd", GF_LOG_ERROR, "Could not end an "
                        "xmlDocument");
                ret = -1;
                goto out;
        }
        ret = 0;
 out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

static int
init_sethelp_xml_doc (xmlTextWriterPtr *writer, xmlBufferPtr  *buf)
{
        int ret;

        *buf = xmlBufferCreateSize (8192);
        if (buf == NULL) {
                gf_log ("glusterd", GF_LOG_ERROR, "Error creating the xml "
                          "buffer");
                ret = -1;
                goto out;
        }

        xmlBufferSetAllocationScheme (*buf,XML_BUFFER_ALLOC_DOUBLEIT);

        *writer = xmlNewTextWriterMemory(*buf, 0);
        if (writer == NULL) {
                gf_log ("glusterd", GF_LOG_ERROR, " Error creating the xml "
                         "writer");
                ret = -1;
                goto out;
        }

        ret = xmlTextWriterStartDocument(*writer, "1.0", "UTF-8", "yes");
        if (ret < 0) {
                gf_log ("glusterd", GF_LOG_ERROR, "Error While starting the "
                         "xmlDoc");
                goto out;
        }

        ret = xmlTextWriterStartElement(*writer,
                                        (xmlChar *)"volumeOptionsDefaults");
        if (ret < 0) {
                gf_log ("glusterd", GF_LOG_ERROR, "Could not create an "
                        "xmlElemetnt");
                ret = -1;
                goto out;
        }


        ret = 0;

 out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

static int
xml_add_volset_element (xmlTextWriterPtr writer, const char *name,
                                 const char *def_val, const char *dscrpt)
{

        int                     ret = -1;

        GF_ASSERT (name);

        ret = xmlTextWriterStartElement(writer, (xmlChar *) "volumeOption");
        if (ret < 0) {
                gf_log ("glusterd", GF_LOG_ERROR, "Could not create an "
                        "xmlElemetnt");
                ret = -1;
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement(writer, (xmlChar*)"defaultValue",
                                              "%s", def_val);
        if (ret < 0) {
                gf_log ("glusterd", GF_LOG_ERROR, "Could not create an "
                        "xmlElemetnt");
                ret = -1;
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement(writer, (xmlChar *)"description",
                                              "%s",  dscrpt );
        if (ret < 0) {
                gf_log ("glusterd", GF_LOG_ERROR, "Could not create an "
                        "xmlElemetnt");
                ret = -1;
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement(writer, (xmlChar *) "name", "%s",
                                               name);
        if (ret < 0) {
                gf_log ("glusterd", GF_LOG_ERROR, "Could not create an "
                        "xmlElemetnt");
                ret = -1;
                goto out;
        }

        ret = xmlTextWriterEndElement(writer);
        if (ret < 0) {
                gf_log ("glusterd", GF_LOG_ERROR, "Could not end an "
                        "xmlElemetnt");
                ret = -1;
                goto out;
        }

        ret = 0;
 out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

#endif

static int
get_key_from_volopt ( struct volopt_map_entry *vme, char **key)
{
        int ret = 0;

        GF_ASSERT (vme);
        GF_ASSERT (key);


        if (vme->option) {
                if  (vme->option[0] == '!') {
                        *key = vme->option + 1;
                        if (!*key[0])
                                ret = -1;
                } else {
                        *key = vme->option;
                }
        } else  {
                *key = strchr (vme->key, '.');
                if (*key) {
                        (*key) ++;
                        if (!*key[0])
                                ret = -1;
                } else {
                        ret = -1;
                }
        }

        if (ret)
                gf_log ("glusterd", GF_LOG_ERROR, "Wrong entry found in  "
                        "glusterd_volopt_map entry %s", vme->key);
        else
                gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}


int
glusterd_get_volopt_content (gf_boolean_t xml_out)
{

        char                    *xlator_type = NULL;
        void                    *dl_handle = NULL;
        volume_opt_list_t          vol_opt_handle = {{0},};
        char                    *key = NULL;
        struct volopt_map_entry *vme = NULL;
        int                      ret = -1;
        char                    *def_val = NULL;
        char                    *descr = NULL;
        char                     output_string[8192] = {0, };
        char                    *output = NULL;
        char                     tmp_str[1024] = {0, };
        dict_t                  *ctx = NULL;
#if (HAVE_LIB_XML)
        xmlTextWriterPtr         writer = NULL;
        xmlBufferPtr             buf = NULL;

        if (xml_out) {
                ret = init_sethelp_xml_doc (&writer, &buf);
                if (ret) /*logging done in init_xml_lib*/
                        goto out;
        }
#endif

        ctx = glusterd_op_get_ctx ();

        if (!ctx) {
                /*extract the vol-set-help output only in host glusterd*/
                ret = 0;
                goto out;
        }

        INIT_LIST_HEAD (&vol_opt_handle.list);

        for (vme = &glusterd_volopt_map[0]; vme->key; vme++) {

                if ( ( vme->type == NO_DOC) || (vme->type == GLOBAL_NO_DOC) )
                        continue;

                if (get_key_from_volopt (vme, &key))
                                goto out; /*Some error while getin key*/

                if (!xlator_type || strcmp (vme->voltype, xlator_type)){
                        ret = xlator_volopt_dynload (vme->voltype,
                                                        &dl_handle,
                                                        &vol_opt_handle);
                        if (ret)
                                continue;
                }

                ret = xlator_option_info_list (&vol_opt_handle, key,
                                               &def_val, &descr);
                if (ret) /*Swallow Error i.e if option not found*/
                        continue;

                if (xml_out) {
#if (HAVE_LIB_XML)
                        if (xml_add_volset_element (writer,vme->key,
                                                        def_val, descr))
                                goto out;
#else
                        gf_log ("glusterd", GF_LOG_ERROR, "Libxml not present");
#endif
                } else {
                        snprintf (tmp_str, 1024, "Option: %s\nDefault "
                                        "Value: %s\nDescription: %s\n\n",
                                        vme->key, def_val, descr);
                        strcat (output_string, tmp_str);
                }
        }

#if (HAVE_LIB_XML)
        if ((xml_out) &&
            (ret = end_sethelp_xml_doc (writer)))
                goto out;
#else
        if (xml_out)
                gf_log ("glusterd", GF_LOG_ERROR, "Libxml not present");
#endif

        if (!xml_out)
                output = gf_strdup (output_string);
        else
#if (HAVE_LIB_XML)
                output = gf_strdup ((char *)buf->content);
#else
                gf_log ("glusterd", GF_LOG_ERROR, "Libxml not present");
#endif

        if (NULL == output) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (ctx, "help-str", output);
 out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

static int
volgen_graph_build_clients (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                            dict_t *set_dict, void *param)
{
        int                      i                  = 0;
        int                      ret                = -1;
        char                     transt[16]         = {0,};
        char                    *volname            = NULL;
        glusterd_brickinfo_t    *brick = NULL;
        xlator_t                *xl                = NULL;

        volname = volinfo->volname;

        if (volinfo->brick_count == 0) {
                gf_log ("", GF_LOG_ERROR,
                        "volume inconsistency: brick count is 0");
                goto out;
        }

        if ((volinfo->dist_leaf_count < volinfo->brick_count) &&
            ((volinfo->brick_count % volinfo->dist_leaf_count) != 0)) {
                gf_log ("", GF_LOG_ERROR,
                        "volume inconsistency: "
                        "total number of bricks (%d) is not divisible with "
                        "number of bricks per cluster (%d) in a multi-cluster "
                        "setup",
                        volinfo->brick_count, volinfo->dist_leaf_count);
                goto out;
        }

        get_transport_type (volinfo, set_dict, transt, _gf_false);

        if (!strcmp (transt, "tcp,rdma"))
                strcpy (transt, "tcp");

        i = 0;
        ret = -1;
        list_for_each_entry (brick, &volinfo->bricks, brick_list) {
                ret = -1;
                xl = volgen_graph_add_nolink (graph, "protocol/client",
                                              "%s-client-%d", volname, i);
                if (!xl)
                        goto out;
                ret = xlator_set_option (xl, "remote-host", brick->hostname);
                if (ret)
                        goto out;
                ret = xlator_set_option (xl, "remote-subvolume", brick->path);
                if (ret)
                        goto out;
                ret = xlator_set_option (xl, "transport-type", transt);
                if (ret)
                        goto out;
                i++;
        }
        if (i != volinfo->brick_count) {
                gf_log ("", GF_LOG_ERROR,
                        "volume inconsistency: actual number of bricks (%d) "
                        "differs from brick count (%d)", i,
                        volinfo->brick_count);

                ret = -1;
                goto out;
        }
        ret = 0;
out:
        return ret;
}

static int
volgen_graph_build_clusters (volgen_graph_t *graph,
                             glusterd_volinfo_t *volinfo, char *xl_type,
                             char *xl_namefmt, size_t child_count,
                             size_t sub_count)
{
        int             i = 0;
        int             j = 0;
        xlator_t        *txl = NULL;
        xlator_t        *xl  = NULL;
        xlator_t        *trav = NULL;
        char            *volname = NULL;
        int             ret     = -1;

        if (child_count == 0)
                goto out;
        volname = volinfo->volname;
        txl = first_of (graph);
        for (trav = txl; --child_count; trav = trav->next);
        for (;; trav = trav->prev) {
                if ((i % sub_count) == 0) {
                        xl = volgen_graph_add_nolink (graph, xl_type,
                                                      xl_namefmt, volname, j);
                        if (!xl) {
                                ret = -1;
                                goto out;
                        }
                        j++;
                }

                ret = volgen_xlator_link (xl, trav);
                if (ret)
                        goto out;

                if (trav == txl)
                        break;
                i++;
        }

        ret = j;
out:
        return ret;
}

gf_boolean_t
_xl_is_client_decommissioned (xlator_t *xl, glusterd_volinfo_t *volinfo)
{
        int             ret = 0;
        gf_boolean_t    decommissioned = _gf_false;
        char            *hostname = NULL;
        char            *path = NULL;

        GF_ASSERT (!strcmp (xl->type, "protocol/client"));
        ret = xlator_get_option (xl, "remote-host", &hostname);
        if (ret) {
                GF_ASSERT (0);
                gf_log ("glusterd", GF_LOG_ERROR, "Failed to get remote-host "
                        "from client %s", xl->name);
                goto out;
        }
        ret = xlator_get_option (xl, "remote-subvolume", &path);
        if (ret) {
                GF_ASSERT (0);
                gf_log ("glusterd", GF_LOG_ERROR, "Failed to get remote-host "
                        "from client %s", xl->name);
                goto out;
        }

        decommissioned = glusterd_is_brick_decommissioned (volinfo, hostname,
                                                           path);
out:
        return decommissioned;
}

gf_boolean_t
_xl_has_decommissioned_clients (xlator_t *xl, glusterd_volinfo_t *volinfo)
{
        xlator_list_t   *xl_child = NULL;
        gf_boolean_t    decommissioned = _gf_false;
        xlator_t        *cxl = NULL;

        if (!xl)
                goto out;

        if (!strcmp (xl->type, "protocol/client")) {
                decommissioned = _xl_is_client_decommissioned (xl, volinfo);
                goto out;
        }

        xl_child = xl->children;
        while (xl_child) {
                cxl = xl_child->xlator;
                /* this can go into 2 depths if the volume type
                   is stripe-replicate */
                decommissioned = _xl_has_decommissioned_clients (cxl, volinfo);
                if (decommissioned)
                        break;

                xl_child = xl_child->next;
        }
out:
        return decommissioned;
}

static int
_graph_get_decommissioned_children (xlator_t *dht, glusterd_volinfo_t *volinfo,
                                    char **children)
{
        int             ret = -1;
        xlator_list_t   *xl_child = NULL;
        xlator_t        *cxl = NULL;
        gf_boolean_t    comma = _gf_false;

        *children = NULL;
        xl_child = dht->children;
        while (xl_child) {
                cxl = xl_child->xlator;
                if (_xl_has_decommissioned_clients (cxl, volinfo)) {
                        if (!*children) {
                                *children = GF_CALLOC (16 * GF_UNIT_KB, 1,
                                                       gf_common_mt_char);
                                if (!*children)
                                        goto out;
                        }

                        if (comma)
                                strcat (*children, ",");
                        strcat (*children, cxl->name);
                        comma = _gf_true;
                }

                xl_child = xl_child->next;
        }
        ret = 0;
out:
        return ret;
}

static int
volgen_graph_build_dht_cluster (volgen_graph_t *graph,
                                glusterd_volinfo_t *volinfo, size_t child_count)
{
        int32_t                 clusters                 = 0;
        int                     ret                      = -1;
        char                    *decommissioned_children = NULL;
        xlator_t                *dht                     = NULL;

        GF_ASSERT (child_count > 1);
        clusters = volgen_graph_build_clusters (graph,  volinfo,
                                                "cluster/distribute", "%s-dht",
                                                child_count, child_count);
        if (clusters < 0)
                goto out;
        dht = first_of (graph);
        ret = _graph_get_decommissioned_children (dht, volinfo,
                                                  &decommissioned_children);
        if (ret)
                goto out;
        if (decommissioned_children) {
                ret = xlator_set_option (dht, "decommissioned-bricks",
                                         decommissioned_children);
                if (ret)
                        goto out;
        }
        ret = 0;
out:
        if (decommissioned_children)
                GF_FREE (decommissioned_children);
        return ret;
}

static int
volume_volgen_graph_build_clusters (volgen_graph_t *graph,
                                    glusterd_volinfo_t *volinfo)
{
        char                    *replicate_args[]   = {"cluster/replicate",
                                                       "%s-replicate-%d"};
        char                    *stripe_args[]      = {"cluster/stripe",
                                                       "%s-stripe-%d"};
        int                     rclusters           = 0;
        int                     clusters            = 0;
        int                     dist_count          = 0;
        int                     ret                 = -1;

        if (!volinfo->dist_leaf_count)
                goto out;

        if (volinfo->dist_leaf_count == 1)
                goto build_distribute;

        /* All other cases, it will have one or the other cluster type */
        switch (volinfo->type) {
        case GF_CLUSTER_TYPE_REPLICATE:
                clusters = volgen_graph_build_clusters (graph, volinfo,
                                                        replicate_args[0],
                                                        replicate_args[1],
                                                        volinfo->brick_count,
                                                        volinfo->replica_count);
                if (clusters < 0)
                        goto out;
                break;
        case GF_CLUSTER_TYPE_STRIPE:
                clusters = volgen_graph_build_clusters (graph, volinfo,
                                                        stripe_args[0],
                                                        stripe_args[1],
                                                        volinfo->brick_count,
                                                        volinfo->stripe_count);
                if (clusters < 0)
                        goto out;
                break;
        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                /* Replicate after the clients, then stripe */
                if (volinfo->replica_count == 0)
                        goto out;
                clusters = volgen_graph_build_clusters (graph, volinfo,
                                                        replicate_args[0],
                                                        replicate_args[1],
                                                        volinfo->brick_count,
                                                        volinfo->replica_count);
                if (clusters < 0)
                        goto out;

                rclusters = volinfo->brick_count / volinfo->replica_count;
                GF_ASSERT (rclusters == clusters);
                clusters = volgen_graph_build_clusters (graph, volinfo,
                                                        stripe_args[0],
                                                        stripe_args[1],
                                                        rclusters,
                                                        volinfo->stripe_count);
                if (clusters < 0)
                        goto out;
                break;
        default:
                gf_log ("", GF_LOG_ERROR, "volume inconsistency: "
                        "unrecognized clustering type");
                goto out;
        }

build_distribute:
        dist_count = volinfo->brick_count / volinfo->dist_leaf_count;
        if (dist_count > 1) {
                ret = volgen_graph_build_dht_cluster (graph, volinfo,
                                                      dist_count);
                if (ret)
                        goto out;
        }
        ret = 0;
out:
        return ret;
}

static int
client_graph_builder (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, void *param)
{
        int                      ret                = 0;
        xlator_t                *xl                 = NULL;
        char                    *volname            = NULL;

        volname = volinfo->volname;
        ret = volgen_graph_build_clients (graph, volinfo, set_dict, param);
        if (ret)
                goto out;

        ret = volume_volgen_graph_build_clusters (graph, volinfo);
        if (ret)
                goto out;

        ret = glusterd_volinfo_get_boolean (volinfo, VKEY_FEATURES_QUOTA);
        if (ret == -1)
                goto out;
        if (ret) {
                xl = volgen_graph_add (graph, "features/quota", volname);

                if (!xl) {
                        ret = -1;
                        goto out;
                }
        }

        ret = volgen_graph_set_options_generic (graph, set_dict, volname,
                                                &perfxl_option_handler);
        if (ret)
                goto out;

        ret = -1;
        xl = volgen_graph_add_as (graph, "debug/io-stats", volname);
        if (!xl)
                goto out;

        ret = volgen_graph_set_options_generic (graph, set_dict, "client",
                                                &loglevel_option_handler);

        if (!ret)
                ret = volgen_graph_set_options_generic (graph, set_dict, "client",
                                                        &sys_loglevel_option_handler);
out:
        return ret;
}


/* builds a graph for client role , with option overrides in mod_dict */
static int
build_client_graph (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                    dict_t *mod_dict)
{
        return build_graph_generic (graph, volinfo, mod_dict, NULL,
                                    &client_graph_builder);
}

static int
shd_option_handler (volgen_graph_t *graph, struct volopt_map_entry *vme,
                           void *param)
{
        int                     ret = 0;
        struct volopt_map_entry new_vme = {0};
        int                     shd = 0;

        shd = !strcmp (vme->option, "!self-heal-daemon");
        if ((vme->option[0] == '!') && !shd)
                goto out;
        new_vme = *vme;
        if (shd)
                new_vme.option = "self-heal-daemon";

        ret = no_filter_option_handler (graph, &new_vme, param);
out:
        return ret;
}

static int
nfs_option_handler (volgen_graph_t *graph,
                    struct volopt_map_entry *vme, void *param)
{
        xlator_t *xl = NULL;
        char *aa = NULL;
        int   ret   = 0;
        glusterd_volinfo_t *volinfo = NULL;

        volinfo = param;

        xl = first_of (graph);

/*        if (vme->type  == GLOBAL_DOC || vme->type == GLOBAL_NO_DOC) {

                ret = xlator_set_option (xl, vme->key, vme->value);
        }*/
        if ( !volinfo || !volinfo->volname)
                return 0;

        if (! strcmp (vme->option, "!rpc-auth.addr.*.allow")) {
                ret = gf_asprintf (&aa, "rpc-auth.addr.%s.allow",
                                        volinfo->volname);

                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }

                if (ret)
                        return -1;
        }

        if (! strcmp (vme->option, "!rpc-auth.addr.*.reject")) {
                ret = gf_asprintf (&aa, "rpc-auth.addr.%s.reject",
                                        volinfo->volname);

                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }

                if (ret)
                        return -1;
        }

        if (! strcmp (vme->option, "!rpc-auth.auth-unix.*")) {
                ret = gf_asprintf (&aa, "rpc-auth.auth.unix.%s",
                                        volinfo->volname);

                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }

                if (ret)
                        return -1;
        }
        if (! strcmp (vme->option, "!rpc-auth.auth.null.*")) {
                ret = gf_asprintf (&aa, "rpc-auth.auth.null.%s",
                                        volinfo->volname);

                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }

                if (ret)
                        return -1;
        }

        if (! strcmp (vme->option, "!nfs3.*.trusted-sync")) {
                ret = gf_asprintf (&aa, "nfs3.%s.trusted-sync",
                                        volinfo->volname);

                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }

                if (ret)
                        return -1;
        }

        if (! strcmp (vme->option, "!nfs3.*.trusted-write")) {
                ret = gf_asprintf (&aa, "nfs3.%s.trusted-write",
                                        volinfo->volname);

                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }

                if (ret)
                        return -1;
        }

        if (! strcmp (vme->option, "!nfs3.*.volume-access")) {
                ret = gf_asprintf (&aa, "nfs3.%s.volume-access",
                                        volinfo->volname);

                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }

                if (ret)
                        return -1;
        }

        if (! strcmp (vme->option, "!nfs3.*.export-dir")) {
                ret = gf_asprintf (&aa, "nfs3.%s.export-dir",
                                        volinfo->volname);

                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }

                if (ret)
                        return -1;
        }



        if (! strcmp (vme->option, "!rpc-auth.ports.*.insecure")) {
                ret = gf_asprintf (&aa, "rpc-auth.ports.%s.insecure",
                                        volinfo->volname);

                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }

                if (ret)
                        return -1;
        }


        if (! strcmp (vme->option, "!nfs-disable")) {
                ret = gf_asprintf (&aa, "nfs.%s.disable",
                                        volinfo->volname);

                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }

                if (ret)
                        return -1;
        }

        if ( (strcmp (vme->voltype, "nfs/server") == 0) &&
             (vme->option && vme->option[0]!='!') ) {
               ret = xlator_set_option (xl, vme->option, vme->value);
                if (ret)
                        return -1;
        }


        /*key = strchr (vme->key, '.') + 1;

        for (trav = xl->children; trav; trav = trav->next) {
                ret = gf_asprintf (&aa, "auth.addr.%s.%s", trav->xlator->name,
                                   key);
                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }
                if (ret)
                        return -1;
        }*/

        return 0;
}

static int
build_shd_graph (volgen_graph_t *graph, dict_t *mod_dict)
{
        volgen_graph_t     cgraph         = {0};
        glusterd_volinfo_t *voliter       = NULL;
        xlator_t           *this          = NULL;
        glusterd_conf_t    *priv          = NULL;
        dict_t             *set_dict      = NULL;
        int                ret            = 0;
        gf_boolean_t       valid_config   = _gf_false;
        xlator_t           *iostxl        = NULL;
        int                rclusters       = 0;
        int                replica_count  = 0;

        this = THIS;
        priv = this->private;

        set_dict = dict_new ();
        if (!set_dict) {
                ret = -ENOMEM;
                goto out;
        }

        iostxl = volgen_graph_add_as (graph, "debug/io-stats", "glustershd");
        if (!iostxl) {
                ret = -1;
                goto out;
        }

        list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (voliter->status != GLUSTERD_STATUS_STARTED)
                        continue;

                if (!glusterd_is_volume_replicate (voliter))
                        continue;

                replica_count = voliter->replica_count;

                valid_config = _gf_true;

                ret = dict_set_str (set_dict, "cluster.self-heal-daemon", "on");
                if (ret)
                        goto out;

                dict_copy (voliter->dict, set_dict);
                if (mod_dict)
                        dict_copy (mod_dict, set_dict);

                memset (&cgraph, 0, sizeof (cgraph));
                ret = volgen_graph_build_clients (&cgraph, voliter, set_dict,
                                                  NULL);
                if (ret)
                        goto out;

                rclusters = volgen_graph_build_clusters (&cgraph, voliter,
                                                        "cluster/replicate",
                                                        "%s-replicate-%d",
                                                        voliter->brick_count,
                                                        replica_count);
                if (rclusters < 0) {
                        ret = -1;
                        goto out;
                }

                ret = volgen_graph_set_options_generic (&cgraph, set_dict, voliter,
                                                        shd_option_handler);
                if (ret)
                        goto out;

                ret = volgen_graph_merge_sub (graph, &cgraph, rclusters);
                if (ret)
                        goto out;

                ret = dict_reset (set_dict);
                if (ret)
                        goto out;
        }
out:
        if (set_dict)
                dict_unref (set_dict);
        if (!valid_config)
                ret = -EINVAL;
        return ret;
}

/* builds a graph for nfs server role, with option overrides in mod_dict */
static int
build_nfs_graph (volgen_graph_t *graph, dict_t *mod_dict)
{
        volgen_graph_t      cgraph        = {0,};
        glusterd_volinfo_t *voliter       = NULL;
        xlator_t           *this          = NULL;
        glusterd_conf_t    *priv          = NULL;
        dict_t             *set_dict      = NULL;
        xlator_t           *nfsxl         = NULL;
        char               *skey          = NULL;
        int                 ret           = 0;
        char               nfs_xprt[16]   = {0,};

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        set_dict = dict_new ();
        if (!set_dict) {
                gf_log ("", GF_LOG_ERROR, "Out of memory");
                return -1;
        }

        nfsxl = volgen_graph_add_as (graph, "nfs/server", "nfs-server");
        if (!nfsxl) {
                ret = -1;
                goto out;
        }
        ret = xlator_set_option (nfsxl, "nfs.dynamic-volumes", "on");
        if (ret)
                goto out;

        list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (voliter->status != GLUSTERD_STATUS_STARTED)
                        continue;

                if (dict_get_str_boolean (voliter->dict, "nfs.disable", 0))
                        continue;

                ret = gf_asprintf (&skey, "rpc-auth.addr.%s.allow",
                                   voliter->volname);
                if (ret == -1) {
                        gf_log ("", GF_LOG_ERROR, "Out of memory");
                        goto out;
                }
                ret = xlator_set_option (nfsxl, skey, "*");
                GF_FREE (skey);
                if (ret)
                        goto out;

                ret = gf_asprintf (&skey, "nfs3.%s.volume-id",
                                   voliter->volname);
                if (ret == -1) {
                        gf_log ("", GF_LOG_ERROR, "Out of memory");
                        goto out;
                }
                ret = xlator_set_option (nfsxl, skey, uuid_utoa (voliter->volume_id));
                GF_FREE (skey);
                if (ret)
                        goto out;

                /* If both RDMA and TCP are the transport_type, use RDMA
                   for NFS client protocols */
                memset (&cgraph, 0, sizeof (cgraph));
                if (mod_dict)
                        get_transport_type (voliter, mod_dict, nfs_xprt, _gf_true);
                else
                        get_transport_type (voliter, voliter->dict, nfs_xprt, _gf_true);

                ret = dict_set_str (set_dict, VKEY_PERF_STAT_PREFETCH, "off");
                if (ret)
                        goto out;

                ret = dict_set_str (set_dict, "performance.client-io-threads", "off");
                if (ret)
                        goto out;

                ret = dict_set_str (set_dict, "client-transport-type",
                                    nfs_xprt);
                ret = build_client_graph (&cgraph, voliter, set_dict);
                if (ret)
                        goto out;

                if (mod_dict) {
                        dict_copy (mod_dict, set_dict);
                        ret = volgen_graph_set_options_generic (&cgraph, set_dict, voliter,
                                                                basic_option_handler);
                } else {
                        ret = volgen_graph_set_options_generic (&cgraph, voliter->dict, voliter,
                                                                basic_option_handler);
                }

                if (ret)
                        goto out;

                ret = volgen_graph_merge_sub (graph, &cgraph, 1);
                if (ret)
                        goto out;
                ret = dict_reset (set_dict);
                if (ret)
                        goto out;
        }

        list_for_each_entry (voliter, &priv->volumes, vol_list) {

                if (mod_dict) {
                        ret = volgen_graph_set_options_generic (graph, mod_dict, voliter,
                                                                nfs_option_handler);
                } else {
                        ret = volgen_graph_set_options_generic (graph, voliter->dict, voliter,
                                                                nfs_option_handler);
                }

                if (ret)
                        gf_log ("glusterd", GF_LOG_WARNING, "Could not set "
                                 "vol-options for the volume %s", voliter->volname);
        }

 out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        dict_destroy (set_dict);

        return ret;
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
        glusterd_conf_t *priv  = NULL;

        priv = THIS->private;

        GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, brick);
        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);

        snprintf (filename, PATH_MAX, "%s/%s.%s.%s.vol",
                  path, volinfo->volname,
                  brickinfo->hostname,
                  brick);
}

gf_boolean_t
glusterd_is_valid_volfpath (char *volname, char *brick)
{
        char                    volfpath[PATH_MAX] = {0,};
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        int32_t                 ret = 0;

        ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_WARNING, "brick path validation failed");
                ret = 0;
                goto out;
        }
        ret = glusterd_volinfo_new (&volinfo);
        if (ret) {
                gf_log ("", GF_LOG_WARNING, "brick path validation failed");
                ret = 0;
                goto out;
        }
        strncpy (volinfo->volname, volname, sizeof (volinfo->volname));
        get_brick_filepath (volfpath, volinfo, brickinfo);

        ret = (strlen (volfpath) < _POSIX_PATH_MAX);

out:
        if (brickinfo)
                glusterd_brickinfo_delete (brickinfo);
        if (volinfo)
                glusterd_volinfo_delete (volinfo);
        return ret;
}

static int
glusterd_generate_brick_volfile (glusterd_volinfo_t *volinfo,
                                 glusterd_brickinfo_t *brickinfo)
{
        volgen_graph_t graph = {0,};
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



static void
get_vol_tstamp_file (char *filename, glusterd_volinfo_t *volinfo)
{
        glusterd_conf_t *priv  = NULL;

        priv = THIS->private;

        GLUSTERD_GET_VOLUME_DIR (filename, volinfo, priv);
        strncat (filename, "/marker.tstamp",
                 PATH_MAX - strlen(filename) - 1);
}

int
generate_brick_volfiles (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t    *brickinfo = NULL;
        char                     tstamp_file[PATH_MAX] = {0,};
        int                      ret = -1;

        ret = glusterd_volinfo_get_boolean (volinfo, VKEY_MARKER_XTIME);
        if (ret == -1)
                return -1;

        get_vol_tstamp_file (tstamp_file, volinfo);

        if (ret) {
                ret = open (tstamp_file, O_WRONLY|O_CREAT|O_EXCL, 0644);
                if (ret == -1 && errno == EEXIST) {
                        gf_log ("", GF_LOG_DEBUG, "timestamp file exist");
                        ret = -2;
                }
                if (ret == -1) {
                        gf_log ("", GF_LOG_ERROR, "failed to create %s (%s)",
                                tstamp_file, strerror (errno));
                        return -1;
                }
                if (ret >= 0)
                        close (ret);
        } else {
                ret = unlink (tstamp_file);
                if (ret == -1 && errno == ENOENT)
                        ret = 0;
                if (ret == -1) {
                        gf_log ("", GF_LOG_ERROR, "failed to unlink %s (%s)",
                                tstamp_file, strerror (errno));
                        return -1;
                }
        }

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

static int
generate_single_transport_client_volfile (glusterd_volinfo_t *volinfo,
                                          char *filepath, dict_t *dict)
{
        volgen_graph_t graph = {0,};
        int     ret = -1;

        ret = build_client_graph (&graph, volinfo, dict);
        if (!ret)
                ret = volgen_write_volfile (&graph, filepath);

        volgen_graph_free (&graph);

        return ret;
}

void
get_client_filepath (char *filepath, glusterd_volinfo_t *volinfo, gf_transport_type type)
{
        char  path[PATH_MAX] = {0,};
        glusterd_conf_t *priv = NULL;

        priv = THIS->private;

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);

        if ((volinfo->transport_type == GF_TRANSPORT_BOTH_TCP_RDMA) &&
            (type == GF_TRANSPORT_RDMA))
                snprintf (filepath, PATH_MAX, "%s/%s.rdma-fuse.vol",
                          path, volinfo->volname);
        else
                snprintf (filepath, PATH_MAX, "%s/%s-fuse.vol",
                          path, volinfo->volname);
}

static void
enumerate_transport_reqs (gf_transport_type type, char **types)
{
        switch (type) {
        case GF_TRANSPORT_TCP:
                types[0] = "tcp";
                break;
        case GF_TRANSPORT_RDMA:
                types[0] = "rdma";
                break;
        case GF_TRANSPORT_BOTH_TCP_RDMA:
                types[0] = "tcp";
                types[1] = "rdma";
                break;
        }
}

static int
generate_client_volfiles (glusterd_volinfo_t *volinfo)
{
        char               filepath[PATH_MAX] = {0,};
        int                ret = -1;
        char               *types[] = {NULL, NULL, NULL};
        int                i = 0;
        dict_t             *dict = NULL;
        gf_transport_type  type = GF_TRANSPORT_TCP;

        enumerate_transport_reqs (volinfo->transport_type, types);
        dict = dict_new ();
        if (!dict)
                goto out;
        for (i = 0; types[i]; i++) {
                memset (filepath, 0, sizeof (filepath));
                ret = dict_set_str (dict, "client-transport-type", types[i]);
                if (ret)
                        goto out;
                type = transport_str_to_type (types[i]);
                get_client_filepath (filepath, volinfo, type);
                ret = generate_single_transport_client_volfile (volinfo,
                                                                filepath,
                                                                dict);
                if (ret)
                        goto out;
        }
out:
        if (dict)
                dict_unref (dict);
        return ret;
}

int
glusterd_create_rb_volfiles (glusterd_volinfo_t *volinfo,
                             glusterd_brickinfo_t *brickinfo)
{
        int ret = -1;

        ret = glusterd_generate_brick_volfile (volinfo, brickinfo);
        if (!ret)
                ret = generate_client_volfiles (volinfo);
        if (!ret)
                ret = glusterd_fetchspec_notify (THIS);

        return ret;
}

int
glusterd_create_volfiles_and_notify_services (glusterd_volinfo_t *volinfo)
{
        int ret = -1;

        ret = generate_brick_volfiles (volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR,
                        "Could not generate volfiles for bricks");
                goto out;
        }

        ret = generate_client_volfiles (volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR,
                        "Could not generate volfile for client");
                goto out;
        }

        ret = glusterd_fetchspec_notify (THIS);

out:
        return ret;
}

int
glusterd_create_global_volfile (int (*builder) (volgen_graph_t *graph,
                                                dict_t *set_dict),
                                char *filepath, dict_t  *mod_dict)
{
        volgen_graph_t graph = {0,};
        int     ret = -1;

        ret = builder (&graph, mod_dict);
        if (!ret)
                ret = volgen_write_volfile (&graph, filepath);

        volgen_graph_free (&graph);

        return ret;
}

int
glusterd_create_nfs_volfile ()
{
        char            filepath[PATH_MAX] = {0,};
        glusterd_conf_t *conf = THIS->private;

        glusterd_get_nodesvc_volfile ("nfs", conf->workdir,
                                            filepath, sizeof (filepath));
        return glusterd_create_global_volfile (build_nfs_graph,
                                               filepath, NULL);
}

int
glusterd_create_shd_volfile ()
{
        char            filepath[PATH_MAX] = {0,};
        int             ret = -1;
        glusterd_conf_t *conf = THIS->private;
        dict_t          *mod_dict = NULL;

        mod_dict = dict_new ();
        if (!mod_dict)
                goto out;

        ret = dict_set_uint32 (mod_dict, "cluster.background-self-heal-count", 0);
        if (ret)
                goto out;

        ret = dict_set_str (mod_dict, "cluster.data-self-heal", "on");
        if (ret)
                goto out;

        glusterd_get_nodesvc_volfile ("glustershd", conf->workdir,
                                      filepath, sizeof (filepath));
        ret = glusterd_create_global_volfile (build_shd_graph, filepath,
                                              mod_dict);
out:
        if (mod_dict)
                dict_unref (mod_dict);
        return ret;
}

int
glusterd_delete_volfile (glusterd_volinfo_t *volinfo,
                         glusterd_brickinfo_t *brickinfo)
{
        int  ret = 0;
        char filename[PATH_MAX] = {0,};

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        get_brick_filepath (filename, volinfo, brickinfo);
        ret = unlink (filename);
        if (ret)
                gf_log ("glusterd", GF_LOG_ERROR, "failed to delete file: %s, "
                        "reason: %s", filename, strerror (errno));
        return ret;
}

int
validate_nfsopts (glusterd_volinfo_t *volinfo,
                    dict_t *val_dict,
                    char **op_errstr)
{
        volgen_graph_t graph = {0,};
        int     ret = -1;
        char    transport_type[16] = {0,};
        char    *tt                = NULL;
        char    err_str[4096]      = {0,};

        graph.errstr = op_errstr;

        get_vol_transport_type (volinfo, transport_type);
        ret = dict_get_str (val_dict, "nfs.transport-type", &tt);
        if (!ret) {
                if (volinfo->transport_type != GF_TRANSPORT_BOTH_TCP_RDMA) {
                        snprintf (err_str, sizeof (err_str), "Changing nfs "
                                  "transport type is allowed only for volumes "
                                  "of transport type tcp,rdma");
                        gf_log ("", GF_LOG_ERROR, "%s", err_str);
                        *op_errstr = gf_strdup (err_str);
                        ret = -1;
                        goto out;
                }
                if (strcmp (tt,"tcp") && strcmp (tt,"rdma")) {
                        snprintf (err_str, sizeof (err_str), "wrong transport "
                                  "type %s", tt);
                        *op_errstr = gf_strdup (err_str);
                        ret = -1;
                        goto out;
                }
        }

        ret = build_nfs_graph (&graph, val_dict);
        if (!ret)
                ret = graph_reconf_validateopt (&graph.graph, op_errstr);

        volgen_graph_free (&graph);

out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
validate_clientopts (glusterd_volinfo_t *volinfo,
                    dict_t *val_dict,
                    char **op_errstr)
{
        volgen_graph_t graph = {0,};
        int     ret = -1;

        GF_ASSERT (volinfo);

        graph.errstr = op_errstr;

        ret = build_client_graph (&graph, volinfo, val_dict);
        if (!ret)
                ret = graph_reconf_validateopt (&graph.graph, op_errstr);

        volgen_graph_free (&graph);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
validate_brickopts (glusterd_volinfo_t *volinfo,
                    char *brickinfo_path,
                    dict_t *val_dict,
                    char **op_errstr)
{
        volgen_graph_t graph = {0,};
        int     ret = -1;

        GF_ASSERT (volinfo);

        graph.errstr = op_errstr;

        ret = build_server_graph (&graph, volinfo, val_dict, brickinfo_path);
        if (!ret)
                ret = graph_reconf_validateopt (&graph.graph, op_errstr);

        volgen_graph_free (&graph);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_validate_brickreconf (glusterd_volinfo_t *volinfo,
                               dict_t *val_dict,
                               char **op_errstr)
{
        glusterd_brickinfo_t *brickinfo = NULL;
        int                   ret = -1;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                gf_log ("", GF_LOG_DEBUG,
                        "Validating %s", brickinfo->hostname);

                ret = validate_brickopts (volinfo, brickinfo->path, val_dict,
                                          op_errstr);
                if (ret)
                        goto out;
        }

        ret = 0;
out:

        return ret;
}

static void
_check_globalopt (dict_t *this, char *key, data_t *value, void *ret_val)
{
        int *ret = NULL;

        ret = ret_val;
        if (*ret)
                return;
        if (!glusterd_check_globaloption (key))
                *ret = 1;
}

int
glusterd_validate_globalopts (glusterd_volinfo_t *volinfo,
                              dict_t *val_dict, char **op_errstr)
{
        int ret = 0;

        dict_foreach (val_dict, _check_globalopt, &ret);
        if (ret) {
                *op_errstr = gf_strdup ( "option specified is not a global option");
                return -1;
        }
        ret = glusterd_validate_brickreconf (volinfo, val_dict, op_errstr);

        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not Validate  bricks");
                goto out;
        }

        ret = validate_clientopts (volinfo, val_dict, op_errstr);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not Validate client");
                goto out;
        }

        ret = validate_nfsopts (volinfo, val_dict, op_errstr);


out:
                gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
                return ret;
}

static void
_check_localopt (dict_t *this, char *key, data_t *value, void *ret_val)
{
        int *ret = NULL;

        ret = ret_val;
        if (*ret)
                return;
        if (!glusterd_check_localoption (key))
                *ret = 1;
}

int
glusterd_validate_reconfopts (glusterd_volinfo_t *volinfo, dict_t *val_dict,
                              char **op_errstr)
{
        int ret = 0;

        dict_foreach (val_dict, _check_localopt, &ret);
        if (ret) {
                *op_errstr = gf_strdup ( "option specified is not a local option");
                return -1;
        }
        ret = glusterd_validate_brickreconf (volinfo, val_dict, op_errstr);

        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not Validate  bricks");
                goto out;
        }

        ret = validate_clientopts (volinfo, val_dict, op_errstr);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not Validate client");
                goto out;
        }

        ret = validate_nfsopts (volinfo, val_dict, op_errstr);


out:
                gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
                return ret;
}
