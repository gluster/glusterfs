/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include <fnmatch.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <utime.h>

#include "xlator.h"
#include "glusterd.h"
#include "defaults.h"
#include "logging.h"
#include "dict.h"
#include "graph-utils.h"
#include "glusterd-store.h"
#include "glusterd-hooks.h"
#include "trie.h"
#include "glusterd-mem-types.h"
#include "cli1-xdr.h"
#include "glusterd-volgen.h"
#include "glusterd-geo-rep.h"
#include "glusterd-utils.h"
#include "run.h"
#include "options.h"
#include "glusterd-snapshot-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-svc-helper.h"
#include "glusterd-snapd-svc-helper.h"

extern struct volopt_map_entry glusterd_volopt_map[];

/*********************************************
 *
 * xlator generation / graph manipulation API
 *
 *********************************************/

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
        CDS_INIT_LIST_HEAD (&xl->volume_options);

        xl->ctx = THIS->ctx;

        return xl;

 error:
        gf_log ("", GF_LOG_ERROR, "creating xlator of type %s failed",
                type);
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
        if (!ret && nodevec.nodes[0])
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

static int
process_option (char *key, data_t *value, void *param)
{
        struct opthandler_data *odt = param;
        struct volopt_map_entry vme = {0,};

        if (odt->rv)
                return 0;
        odt->found = _gf_true;

        vme.key = key;
        vme.voltype = odt->vme->voltype;
        vme.option = odt->vme->option;
        vme.op_version = odt->vme->op_version;

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
        return 0;
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

                data = dict_get (dict, vme->key);

                if (data)
                        process_option (vme->key, data, &odt);
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
                        process_option (vme->key, (data_t *)vme->value, &odt);
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
        gf_boolean_t  enabled = _gf_false;
        int ret = 0;

        ret = glusterd_volinfo_get (volinfo, key, &val);
        if (ret)
                return -1;

        if (val)
                ret = gf_string2boolean (val, &enabled);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "value for %s option is not valid", key);

                return -1;
        }

        return enabled;
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
        struct volopt_map_entry vme = {0,};
        struct volopt_map_entry *vmep = NULL;
        int ret = 0;
        xlator_t *this = THIS;

        (void)vme;
        (void)vmep;

        if (!strchr (key, '.')) {
                if (completion) {
                        ret = option_complete (key, completion);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Out of memory");
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

        for (vmep = glusterd_volopt_map; vmep->key; vmep++) {
                if (strcmp (vmep->key, key) == 0) {
                        ret = 1;
                        break;
                }
        }

        if (ret || !completion)
                return ret;

 trie:
        ret = volopt_trie (key, completion);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Some error occurred during keyword hinting");
        }

        return ret;
}

int
glusterd_volopt_validate (glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                          char *value, char **op_errstr)
{
        struct volopt_map_entry *vme     = NULL;
        char                    *volname = NULL;
        int                     ret = 0;
        xlator_t                *this = THIS;

        if (!dict || !key || !value) {
                gf_log_callingfn (this->name, GF_LOG_WARNING, "Invalid "
                                  "Arguments (dict=%p, key=%s, value=%s)", dict,
                                  key, value);
                return -1;
        }

        for (vme = &glusterd_volopt_map[0]; vme->key; vme++) {
                if ((vme->validate_fn) &&
                    ((!strcmp (key, vme->key)) ||
                     (!strcmp (key, strchr (vme->key, '.') + 1)))) {
                        ret = vme->validate_fn (volinfo, dict, key, value,
                                                op_errstr);
                        if (ret)
                                goto out;
                        break;
                }
        }
out:
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

        closedir (filterdir);
}

static int
volgen_write_volfile (volgen_graph_t *graph, char *filename)
{
        char        *ftmp = NULL;
        FILE        *f = NULL;
        int          fd   = 0;
        xlator_t    *this = NULL;

        this = THIS;

        if (gf_asprintf (&ftmp, "%s.tmp", filename) == -1) {
                ftmp = NULL;

                goto error;
        }

        fd = creat (ftmp, S_IRUSR | S_IWUSR);
        if (fd < 0) {
                gf_log (this->name, GF_LOG_ERROR, "%s",
                        strerror (errno));
                goto error;
        }

        close (fd);

        f = fopen (ftmp, "w");
        if (!f)
                goto error;

        if (glusterfs_graph_print_file (f, &graph->graph) == -1)
                goto error;

        if (fclose (f) != 0) {
                gf_log (THIS->name, GF_LOG_ERROR, "fclose on the file %s "
                        "failed (%s)", ftmp, strerror (errno));
                /*
                 * Even though fclose has failed here, we have to set f to NULL.
                 * Otherwise when the code path goes to error, there again we
                 * try to close it which might cause undefined behavior such as
                 * process crash.
                 */
                f = NULL;
                goto error;
        }

        f = NULL;

        if (rename (ftmp, filename) == -1)
                goto error;

        GF_FREE (ftmp);

        volgen_apply_filters(filename);

        return 0;

 error:

        GF_FREE (ftmp);
        if (f)
                fclose (f);

        gf_log (this->name, GF_LOG_ERROR,
                "failed to create volfile %s", filename);

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

/* If no value has specfied for tcp,rdma volume from cli
 * use tcp as default value.Otherwise, use transport type
 * mentioned in volinfo
 */
static void
get_vol_nfs_transport_type (glusterd_volinfo_t *volinfo, char *tt)
{
        if (volinfo->transport_type == GF_TRANSPORT_BOTH_TCP_RDMA) {
                strcpy (tt, "tcp");
                gf_log ("glusterd", GF_LOG_INFO,
                        "The default transport type for tcp,rdma volume "
                        "is tcp if option is not defined by the user ");
        } else
                transport_type_to_str (volinfo->transport_type, tt);
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
        gf_boolean_t enabled = _gf_false;
        int ret = 0;

        GF_ASSERT (volinfo);
        GF_ASSERT (vme);

        if (strcmp (vme->option, "!xtime") != 0)
                return 0;

        ret = gf_string2boolean (vme->value, &enabled);
        if (ret || enabled)
                goto out;

        ret = glusterd_volinfo_get_boolean (volinfo, VKEY_MARKER_XTIME);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "failed to get the marker status");
                ret = -1;
                goto out;
        }

        if (ret) {
                enabled = _gf_false;
                glusterd_check_geo_rep_configured (volinfo, &enabled);

                if (enabled) {
                        gf_log ("", GF_LOG_WARNING, GEOREP" sessions active"
                                "for the volume %s, cannot disable marker "
                                ,volinfo->volname);
                        set_graph_errstr (graph,
                                          VKEY_MARKER_XTIME" cannot be disabled "
                                          "while "GEOREP" sessions exist");
                        ret = -1;
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
logger_option_handler (volgen_graph_t *graph, struct volopt_map_entry *vme,
                       void *param)
{
        char  *role = NULL;
        struct volopt_map_entry vme2 = {0,};

        role = (char *) param;

        if (strcmp (vme->option, "!logger") != 0 ||
            !strstr (vme->key, role))
                return 0;

        memcpy (&vme2, vme, sizeof (vme2));
        vme2.option = "logger";

        return basic_option_handler (graph, &vme2, NULL);
}

static int
log_format_option_handler (volgen_graph_t *graph, struct volopt_map_entry *vme,
                           void *param)
{
        char  *role = NULL;
        struct volopt_map_entry vme2 = {0,};

        role = (char *) param;

        if (strcmp (vme->option, "!log-format") != 0 ||
            !strstr (vme->key, role))
                return 0;

        memcpy (&vme2, vme, sizeof (vme2));
        vme2.option = "log-format";

        return basic_option_handler (graph, &vme2, NULL);
}

static int
log_buf_size_option_handler (volgen_graph_t *graph,
                             struct volopt_map_entry *vme,
                             void *param)
{
        char  *role = NULL;
        struct volopt_map_entry vme2 = {0,};

        role = (char *) param;

        if (strcmp (vme->option, "!log-buf-size") != 0 ||
            !strstr (vme->key, role))
                return 0;

        memcpy (&vme2, vme, sizeof (vme2));
        vme2.option = "log-buf-size";

        return basic_option_handler (graph, &vme2, NULL);
}

static int
log_flush_timeout_option_handler (volgen_graph_t *graph,
                                  struct volopt_map_entry *vme,
                                  void *param)
{
        char  *role = NULL;
        struct volopt_map_entry vme2 = {0,};

        role = (char *) param;

        if (strcmp (vme->option, "!log-flush-timeout") != 0 ||
            !strstr (vme->key, role))
                return 0;

        memcpy (&vme2, vme, sizeof (vme2));
        vme2.option = "log-flush-timeout";

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

        if (!ret)
                ret = logger_option_handler (graph, vme, "brick");

        if (!ret)
                ret = log_format_option_handler (graph, vme, "brick");

        if (!ret)
                ret = log_buf_size_option_handler (graph, vme, "brick");

        if (!ret)
                ret = log_flush_timeout_option_handler (graph, vme, "brick");

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
brick_graph_add_posix (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                        dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
        int             ret = -1;
        gf_boolean_t    quota_enabled = _gf_true;
        gf_boolean_t    pgfid_feat    = _gf_false;
        char            *value = NULL;
        xlator_t        *xl = NULL;

        if (!graph || !volinfo || !set_dict || !brickinfo)
                goto out;

        ret = glusterd_volinfo_get (volinfo, VKEY_FEATURES_QUOTA, &value);
        if (value) {
                ret = gf_string2boolean (value, &quota_enabled);
                if (ret)
                        goto out;
        }

        ret = glusterd_volinfo_get (volinfo,
                                    "update-link-count-parent",
                                    &value);
        if (value) {
                ret = gf_string2boolean (value, &pgfid_feat);
                if (ret)
                        goto out;
        }

        ret = -1;

        xl = volgen_graph_add (graph, "storage/posix", volinfo->volname);
        if (!xl)
                goto out;

        ret = xlator_set_option (xl, "directory", brickinfo->path);
        if (ret)
                goto out;

        ret = xlator_set_option (xl, "volume-id",
                                 uuid_utoa (volinfo->volume_id));
        if (ret)
                goto out;

        if (quota_enabled || pgfid_feat)
                xlator_set_option (xl, "update-link-count-parent",
                                   "on");
out:
        return ret;
}

static int
brick_graph_add_bd (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                     dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
        int ret = -1;

        if (!graph || !volinfo || !set_dict || !brickinfo)
                goto out;

        ret = 0;

#ifdef HAVE_BD_XLATOR
        if (*brickinfo->vg != '\0') {
                xlator_t *xl = NULL;
                /* Now add BD v2 xlator if volume is BD type */
                xl = volgen_graph_add (graph, "storage/bd", volinfo->volname);
                if (!xl) {
                        ret = -1;
                        goto out;
                }

                ret = xlator_set_option (xl, "device", "vg");
                if (ret)
                        goto out;

                ret = xlator_set_option (xl, "export", brickinfo->vg);
                if (ret)
                        goto out;
        }
#endif

out:
        return ret;
}

static int
brick_graph_add_changelog (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                            dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
        xlator_t        *xl = NULL;
        char            changelog_basepath[PATH_MAX]    = {0,};
        int             ret = -1;

        if (!graph || !volinfo || !set_dict || !brickinfo)
                goto out;

        xl = volgen_graph_add (graph, "features/changelog", volinfo->volname);
        if (!xl)
                goto out;

        ret = xlator_set_option (xl, "changelog-brick", brickinfo->path);
        if (ret)
                goto out;

        snprintf (changelog_basepath, sizeof (changelog_basepath),
                  "%s/%s", brickinfo->path, ".glusterfs/changelogs");
        ret = xlator_set_option (xl, "changelog-dir", changelog_basepath);
        if (ret)
                goto out;
out:
        return ret;
}

static int
brick_graph_add_acl (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{

        xlator_t        *xl = NULL;
        int             ret = -1;

        if (!graph || !volinfo || !set_dict)
                goto out;

        xl = volgen_graph_add (graph, "features/access-control",
                                volinfo->volname);
        if (!xl)
                goto out;

        ret = 0;
out:
        return ret;
}

static int
brick_graph_add_locks (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                        dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{

        xlator_t        *xl = NULL;
        int             ret = -1;

        if (!graph || !volinfo || !set_dict)
                goto out;

        xl = volgen_graph_add (graph, "features/locks",
                                volinfo->volname);
        if (!xl)
                goto out;

        ret = 0;
out:
        return ret;
}

static int
brick_graph_add_iot (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{

        xlator_t        *xl = NULL;
        int             ret = -1;

        if (!graph || !volinfo || !set_dict)
                goto out;

        xl = volgen_graph_add (graph, "performance/io-threads",
                                volinfo->volname);
        if (!xl)
                goto out;
        ret = 0;
out:
        return ret;
}

static int
brick_graph_add_barrier (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                         dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{

        xlator_t        *xl = NULL;
        int             ret = -1;

        if (!graph || !volinfo)
                goto out;

        xl = volgen_graph_add (graph, "features/barrier", volinfo->volname);
        if (!xl)
                goto out;

        ret = 0;
out:
        return ret;
}

static int
brick_graph_add_index (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                        dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
        xlator_t        *xl = NULL;
        char            index_basepath[PATH_MAX]   = {0};
        int             ret = -1;

        if (!graph || !volinfo || !brickinfo || !set_dict)
                goto out;

        xl = volgen_graph_add (graph, "features/index", volinfo->volname);
        if (!xl)
                goto out;

        snprintf (index_basepath, sizeof (index_basepath), "%s/%s",
                  brickinfo->path, ".glusterfs/indices");
        ret = xlator_set_option (xl, "index-base", index_basepath);
        if (ret)
                goto out;
out:
        return ret;
}

static int
brick_graph_add_marker (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                         dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
        int             ret = -1;
        xlator_t        *xl = NULL;
        char            tstamp_file[PATH_MAX] = {0,};
        char            volume_id[64] = {0,};

        if (!graph || !volinfo || !set_dict)
                goto out;

        xl = volgen_graph_add (graph, "features/marker", volinfo->volname);
        if (!xl)
                goto out;

        uuid_unparse (volinfo->volume_id, volume_id);
        ret = xlator_set_option (xl, "volume-uuid", volume_id);
        if (ret)
                goto out;
        get_vol_tstamp_file (tstamp_file, volinfo);
        ret = xlator_set_option (xl, "timestamp-file", tstamp_file);
        if (ret)
                goto out;

out:
        return ret;
}

static int
brick_graph_add_quota (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                         dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
        int             ret = -1;
        xlator_t        *xl = NULL;
        char            *value = NULL;

        if (!graph || !volinfo || !set_dict)
                goto out;

        xl = volgen_graph_add (graph, "features/quota", volinfo->volname);
        if (!xl)
                goto out;

        ret = xlator_set_option (xl, "volume-uuid", volinfo->volname);
        if (ret)
                goto out;

        ret = glusterd_volinfo_get (volinfo, VKEY_FEATURES_QUOTA, &value);
        if (value) {
                ret = xlator_set_option (xl, "server-quota", value);
                if (ret)
                        goto out;
        }
out:
        return ret;
}

static int
brick_graph_add_ro (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                     dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
        int             ret = -1;
        xlator_t        *xl = NULL;

        if (!graph || !volinfo || !set_dict)
                goto out;

        if (dict_get_str_boolean (set_dict, "features.read-only", 0) &&
            dict_get_str_boolean (set_dict, "features.worm", 0)) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "read-only and worm cannot be set together");
                ret = -1;
                goto out;
        }

        /* Check for read-only volume option, and add it to the graph */
        if (dict_get_str_boolean (set_dict, "features.read-only", 0)){
                xl = volgen_graph_add (graph, "features/read-only",
                                       volinfo->volname);
                if (!xl) {
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;

out:
        return ret;
}

static int
brick_graph_add_worm (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                       dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
        int             ret = -1;
        xlator_t        *xl = NULL;

        if (!graph || !volinfo || !set_dict)
                goto out;

        if (dict_get_str_boolean (set_dict, "features.read-only", 0) &&
            dict_get_str_boolean (set_dict, "features.worm", 0)) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "read-only and worm cannot be set together");
                ret = -1;
                goto out;
        }

        /* Check for worm volume option, and add it to the graph */
        if (dict_get_str_boolean (set_dict, "features.worm", 0)) {
                xl = volgen_graph_add (graph, "features/worm",
                                       volinfo->volname);
                if (!xl) {
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;

out:
        return ret;
}

static int
brick_graph_add_cdc (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
        int             ret = -1;
        xlator_t        *xl = NULL;

        if (!graph || !volinfo || !set_dict)
                goto out;

        /* Check for compress volume option, and add it to the graph on
         * server side */
        ret = dict_get_str_boolean (set_dict, "network.compression", 0);
        if (ret == -1)
                goto out;
        if (ret) {
                xl = volgen_graph_add (graph, "features/cdc",
                                       volinfo->volname);
                if (!xl) {
                        ret = -1;
                        goto out;
                }
                ret = xlator_set_option (xl, "mode", "server");
                if (ret)
                        goto out;
        }
out:
        return ret;
}

static int
brick_graph_add_io_stats (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                           dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
        int             ret = -1;
        xlator_t        *xl = NULL;

        if (!graph || !volinfo || !set_dict || !brickinfo)
                goto out;

        xl = volgen_graph_add_as (graph, "debug/io-stats", brickinfo->path);
        if (!xl)
                goto out;

        ret = 0;
out:
        return ret;
}

static int
brick_graph_add_server (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                         dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
        int             ret = -1;
        xlator_t        *xl = NULL;
        char            transt[16] = {0,};
        char            *username = NULL;
        char            *password = NULL;
        char            key[1024] = {0};
        char            *ssl_user = NULL;
        char            *value = NULL;

        if (!graph || !volinfo || !set_dict || !brickinfo)
                goto out;

        get_vol_transport_type (volinfo, transt);

        username = glusterd_auth_get_username (volinfo);
        password = glusterd_auth_get_password (volinfo);

        xl = volgen_graph_add (graph, "protocol/server", volinfo->volname);
        if (!xl)
                goto out;

        ret = xlator_set_option (xl, "transport-type", transt);
        if (ret)
                goto out;

        /*In the case of running multiple glusterds on a single machine,
         * we should ensure that bricks don't listen on all IPs on that
         * machine and break the IP based separation being brought about.*/
        if (dict_get (THIS->options, "transport.socket.bind-address")) {
                ret = xlator_set_option (xl, "transport.socket.bind-address",
                                         brickinfo->hostname);
                if (ret)
                        return -1;
        }

        if (dict_get_str (set_dict, SSL_CERT_DEPTH_OPT, &value) == 0) {
                ret = xlator_set_option (xl, "ssl-cert-depth", value);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_WARNING,
                                "failed to set ssl-cert-depth");
                        return -1;
                }
        }

        if (dict_get_str (set_dict, SSL_CIPHER_LIST_OPT, &value) == 0) {
                ret = xlator_set_option (xl, "ssl-cipher-list", value);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_WARNING,
                                "failed to set ssl-cipher-list");
                        return -1;
                }
        }

        if (username) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "auth.login.%s.allow",
                                        brickinfo->path);

                ret = xlator_set_option (xl, key, username);
                if (ret)
                        return -1;
        }

        if (password) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "auth.login.%s.password",
                          username);

                ret = xlator_set_option (xl, key, password);
                if (ret)
                        return -1;
        }

        if (dict_get_str (volinfo->dict, "auth.ssl-allow", &ssl_user) == 0) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "auth.login.%s.ssl-allow",
                                       brickinfo->path);

                ret = xlator_set_option (xl, key, ssl_user);
                if (ret)
                        return -1;
        }

out:
        return ret;
}

static int
brick_graph_add_pump (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                       dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
        int             ret = -1;
        int             pump          = 0;
        xlator_t        *xl            = NULL;
        xlator_t        *txl           = NULL;
        xlator_t        *rbxl          = NULL;
        char            *username      = NULL;
        char            *password      = NULL;
        char            *ptranst       = NULL;
        char            *value         = NULL;


        if (!graph || !volinfo || !set_dict)
                goto out;

        ret = dict_get_int32 (volinfo->dict, "enable-pump", &pump);
        if (ret == -ENOENT)
                ret = pump = 0;
        if (ret)
                return -1;

        username = glusterd_auth_get_username (volinfo);
        password = glusterd_auth_get_password (volinfo);

        if (pump) {
                txl = first_of (graph);

                rbxl = volgen_graph_add_nolink (graph, "protocol/client",
                                                "%s-replace-brick",
                                                volinfo->volname);
                if (!rbxl)
                        return -1;

                ptranst = glusterd_get_trans_type_rb (volinfo->transport_type);
                if (NULL == ptranst)
                        return -1;

                if (dict_get_str (set_dict, SSL_CERT_DEPTH_OPT, &value) == 0) {
                        ret = xlator_set_option (rbxl, "ssl-cert-depth", value);
                        if (ret) {
                                gf_log ("glusterd", GF_LOG_WARNING,
                                        "failed to set ssl-cert-depth");
                                return -1;
                        }
                }

                if (dict_get_str (set_dict, SSL_CIPHER_LIST_OPT, &value) == 0) {
                        ret = xlator_set_option (rbxl, "ssl-cipher-list",
                                                 value);
                        if (ret) {
                                gf_log ("glusterd", GF_LOG_WARNING,
                                        "failed to set ssl-cipher-list");
                                return -1;
                        }
                }

                if (username) {
                        ret = xlator_set_option (rbxl, "username", username);
                        if (ret)
                                return -1;
                }

                if (password) {
                        ret = xlator_set_option (rbxl, "password", password);
                        if (ret)
                                return -1;
                }

                ret = xlator_set_option (rbxl, "transport-type", ptranst);
                GF_FREE (ptranst);
                if (ret)
                        return -1;

                xl = volgen_graph_add_nolink (graph, "cluster/pump", "%s-pump",
                                              volinfo->volname);
                if (!xl)
                        return -1;
                ret = volgen_xlator_link (xl, txl);
                if (ret)
                        return -1;
                ret = volgen_xlator_link (xl, rbxl);
                if (ret)
                        return -1;
        }

out:
        return ret;
}


/* The order of xlator definition here determines
 * the topology of the brick graph */
static volgen_brick_xlator_t server_graph_table[] = {
        {brick_graph_add_server, NULL},
        {brick_graph_add_io_stats, NULL},
        {brick_graph_add_cdc, NULL},
        {brick_graph_add_ro, NULL},
        {brick_graph_add_worm, NULL},
        {brick_graph_add_quota, "quota"},
        {brick_graph_add_marker, "marker"},
        {brick_graph_add_index, "index"},
        {brick_graph_add_barrier, NULL},
        {brick_graph_add_iot, "io-threads"},
        {brick_graph_add_pump, NULL},
        {brick_graph_add_locks, "locks"},
        {brick_graph_add_acl, "acl"},
        {brick_graph_add_changelog, "changelog"},
        {brick_graph_add_bd, "bd"},
        {brick_graph_add_posix, "posix"},
};

static glusterd_server_xlator_t
get_server_xlator (char *xlator)
{
        int i = 0;
        int size = sizeof (server_graph_table)/sizeof (server_graph_table[0]);

        for (i = 0; i < size; i++) {
                if (!server_graph_table[i].dbg_key)
                        continue;
                if (strcmp (xlator, server_graph_table[i].dbg_key))
                        return GF_XLATOR_SERVER;
        }

        return GF_XLATOR_NONE;
}

static glusterd_client_xlator_t
get_client_xlator (char *xlator)
{
        glusterd_client_xlator_t subvol = GF_CLNT_XLATOR_NONE;

        if (strcmp (xlator, "client") == 0)
                subvol = GF_CLNT_XLATOR_FUSE;

        return subvol;
}

static int
debugxl_option_handler (volgen_graph_t *graph, struct volopt_map_entry *vme,
                       void *param)
{
        char *volname = NULL;
        gf_boolean_t enabled = _gf_false;

        volname = param;

        if (strcmp (vme->option, "!debug") != 0)
                return 0;

        if (!strcmp (vme->key , "debug.trace") ||
            !strcmp (vme->key, "debug.error-gen")) {
                if (get_server_xlator (vme->value) == GF_XLATOR_NONE &&
                    get_client_xlator (vme->value) == GF_CLNT_XLATOR_NONE)
                        return 0;
                else
                        goto add_graph;
        }

        if (gf_string2boolean (vme->value, &enabled) == -1)
                return -1;
        if (!enabled)
                return 0;

add_graph:
        if (volgen_graph_add (graph, vme->voltype, volname))
                return 0;
        else
                return -1;
}

int
check_and_add_debug_xl (volgen_graph_t *graph, dict_t *set_dict, char *volname,
                        char *xlname)
{
        int       ret       = 0;
        char     *value_str = NULL;

        if (!xlname)
                goto out;

        ret = dict_get_str (set_dict, "debug.trace", &value_str);
        if (!ret) {
                if (strcmp (xlname, value_str) == 0) {
                        ret = volgen_graph_set_options_generic (graph,
                                                set_dict, volname,
                                                &debugxl_option_handler);
                        if (ret)
                                goto out;
                }
        }

        ret = dict_get_str (set_dict, "debug.error-gen", &value_str);
        if (!ret) {
                if (strcmp (xlname, value_str) == 0) {
                        ret = volgen_graph_set_options_generic (graph,
                                                set_dict, volname,
                                                &debugxl_option_handler);
                        if (ret)
                        goto out;
                }
        }

        ret = 0;

out:
        return ret;
}

static int
server_graph_builder (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, void *param)
{
        int                   ret           = 0;
        char                 *xlator        = NULL;
        char                 *loglevel      = NULL;
        int                   i             = 0;


        i = sizeof (server_graph_table)/sizeof (server_graph_table[0]) - 1;

        while (i >= 0) {
                ret = server_graph_table[i].builder (graph, volinfo, set_dict,
                                                     param);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "Builing graph "
                                "failed for server graph table entry: %d", i);
                        goto out;
                }

                ret = check_and_add_debug_xl (graph, set_dict,
                                              volinfo->volname,
                                              server_graph_table[i].dbg_key);
                if (ret)
                        goto out;

                i--;
        }


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
                    dict_t *mod_dict, glusterd_brickinfo_t *brickinfo)
{
        return build_graph_generic (graph, volinfo, mod_dict, brickinfo,
                                    &server_graph_builder);
}

static int
perfxl_option_handler (volgen_graph_t *graph, struct volopt_map_entry *vme,
                       void *param)
{
        gf_boolean_t enabled = _gf_false;
        glusterd_volinfo_t *volinfo = NULL;

        GF_ASSERT (param);
        volinfo = param;

        if (strcmp (vme->option, "!perf") != 0)
                return 0;

        if (gf_string2boolean (vme->value, &enabled) == -1)
                return -1;
        if (!enabled)
                return 0;

        /* Check op-version before adding the 'open-behind' xlator in the graph
         */
        if (!strcmp (vme->key, "performance.open-behind") &&
            (vme->op_version > volinfo->client_op_version))
                return 0;

        if (volgen_graph_add (graph, vme->voltype, volinfo->volname))
                return 0;
        else
                return -1;
}

static int
nfsperfxl_option_handler (volgen_graph_t *graph, struct volopt_map_entry *vme,
                       void *param)
{
        char *volname = NULL;
        gf_boolean_t enabled = _gf_false;

        volname = param;

        if (strcmp (vme->option, "!nfsperf") != 0)
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
int
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

int
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

        ret = xmlTextWriterStartElement(*writer, (xmlChar *)"options");
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

int
xml_add_volset_element (xmlTextWriterPtr writer, const char *name,
                                 const char *def_val, const char *dscrpt)
{

        int                     ret = -1;

        GF_ASSERT (name);

        ret = xmlTextWriterStartElement(writer, (xmlChar *) "option");
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

int
_get_xlator_opt_key_from_vme ( struct volopt_map_entry *vme, char **key)
{
        int ret = 0;

        GF_ASSERT (vme);
        GF_ASSERT (key);


        if (!strcmp (vme->key, AUTH_ALLOW_MAP_KEY))
                *key = gf_strdup (AUTH_ALLOW_OPT_KEY);
        else if (!strcmp (vme->key, AUTH_REJECT_MAP_KEY))
                *key = gf_strdup (AUTH_REJECT_OPT_KEY);
        else if (!strcmp (vme->key, NFS_DISABLE_MAP_KEY))
                *key = gf_strdup (NFS_DISABLE_OPT_KEY);
        else {
                if (vme->option) {
                        if  (vme->option[0] == '!') {
                                *key = vme->option + 1;
                                if (!*key[0])
                                        ret = -1;
                        } else {
                                *key = vme->option;
                        }
                } else {
                        *key = strchr (vme->key, '.');
                        if (*key) {
                                (*key) ++;
                                if (!*key[0])
                                        ret = -1;
                        } else {
                                ret = -1;
                        }
                }
        }
        if (ret)
                gf_log ("glusterd", GF_LOG_ERROR, "Wrong entry found in  "
                        "glusterd_volopt_map entry %s", vme->key);
        else
                gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

void
_free_xlator_opt_key (char *key)
{
        GF_ASSERT (key);

        if (!strcmp (key, AUTH_ALLOW_OPT_KEY) ||
            !strcmp (key, AUTH_REJECT_OPT_KEY) ||
            !strcmp (key, NFS_DISABLE_OPT_KEY))
                GF_FREE (key);

        return;
}

static xlator_t *
volgen_graph_build_client (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                           char *hostname, char *subvol, char *xl_id,
                           char *transt, dict_t *set_dict)
{
        xlator_t                *xl                 = NULL;
        int                      ret                = -2;
        uint32_t                 client_type        = GF_CLIENT_OTHER;
        char                    *str                = NULL;
        char                    *ssl_str            = NULL;
        gf_boolean_t             ssl_bool           = _gf_false;
        char                    *value              = NULL;

        GF_ASSERT (graph);
        GF_ASSERT (subvol);
        GF_ASSERT (xl_id);
        GF_ASSERT (transt);

        xl = volgen_graph_add_nolink (graph, "protocol/client",
                                      "%s", xl_id);
        if (!xl)
                goto err;

        ret = xlator_set_option (xl, "ping-timeout", "42");
        if (ret)
                goto err;

        if (hostname) {
                ret = xlator_set_option (xl, "remote-host", hostname);
                if (ret)
                        goto err;
        }

        ret = xlator_set_option (xl, "remote-subvolume", subvol);
        if (ret)
                goto err;

        ret = xlator_set_option (xl, "transport-type", transt);
        if (ret)
                goto err;

        ret = dict_get_uint32 (set_dict, "trusted-client",
                               &client_type);

        if (!ret && client_type == GF_CLIENT_TRUSTED) {
                str = NULL;
                str = glusterd_auth_get_username (volinfo);
                if (str) {
                        ret = xlator_set_option (xl, "username",
                                                 str);
                        if (ret)
                                goto err;
                }

                str = glusterd_auth_get_password (volinfo);
                if (str) {
                        ret = xlator_set_option (xl, "password",
                                                 str);
                        if (ret)
                                goto err;
                }
        }

        if (dict_get_str(set_dict,"client.ssl",&ssl_str) == 0) {
                if (gf_string2boolean(ssl_str,&ssl_bool) == 0) {
                        if (ssl_bool) {
                                ret = xlator_set_option(xl,
                                        "transport.socket.ssl-enabled",
                                        "true");
                                if (ret) {
                                        goto err;
                                }
                        }
                }
        }

        if (dict_get_str (set_dict, SSL_CERT_DEPTH_OPT, &value) == 0) {
                ret = xlator_set_option (xl, "ssl-cert-depth", value);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_WARNING,
                                "failed to set ssl-cert-depth");
                        goto err;
                }
        }

        if (dict_get_str (set_dict, SSL_CIPHER_LIST_OPT, &value) == 0) {
                ret = xlator_set_option (xl, "ssl-cipher-list", value);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_WARNING,
                                "failed to set ssl-cipher-list");
                        goto err;
                }
        }

        return xl;
err:
        return NULL;
}

static int
volgen_graph_build_clients (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                            dict_t *set_dict, void *param)
{
        int                      i                  = 0;
        int                      ret                = -1;
        char                     transt[16]         = {0,};
        char                    *volname            = NULL;
        glusterd_brickinfo_t    *brick              = NULL;
        xlator_t                *xl                 = NULL;

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
        cds_list_for_each_entry (brick, &volinfo->bricks, brick_list) {
                xl = volgen_graph_build_client (graph, volinfo,
                                                brick->hostname, brick->path,
                                                brick->brick_id,
                                                transt, set_dict);
                if (!xl) {
                        ret = -1;
                        goto out;
                }

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

/**
 * This is the build graph function for user-serviceable snapshots.
 * Generates  snapview-client
 */
static int
volgen_graph_build_snapview_client (volgen_graph_t *graph,
                                    glusterd_volinfo_t *volinfo,
                                    char *volname, dict_t *set_dict)
{
        int                ret                  = 0;
        xlator_t          *prev_top             = NULL;
        xlator_t          *prot_clnt            = NULL;
        xlator_t          *svc                  = NULL;
        char               transt [16]          = {0,};
        char              *svc_args[]           = {"features/snapview-client",
                                                   "%s-snapview-client"};
        char               subvol [1024]        = {0,};
        char               xl_id [1024]         = {0,};

        prev_top = (xlator_t *)(graph->graph.first);

        snprintf (subvol, sizeof (subvol), "snapd-%s", volinfo->volname);
        snprintf (xl_id, sizeof (xl_id), "%s-snapd-client", volinfo->volname);

        get_transport_type (volinfo, set_dict, transt, _gf_false);

        prot_clnt = volgen_graph_build_client (graph, volinfo, NULL, subvol,
                                               xl_id, transt, set_dict);
        if (!prot_clnt) {
                ret = -1;
                goto out;
        }

        svc = volgen_graph_add_nolink (graph, svc_args[0], svc_args[1],
                                       volname);
        if (!svc) {
                ret = -1;
                goto out;
        }

        /**
         * Ordering the below two traslators (cur_top & prot_clnt) is important
         * as snapview client implementation is built on the policy that
         * normal volume path goes to FIRST_CHILD and snap world operations
         * goes to SECOND_CHILD
         **/
        ret = volgen_xlator_link (graph->graph.first, prev_top);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to link the "
                        "snapview-client to distribute");
                goto out;
        }

        ret = volgen_xlator_link (graph->graph.first, prot_clnt);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to link the "
                        "snapview-client to snapview-server");
                goto out;
        }

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
                                glusterd_volinfo_t *volinfo, size_t child_count,
                                gf_boolean_t is_quotad)
{
        int32_t                 clusters                 = 0;
        int                     ret                      = -1;
        char                    *decommissioned_children = NULL;
        xlator_t                *dht                     = NULL;
        char                    *voltype                 = "cluster/distribute";
        char                    *name_fmt                = NULL;

        /* NUFA and Switch section */
        if (dict_get_str_boolean (volinfo->dict, "cluster.nufa", 0) &&
            dict_get_str_boolean (volinfo->dict, "cluster.switch", 0)) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "nufa and switch cannot be set together");
                ret = -1;
                goto out;
        }

        /* Check for NUFA volume option, and change the voltype */
        if (dict_get_str_boolean (volinfo->dict, "cluster.nufa", 0))
                voltype = "cluster/nufa";

        /* Check for switch volume option, and change the voltype */
        if (dict_get_str_boolean (volinfo->dict, "cluster.switch", 0))
                voltype = "cluster/switch";

        if (is_quotad)
                name_fmt = "%s";
        else
                name_fmt = "%s-dht";

        clusters = volgen_graph_build_clusters (graph,  volinfo,
                                                voltype,
                                                name_fmt,
                                                child_count,
                                                child_count);
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
        GF_FREE (decommissioned_children);
        return ret;
}

static int
volgen_graph_build_ec_clusters (volgen_graph_t *graph,
                                glusterd_volinfo_t *volinfo)
{
        int                     i = 0;
        int                     ret = 0;
        int                     clusters            = 0;
        char                    *disperse_args[]    = {"cluster/disperse",
                                                       "%s-disperse-%d"};
        xlator_t                *ec                 = NULL;
        char                    option[32]          = {0};

        clusters = volgen_graph_build_clusters (graph, volinfo,
                                                disperse_args[0],
                                                disperse_args[1],
                                                volinfo->brick_count,
                                                volinfo->disperse_count);
        if (clusters < 0)
                goto out;

        sprintf(option, "%d", volinfo->redundancy_count);
        ec = first_of (graph);
        for (i = 0; i < clusters; i++) {
                ret = xlator_set_option (ec, "redundancy", option);
                if (ret) {
                        clusters = -1;
                        goto out;
                }

                ec = ec->next;
        }
out:
        return clusters;
}

static int
volume_volgen_graph_build_clusters (volgen_graph_t *graph,
                                    glusterd_volinfo_t *volinfo,
                                    gf_boolean_t is_quotad)
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

        case GF_CLUSTER_TYPE_DISPERSE:
                clusters = volgen_graph_build_ec_clusters (graph, volinfo);
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
        if (!dist_count) {
                ret = -1;
                goto out;
        }

        ret = volgen_graph_build_dht_cluster (graph, volinfo,
                                              dist_count, is_quotad);
        if (ret)
                goto out;

        ret = 0;
out:
        return ret;
}

static int client_graph_set_perf_options(volgen_graph_t *graph,
                                         glusterd_volinfo_t *volinfo,
                                         dict_t *set_dict)
{
        data_t *tmp_data = NULL;
        char *volname = NULL;

        /*
         * Logic to make sure NFS doesn't have performance translators by
         * default for a volume
         */
        volname = volinfo->volname;
        tmp_data = dict_get (set_dict, "nfs-volume-file");
        if (!tmp_data)
                return volgen_graph_set_options_generic(graph, set_dict,
                                                        volinfo,
                                                        &perfxl_option_handler);
        else
                return volgen_graph_set_options_generic(graph, set_dict,
                                                        volname,
                                                        &nfsperfxl_option_handler);
}

static int
graph_set_generic_options (xlator_t *this, volgen_graph_t *graph,
                           dict_t *set_dict, char *identifier)
{
        int     ret = 0;

        ret = volgen_graph_set_options_generic (graph, set_dict, "client",
                                                &loglevel_option_handler);

        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "changing %s log level"
                        " failed", identifier);

        ret = volgen_graph_set_options_generic (graph, set_dict, "client",
                                                &sys_loglevel_option_handler);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "changing %s syslog "
                        "level failed", identifier);

        ret = volgen_graph_set_options_generic (graph, set_dict, "client",
                                                &logger_option_handler);

        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "changing %s logger"
                        " failed", identifier);

        ret = volgen_graph_set_options_generic (graph, set_dict, "client",
                                                &log_format_option_handler);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "changing %s log format"
                        " failed", identifier);

        ret = volgen_graph_set_options_generic (graph, set_dict, "client",
                                                &log_buf_size_option_handler);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "Failed to change "
                        "log-buf-size option");

        ret = volgen_graph_set_options_generic (graph, set_dict, "client",
                                            &log_flush_timeout_option_handler);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "Failed to change "
                        "log-flush-timeout option");
        return 0;
}

static int
client_graph_builder (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, void *param)
{
        int              ret           = 0;
        xlator_t        *xl            = NULL;
        char            *volname       = NULL;
        glusterd_conf_t *conf          = THIS->private;
        char            *tmp           = NULL;
        gf_boolean_t     var           = _gf_false;
        gf_boolean_t     ob            = _gf_false;
        gf_boolean_t     uss_enabled   = _gf_false;
        gf_boolean_t     rebal_volfile = _gf_false;
        xlator_t        *this          = THIS;

        GF_ASSERT (this);
        GF_ASSERT (conf);

        volname = volinfo->volname;
        ret = volgen_graph_build_clients (graph, volinfo, set_dict, param);
        if (ret)
                goto out;

        ret = volume_volgen_graph_build_clusters (graph, volinfo, _gf_false);
        if (ret == -1)
                goto out;

        /* As of now snapshot volume is read-only. Read-only xlator is loaded
         * in client graph so that AFR & DHT healing can be done in server.
         */
        if (volinfo->is_snap_volume) {
                xl = volgen_graph_add (graph, "features/read-only", volname);
                if (!xl) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to add "
                                "read-only feature to the graph of %s "
                                "snapshot with %s origin volume",
                                volname, volinfo->parent_volname);
                        ret = -1;
                        goto out;
                }
        }

        /* Check for compress volume option, and add it to the graph on client side */
        ret = dict_get_str_boolean (set_dict, "network.compression", 0);
        if (ret == -1)
                goto out;
        if (ret) {
                xl = volgen_graph_add (graph, "features/cdc", volname);
                if (!xl) {
                        ret = -1;
                        goto out;
                }
                ret = xlator_set_option (xl, "mode", "client");
                if (ret)
                        goto out;
        }

        ret = dict_get_str_boolean (set_dict, "features.encryption", _gf_false);
        if (ret == -1)
                goto out;
        if (ret) {
                xl = volgen_graph_add (graph, "encryption/crypt", volname);

                if (!xl) {
                        ret = -1;
                        goto out;
                }
        }

        if (conf->op_version == GD_OP_VERSION_MIN) {
                ret = glusterd_volinfo_get_boolean (volinfo,
                                                    VKEY_FEATURES_QUOTA);
                if (ret == -1)
                        goto out;
                if (ret) {
                        xl = volgen_graph_add (graph, "features/quota",
                                              volname);
                        if (!xl) {
                                ret = -1;
                                goto out;
                        }
                }
        }


        ret = dict_get_str_boolean (set_dict, "features.file-snapshot",
                                    _gf_false);
        if (ret == -1)
                goto out;
        if (ret) {
                xl = volgen_graph_add (graph, "features/qemu-block", volname);

                if (!xl) {
                        ret = -1;
                        goto out;
                }
        }

        /* Do not allow changing read-after-open option if root-squash is
           enabled.
        */
        ret = dict_get_str (set_dict, "performance.read-after-open", &tmp);
        if (!ret) {
                ret = dict_get_str (volinfo->dict, "server.root-squash", &tmp);
                if (!ret) {
                        ob = _gf_false;
                        ret = gf_string2boolean (tmp, &ob);
                        if (!ret && ob) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "root-squash is enabled. Please turn it"
                                        " off to change read-after-open "
                                        "option");
                                ret = -1;
                                goto out;
                        }
                }
        }

        /* open behind causes problems when root-squash is enabled
           (by allowing reads to happen even though the squashed user
           does not have permissions to do so) as it fakes open to be
           successful and later sends reads on anonymous fds. So when
           root-squash is enabled, open-behind's option to read after
           open is done is also enabled.
        */
        ret = dict_get_str (set_dict, "server.root-squash", &tmp);
        if (!ret) {
                ret = gf_string2boolean (tmp, &var);
                if (ret)
                        goto out;

                if (var) {
                        ret = dict_get_str (volinfo->dict,
                                            "performance.read-after-open",
                                            &tmp);
                        if (!ret) {
                                ret = gf_string2boolean (tmp, &ob);
                                /* go ahead with turning read-after-open on
                                   even if string2boolean conversion fails,
                                   OR if read-after-open option is turned off
                                */
                                if (ret || !ob)
                                        ret = dict_set_str (set_dict,
                                                 "performance.read-after-open",
                                                            "yes");
                        } else {
                                ret = dict_set_str (set_dict,
                                                    "performance.read-after-open",
                                                    "yes");
                        }
                } else {
                        /* When root-squash has to be turned off, open-behind's
                           read-after-open option should be reset to what was
                           there before root-squash was turned on. If the option
                           cannot be found in volinfo's dict, it means that
                           option was not set before turning on root-squash.
                        */
                        ob = _gf_false;
                        ret = dict_get_str (volinfo->dict,
                                            "performance.read-after-open",
                                            &tmp);
                        if (!ret) {
                                ret = gf_string2boolean (tmp, &ob);

                                if (!ret && ob) {
                                        ret = dict_set_str (set_dict,
                                                 "performance.read-after-open",
                                                            "yes");
                                }
                        }
                        /* consider operation is failure only if read-after-open
                           option is enabled and could not set into set_dict
                        */
                        if (!ob)
                                ret = 0;
                }
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "setting "
                                "open behind option as part of root "
                                "squash failed");
                        goto out;
                }
        }

        ret = dict_get_str_boolean (set_dict, "server.manage-gids", _gf_false);
        if (ret != -1) {
               ret = dict_set_str (set_dict, "client.send-gids",
                                   ret ? "false" : "true");
               if (ret)
                       gf_log (THIS->name, GF_LOG_WARNING, "changing client"
                               " protocol option failed");
        }

        ret = client_graph_set_perf_options(graph, volinfo, set_dict);
        if (ret)
                goto out;

        uss_enabled = dict_get_str_boolean (set_dict, "features.uss",
                                            _gf_false);
        if (uss_enabled == -1)
                goto out;
        if (uss_enabled && !volinfo->is_snap_volume) {
                rebal_volfile = dict_get_str_boolean (set_dict,
                                                   "rebalance-volfile-creation",
                                                   _gf_false);
                if (rebal_volfile == -1)
                        goto out;

                if (!rebal_volfile) {
                        ret = volgen_graph_build_snapview_client
                                                   (graph, volinfo,
                                                    volname, set_dict);
                        if (ret == -1)
                                goto out;
                }
        }

        /* add debug translators depending on the options */
        ret = check_and_add_debug_xl (graph, set_dict, volname,
                                      "client");
        if (ret)
                return -1;

        ret = -1;
        xl = volgen_graph_add_as (graph, "debug/io-stats", volname);
        if (!xl)
                goto out;

        ret = graph_set_generic_options (this, graph, set_dict, "client");
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

char *gd_shd_options[] = {
        "!self-heal-daemon",
        "!heal-timeout",
        NULL
};

char*
gd_get_matching_option (char **options, char *option)
{
        while (*options && strcmp (*options, option))
                options++;
        return *options;
}

static int
shd_option_handler (volgen_graph_t *graph, struct volopt_map_entry *vme,
                    void *param)
{
        int                     ret = 0;
        struct volopt_map_entry new_vme = {0};
        char                    *shd_option = NULL;

        shd_option = gd_get_matching_option (gd_shd_options, vme->option);
        if ((vme->option[0] == '!') && !shd_option)
                goto out;
        new_vme = *vme;
        if (shd_option) {
                new_vme.option = shd_option + 1;//option with out '!'
        }

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
        if (!volinfo || (volinfo->volname[0] == '\0'))
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
                ret = gf_asprintf (&aa, "rpc-auth.auth-unix.%s",
                                        volinfo->volname);

                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }

                if (ret)
                        return -1;
        }
        if (! strcmp (vme->option, "!rpc-auth.auth-null.*")) {
                ret = gf_asprintf (&aa, "rpc-auth.auth-null.%s",
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
                        ret = gf_canonicalize_path (vme->value);
                        if (ret)
                                return -1;

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

          if (! strcmp (vme->option, "!nfs-ganesha.enable")) {
                ret = gf_asprintf (&aa, "nfs-ganesha.%s.enable",
                                        volinfo->volname);

                if (ret != -1) {
                        ret = xlator_set_option (xl, aa, vme->value);
                        GF_FREE (aa);
                }

                if (ret)
                        return -1;
        }

         if (! strcmp (vme->option, "!nfs-ganesha.host")) {
                ret = gf_asprintf (&aa, "nfs-ganesha.%s.host",
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

char*
volgen_get_shd_key (glusterd_volinfo_t *volinfo)
{
        char    *key = NULL;

        switch (volinfo->type) {
        case GF_CLUSTER_TYPE_REPLICATE:
        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                key = "cluster.self-heal-daemon";
                break;
        case GF_CLUSTER_TYPE_DISPERSE:
                key = "cluster.disperse-self-heal-daemon";
                break;
        default:
                key = NULL;
                break;
        }

        return key;
}

static gf_boolean_t
volgen_is_shd_compatible_xl (char *xl_type)
{
        char            *shd_xls[] = {"cluster/replicate", "cluster/disperse",
                                      NULL};
        if (gf_get_index_by_elem (shd_xls, xl_type) != -1)
                return _gf_true;

        return _gf_false;
}

static int
volgen_graph_set_iam_shd (volgen_graph_t *graph)
{
        xlator_t        *trav;
        int             ret = 0;

        for (trav = first_of (graph); trav; trav = trav->next) {
                if (!volgen_is_shd_compatible_xl (trav->type))
                        continue;

                ret = xlator_set_option (trav, "iam-self-heal-daemon", "yes");
                if (ret)
                        break;
        }
        return ret;
}

static int
prepare_shd_volume_options (glusterd_volinfo_t *volinfo,
                            dict_t *mod_dict, dict_t *set_dict)
{
        char    *key = NULL;
        int     ret = 0;

        key = volgen_get_shd_key (volinfo);
        if (!key) {
                ret = -1;
                goto out;
        }

        ret = dict_set_str (set_dict, key, "enable");
        if (ret)
                goto out;

        ret = dict_set_uint32 (set_dict, "trusted-client", GF_CLIENT_TRUSTED);
        if (ret)
                goto out;

        dict_copy (volinfo->dict, set_dict);
        if (mod_dict)
                dict_copy (mod_dict, set_dict);
out:
        return ret;
}

static int
volgen_graph_build_replicate_clusters (volgen_graph_t *graph,
                                       glusterd_volinfo_t *volinfo)
{
        char               *replicate_args[]   = {"cluster/replicate",
                                                  "%s-replicate-%d"};

        return volgen_graph_build_clusters (graph, volinfo, "cluster/replicate",
                                            "%s-replicate-%d",
                                            volinfo->brick_count,
                                            volinfo->replica_count);
}

static int
build_shd_clusters (volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                    dict_t *set_dict)
{
        int     ret = 0;
        int     clusters = -1;

        ret = volgen_graph_build_clients (graph, volinfo, set_dict, NULL);
        if (ret)
                goto out;

        switch (volinfo->type) {
        case GF_CLUSTER_TYPE_REPLICATE:
        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                clusters = volgen_graph_build_replicate_clusters (graph,
                                                                  volinfo);
                break;

        case GF_CLUSTER_TYPE_DISPERSE:
                clusters = volgen_graph_build_ec_clusters (graph, volinfo);
                break;
        }
out:
        return clusters;
}

static int
build_shd_volume_graph (xlator_t *this, volgen_graph_t *graph,
                        glusterd_volinfo_t *volinfo,
                        dict_t *mod_dict, dict_t *set_dict,
                        gf_boolean_t graph_check, gf_boolean_t *valid_config)
{
        volgen_graph_t     cgraph         = {0};
        int     ret = 0;
        int     clusters = -1;

        if (!graph_check && (volinfo->status != GLUSTERD_STATUS_STARTED))
                goto out;

        if (!glusterd_is_shd_compatible_volume (volinfo))
                goto out;

        /* Shd graph is valid only when there is at least one
         * replica/disperse volume is present
         */
        *valid_config = _gf_true;

        ret = prepare_shd_volume_options (volinfo, mod_dict, set_dict);
        if (ret)
                goto out;

        clusters = build_shd_clusters (&cgraph, volinfo, set_dict);
        if (clusters < 0) {
                ret = -1;
                goto out;
        }

        ret = volgen_graph_set_options_generic (&cgraph, set_dict,
                                                volinfo, shd_option_handler);
        if (ret)
                goto out;

        ret = volgen_graph_set_iam_shd (&cgraph);
        if (ret)
                goto out;

        ret = volgen_graph_merge_sub (graph, &cgraph, clusters);
        if (ret)
                goto out;

        ret = graph_set_generic_options (this, graph, set_dict,
                                         "self-heal daemon");
out:
        return ret;
}

int
build_shd_graph (volgen_graph_t *graph, dict_t *mod_dict)
{
        glusterd_volinfo_t *voliter       = NULL;
        xlator_t           *this          = NULL;
        glusterd_conf_t    *priv          = NULL;
        dict_t             *set_dict      = NULL;
        int                ret            = 0;
        gf_boolean_t       valid_config   = _gf_false;
        xlator_t           *iostxl        = NULL;
        int                clusters      = 0;
        gf_boolean_t       graph_check    = _gf_false;

        this = THIS;
        priv = this->private;

        set_dict = dict_new ();
        if (!set_dict) {
                ret = -ENOMEM;
                goto out;
        }

        graph_check = dict_get_str_boolean (mod_dict, "graph-check", 0);
        iostxl = volgen_graph_add_as (graph, "debug/io-stats", "glustershd");
        if (!iostxl) {
                ret = -1;
                goto out;
        }

        cds_list_for_each_entry (voliter, &priv->volumes, vol_list) {
                ret = build_shd_volume_graph (this, graph, voliter, mod_dict,
                                              set_dict, graph_check,
                                              &valid_config);

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
int
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
        char               *volname       = NULL;
        data_t             *data          = NULL;

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

        ret = xlator_set_option (nfsxl, "nfs.nlm", "on");
        if (ret)
                goto out;

        ret = xlator_set_option (nfsxl, "nfs.drc", "off");
        if (ret)
                goto out;

        cds_list_for_each_entry (voliter, &priv->volumes, vol_list) {
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

                /* If both RDMA and TCP are the transport_type, use TCP for NFS
                 * client protocols, because tcp,rdma volume can be created in
                 * servers which does not have rdma supported hardware
                 * The transport type specified here is client transport type
                 * which is used for communication between gluster-nfs and brick
                 * processes.
                 * User can specify client transport for tcp,rdma volume using
                 * nfs.transport-type, if it is not set by user default
                 * one will be tcp.
                 */
                memset (&cgraph, 0, sizeof (cgraph));
                if (mod_dict)
                        get_transport_type (voliter, mod_dict, nfs_xprt, _gf_true);
                else
                        get_transport_type (voliter, voliter->dict, nfs_xprt, _gf_true);

                ret = dict_set_str (set_dict, "performance.stat-prefetch", "off");
                if (ret)
                        goto out;

                ret = dict_set_str (set_dict, "performance.client-io-threads",
                                    "off");
                if (ret)
                        goto out;

                ret = dict_set_str (set_dict, "client-transport-type",
                                    nfs_xprt);
                if (ret)
                        goto out;

                ret = dict_set_uint32 (set_dict, "trusted-client",
                                       GF_CLIENT_TRUSTED);
                if (ret)
                        goto out;

                ret = dict_set_str (set_dict, "nfs-volume-file", "yes");
                if (ret)
                        goto out;

                if (mod_dict && (data = dict_get (mod_dict, "volume-name"))) {
                        volname = data->data;
                        if (strcmp (volname, voliter->volname) == 0)
                                dict_copy (mod_dict, set_dict);
                }

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

        cds_list_for_each_entry (voliter, &priv->volumes, vol_list) {

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
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_brickinfo_new_from_brick (brick, &brickinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Failed to create brickinfo"
                        " for brick %s", brick );
                ret = 0;
                goto out;
        }
        ret = glusterd_volinfo_new (&volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Failed to create volinfo");
                ret = 0;
                goto out;
        }
        strncpy (volinfo->volname, volname, sizeof (volinfo->volname));
        get_brick_filepath (volfpath, volinfo, brickinfo);

        ret = ((strlen(volfpath) < PATH_MAX) &&
                strlen (strrchr(volfpath, '/')) < _POSIX_PATH_MAX);

out:
        if (brickinfo)
                glusterd_brickinfo_delete (brickinfo);
        if (volinfo)
                glusterd_volinfo_unref (volinfo);
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

        ret = build_server_graph (&graph, volinfo, NULL, brickinfo);
        if (!ret)
                ret = volgen_write_volfile (&graph, filename);

        volgen_graph_free (&graph);

        return ret;
}

int
build_quotad_graph (volgen_graph_t *graph, dict_t *mod_dict)
{
        volgen_graph_t     cgraph         = {0};
        glusterd_volinfo_t *voliter       = NULL;
        xlator_t           *this          = NULL;
        glusterd_conf_t    *priv          = NULL;
        dict_t             *set_dict      = NULL;
        int                ret            = 0;
        xlator_t           *quotad_xl     = NULL;
        char               *skey          = NULL;

        this = THIS;
        priv = this->private;

        set_dict = dict_new ();
        if (!set_dict) {
                ret = -ENOMEM;
                goto out;
        }

        quotad_xl = volgen_graph_add_as (graph, "features/quotad", "quotad");
        if (!quotad_xl) {
                ret = -1;
                goto out;
        }

        cds_list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (voliter->status != GLUSTERD_STATUS_STARTED)
                        continue;

                if (1 != glusterd_is_volume_quota_enabled (voliter))
                        continue;

                ret = dict_set_uint32 (set_dict, "trusted-client",
                                       GF_CLIENT_TRUSTED);
                if (ret)
                        goto out;

                dict_copy (voliter->dict, set_dict);
                if (mod_dict)
                        dict_copy (mod_dict, set_dict);

                ret = gf_asprintf(&skey, "%s.volume-id", voliter->volname);
                if (ret == -1) {
                        gf_log("", GF_LOG_ERROR, "Out of memory");
                        goto out;
                }
                ret = xlator_set_option(quotad_xl, skey, voliter->volname);
                GF_FREE(skey);
                if (ret)
                        goto out;

                memset (&cgraph, 0, sizeof (cgraph));
                ret = volgen_graph_build_clients (&cgraph, voliter, set_dict,
                                                  NULL);
                if (ret)
                        goto out;

                ret = volume_volgen_graph_build_clusters (&cgraph, voliter,
                                                          _gf_true);
                if (ret) {
                        ret = -1;
                        goto out;
                }

                if (mod_dict) {
                        dict_copy (mod_dict, set_dict);
                        ret = volgen_graph_set_options_generic (&cgraph, set_dict,
                                                        voliter,
                                                        basic_option_handler);
                } else {
                        ret = volgen_graph_set_options_generic (&cgraph,
                                                                voliter->dict,
                                                                voliter,
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

out:
        if (set_dict)
                dict_unref (set_dict);
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

static void
get_parent_vol_tstamp_file (char *filename, glusterd_volinfo_t *volinfo)
{
        glusterd_conf_t *priv  = NULL;
        xlator_t        *this  = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        snprintf (filename, PATH_MAX, "%s/vols/%s", priv->workdir,
                  volinfo->parent_volname);
        strncat (filename, "/marker.tstamp",
                 PATH_MAX - strlen(filename) - 1);
}

int
generate_brick_volfiles (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t    *brickinfo                    = NULL;
        char                     tstamp_file[PATH_MAX]        = {0,};
        char                     parent_tstamp_file[PATH_MAX] = {0,};
        int                      ret                          = -1;
        xlator_t                *this                         = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_volinfo_get_boolean (volinfo, VKEY_MARKER_XTIME);
        if (ret == -1)
                return -1;

        get_vol_tstamp_file (tstamp_file, volinfo);

        if (ret) {
                ret = open (tstamp_file, O_WRONLY|O_CREAT|O_EXCL, 0600);
                if (ret == -1 && errno == EEXIST) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "timestamp file exist");
                        ret = -2;
                }
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to create "
                                "%s (%s)", tstamp_file, strerror (errno));
                        return -1;
                }
                if (ret >= 0) {
                        close (ret);
                        /* If snap_volume, retain timestamp for marker.tstamp
                         * from parent. Geo-replication depends on mtime of
                         * 'marker.tstamp' to decide the volume-mark, i.e.,
                         * geo-rep start time just after session is created.
                         */
                        if (volinfo->is_snap_volume) {
                                get_parent_vol_tstamp_file (parent_tstamp_file,
                                                            volinfo);
                                ret = gf_set_timestamp (parent_tstamp_file,
                                                        tstamp_file);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Unable to set atime and mtime"
                                                " of %s as of %s", tstamp_file,
                                                parent_tstamp_file);
                                        goto out;
                                }
                        }
                }
        } else {
                ret = unlink (tstamp_file);
                if (ret == -1 && errno == ENOENT)
                        ret = 0;
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to unlink "
                                "%s (%s)", tstamp_file, strerror (errno));
                        return -1;
                }
        }

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Found a brick - %s:%s", brickinfo->hostname,
                        brickinfo->path);

                ret = glusterd_generate_brick_volfile (volinfo, brickinfo);
                if (ret)
                        goto out;

        }

        ret = 0;

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
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

int
generate_client_volfiles (glusterd_volinfo_t *volinfo,
                          glusterd_client_type_t client_type)
{
        int                i                  = 0;
        int                ret                = -1;
        char               filepath[PATH_MAX] = {0,};
        char               *types[]           = {NULL, NULL, NULL};
        dict_t             *dict              = NULL;
        xlator_t           *this              = NULL;
        gf_transport_type  type               = GF_TRANSPORT_TCP;

        this = THIS;

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

                ret = dict_set_uint32 (dict, "trusted-client", client_type);
                if (ret)
                        goto out;

                if (client_type == GF_CLIENT_TRUSTED) {
                        ret = glusterd_get_trusted_client_filepath (filepath,
                                                                    volinfo,
                                                                    type);
                } else {
                        ret = glusterd_get_client_filepath (filepath,
                                                            volinfo,
                                                            type);
                }
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Received invalid transport-type");
                        goto out;
                }

                ret = generate_single_transport_client_volfile (volinfo,
                                                                filepath,
                                                                dict);
                if (ret)
                        goto out;
        }

        /* Generate volfile for rebalance process */
        ret = dict_set_int32 (dict, "rebalance-volfile-creation", _gf_true);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set rebalance-volfile-creation");
                goto out;
        }

        glusterd_get_rebalance_volfile (volinfo, filepath, PATH_MAX);

        ret = generate_single_transport_client_volfile (volinfo,
                                                        filepath,
                                                        dict);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to create rebalance volfile for %s",
                        volinfo->volname);
                goto out;
        }

out:
        if (dict)
                dict_unref (dict);

        gf_log ("", GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

int
glusterd_snapdsvc_generate_volfile (volgen_graph_t *graph,
                                    glusterd_volinfo_t *volinfo)
{
        xlator_t       *xl              = NULL;
        char           *username        = NULL;
        char           *passwd          = NULL;
        int             ret             = 0;
        char            key [PATH_MAX]  = {0, };
        dict_t         *set_dict        = NULL;
        char           *loglevel        = NULL;
        char           *xlator          = NULL;
        char           *value           = NULL;

        set_dict = dict_copy (volinfo->dict, NULL);
        if (!set_dict)
                return -1;

        ret = dict_get_str (set_dict, "xlator", &xlator);
        if (!ret) {
                ret = dict_get_str (set_dict, "loglevel", &loglevel);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "could not get both"
                                " translator name and loglevel for log level "
                                "request");
                        return -1;
                }
        }

        xl = volgen_graph_add (graph, "features/snapview-server",
                               volinfo->volname);
        if (!xl)
                return -1;

        ret = xlator_set_option (xl, "volname", volinfo->volname);
        if (ret)
                return -1;

        xl = volgen_graph_add (graph, "performance/io-threads",
                               volinfo->volname);
        if (!xl)
                return -1;

        snprintf (key, sizeof (key), "snapd-%s", volinfo->volname);
        xl = volgen_graph_add_as (graph, "debug/io-stats", key);
        if (!xl)
                return -1;

        xl = volgen_graph_add (graph, "protocol/server", volinfo->volname);
        if (!xl)
                return -1;

        ret = xlator_set_option (xl, "transport-type", "tcp");
        if (ret)
                return -1;

        if (dict_get_str (set_dict, SSL_CERT_DEPTH_OPT, &value) == 0) {
                ret = xlator_set_option (xl, "ssl-cert-depth", value);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_WARNING,
                                "failed to set ssl-cert-depth");
                        return -1;
                }
        }

        if (dict_get_str (set_dict, SSL_CIPHER_LIST_OPT, &value) == 0) {
                ret = xlator_set_option (xl, "ssl-cipher-list", value);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_WARNING,
                                "failed to set ssl-cipher-list");
                        return -1;
                }
        }

        username = glusterd_auth_get_username (volinfo);
        passwd = glusterd_auth_get_password (volinfo);

        snprintf (key, sizeof (key), "auth.login.snapd-%s.allow",
                                                        volinfo->volname);
        ret = xlator_set_option (xl, key, username);
        if (ret)
                return -1;

        snprintf (key, sizeof (key), "auth.login.%s.password", username);
        ret = xlator_set_option (xl, key, passwd);
        if (ret)
                return -1;

        ret = volgen_graph_set_options_generic
                (graph, set_dict,
                 (xlator && loglevel)? (void *)set_dict: volinfo,
                 (xlator && loglevel) ?
                 &server_spec_extended_option_handler:
                 &server_spec_option_handler);

        return ret;
}

int
glusterd_snapdsvc_create_volfile (glusterd_volinfo_t *volinfo)
{
        volgen_graph_t  graph                   = {0,};
        int             ret                     = -1;
        char            filename [PATH_MAX]     = {0,};

        glusterd_svc_build_snapd_volfile (volinfo, filename, PATH_MAX);

        ret = glusterd_snapdsvc_generate_volfile (&graph, volinfo);
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
                ret = generate_client_volfiles (volinfo, GF_CLIENT_TRUSTED);
        if (!ret)
                ret = glusterd_fetchspec_notify (THIS);

        return ret;
}

int
glusterd_create_volfiles (glusterd_volinfo_t *volinfo)
{
        int        ret  = -1;
        xlator_t  *this = NULL;

        this = THIS;

        ret = generate_brick_volfiles (volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not generate volfiles for bricks");
                goto out;
        }

        ret = generate_client_volfiles (volinfo, GF_CLIENT_TRUSTED);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not generate trusted client volfiles");
                goto out;
        }

        ret = generate_client_volfiles (volinfo, GF_CLIENT_OTHER);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not generate client volfiles");

out:
        return ret;
}

int
glusterd_create_volfiles_and_notify_services (glusterd_volinfo_t *volinfo)
{
        int        ret  = -1;
        xlator_t  *this = NULL;

        this = THIS;

        ret = glusterd_create_volfiles (volinfo);
        if (ret)
                goto out;

        ret = glusterd_fetchspec_notify (this);

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
validate_shdopts (glusterd_volinfo_t *volinfo,
                  dict_t *val_dict,
                  char **op_errstr)
{
        volgen_graph_t graph = {0,};
        int     ret = -1;

        graph.errstr = op_errstr;

        if (!glusterd_is_shd_compatible_volume (volinfo)) {
                ret = 0;
                goto out;
        }
        ret = dict_set_str (val_dict, "graph-check", "on");
        if (ret)
                goto out;
        ret = build_shd_graph (&graph, val_dict);
        if (!ret)
                ret = graph_reconf_validateopt (&graph.graph, op_errstr);

        volgen_graph_free (&graph);

        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
out:
        dict_del (val_dict, "graph-check");
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
        xlator_t *this             = THIS;

        GF_ASSERT (this);

        graph.errstr = op_errstr;

        get_vol_transport_type (volinfo, transport_type);
        ret = dict_get_str (val_dict, "nfs.transport-type", &tt);
        if (!ret) {
                if (volinfo->transport_type != GF_TRANSPORT_BOTH_TCP_RDMA) {
                        snprintf (err_str, sizeof (err_str), "Changing nfs "
                                  "transport type is allowed only for volumes "
                                  "of transport type tcp,rdma");
                        gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
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

        ret = dict_set_str (val_dict, "volume-name", volinfo->volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set volume name");
                goto out;
        }

        ret = build_nfs_graph (&graph, val_dict);
        if (!ret)
                ret = graph_reconf_validateopt (&graph.graph, op_errstr);

        volgen_graph_free (&graph);

out:
        if (dict_get (val_dict, "volume-name"))
                dict_del (val_dict, "volume-name");
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int
validate_clientopts (glusterd_volinfo_t *volinfo,
                     dict_t *val_dict,
                     char **op_errstr)
{
        volgen_graph_t graph = {0,};
        int            ret   = -1;

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
                    glusterd_brickinfo_t *brickinfo,
                    dict_t *val_dict,
                    char **op_errstr)
{
        volgen_graph_t graph = {0,};
        int            ret   = -1;

        GF_ASSERT (volinfo);

        graph.errstr = op_errstr;

        ret = build_server_graph (&graph, volinfo, val_dict, brickinfo);
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

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                gf_log ("", GF_LOG_DEBUG,
                        "Validating %s", brickinfo->hostname);

                ret = validate_brickopts (volinfo, brickinfo, val_dict,
                                          op_errstr);
                if (ret)
                        goto out;
        }

        ret = 0;

out:
        return ret;
}

static int
_check_globalopt (dict_t *this, char *key, data_t *value, void *ret_val)
{
        int *ret = NULL;

        ret = ret_val;
        if (*ret)
                return 0;
        if (!glusterd_check_globaloption (key))
                *ret = 1;

        return 0;
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
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Could not Validate nfs");
                goto out;
        }

        ret = validate_shdopts (volinfo, val_dict, op_errstr);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Could not Validate self-heald");
                goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
_check_localopt (dict_t *this, char *key, data_t *value, void *ret_val)
{
        int *ret = NULL;

        ret = ret_val;
        if (*ret)
                return 0;
        if (!glusterd_check_localoption (key))
                *ret = 1;

        return 0;
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
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Could not Validate nfs");
                goto out;
        }


        ret = validate_shdopts (volinfo, val_dict, op_errstr);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Could not Validate self-heald");
                goto out;
        }


out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static struct volopt_map_entry *
_gd_get_vmep (char *key) {
        char *completion = NULL;
        struct volopt_map_entry *vmep = NULL;
        int ret = 0;

        COMPLETE_OPTION ((char *)key, completion, ret);
        for (vmep = glusterd_volopt_map; vmep->key; vmep++) {
                if (strcmp (vmep->key, key) == 0)
                        return vmep;
        }

        return NULL;
}

uint32_t
glusterd_get_op_version_for_key (char *key)
{
        struct volopt_map_entry *vmep = NULL;

        GF_ASSERT (key);

        vmep = _gd_get_vmep (key);
        if (vmep)
                return vmep->op_version;

        return 0;
}

gf_boolean_t
gd_is_client_option (char *key)
{
        struct volopt_map_entry *vmep = NULL;

        GF_ASSERT (key);

        vmep = _gd_get_vmep (key);
        if (vmep && (vmep->flags & OPT_FLAG_CLIENT_OPT))
                return _gf_true;

        return _gf_false;
}

gf_boolean_t
gd_is_xlator_option (char *key)
{
        struct volopt_map_entry *vmep = NULL;

        GF_ASSERT (key);

        vmep = _gd_get_vmep (key);
        if (vmep && (vmep->flags & OPT_FLAG_XLATOR_OPT))
                return _gf_true;

        return _gf_false;
}

volume_option_type_t
_gd_get_option_type (char *key)
{
        struct volopt_map_entry *vmep = NULL;
        void                    *dl_handle = NULL;
        volume_opt_list_t       vol_opt_list = {{0},};
        int                     ret = -1;
        volume_option_t         *opt = NULL;
        char                    *xlopt_key = NULL;
        volume_option_type_t    opt_type = GF_OPTION_TYPE_MAX;

        GF_ASSERT (key);

        vmep = _gd_get_vmep (key);

        if (vmep) {
                CDS_INIT_LIST_HEAD (&vol_opt_list.list);
                ret = xlator_volopt_dynload (vmep->voltype, &dl_handle,
                                             &vol_opt_list);
                if (ret)
                        goto out;

                if (_get_xlator_opt_key_from_vme (vmep, &xlopt_key))
                        goto out;

                opt = xlator_volume_option_get_list (&vol_opt_list, xlopt_key);
                _free_xlator_opt_key (xlopt_key);

                if (opt)
                        opt_type = opt->type;
        }

out:
        if (dl_handle) {
                dlclose (dl_handle);
                dl_handle = NULL;
        }

        return opt_type;
}

gf_boolean_t
gd_is_boolean_option (char *key)
{
        GF_ASSERT (key);

        if (GF_OPTION_TYPE_BOOL == _gd_get_option_type (key))
                return _gf_true;

        return _gf_false;
}
