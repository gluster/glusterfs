/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <fnmatch.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <utime.h>

#include <glusterfs/xlator.h>
#include "glusterd.h"
#include <glusterfs/defaults.h>
#include <glusterfs/syscall.h>
#include <glusterfs/logging.h>
#include <glusterfs/dict.h>
#include <glusterfs/graph-utils.h>
#include <glusterfs/common-utils.h>
#include "glusterd-store.h"
#include "glusterd-hooks.h"
#include <glusterfs/trie.h>
#include "glusterd-mem-types.h"
#include "cli1-xdr.h"
#include "glusterd-volgen.h"
#include "glusterd-geo-rep.h"
#include "glusterd-utils.h"
#include "glusterd-messages.h"
#include <glusterfs/run.h>
#include <glusterfs/options.h>
#include "glusterd-snapshot-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-svc-helper.h"
#include "glusterd-snapd-svc-helper.h"
#include "glusterd-gfproxyd-svc-helper.h"

struct gd_validate_reconf_opts {
    dict_t *options;
    char **op_errstr;
};

extern struct volopt_map_entry glusterd_volopt_map[];

struct check_and_add_user_xlator_t {
    volgen_graph_t *graph;
    char *volname;
};

#define RPC_SET_OPT(XL, CLI_OPT, XLATOR_OPT, ERROR_CMD)                        \
    do {                                                                       \
        char *_value = NULL;                                                   \
                                                                               \
        if (dict_get_str_sizen(set_dict, CLI_OPT, &_value) == 0) {             \
            if (xlator_set_fixed_option(XL, "transport.socket." XLATOR_OPT,    \
                                        _value) != 0) {                        \
                gf_msg("glusterd", GF_LOG_WARNING, errno,                      \
                       GD_MSG_XLATOR_SET_OPT_FAIL,                             \
                       "failed to set " XLATOR_OPT);                           \
                ERROR_CMD;                                                     \
            }                                                                  \
        }                                                                      \
    } while (0 /* CONSTCOND */)

static int
volgen_graph_build_clients(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                           dict_t *set_dict, void *param);

static int
build_client_graph(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                   dict_t *mod_dict);

/*********************************************
 *
 * xlator generation / graph manipulation API
 *
 *********************************************/

static void
set_graph_errstr(volgen_graph_t *graph, const char *str)
{
    if (!graph->errstr)
        return;

    *graph->errstr = gf_strdup(str);
}

static xlator_t *
xlator_instantiate_va(const char *type, const char *format, va_list arg)
{
    xlator_t *xl = NULL;
    char *volname = NULL;
    int ret = 0;
    xlator_t *this = THIS;

    ret = gf_vasprintf(&volname, format, arg);
    if (ret < 0) {
        volname = NULL;

        goto error;
    }

    xl = GF_CALLOC(1, sizeof(*xl), gf_common_mt_xlator_t);
    if (!xl) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY, NULL);
        goto error;
    }
    ret = xlator_set_type_virtual(xl, type);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_XLATOR_SET_OPT_FAIL,
                NULL);
        goto error;
    }
    xl->options = dict_new();
    if (!xl->options) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        goto error;
    }
    xl->name = volname;
    CDS_INIT_LIST_HEAD(&xl->volume_options);

    xl->ctx = this->ctx;

    return xl;

error:
    gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_XLATOR_CREATE_FAIL, "Type=%s",
            type, NULL);
    GF_FREE(volname);
    if (xl)
        xlator_destroy(xl);

    return NULL;
}

static int
volgen_xlator_link(xlator_t *pxl, xlator_t *cxl)
{
    int ret = 0;

    ret = glusterfs_xlator_link(pxl, cxl);
    if (ret == -1) {
        gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Out of memory, cannot link xlators %s <- %s", pxl->name,
               cxl->name);
    }

    return ret;
}

static int
volgen_graph_link(volgen_graph_t *graph, xlator_t *xl)
{
    int ret = 0;

    /* no need to care about graph->top here */
    if (graph->graph.first)
        ret = volgen_xlator_link(xl, graph->graph.first);
    if (ret == -1) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_GRAPH_ENTRY_ADD_FAIL,
               "failed to add graph entry %s", xl->name);

        return -1;
    }

    return 0;
}

static xlator_t *
volgen_graph_add_as(volgen_graph_t *graph, const char *type, const char *format,
                    ...)
{
    va_list arg;
    xlator_t *xl = NULL;

    va_start(arg, format);
    xl = xlator_instantiate_va(type, format, arg);
    va_end(arg);

    if (!xl)
        return NULL;

    if (volgen_graph_link(graph, xl)) {
        xlator_destroy(xl);

        return NULL;
    } else
        glusterfs_graph_set_first(&graph->graph, xl);

    return xl;
}

static xlator_t *
volgen_graph_add_nolink(volgen_graph_t *graph, const char *type,
                        const char *format, ...)
{
    va_list arg;
    xlator_t *xl = NULL;

    va_start(arg, format);
    xl = xlator_instantiate_va(type, format, arg);
    va_end(arg);

    if (!xl)
        return NULL;

    glusterfs_graph_set_first(&graph->graph, xl);

    return xl;
}

static xlator_t *
volgen_graph_add(volgen_graph_t *graph, char *type, char *volname)
{
    char *shorttype = NULL;

    shorttype = strrchr(type, '/');
    GF_ASSERT(shorttype);
    shorttype++;
    GF_ASSERT(*shorttype);

    return volgen_graph_add_as(graph, type, "%s-%s", volname, shorttype);
}

#define xlator_set_fixed_option(xl, key, value)                                \
    xlator_set_option(xl, key, SLEN(key), value)

/* XXX Seems there is no such generic routine?
 * Maybe should put to xlator.c ??
 */
static int
xlator_set_option(xlator_t *xl, char *key, const int keylen, char *value)
{
    char *dval = gf_strdup(value);

    if (!dval) {
        gf_msg("glusterd", GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY,
               "failed to set xlator opt: %s[%s] = %s", xl->name, key, value);

        return -1;
    }

    return dict_set_dynstrn(xl->options, key, keylen, dval);
}

#define xlator_get_fixed_option(xl, key, value)                                \
    xlator_get_option(xl, key, SLEN(key), value)

static int
xlator_get_option(xlator_t *xl, char *key, const int keylen, char **value)
{
    GF_ASSERT(xl);
    return dict_get_strn(xl->options, key, keylen, value);
}

static xlator_t *
first_of(volgen_graph_t *graph)
{
    return (xlator_t *)graph->graph.first;
}

/**************************
 *
 * Trie glue
 *
 *************************/

static int
volopt_selector(int lvl, char **patt, void *param,
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
                w = strtail(w, patt[i]);
                GF_ASSERT(!w || *w);
                if (!w || *w != '.')
                    goto next;
            } else {
                w = strchr(w, '.');
                GF_ASSERT(w);
            }
            w++;
        }

        dot = strchr(w, '.');
        if (dot) {
            len = dot - w;
            w = gf_strdup(w);
            if (!w)
                return -1;
            w[len] = '\0';
        }
        ret = optcbk(w, param);
        if (dot)
            GF_FREE(w);
        if (ret)
            return -1;
    next:
        continue;
    }

    return 0;
}

static int
volopt_trie_cbk(char *word, void *param)
{
    return trie_add((trie_t *)param, word);
}

static int
process_nodevec(struct trienodevec *nodevec, char **outputhint, char *inputhint)
{
    int ret = 0;
    char *hint1 = NULL;
    char *hint2 = NULL;
    char *hintinfx = "";
    trienode_t **nodes = nodevec->nodes;

    if (!nodes[0]) {
        *outputhint = NULL;
        return 0;
    }

#if 0
        /* Limit as in git */
        if (trienode_get_dist (nodes[0]) >= 6) {
                *outputhint = NULL;
                return 0;
        }
#endif

    if (trienode_get_word(nodes[0], &hint1))
        return -1;

    if (nodevec->cnt < 2 || !nodes[1]) {
        *outputhint = hint1;
        return 0;
    }

    if (trienode_get_word(nodes[1], &hint2)) {
        GF_FREE(hint1);
        return -1;
    }

    if (inputhint)
        hintinfx = inputhint;
    ret = gf_asprintf(outputhint, "%s or %s%s", hint1, hintinfx, hint2);
    if (ret > 0)
        ret = 0;
    if (hint1)
        GF_FREE(hint1);
    if (hint2)
        GF_FREE(hint2);
    return ret;
}

static int
volopt_trie_section(int lvl, char **patt, char *word, char **outputhint,
                    char *inputhint, int hints)
{
    trienode_t *nodes[] = {NULL, NULL};
    struct trienodevec nodevec = {nodes, 2};
    trie_t *trie = NULL;
    int ret = 0;

    trie = trie_new();
    if (!trie)
        return -1;

    if (volopt_selector(lvl, patt, trie, &volopt_trie_cbk)) {
        trie_destroy(trie);

        return -1;
    }

    GF_ASSERT(hints <= 2);
    nodevec.cnt = hints;
    ret = trie_measure_vec(trie, word, &nodevec);
    if (!ret && nodevec.nodes[0])
        ret = process_nodevec(&nodevec, outputhint, inputhint);

    trie_destroy(trie);

    return ret;
}

static int
volopt_trie(char *key, char **hint)
{
    char *patt[] = {NULL};
    char *fullhint = NULL;
    char *inputhint = NULL;
    char *dot = NULL;
    char *dom = NULL;
    int len = 0;
    int ret = 0;

    *hint = NULL;

    dot = strchr(key, '.');
    if (!dot)
        return volopt_trie_section(1, patt, key, hint, inputhint, 2);

    len = dot - key;
    dom = gf_strdup(key);
    if (!dom)
        return -1;
    dom[len] = '\0';

    ret = volopt_trie_section(0, NULL, dom, patt, inputhint, 1);
    GF_FREE(dom);
    if (ret) {
        patt[0] = NULL;
        goto out;
    }
    if (!patt[0])
        goto out;

    inputhint = "...";
    ret = volopt_trie_section(1, patt, dot + 1, hint, inputhint, 2);
    if (ret)
        goto out;
    if (*hint) {
        ret = gf_asprintf(&fullhint, "%s.%s", patt[0], *hint);
        GF_FREE(*hint);
        if (ret >= 0) {
            ret = 0;
            *hint = fullhint;
        }
    }

out:
    GF_FREE(patt[0]);
    if (ret)
        *hint = NULL;

    return ret;
}

/**************************
 *
 * Volume generation engine
 *
 **************************/

typedef int (*volgen_opthandler_t)(volgen_graph_t *graph,
                                   struct volopt_map_entry *vme, void *param);

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

static void
process_option(char *key, data_t *value, void *param)
{
    struct opthandler_data *odt = param;
    struct volopt_map_entry vme = {
        0,
    };

    if (odt->rv)
        return;
    odt->found = _gf_true;

    vme.key = key;
    vme.voltype = odt->vme->voltype;
    vme.option = odt->vme->option;
    vme.op_version = odt->vme->op_version;

    if (!vme.option) {
        vme.option = strrchr(key, '.');
        if (vme.option)
            vme.option++;
        else
            vme.option = key;
    }
    if (odt->data_t_fake)
        vme.value = (char *)value;
    else
        vme.value = value->data;

    odt->rv = odt->handler(odt->graph, &vme, odt->param);
    return;
}

static int
volgen_graph_set_options_generic(volgen_graph_t *graph, dict_t *dict,
                                 void *param, volgen_opthandler_t handler)
{
    struct volopt_map_entry *vme = NULL;
    struct opthandler_data odt = {
        0,
    };
    data_t *data = NULL;
    int keylen;

    odt.graph = graph;
    odt.handler = handler;
    odt.param = param;
    (void)data;

    for (vme = glusterd_volopt_map; vme->key; vme++) {
        keylen = strlen(vme->key);
        if (keylen == SLEN("performance.client-io-threads") &&
            !strcmp(vme->key, "performance.client-io-threads") &&
            dict_get_str_boolean(dict, "skip-CLIOT", _gf_false) == _gf_true) {
            continue;
        }

        odt.vme = vme;
        odt.found = _gf_false;
        odt.data_t_fake = _gf_false;
        data = dict_getn(dict, vme->key, keylen);
        if (data)
            process_option(vme->key, data, &odt);
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
            process_option(vme->key, (data_t *)vme->value, &odt);
            if (odt.rv)
                return odt.rv;
        }
    }

    return 0;
}

static int
no_filter_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                         void *param)
{
    xlator_t *trav;
    int ret = 0;

    for (trav = first_of(graph); trav; trav = trav->next) {
        if (strcmp(trav->type, vme->voltype) != 0)
            continue;
        if (strcmp(vme->option, "ta-remote-port") == 0) {
            if (strstr(trav->name, "-ta-") != NULL) {
                ret = xlator_set_option(trav, "remote-port",
                                        strlen(vme->option), vme->value);
            }
            continue;
        }
        ret = xlator_set_option(trav, vme->option, strlen(vme->option),
                                vme->value);
        if (ret)
            break;
    }
    return ret;
}

static int
basic_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                     void *param)
{
    int ret = 0;

    if (vme->option[0] == '!')
        goto out;

    ret = no_filter_option_handler(graph, vme, param);
out:
    return ret;
}

static int
volgen_graph_set_options(volgen_graph_t *graph, dict_t *dict)
{
    return volgen_graph_set_options_generic(graph, dict, NULL,
                                            &basic_option_handler);
}

static int
optget_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                      void *param)
{
    struct volopt_map_entry *vme2 = param;

    if (strcmp(vme->key, vme2->key) == 0)
        vme2->value = vme->value;

    return 0;
}

/* This getter considers defaults also. */
static int
volgen_dict_get(dict_t *dict, char *key, char **value)
{
    struct volopt_map_entry vme = {
        0,
    };
    int ret = 0;

    vme.key = key;

    ret = volgen_graph_set_options_generic(NULL, dict, &vme,
                                           &optget_option_handler);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Out of memory");

        return -1;
    }

    *value = vme.value;

    return 0;
}

static int
option_complete(char *key, char **completion)
{
    struct volopt_map_entry *vme = NULL;

    *completion = NULL;
    for (vme = glusterd_volopt_map; vme->key; vme++) {
        if (strcmp(strchr(vme->key, '.') + 1, key) != 0)
            continue;

        if (*completion && strcmp(*completion, vme->key) != 0) {
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
        *completion = gf_strdup(*completion);
        return -!*completion;
    }

    return 0;
}

int
glusterd_volinfo_get(glusterd_volinfo_t *volinfo, char *key, char **value)
{
    return volgen_dict_get(volinfo->dict, key, value);
}

int
glusterd_volinfo_get_boolean(glusterd_volinfo_t *volinfo, char *key)
{
    char *val = NULL;
    gf_boolean_t enabled = _gf_false;
    int ret = 0;

    ret = glusterd_volinfo_get(volinfo, key, &val);
    if (ret)
        return -1;

    if (val)
        ret = gf_string2boolean(val, &enabled);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "value for %s option is not valid", key);

        return -1;
    }

    return enabled;
}

gf_boolean_t
glusterd_check_voloption_flags(char *key, int32_t flags)
{
    char *completion = NULL;
    struct volopt_map_entry *vmep = NULL;
    int ret = 0;

    COMPLETE_OPTION(key, completion, ret);
    for (vmep = glusterd_volopt_map; vmep->key; vmep++) {
        if (strcmp(vmep->key, key) == 0) {
            if (vmep->flags & flags)
                return _gf_true;
            else
                return _gf_false;
        }
    }

    return _gf_false;
}

gf_boolean_t
glusterd_check_globaloption(char *key)
{
    char *completion = NULL;
    struct volopt_map_entry *vmep = NULL;
    int ret = 0;

    COMPLETE_OPTION(key, completion, ret);
    for (vmep = glusterd_volopt_map; vmep->key; vmep++) {
        if (strcmp(vmep->key, key) == 0) {
            if ((vmep->type == GLOBAL_DOC) || (vmep->type == GLOBAL_NO_DOC))
                return _gf_true;
            else
                return _gf_false;
        }
    }

    return _gf_false;
}

gf_boolean_t
glusterd_check_localoption(char *key)
{
    char *completion = NULL;
    struct volopt_map_entry *vmep = NULL;
    int ret = 0;

    COMPLETE_OPTION(key, completion, ret);
    for (vmep = glusterd_volopt_map; vmep->key; vmep++) {
        if (strcmp(vmep->key, key) == 0) {
            if ((vmep->type == DOC) || (vmep->type == NO_DOC))
                return _gf_true;
            else
                return _gf_false;
        }
    }

    return _gf_false;
}

int
glusterd_check_option_exists(char *key, char **completion)
{
    struct volopt_map_entry vme = {
        0,
    };
    struct volopt_map_entry *vmep = NULL;
    int ret = 0;

    (void)vme;
    (void)vmep;

    if (!strchr(key, '.')) {
        if (completion) {
            ret = option_complete(key, completion);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
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
        if (strcmp(vmep->key, key) == 0) {
            ret = 1;
            break;
        }
    }

    if (ret || !completion)
        return ret;

trie:
    ret = volopt_trie(key, completion);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_ERROR_ENCOUNTERED,
               "Some error occurred during keyword hinting");
    }

    return ret;
}

int
glusterd_volopt_validate(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                         char *value, char **op_errstr)
{
    struct volopt_map_entry *vme = NULL;
    int ret = 0;

    if (!dict || !key || !value) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, EINVAL,
                         GD_MSG_INVALID_ENTRY,
                         "Invalid "
                         "Arguments (dict=%p, key=%s, value=%s)",
                         dict, key, value);
        return -1;
    }

    for (vme = &glusterd_volopt_map[0]; vme->key; vme++) {
        if ((vme->validate_fn) && ((!strcmp(key, vme->key)) ||
                                   (!strcmp(key, strchr(vme->key, '.') + 1)))) {
            if ((vme->type != GLOBAL_DOC && vme->type != GLOBAL_NO_DOC) &&
                !volinfo) {
                gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
                       "%s is not"
                       " a global option",
                       vme->key);
                ret = -1;
                goto out;
            }
            ret = vme->validate_fn(volinfo, dict, key, value, op_errstr);
            if (ret)
                goto out;
            break;
        }
    }
out:
    return ret;
}

char *
glusterd_get_trans_type_rb(gf_transport_type ttype)
{
    char *trans_type = NULL;

    switch (ttype) {
        case GF_TRANSPORT_RDMA:
            gf_asprintf(&trans_type, "rdma");
            break;
        case GF_TRANSPORT_TCP:
        case GF_TRANSPORT_BOTH_TCP_RDMA:
            gf_asprintf(&trans_type, "tcp");
            break;
        default:
            gf_msg(THIS->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                   "Unknown "
                   "transport type");
    }

    return trans_type;
}

static int
_xl_link_children(xlator_t *parent, xlator_t *children, size_t child_count)
{
    xlator_t *trav = NULL;
    size_t seek = 0;
    int ret = -1;
    xlator_t *this = THIS;

    if (child_count == 0)
        goto out;
    seek = child_count;
    for (trav = children; --seek; trav = trav->next)
        ;
    for (; child_count--; trav = trav->prev) {
        ret = volgen_xlator_link(parent, trav);
        gf_msg_debug(this->name, 0, "%s:%s", parent->name, trav->name);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_XLATOR_LINK_FAIL,
                    NULL);
            goto out;
        }
    }
    ret = 0;
out:
    return ret;
}

static int
volgen_graph_merge_sub(volgen_graph_t *dgraph, volgen_graph_t *sgraph,
                       size_t child_count)
{
    xlator_t *trav = NULL;
    int ret = 0;

    GF_ASSERT(dgraph->graph.first);

    ret = _xl_link_children(first_of(dgraph), first_of(sgraph), child_count);
    if (ret)
        goto out;

    for (trav = first_of(dgraph); trav->next; trav = trav->next)
        ;

    trav->next = first_of(sgraph);
    trav->next->prev = trav;
    dgraph->graph.xl_count += sgraph->graph.xl_count;

out:
    return ret;
}

static void
volgen_apply_filters(char *orig_volfile)
{
    DIR *filterdir = NULL;
    struct dirent *entry = NULL;
    struct dirent scratch[2] = {
        {
            0,
        },
    };
    struct stat statbuf = {
        0,
    };
    char filterpath[PATH_MAX] = {
        0,
    };

    filterdir = sys_opendir(FILTERDIR);

    if (!filterdir)
        return;

    for (;;) {
        errno = 0;

        entry = sys_readdir(filterdir, scratch);

        if (!entry || errno != 0) {
            gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_READ_ERROR, NULL);
            break;
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        /*
         * d_type isn't guaranteed to be present/valid on all systems,
         * so do an explicit stat instead.
         */
        (void)snprintf(filterpath, sizeof(filterpath), "%s/%s", FILTERDIR,
                       entry->d_name);

        /* Deliberately use stat instead of lstat to allow symlinks. */
        if (sys_stat(filterpath, &statbuf) == -1)
            continue;

        if (!S_ISREG(statbuf.st_mode))
            continue;
        /*
         * We could check the mode in statbuf directly, or just skip
         * this entirely and check for EPERM after exec fails, but this
         * is cleaner.
         */
        if (sys_access(filterpath, X_OK) != 0)
            continue;

        if (runcmd(filterpath, orig_volfile, NULL)) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_FILTER_RUN_FAILED,
                   "failed to run filter %s", entry->d_name);
        }
    }

    (void)sys_closedir(filterdir);
}

static int
volgen_write_volfile(volgen_graph_t *graph, char *filename)
{
    char *ftmp = NULL;
    FILE *f = NULL;
    int fd = 0;
    xlator_t *this = THIS;

    if (gf_asprintf(&ftmp, "%s.tmp", filename) == -1) {
        ftmp = NULL;
        goto error;
    }

    fd = sys_creat(ftmp, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "file creation failed");
        goto error;
    }

    sys_close(fd);

    f = fopen(ftmp, "w");
    if (!f)
        goto error;

    if (glusterfs_graph_print_file(f, &graph->graph) == -1)
        goto error;

    if (fclose(f) != 0) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "fclose on the file %s "
               "failed",
               ftmp);
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

    if (sys_rename(ftmp, filename) == -1)
        goto error;

    GF_FREE(ftmp);

    volgen_apply_filters(filename);

    return 0;

error:

    GF_FREE(ftmp);
    if (f)
        fclose(f);

    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
           "failed to create volfile %s", filename);

    return -1;
}

static void
volgen_graph_free(volgen_graph_t *graph)
{
    xlator_t *trav = NULL;
    xlator_t *trav_old = NULL;

    for (trav = first_of(graph);; trav = trav->next) {
        if (trav_old)
            xlator_destroy(trav_old);

        trav_old = trav;

        if (!trav)
            break;
    }
}

static int
build_graph_generic(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                    dict_t *mod_dict, void *param,
                    int (*builder)(volgen_graph_t *graph,
                                   glusterd_volinfo_t *volinfo,
                                   dict_t *set_dict, void *param))
{
    dict_t *set_dict = NULL;
    int ret = 0;

    if (mod_dict) {
        set_dict = dict_copy_with_ref(volinfo->dict, NULL);
        if (!set_dict)
            return -1;
        dict_copy(mod_dict, set_dict);
        /* XXX dict_copy swallows errors */
    } else {
        set_dict = volinfo->dict;
    }

    ret = builder(graph, volinfo, set_dict, param);
    if (!ret)
        ret = volgen_graph_set_options(graph, set_dict);

    if (mod_dict)
        dict_unref(set_dict);

    return ret;
}

static gf_transport_type
transport_str_to_type(char *tt)
{
    gf_transport_type type = GF_TRANSPORT_TCP;

    if (!strcmp("tcp", tt))
        type = GF_TRANSPORT_TCP;
    else if (!strcmp("rdma", tt))
        type = GF_TRANSPORT_RDMA;
    else if (!strcmp("tcp,rdma", tt))
        type = GF_TRANSPORT_BOTH_TCP_RDMA;
    return type;
}

static void
transport_type_to_str(gf_transport_type type, char *tt)
{
    switch (type) {
        case GF_TRANSPORT_RDMA:
            strcpy(tt, "rdma");
            break;
        case GF_TRANSPORT_TCP:
            strcpy(tt, "tcp");
            break;
        case GF_TRANSPORT_BOTH_TCP_RDMA:
            strcpy(tt, "tcp,rdma");
            break;
    }
}

static void
get_vol_transport_type(glusterd_volinfo_t *volinfo, char *tt)
{
    transport_type_to_str(volinfo->transport_type, tt);
}

#ifdef BUILD_GNFS
/* If no value has specified for tcp,rdma volume from cli
 * use tcp as default value.Otherwise, use transport type
 * mentioned in volinfo
 */
static void
get_vol_nfs_transport_type(glusterd_volinfo_t *volinfo, char *tt)
{
    if (volinfo->transport_type == GF_TRANSPORT_BOTH_TCP_RDMA) {
        strcpy(tt, "tcp");
        gf_msg("glusterd", GF_LOG_INFO, 0, GD_MSG_DEFAULT_OPT_INFO,
               "The default transport type for tcp,rdma volume "
               "is tcp if option is not defined by the user ");
    } else
        transport_type_to_str(volinfo->transport_type, tt);
}
#endif

/*  gets the volinfo, dict, a character array for filling in
 *  the transport type and a boolean option which says whether
 *  the transport type is required for nfs or not. If its not
 *  for nfs, then it is considered as the client transport
 *  and client transport type is filled in the character array
 */
static void
get_transport_type(glusterd_volinfo_t *volinfo, dict_t *set_dict, char *transt,
                   gf_boolean_t is_nfs)
{
    int ret = -1;
    char *tt = NULL;

    if (is_nfs == _gf_false) {
        ret = dict_get_str_sizen(set_dict, "client-transport-type", &tt);
        if (ret)
            get_vol_transport_type(volinfo, transt);
    } else {
#ifdef BUILD_GNFS
        ret = dict_get_str_sizen(set_dict, "nfs.transport-type", &tt);
        if (ret)
            get_vol_nfs_transport_type(volinfo, transt);
#endif
    }

    if (!ret)
        strcpy(transt, tt);
}

static int
server_auth_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                           void *param)
{
    xlator_t *xl = NULL;
    char *aa = NULL;
    int ret = 0;
    char *key = NULL;
    char *auth_path = NULL;

    if (strcmp(vme->option, "!server-auth") != 0)
        return 0;

    xl = first_of(graph);

    /* from 'auth.allow' -> 'allow', and 'auth.reject' -> 'reject' */
    key = strchr(vme->key, '.') + 1;

    ret = xlator_get_fixed_option(xl, "auth-path", &auth_path);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DEFAULT_OPT_INFO,
               "Failed to get auth-path from server graph");
        return -1;
    }
    ret = gf_asprintf(&aa, "auth.addr.%s.%s", auth_path, key);
    if (ret != -1) {
        ret = xlator_set_option(xl, aa, ret, vme->value);
        GF_FREE(aa);
    }
    if (ret)
        return -1;

    return 0;
}

static int
loglevel_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                        void *param)
{
    char *role = param;
    struct volopt_map_entry vme2 = {
        0,
    };

    if ((strcmp(vme->option, "!client-log-level") != 0 &&
         strcmp(vme->option, "!brick-log-level") != 0) ||
        !strstr(vme->key, role))
        return 0;

    memcpy(&vme2, vme, sizeof(vme2));
    vme2.option = "log-level";

    return basic_option_handler(graph, &vme2, NULL);
}

static int
threads_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                       void *param)
{
    char *role = param;
    struct volopt_map_entry vme2 = {
        0,
    };

    if ((strcmp(vme->option, "!client-threads") != 0 &&
         strcmp(vme->option, "!brick-threads") != 0) ||
        !strstr(vme->key, role))
        return 0;

    memcpy(&vme2, vme, sizeof(vme2));
    vme2.option = "threads";

    return basic_option_handler(graph, &vme2, NULL);
}

static int
server_check_changelog_off(volgen_graph_t *graph, struct volopt_map_entry *vme,
                           glusterd_volinfo_t *volinfo)
{
    gf_boolean_t enabled = _gf_false;
    int ret = 0;

    GF_ASSERT(volinfo);
    GF_ASSERT(vme);

    if (strcmp(vme->option, "changelog") != 0)
        return 0;

    ret = gf_string2boolean(vme->value, &enabled);
    if (ret || enabled)
        goto out;

    ret = glusterd_volinfo_get_boolean(volinfo, VKEY_CHANGELOG);
    if (ret < 0) {
        gf_msg("glusterd", GF_LOG_WARNING, 0, GD_MSG_CHANGELOG_GET_FAIL,
               "failed to get the changelog status");
        ret = -1;
        goto out;
    }

    if (ret) {
        enabled = _gf_false;
        glusterd_check_geo_rep_configured(volinfo, &enabled);

        if (enabled) {
            gf_msg("glusterd", GF_LOG_WARNING, 0, GD_MSG_XLATOR_SET_OPT_FAIL,
                   GEOREP
                   " sessions active"
                   "for the volume %s, cannot disable changelog ",
                   volinfo->volname);
            set_graph_errstr(graph, VKEY_CHANGELOG
                             " cannot be disabled "
                             "while " GEOREP " sessions exist");
            ret = -1;
            goto out;
        }
    }

    ret = 0;
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

static int
server_check_marker_off(volgen_graph_t *graph, struct volopt_map_entry *vme,
                        glusterd_volinfo_t *volinfo)
{
    gf_boolean_t enabled = _gf_false;
    int ret = 0;

    GF_ASSERT(volinfo);
    GF_ASSERT(vme);

    if (strcmp(vme->option, "!xtime") != 0)
        return 0;

    ret = gf_string2boolean(vme->value, &enabled);
    if (ret || enabled)
        goto out;

    ret = glusterd_volinfo_get_boolean(volinfo, VKEY_MARKER_XTIME);
    if (ret < 0) {
        gf_msg("glusterd", GF_LOG_WARNING, 0, GD_MSG_MARKER_STATUS_GET_FAIL,
               "failed to get the marker status");
        ret = -1;
        goto out;
    }

    if (ret) {
        enabled = _gf_false;
        glusterd_check_geo_rep_configured(volinfo, &enabled);

        if (enabled) {
            gf_msg("glusterd", GF_LOG_WARNING, 0, GD_MSG_MARKER_DISABLE_FAIL,
                   GEOREP
                   " sessions active"
                   "for the volume %s, cannot disable marker ",
                   volinfo->volname);
            set_graph_errstr(graph, VKEY_MARKER_XTIME
                             " cannot be disabled "
                             "while " GEOREP " sessions exist");
            ret = -1;
            goto out;
        }
    }

    ret = 0;
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

static int
sys_loglevel_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                            void *param)
{
    char *role = NULL;
    struct volopt_map_entry vme2 = {
        0,
    };

    role = (char *)param;

    if (strcmp(vme->option, "!sys-log-level") != 0 || !strstr(vme->key, role))
        return 0;

    memcpy(&vme2, vme, sizeof(vme2));
    vme2.option = "sys-log-level";

    return basic_option_handler(graph, &vme2, NULL);
}

static int
logger_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                      void *param)
{
    char *role = NULL;
    struct volopt_map_entry vme2 = {
        0,
    };

    role = (char *)param;

    if (strcmp(vme->option, "!logger") != 0 || !strstr(vme->key, role))
        return 0;

    memcpy(&vme2, vme, sizeof(vme2));
    vme2.option = "logger";

    return basic_option_handler(graph, &vme2, NULL);
}

static int
log_format_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                          void *param)
{
    char *role = NULL;
    struct volopt_map_entry vme2 = {
        0,
    };

    role = (char *)param;

    if (strcmp(vme->option, "!log-format") != 0 || !strstr(vme->key, role))
        return 0;

    memcpy(&vme2, vme, sizeof(vme2));
    vme2.option = "log-format";

    return basic_option_handler(graph, &vme2, NULL);
}

static int
log_localtime_logging_option_handler(volgen_graph_t *graph,
                                     struct volopt_map_entry *vme, void *param)
{
    char *role = NULL;
    struct volopt_map_entry vme2 = {
        0,
    };

    role = (char *)param;

    if (strcmp(vme->option, "!cluster.localtime-logging") != 0 ||
        !strstr(vme->key, role))
        return 0;

    memcpy(&vme2, vme, sizeof(vme2));
    vme2.option = GLUSTERD_LOCALTIME_LOGGING_KEY;

    return basic_option_handler(graph, &vme2, NULL);
}

static int
log_buf_size_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                            void *param)
{
    char *role = NULL;
    struct volopt_map_entry vme2 = {
        0,
    };

    role = (char *)param;

    if (strcmp(vme->option, "!log-buf-size") != 0 || !strstr(vme->key, role))
        return 0;

    memcpy(&vme2, vme, sizeof(vme2));
    vme2.option = "log-buf-size";

    return basic_option_handler(graph, &vme2, NULL);
}

static int
log_flush_timeout_option_handler(volgen_graph_t *graph,
                                 struct volopt_map_entry *vme, void *param)
{
    char *role = NULL;
    struct volopt_map_entry vme2 = {
        0,
    };

    role = (char *)param;

    if (strcmp(vme->option, "!log-flush-timeout") != 0 ||
        !strstr(vme->key, role))
        return 0;

    memcpy(&vme2, vme, sizeof(vme2));
    vme2.option = "log-flush-timeout";

    return basic_option_handler(graph, &vme2, NULL);
}

static int
volgen_graph_set_xl_options(volgen_graph_t *graph, dict_t *dict)
{
    int32_t ret = -1;
    char *xlator = NULL;
    char xlator_match[1024] = {
        0,
    }; /* for posix* -> *posix* */
    char *loglevel = NULL;
    xlator_t *trav = NULL;

    ret = dict_get_str_sizen(dict, "xlator", &xlator);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=xlator", NULL);
        goto out;
    }

    ret = dict_get_str_sizen(dict, "loglevel", &loglevel);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=loglevel", NULL);
        goto out;
    }

    snprintf(xlator_match, 1024, "*%s", xlator);

    for (trav = first_of(graph); trav; trav = trav->next) {
        if (fnmatch(xlator_match, trav->type, FNM_NOESCAPE) == 0) {
            gf_msg_debug("glusterd", 0, "Setting log level for xlator: %s",
                         trav->type);
            ret = xlator_set_fixed_option(trav, "log-level", loglevel);
            if (ret)
                break;
        }
    }

out:
    return ret;
}

static int
server_spec_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                           void *param)
{
    int ret = 0;
    glusterd_volinfo_t *volinfo = NULL;

    volinfo = param;

    ret = server_auth_option_handler(graph, vme, NULL);
    if (!ret)
        ret = server_check_marker_off(graph, vme, volinfo);

    if (!ret)
        ret = server_check_changelog_off(graph, vme, volinfo);

    if (!ret)
        ret = loglevel_option_handler(graph, vme, "brick");

    if (!ret)
        ret = sys_loglevel_option_handler(graph, vme, "brick");

    if (!ret)
        ret = logger_option_handler(graph, vme, "brick");

    if (!ret)
        ret = log_format_option_handler(graph, vme, "brick");

    if (!ret)
        ret = log_buf_size_option_handler(graph, vme, "brick");

    if (!ret)
        ret = log_flush_timeout_option_handler(graph, vme, "brick");

    if (!ret)
        ret = log_localtime_logging_option_handler(graph, vme, "brick");

    if (!ret)
        ret = threads_option_handler(graph, vme, "brick");

    return ret;
}

static int
server_spec_extended_option_handler(volgen_graph_t *graph,
                                    struct volopt_map_entry *vme, void *param)
{
    int ret = 0;
    dict_t *dict = NULL;

    GF_ASSERT(param);
    dict = (dict_t *)param;

    ret = server_auth_option_handler(graph, vme, NULL);
    if (!ret)
        ret = volgen_graph_set_xl_options(graph, dict);

    return ret;
}

static void
get_vol_tstamp_file(char *filename, glusterd_volinfo_t *volinfo);

static int
gfproxy_server_graph_builder(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                             dict_t *set_dict, void *param)
{
    xlator_t *xl = NULL;
    /*char            *value          = NULL;*/
    char transt[16] = {
        0,
    };
    char key[1024] = {
        0,
    };
    int keylen;
    /*char            port_str[7]     = {0, };*/
    int ret = 0;
    char *username = NULL;
    char *password = NULL;
    /*int             rclusters       = 0;*/

    /* We are a trusted client */
    ret = dict_set_uint32(set_dict, "trusted-client", GF_CLIENT_TRUSTED);
    if (ret != 0) {
        gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=trusted-client", NULL);
        goto out;
    }

    ret = dict_set_int32_sizen(set_dict, "gfproxy-server", 1);
    if (ret != 0) {
        gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=gfproxy-server", NULL);
        goto out;
    }

    /* Build the client section of the graph first */
    build_client_graph(graph, volinfo, set_dict);

    /* Clear this setting so that future users of set_dict do not end up
     * thinking they are a gfproxy server */
    dict_del_sizen(set_dict, "gfproxy-server");
    dict_del_sizen(set_dict, "trusted-client");

    /* Then add the server to it */
    get_vol_transport_type(volinfo, transt);
    xl = volgen_graph_add(graph, "protocol/server", volinfo->volname);
    if (!xl)
        goto out;

    ret = xlator_set_fixed_option(xl, "transport-type", transt);
    if (ret != 0)
        goto out;

    /* Set username and password */
    username = glusterd_auth_get_username(volinfo);
    password = glusterd_auth_get_password(volinfo);
    if (username) {
        keylen = snprintf(key, sizeof(key), "auth.login.gfproxyd-%s.allow",
                          volinfo->volname);
        ret = xlator_set_option(xl, key, keylen, username);
        if (ret)
            return -1;
    }

    if (password) {
        keylen = snprintf(key, sizeof(key), "auth.login.%s.password", username);
        ret = xlator_set_option(xl, key, keylen, password);
        if (ret != 0)
            goto out;
    }

    snprintf(key, sizeof(key), "gfproxyd-%s", volinfo->volname);
    ret = xlator_set_fixed_option(xl, "auth-path", key);

out:
    return ret;
}

static int
brick_graph_add_posix(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    char tmpstr[10] = {
        0,
    };
    int ret = -1;
    gf_boolean_t quota_enabled = _gf_true;
    gf_boolean_t trash_enabled = _gf_false;
    gf_boolean_t pgfid_feat = _gf_false;
    char *value = NULL;
    xlator_t *xl = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;

    if (!graph || !volinfo || !set_dict || !brickinfo) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    priv = this->private;
    GF_VALIDATE_OR_GOTO("glusterd", priv, out);

    ret = glusterd_volinfo_get(volinfo, VKEY_FEATURES_QUOTA, &value);
    if (value) {
        ret = gf_string2boolean(value, &quota_enabled);
        if (ret)
            goto out;
    }

    ret = glusterd_volinfo_get(volinfo, VKEY_FEATURES_TRASH, &value);
    if (value) {
        ret = gf_string2boolean(value, &trash_enabled);
        if (ret)
            goto out;
    }

    ret = glusterd_volinfo_get(volinfo, "update-link-count-parent", &value);
    if (value) {
        ret = gf_string2boolean(value, &pgfid_feat);
        if (ret)
            goto out;
    }

    ret = -1;

    xl = volgen_graph_add(graph, "storage/posix", volinfo->volname);
    if (!xl)
        goto out;

    ret = xlator_set_fixed_option(xl, "directory", brickinfo->path);
    if (ret)
        goto out;

    ret = xlator_set_fixed_option(xl, "volume-id",
                                  uuid_utoa(volinfo->volume_id));
    if (ret)
        goto out;

    if (quota_enabled || pgfid_feat || trash_enabled) {
        ret = xlator_set_fixed_option(xl, "update-link-count-parent", "on");
        if (ret) {
            goto out;
        }
    }

    if (priv->op_version >= GD_OP_VERSION_7_0) {
        ret = xlator_set_fixed_option(xl, "fips-mode-rchecksum", "on");
        if (ret) {
            goto out;
        }
    }
    snprintf(tmpstr, sizeof(tmpstr), "%d", brickinfo->fs_share_count);
    ret = xlator_set_fixed_option(xl, "shared-brick-count", tmpstr);
out:
    return ret;
}

static int
brick_graph_add_selinux(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                        dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    int ret = -1;

    if (!graph || !volinfo) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    xl = volgen_graph_add(graph, "features/selinux", volinfo->volname);
    if (!xl)
        goto out;

    ret = 0;
out:
    return ret;
}

static int
brick_graph_add_trash(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    int ret = -1;
    xlator_t *xl = NULL;

    xl = volgen_graph_add(graph, "features/trash", volinfo->volname);
    if (!xl)
        goto out;
    ret = xlator_set_fixed_option(xl, "trash-dir", ".trashcan");
    if (ret)
        goto out;
    ret = xlator_set_fixed_option(xl, "brick-path", brickinfo->path);
    if (ret)
        goto out;
    ret = xlator_set_fixed_option(xl, "trash-internal-op", "off");
    if (ret)
        goto out;
out:
    return ret;
}

static int
brick_graph_add_arbiter(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                        dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    glusterd_brickinfo_t *last = NULL;
    int ret = -1;

    if (volinfo->arbiter_count != 1)
        return 0;

    /* Add arbiter only if it is the last (i.e. 3rd) brick. */
    last = get_last_brick_of_brick_group(volinfo, brickinfo);
    if (last != brickinfo)
        return 0;

    xl = volgen_graph_add(graph, "features/arbiter", volinfo->volname);
    if (!xl)
        goto out;
    ret = 0;
out:
    return ret;
}

static int
brick_graph_add_bitrot_stub(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                            dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    int ret = -1;
    char *value = NULL;

    if (!graph || !volinfo || !set_dict || !brickinfo) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    xl = volgen_graph_add(graph, "features/bitrot-stub", volinfo->volname);
    if (!xl)
        goto out;

    ret = xlator_set_fixed_option(xl, "export", brickinfo->path);
    if (ret) {
        gf_log(THIS->name, GF_LOG_WARNING,
               "failed to set the export "
               "option in bit-rot-stub");
        goto out;
    }

    ret = glusterd_volinfo_get(volinfo, VKEY_FEATURES_BITROT, &value);
    ret = xlator_set_fixed_option(xl, "bitrot", value);
    if (ret)
        gf_log(THIS->name, GF_LOG_WARNING,
               "failed to set bitrot "
               "enable option in bit-rot-stub");

out:
    return ret;
}

static int
brick_graph_add_changelog(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                          dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    char changelog_basepath[PATH_MAX] = {
        0,
    };
    int ret = -1;
    int32_t len = 0;

    if (!graph || !volinfo || !set_dict || !brickinfo) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    xl = volgen_graph_add(graph, "features/changelog", volinfo->volname);
    if (!xl)
        goto out;

    ret = xlator_set_fixed_option(xl, "changelog-brick", brickinfo->path);
    if (ret)
        goto out;

    len = snprintf(changelog_basepath, sizeof(changelog_basepath), "%s/%s",
                   brickinfo->path, ".glusterfs/changelogs");
    if ((len < 0) || (len >= sizeof(changelog_basepath))) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
        ret = -1;
        goto out;
    }
    ret = xlator_set_fixed_option(xl, "changelog-dir", changelog_basepath);
    if (ret)
        goto out;

    ret = glusterd_is_bitrot_enabled(volinfo);
    if (ret == -1) {
        goto out;
    } else if (ret) {
        ret = xlator_set_fixed_option(xl, "changelog-notification", "on");
        if (ret)
            goto out;
    } else {
        ret = xlator_set_fixed_option(xl, "changelog-notification", "off");
        if (ret)
            goto out;
    }
out:
    return ret;
}

static int
brick_graph_add_acl(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                    dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    int ret = -1;

    if (!graph || !volinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    ret = dict_get_str_boolean(set_dict, "features.acl", 1);
    if (!ret) {
        /* Skip creating this volume if option is disabled */
        /* By default, this is 'true' */
        goto out;
    } else if (ret < 0) {
        /* lets not treat this as error, as this option is not critical,
           and implemented for debug help */
        gf_log(THIS->name, GF_LOG_INFO,
               "failed to get 'features.acl' flag from dict");
    }

    xl = volgen_graph_add(graph, "features/access-control", volinfo->volname);
    if (!xl) {
        ret = -1;
        goto out;
    }
    ret = 0;
out:
    return ret;
}

static int
brick_graph_add_locks(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    int ret = -1;

    if (!graph || !volinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    xl = volgen_graph_add(graph, "features/locks", volinfo->volname);
    if (!xl)
        goto out;

    ret = 0;
out:
    return ret;
}

static int
brick_graph_add_iot(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                    dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    int ret = -1;

    if (!graph || !volinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    xl = volgen_graph_add(graph, "performance/io-threads", volinfo->volname);
    if (!xl)
        goto out;
    ret = 0;
out:
    return ret;
}

static int
brick_graph_add_barrier(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                        dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    int ret = -1;

    if (!graph || !volinfo) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    xl = volgen_graph_add(graph, "features/barrier", volinfo->volname);
    if (!xl)
        goto out;

    ret = 0;
out:
    return ret;
}

static int
brick_graph_add_sdfs(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                     dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    int ret = -1;

    if (!graph || !volinfo) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    if (!dict_get_str_boolean(set_dict, "features.sdfs", 0)) {
        /* update only if option is enabled */
        ret = 0;
        goto out;
    }

    xl = volgen_graph_add(graph, "features/sdfs", volinfo->volname);
    if (!xl)
        goto out;
    /* If we don't set this option here, the translator by default marks
       it 'pass-through' */
    ret = xlator_set_fixed_option(xl, "pass-through", "false");
    if (ret)
        goto out;

    ret = 0;
out:
    return ret;
}

static int
brick_graph_add_namespace(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                          dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    int ret = -1;

    if (!graph || !volinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    ret = dict_get_str_boolean(set_dict, "features.tag-namespaces", 0);
    if (ret == -1)
        goto out;

    if (ret) {
        xl = volgen_graph_add(graph, "features/namespace", volinfo->volname);
        if (!xl)
            goto out;
    }

    ret = 0;
out:
    return ret;
}

xlator_t *
add_one_peer(volgen_graph_t *graph, glusterd_brickinfo_t *peer, char *volname,
             uint16_t index)
{
    xlator_t *kid;

    kid = volgen_graph_add_nolink(graph, "protocol/client", "%s-client-%u",
                                  volname, index++);
    if (!kid) {
        return NULL;
    }

    /* TBD: figure out where to get the proper transport list */
    if (xlator_set_fixed_option(kid, "transport-type", "socket")) {
        return NULL;
    }
    if (xlator_set_fixed_option(kid, "remote-host", peer->hostname)) {
        return NULL;
    }
    if (xlator_set_fixed_option(kid, "remote-subvolume", peer->path)) {
        return NULL;
    }
    /* TBD: deal with RDMA, SSL */

    return kid;
}

static int
brick_graph_add_index(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    char *pending_xattr = NULL;
    char index_basepath[PATH_MAX] = {0};
    int ret = -1;
    int32_t len = 0;

    if (!graph || !volinfo || !brickinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    xl = volgen_graph_add(graph, "features/index", volinfo->volname);
    if (!xl)
        goto out;

    len = snprintf(index_basepath, sizeof(index_basepath), "%s/%s",
                   brickinfo->path, ".glusterfs/indices");
    if ((len < 0) || (len >= sizeof(index_basepath))) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
        goto out;
    }

    ret = xlator_set_fixed_option(xl, "index-base", index_basepath);
    if (ret)
        goto out;
    if (volinfo->type == GF_CLUSTER_TYPE_DISPERSE) {
        ret = xlator_set_fixed_option(xl, "xattrop64-watchlist",
                                      "trusted.ec.dirty");
        if (ret)
            goto out;
    }
    if ((volinfo->type == GF_CLUSTER_TYPE_REPLICATE ||
         volinfo->type == GF_CLUSTER_TYPE_NONE)) {
        ret = xlator_set_fixed_option(xl, "xattrop-dirty-watchlist",
                                      "trusted.afr.dirty");
        if (ret)
            goto out;
        ret = gf_asprintf(&pending_xattr, "trusted.afr.%s-", volinfo->volname);
        if (ret < 0)
            goto out;
        ret = xlator_set_fixed_option(xl, "xattrop-pending-watchlist",
                                      pending_xattr);
        if (ret)
            goto out;
    }
out:
    GF_FREE(pending_xattr);
    return ret;
}

static int
brick_graph_add_marker(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                       dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    int ret = -1;
    xlator_t *xl = NULL;
    char tstamp_file[PATH_MAX] = {
        0,
    };
    char volume_id[64] = {
        0,
    };
    char buf[32] = {
        0,
    };

    if (!graph || !volinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    xl = volgen_graph_add(graph, "features/marker", volinfo->volname);
    if (!xl)
        goto out;

    gf_uuid_unparse(volinfo->volume_id, volume_id);
    ret = xlator_set_fixed_option(xl, "volume-uuid", volume_id);
    if (ret)
        goto out;
    get_vol_tstamp_file(tstamp_file, volinfo);
    ret = xlator_set_fixed_option(xl, "timestamp-file", tstamp_file);
    if (ret)
        goto out;

    snprintf(buf, sizeof(buf), "%d", volinfo->quota_xattr_version);
    ret = xlator_set_fixed_option(xl, "quota-version", buf);
    if (ret)
        goto out;

out:
    return ret;
}

static int
brick_graph_add_quota(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    int ret = -1;
    xlator_t *xl = NULL;
    char *value = NULL;

    if (!graph || !volinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    xl = volgen_graph_add(graph, "features/quota", volinfo->volname);
    if (!xl)
        goto out;

    ret = xlator_set_fixed_option(xl, "volume-uuid", volinfo->volname);
    if (ret)
        goto out;

    ret = glusterd_volinfo_get(volinfo, VKEY_FEATURES_QUOTA, &value);
    if (value) {
        ret = xlator_set_fixed_option(xl, "server-quota", value);
        if (ret)
            goto out;
    }
out:
    return ret;
}

static int
brick_graph_add_ro(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                   dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    int ret = -1;
    xlator_t *xl = NULL;

    if (!graph || !volinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    if (dict_get_str_boolean(set_dict, "features.read-only", 0) &&
        (dict_get_str_boolean(set_dict, "features.worm", 0) ||
         dict_get_str_boolean(set_dict, "features.worm-file-level", 0))) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "read-only and worm cannot be set together");
        ret = -1;
        goto out;
    }

    xl = volgen_graph_add(graph, "features/read-only", volinfo->volname);
    if (!xl)
        return -1;
    ret = xlator_set_fixed_option(xl, "read-only", "off");
    if (ret)
        return -1;

    ret = 0;

out:
    return ret;
}

static int
brick_graph_add_worm(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                     dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    int ret = -1;
    xlator_t *xl = NULL;

    if (!graph || !volinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    if (dict_get_str_boolean(set_dict, "features.read-only", 0) &&
        (dict_get_str_boolean(set_dict, "features.worm", 0) ||
         dict_get_str_boolean(set_dict, "features.worm-file-level", 0))) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_INCOMPATIBLE_VALUE,
               "read-only and worm cannot be set together");
        ret = -1;
        goto out;
    }

    xl = volgen_graph_add(graph, "features/worm", volinfo->volname);
    if (!xl)
        return -1;

    ret = 0;

out:
    return ret;
}

static int
brick_graph_add_cdc(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                    dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    int ret = -1;
    xlator_t *xl = NULL;

    if (!graph || !volinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    /* Check for compress volume option, and add it to the graph on
     * server side */
    ret = dict_get_str_boolean(set_dict, "network.compression", 0);
    if (ret == -1)
        goto out;
    if (ret) {
        xl = volgen_graph_add(graph, "features/cdc", volinfo->volname);
        if (!xl) {
            ret = -1;
            goto out;
        }
        ret = xlator_set_fixed_option(xl, "mode", "server");
        if (ret)
            goto out;
    }
out:
    return ret;
}

static int
brick_graph_add_io_stats(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                         dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    int ret = -1;
    xlator_t *xl = NULL;
    glusterd_conf_t *priv = THIS->private;

    if (!graph || !set_dict || !brickinfo) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    xl = volgen_graph_add_as(graph, "debug/io-stats", brickinfo->path);
    if (!xl)
        goto out;

    ret = xlator_set_fixed_option(xl, "unique-id", brickinfo->path);
    if (ret)
        goto out;

    if (priv->op_version >= GD_OP_VERSION_7_1) {
        ret = xlator_set_fixed_option(xl, "volume-id",
                                      uuid_utoa(volinfo->volume_id));
        if (ret)
            goto out;
    }

    ret = 0;
out:
    return ret;
}

static int
brick_graph_add_upcall(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                       dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    int ret = -1;

    if (!graph || !volinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    xl = volgen_graph_add(graph, "features/upcall", volinfo->volname);
    if (!xl) {
        gf_msg("glusterd", GF_LOG_WARNING, 0, GD_MSG_GRAPH_FEATURE_ADD_FAIL,
               "failed to add features/upcall to graph");
        goto out;
    }

    ret = 0;
out:
    return ret;
}

static int
brick_graph_add_leases(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                       dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    xlator_t *xl = NULL;
    int ret = -1;

    if (!graph || !volinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    xl = volgen_graph_add(graph, "features/leases", volinfo->volname);
    if (!xl) {
        gf_msg("glusterd", GF_LOG_WARNING, 0, GD_MSG_GRAPH_FEATURE_ADD_FAIL,
               "failed to add features/leases to graph");
        goto out;
    }

    ret = 0;
out:
    return ret;
}

static int
brick_graph_add_server(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                       dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    int ret = -1;
    xlator_t *xl = NULL;
    char transt[16] = {
        0,
    };
    char *username = NULL;
    char *password = NULL;
    char key[1024] = {0};
    char *ssl_user = NULL;
    char *volname = NULL;
    char *address_family_data = NULL;
    int32_t len = 0;

    if (!graph || !volinfo || !set_dict || !brickinfo) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    get_vol_transport_type(volinfo, transt);

    username = glusterd_auth_get_username(volinfo);
    password = glusterd_auth_get_password(volinfo);

    xl = volgen_graph_add(graph, "protocol/server", volinfo->volname);
    if (!xl)
        goto out;

    ret = xlator_set_fixed_option(xl, "transport-type", transt);
    if (ret)
        goto out;

    /*In the case of running multiple glusterds on a single machine,
     * we should ensure that bricks don't listen on all IPs on that
     * machine and break the IP based separation being brought about.*/
    if (dict_get_sizen(THIS->options, "transport.socket.bind-address")) {
        ret = xlator_set_fixed_option(xl, "transport.socket.bind-address",
                                      brickinfo->hostname);
        if (ret)
            return -1;
    }

    RPC_SET_OPT(xl, SSL_OWN_CERT_OPT, "ssl-own-cert", return -1);
    RPC_SET_OPT(xl, SSL_PRIVATE_KEY_OPT, "ssl-private-key", return -1);
    RPC_SET_OPT(xl, SSL_CA_LIST_OPT, "ssl-ca-list", return -1);
    RPC_SET_OPT(xl, SSL_CRL_PATH_OPT, "ssl-crl-path", return -1);
    RPC_SET_OPT(xl, SSL_CERT_DEPTH_OPT, "ssl-cert-depth", return -1);
    RPC_SET_OPT(xl, SSL_CIPHER_LIST_OPT, "ssl-cipher-list", return -1);
    RPC_SET_OPT(xl, SSL_DH_PARAM_OPT, "ssl-dh-param", return -1);
    RPC_SET_OPT(xl, SSL_EC_CURVE_OPT, "ssl-ec-curve", return -1);

    if (dict_get_str_sizen(volinfo->dict, "transport.address-family",
                           &address_family_data) == 0) {
        ret = xlator_set_fixed_option(xl, "transport.address-family",
                                      address_family_data);
        if (ret) {
            gf_log("glusterd", GF_LOG_WARNING,
                   "failed to set transport.address-family");
            return -1;
        }
    }

    if (username) {
        len = snprintf(key, sizeof(key), "auth.login.%s.allow",
                       brickinfo->path);
        if ((len < 0) || (len >= sizeof(key))) {
            return -1;
        }

        ret = xlator_set_option(xl, key, len, username);
        if (ret)
            return -1;
    }

    if (password) {
        len = snprintf(key, sizeof(key), "auth.login.%s.password", username);
        if ((len < 0) || (len >= sizeof(key))) {
            return -1;
        }
        ret = xlator_set_option(xl, key, len, password);
        if (ret)
            return -1;
    }

    ret = xlator_set_fixed_option(xl, "auth-path", brickinfo->path);
    if (ret)
        return -1;

    volname = volinfo->is_snap_volume ? volinfo->parent_volname
                                      : volinfo->volname;

    if (volname && !strcmp(volname, GLUSTER_SHARED_STORAGE)) {
        ret = xlator_set_fixed_option(xl, "strict-auth-accept", "true");
        if (ret)
            return -1;
    }

    if (dict_get_str_sizen(volinfo->dict, "auth.ssl-allow", &ssl_user) == 0) {
        len = snprintf(key, sizeof(key), "auth.login.%s.ssl-allow",
                       brickinfo->path);
        if ((len < 0) || (len >= sizeof(key))) {
            return -1;
        }

        ret = xlator_set_option(xl, key, len, ssl_user);
        if (ret)
            return -1;
    }

out:
    return ret;
}

static int
brick_graph_add_pump(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                     dict_t *set_dict, glusterd_brickinfo_t *brickinfo)
{
    int ret = -1;
    int pump = 0;
    xlator_t *xl = NULL;
    xlator_t *txl = NULL;
    xlator_t *rbxl = NULL;
    char *username = NULL;
    char *password = NULL;
    char *ptranst = NULL;
    char *address_family_data = NULL;

    if (!graph || !volinfo || !set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    ret = dict_get_int32(volinfo->dict, "enable-pump", &pump);
    if (ret == -ENOENT) {
        gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=enable-pump", NULL);
        ret = pump = 0;
    }
    if (ret)
        return -1;

    username = glusterd_auth_get_username(volinfo);
    password = glusterd_auth_get_password(volinfo);

    if (pump) {
        txl = first_of(graph);

        rbxl = volgen_graph_add_nolink(graph, "protocol/client",
                                       "%s-replace-brick", volinfo->volname);
        if (!rbxl)
            return -1;

        ptranst = glusterd_get_trans_type_rb(volinfo->transport_type);
        if (NULL == ptranst)
            return -1;

        RPC_SET_OPT(rbxl, SSL_OWN_CERT_OPT, "ssl-own-cert", return -1);
        RPC_SET_OPT(rbxl, SSL_PRIVATE_KEY_OPT, "ssl-private-key", return -1);
        RPC_SET_OPT(rbxl, SSL_CA_LIST_OPT, "ssl-ca-list", return -1);
        RPC_SET_OPT(rbxl, SSL_CRL_PATH_OPT, "ssl-crl-path", return -1);
        RPC_SET_OPT(rbxl, SSL_CERT_DEPTH_OPT, "ssl-cert-depth", return -1);
        RPC_SET_OPT(rbxl, SSL_CIPHER_LIST_OPT, "ssl-cipher-list", return -1);
        RPC_SET_OPT(rbxl, SSL_DH_PARAM_OPT, "ssl-dh-param", return -1);
        RPC_SET_OPT(rbxl, SSL_EC_CURVE_OPT, "ssl-ec-curve", return -1);

        if (username) {
            ret = xlator_set_fixed_option(rbxl, "username", username);
            if (ret)
                return -1;
        }

        if (password) {
            ret = xlator_set_fixed_option(rbxl, "password", password);
            if (ret)
                return -1;
        }

        ret = xlator_set_fixed_option(rbxl, "transport-type", ptranst);
        GF_FREE(ptranst);
        if (ret)
            return -1;

        if (dict_get_str_sizen(volinfo->dict, "transport.address-family",
                               &address_family_data) == 0) {
            ret = xlator_set_fixed_option(rbxl, "transport.address-family",
                                          address_family_data);
            if (ret) {
                gf_log("glusterd", GF_LOG_WARNING,
                       "failed to set transport.address-family");
                return -1;
            }
        }

        xl = volgen_graph_add_nolink(graph, "cluster/pump", "%s-pump",
                                     volinfo->volname);
        if (!xl)
            return -1;
        ret = volgen_xlator_link(xl, txl);
        if (ret)
            return -1;
        ret = volgen_xlator_link(xl, rbxl);
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
    {brick_graph_add_io_stats, "io-stats"},
    {brick_graph_add_sdfs, "sdfs"},
    {brick_graph_add_namespace, "namespace"},
    {brick_graph_add_cdc, "cdc"},
    {brick_graph_add_quota, "quota"},
    {brick_graph_add_index, "index"},
    {brick_graph_add_barrier, "barrier"},
    {brick_graph_add_marker, "marker"},
    {brick_graph_add_selinux, "selinux"},
    {brick_graph_add_iot, "io-threads"},
    {brick_graph_add_upcall, "upcall"},
    {brick_graph_add_leases, "leases"},
    {brick_graph_add_pump, "pump"},
    {brick_graph_add_ro, "read-only"},
    {brick_graph_add_worm, "worm"},
    {brick_graph_add_locks, "locks"},
    {brick_graph_add_acl, "access-control"},
    {brick_graph_add_bitrot_stub, "bitrot-stub"},
    {brick_graph_add_changelog, "changelog"},
    {brick_graph_add_trash, "trash"},
    {brick_graph_add_arbiter, "arbiter"},
    {brick_graph_add_posix, "posix"},
};

static glusterd_server_xlator_t
get_server_xlator(char *xlator)
{
    int i = 0;
    int size = sizeof(server_graph_table) / sizeof(server_graph_table[0]);

    for (i = 0; i < size; i++) {
        if (!server_graph_table[i].dbg_key)
            continue;
        if (strcmp(xlator, server_graph_table[i].dbg_key))
            return GF_XLATOR_SERVER;
    }

    return GF_XLATOR_NONE;
}

static glusterd_client_xlator_t
get_client_xlator(char *xlator)
{
    glusterd_client_xlator_t subvol = GF_CLNT_XLATOR_NONE;

    if (strcmp(xlator, "client") == 0)
        subvol = GF_CLNT_XLATOR_FUSE;

    return subvol;
}

static int
debugxl_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                       void *param)
{
    char *volname = NULL;
    gf_boolean_t enabled = _gf_false;

    volname = param;

    if (strcmp(vme->option, "!debug") != 0)
        return 0;

    if (!strcmp(vme->key, "debug.trace") ||
        !strcmp(vme->key, "debug.error-gen") ||
        !strcmp(vme->key, "debug.delay-gen")) {
        if (get_server_xlator(vme->value) == GF_XLATOR_NONE &&
            get_client_xlator(vme->value) == GF_CLNT_XLATOR_NONE)
            return 0;
    }

    if (gf_string2boolean(vme->value, &enabled) == -1)
        goto add_graph;
    if (!enabled)
        return 0;

add_graph:
    if (strcmp(vme->value, "off") == 0)
        return 0;
    if (volgen_graph_add(graph, vme->voltype, volname))
        return 0;
    else
        return -1;
}

int
check_and_add_debug_xl(volgen_graph_t *graph, dict_t *set_dict, char *volname,
                       char *xlname)
{
    int i = 0;
    int ret = 0;
    char *value_str = NULL;
    static char *xls[] = {"debug.trace", "debug.error-gen", "debug.delay-gen",
                          NULL};

    if (!xlname)
        goto out;

    while (xls[i]) {
        ret = dict_get_str(set_dict, xls[i], &value_str);
        if (!ret) {
            if (strcmp(xlname, value_str) == 0) {
                ret = volgen_graph_set_options_generic(graph, set_dict, volname,
                                                       &debugxl_option_handler);
                if (ret)
                    goto out;
            }
        }
        i++;
    }
    ret = 0;

out:
    return ret;
}

static gf_boolean_t
check_user_xlator_position(dict_t *dict, char *key, data_t *value,
                           void *prev_xlname)
{
    if (strncmp(key, "user.xlator.", SLEN("user.xlator.")) != 0) {
        return false;
    }

    if (fnmatch("user.xlator.*.*", key, 0) == 0) {
        return false;
    }

    char *value_str = data_to_str(value);
    if (!value_str) {
        return false;
    }

    if (strcmp(value_str, prev_xlname) == 0) {
        gf_log("glusterd", GF_LOG_INFO,
               "found insert position of user-xlator(%s)", key);
        return true;
    }

    return false;
}

static int
set_user_xlator_option(dict_t *set_dict, char *key, data_t *value, void *data)
{
    xlator_t *xl = data;
    char *optname = strrchr(key, '.') + 1;

    gf_log("glusterd", GF_LOG_DEBUG, "set user xlator option %s = %s", key,
           value->data);

    return xlator_set_option(xl, optname, strlen(optname), data_to_str(value));
}

static int
insert_user_xlator_to_graph(dict_t *set_dict, char *key, data_t *value,
                            void *action_data)
{
    int ret = -1;

    struct check_and_add_user_xlator_t *data = action_data;

    char *xlator_name = strrchr(key, '.') + 1;  // user.xlator.<xlator_name>
    char *xlator_option_matcher = NULL;
    char *type = NULL;
    xlator_t *xl = NULL;

    // convert optkey to xlator type
    if (gf_asprintf(&type, "user/%s", xlator_name) < 0) {
        gf_log("glusterd", GF_LOG_ERROR, "failed to generate user-xlator type");
        goto out;
    }

    gf_log("glusterd", GF_LOG_INFO, "add user xlator=%s to graph", type);

    xl = volgen_graph_add(data->graph, type, data->volname);
    if (!xl) {
        goto out;
    }

    ret = gf_asprintf(&xlator_option_matcher, "user.xlator.%s.*", xlator_name);
    if (ret < 0) {
        gf_log("glusterd", GF_LOG_ERROR,
               "failed to generate user-xlator option matcher");
        goto out;
    }

    dict_foreach_fnmatch(set_dict, xlator_option_matcher,
                         set_user_xlator_option, xl);

out:
    if (type)
        GF_FREE(type);
    if (xlator_option_matcher)
        GF_FREE(xlator_option_matcher);

    return ret;
}

static int
validate_user_xlator_position(dict_t *this, char *key, data_t *value,
                              void *unused)
{
    int ret = -1;
    int i = 0;
    char *value_str = NULL;

    if (!value)
        goto out;

    value_str = data_to_str(value);
    if (!value_str)
        goto out;

    if (fnmatch("user.xlator.*.*", key, 0) == 0) {
        ret = 0;
        goto out;
    }

    int num_xlators = sizeof(server_graph_table) /
                      sizeof(server_graph_table[0]);

    for (i = 0; i < num_xlators; i++) {
        if (server_graph_table[i].dbg_key &&
            strcmp(value_str, server_graph_table[i].dbg_key) == 0) {
            ret = 0;
            goto out;
        }
    }

out:
    if (ret == -1)
        gf_log("glusterd", GF_LOG_ERROR, "invalid user xlator position %s = %s",
               key, value_str);

    return ret;
}

static int
check_and_add_user_xl(volgen_graph_t *graph, dict_t *set_dict, char *volname,
                      char *prev_xlname)
{
    if (!prev_xlname)
        goto out;

    struct check_and_add_user_xlator_t data = {.graph = graph,
                                               .volname = volname};

    if (dict_foreach_match(set_dict, check_user_xlator_position, prev_xlname,
                           insert_user_xlator_to_graph, &data) < 0) {
        return -1;
    }

out:
    return 0;
}

static int
server_graph_builder(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                     dict_t *set_dict, void *param)
{
    int ret = 0;
    char *xlator = NULL;
    char *loglevel = NULL;
    int i = 0;

    if (dict_foreach_fnmatch(set_dict, "user.xlator.*",
                             validate_user_xlator_position, NULL) < 0) {
        ret = -EINVAL;
        goto out;
    }

    i = sizeof(server_graph_table) / sizeof(server_graph_table[0]) - 1;

    while (i >= 0) {
        ret = server_graph_table[i].builder(graph, volinfo, set_dict, param);
        if (ret) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_BUILD_GRAPH_FAILED,
                   "Builing graph "
                   "failed for server graph table entry: %d",
                   i);
            goto out;
        }

        ret = check_and_add_debug_xl(graph, set_dict, volinfo->volname,
                                     server_graph_table[i].dbg_key);
        if (ret)
            goto out;

        ret = check_and_add_user_xl(graph, set_dict, volinfo->volname,
                                    server_graph_table[i].dbg_key);
        if (ret)
            goto out;

        i--;
    }

    ret = dict_get_str_sizen(set_dict, "xlator", &xlator);

    /* got a cli log level request */
    if (!ret) {
        ret = dict_get_str_sizen(set_dict, "loglevel", &loglevel);
        if (ret) {
            gf_msg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                   "could not get both"
                   " translator name and loglevel for log level request");
            goto out;
        }
    }

    ret = volgen_graph_set_options_generic(
        graph, set_dict, (xlator && loglevel) ? (void *)set_dict : volinfo,
        (xlator && loglevel) ? &server_spec_extended_option_handler
                             : &server_spec_option_handler);

out:
    return ret;
}

/* builds a graph for server role , with option overrides in mod_dict */
static int
build_server_graph(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                   dict_t *mod_dict, glusterd_brickinfo_t *brickinfo)
{
    return build_graph_generic(graph, volinfo, mod_dict, brickinfo,
                               &server_graph_builder);
}

static int
perfxl_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                      void *param)
{
    gf_boolean_t enabled = _gf_false;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_conf_t *priv = NULL;

    GF_VALIDATE_OR_GOTO("glusterd", param, out);
    volinfo = param;
    priv = THIS->private;
    GF_VALIDATE_OR_GOTO("glusterd", priv, out);

    if (strcmp(vme->option, "!perf") != 0)
        return 0;

    if (gf_string2boolean(vme->value, &enabled) == -1)
        return -1;
    if (!enabled)
        return 0;

    /* Check op-version before adding the 'open-behind' xlator in the graph
     */
    if (!strcmp(vme->key, "performance.open-behind") &&
        (vme->op_version > volinfo->client_op_version))
        return 0;

    if (priv->op_version < GD_OP_VERSION_3_12_2) {
        /* For replicate volumes do not load io-threads as it affects
         * performance
         */
        if (!strcmp(vme->key, "performance.client-io-threads") &&
            (GF_CLUSTER_TYPE_REPLICATE == volinfo->type))
            return 0;
    }

    /* if VKEY_READDIR_AHEAD is enabled and parallel readdir is
     * not enabled then load readdir-ahead here else it will be
     * loaded as a child of dht */
    if (!strcmp(vme->key, VKEY_READDIR_AHEAD) &&
        glusterd_volinfo_get_boolean(volinfo, VKEY_PARALLEL_READDIR))
        return 0;

    if (volgen_graph_add(graph, vme->voltype, volinfo->volname))
        return 0;
out:
    return -1;
}

static int
gfproxy_server_perfxl_option_handler(volgen_graph_t *graph,
                                     struct volopt_map_entry *vme, void *param)
{
    GF_ASSERT(param);

    /* write-behind is the *not* allowed for gfproxy-servers */
    if (strstr(vme->key, "write-behind")) {
        return 0;
    }

    perfxl_option_handler(graph, vme, param);

    return 0;
}

static int
gfproxy_client_perfxl_option_handler(volgen_graph_t *graph,
                                     struct volopt_map_entry *vme, void *param)
{
    GF_ASSERT(param);

    /* write-behind is the only allowed "perf" for gfproxy-clients */
    if (!strstr(vme->key, "write-behind"))
        return 0;

    perfxl_option_handler(graph, vme, param);

    return 0;
}

#ifdef BUILD_GNFS
static int
nfsperfxl_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                         void *param)
{
    char *volname = NULL;
    gf_boolean_t enabled = _gf_false;

    volname = param;

    if (strcmp(vme->option, "!nfsperf") != 0)
        return 0;

    if (gf_string2boolean(vme->value, &enabled) == -1)
        return -1;
    if (!enabled)
        return 0;

    if (volgen_graph_add(graph, vme->voltype, volname))
        return 0;
    else
        return -1;
}
#endif

#if (HAVE_LIB_XML)
int
end_sethelp_xml_doc(xmlTextWriterPtr writer)
{
    int ret = -1;

    ret = xmlTextWriterEndElement(writer);
    if (ret < 0) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_XML_TEXT_WRITE_FAIL,
               "Could not end an "
               "xmlElement");
        ret = -1;
        goto out;
    }
    ret = xmlTextWriterEndDocument(writer);
    if (ret < 0) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_XML_TEXT_WRITE_FAIL,
               "Could not end an "
               "xmlDocument");
        ret = -1;
        goto out;
    }
    ret = 0;
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

int
init_sethelp_xml_doc(xmlTextWriterPtr *writer, xmlBufferPtr *buf)
{
    int ret = -1;

    if (!writer || !buf)
        goto out;

    *buf = xmlBufferCreateSize(8192);
    if (buf == NULL) {
        gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Error creating the xml "
               "buffer");
        ret = -1;
        goto out;
    }

    xmlBufferSetAllocationScheme(*buf, XML_BUFFER_ALLOC_DOUBLEIT);

    *writer = xmlNewTextWriterMemory(*buf, 0);
    if (writer == NULL) {
        gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               " Error creating the xml "
               "writer");
        ret = -1;
        goto out;
    }

    ret = xmlTextWriterStartDocument(*writer, "1.0", "UTF-8", "yes");
    if (ret < 0) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_XML_DOC_START_FAIL,
               "Error While starting the "
               "xmlDoc");
        goto out;
    }

    ret = xmlTextWriterStartElement(*writer, (xmlChar *)"options");
    if (ret < 0) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_XML_ELE_CREATE_FAIL,
               "Could not create an "
               "xmlElement");
        ret = -1;
        goto out;
    }

    ret = 0;

out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

int
xml_add_volset_element(xmlTextWriterPtr writer, const char *name,
                       const char *def_val, const char *dscrpt)
{
    int ret = -1;

    GF_ASSERT(name);

    ret = xmlTextWriterStartElement(writer, (xmlChar *)"option");
    if (ret < 0) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_XML_ELE_CREATE_FAIL,
               "Could not create an "
               "xmlElemetnt");
        ret = -1;
        goto out;
    }

    ret = xmlTextWriterWriteFormatElement(writer, (xmlChar *)"defaultValue",
                                          "%s", def_val);
    if (ret < 0) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_XML_ELE_CREATE_FAIL,
               "Could not create an "
               "xmlElemetnt");
        ret = -1;
        goto out;
    }

    ret = xmlTextWriterWriteFormatElement(writer, (xmlChar *)"description",
                                          "%s", dscrpt);
    if (ret < 0) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_XML_ELE_CREATE_FAIL,
               "Could not create an "
               "xmlElemetnt");
        ret = -1;
        goto out;
    }

    ret = xmlTextWriterWriteFormatElement(writer, (xmlChar *)"name", "%s",
                                          name);
    if (ret < 0) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_XML_ELE_CREATE_FAIL,
               "Could not create an "
               "xmlElemetnt");
        ret = -1;
        goto out;
    }

    ret = xmlTextWriterEndElement(writer);
    if (ret < 0) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_XML_ELE_CREATE_FAIL,
               "Could not end an "
               "xmlElemetnt");
        ret = -1;
        goto out;
    }

    ret = 0;
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

#endif

int
_get_xlator_opt_key_from_vme(struct volopt_map_entry *vme, char **key)
{
    int ret = 0;

    GF_ASSERT(vme);
    GF_ASSERT(key);

    if (!strcmp(vme->key, AUTH_ALLOW_MAP_KEY))
        *key = gf_strdup(AUTH_ALLOW_OPT_KEY);
    else if (!strcmp(vme->key, AUTH_REJECT_MAP_KEY))
        *key = gf_strdup(AUTH_REJECT_OPT_KEY);
#ifdef BUILD_GNFS
    else if (!strcmp(vme->key, NFS_DISABLE_MAP_KEY))
        *key = gf_strdup(NFS_DISABLE_OPT_KEY);
#endif
    else {
        if (vme->option) {
            if (vme->option[0] == '!') {
                *key = vme->option + 1;
                if (!*key[0])
                    ret = -1;
            } else {
                *key = vme->option;
            }
        } else {
            *key = strchr(vme->key, '.');
            if (*key) {
                (*key)++;
                if (!*key[0])
                    ret = -1;
            } else {
                ret = -1;
            }
        }
    }
    if (ret)
        gf_msg("glusterd", GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "Wrong entry found in  "
               "glusterd_volopt_map entry %s",
               vme->key);
    else
        gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}

void
_free_xlator_opt_key(char *key)
{
    GF_ASSERT(key);

    if (!strcmp(key, AUTH_ALLOW_OPT_KEY) || !strcmp(key, AUTH_REJECT_OPT_KEY) ||
        !strcmp(key, NFS_DISABLE_OPT_KEY))
        GF_FREE(key);

    return;
}

static xlator_t *
volgen_graph_build_client(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                          char *hostname, char *port, char *subvol, char *xl_id,
                          char *transt, dict_t *set_dict)
{
    xlator_t *xl = NULL;
    int ret = -2;
    uint32_t client_type = GF_CLIENT_OTHER;
    char *str = NULL;
    char *ssl_str = NULL;
    gf_boolean_t ssl_bool = _gf_false;
    char *address_family_data = NULL;

    GF_ASSERT(graph);
    GF_ASSERT(subvol);
    GF_ASSERT(xl_id);
    GF_ASSERT(transt);

    xl = volgen_graph_add_nolink(graph, "protocol/client", "%s", xl_id);
    if (!xl)
        goto err;

    ret = xlator_set_fixed_option(xl, "ping-timeout", "42");
    if (ret)
        goto err;

    if (hostname) {
        ret = xlator_set_fixed_option(xl, "remote-host", hostname);
        if (ret)
            goto err;
    }

    if (port) {
        ret = xlator_set_fixed_option(xl, "remote-port", port);
        if (ret)
            goto err;
    }

    ret = xlator_set_fixed_option(xl, "remote-subvolume", subvol);
    if (ret)
        goto err;

    ret = xlator_set_fixed_option(xl, "transport-type", transt);
    if (ret)
        goto err;

    if (dict_get_str_sizen(volinfo->dict, "transport.address-family",
                           &address_family_data) == 0) {
        ret = xlator_set_fixed_option(xl, "transport.address-family",
                                      address_family_data);
        if (ret) {
            gf_log("glusterd", GF_LOG_WARNING,
                   "failed to set transport.address-family");
            goto err;
        }
    }

    ret = dict_get_uint32(set_dict, "trusted-client", &client_type);

    if (!ret && (client_type == GF_CLIENT_TRUSTED ||
                 client_type == GF_CLIENT_TRUSTED_PROXY)) {
        str = NULL;
        str = glusterd_auth_get_username(volinfo);
        if (str) {
            ret = xlator_set_fixed_option(xl, "username", str);
            if (ret)
                goto err;
        }

        str = glusterd_auth_get_password(volinfo);
        if (str) {
            ret = xlator_set_fixed_option(xl, "password", str);
            if (ret)
                goto err;
        }
    }

    if (dict_get_str_sizen(set_dict, "client.ssl", &ssl_str) == 0) {
        if (gf_string2boolean(ssl_str, &ssl_bool) == 0) {
            if (ssl_bool) {
                ret = xlator_set_fixed_option(
                    xl, "transport.socket.ssl-enabled", "true");
                if (ret) {
                    goto err;
                }
            }
        }
    }

    RPC_SET_OPT(xl, SSL_OWN_CERT_OPT, "ssl-own-cert", goto err);
    RPC_SET_OPT(xl, SSL_PRIVATE_KEY_OPT, "ssl-private-key", goto err);
    RPC_SET_OPT(xl, SSL_CA_LIST_OPT, "ssl-ca-list", goto err);
    RPC_SET_OPT(xl, SSL_CRL_PATH_OPT, "ssl-crl-path", goto err);
    RPC_SET_OPT(xl, SSL_CERT_DEPTH_OPT, "ssl-cert-depth", goto err);
    RPC_SET_OPT(xl, SSL_CIPHER_LIST_OPT, "ssl-cipher-list", goto err);
    RPC_SET_OPT(xl, SSL_DH_PARAM_OPT, "ssl-dh-param", goto err);
    RPC_SET_OPT(xl, SSL_EC_CURVE_OPT, "ssl-ec-curve", goto err);

    return xl;
err:
    return NULL;
}

static int
volgen_graph_build_clients(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                           dict_t *set_dict, void *param)
{
    int i = 0;
    int ret = -1;
    char transt[16] = {
        0,
    };
    glusterd_brickinfo_t *brick = NULL;
    glusterd_brickinfo_t *ta_brick = NULL;
    xlator_t *xl = NULL;
    int subvol_index = 0;
    int thin_arbiter_index = 0;

    if (volinfo->brick_count == 0) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLUME_INCONSISTENCY,
               "volume inconsistency: brick count is 0");
        goto out;
    }

    if ((volinfo->dist_leaf_count < volinfo->brick_count) &&
        ((volinfo->brick_count % volinfo->dist_leaf_count) != 0)) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLUME_INCONSISTENCY,
               "volume inconsistency: "
               "total number of bricks (%d) is not divisible with "
               "number of bricks per cluster (%d) in a multi-cluster "
               "setup",
               volinfo->brick_count, volinfo->dist_leaf_count);
        goto out;
    }

    get_transport_type(volinfo, set_dict, transt, _gf_false);

    if (!strcmp(transt, "tcp,rdma"))
        strcpy(transt, "tcp");

    i = 0;
    cds_list_for_each_entry(brick, &volinfo->bricks, brick_list)
    {
        /* insert ta client xlator entry.
         * eg - If subvol count is > 1, then after every two client xlator
         * entries there should be a ta client xlator entry in the volfile. ta
         * client xlator indexes are - 2, 5, 8 etc depending on the index of
         * subvol.
         */
        if (volinfo->thin_arbiter_count &&
            (i + 1) % (volinfo->replica_count + 1) == 0) {
            thin_arbiter_index = 0;
            cds_list_for_each_entry(ta_brick, &volinfo->ta_bricks, brick_list)
            {
                if (thin_arbiter_index == subvol_index) {
                    xl = volgen_graph_build_client(
                        graph, volinfo, ta_brick->hostname, NULL,
                        ta_brick->path, ta_brick->brick_id, transt, set_dict);
                    if (!xl) {
                        ret = -1;
                        goto out;
                    }
                }
                thin_arbiter_index++;
            }
            subvol_index++;
        }
        xl = volgen_graph_build_client(graph, volinfo, brick->hostname, NULL,
                                       brick->path, brick->brick_id, transt,
                                       set_dict);
        if (!xl) {
            ret = -1;
            goto out;
        }

        i++;
    }

    /* Add ta client xlator entry for last subvol
     * Above loop will miss out on making the ta client
     * xlator entry for the last subvolume in the volfile
     */
    if (volinfo->thin_arbiter_count) {
        thin_arbiter_index = 0;
        cds_list_for_each_entry(ta_brick, &volinfo->ta_bricks, brick_list)
        {
            if (thin_arbiter_index == subvol_index) {
                xl = volgen_graph_build_client(
                    graph, volinfo, ta_brick->hostname, NULL, ta_brick->path,
                    ta_brick->brick_id, transt, set_dict);
                if (!xl) {
                    ret = -1;
                    goto out;
                }
            }

            thin_arbiter_index++;
        }
    }

    if (i != volinfo->brick_count) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLUME_INCONSISTENCY,
               "volume inconsistency: actual number of bricks (%d) "
               "differs from brick count (%d)",
               i, volinfo->brick_count);

        ret = -1;
        goto out;
    }
    ret = 0;
out:
    return ret;
}

static int
volgen_link_bricks(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                   char *xl_type, char *xl_namefmt, size_t child_count,
                   size_t sub_count, size_t start_count, xlator_t *trav)
{
    int i = 0;
    int j = start_count;
    xlator_t *xl = NULL;
    char *volname = NULL;
    int ret = -1;

    if (child_count == 0)
        goto out;
    volname = volinfo->volname;

    for (;; trav = trav->prev) {
        if ((i % sub_count) == 0) {
            xl = volgen_graph_add_nolink(graph, xl_type, xl_namefmt, volname,
                                         j);
            j++;
        }

        if (!xl) {
            ret = -1;
            goto out;
        }

        if (strncmp(xl_type, "performance/readdir-ahead",
                    SLEN("performance/readdir-ahead")) == 0) {
            ret = xlator_set_fixed_option(xl, "performance.readdir-ahead",
                                          "on");
            if (ret)
                goto out;
        }

        ret = volgen_xlator_link(xl, trav);
        if (ret)
            goto out;

        i++;
        if (i == child_count)
            break;
    }

    ret = j - start_count;
out:
    return ret;
}

static int
volgen_link_bricks_from_list_tail_start(volgen_graph_t *graph,
                                        glusterd_volinfo_t *volinfo,
                                        char *xl_type, char *xl_namefmt,
                                        size_t child_count, size_t sub_count,
                                        size_t start_count)
{
    xlator_t *trav = NULL;
    size_t cnt = child_count;

    if (!cnt)
        return -1;

    for (trav = first_of(graph); --cnt; trav = trav->next)
        ;

    return volgen_link_bricks(graph, volinfo, xl_type, xl_namefmt, child_count,
                              sub_count, start_count, trav);
}

static int
volgen_link_bricks_from_list_tail(volgen_graph_t *graph,
                                  glusterd_volinfo_t *volinfo, char *xl_type,
                                  char *xl_namefmt, size_t child_count,
                                  size_t sub_count)
{
    xlator_t *trav = NULL;
    size_t cnt = child_count;

    if (!cnt)
        return -1;

    for (trav = first_of(graph); --cnt; trav = trav->next)
        ;

    return volgen_link_bricks(graph, volinfo, xl_type, xl_namefmt, child_count,
                              sub_count, 0, trav);
}

/**
 * This is the build graph function for user-serviceable snapshots.
 * Generates  snapview-client
 */
static int
volgen_graph_build_snapview_client(volgen_graph_t *graph,
                                   glusterd_volinfo_t *volinfo, char *volname,
                                   dict_t *set_dict)
{
    int ret = 0;
    xlator_t *prev_top = NULL;
    xlator_t *prot_clnt = NULL;
    xlator_t *svc = NULL;
    char transt[16] = {
        0,
    };
    char *svc_args[] = {"features/snapview-client", "%s-snapview-client"};
    char subvol[1024] = {
        0,
    };
    char xl_id[1024] = {
        0,
    };

    prev_top = (xlator_t *)(graph->graph.first);

    snprintf(subvol, sizeof(subvol), "snapd-%s", volinfo->volname);
    snprintf(xl_id, sizeof(xl_id), "%s-snapd-client", volinfo->volname);

    get_transport_type(volinfo, set_dict, transt, _gf_false);

    prot_clnt = volgen_graph_build_client(graph, volinfo, NULL, NULL, subvol,
                                          xl_id, transt, set_dict);
    if (!prot_clnt) {
        ret = -1;
        goto out;
    }

    svc = volgen_graph_add_nolink(graph, svc_args[0], svc_args[1], volname);
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
    ret = volgen_xlator_link(graph->graph.first, prev_top);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_XLATOR_LINK_FAIL,
               "failed to link the "
               "snapview-client to distribute");
        goto out;
    }

    ret = volgen_xlator_link(graph->graph.first, prot_clnt);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_XLATOR_LINK_FAIL,
               "failed to link the "
               "snapview-client to snapview-server");
        goto out;
    }

out:
    return ret;
}

gf_boolean_t
_xl_is_client_decommissioned(xlator_t *xl, glusterd_volinfo_t *volinfo)
{
    int ret = 0;
    gf_boolean_t decommissioned = _gf_false;
    char *hostname = NULL;
    char *path = NULL;

    GF_ASSERT(!strcmp(xl->type, "protocol/client"));
    ret = xlator_get_fixed_option(xl, "remote-host", &hostname);
    if (ret) {
        GF_ASSERT(0);
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_REMOTE_HOST_GET_FAIL,
               "Failed to get remote-host "
               "from client %s",
               xl->name);
        goto out;
    }
    ret = xlator_get_fixed_option(xl, "remote-subvolume", &path);
    if (ret) {
        GF_ASSERT(0);
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_REMOTE_HOST_GET_FAIL,
               "Failed to get remote-host "
               "from client %s",
               xl->name);
        goto out;
    }

    decommissioned = glusterd_is_brick_decommissioned(volinfo, hostname, path);
out:
    return decommissioned;
}

gf_boolean_t
_xl_has_decommissioned_clients(xlator_t *xl, glusterd_volinfo_t *volinfo)
{
    xlator_list_t *xl_child = NULL;
    gf_boolean_t decommissioned = _gf_false;
    xlator_t *cxl = NULL;

    if (!xl)
        goto out;

    if (!strcmp(xl->type, "protocol/client")) {
        decommissioned = _xl_is_client_decommissioned(xl, volinfo);
        goto out;
    }

    xl_child = xl->children;
    while (xl_child) {
        cxl = xl_child->xlator;
        decommissioned = _xl_has_decommissioned_clients(cxl, volinfo);
        if (decommissioned)
            break;

        xl_child = xl_child->next;
    }
out:
    return decommissioned;
}

static int
_graph_get_decommissioned_children(xlator_t *dht, glusterd_volinfo_t *volinfo,
                                   char **children)
{
    int ret = -1;
    xlator_list_t *xl_child = NULL;
    xlator_t *cxl = NULL;
    gf_boolean_t comma = _gf_false;

    *children = NULL;
    xl_child = dht->children;
    while (xl_child) {
        cxl = xl_child->xlator;
        if (_xl_has_decommissioned_clients(cxl, volinfo)) {
            if (!*children) {
                *children = GF_CALLOC(16 * GF_UNIT_KB, 1, gf_common_mt_char);
                if (!*children)
                    goto out;
            }

            if (comma)
                strcat(*children, ",");
            strcat(*children, cxl->name);
            comma = _gf_true;
        }

        xl_child = xl_child->next;
    }
    ret = 0;
out:
    return ret;
}

static int
volgen_graph_build_readdir_ahead(volgen_graph_t *graph,
                                 glusterd_volinfo_t *volinfo,
                                 size_t child_count)
{
    int32_t clusters = 0;

    if (graph->type == GF_QUOTAD || graph->type == GF_SNAPD ||
        !glusterd_volinfo_get_boolean(volinfo, VKEY_PARALLEL_READDIR))
        goto out;

    clusters = volgen_link_bricks_from_list_tail(
        graph, volinfo, "performance/readdir-ahead", "%s-readdir-ahead-%d",
        child_count, 1);

out:
    return clusters;
}

static int
volgen_graph_build_dht_cluster(volgen_graph_t *graph,
                               glusterd_volinfo_t *volinfo, size_t child_count,
                               gf_boolean_t is_quotad)
{
    int32_t clusters = 0;
    int ret = -1;
    char *decommissioned_children = NULL;
    xlator_t *dht = NULL;
    char *voltype = "cluster/distribute";
    char *name_fmt = NULL;

    /* NUFA and Switch section */
    if (dict_get_str_boolean(volinfo->dict, "cluster.nufa", 0) &&
        dict_get_str_boolean(volinfo->dict, "cluster.switch", 0)) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "nufa and switch cannot be set together");
        ret = -1;
        goto out;
    }

    /* Check for NUFA volume option, and change the voltype */
    if (dict_get_str_boolean(volinfo->dict, "cluster.nufa", 0))
        voltype = "cluster/nufa";

    /* Check for switch volume option, and change the voltype */
    if (dict_get_str_boolean(volinfo->dict, "cluster.switch", 0))
        voltype = "cluster/switch";

    if (is_quotad)
        name_fmt = "%s";
    else
        name_fmt = "%s-dht";

    clusters = volgen_link_bricks_from_list_tail(
        graph, volinfo, voltype, name_fmt, child_count, child_count);
    if (clusters < 0)
        goto out;

    dht = first_of(graph);
    ret = _graph_get_decommissioned_children(dht, volinfo,
                                             &decommissioned_children);
    if (ret)
        goto out;
    if (decommissioned_children) {
        ret = xlator_set_fixed_option(dht, "decommissioned-bricks",
                                      decommissioned_children);
        if (ret)
            goto out;
    }
    ret = 0;
out:
    GF_FREE(decommissioned_children);
    return ret;
}

static int
volgen_graph_build_ec_clusters(volgen_graph_t *graph,
                               glusterd_volinfo_t *volinfo)
{
    int i = 0;
    int ret = 0;
    int clusters = 0;
    char *disperse_args[] = {"cluster/disperse", "%s-disperse-%d"};
    xlator_t *ec = NULL;
    char option[32] = {0};
    int start_count = 0;

    clusters = volgen_link_bricks_from_list_tail_start(
        graph, volinfo, disperse_args[0], disperse_args[1],
        volinfo->brick_count, volinfo->disperse_count, start_count);
    if (clusters < 0)
        goto out;

    sprintf(option, "%d", volinfo->redundancy_count);
    ec = first_of(graph);
    for (i = 0; i < clusters; i++) {
        ret = xlator_set_fixed_option(ec, "redundancy", option);
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
set_afr_pending_xattrs_option(volgen_graph_t *graph,
                              glusterd_volinfo_t *volinfo, int clusters)
{
    xlator_t *xlator = NULL;
    xlator_t **afr_xlators_list = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    glusterd_brickinfo_t *brick = NULL;
    glusterd_brickinfo_t *ta_brick = NULL;
    char *ptr = NULL;
    int i = 0;
    int index = -1;
    int ret = 0;
    char *afr_xattrs_list = NULL;
    int list_size = -1;
    int ta_brick_index = 0;
    int subvol_index = 0;

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    if (conf->op_version < GD_OP_VERSION_3_9_0)
        return ret;

    /* (brick_id x rep.count) + (rep.count-1 commas) + NULL*/
    list_size = (1024 * volinfo->replica_count) + (volinfo->replica_count - 1) +
                1;
    afr_xattrs_list = GF_CALLOC(1, list_size, gf_common_mt_char);
    if (!afr_xattrs_list)
        goto out;

    ptr = afr_xattrs_list;
    afr_xlators_list = GF_CALLOC(clusters, sizeof(xlator_t *),
                                 gf_common_mt_xlator_t);
    if (!afr_xlators_list)
        goto out;

    xlator = first_of(graph);

    for (i = 0, index = clusters - 1; i < clusters; i++) {
        afr_xlators_list[index--] = xlator;
        xlator = xlator->next;
    }

    i = 1;
    index = 0;

    cds_list_for_each_entry(brick, &volinfo->bricks, brick_list)
    {
        if (index == clusters)
            break;
        strncat(ptr, brick->brick_id, strlen(brick->brick_id));
        if (i == volinfo->replica_count) {
            /* add ta client xlator in afr-pending-xattrs before making entries
             * for client xlators in volfile.
             * ta client xlator indexes are - 2, 5, 8 depending on the index of
             * subvol. e.g- For first subvol ta client xlator id is volname-ta-2
             * For pending-xattr, ta name would be
             * 'volname-ta-2.{{volume-uuid}}' from GD_OP_VERSION_7_3.
             */
            ta_brick_index = 0;
            if (volinfo->thin_arbiter_count == 1) {
                ptr[strlen(brick->brick_id)] = ',';
                cds_list_for_each_entry(ta_brick, &volinfo->ta_bricks,
                                        brick_list)
                {
                    if (ta_brick_index == subvol_index) {
                        break;
                    }
                    ta_brick_index++;
                }
                if (conf->op_version < GD_OP_VERSION_7_3) {
                    strncat(ptr, ta_brick->brick_id,
                            strlen(ta_brick->brick_id));
                } else {
                    char ta_volname[PATH_MAX] = "";
                    int len = snprintf(ta_volname, PATH_MAX, "%s.%s",
                                       ta_brick->brick_id,
                                       uuid_utoa(volinfo->volume_id));
                    strncat(ptr, ta_volname, len);
                }
            }

            ret = xlator_set_fixed_option(afr_xlators_list[index++],
                                          "afr-pending-xattr", afr_xattrs_list);
            if (ret)
                goto out;
            memset(afr_xattrs_list, 0, list_size);
            ptr = afr_xattrs_list;
            i = 1;
            subvol_index++;
            continue;
        }
        ptr[strlen(brick->brick_id)] = ',';
        ptr += strlen(brick->brick_id) + 1;
        i++;
    }

out:
    GF_FREE(afr_xattrs_list);
    GF_FREE(afr_xlators_list);
    return ret;
}

static int
set_volfile_id_option(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                      int clusters)
{
    xlator_t *xlator = NULL;
    int i = 0;
    int ret = -1;
    glusterd_conf_t *conf = NULL;
    xlator_t *this = THIS;

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    if (conf->op_version < GD_OP_VERSION_9_0)
        return 0;
    xlator = first_of(graph);

    for (i = 0; i < clusters; i++) {
        ret = xlator_set_fixed_option(xlator, "volume-id",
                                      uuid_utoa(volinfo->volume_id));
        if (ret)
            goto out;

        xlator = xlator->next;
    }

out:
    return ret;
}

static int
volgen_graph_build_afr_clusters(volgen_graph_t *graph,
                                glusterd_volinfo_t *volinfo)
{
    int i = 0;
    int ret = 0;
    int clusters = 0;
    char *replicate_type = "cluster/replicate";
    char *replicate_name = "%s-replicate-%d";
    xlator_t *afr = NULL;
    char option[32] = {0};
    glusterd_brickinfo_t *ta_brick = NULL;
    int ta_brick_index = 0;
    int ta_replica_offset = 0;
    int ta_brick_offset = 0;
    char ta_option[4096] = {
        0,
    };

    /* In thin-arbiter case brick count and replica count remain same
     * but due to additional entries of ta client xlators in the volfile,
     * GD1 is manipulated to include these client xlators while linking them to
     * afr/cluster entry in the volfile.
     */
    if (volinfo->thin_arbiter_count == 1) {
        ta_replica_offset = 1;
        ta_brick_offset = volinfo->subvol_count;
    }

    clusters = volgen_link_bricks_from_list_tail(
        graph, volinfo, replicate_type, replicate_name,
        volinfo->brick_count + ta_brick_offset,
        volinfo->replica_count + ta_replica_offset);

    if (clusters < 0)
        goto out;

    ret = set_afr_pending_xattrs_option(graph, volinfo, clusters);
    if (ret) {
        clusters = -1;
        goto out;
    }

    ret = set_volfile_id_option(graph, volinfo, clusters);
    if (ret) {
        clusters = -1;
        goto out;
    }

    if (!volinfo->arbiter_count && !volinfo->thin_arbiter_count)
        goto out;

    afr = first_of(graph);

    if (volinfo->arbiter_count) {
        sprintf(option, "%d", volinfo->arbiter_count);
        for (i = 0; i < clusters; i++) {
            ret = xlator_set_fixed_option(afr, "arbiter-count", option);
            if (ret) {
                clusters = -1;
                goto out;
            }

            afr = afr->next;
        }
    }

    if (volinfo->thin_arbiter_count == 1) {
        for (i = 0; i < clusters; i++) {
            ta_brick_index = 0;
            cds_list_for_each_entry(ta_brick, &volinfo->ta_bricks, brick_list)
            {
                if (ta_brick_index == i) {
                    break;
                }
                ta_brick_index++;
            }
            snprintf(ta_option, sizeof(ta_option), "%s:%s", ta_brick->hostname,
                     ta_brick->path);
            ret = xlator_set_fixed_option(afr, "thin-arbiter", ta_option);
            if (ret) {
                clusters = -1;
                goto out;
            }
            afr = afr->next;
        }
    }
out:
    return clusters;
}

static int
volume_volgen_graph_build_clusters(volgen_graph_t *graph,
                                   glusterd_volinfo_t *volinfo,
                                   gf_boolean_t is_quotad)
{
    int clusters = 0;
    int dist_count = 0;
    int ret = -1;

    if (!volinfo->dist_leaf_count)
        goto out;

    if (volinfo->dist_leaf_count == 1)
        goto build_distribute;

    /* All other cases, it will have one or the other cluster type */
    switch (volinfo->type) {
        case GF_CLUSTER_TYPE_REPLICATE:
            clusters = volgen_graph_build_afr_clusters(graph, volinfo);
            if (clusters < 0)
                goto out;
            break;
        case GF_CLUSTER_TYPE_DISPERSE:
            clusters = volgen_graph_build_ec_clusters(graph, volinfo);
            if (clusters < 0)
                goto out;

            break;
        default:
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLUME_INCONSISTENCY,
                   "volume inconsistency: "
                   "unrecognized clustering type");
            goto out;
    }

build_distribute:
    dist_count = volinfo->brick_count / volinfo->dist_leaf_count;
    if (!dist_count) {
        ret = -1;
        goto out;
    }
    clusters = volgen_graph_build_readdir_ahead(graph, volinfo, dist_count);
    if (clusters < 0)
        goto out;

    ret = volgen_graph_build_dht_cluster(graph, volinfo, dist_count, is_quotad);
    if (ret)
        goto out;

    ret = 0;
out:
    return ret;
}

static int
client_graph_set_rda_options(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                             dict_t *set_dict)
{
    char *rda_cache_s = NULL;
    int32_t ret = 0;
    uint64_t rda_cache_size = 0;
    char *rda_req_s = NULL;
    uint64_t rda_req_size = 0;
    uint64_t new_cache_size = 0;
    char new_cache_size_str[50] = {
        0,
    };
    char new_req_size_str[50] = {
        0,
    };
    int dist_count = 0;

    dist_count = volinfo->brick_count / volinfo->dist_leaf_count;
    if (dist_count <= 1)
        goto out;

    if (graph->type == GF_QUOTAD || graph->type == GF_SNAPD ||
        !glusterd_volinfo_get_boolean(volinfo, VKEY_PARALLEL_READDIR) ||
        !glusterd_volinfo_get_boolean(volinfo, VKEY_READDIR_AHEAD))
        goto out;

    /* glusterd_volinfo_get() will get the default value if nothing set
     * explicitly. Hence it is important to check set_dict before checking
     * glusterd_volinfo_get, so that we consider key value of the in
     * progress volume set option.
     */
    ret = dict_get_str_sizen(set_dict, VKEY_RDA_CACHE_LIMIT, &rda_cache_s);
    if (ret < 0) {
        ret = glusterd_volinfo_get(volinfo, VKEY_RDA_CACHE_LIMIT, &rda_cache_s);
        if (ret < 0)
            goto out;
    }
    ret = gf_string2bytesize_uint64(rda_cache_s, &rda_cache_size);
    if (ret < 0) {
        set_graph_errstr(
            graph, "invalid number format in option " VKEY_RDA_CACHE_LIMIT);
        goto out;
    }

    ret = dict_get_str_sizen(set_dict, VKEY_RDA_REQUEST_SIZE, &rda_req_s);
    if (ret < 0) {
        ret = glusterd_volinfo_get(volinfo, VKEY_RDA_REQUEST_SIZE, &rda_req_s);
        if (ret < 0)
            goto out;
    }
    ret = gf_string2bytesize_uint64(rda_req_s, &rda_req_size);
    if (ret < 0) {
        set_graph_errstr(
            graph, "invalid number format in option " VKEY_RDA_REQUEST_SIZE);
        goto out;
    }

    if (rda_cache_size == 0 || rda_req_size == 0) {
        set_graph_errstr(graph, "Value cannot be 0");
        ret = -1;
        goto out;
    }

    new_cache_size = rda_cache_size / dist_count;
    if (new_cache_size < rda_req_size) {
        if (new_cache_size < 4 * 1024)
            new_cache_size = rda_req_size = 4 * 1024;
        else
            rda_req_size = new_cache_size;

        snprintf(new_req_size_str, sizeof(new_req_size_str), "%" PRId64 "%s",
                 rda_req_size, "B");
        ret = dict_set_dynstr_with_alloc(set_dict, VKEY_RDA_REQUEST_SIZE,
                                         new_req_size_str);
        if (ret < 0)
            goto out;
    }

    snprintf(new_cache_size_str, sizeof(new_cache_size_str), "%" PRId64 "%s",
             new_cache_size, "B");
    ret = dict_set_dynstr_with_alloc(set_dict, VKEY_RDA_CACHE_LIMIT,
                                     new_cache_size_str);
    if (ret < 0)
        goto out;

out:
    return ret;
}

static int
client_graph_set_perf_options(volgen_graph_t *graph,
                              glusterd_volinfo_t *volinfo, dict_t *set_dict)
{
    int ret = 0;

    /*
     * Logic to make sure gfproxy-client gets custom performance translators
     */
    ret = dict_get_str_boolean(set_dict, "gfproxy-client", 0);
    if (ret == 1) {
        return volgen_graph_set_options_generic(
            graph, set_dict, volinfo, &gfproxy_client_perfxl_option_handler);
    }

    /*
     * Logic to make sure gfproxy-server gets custom performance translators
     */
    ret = dict_get_str_boolean(set_dict, "gfproxy-server", 0);
    if (ret == 1) {
        return volgen_graph_set_options_generic(
            graph, set_dict, volinfo, &gfproxy_server_perfxl_option_handler);
    }

    /*
     * Logic to make sure NFS doesn't have performance translators by
     * default for a volume
     */
    ret = client_graph_set_rda_options(graph, volinfo, set_dict);
    if (ret < 0)
        return ret;

#ifdef BUILD_GNFS
    data_t *tmp_data = NULL;
    char *volname = NULL;

    tmp_data = dict_get_sizen(set_dict, "nfs-volume-file");
    if (tmp_data) {
        volname = volinfo->volname;
        return volgen_graph_set_options_generic(graph, set_dict, volname,
                                                &nfsperfxl_option_handler);
    } else
#endif
        return volgen_graph_set_options_generic(graph, set_dict, volinfo,
                                                &perfxl_option_handler);
}

static int
graph_set_generic_options(xlator_t *this, volgen_graph_t *graph,
                          dict_t *set_dict, char *identifier)
{
    int ret = 0;

    ret = volgen_graph_set_options_generic(graph, set_dict, "client",
                                           &loglevel_option_handler);

    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_GRAPH_SET_OPT_FAIL,
               "changing %s log level"
               " failed",
               identifier);

    ret = volgen_graph_set_options_generic(graph, set_dict, "client",
                                           &sys_loglevel_option_handler);
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_GRAPH_SET_OPT_FAIL,
               "changing %s syslog "
               "level failed",
               identifier);

    ret = volgen_graph_set_options_generic(graph, set_dict, "client",
                                           &logger_option_handler);

    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_GRAPH_SET_OPT_FAIL,
               "changing %s logger"
               " failed",
               identifier);

    ret = volgen_graph_set_options_generic(graph, set_dict, "client",
                                           &log_format_option_handler);
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_GRAPH_SET_OPT_FAIL,
               "changing %s log format"
               " failed",
               identifier);

    ret = volgen_graph_set_options_generic(graph, set_dict, "client",
                                           &log_buf_size_option_handler);
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_GRAPH_SET_OPT_FAIL,
               "Failed to change "
               "log-buf-size option");

    ret = volgen_graph_set_options_generic(graph, set_dict, "client",
                                           &log_flush_timeout_option_handler);
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_GRAPH_SET_OPT_FAIL,
               "Failed to change "
               "log-flush-timeout option");

    ret = volgen_graph_set_options_generic(
        graph, set_dict, "client", &log_localtime_logging_option_handler);
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_GRAPH_SET_OPT_FAIL,
               "Failed to change "
               "log-localtime-logging option");

    ret = volgen_graph_set_options_generic(graph, set_dict, "client",
                                           &threads_option_handler);

    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_GRAPH_SET_OPT_FAIL,
               "changing %s threads failed", identifier);

    return 0;
}

static int
client_graph_builder(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                     dict_t *set_dict, void *param)
{
    int ret = 0;
    xlator_t *xl = NULL;
    char *volname = NULL;
    char *tmp = NULL;
    gf_boolean_t var = _gf_false;
    gf_boolean_t ob = _gf_false;
    int uss_enabled = -1;
    xlator_t *this = THIS;
    char *subvol = NULL;
    size_t namelen = 0;
    char *xl_id = NULL;
    gf_boolean_t gfproxy_clnt = _gf_false;
    glusterd_conf_t *conf = this->private;

    GF_ASSERT(conf);

    ret = dict_get_str_boolean(set_dict, "gfproxy-client", 0);
    if (ret == -1)
        goto out;

    volname = volinfo->volname;
    if (ret == 0) {
        ret = volgen_graph_build_clients(graph, volinfo, set_dict, param);
        if (ret)
            goto out;

        else
            ret = volume_volgen_graph_build_clusters(graph, volinfo, _gf_false);

        if (ret == -1)
            goto out;
    } else {
        gfproxy_clnt = _gf_true;
        namelen = strlen(volinfo->volname) + SLEN("gfproxyd-") + 1;
        subvol = alloca(namelen);
        snprintf(subvol, namelen, "gfproxyd-%s", volinfo->volname);

        namelen = strlen(volinfo->volname) + SLEN("-gfproxy-client") + 1;
        xl_id = alloca(namelen);
        snprintf(xl_id, namelen, "%s-gfproxy-client", volinfo->volname);
        volgen_graph_build_client(graph, volinfo, NULL, NULL, subvol, xl_id,
                                  "tcp", set_dict);
    }

    ret = dict_get_str_boolean(set_dict, "features.cloudsync", _gf_false);
    if (ret == -1)
        goto out;

    if (ret) {
        xl = volgen_graph_add(graph, "features/cloudsync", volname);
        if (!xl) {
            ret = -1;
            goto out;
        }
    }

    ret = dict_get_str_boolean(set_dict, "features.shard", _gf_false);
    if (ret == -1)
        goto out;

    if (ret) {
        xl = volgen_graph_add(graph, "features/shard", volname);
        if (!xl) {
            ret = -1;
            goto out;
        }
    }
    /* a. ret will be -1 if features.ctime is not set in the volinfo->dict which
     * means ctime should be loaded into the graph.
     * b. ret will be 1 if features.ctime is explicitly turned on through
     * volume set and in that case ctime should be loaded into the graph.
     * c. ret will be 0 if features.ctime is explicitly turned off and in that
     * case ctime shouldn't be loaded into the graph.
     */
    ret = dict_get_str_boolean(set_dict, "features.ctime", -1);
    if (conf->op_version >= GD_OP_VERSION_5_0 && ret) {
        xl = volgen_graph_add(graph, "features/utime", volname);
        if (!xl) {
            ret = -1;
            goto out;
        }
    }

    /* As of now snapshot volume is read-only. Read-only xlator is loaded
     * in client graph so that AFR & DHT healing can be done in server.
     */
    if (volinfo->is_snap_volume) {
        xl = volgen_graph_add(graph, "features/read-only", volname);
        if (!xl) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GRAPH_FEATURE_ADD_FAIL,
                   "Failed to add "
                   "read-only feature to the graph of %s "
                   "snapshot with %s origin volume",
                   volname, volinfo->parent_volname);
            ret = -1;
            goto out;
        }
        ret = xlator_set_fixed_option(xl, "read-only", "on");
        if (ret)
            goto out;
    }

    /* Check for compress volume option, and add it to the graph on client side
     */
    ret = dict_get_str_boolean(set_dict, "network.compression", 0);
    if (ret == -1)
        goto out;
    if (ret) {
        xl = volgen_graph_add(graph, "features/cdc", volname);
        if (!xl) {
            ret = -1;
            goto out;
        }
        ret = xlator_set_fixed_option(xl, "mode", "client");
        if (ret)
            goto out;
    }

    /* gfproxy needs the quiesce translator */
    if (gfproxy_clnt) {
        xl = volgen_graph_add(graph, "features/quiesce", volname);
        if (!xl) {
            ret = -1;
            goto out;
        }
    }

    if (conf->op_version == GD_OP_VERSION_MIN) {
        ret = glusterd_volinfo_get_boolean(volinfo, VKEY_FEATURES_QUOTA);
        if (ret == -1)
            goto out;
        if (ret) {
            xl = volgen_graph_add(graph, "features/quota", volname);
            if (!xl) {
                ret = -1;
                goto out;
            }
        }
    }

    /* Do not allow changing read-after-open option if root-squash is
       enabled.
    */
    ret = dict_get_str_sizen(set_dict, "performance.read-after-open", &tmp);
    if (!ret) {
        ret = dict_get_str_sizen(volinfo->dict, "server.root-squash", &tmp);
        if (!ret) {
            ob = _gf_false;
            ret = gf_string2boolean(tmp, &ob);
            if (!ret && ob) {
                gf_msg(this->name, GF_LOG_WARNING, 0,
                       GD_MSG_ROOT_SQUASH_ENABLED,
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
    ret = dict_get_str_sizen(set_dict, "server.root-squash", &tmp);
    if (!ret) {
        ret = gf_string2boolean(tmp, &var);
        if (ret)
            goto out;

        if (var) {
            ret = dict_get_str_sizen(volinfo->dict,
                                     "performance.read-after-open", &tmp);
            if (!ret) {
                ret = gf_string2boolean(tmp, &ob);
                /* go ahead with turning read-after-open on
                   even if string2boolean conversion fails,
                   OR if read-after-open option is turned off
                */
                if (ret || !ob)
                    ret = dict_set_sizen_str_sizen(
                        set_dict, "performance.read-after-open", "yes");
            } else {
                ret = dict_set_sizen_str_sizen(
                    set_dict, "performance.read-after-open", "yes");
            }
        } else {
            /* When root-squash has to be turned off, open-behind's
               read-after-open option should be reset to what was
               there before root-squash was turned on. If the option
               cannot be found in volinfo's dict, it means that
               option was not set before turning on root-squash.
            */
            ob = _gf_false;
            ret = dict_get_str_sizen(volinfo->dict,
                                     "performance.read-after-open", &tmp);
            if (!ret) {
                ret = gf_string2boolean(tmp, &ob);

                if (!ret && ob) {
                    ret = dict_set_sizen_str_sizen(
                        set_dict, "performance.read-after-open", "yes");
                }
            }
            /* consider operation is failure only if read-after-open
               option is enabled and could not set into set_dict
            */
            if (!ob)
                ret = 0;
        }
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_ROOT_SQUASH_FAILED,
                   "setting "
                   "open behind option as part of root "
                   "squash failed");
            goto out;
        }
    }

    ret = dict_get_str_boolean(set_dict, "server.manage-gids", _gf_false);
    if (ret != -1) {
        ret = dict_set_str_sizen(set_dict, "client.send-gids",
                                 ret ? "false" : "true");
        if (ret)
            gf_msg(THIS->name, GF_LOG_WARNING, -ret, GD_MSG_DICT_SET_FAILED,
                   "changing client"
                   " protocol option failed");
    }

    ret = client_graph_set_perf_options(graph, volinfo, set_dict);
    if (ret)
        goto out;

    uss_enabled = dict_get_str_boolean(set_dict, "features.uss", _gf_false);
    if (uss_enabled == -1)
        goto out;
    if (uss_enabled && !volinfo->is_snap_volume) {
        ret = volgen_graph_build_snapview_client(graph, volinfo, volname,
                                                 set_dict);
        if (ret == -1)
            goto out;
    }

    /* add debug translators depending on the options */
    ret = check_and_add_debug_xl(graph, set_dict, volname, "client");
    if (ret)
        return -1;

    /* if the client is part of 'gfproxyd' server, then we need to keep the
       volume name as 'gfproxyd-<volname>', for better portmapper options */
    subvol = volname;
    ret = dict_get_str_boolean(set_dict, "gfproxy-server", 0);
    if (ret > 0) {
        namelen = strlen(volinfo->volname) + SLEN("gfproxyd-") + 1;
        subvol = alloca(namelen);
        snprintf(subvol, namelen, "gfproxyd-%s", volname);
    }

    ret = -1;
    xl = volgen_graph_add_as(graph, "debug/io-stats", subvol);
    if (!xl) {
        goto out;
    }

    ret = graph_set_generic_options(this, graph, set_dict, "client");
out:
    return ret;
}

/* builds a graph for client role , with option overrides in mod_dict */
static int
build_client_graph(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                   dict_t *mod_dict)
{
    return build_graph_generic(graph, volinfo, mod_dict, NULL,
                               &client_graph_builder);
}

char *gd_shd_options[] = {"!self-heal-daemon", "!heal-timeout", NULL};

char *
gd_get_matching_option(char **options, char *option)
{
    while (*options && strcmp(*options, option))
        options++;
    return *options;
}

static int
bitrot_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                      void *param)
{
    xlator_t *xl = NULL;
    int ret = 0;

    xl = first_of(graph);

    if (!strcmp(vme->option, "expiry-time")) {
        ret = xlator_set_fixed_option(xl, "expiry-time", vme->value);
        if (ret)
            return -1;
    }

    if (!strcmp(vme->option, "signer-threads")) {
        ret = xlator_set_fixed_option(xl, "signer-threads", vme->value);
        if (ret)
            return -1;
    }

    return ret;
}

static int
scrubber_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                        void *param)
{
    xlator_t *xl = NULL;
    int ret = 0;

    xl = first_of(graph);

    if (!strcmp(vme->option, "scrub-throttle")) {
        ret = xlator_set_fixed_option(xl, "scrub-throttle", vme->value);
        if (ret)
            return -1;
    }

    if (!strcmp(vme->option, "scrub-frequency")) {
        ret = xlator_set_fixed_option(xl, "scrub-freq", vme->value);
        if (ret)
            return -1;
    }

    if (!strcmp(vme->option, "scrubber")) {
        if (!strcmp(vme->value, "pause")) {
            ret = xlator_set_fixed_option(xl, "scrub-state", vme->value);
            if (ret)
                return -1;
        }
    }

    return ret;
}

static int
shd_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                   void *param)
{
    int ret = 0;
    struct volopt_map_entry new_vme = {0};
    char *shd_option = NULL;

    shd_option = gd_get_matching_option(gd_shd_options, vme->option);
    if ((vme->option[0] == '!') && !shd_option)
        goto out;
    new_vme = *vme;
    if (shd_option) {
        new_vme.option = shd_option + 1;  // option with out '!'
    }

    ret = no_filter_option_handler(graph, &new_vme, param);
out:
    return ret;
}

#ifdef BUILD_GNFS
static int
nfs_option_handler(volgen_graph_t *graph, struct volopt_map_entry *vme,
                   void *param)
{
    static struct nfs_opt nfs_opts[] = {
        /* {pattern, printf_pattern} */
        {"!rpc-auth.addr.*.allow", "rpc-auth.addr.%s.allow"},
        {"!rpc-auth.addr.*.reject", "rpc-auth.addr.%s.reject"},
        {"!rpc-auth.auth-unix.*", "rpc-auth.auth-unix.%s"},
        {"!rpc-auth.auth-null.*", "rpc-auth.auth-null.%s"},
        {"!nfs3.*.trusted-sync", "nfs3.%s.trusted-sync"},
        {"!nfs3.*.trusted-write", "nfs3.%s.trusted-write"},
        {"!nfs3.*.volume-access", "nfs3.%s.volume-access"},
        {"!rpc-auth.ports.*.insecure", "rpc-auth.ports.%s.insecure"},
        {"!nfs-disable", "nfs.%s.disable"},
        {NULL, NULL}};
    xlator_t *xl = NULL;
    char *aa = NULL;
    int ret = 0;
    glusterd_volinfo_t *volinfo = NULL;
    int keylen;
    struct nfs_opt *opt = NULL;

    volinfo = param;

    if (!volinfo || (volinfo->volname[0] == '\0')) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        return 0;
    }

    if (!vme || !(vme->option)) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        return 0;
    }

    xl = first_of(graph);

    for (opt = nfs_opts; opt->pattern; opt++) {
        if (!strcmp(vme->option, opt->pattern)) {
            keylen = gf_asprintf(&aa, opt->printf_pattern, volinfo->volname);

            if (keylen == -1) {
                return -1;
            }

            ret = xlator_set_option(xl, aa, keylen, vme->value);
            GF_FREE(aa);

            if (ret)
                return -1;

            goto out;
        }
    }

    if (!strcmp(vme->option, "!nfs3.*.export-dir")) {
        keylen = gf_asprintf(&aa, "nfs3.%s.export-dir", volinfo->volname);

        if (keylen == -1) {
            return -1;
        }

        ret = gf_canonicalize_path(vme->value);
        if (ret) {
            GF_FREE(aa);
            return -1;
        }
        ret = xlator_set_option(xl, aa, keylen, vme->value);
        GF_FREE(aa);

        if (ret)
            return -1;
    } else if ((strcmp(vme->voltype, "nfs/server") == 0) &&
               (vme->option[0] != '!')) {
        ret = xlator_set_option(xl, vme->option, strlen(vme->option),
                                vme->value);
        if (ret)
            return -1;
    }

out:
    return 0;
}

#endif
char *
volgen_get_shd_key(int type)
{
    char *key = NULL;

    switch (type) {
        case GF_CLUSTER_TYPE_REPLICATE:
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

static int
volgen_set_shd_key_enable(dict_t *set_dict, const int type)
{
    int ret = -1;

    switch (type) {
        case GF_CLUSTER_TYPE_REPLICATE:
            ret = dict_set_sizen_str_sizen(set_dict, "cluster.self-heal-daemon",
                                           "enable");
            break;
        case GF_CLUSTER_TYPE_DISPERSE:
            ret = dict_set_sizen_str_sizen(
                set_dict, "cluster.disperse-self-heal-daemon", "enable");
            break;
        default:
            break;
    }

    return ret;
}

static gf_boolean_t
volgen_is_shd_compatible_xl(char *xl_type)
{
    char *shd_xls[] = {"cluster/replicate", "cluster/disperse", NULL};
    if (gf_get_index_by_elem(shd_xls, xl_type) != -1)
        return _gf_true;

    return _gf_false;
}

static int
volgen_graph_set_iam_shd(volgen_graph_t *graph)
{
    xlator_t *trav;
    int ret = 0;

    for (trav = first_of(graph); trav; trav = trav->next) {
        if (!volgen_is_shd_compatible_xl(trav->type))
            continue;

        ret = xlator_set_fixed_option(trav, "iam-self-heal-daemon", "yes");
        if (ret)
            break;
    }
    return ret;
}

static int
prepare_shd_volume_options(glusterd_volinfo_t *volinfo, dict_t *mod_dict,
                           dict_t *set_dict)
{
    int ret = 0;

    ret = volgen_set_shd_key_enable(set_dict, volinfo->type);
    if (ret)
        goto out;

    ret = dict_set_uint32(set_dict, "trusted-client", GF_CLIENT_TRUSTED);
    if (ret) {
        gf_smsg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=trusted-client", NULL);
        goto out;
    }

    dict_copy(volinfo->dict, set_dict);
    if (mod_dict)
        dict_copy(mod_dict, set_dict);
out:
    return ret;
}

static int
build_afr_ec_clusters(volgen_graph_t *graph, glusterd_volinfo_t *volinfo)
{
    int clusters = -1;
    switch (volinfo->type) {
        case GF_CLUSTER_TYPE_REPLICATE:
            clusters = volgen_graph_build_afr_clusters(graph, volinfo);
            break;

        case GF_CLUSTER_TYPE_DISPERSE:
            clusters = volgen_graph_build_ec_clusters(graph, volinfo);
            break;
    }
    return clusters;
}

static int
build_shd_clusters(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                   dict_t *set_dict)
{
    int ret = 0;
    int clusters = -1;

    ret = volgen_graph_build_clients(graph, volinfo, set_dict, NULL);
    if (ret)
        goto out;
    clusters = build_afr_ec_clusters(graph, volinfo);

out:
    return clusters;
}

gf_boolean_t
gd_is_self_heal_enabled(glusterd_volinfo_t *volinfo, dict_t *dict)
{
    char *shd_key = NULL;
    gf_boolean_t shd_enabled = _gf_false;

    GF_VALIDATE_OR_GOTO("glusterd", volinfo, out);

    switch (volinfo->type) {
        case GF_CLUSTER_TYPE_REPLICATE:
        case GF_CLUSTER_TYPE_DISPERSE:
            shd_key = volgen_get_shd_key(volinfo->type);
            shd_enabled = dict_get_str_boolean(dict, shd_key, _gf_true);
            break;
        default:
            break;
    }
out:
    return shd_enabled;
}

int
build_rebalance_volfile(glusterd_volinfo_t *volinfo, char *filepath,
                        dict_t *mod_dict)
{
    volgen_graph_t graph = {
        0,
    };
    xlator_t *xl = NULL;
    int ret = -1;
    dict_t *set_dict = NULL;

    graph.type = GF_REBALANCED;

    if (volinfo->brick_count <= volinfo->dist_leaf_count) {
        /*
         * Volume is not a distribute volume or
         * contains only 1 brick, no need to create
         * the volfiles.
         */
        return 0;
    }

    set_dict = dict_copy_with_ref(volinfo->dict, NULL);
    if (!set_dict)
        return -1;

    if (mod_dict) {
        dict_copy(mod_dict, set_dict);
        /* XXX dict_copy swallows errors */
    }

    /* Rebalance is always a trusted client*/
    ret = dict_set_uint32(set_dict, "trusted-client", GF_CLIENT_TRUSTED);
    if (ret)
        return -1;

    ret = volgen_graph_build_clients(&graph, volinfo, set_dict, NULL);
    if (ret)
        goto out;

    ret = volume_volgen_graph_build_clusters(&graph, volinfo, _gf_false);
    if (ret)
        goto out;

    xl = volgen_graph_add_as(&graph, "debug/io-stats", volinfo->volname);
    if (!xl) {
        ret = -1;
        goto out;
    }

    ret = graph_set_generic_options(THIS, &graph, set_dict, "rebalance-daemon");
    if (ret)
        goto out;

    ret = volgen_graph_set_options_generic(&graph, set_dict, volinfo,
                                           basic_option_handler);

    if (!ret)
        ret = volgen_write_volfile(&graph, filepath);

out:
    volgen_graph_free(&graph);

    dict_unref(set_dict);

    return ret;
}

static int
build_shd_volume_graph(xlator_t *this, volgen_graph_t *graph,
                       glusterd_volinfo_t *volinfo, dict_t *mod_dict,
                       dict_t *set_dict, gf_boolean_t graph_check,
                       gf_boolean_t *valid_config)
{
    volgen_graph_t cgraph = {0};
    int ret = 0;
    int clusters = -1;

    if (!graph_check && (volinfo->status != GLUSTERD_STATUS_STARTED))
        goto out;

    if (!glusterd_is_shd_compatible_volume(volinfo))
        goto out;

    /* Shd graph is valid only when there is at least one
     * replica/disperse volume is present
     */
    *valid_config = _gf_true;

    ret = prepare_shd_volume_options(volinfo, mod_dict, set_dict);
    if (ret)
        goto out;

    clusters = build_shd_clusters(&cgraph, volinfo, set_dict);
    if (clusters < 0) {
        ret = -1;
        goto out;
    }

    ret = volgen_graph_set_options_generic(&cgraph, set_dict, volinfo,
                                           shd_option_handler);
    if (ret)
        goto out;

    ret = volgen_graph_set_iam_shd(&cgraph);
    if (ret)
        goto out;

    ret = volgen_graph_merge_sub(graph, &cgraph, clusters);
    if (ret)
        goto out;

    ret = graph_set_generic_options(this, graph, set_dict, "self-heal daemon");
out:
    return ret;
}

int
build_shd_graph(volgen_graph_t *graph, dict_t *mod_dict)
{
    glusterd_volinfo_t *voliter = NULL;
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;
    dict_t *set_dict = NULL;
    int ret = 0;
    gf_boolean_t valid_config = _gf_false;
    xlator_t *iostxl = NULL;
    gf_boolean_t graph_check = _gf_false;

    this = THIS;
    priv = this->private;

    set_dict = dict_new();
    if (!set_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        ret = -ENOMEM;
        goto out;
    }

    if (mod_dict)
        graph_check = dict_get_str_boolean(mod_dict, "graph-check", 0);
    iostxl = volgen_graph_add_as(graph, "debug/io-stats", "glustershd");
    if (!iostxl) {
        ret = -1;
        goto out;
    }

    cds_list_for_each_entry(voliter, &priv->volumes, vol_list)
    {
        ret = build_shd_volume_graph(this, graph, voliter, mod_dict, set_dict,
                                     graph_check, &valid_config);
        ret = dict_reset(set_dict);
        if (ret)
            goto out;
    }

out:
    if (set_dict)
        dict_unref(set_dict);
    if (!valid_config)
        ret = -EINVAL;
    return ret;
}

#ifdef BUILD_GNFS

static int
volgen_graph_set_iam_nfsd(const volgen_graph_t *graph)
{
    xlator_t *trav;
    int ret = 0;

    for (trav = first_of((volgen_graph_t *)graph); trav; trav = trav->next) {
        if (strcmp(trav->type, "cluster/replicate") != 0)
            continue;

        ret = xlator_set_fixed_option(trav, "iam-nfs-daemon", "yes");
        if (ret)
            break;
    }
    return ret;
}

/* builds a graph for nfs server role, with option overrides in mod_dict */
int
build_nfs_graph(volgen_graph_t *graph, dict_t *mod_dict)
{
    volgen_graph_t cgraph = {
        0,
    };
    glusterd_volinfo_t *voliter = NULL;
    glusterd_conf_t *priv = NULL;
    dict_t *set_dict = NULL;
    xlator_t *nfsxl = NULL;
    char *skey = NULL;
    int ret = 0;
    char nfs_xprt[16] = {
        0,
    };
    char *volname = NULL;
    data_t *data = NULL;

    priv = THIS->private;
    GF_ASSERT(priv);

    set_dict = dict_new();
    if (!set_dict) {
        gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Out of memory");
        return -1;
    }

    nfsxl = volgen_graph_add_as(graph, "nfs/server", "nfs-server");
    if (!nfsxl) {
        ret = -1;
        goto out;
    }
    ret = xlator_set_fixed_option(nfsxl, "nfs.dynamic-volumes", "on");
    if (ret)
        goto out;

    ret = xlator_set_fixed_option(nfsxl, "nfs.nlm", "on");
    if (ret)
        goto out;

    ret = xlator_set_fixed_option(nfsxl, "nfs.drc", "off");
    if (ret)
        goto out;

    cds_list_for_each_entry(voliter, &priv->volumes, vol_list)
    {
        if (voliter->status != GLUSTERD_STATUS_STARTED)
            continue;

        if (dict_get_str_boolean(voliter->dict, NFS_DISABLE_MAP_KEY, 0))
            continue;

        ret = gf_asprintf(&skey, "rpc-auth.addr.%s.allow", voliter->volname);
        if (ret == -1) {
            gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "Out of memory");
            goto out;
        }
        ret = xlator_set_option(nfsxl, skey, ret, "*");
        GF_FREE(skey);
        if (ret)
            goto out;

        ret = gf_asprintf(&skey, "nfs3.%s.volume-id", voliter->volname);
        if (ret == -1) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_NO_MEMORY,
                   "Out of memory");
            goto out;
        }
        ret = xlator_set_option(nfsxl, skey, ret,
                                uuid_utoa(voliter->volume_id));
        GF_FREE(skey);
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
        memset(&cgraph, 0, sizeof(cgraph));
        if (mod_dict)
            get_transport_type(voliter, mod_dict, nfs_xprt, _gf_true);
        else
            get_transport_type(voliter, voliter->dict, nfs_xprt, _gf_true);

        ret = dict_set_sizen_str_sizen(set_dict, "performance.stat-prefetch",
                                       "off");
        if (ret) {
            gf_smsg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=performance.stat-prefetch", NULL);
            goto out;
        }

        ret = dict_set_sizen_str_sizen(set_dict,
                                       "performance.client-io-threads", "off");
        if (ret) {
            gf_smsg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=performance.client-io-threads", NULL);
            goto out;
        }

        ret = dict_set_str_sizen(set_dict, "client-transport-type", nfs_xprt);
        if (ret) {
            gf_smsg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=client-transport-type", NULL);
            goto out;
        }

        ret = dict_set_uint32(set_dict, "trusted-client", GF_CLIENT_TRUSTED);
        if (ret) {
            gf_smsg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=trusted-client", NULL);
            goto out;
        }

        ret = dict_set_sizen_str_sizen(set_dict, "nfs-volume-file", "yes");
        if (ret) {
            gf_smsg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=nfs-volume-file", NULL);
            goto out;
        }

        if (mod_dict && (data = dict_get_sizen(mod_dict, "volume-name"))) {
            volname = data->data;
            if (strcmp(volname, voliter->volname) == 0)
                dict_copy(mod_dict, set_dict);
        }

        ret = build_client_graph(&cgraph, voliter, set_dict);
        if (ret)
            goto out;

        if (mod_dict) {
            dict_copy(mod_dict, set_dict);
            ret = volgen_graph_set_options_generic(&cgraph, set_dict, voliter,
                                                   basic_option_handler);
        } else {
            ret = volgen_graph_set_options_generic(
                &cgraph, voliter->dict, voliter, basic_option_handler);
        }

        if (ret)
            goto out;

        ret = volgen_graph_set_iam_nfsd(&cgraph);
        if (ret)
            goto out;

        ret = volgen_graph_merge_sub(graph, &cgraph, 1);
        if (ret)
            goto out;
        ret = dict_reset(set_dict);
        if (ret)
            goto out;
    }

    cds_list_for_each_entry(voliter, &priv->volumes, vol_list)
    {
        if (mod_dict) {
            ret = volgen_graph_set_options_generic(graph, mod_dict, voliter,
                                                   nfs_option_handler);
        } else {
            ret = volgen_graph_set_options_generic(graph, voliter->dict,
                                                   voliter, nfs_option_handler);
        }

        if (ret)
            gf_msg("glusterd", GF_LOG_WARNING, 0, GD_MSG_GRAPH_SET_OPT_FAIL,
                   "Could not set "
                   "vol-options for the volume %s",
                   voliter->volname);
    }

out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    dict_unref(set_dict);

    return ret;
}
#endif
/****************************
 *
 * Volume generation interface
 *
 ****************************/

static void
get_brick_filepath(char *filename, glusterd_volinfo_t *volinfo,
                   glusterd_brickinfo_t *brickinfo, char *prefix)
{
    char path[PATH_MAX] = {
        0,
    };
    char brick[PATH_MAX] = {
        0,
    };
    int32_t len = 0;
    glusterd_conf_t *priv = THIS->private;

    GLUSTERD_REMOVE_SLASH_FROM_PATH(brickinfo->path, brick);
    GLUSTERD_GET_VOLUME_DIR(path, volinfo, priv);

    if (prefix)
        len = snprintf(filename, PATH_MAX, "%s/%s.%s.%s.%s.vol", path,
                       volinfo->volname, prefix, brickinfo->hostname, brick);
    else
        len = snprintf(filename, PATH_MAX, "%s/%s.%s.%s.vol", path,
                       volinfo->volname, brickinfo->hostname, brick);
    if ((len < 0) || (len >= PATH_MAX)) {
        filename[0] = 0;
    }
}

gf_boolean_t
glusterd_is_valid_volfpath(char *volname, char *brick)
{
    char volfpath[PATH_MAX] = {
        0,
    };
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    int32_t ret = 0;

    ret = glusterd_brickinfo_new_from_brick(brick, &brickinfo, _gf_false, NULL);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_WARNING, 0, GD_MSG_BRICKINFO_CREATE_FAIL,
               "Failed to create brickinfo"
               " for brick %s",
               brick);
        ret = 0;
        goto out;
    }
    ret = glusterd_volinfo_new(&volinfo);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_WARNING, 0, GD_MSG_VOLINFO_STORE_FAIL,
               "Failed to create volinfo");
        ret = 0;
        goto out;
    }
    (void)snprintf(volinfo->volname, sizeof(volinfo->volname), "%s", volname);
    get_brick_filepath(volfpath, volinfo, brickinfo, NULL);

    ret = ((strlen(volfpath) < PATH_MAX) &&
           strlen(strrchr(volfpath, '/')) < _POSIX_PATH_MAX);

out:
    if (brickinfo)
        glusterd_brickinfo_delete(brickinfo);
    if (volinfo)
        glusterd_volinfo_unref(volinfo);
    return ret;
}

int
glusterd_build_gfproxyd_volfile(glusterd_volinfo_t *volinfo, char *filename)
{
    volgen_graph_t graph = {
        0,
    };
    int ret = -1;

    ret = build_graph_generic(&graph, volinfo, NULL, NULL,
                              &gfproxy_server_graph_builder);
    if (ret == 0)
        ret = volgen_write_volfile(&graph, filename);

    volgen_graph_free(&graph);

    return ret;
}

int
glusterd_generate_gfproxyd_volfile(glusterd_volinfo_t *volinfo)
{
    char filename[PATH_MAX] = {
        0,
    };
    int ret = -1;

    GF_ASSERT(volinfo);

    glusterd_svc_build_gfproxyd_volfile_path(volinfo, filename, PATH_MAX - 1);

    ret = glusterd_build_gfproxyd_volfile(volinfo, filename);

    return ret;
}

static int
glusterd_generate_brick_volfile(glusterd_volinfo_t *volinfo,
                                glusterd_brickinfo_t *brickinfo,
                                dict_t *mod_dict, void *data)
{
    volgen_graph_t graph = {
        0,
    };
    char filename[PATH_MAX] = {
        0,
    };
    int ret = -1;

    GF_ASSERT(volinfo);
    GF_ASSERT(brickinfo);

    get_brick_filepath(filename, volinfo, brickinfo, NULL);

    ret = build_server_graph(&graph, volinfo, mod_dict, brickinfo);
    if (!ret)
        ret = volgen_write_volfile(&graph, filename);

    volgen_graph_free(&graph);

    return ret;
}

int
build_quotad_graph(volgen_graph_t *graph, dict_t *mod_dict)
{
    volgen_graph_t cgraph = {0};
    glusterd_volinfo_t *voliter = NULL;
    glusterd_conf_t *priv = NULL;
    dict_t *set_dict = NULL;
    int ret = 0;
    xlator_t *quotad_xl = NULL;
    char *skey = NULL;

    priv = THIS->private;
    GF_ASSERT(priv);

    graph->type = GF_QUOTAD;

    set_dict = dict_new();
    if (!set_dict) {
        ret = -ENOMEM;
        goto out;
    }

    quotad_xl = volgen_graph_add_as(graph, "features/quotad", "quotad");
    if (!quotad_xl) {
        ret = -1;
        goto out;
    }

    cds_list_for_each_entry(voliter, &priv->volumes, vol_list)
    {
        if (voliter->status != GLUSTERD_STATUS_STARTED)
            continue;

        if (1 != glusterd_is_volume_quota_enabled(voliter))
            continue;

        ret = dict_set_uint32(set_dict, "trusted-client", GF_CLIENT_TRUSTED);
        if (ret) {
            gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=trusted-client", NULL);
            goto out;
        }

        dict_copy(voliter->dict, set_dict);
        if (mod_dict)
            dict_copy(mod_dict, set_dict);

        ret = gf_asprintf(&skey, "%s.volume-id", voliter->volname);
        if (ret == -1) {
            gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "Out of memory");
            goto out;
        }
        ret = xlator_set_option(quotad_xl, skey, ret, voliter->volname);
        GF_FREE(skey);
        if (ret)
            goto out;

        memset(&cgraph, 0, sizeof(cgraph));
        ret = volgen_graph_build_clients(&cgraph, voliter, set_dict, NULL);
        if (ret)
            goto out;

        ret = volume_volgen_graph_build_clusters(&cgraph, voliter, _gf_true);
        if (ret) {
            ret = -1;
            goto out;
        }

        if (mod_dict) {
            dict_copy(mod_dict, set_dict);
            ret = volgen_graph_set_options_generic(&cgraph, set_dict, voliter,
                                                   basic_option_handler);
        } else {
            ret = volgen_graph_set_options_generic(
                &cgraph, voliter->dict, voliter, basic_option_handler);
        }
        if (ret)
            goto out;

        ret = volgen_graph_merge_sub(graph, &cgraph, 1);
        if (ret)
            goto out;

        ret = dict_reset(set_dict);
        if (ret)
            goto out;
    }

out:
    if (set_dict)
        dict_unref(set_dict);
    return ret;
}

static void
get_vol_tstamp_file(char *filename, glusterd_volinfo_t *volinfo)
{
    glusterd_conf_t *priv = THIS->private;
    GLUSTERD_GET_VOLUME_DIR(filename, volinfo, priv);
    strncat(filename, "/marker.tstamp", PATH_MAX - strlen(filename) - 1);
}

static void
get_parent_vol_tstamp_file(char *filename, glusterd_volinfo_t *volinfo)
{
    glusterd_conf_t *priv = NULL;
    int32_t len = 0;

    priv = THIS->private;
    GF_ASSERT(priv);

    len = snprintf(filename, PATH_MAX, "%s/vols/%s/marker.tstamp",
                   priv->workdir, volinfo->parent_volname);
    if ((len < 0) || (len >= PATH_MAX)) {
        filename[0] = 0;
    }
}

int
generate_brick_volfiles(glusterd_volinfo_t *volinfo)
{
    char tstamp_file[PATH_MAX] = {
        0,
    };
    char parent_tstamp_file[PATH_MAX] = {
        0,
    };
    int ret = -1;
    xlator_t *this = THIS;

    ret = glusterd_volinfo_get_boolean(volinfo, VKEY_MARKER_XTIME);
    if (ret == -1)
        return -1;

    assign_brick_groups(volinfo);
    get_vol_tstamp_file(tstamp_file, volinfo);

    if (ret) {
        ret = open(tstamp_file, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (ret == -1 && errno == EEXIST) {
            gf_msg_debug(this->name, 0, "timestamp file exist");
            ret = -2;
        }
        if (ret == -1) {
            gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
                   "failed to create "
                   "%s",
                   tstamp_file);
            return -1;
        }
        if (ret >= 0) {
            sys_close(ret);
            /* If snap_volume, retain timestamp for marker.tstamp
             * from parent. Geo-replication depends on mtime of
             * 'marker.tstamp' to decide the volume-mark, i.e.,
             * geo-rep start time just after session is created.
             */
            if (volinfo->is_snap_volume) {
                get_parent_vol_tstamp_file(parent_tstamp_file, volinfo);
                ret = gf_set_timestamp(parent_tstamp_file, tstamp_file);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_TSTAMP_SET_FAIL,
                           "Unable to set atime and mtime"
                           " of %s as of %s",
                           tstamp_file, parent_tstamp_file);
                    goto out;
                }
            }
        }
    } else {
        ret = sys_unlink(tstamp_file);
        if (ret == -1 && errno == ENOENT)
            ret = 0;
        if (ret == -1) {
            gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
                   "failed to unlink "
                   "%s",
                   tstamp_file);
            return -1;
        }
    }

    ret = glusterd_volume_brick_for_each(volinfo, NULL,
                                         glusterd_generate_brick_volfile);
    if (ret)
        goto out;

    ret = 0;

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
generate_single_transport_client_volfile(glusterd_volinfo_t *volinfo,
                                         char *filepath, dict_t *dict)
{
    volgen_graph_t graph = {
        0,
    };
    int ret = -1;

    ret = build_client_graph(&graph, volinfo, dict);
    if (!ret)
        ret = volgen_write_volfile(&graph, filepath);

    volgen_graph_free(&graph);

    return ret;
}

int
glusterd_generate_client_per_brick_volfile(glusterd_volinfo_t *volinfo)
{
    char filepath[PATH_MAX] = {
        0,
    };
    glusterd_brickinfo_t *brick = NULL;
    volgen_graph_t graph = {
        0,
    };
    dict_t *dict = NULL;
    xlator_t *xl = NULL;
    int ret = -1;
    char *ssl_str = NULL;
    gf_boolean_t ssl_bool = _gf_false;

    dict = dict_new();
    if (!dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        goto out;
    }

    ret = dict_set_uint32(dict, "trusted-client", GF_CLIENT_TRUSTED);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=trusted-client", NULL);
        goto free_dict;
    }

    if (dict_get_str_sizen(volinfo->dict, "client.ssl", &ssl_str) == 0) {
        if (gf_string2boolean(ssl_str, &ssl_bool) == 0) {
            if (ssl_bool) {
                if (dict_set_dynstr_with_alloc(dict, "client.ssl", "on") != 0) {
                    ret = -1;
                    goto free_dict;
                }
            }
        } else {
            ret = -1;
            goto free_dict;
        }
    }

    cds_list_for_each_entry(brick, &volinfo->bricks, brick_list)
    {
        xl = volgen_graph_build_client(&graph, volinfo, brick->hostname, NULL,
                                       brick->path, brick->brick_id, "tcp",
                                       dict);
        if (!xl) {
            ret = -1;
            goto out;
        }

        get_brick_filepath(filepath, volinfo, brick, "client");
        ret = volgen_write_volfile(&graph, filepath);
        if (ret < 0)
            goto out;

        volgen_graph_free(&graph);
        memset(&graph, 0, sizeof(graph));
    }

    ret = 0;
out:
    if (ret)
        volgen_graph_free(&graph);

free_dict:

    if (dict)
        dict_unref(dict);

    return ret;
}

static void
enumerate_transport_reqs(gf_transport_type type, char **types)
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
generate_dummy_client_volfiles(glusterd_volinfo_t *volinfo)
{
    int i = 0;
    int ret = -1;
    char filepath[PATH_MAX] = {
        0,
    };
    char *types[] = {NULL, NULL, NULL};
    dict_t *dict = NULL;
    gf_transport_type type = GF_TRANSPORT_TCP;

    enumerate_transport_reqs(volinfo->transport_type, types);
    dict = dict_new();
    if (!dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        goto out;
    }
    for (i = 0; types[i]; i++) {
        ret = dict_set_str(dict, "client-transport-type", types[i]);
        if (ret) {
            gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=client-transport-type", NULL);
            goto out;
        }
        type = transport_str_to_type(types[i]);

        ret = dict_set_uint32(dict, "trusted-client", GF_CLIENT_OTHER);
        if (ret) {
            gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=trusted-client", NULL);
            goto out;
        }

        ret = glusterd_get_dummy_client_filepath(filepath, volinfo, type);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                   "Received invalid transport-type.");
            goto out;
        }

        ret = generate_single_transport_client_volfile(volinfo, filepath, dict);
        if (ret)
            goto out;
    }

out:
    if (dict)
        dict_unref(dict);

    gf_msg_trace("glusterd", 0, "Returning %d", ret);
    return ret;
}

int
generate_client_volfiles(glusterd_volinfo_t *volinfo,
                         glusterd_client_type_t client_type)
{
    int i = 0;
    int ret = -1;
    char filepath[PATH_MAX] = {
        0,
    };
    char *volname = NULL;
    char *types[] = {NULL, NULL, NULL};
    dict_t *dict = NULL;
    gf_transport_type type = GF_TRANSPORT_TCP;

    volname = volinfo->is_snap_volume ? volinfo->parent_volname
                                      : volinfo->volname;

    if (volname && !strcmp(volname, GLUSTER_SHARED_STORAGE) &&
        client_type != GF_CLIENT_TRUSTED) {
        /*
         * shared storage volume cannot be mounted from non trusted
         * nodes. So we are not creating volfiles for non-trusted
         * clients for shared volumes as well as snapshot of shared
         * volumes.
         */

        ret = 0;
        gf_msg_debug("glusterd", 0,
                     "Skipping the non-trusted volfile"
                     "creation for shared storage volume. Volume %s",
                     volname);
        goto out;
    }

    enumerate_transport_reqs(volinfo->transport_type, types);
    dict = dict_new();
    if (!dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        goto out;
    }
    for (i = 0; types[i]; i++) {
        ret = dict_set_str(dict, "client-transport-type", types[i]);
        if (ret) {
            gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=client-transport-type", NULL);
            goto out;
        }
        type = transport_str_to_type(types[i]);

        ret = dict_set_uint32(dict, "trusted-client", client_type);
        if (ret) {
            gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=trusted-client", NULL);
            goto out;
        }

        if (client_type == GF_CLIENT_TRUSTED) {
            ret = glusterd_get_trusted_client_filepath(filepath, volinfo, type);
        } else if (client_type == GF_CLIENT_TRUSTED_PROXY) {
            glusterd_get_gfproxy_client_volfile(volinfo, filepath, PATH_MAX);
            ret = dict_set_int32_sizen(dict, "gfproxy-client", 1);
        } else {
            ret = glusterd_get_client_filepath(filepath, volinfo, type);
        }
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                   "Received invalid transport-type");
            goto out;
        }

        ret = generate_single_transport_client_volfile(volinfo, filepath, dict);
        if (ret)
            goto out;
    }

    /* Generate volfile for rebalance process */
    glusterd_get_rebalance_volfile(volinfo, filepath, PATH_MAX);
    ret = build_rebalance_volfile(volinfo, filepath, dict);

    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
               "Failed to create rebalance volfile for %s", volinfo->volname);
        goto out;
    }

out:
    if (dict)
        dict_unref(dict);

    gf_msg_trace("glusterd", 0, "Returning %d", ret);
    return ret;
}

int
glusterd_snapdsvc_generate_volfile(volgen_graph_t *graph,
                                   glusterd_volinfo_t *volinfo)
{
    xlator_t *xl = NULL;
    char *username = NULL;
    char *passwd = NULL;
    int ret = 0;
    char key[PATH_MAX] = {
        0,
    };
    dict_t *set_dict = NULL;
    char *loglevel = NULL;
    char *xlator = NULL;
    char *ssl_str = NULL;
    gf_boolean_t ssl_bool = _gf_false;

    set_dict = dict_copy(volinfo->dict, NULL);
    if (!set_dict)
        return -1;

    ret = dict_get_str_sizen(set_dict, "xlator", &xlator);
    if (!ret) {
        ret = dict_get_str_sizen(set_dict, "loglevel", &loglevel);
        if (ret) {
            gf_msg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                   "could not get both"
                   " translator name and loglevel for log level "
                   "request");
            return -1;
        }
    }

    xl = volgen_graph_add(graph, "features/snapview-server", volinfo->volname);
    if (!xl)
        return -1;

    ret = xlator_set_fixed_option(xl, "volname", volinfo->volname);
    if (ret)
        return -1;

    xl = volgen_graph_add(graph, "performance/io-threads", volinfo->volname);
    if (!xl)
        return -1;

    snprintf(key, sizeof(key), "snapd-%s", volinfo->volname);
    xl = volgen_graph_add_as(graph, "debug/io-stats", key);
    if (!xl)
        return -1;

    xl = volgen_graph_add(graph, "protocol/server", volinfo->volname);
    if (!xl)
        return -1;

    ret = xlator_set_fixed_option(xl, "transport-type", "tcp");
    if (ret)
        return -1;

    if (dict_get_str_sizen(set_dict, "server.ssl", &ssl_str) == 0) {
        if (gf_string2boolean(ssl_str, &ssl_bool) == 0) {
            if (ssl_bool) {
                ret = xlator_set_fixed_option(
                    xl, "transport.socket.ssl-enabled", "true");
                if (ret) {
                    return -1;
                }
            }
        }
    }

    RPC_SET_OPT(xl, SSL_OWN_CERT_OPT, "ssl-own-cert", return -1);
    RPC_SET_OPT(xl, SSL_PRIVATE_KEY_OPT, "ssl-private-key", return -1);
    RPC_SET_OPT(xl, SSL_CA_LIST_OPT, "ssl-ca-list", return -1);
    RPC_SET_OPT(xl, SSL_CRL_PATH_OPT, "ssl-crl-path", return -1);
    RPC_SET_OPT(xl, SSL_CERT_DEPTH_OPT, "ssl-cert-depth", return -1);
    RPC_SET_OPT(xl, SSL_CIPHER_LIST_OPT, "ssl-cipher-list", return -1);
    RPC_SET_OPT(xl, SSL_DH_PARAM_OPT, "ssl-dh-param", return -1);
    RPC_SET_OPT(xl, SSL_EC_CURVE_OPT, "ssl-ec-curve", return -1);

    username = glusterd_auth_get_username(volinfo);
    passwd = glusterd_auth_get_password(volinfo);

    ret = snprintf(key, sizeof(key), "auth.login.snapd-%s.allow",
                   volinfo->volname);
    ret = xlator_set_option(xl, key, ret, username);
    if (ret)
        return -1;

    ret = snprintf(key, sizeof(key), "auth.login.%s.password", username);
    ret = xlator_set_option(xl, key, ret, passwd);
    if (ret)
        return -1;

    snprintf(key, sizeof(key), "snapd-%s", volinfo->volname);
    ret = xlator_set_fixed_option(xl, "auth-path", key);
    if (ret)
        return -1;

    ret = volgen_graph_set_options_generic(
        graph, set_dict, (xlator && loglevel) ? (void *)set_dict : volinfo,
        (xlator && loglevel) ? &server_spec_extended_option_handler
                             : &server_spec_option_handler);

    return ret;
}

static int
prepare_bitrot_scrub_volume_options(glusterd_volinfo_t *volinfo,
                                    dict_t *mod_dict, dict_t *set_dict)
{
    int ret = 0;

    ret = dict_set_uint32(set_dict, "trusted-client", GF_CLIENT_TRUSTED);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=trusted-client", NULL);
        goto out;
    }

    dict_copy(volinfo->dict, set_dict);
    if (mod_dict)
        dict_copy(mod_dict, set_dict);

out:
    return ret;
}

static int
build_bitd_clusters(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                    dict_t *set_dict, int brick_count, unsigned int numbricks)
{
    int ret = -1;
    int clusters = 0;
    xlator_t *xl = NULL;
    char *brick_hint = NULL;
    char *bitrot_args[] = {"features/bit-rot", "%s-bit-rot-%d"};

    ret = volgen_link_bricks_from_list_tail(graph, volinfo, bitrot_args[0],
                                            bitrot_args[1], brick_count,
                                            brick_count);
    clusters = ret;

    xl = first_of(graph);

    ret = gf_asprintf(&brick_hint, "%d", numbricks);
    if (ret < 0)
        goto out;

    ret = xlator_set_fixed_option(xl, "brick-count", brick_hint);
    if (ret)
        goto out;

    ret = clusters;

out:
    GF_FREE(brick_hint);
    brick_hint = NULL;
    return ret;
}

static int
build_bitd_volume_graph(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                        dict_t *mod_dict, unsigned int numbricks)
{
    volgen_graph_t cgraph = {0};
    xlator_t *this = THIS;
    xlator_t *xl = NULL;
    dict_t *set_dict = NULL;
    glusterd_conf_t *priv = NULL;
    int ret = 0;
    int clusters = -1;
    glusterd_brickinfo_t *brickinfo = NULL;
    int brick_count = 0;
    char transt[16] = {
        0,
    };

    priv = this->private;
    GF_ASSERT(priv);

    set_dict = dict_new();
    if (!set_dict) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        ret = -1;
        goto out;
    }

    ret = prepare_bitrot_scrub_volume_options(volinfo, mod_dict, set_dict);
    if (ret)
        goto out;

    get_transport_type(volinfo, set_dict, transt, _gf_false);
    if (!strncmp(transt, "tcp,rdma", SLEN("tcp,rdma")))
        (void)snprintf(transt, sizeof(transt), "%s", "tcp");

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (!glusterd_is_local_brick(this, volinfo, brickinfo))
            continue;

        xl = volgen_graph_build_client(&cgraph, volinfo, brickinfo->hostname,
                                       NULL, brickinfo->path,
                                       brickinfo->brick_id, transt, set_dict);
        if (!xl) {
            ret = -1;
            goto out;
        }
        brick_count++;
    }

    if (brick_count == 0) {
        ret = 0;
        goto out;
    }

    clusters = build_bitd_clusters(&cgraph, volinfo, set_dict, brick_count,
                                   numbricks);
    if (clusters < 0) {
        ret = -1;
        goto out;
    }

    ret = volgen_graph_set_options_generic(&cgraph, set_dict, volinfo,
                                           bitrot_option_handler);
    if (ret)
        goto out;

    ret = volgen_graph_merge_sub(graph, &cgraph, clusters);
    if (ret)
        goto out;

    ret = graph_set_generic_options(this, graph, set_dict, "Bitrot");

out:
    if (set_dict)
        dict_unref(set_dict);

    return ret;
}

int
build_bitd_graph(volgen_graph_t *graph, dict_t *mod_dict)
{
    glusterd_volinfo_t *voliter = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    int ret = 0;
    xlator_t *iostxl = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    unsigned int numbricks = 0;

    priv = this->private;
    GF_ASSERT(priv);

    iostxl = volgen_graph_add_as(graph, "debug/io-stats", "bitd");
    if (!iostxl) {
        ret = -1;
        goto out;
    }

    /* TODO: do way with this extra loop _if possible_ */
    cds_list_for_each_entry(voliter, &priv->volumes, vol_list)
    {
        if (voliter->status != GLUSTERD_STATUS_STARTED)
            continue;
        if (!glusterd_is_bitrot_enabled(voliter))
            continue;

        cds_list_for_each_entry(brickinfo, &voliter->bricks, brick_list)
        {
            if (!glusterd_is_local_brick(this, voliter, brickinfo))
                continue;
            numbricks++;
        }
    }

    cds_list_for_each_entry(voliter, &priv->volumes, vol_list)
    {
        if (voliter->status != GLUSTERD_STATUS_STARTED)
            continue;

        if (!glusterd_is_bitrot_enabled(voliter))
            continue;

        ret = build_bitd_volume_graph(graph, voliter, mod_dict, numbricks);
    }
out:
    return ret;
}

static int
build_scrub_clusters(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                     dict_t *set_dict, int brick_count)
{
    int ret = -1;
    int clusters = 0;
    xlator_t *xl = NULL;
    char *scrub_args[] = {"features/bit-rot", "%s-bit-rot-%d"};

    ret = volgen_link_bricks_from_list_tail(
        graph, volinfo, scrub_args[0], scrub_args[1], brick_count, brick_count);
    clusters = ret;

    xl = first_of(graph);

    ret = xlator_set_fixed_option(xl, "scrubber", "true");
    if (ret)
        goto out;

    ret = clusters;

out:
    return ret;
}

static int
build_scrub_volume_graph(volgen_graph_t *graph, glusterd_volinfo_t *volinfo,
                         dict_t *mod_dict)
{
    volgen_graph_t cgraph = {0};
    dict_t *set_dict = NULL;
    xlator_t *this = THIS;
    xlator_t *xl = NULL;
    glusterd_conf_t *priv = NULL;
    int ret = 0;
    int clusters = -1;
    int brick_count = 0;
    char transt[16] = {
        0,
    };
    glusterd_brickinfo_t *brickinfo = NULL;

    priv = this->private;
    GF_ASSERT(priv);

    set_dict = dict_new();
    if (!set_dict) {
        ret = -1;
        goto out;
    }

    ret = prepare_bitrot_scrub_volume_options(volinfo, mod_dict, set_dict);
    if (ret)
        goto out;

    get_transport_type(volinfo, set_dict, transt, _gf_false);
    if (!strncmp(transt, "tcp,rdma", SLEN("tcp,rdma")))
        (void)snprintf(transt, sizeof(transt), "%s", "tcp");

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (!glusterd_is_local_brick(this, volinfo, brickinfo))
            continue;

        xl = volgen_graph_build_client(&cgraph, volinfo, brickinfo->hostname,
                                       NULL, brickinfo->path,
                                       brickinfo->brick_id, transt, set_dict);
        if (!xl) {
            ret = -1;
            goto out;
        }
        brick_count++;
    }

    if (brick_count == 0) {
        ret = 0;
        goto out;
    }

    clusters = build_scrub_clusters(&cgraph, volinfo, set_dict, brick_count);
    if (clusters < 0) {
        ret = -1;
        goto out;
    }

    ret = volgen_graph_set_options_generic(&cgraph, set_dict, volinfo,
                                           scrubber_option_handler);
    if (ret)
        goto out;

    ret = volgen_graph_merge_sub(graph, &cgraph, clusters);
    if (ret)
        goto out;

    ret = graph_set_generic_options(this, graph, set_dict, "Scrubber");
out:
    if (set_dict)
        dict_unref(set_dict);

    return ret;
}

int
build_scrub_graph(volgen_graph_t *graph, dict_t *mod_dict)
{
    glusterd_volinfo_t *voliter = NULL;
    glusterd_conf_t *priv = NULL;
    int ret = 0;
    xlator_t *iostxl = NULL;

    priv = THIS->private;
    GF_ASSERT(priv);

    iostxl = volgen_graph_add_as(graph, "debug/io-stats", "scrub");
    if (!iostxl) {
        ret = -1;
        goto out;
    }

    cds_list_for_each_entry(voliter, &priv->volumes, vol_list)
    {
        if (voliter->status != GLUSTERD_STATUS_STARTED)
            continue;

        if (!glusterd_is_bitrot_enabled(voliter))
            continue;

        ret = build_scrub_volume_graph(graph, voliter, mod_dict);
    }
out:
    return ret;
}

int
glusterd_snapdsvc_create_volfile(glusterd_volinfo_t *volinfo)
{
    volgen_graph_t graph = {
        0,
    };
    int ret = -1;
    char filename[PATH_MAX] = {
        0,
    };

    graph.type = GF_SNAPD;
    glusterd_svc_build_snapd_volfile(volinfo, filename, PATH_MAX);

    ret = glusterd_snapdsvc_generate_volfile(&graph, volinfo);
    if (!ret)
        ret = volgen_write_volfile(&graph, filename);

    volgen_graph_free(&graph);

    return ret;
}

int
glusterd_create_rb_volfiles(glusterd_volinfo_t *volinfo,
                            glusterd_brickinfo_t *brickinfo)
{
    int ret = -1;

    ret = glusterd_generate_brick_volfile(volinfo, brickinfo, NULL, NULL);
    if (!ret)
        ret = generate_client_volfiles(volinfo, GF_CLIENT_TRUSTED);
    if (!ret)
        ret = glusterd_fetchspec_notify(THIS);

    return ret;
}

int
glusterd_create_volfiles(glusterd_volinfo_t *volinfo)
{
    int ret = -1;

    ret = generate_brick_volfiles(volinfo);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
               "Could not generate volfiles for bricks");
        goto out;
    }

    ret = generate_client_volfiles(volinfo, GF_CLIENT_TRUSTED);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
               "Could not generate trusted client volfiles");
        goto out;
    }

    ret = generate_client_volfiles(volinfo, GF_CLIENT_TRUSTED_PROXY);
    if (ret) {
        gf_log(THIS->name, GF_LOG_ERROR,
               "Could not generate gfproxy client volfiles");
        goto out;
    }

    ret = generate_client_volfiles(volinfo, GF_CLIENT_OTHER);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
               "Could not generate client volfiles");

    ret = glusterd_generate_gfproxyd_volfile(volinfo);
    if (ret)
        gf_log(THIS->name, GF_LOG_ERROR, "Could not generate gfproxy volfiles");

    dict_del_sizen(volinfo->dict, "skip-CLIOT");

out:
    return ret;
}

int
glusterd_create_volfiles_and_notify_services(glusterd_volinfo_t *volinfo)
{
    int ret = -1;

    ret = glusterd_create_volfiles(volinfo);
    if (ret)
        goto out;

    ret = glusterd_fetchspec_notify(THIS);

out:
    return ret;
}

int
glusterd_create_global_volfile(glusterd_graph_builder_t builder, char *filepath,
                               dict_t *mod_dict)
{
    volgen_graph_t graph = {
        0,
    };
    int ret = -1;

    ret = builder(&graph, mod_dict);
    if (!ret)
        ret = volgen_write_volfile(&graph, filepath);

    volgen_graph_free(&graph);

    return ret;
}

int
glusterd_delete_volfile(glusterd_volinfo_t *volinfo,
                        glusterd_brickinfo_t *brickinfo)
{
    int ret = 0;
    char filename[PATH_MAX] = {
        0,
    };

    GF_ASSERT(volinfo);
    GF_ASSERT(brickinfo);

    get_brick_filepath(filename, volinfo, brickinfo, NULL);
    ret = sys_unlink(filename);
    if (ret)
        gf_msg("glusterd", GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "failed to delete file: %s", filename);
    return ret;
}

int
validate_shdopts(glusterd_volinfo_t *volinfo, dict_t *val_dict,
                 char **op_errstr)
{
    volgen_graph_t graph = {
        0,
    };
    int ret = -1;

    graph.errstr = op_errstr;

    if (!glusterd_is_shd_compatible_volume(volinfo)) {
        ret = 0;
        goto out;
    }
    ret = dict_set_int32_sizen(val_dict, "graph-check", 1);
    if (ret) {
        gf_smsg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=graph-check", NULL);
        goto out;
    }
    ret = build_shd_graph(&graph, val_dict);
    if (!ret)
        ret = graph_reconf_validateopt(&graph.graph, op_errstr);

    volgen_graph_free(&graph);

    gf_msg_debug("glusterd", 0, "Returning %d", ret);
out:
    dict_del_sizen(val_dict, "graph-check");
    return ret;
}

#ifdef BUILD_GNFS
static int
validate_nfsopts(glusterd_volinfo_t *volinfo, dict_t *val_dict,
                 char **op_errstr)
{
    volgen_graph_t graph = {
        0,
    };
    int ret = -1;
    char transport_type[16] = {
        0,
    };
    char *tt = NULL;
    char err_str[128] = {
        0,
    };
    xlator_t *this = THIS;

    graph.errstr = op_errstr;

    get_vol_transport_type(volinfo, transport_type);
    ret = dict_get_str_sizen(val_dict, "nfs.transport-type", &tt);
    if (!ret) {
        if (volinfo->transport_type != GF_TRANSPORT_BOTH_TCP_RDMA) {
            snprintf(err_str, sizeof(err_str),
                     "Changing nfs "
                     "transport type is allowed only for volumes "
                     "of transport type tcp,rdma");
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_OP_UNSUPPORTED, "%s",
                   err_str);
            *op_errstr = gf_strdup(err_str);
            ret = -1;
            goto out;
        }
        if (strcmp(tt, "tcp") && strcmp(tt, "rdma")) {
            snprintf(err_str, sizeof(err_str),
                     "wrong transport "
                     "type %s",
                     tt);
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_INCOMPATIBLE_VALUE,
                    "Type=%s", tt, NULL);
            *op_errstr = gf_strdup(err_str);
            ret = -1;
            goto out;
        }
    }

    ret = dict_set_str_sizen(val_dict, "volume-name", volinfo->volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
               "Failed to set volume name");
        goto out;
    }

    ret = build_nfs_graph(&graph, val_dict);
    if (!ret)
        ret = graph_reconf_validateopt(&graph.graph, op_errstr);

    volgen_graph_free(&graph);

out:
    if (dict_get_sizen(val_dict, "volume-name"))
        dict_del_sizen(val_dict, "volume-name");
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}
#endif

int
validate_clientopts(glusterd_volinfo_t *volinfo, dict_t *val_dict,
                    char **op_errstr)
{
    volgen_graph_t graph = {
        0,
    };
    int ret = -1;

    GF_ASSERT(volinfo);

    graph.errstr = op_errstr;

    ret = build_client_graph(&graph, volinfo, val_dict);
    if (!ret)
        ret = graph_reconf_validateopt(&graph.graph, op_errstr);

    volgen_graph_free(&graph);

    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

int
validate_brickopts(glusterd_volinfo_t *volinfo, glusterd_brickinfo_t *brickinfo,
                   dict_t *mod_dict, void *reconf)
{
    volgen_graph_t graph = {
        0,
    };
    int ret = -1;
    struct gd_validate_reconf_opts *brickreconf = reconf;
    dict_t *val_dict = brickreconf->options;
    char **op_errstr = brickreconf->op_errstr;
    dict_t *full_dict = NULL;

    GF_ASSERT(volinfo);

    graph.errstr = op_errstr;
    full_dict = dict_new();
    if (!full_dict) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        ret = -1;
        goto out;
    }

    if (mod_dict)
        dict_copy(mod_dict, full_dict);

    if (val_dict)
        dict_copy(val_dict, full_dict);

    ret = build_server_graph(&graph, volinfo, full_dict, brickinfo);
    if (!ret)
        ret = graph_reconf_validateopt(&graph.graph, op_errstr);

    volgen_graph_free(&graph);

out:
    if (full_dict)
        dict_unref(full_dict);

    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

int
glusterd_validate_brickreconf(glusterd_volinfo_t *volinfo, dict_t *val_dict,
                              char **op_errstr)
{
    int ret = -1;
    struct gd_validate_reconf_opts brickreconf = {0};

    brickreconf.options = val_dict;
    brickreconf.op_errstr = op_errstr;
    ret = glusterd_volume_brick_for_each(volinfo, &brickreconf,
                                         validate_brickopts);
    return ret;
}

static int
_check_globalopt(dict_t *this, char *key, data_t *value, void *ret_val)
{
    int *ret = NULL;

    ret = ret_val;
    if (*ret)
        return 0;
    if (!glusterd_check_globaloption(key))
        *ret = 1;

    return 0;
}

int
glusterd_validate_globalopts(glusterd_volinfo_t *volinfo, dict_t *val_dict,
                             char **op_errstr)
{
    int ret = 0;

    dict_foreach(val_dict, _check_globalopt, &ret);
    if (ret) {
        *op_errstr = gf_strdup("option specified is not a global option");
        return -1;
    }
    ret = glusterd_validate_brickreconf(volinfo, val_dict, op_errstr);

    if (ret) {
        gf_msg_debug("glusterd", 0, "Could not Validate  bricks");
        goto out;
    }

    ret = validate_clientopts(volinfo, val_dict, op_errstr);
    if (ret) {
        gf_msg_debug("glusterd", 0, "Could not Validate client");
        goto out;
    }
#ifdef BUILD_GNFS
    ret = validate_nfsopts(volinfo, val_dict, op_errstr);
    if (ret) {
        gf_msg_debug("glusterd", 0, "Could not Validate nfs");
        goto out;
    }
#endif
    ret = validate_shdopts(volinfo, val_dict, op_errstr);
    if (ret) {
        gf_msg_debug("glusterd", 0, "Could not Validate self-heald");
        goto out;
    }

out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

static int
_check_localopt(dict_t *this, char *key, data_t *value, void *ret_val)
{
    int *ret = NULL;

    ret = ret_val;
    if (*ret)
        return 0;
    if (!glusterd_check_localoption(key))
        *ret = 1;

    return 0;
}

int
glusterd_validate_reconfopts(glusterd_volinfo_t *volinfo, dict_t *val_dict,
                             char **op_errstr)
{
    int ret = 0;

    dict_foreach(val_dict, _check_localopt, &ret);
    if (ret) {
        *op_errstr = gf_strdup("option specified is not a local option");
        return -1;
    }
    ret = glusterd_validate_brickreconf(volinfo, val_dict, op_errstr);

    if (ret) {
        gf_msg_debug("glusterd", 0, "Could not Validate  bricks");
        goto out;
    }

    ret = validate_clientopts(volinfo, val_dict, op_errstr);
    if (ret) {
        gf_msg_debug("glusterd", 0, "Could not Validate client");
        goto out;
    }

#ifdef BUILD_GNFS
    ret = validate_nfsopts(volinfo, val_dict, op_errstr);
    if (ret) {
        gf_msg_debug("glusterd", 0, "Could not Validate nfs");
        goto out;
    }
#endif
    ret = validate_shdopts(volinfo, val_dict, op_errstr);
    if (ret) {
        gf_msg_debug("glusterd", 0, "Could not Validate self-heald");
        goto out;
    }

out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

struct volopt_map_entry *
gd_get_vmep(const char *key)
{
    char *completion = NULL;
    struct volopt_map_entry *vmep = NULL;
    int ret = 0;

    if (!key)
        return NULL;

    COMPLETE_OPTION((char *)key, completion, ret);
    for (vmep = glusterd_volopt_map; vmep->key; vmep++) {
        if (strcmp(vmep->key, key) == 0)
            return vmep;
    }

    return NULL;
}

uint32_t
glusterd_get_op_version_from_vmep(struct volopt_map_entry *vmep)
{
    if (vmep)
        return vmep->op_version;

    return 0;
}

gf_boolean_t
gd_is_client_option(struct volopt_map_entry *vmep)
{
    if (vmep && (vmep->flags & VOLOPT_FLAG_CLIENT_OPT))
        return _gf_true;

    return _gf_false;
}

gf_boolean_t
gd_is_xlator_option(struct volopt_map_entry *vmep)
{
    if (vmep && (vmep->flags & VOLOPT_FLAG_XLATOR_OPT))
        return _gf_true;

    return _gf_false;
}

static volume_option_type_t
_gd_get_option_type(struct volopt_map_entry *vmep)
{
    void *dl_handle = NULL;
    volume_opt_list_t vol_opt_list = {
        {0},
    };
    int ret = -1;
    volume_option_t *opt = NULL;
    char *xlopt_key = NULL;
    volume_option_type_t opt_type = GF_OPTION_TYPE_MAX;

    if (vmep) {
        CDS_INIT_LIST_HEAD(&vol_opt_list.list);
        ret = xlator_volopt_dynload(vmep->voltype, &dl_handle, &vol_opt_list);
        if (ret)
            goto out;

        if (_get_xlator_opt_key_from_vme(vmep, &xlopt_key))
            goto out;

        opt = xlator_volume_option_get_list(&vol_opt_list, xlopt_key);
        _free_xlator_opt_key(xlopt_key);

        if (opt)
            opt_type = opt->type;
    }

out:
    if (dl_handle) {
        dlclose(dl_handle);
        dl_handle = NULL;
    }

    return opt_type;
}

gf_boolean_t
gd_is_boolean_option(struct volopt_map_entry *vmep)
{
    if (GF_OPTION_TYPE_BOOL == _gd_get_option_type(vmep))
        return _gf_true;

    return _gf_false;
}
