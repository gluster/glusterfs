/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <stdint.h>                  // for uint32_t
#include <sys/time.h>                // for timeval
#include <errno.h>                   // for EIO, errno, EINVAL, ENOMEM
#include <fnmatch.h>                 // for fnmatch, FNM_NOESCAPE
#include <openssl/sha.h>             // for SHA256_DIGEST_LENGTH
#include <regex.h>                   // for regmatch_t, regcomp
#include <stdio.h>                   // for fclose, fopen, snprintf
#include <stdlib.h>                  // for NULL, atoi, mkstemp
#include <string.h>                  // for strcmp, strerror, memcpy
#include <strings.h>                 // for rindex
#include <sys/stat.h>                // for stat
#include <sys/time.h>                // for gettimeofday
#include <unistd.h>                  // for gethostname, getpid
#include "glusterfs/common-utils.h"  // for gf_strncpy, gf_time_fmt
#include "glusterfs/defaults.h"
#include "glusterfs/dict.h"                   // for dict_foreach, dict_set_...
#include "glusterfs/globals.h"                // for xlator_t, xlator_list_t
#include "glusterfs/glusterfs.h"              // for glusterfs_graph_t, glus...
#include "glusterfs/glusterfs-fops.h"         // for GF_EVENT_GRAPH_NEW, GF_...
#include "glusterfs/libglusterfs-messages.h"  // for LG_MSG_GRAPH_ERROR, LG_...
#include "glusterfs/list.h"                   // for list_add, list_del_init
#include "glusterfs/logging.h"                // for gf_msg, GF_LOG_ERROR
#include "glusterfs/mem-pool.h"               // for GF_FREE, gf_strdup, GF_...
#include "glusterfs/mem-types.h"              // for gf_common_mt_xlator_list_t
#include "glusterfs/options.h"                // for xlator_tree_reconfigure
#include "glusterfs/syscall.h"                // for sys_close, sys_stat

#if 0
static void
_gf_dump_details (int argc, char **argv)
{
        extern FILE *gf_log_logfile;
        int          i = 0;
        char         timestr[GF_TIMESTR_SIZE];
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
glusterfs_read_secure_access_file(void)
{
    FILE *fp = NULL;
    char line[100] = {
        0,
    };
    int cert_depth = 1; /* Default SSL CERT DEPTH */
    regex_t regcmpl;
    char *key = {"^option transport.socket.ssl-cert-depth"};
    char keyval[50] = {
        0,
    };
    int start = 0, end = 0, copy_len = 0;
    regmatch_t result[1] = {{0}};

    fp = fopen(SECURE_ACCESS_FILE, "r");
    if (!fp)
        goto out;

    /* Check if any line matches with key */
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (regcomp(&regcmpl, key, REG_EXTENDED)) {
            goto out;
        }
        if (!regexec(&regcmpl, line, 1, result, 0)) {
            start = result[0].rm_so;
            end = result[0].rm_eo;
            copy_len = end - start;
            gf_strncpy(keyval, line + copy_len, sizeof(keyval));
            if (keyval[0]) {
                cert_depth = atoi(keyval);
                if (cert_depth == 0)
                    cert_depth = 1; /* Default SSL CERT DEPTH */
                break;
            }
        }
        regfree(&regcmpl);
    }

out:
    if (fp)
        fclose(fp);
    return cert_depth;
}

xlator_t *
glusterfs_get_last_xlator(glusterfs_graph_t *graph)
{
    xlator_t *trav = graph->first;
    if (!trav)
        return NULL;

    while (trav->next)
        trav = trav->next;

    return trav;
}

xlator_t *
glusterfs_mux_xlator_unlink(xlator_t *pxl, xlator_t *cxl)
{
    xlator_list_t *unlink = NULL;
    xlator_list_t *prev = NULL;
    xlator_list_t **tmp = NULL;
    xlator_t *next_child = NULL;
    xlator_t *xl = NULL;

    for (tmp = &pxl->children; *tmp; tmp = &(*tmp)->next) {
        if ((*tmp)->xlator == cxl) {
            unlink = *tmp;
            *tmp = (*tmp)->next;
            if (*tmp)
                next_child = (*tmp)->xlator;
            break;
        }
        prev = *tmp;
    }

    if (!prev)
        xl = pxl;
    else if (prev->xlator)
        xl = prev->xlator->graph->last_xl;

    if (xl)
        xl->next = next_child;
    if (next_child)
        next_child->prev = xl;

    GF_FREE(unlink);
    return next_child;
}

int
glusterfs_xlator_link(xlator_t *pxl, xlator_t *cxl)
{
    xlator_list_t *xlchild = NULL;
    xlator_list_t *xlparent = NULL;
    xlator_list_t **tmp = NULL;

    xlparent = (void *)GF_CALLOC(1, sizeof(*xlparent),
                                 gf_common_mt_xlator_list_t);
    if (!xlparent)
        return -1;

    xlchild = (void *)GF_CALLOC(1, sizeof(*xlchild),
                                gf_common_mt_xlator_list_t);
    if (!xlchild) {
        GF_FREE(xlparent);

        return -1;
    }

    xlparent->xlator = pxl;
    for (tmp = &cxl->parents; *tmp; tmp = &(*tmp)->next)
        ;
    *tmp = xlparent;

    xlchild->xlator = cxl;
    for (tmp = &pxl->children; *tmp; tmp = &(*tmp)->next)
        ;
    *tmp = xlchild;

    return 0;
}

void
glusterfs_graph_set_first(glusterfs_graph_t *graph, xlator_t *xl)
{
    xl->next = graph->first;
    if (graph->first)
        ((xlator_t *)graph->first)->prev = xl;
    graph->first = xl;

    graph->xl_count++;
    xl->xl_id = graph->xl_count;
}

int
glusterfs_graph_insert(glusterfs_graph_t *graph, glusterfs_ctx_t *ctx,
                       const char *type, const char *name,
                       gf_boolean_t autoload)
{
    xlator_t *ixl = NULL;

    if (!ctx->primary) {
        gf_msg("glusterfs", GF_LOG_ERROR, 0, LG_MSG_VOLUME_ERROR,
               "volume \"%s\" can be added from command line only "
               "on client side",
               type);

        return -1;
    }

    ixl = GF_CALLOC(1, sizeof(*ixl), gf_common_mt_xlator_t);
    if (!ixl)
        return -1;

    ixl->ctx = ctx;
    ixl->graph = graph;
    ixl->options = dict_new();
    if (!ixl->options)
        goto err;

    ixl->name = gf_strdup(name);
    if (!ixl->name)
        goto err;

    ixl->is_autoloaded = autoload;

    if (xlator_set_type(ixl, type) == -1) {
        gf_msg("glusterfs", GF_LOG_ERROR, 0, LG_MSG_INIT_FAILED,
               "%s (%s) initialization failed", name, type);
        return -1;
    }

    if (glusterfs_xlator_link(ixl, graph->top) == -1)
        goto err;
    glusterfs_graph_set_first(graph, ixl);
    graph->top = ixl;

    return 0;
err:
    xlator_destroy(ixl);
    return -1;
}

int
glusterfs_graph_acl(glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
    int ret = 0;
    cmd_args_t *cmd_args = NULL;

    cmd_args = &ctx->cmd_args;

    if (!cmd_args->acl)
        return 0;

    ret = glusterfs_graph_insert(graph, ctx, "system/posix-acl",
                                 "posix-acl-autoload", 1);
    return ret;
}

int
glusterfs_graph_worm(glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
    int ret = 0;
    cmd_args_t *cmd_args = NULL;

    cmd_args = &ctx->cmd_args;

    if (!cmd_args->worm)
        return 0;

    ret = glusterfs_graph_insert(graph, ctx, "features/worm", "worm-autoload",
                                 1);
    return ret;
}

int
glusterfs_graph_meta(glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
    int ret = 0;

    if (!ctx->primary)
        return 0;

    ret = glusterfs_graph_insert(graph, ctx, "meta", "meta-autoload", 1);
    return ret;
}

int
glusterfs_graph_mac_compat(glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
    int ret = 0;
    cmd_args_t *cmd_args = NULL;

    cmd_args = &ctx->cmd_args;

    if (cmd_args->mac_compat == GF_OPTION_DISABLE)
        return 0;

    ret = glusterfs_graph_insert(graph, ctx, "features/mac-compat",
                                 "mac-compat-autoload", 1);

    return ret;
}

int
glusterfs_graph_gfid_access(glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
    int ret = 0;
    cmd_args_t *cmd_args = NULL;

    cmd_args = &ctx->cmd_args;

    if (!cmd_args->aux_gfid_mount)
        return 0;

    ret = glusterfs_graph_insert(graph, ctx, "features/gfid-access",
                                 "gfid-access-autoload", 1);
    return ret;
}

static void
gf_add_cmdline_options(glusterfs_graph_t *graph, cmd_args_t *cmd_args)
{
    int ret = 0;
    xlator_t *trav = NULL;
    xlator_cmdline_option_t *cmd_option = NULL;

    trav = graph->first;

    while (trav) {
        list_for_each_entry(cmd_option, &cmd_args->xlator_options, cmd_args)
        {
            if (!fnmatch(cmd_option->volume, trav->name, FNM_NOESCAPE)) {
                ret = dict_set_str(trav->options, cmd_option->key,
                                   cmd_option->value);
                if (ret == 0) {
                    gf_msg(trav->name, GF_LOG_TRACE, 0, LG_MSG_VOL_OPTION_ADD,
                           "adding option '%s' for "
                           "volume '%s' with value '%s'",
                           cmd_option->key, trav->name, cmd_option->value);
                } else {
                    gf_msg(trav->name, GF_LOG_WARNING, -ret,
                           LG_MSG_VOL_OPTION_ADD,
                           "adding option '%s' for "
                           "volume '%s' failed",
                           cmd_option->key, trav->name);
                }
            }
        }
        trav = trav->next;
    }
}

int
glusterfs_graph_validate_options(glusterfs_graph_t *graph)
{
    xlator_t *trav = NULL;
    int ret = -1;
    char *errstr = NULL;

    trav = graph->first;

    while (trav) {
        if (list_empty(&trav->volume_options)) {
            trav = trav->next;
            continue;
        }

        ret = xlator_options_validate(trav, trav->options, &errstr);
        if (ret) {
            gf_msg(trav->name, GF_LOG_ERROR, 0, LG_MSG_VALIDATION_FAILED,
                   "validation failed: "
                   "%s",
                   errstr);
            return ret;
        }
        trav = trav->next;
    }

    return 0;
}

int
glusterfs_graph_init(glusterfs_graph_t *graph)
{
    xlator_t *trav = NULL;
    int ret = -1;

    trav = graph->first;

    while (trav) {
        ret = xlator_init(trav);
        if (ret) {
            gf_msg(trav->name, GF_LOG_ERROR, 0, LG_MSG_TRANSLATOR_INIT_FAILED,
                   "initializing translator failed");
            return ret;
        }
        trav = trav->next;
    }

    return 0;
}

int
glusterfs_graph_deactivate(glusterfs_graph_t *graph)
{
    xlator_t *top = NULL;

    if (graph == NULL)
        goto out;

    top = graph->top;
    xlator_tree_fini(top);
out:
    return 0;
}

static int
_log_if_unknown_option(dict_t *dict, char *key, data_t *value, void *data)
{
    volume_option_t *found = NULL;
    xlator_t *xl = NULL;

    xl = data;

    found = xlator_volume_option_get(xl, key);

    if (!found) {
        gf_msg(xl->name, GF_LOG_DEBUG, 0, LG_MSG_XLATOR_OPTION_INVALID,
               "option '%s' is not recognized", key);
    }

    return 0;
}

static void
_xlator_check_unknown_options(xlator_t *xl, void *data)
{
    dict_foreach(xl->options, _log_if_unknown_option, xl);
}

static int
glusterfs_graph_unknown_options(glusterfs_graph_t *graph)
{
    xlator_foreach(graph->first, _xlator_check_unknown_options, NULL);
    return 0;
}

static void
fill_uuid(char *uuid, int size, struct timeval tv)
{
    char hostname[50] = {
        0,
    };
    char now_str[GF_TIMESTR_SIZE];

    if (gethostname(hostname, sizeof(hostname) - 1) != 0) {
        gf_msg("graph", GF_LOG_ERROR, errno, LG_MSG_GETHOSTNAME_FAILED,
               "gethostname failed");
        hostname[sizeof(hostname) - 1] = '\0';
    }

    gf_time_fmt_tv(now_str, sizeof now_str, &tv, gf_timefmt_dirent);
    snprintf(uuid, size, "%s-%d-%s", hostname, getpid(), now_str);

    return;
}

static int
glusterfs_graph_settop(glusterfs_graph_t *graph, char *volume_name,
                       gf_boolean_t exact_match)
{
    int ret = -1;
    xlator_t *trav = NULL;

    if (!volume_name || !exact_match) {
        graph->top = graph->first;
        ret = 0;
    } else {
        for (trav = graph->first; trav; trav = trav->next) {
            if (strcmp(trav->name, volume_name) == 0) {
                graph->top = trav;
                ret = 0;
                break;
            }
        }
    }

    return ret;
}

int
glusterfs_graph_parent_up(glusterfs_graph_t *graph)
{
    xlator_t *trav = NULL;
    int ret = -1;

    trav = graph->first;

    while (trav) {
        if (!xlator_has_parent(trav)) {
            ret = xlator_notify(trav, GF_EVENT_PARENT_UP, trav);
        }

        if (ret)
            break;

        trav = trav->next;
    }

    return ret;
}

int
glusterfs_graph_prepare(glusterfs_graph_t *graph, glusterfs_ctx_t *ctx,
                        char *volume_name)
{
    xlator_t *trav = NULL;
    int ret = 0;

    /* XXX: CHECKSUM */

    /* XXX: attach to -n volname */
    /* A '/' in the volume name suggests brick multiplexing is used, find
     * the top of the (sub)graph. The volname MUST match the subvol in this
     * case. In other cases (like for gfapi) the default top for the
     * (sub)graph is ok. */
    if (!volume_name) {
        /* GlusterD does not pass a volume_name */
        ret = glusterfs_graph_settop(graph, volume_name, _gf_false);
    } else if (strncmp(volume_name, "/snaps/", 7) == 0) {
        /* snap shots have their top xlator named like "/snaps/..."  */
        ret = glusterfs_graph_settop(graph, volume_name, _gf_false);
    } else if (volume_name[0] == '/') {
        /* brick multiplexing passes the brick path */
        ret = glusterfs_graph_settop(graph, volume_name, _gf_true);
    } else {
        ret = glusterfs_graph_settop(graph, volume_name, _gf_false);
    }

    if (ret) {
        gf_msg("graph", GF_LOG_ERROR, EINVAL, LG_MSG_GRAPH_ERROR,
               "glusterfs graph settop failed");
        errno = EINVAL;
        return -1;
    }

    /* XXX: WORM VOLUME */
    ret = glusterfs_graph_worm(graph, ctx);
    if (ret) {
        gf_msg("graph", GF_LOG_ERROR, 0, LG_MSG_GRAPH_ERROR,
               "glusterfs graph worm failed");
        return -1;
    }
    ret = glusterfs_graph_acl(graph, ctx);
    if (ret) {
        gf_msg("graph", GF_LOG_ERROR, 0, LG_MSG_GRAPH_ERROR,
               "glusterfs graph ACL failed");
        return -1;
    }

    /* XXX: MAC COMPAT */
    ret = glusterfs_graph_mac_compat(graph, ctx);
    if (ret) {
        gf_msg("graph", GF_LOG_ERROR, 0, LG_MSG_GRAPH_ERROR,
               "glusterfs graph mac compat failed");
        return -1;
    }

    /* XXX: gfid-access */
    ret = glusterfs_graph_gfid_access(graph, ctx);
    if (ret) {
        gf_msg("graph", GF_LOG_ERROR, 0, LG_MSG_GRAPH_ERROR,
               "glusterfs graph 'gfid-access' failed");
        return -1;
    }

    /* XXX: topmost xlator */
    ret = glusterfs_graph_meta(graph, ctx);
    if (ret) {
        gf_msg("graph", GF_LOG_ERROR, 0, LG_MSG_GRAPH_ERROR,
               "glusterfs graph meta failed");
        return -1;
    }

    /* XXX: this->ctx setting */
    for (trav = graph->first; trav; trav = trav->next) {
        trav->ctx = ctx;
    }

    /* XXX: DOB setting */
    gettimeofday(&graph->dob, NULL);

    fill_uuid(graph->graph_uuid, sizeof(graph->graph_uuid), graph->dob);

    graph->id = ctx->graph_id++;

    /* XXX: --xlator-option additions */
    gf_add_cmdline_options(graph, &ctx->cmd_args);

    return 0;
}

static xlator_t *
glusterfs_root(glusterfs_graph_t *graph)
{
    return graph->first;
}

static int
glusterfs_is_leaf(xlator_t *xl)
{
    int ret = 0;

    if (!xl->children)
        ret = 1;

    return ret;
}

static uint32_t
glusterfs_count_leaves(xlator_t *xl)
{
    int n = 0;
    xlator_list_t *list = NULL;

    if (glusterfs_is_leaf(xl))
        n = 1;
    else
        for (list = xl->children; list; list = list->next)
            n += glusterfs_count_leaves(list->xlator);

    return n;
}

int
glusterfs_get_leaf_count(glusterfs_graph_t *graph)
{
    return graph->leaf_count;
}

static int
_glusterfs_leaf_position(xlator_t *tgt, int *id, xlator_t *xl)
{
    xlator_list_t *list = NULL;
    int found = 0;

    if (xl == tgt)
        found = 1;
    else if (glusterfs_is_leaf(xl))
        *id += 1;
    else
        for (list = xl->children; !found && list; list = list->next)
            found = _glusterfs_leaf_position(tgt, id, list->xlator);

    return found;
}

int
glusterfs_leaf_position(xlator_t *tgt)
{
    xlator_t *root = NULL;
    int pos = 0;

    root = glusterfs_root(tgt->graph);

    if (!_glusterfs_leaf_position(tgt, &pos, root))
        pos = -1;

    return pos;
}

static int
_glusterfs_reachable_leaves(xlator_t *base, xlator_t *xl, dict_t *leaves)
{
    xlator_list_t *list = NULL;
    int err = 1;
    int pos = 0;
    char *strpos = NULL;

    if (glusterfs_is_leaf(xl)) {
        pos = glusterfs_leaf_position(xl);
        if (pos < 0)
            goto out;

        err = gf_asprintf(&strpos, "%d", pos);

        if (err >= 0) {
            err = dict_set_static_ptr(leaves, strpos, base);
            GF_FREE(strpos);
        }
    } else {
        for (err = 0, list = xl->children; !err && list; list = list->next)
            err = _glusterfs_reachable_leaves(base, list->xlator, leaves);
    }

out:
    return err;
}

/*
 * This function determines which leaves are children (or grandchildren)
 * of the given base. The base may have multiple sub volumes. Each sub
 * volumes in turn may have sub volumes.. until the leaves are reached.
 * Each leaf is numbered 1,2,3,...etc.
 *
 * The base translator calls this function to see which of *its* subvolumes
 * it would forward an FOP to, to *get to* a particular leaf.
 * That information is built into the "leaves" dictionary.
 * key:destination leaf# -> value:base subvolume xlator.
 */

int
glusterfs_reachable_leaves(xlator_t *base, dict_t *leaves)
{
    xlator_list_t *list = NULL;
    int err = 0;

    for (list = base->children; !err && list; list = list->next)
        err = _glusterfs_reachable_leaves(list->xlator, list->xlator, leaves);

    return err;
}

int
glusterfs_graph_activate(glusterfs_graph_t *graph, glusterfs_ctx_t *ctx)
{
    int ret = 0;
    xlator_t *root = NULL;

    root = glusterfs_root(graph);

    graph->leaf_count = glusterfs_count_leaves(root);

    /* XXX: all xlator options validation */
    ret = glusterfs_graph_validate_options(graph);
    if (ret) {
        gf_msg("graph", GF_LOG_ERROR, 0, LG_MSG_VALIDATION_FAILED,
               "validate options failed");
        return ret;
    }

    /* XXX: perform init () */
    ret = glusterfs_graph_init(graph);
    if (ret) {
        gf_msg("graph", GF_LOG_ERROR, 0, LG_MSG_GRAPH_INIT_FAILED,
               "init failed");
        return ret;
    }

    ret = glusterfs_graph_unknown_options(graph);
    if (ret) {
        gf_msg("graph", GF_LOG_ERROR, 0, LG_MSG_UNKNOWN_OPTIONS_FAILED,
               "unknown options "
               "failed");
        return ret;
    }

    /* XXX: log full graph (_gf_dump_details) */

    list_add(&graph->list, &ctx->graphs);
    ctx->active = graph;

    /* XXX: attach to master and set active pointer */
    if (ctx->primary) {
        ret = xlator_notify(ctx->primary, GF_EVENT_GRAPH_NEW, graph);
        if (ret) {
            gf_msg("graph", GF_LOG_ERROR, 0, LG_MSG_EVENT_NOTIFY_FAILED,
                   "graph new notification failed");
            return ret;
        }
        ((xlator_t *)ctx->primary)->next = graph->top;
    }

    /* XXX: perform parent up */
    ret = glusterfs_graph_parent_up(graph);
    if (ret) {
        gf_msg("graph", GF_LOG_ERROR, 0, LG_MSG_EVENT_NOTIFY_FAILED,
               "parent up notification failed");
        return ret;
    }

    return 0;
}

int
xlator_equal_rec(xlator_t *xl1, xlator_t *xl2)
{
    xlator_list_t *trav1 = NULL;
    xlator_list_t *trav2 = NULL;
    int ret = 0;

    if (xl1 == NULL || xl2 == NULL) {
        gf_msg_debug("xlator", 0, "invalid argument");
        return -1;
    }

    trav1 = xl1->children;
    trav2 = xl2->children;

    while (trav1 && trav2) {
        ret = xlator_equal_rec(trav1->xlator, trav2->xlator);
        if (ret) {
            gf_msg_debug("glusterfsd-mgmt", 0,
                         "xlators children "
                         "not equal");
            goto out;
        }

        trav1 = trav1->next;
        trav2 = trav2->next;
    }

    if (trav1 || trav2) {
        ret = -1;
        goto out;
    }

    if (strcmp(xl1->name, xl2->name)) {
        ret = -1;
        goto out;
    }

    /* type could have changed even if xlator names match,
       e.g cluster/distribute and cluster/nufa share the same
       xlator name
    */
    if (strcmp(xl1->type, xl2->type)) {
        ret = -1;
        goto out;
    }
out:
    return ret;
}

gf_boolean_t
is_graph_topology_equal(glusterfs_graph_t *graph1, glusterfs_graph_t *graph2)
{
    xlator_t *trav1 = NULL;
    xlator_t *trav2 = NULL;
    gf_boolean_t ret = _gf_true;
    xlator_list_t *ltrav;

    trav1 = graph1->first;
    trav2 = graph2->first;

    if (strcmp(trav2->type, "protocol/server") == 0) {
        trav2 = trav2->children->xlator;
        for (ltrav = trav1->children; ltrav; ltrav = ltrav->next) {
            trav1 = ltrav->xlator;
            if (!trav1->cleanup_starting && !strcmp(trav1->name, trav2->name)) {
                break;
            }
        }
        if (!ltrav) {
            return _gf_false;
        }
    }

    ret = xlator_equal_rec(trav1, trav2);

    if (ret) {
        gf_msg_debug("glusterfsd-mgmt", 0, "graphs are not equal");
        ret = _gf_false;
        goto out;
    }

    ret = _gf_true;
    gf_msg_debug("glusterfsd-mgmt", 0, "graphs are equal");

out:
    return ret;
}

/* Function has 3types of return value 0, -ve , 1
 *   return 0          =======> reconfiguration of options has succeeded
 *   return 1          =======> the graph has to be reconstructed and all the
 * xlators should be inited return -1(or -ve) =======> Some Internal Error
 * occurred during the operation
 */
int
glusterfs_volfile_reconfigure(FILE *newvolfile_fp, glusterfs_ctx_t *ctx)
{
    glusterfs_graph_t *oldvolfile_graph = NULL;
    glusterfs_graph_t *newvolfile_graph = NULL;

    int ret = -1;

    if (!ctx) {
        gf_msg("glusterfsd-mgmt", GF_LOG_ERROR, 0, LG_MSG_CTX_NULL,
               "ctx is NULL");
        goto out;
    }

    oldvolfile_graph = ctx->active;
    if (!oldvolfile_graph) {
        ret = 1;
        goto out;
    }

    newvolfile_graph = glusterfs_graph_construct(newvolfile_fp);

    if (!newvolfile_graph) {
        goto out;
    }

    glusterfs_graph_prepare(newvolfile_graph, ctx, ctx->cmd_args.volume_name);

    if (!is_graph_topology_equal(oldvolfile_graph, newvolfile_graph)) {
        ret = 1;
        gf_msg_debug("glusterfsd-mgmt", 0,
                     "Graph topology not "
                     "equal(should call INIT)");
        goto out;
    }

    gf_msg_debug("glusterfsd-mgmt", 0,
                 "Only options have changed in the"
                 " new graph");

    ret = glusterfs_graph_reconfigure(oldvolfile_graph, newvolfile_graph);
    if (ret) {
        gf_msg_debug("glusterfsd-mgmt", 0,
                     "Could not reconfigure "
                     "new options in old graph");
        goto out;
    }

    ret = 0;
out:

    if (newvolfile_graph)
        glusterfs_graph_destroy(newvolfile_graph);

    return ret;
}

/* This function need to remove. This added to support gfapi volfile
 * reconfigure.
 */

int
gf_volfile_reconfigure(int oldvollen, FILE *newvolfile_fp, glusterfs_ctx_t *ctx,
                       const char *oldvolfile)
{
    glusterfs_graph_t *oldvolfile_graph = NULL;
    glusterfs_graph_t *newvolfile_graph = NULL;
    FILE *oldvolfile_fp = NULL;
    /*Since the function mkstemp() replaces XXXXXX,
     * assigning it to a variable
     */
    char temp_file[] = "/tmp/temp_vol_file_XXXXXX";
    gf_boolean_t active_graph_found = _gf_true;

    int ret = -1;
    int u_ret = -1;
    int file_desc = -1;

    if (!oldvollen) {
        ret = 1;  // Has to call INIT for the whole graph
        goto out;
    }

    if (!ctx) {
        gf_msg("glusterfsd-mgmt", GF_LOG_ERROR, 0, LG_MSG_CTX_NULL,
               "ctx is NULL");
        goto out;
    }

    oldvolfile_graph = ctx->active;
    if (!oldvolfile_graph) {
        active_graph_found = _gf_false;
        gf_msg("glusterfsd-mgmt", GF_LOG_ERROR, 0, LG_MSG_ACTIVE_GRAPH_NULL,
               "glusterfs_ctx->active is NULL");

        /* coverity[secure_temp] mkstemp uses 0600 as the mode and is safe */
        file_desc = mkstemp(temp_file);
        if (file_desc < 0) {
            gf_msg("glusterfsd-mgmt", GF_LOG_ERROR, errno,
                   LG_MSG_TMPFILE_CREATE_FAILED,
                   "Unable to "
                   "create temporary volfile");
            goto out;
        }

        /*Calling unlink so that when the file is closed or program
         *terminates the tempfile is deleted.
         */
        u_ret = sys_unlink(temp_file);

        if (u_ret < 0) {
            gf_msg("glusterfsd-mgmt", GF_LOG_ERROR, errno,
                   LG_MSG_TMPFILE_DELETE_FAILED,
                   "Temporary file"
                   " delete failed.");
            sys_close(file_desc);
            goto out;
        }

        oldvolfile_fp = fdopen(file_desc, "w+b");
        if (!oldvolfile_fp)
            goto out;

        fwrite(oldvolfile, oldvollen, 1, oldvolfile_fp);
        fflush(oldvolfile_fp);
        if (ferror(oldvolfile_fp)) {
            goto out;
        }

        oldvolfile_graph = glusterfs_graph_construct(oldvolfile_fp);
        if (!oldvolfile_graph)
            goto out;
    }

    newvolfile_graph = glusterfs_graph_construct(newvolfile_fp);
    if (!newvolfile_graph) {
        goto out;
    }

    glusterfs_graph_prepare(newvolfile_graph, ctx, ctx->cmd_args.volume_name);

    if (!is_graph_topology_equal(oldvolfile_graph, newvolfile_graph)) {
        ret = 1;
        gf_msg_debug("glusterfsd-mgmt", 0,
                     "Graph topology not "
                     "equal(should call INIT)");
        goto out;
    }

    gf_msg_debug("glusterfsd-mgmt", 0,
                 "Only options have changed in the"
                 " new graph");

    /* */
    ret = glusterfs_graph_reconfigure(oldvolfile_graph, newvolfile_graph);
    if (ret) {
        gf_msg_debug("glusterfsd-mgmt", 0,
                     "Could not reconfigure "
                     "new options in old graph");
        goto out;
    }

    ret = 0;
out:
    if (oldvolfile_fp)
        fclose(oldvolfile_fp);

    /*  Do not simply destroy the old graph here. If the oldgraph
        is constructed here in this function itself instead of getting
        it from ctx->active (which happens only of ctx->active is NULL),
        then destroy the old graph. If some i/o is still happening in
        the old graph and the old graph is obtained from ctx->active,
        then destroying the graph will cause problems.
    */
    if (!active_graph_found && oldvolfile_graph)
        glusterfs_graph_destroy(oldvolfile_graph);
    if (newvolfile_graph)
        glusterfs_graph_destroy(newvolfile_graph);

    return ret;
}

int
glusterfs_graph_reconfigure(glusterfs_graph_t *oldgraph,
                            glusterfs_graph_t *newgraph)
{
    xlator_t *old_xl = NULL;
    xlator_t *new_xl = NULL;
    xlator_list_t *trav;

    GF_ASSERT(oldgraph);
    GF_ASSERT(newgraph);

    old_xl = oldgraph->first;
    while (old_xl->is_autoloaded) {
        old_xl = old_xl->children->xlator;
    }

    new_xl = newgraph->first;
    while (new_xl->is_autoloaded) {
        new_xl = new_xl->children->xlator;
    }

    if (strcmp(old_xl->type, "protocol/server") != 0) {
        return xlator_tree_reconfigure(old_xl, new_xl);
    }

    /* Some options still need to be handled by the server translator. */
    if (old_xl->reconfigure) {
        old_xl->reconfigure(old_xl, new_xl->options);
    }

    (void)copy_opts_to_child(new_xl, FIRST_CHILD(new_xl), "*auth*");
    new_xl = FIRST_CHILD(new_xl);

    for (trav = old_xl->children; trav; trav = trav->next) {
        if (!trav->xlator->cleanup_starting &&
            !strcmp(trav->xlator->name, new_xl->name)) {
            return xlator_tree_reconfigure(trav->xlator, new_xl);
        }
    }

    return -1;
}

int
glusterfs_graph_destroy_residual(glusterfs_graph_t *graph)
{
    int ret = -1;

    if (graph == NULL)
        return ret;

    ret = xlator_tree_free_memacct(graph->first);

    list_del_init(&graph->list);
    pthread_mutex_destroy(&graph->mutex);
    pthread_cond_destroy(&graph->child_down_cond);
    GF_FREE(graph);

    return ret;
}

/* This function destroys all the xlator members except for the
 * xlator strcuture and its mem accounting field.
 *
 * If otherwise, it would destroy the master xlator object as well
 * its mem accounting, which would mean after calling glusterfs_graph_destroy()
 * there cannot be any reference to GF_FREE() from the master xlator, this is
 * not possible because of the following dependencies:
 * - glusterfs_ctx_t will have mem pools allocated by the master xlators
 * - xlator objects will have references to those mem pools(g: dict)
 *
 * Ordering the freeing in any of the order will also not solve the dependency:
 * - Freeing xlator objects(including memory accounting) before mem pools
 *   destruction will mean not use GF_FREE while destroying mem pools.
 * - Freeing mem pools and then destroying xlator objects would lead to crashes
 *   when xlator tries to unref dict or other mem pool objects.
 *
 * Hence the way chosen out of this interdependency is to split xlator object
 * free into two stages:
 * - Free all the xlator members excpet for its mem accounting structure
 * - Free all the mem accouting structures of xlator along with the xlator
 *   object itself.
 */
int
glusterfs_graph_destroy(glusterfs_graph_t *graph)
{
    int ret = 0;

    GF_VALIDATE_OR_GOTO("graph", graph, out);

    ret = xlator_tree_free_members(graph->first);

    ret = glusterfs_graph_destroy_residual(graph);
out:
    return ret;
}

int
glusterfs_graph_fini(glusterfs_graph_t *graph)
{
    xlator_t *trav = NULL;

    trav = graph->first;

    while (trav) {
        if (trav->init_succeeded) {
            trav->cleanup_starting = 1;
            trav->fini(trav);
            if (trav->local_pool) {
                mem_pool_destroy(trav->local_pool);
                trav->local_pool = NULL;
            }
            if (trav->itable) {
                inode_table_destroy(trav->itable);
                trav->itable = NULL;
            }
            trav->init_succeeded = 0;
        }
        trav = trav->next;
    }

    return 0;
}

int
glusterfs_graph_attach(glusterfs_graph_t *orig_graph, char *path,
                       glusterfs_graph_t **newgraph)
{
    xlator_t *this = THIS;
    FILE *fp;
    glusterfs_graph_t *graph;
    xlator_t *xl;
    char *volfile_id = NULL;
    char *volfile_content = NULL;
    struct stat stbuf = {
        0,
    };
    size_t file_len = -1;
    gf_volfile_t *volfile_obj = NULL;
    int ret = -1;
    char sha256_hash[SHA256_DIGEST_LENGTH] = {
        0,
    };

    if (!orig_graph) {
        return -EINVAL;
    }

    ret = sys_stat(path, &stbuf);
    if (ret < 0) {
        gf_log(THIS->name, GF_LOG_ERROR, "Unable to stat %s (%s)", path,
               strerror(errno));
        return -EINVAL;
    }

    file_len = stbuf.st_size;
    volfile_content = GF_MALLOC(file_len + 1, gf_common_mt_char);
    if (!volfile_content)
        return -ENOMEM;

    fp = fopen(path, "r");
    if (!fp) {
        gf_log(THIS->name, GF_LOG_WARNING, "oops, %s disappeared on us", path);
        GF_FREE(volfile_content);
        return -EIO;
    }

    ret = fread(volfile_content, sizeof(char), file_len, fp);
    if (ret == file_len) {
        glusterfs_compute_sha256((const unsigned char *)volfile_content,
                                 file_len, sha256_hash);
    } else {
        gf_log(THIS->name, GF_LOG_ERROR,
               "read failed on path %s. File size=%" GF_PRI_SIZET
               "read size=%d",
               path, file_len, ret);
        GF_FREE(volfile_content);
        fclose(fp);
        return -EIO;
    }

    GF_FREE(volfile_content);

    graph = glusterfs_graph_construct(fp);
    fclose(fp);
    if (!graph) {
        gf_log(this->name, GF_LOG_WARNING, "could not create graph from %s",
               path);
        return -EIO;
    }

    /*
     * If there's a server translator on top, we want whatever's below
     * that.
     */
    xl = graph->first;
    if (strcmp(xl->type, "protocol/server") == 0) {
        (void)copy_opts_to_child(xl, FIRST_CHILD(xl), "*auth*");
        xl = FIRST_CHILD(xl);
    }
    graph->first = xl;
    *newgraph = graph;

    volfile_id = strstr(path, "/snaps/");
    if (!volfile_id) {
        volfile_id = rindex(path, '/');
        if (volfile_id) {
            ++volfile_id;
        }
    }
    if (volfile_id) {
        xl->volfile_id = gf_strdup(volfile_id);
        /* There's a stray ".vol" at the end. */
        xl->volfile_id[strlen(xl->volfile_id) - 4] = '\0';
    }

    /* TODO memory leaks everywhere need to free graph in case of error */
    if (glusterfs_graph_prepare(graph, this->ctx, xl->name)) {
        gf_log(this->name, GF_LOG_WARNING,
               "failed to prepare graph for xlator %s", xl->name);
        return -EIO;
    } else if (glusterfs_graph_init(graph)) {
        gf_log(this->name, GF_LOG_WARNING,
               "failed to initialize graph for xlator %s", xl->name);
        return -EIO;
    } else if (glusterfs_xlator_link(orig_graph->top, graph->top)) {
        gf_log(this->name, GF_LOG_WARNING,
               "failed to link the graphs for xlator %s ", xl->name);
        return -EIO;
    }

    if (!volfile_obj) {
        volfile_obj = GF_CALLOC(1, sizeof(gf_volfile_t), gf_common_volfile_t);
        if (!volfile_obj) {
            return -EIO;
        }
    }

    INIT_LIST_HEAD(&volfile_obj->volfile_list);
    snprintf(volfile_obj->vol_id, sizeof(volfile_obj->vol_id), "%s",
             xl->volfile_id);
    memcpy(volfile_obj->volfile_checksum, sha256_hash,
           sizeof(volfile_obj->volfile_checksum));
    list_add(&volfile_obj->volfile_list, &this->ctx->volfile_list);

    return 0;
}
int
glusterfs_muxsvc_cleanup_parent(glusterfs_ctx_t *ctx,
                                glusterfs_graph_t *parent_graph)
{
    if (parent_graph) {
        if (parent_graph->first) {
            xlator_destroy(parent_graph->first);
        }
        ctx->active = NULL;
        GF_FREE(parent_graph);
        parent_graph = NULL;
    }
    return 0;
}

void *
glusterfs_graph_cleanup(void *arg)
{
    glusterfs_graph_t *graph = NULL;
    glusterfs_ctx_t *ctx = THIS->ctx;
    int ret = -1;
    graph = arg;

    if (!graph)
        return NULL;

    /* To destroy the graph, fitst sent a GF_EVENT_PARENT_DOWN
     * Then wait for GF_EVENT_CHILD_DOWN to get on the top
     * xl. Once we have GF_EVENT_CHILD_DOWN event, then proceed
     * to fini.
     *
     * During fini call, this will take a last unref on rpc and
     * rpc_transport_object.
     */
    if (graph->first)
        default_notify(graph->first, GF_EVENT_PARENT_DOWN, graph->first);

    ret = pthread_mutex_lock(&graph->mutex);
    if (ret != 0) {
        gf_msg("glusterfs", GF_LOG_ERROR, EAGAIN, LG_MSG_GRAPH_CLEANUP_FAILED,
               "Failed to acquire a lock");
        goto out;
    }
    /* check and wait for CHILD_DOWN for top xlator*/
    while (graph->used) {
        ret = pthread_cond_wait(&graph->child_down_cond, &graph->mutex);
        if (ret != 0)
            gf_msg("glusterfs", GF_LOG_INFO, 0, LG_MSG_GRAPH_CLEANUP_FAILED,
                   "cond wait failed ");
    }

    ret = pthread_mutex_unlock(&graph->mutex);
    if (ret != 0) {
        gf_msg("glusterfs", GF_LOG_ERROR, EAGAIN, LG_MSG_GRAPH_CLEANUP_FAILED,
               "Failed to release a lock");
    }

    /* Though we got a child down on top xlator, we have to wait until
     * all the notifier to exit. Because there should not be any threads
     * that access xl variables.
     */
    pthread_mutex_lock(&ctx->notify_lock);
    {
        while (ctx->notifying)
            pthread_cond_wait(&ctx->notify_cond, &ctx->notify_lock);
    }
    pthread_mutex_unlock(&ctx->notify_lock);

    pthread_mutex_lock(&ctx->cleanup_lock);
    {
        glusterfs_graph_fini(graph);
        glusterfs_graph_destroy(graph);
    }
    pthread_mutex_unlock(&ctx->cleanup_lock);
out:
    return NULL;
}

glusterfs_graph_t *
glusterfs_muxsvc_setup_parent_graph(glusterfs_ctx_t *ctx, char *name,
                                    char *type)
{
    glusterfs_graph_t *parent_graph = NULL;
    xlator_t *ixl = NULL;
    int ret = -1;
    parent_graph = GF_CALLOC(1, sizeof(*parent_graph),
                             gf_common_mt_glusterfs_graph_t);
    if (!parent_graph)
        goto out;

    INIT_LIST_HEAD(&parent_graph->list);

    ctx->active = parent_graph;
    ixl = GF_CALLOC(1, sizeof(*ixl), gf_common_mt_xlator_t);
    if (!ixl)
        goto out;

    ixl->ctx = ctx;
    ixl->graph = parent_graph;
    ixl->options = dict_new();
    if (!ixl->options)
        goto out;

    ixl->name = gf_strdup(name);
    if (!ixl->name)
        goto out;

    ixl->is_autoloaded = 1;

    if (xlator_set_type(ixl, type) == -1) {
        gf_msg("glusterfs", GF_LOG_ERROR, EINVAL, LG_MSG_GRAPH_SETUP_FAILED,
               "%s (%s) set type failed", name, type);
        goto out;
    }

    glusterfs_graph_set_first(parent_graph, ixl);
    parent_graph->top = ixl;
    ixl = NULL;

    gettimeofday(&parent_graph->dob, NULL);
    fill_uuid(parent_graph->graph_uuid, 128, parent_graph->dob);
    parent_graph->id = ctx->graph_id++;
    ret = 0;
out:
    if (ixl)
        xlator_destroy(ixl);

    if (ret) {
        glusterfs_muxsvc_cleanup_parent(ctx, parent_graph);
        parent_graph = NULL;
    }
    return parent_graph;
}

int
glusterfs_svc_mux_pidfile_cleanup(gf_volfile_t *volfile_obj)
{
    if (!volfile_obj || !volfile_obj->pidfp)
        return 0;

    gf_msg_trace("glusterfsd", 0, "pidfile %s cleanup", volfile_obj->vol_id);

    lockf(fileno(volfile_obj->pidfp), F_ULOCK, 0);
    fclose(volfile_obj->pidfp);
    volfile_obj->pidfp = NULL;

    return 0;
}

int
glusterfs_process_svc_detach(glusterfs_ctx_t *ctx, gf_volfile_t *volfile_obj)
{
    xlator_t *last_xl = NULL;
    glusterfs_graph_t *graph = NULL;
    glusterfs_graph_t *parent_graph = NULL;
    pthread_t clean_graph = {
        0,
    };
    int ret = -1;
    xlator_t *xl = NULL;

    if (!ctx || !ctx->active || !volfile_obj)
        goto out;

    pthread_mutex_lock(&ctx->cleanup_lock);
    {
        parent_graph = ctx->active;
        graph = volfile_obj->graph;
        if (!graph)
            goto unlock;
        if (graph->first)
            xl = graph->first;

        last_xl = graph->last_xl;
        if (last_xl)
            last_xl->next = NULL;
        if (!xl || xl->cleanup_starting)
            goto unlock;

        xl->cleanup_starting = 1;
        gf_msg("mgmt", GF_LOG_INFO, 0, LG_MSG_GRAPH_DETACH_STARTED,
               "detaching child %s", volfile_obj->vol_id);

        list_del_init(&volfile_obj->volfile_list);
        glusterfs_mux_xlator_unlink(parent_graph->top, xl);
        glusterfs_svc_mux_pidfile_cleanup(volfile_obj);
        parent_graph->last_xl = glusterfs_get_last_xlator(parent_graph);
        parent_graph->xl_count -= graph->xl_count;
        parent_graph->leaf_count -= graph->leaf_count;
        parent_graph->id++;
        ret = 0;
    }
unlock:
    pthread_mutex_unlock(&ctx->cleanup_lock);
out:
    if (!ret) {
        list_del_init(&volfile_obj->volfile_list);
        if (graph) {
            ret = gf_thread_create_detached(
                &clean_graph, glusterfs_graph_cleanup, graph, "graph_clean");
            if (ret) {
                gf_msg("glusterfs", GF_LOG_ERROR, EINVAL,
                       LG_MSG_GRAPH_CLEANUP_FAILED,
                       "%s failed to create clean "
                       "up thread",
                       volfile_obj->vol_id);
                ret = 0;
            }
        }
        GF_FREE(volfile_obj);
    }
    return ret;
}

int
glusterfs_svc_mux_pidfile_setup(gf_volfile_t *volfile_obj, const char *pid_file)
{
    int ret = -1;
    FILE *pidfp = NULL;

    if (!pid_file || !volfile_obj)
        goto out;

    if (volfile_obj->pidfp) {
        ret = 0;
        goto out;
    }
    pidfp = fopen(pid_file, "a+");
    if (!pidfp) {
        goto out;
    }
    volfile_obj->pidfp = pidfp;

    ret = lockf(fileno(pidfp), F_TLOCK, 0);
    if (ret) {
        ret = 0;
        goto out;
    }
out:
    return ret;
}

int
glusterfs_svc_mux_pidfile_update(gf_volfile_t *volfile_obj,
                                 const char *pid_file, pid_t pid)
{
    int ret = 0;
    FILE *pidfp = NULL;
    int old_pid;

    if (!volfile_obj->pidfp) {
        ret = glusterfs_svc_mux_pidfile_setup(volfile_obj, pid_file);
        if (ret == -1)
            goto out;
    }
    pidfp = volfile_obj->pidfp;
    ret = fscanf(pidfp, "%d", &old_pid);
    if (ret <= 0) {
        goto update;
    }
    if (old_pid == pid) {
        ret = 0;
        goto out;
    } else {
        gf_msg("mgmt", GF_LOG_INFO, 0, LG_MSG_GRAPH_ATTACH_PID_FILE_UPDATED,
               "Old pid=%d found in pidfile %s. Cleaning the old pid and "
               "Updating new pid=%d",
               old_pid, pid_file, pid);
    }
update:
    ret = sys_ftruncate(fileno(pidfp), 0);
    if (ret) {
        gf_msg("glusterfsd", GF_LOG_ERROR, errno,
               LG_MSG_GRAPH_ATTACH_PID_FILE_UPDATED,
               "pidfile %s truncation failed", pid_file);
        goto out;
    }

    ret = fprintf(pidfp, "%d\n", pid);
    if (ret <= 0) {
        gf_msg("glusterfsd", GF_LOG_ERROR, errno,
               LG_MSG_GRAPH_ATTACH_PID_FILE_UPDATED, "pidfile %s write failed",
               pid_file);
        goto out;
    }

    ret = fflush(pidfp);
    if (ret) {
        gf_msg("glusterfsd", GF_LOG_ERROR, errno,
               LG_MSG_GRAPH_ATTACH_PID_FILE_UPDATED, "pidfile %s write failed",
               pid_file);
        goto out;
    }
out:
    return ret;
}

int
glusterfs_update_mux_pid(dict_t *dict, gf_volfile_t *volfile_obj)
{
    char *file = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("graph", dict, out);
    GF_VALIDATE_OR_GOTO("graph", volfile_obj, out);

    ret = dict_get_str(dict, "pidfile", &file);
    if (ret < 0) {
        gf_msg("mgmt", GF_LOG_ERROR, EINVAL, LG_MSG_GRAPH_SETUP_FAILED,
               "Failed to get pidfile from dict for  volfile_id=%s",
               volfile_obj->vol_id);
    }

    ret = glusterfs_svc_mux_pidfile_update(volfile_obj, file, getpid());
    if (ret < 0) {
        ret = -1;
        gf_msg("mgmt", GF_LOG_ERROR, EINVAL, LG_MSG_GRAPH_SETUP_FAILED,
               "Failed to update "
               "the pidfile for volfile_id=%s",
               volfile_obj->vol_id);

        goto out;
    }

    if (ret == 1)
        gf_msg("mgmt", GF_LOG_INFO, 0, LG_MSG_GRAPH_ATTACH_PID_FILE_UPDATED,
               "PID %d updated in pidfile=%s", getpid(), file);
    ret = 0;
out:
    return ret;
}
int
glusterfs_process_svc_attach_volfp(glusterfs_ctx_t *ctx, FILE *fp,
                                   char *volfile_id, char *checksum,
                                   dict_t *dict)
{
    glusterfs_graph_t *graph = NULL;
    glusterfs_graph_t *parent_graph = NULL;
    glusterfs_graph_t *clean_graph = NULL;
    int ret = -1;
    xlator_t *xl = NULL;
    xlator_t *last_xl = NULL;
    gf_volfile_t *volfile_obj = NULL;
    pthread_t thread_id = {
        0,
    };

    if (!ctx)
        goto out;
    parent_graph = ctx->active;
    graph = glusterfs_graph_construct(fp);
    if (!graph) {
        gf_msg("glusterfsd", GF_LOG_ERROR, EINVAL, LG_MSG_GRAPH_ATTACH_FAILED,
               "failed to construct the graph");
        goto out;
    }
    graph->parent_down = 0;
    graph->last_xl = glusterfs_get_last_xlator(graph);

    for (xl = graph->first; xl; xl = xl->next) {
        if (strcmp(xl->type, "mount/fuse") == 0) {
            gf_msg("glusterfsd", GF_LOG_ERROR, EINVAL,
                   LG_MSG_GRAPH_ATTACH_FAILED,
                   "fuse xlator cannot be specified in volume file");
            goto out;
        }
    }

    graph->leaf_count = glusterfs_count_leaves(glusterfs_root(graph));
    xl = graph->first;
    /* TODO memory leaks everywhere need to free graph in case of error */
    if (glusterfs_graph_prepare(graph, ctx, xl->name)) {
        gf_msg("glusterfsd", GF_LOG_WARNING, EINVAL, LG_MSG_GRAPH_ATTACH_FAILED,
               "failed to prepare graph for xlator %s", xl->name);
        ret = -1;
        goto out;
    } else if (glusterfs_graph_init(graph)) {
        gf_msg("glusterfsd", GF_LOG_WARNING, EINVAL, LG_MSG_GRAPH_ATTACH_FAILED,
               "failed to initialize graph for xlator %s", xl->name);
        ret = -1;
        goto out;
    } else if (glusterfs_graph_parent_up(graph)) {
        gf_msg("glusterfsd", GF_LOG_WARNING, EINVAL, LG_MSG_GRAPH_ATTACH_FAILED,
               "failed to link the graphs for xlator %s ", xl->name);
        ret = -1;
        goto out;
    }

    if (!parent_graph) {
        parent_graph = glusterfs_muxsvc_setup_parent_graph(ctx, "glustershd",
                                                           "debug/io-stats");
        if (!parent_graph)
            goto out;
        ((xlator_t *)parent_graph->top)->next = xl;
        clean_graph = parent_graph;
    } else {
        last_xl = parent_graph->last_xl;
        if (last_xl)
            last_xl->next = xl;
        xl->prev = last_xl;
    }
    parent_graph->last_xl = graph->last_xl;

    ret = glusterfs_xlator_link(parent_graph->top, xl);
    if (ret) {
        gf_msg("graph", GF_LOG_ERROR, 0, LG_MSG_EVENT_NOTIFY_FAILED,
               "parent up notification failed");
        goto out;
    }
    parent_graph->xl_count += graph->xl_count;
    parent_graph->leaf_count += graph->leaf_count;
    parent_graph->id++;

    volfile_obj = GF_CALLOC(1, sizeof(gf_volfile_t), gf_common_volfile_t);
    if (!volfile_obj) {
        ret = -1;
        goto out;
    }
    volfile_obj->pidfp = NULL;
    snprintf(volfile_obj->vol_id, sizeof(volfile_obj->vol_id), "%s",
             volfile_id);

    if (strcmp(ctx->cmd_args.process_name, "glustershd") == 0) {
        ret = glusterfs_update_mux_pid(dict, volfile_obj);
        if (ret == -1) {
            GF_FREE(volfile_obj);
            goto out;
        }
    }

    graph->used = 1;
    parent_graph->id++;
    list_add(&graph->list, &ctx->graphs);
    INIT_LIST_HEAD(&volfile_obj->volfile_list);
    volfile_obj->graph = graph;
    memcpy(volfile_obj->volfile_checksum, checksum,
           sizeof(volfile_obj->volfile_checksum));
    list_add_tail(&volfile_obj->volfile_list, &ctx->volfile_list);
    gf_log_dump_graph(fp, graph);
    graph = NULL;

    ret = 0;
out:
    if (ret) {
        if (graph) {
            gluster_graph_take_reference(graph->first);
            ret = gf_thread_create_detached(&thread_id, glusterfs_graph_cleanup,
                                            graph, "graph_clean");
            if (ret) {
                gf_msg("glusterfs", GF_LOG_ERROR, EINVAL,
                       LG_MSG_GRAPH_CLEANUP_FAILED,
                       "%s failed to create clean "
                       "up thread",
                       volfile_id);
                ret = 0;
            }
        }
        if (clean_graph)
            glusterfs_muxsvc_cleanup_parent(ctx, clean_graph);
    }
    return ret;
}

int
glusterfs_mux_volfile_reconfigure(FILE *newvolfile_fp, glusterfs_ctx_t *ctx,
                                  gf_volfile_t *volfile_obj, char *checksum,
                                  dict_t *dict)
{
    glusterfs_graph_t *oldvolfile_graph = NULL;
    glusterfs_graph_t *newvolfile_graph = NULL;
    char vol_id[NAME_MAX + 1];

    int ret = -1;

    if (!ctx) {
        gf_msg("glusterfsd-mgmt", GF_LOG_ERROR, 0, LG_MSG_CTX_NULL,
               "ctx is NULL");
        goto out;
    }

    /* Change the message id */
    if (!volfile_obj) {
        gf_msg("glusterfsd-mgmt", GF_LOG_ERROR, 0, LG_MSG_CTX_NULL,
               "failed to get volfile object");
        goto out;
    }

    oldvolfile_graph = volfile_obj->graph;
    if (!oldvolfile_graph) {
        goto out;
    }

    newvolfile_graph = glusterfs_graph_construct(newvolfile_fp);

    if (!newvolfile_graph) {
        goto out;
    }
    newvolfile_graph->last_xl = glusterfs_get_last_xlator(newvolfile_graph);

    glusterfs_graph_prepare(newvolfile_graph, ctx, newvolfile_graph->first);

    if (!is_graph_topology_equal(oldvolfile_graph, newvolfile_graph)) {
        ret = snprintf(vol_id, sizeof(vol_id), "%s", volfile_obj->vol_id);
        if (ret < 0)
            goto out;
        ret = glusterfs_process_svc_detach(ctx, volfile_obj);
        if (ret) {
            gf_msg("glusterfsd-mgmt", GF_LOG_ERROR, EINVAL,
                   LG_MSG_GRAPH_CLEANUP_FAILED,
                   "Could not detach "
                   "old graph. Aborting the reconfiguration operation");
            goto out;
        }
        volfile_obj = NULL;
        ret = glusterfs_process_svc_attach_volfp(ctx, newvolfile_fp, vol_id,
                                                 checksum, dict);
        goto out;
    }

    gf_msg_debug("glusterfsd-mgmt", 0,
                 "Only options have changed in the"
                 " new graph");

    ret = glusterfs_graph_reconfigure(oldvolfile_graph, newvolfile_graph);
    if (ret) {
        gf_msg_debug("glusterfsd-mgmt", 0,
                     "Could not reconfigure "
                     "new options in old graph");
        goto out;
    }
    memcpy(volfile_obj->volfile_checksum, checksum,
           sizeof(volfile_obj->volfile_checksum));

    ret = 0;
out:

    if (newvolfile_graph)
        glusterfs_graph_destroy(newvolfile_graph);

    return ret;
}
