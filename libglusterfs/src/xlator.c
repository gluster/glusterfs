/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs/xlator.h"
#include <dlfcn.h>
#include <netdb.h>
#include <fnmatch.h>
#include "glusterfs/defaults.h"
#include "glusterfs/libglusterfs-messages.h"

#define SET_DEFAULT_FOP(fn)                                                    \
    do {                                                                       \
        if (!xl->fops->fn)                                                     \
            xl->fops->fn = default_##fn;                                       \
        if (!xl->pass_through_fops->fn)                                        \
            xl->pass_through_fops->fn = default_##fn;                          \
    } while (0)

#define SET_DEFAULT_CBK(fn)                                                    \
    do {                                                                       \
        if (!xl->cbks->fn)                                                     \
            xl->cbks->fn = default_##fn;                                       \
    } while (0)

pthread_mutex_t xlator_init_mutex = PTHREAD_MUTEX_INITIALIZER;

void
xlator_init_lock(void)
{
    (void)pthread_mutex_lock(&xlator_init_mutex);
}

void
xlator_init_unlock(void)
{
    (void)pthread_mutex_unlock(&xlator_init_mutex);
}

static struct xlator_cbks default_cbks = {};
struct volume_options default_options[] = {
    {
        .key = {"log-level"},
        .type = GF_OPTION_TYPE_STR,
        .op_version = {GD_OP_VERSION_6_0},
        .flags = OPT_FLAG_SETTABLE,
        .tags = {"generic"},
        .value = {"DEBUG", "WARNING", "ERROR", "INFO", "CRITICAL", "NONE",
                  "TRACE"},
        .description = "Option to set log-level of given translator",
    },
    {
        .key = {NULL},
    },
};

/* Handle the common options in each translator */
void
handle_default_options(xlator_t *xl, dict_t *options)
{
    int ret;
    char *value;

    /* log-level */
    ret = dict_get_str(options, "log-level", &value);
    if (!ret) {
        int log_level = glusterd_check_log_level(value);
        if (log_level != -1) {
            xl->loglevel = log_level;
        }
    }
}

static void
fill_defaults(xlator_t *xl)
{
    if (xl == NULL) {
        gf_msg_callingfn("xlator", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        return;
    }

    if (!xl->pass_through_fops)
        xl->pass_through_fops = default_fops;

    SET_DEFAULT_FOP(create);
    SET_DEFAULT_FOP(open);
    SET_DEFAULT_FOP(stat);
    SET_DEFAULT_FOP(readlink);
    SET_DEFAULT_FOP(mknod);
    SET_DEFAULT_FOP(mkdir);
    SET_DEFAULT_FOP(unlink);
    SET_DEFAULT_FOP(rmdir);
    SET_DEFAULT_FOP(symlink);
    SET_DEFAULT_FOP(rename);
    SET_DEFAULT_FOP(link);
    SET_DEFAULT_FOP(truncate);
    SET_DEFAULT_FOP(readv);
    SET_DEFAULT_FOP(writev);
    SET_DEFAULT_FOP(statfs);
    SET_DEFAULT_FOP(flush);
    SET_DEFAULT_FOP(fsync);
    SET_DEFAULT_FOP(setxattr);
    SET_DEFAULT_FOP(getxattr);
    SET_DEFAULT_FOP(fsetxattr);
    SET_DEFAULT_FOP(fgetxattr);
    SET_DEFAULT_FOP(removexattr);
    SET_DEFAULT_FOP(fremovexattr);
    SET_DEFAULT_FOP(opendir);
    SET_DEFAULT_FOP(readdir);
    SET_DEFAULT_FOP(readdirp);
    SET_DEFAULT_FOP(fsyncdir);
    SET_DEFAULT_FOP(access);
    SET_DEFAULT_FOP(ftruncate);
    SET_DEFAULT_FOP(fstat);
    SET_DEFAULT_FOP(lk);
    SET_DEFAULT_FOP(inodelk);
    SET_DEFAULT_FOP(finodelk);
    SET_DEFAULT_FOP(entrylk);
    SET_DEFAULT_FOP(fentrylk);
    SET_DEFAULT_FOP(lookup);
    SET_DEFAULT_FOP(rchecksum);
    SET_DEFAULT_FOP(xattrop);
    SET_DEFAULT_FOP(fxattrop);
    SET_DEFAULT_FOP(setattr);
    SET_DEFAULT_FOP(fsetattr);
    SET_DEFAULT_FOP(fallocate);
    SET_DEFAULT_FOP(discard);
    SET_DEFAULT_FOP(zerofill);
    SET_DEFAULT_FOP(ipc);
    SET_DEFAULT_FOP(seek);
    SET_DEFAULT_FOP(lease);
    SET_DEFAULT_FOP(getactivelk);
    SET_DEFAULT_FOP(setactivelk);
    SET_DEFAULT_FOP(put);

    SET_DEFAULT_FOP(getspec);
    SET_DEFAULT_FOP(icreate);
    SET_DEFAULT_FOP(namelink);
    SET_DEFAULT_FOP(copy_file_range);

    if (!xl->cbks)
        xl->cbks = &default_cbks;

    SET_DEFAULT_CBK(release);
    SET_DEFAULT_CBK(releasedir);
    SET_DEFAULT_CBK(forget);

    if (!xl->fini)
        xl->fini = default_fini;

    if (!xl->notify)
        xl->notify = default_notify;

    if (!xl->mem_acct_init)
        xl->mem_acct_init = default_mem_acct_init;

    return;
}

int
xlator_set_type_virtual(xlator_t *xl, const char *type)
{
    GF_VALIDATE_OR_GOTO("xlator", xl, out);
    GF_VALIDATE_OR_GOTO("xlator", type, out);

    xl->type = gf_strdup(type);

    if (xl->type)
        return 0;

out:
    return -1;
}

int
xlator_volopt_dynload(char *xlator_type, void **dl_handle,
                      volume_opt_list_t *opt_list)
{
    int ret = -1;
    int flag = 0;
    char *name = NULL;
    void *handle = NULL;
    xlator_api_t *xlapi = NULL;
    volume_option_t *opt = NULL;

    GF_VALIDATE_OR_GOTO("xlator", xlator_type, out);

    /* socket.so doesn't fall under the default xlator directory, hence we
     * need this check */
    if (!strstr(xlator_type, "rpc-transport"))
        ret = gf_asprintf(&name, "%s/%s.so", XLATORDIR, xlator_type);
    else {
        flag = 1;
        ret = gf_asprintf(&name, "%s/%s.so", XLATORPARENTDIR, xlator_type);
    }
    if (-1 == ret) {
        goto out;
    }

    ret = -1;

    gf_msg_trace("xlator", 0, "attempt to load file %s", name);

    handle = dlopen(name, RTLD_NOW);
    if (!handle) {
        gf_smsg("xlator", GF_LOG_WARNING, 0, LG_MSG_DLOPEN_FAILED, "error=%s",
                dlerror(), NULL);
        goto out;
    }

    if (flag == 0) {
        /* check new struct first, and then check this */
        xlapi = dlsym(handle, "xlator_api");
        if (!xlapi) {
            gf_smsg("xlator", GF_LOG_ERROR, 0, LG_MSG_DLSYM_ERROR, "error=%s",
                    dlerror(), NULL);
            goto out;
        }

        opt_list->given_opt = xlapi->options;
        if (!opt_list->given_opt) {
            gf_smsg("xlator", GF_LOG_ERROR, 0, LG_MSG_LOAD_FAILED, NULL);
            goto out;
        }
    } else {
        opt = dlsym(handle, "options");
        if (!opt) {
            gf_smsg("xlator", GF_LOG_ERROR, 0, LG_MSG_DLSYM_ERROR, "error=%s",
                    dlerror(), NULL);
            goto out;
        }

        opt_list->given_opt = opt;
    }

    *dl_handle = handle;
    handle = NULL;

    ret = 0;
out:
    GF_FREE(name);
    if (handle)
        dlclose(handle);

    gf_msg_debug("xlator", 0, "Returning %d", ret);
    return ret;
}

static int
xlator_dynload_apis(xlator_t *xl)
{
    int ret = -1;
    void *handle = NULL;
    volume_opt_list_t *vol_opt = NULL;
    xlator_api_t *xlapi = NULL;
    int i = 0;

    handle = xl->dlhandle;

    xlapi = dlsym(handle, "xlator_api");
    if (!xlapi) {
        gf_smsg("xlator", GF_LOG_ERROR, 0, LG_MSG_DLSYM_ERROR, "dlsym=%s",
                dlerror(), NULL);
        ret = -1;
        goto out;
    }

    xl->fops = xlapi->fops;
    if (!xl->fops) {
        gf_smsg("xlator", GF_LOG_WARNING, 0, LG_MSG_STRUCT_MISS, "name=%s",
                xl->name, NULL);
        goto out;
    }

    xl->cbks = xlapi->cbks;
    if (!xl->cbks) {
        gf_msg_trace("xlator", 0, "%s: struct missing (cbks)", xl->name);
    }

    xl->init = xlapi->init;
    if (!xl->init) {
        gf_smsg("xlator", GF_LOG_WARNING, 0, LG_MSG_METHOD_MISS, "name=%s",
                xl->name, NULL);
        goto out;
    }

    xl->fini = xlapi->fini;
    if (!xl->fini) {
        gf_msg_trace("xlator", 0, "%s: method missing (fini)", xl->name);
    }

    xl->reconfigure = xlapi->reconfigure;
    if (!xl->reconfigure) {
        gf_msg_trace("xlator", 0, "%s: method missing (reconfigure)", xl->name);
    }
    xl->notify = xlapi->notify;
    if (!xl->notify) {
        gf_msg_trace("xlator", 0, "%s: method missing (notify)", xl->name);
    }
    xl->dumpops = xlapi->dumpops;
    if (!xl->dumpops) {
        gf_msg_trace("xlator", 0, "%s: method missing (dumpops)", xl->name);
    }
    xl->mem_acct_init = xlapi->mem_acct_init;
    if (!xl->mem_acct_init) {
        gf_msg_trace("xlator", 0, "%s: method missing (mem_acct_init)",
                     xl->name);
    }

    xl->dump_metrics = xlapi->dump_metrics;
    if (!xl->dump_metrics) {
        gf_msg_trace("xlator", 0, "%s: method missing (dump_metrics)",
                     xl->name);
    }

    xl->pass_through_fops = xlapi->pass_through_fops;
    if (!xl->pass_through_fops) {
        gf_msg_trace("xlator", 0,
                     "%s: method missing (pass_through_fops), "
                     "falling back to default",
                     xl->name);
    }

    vol_opt = GF_CALLOC(1, sizeof(volume_opt_list_t),
                        gf_common_mt_volume_opt_list_t);
    if (!vol_opt) {
        goto out;
    }
    INIT_LIST_HEAD(&vol_opt->list);

    vol_opt->given_opt = default_options;
    list_add_tail(&vol_opt->list, &xl->volume_options);

    if (xlapi->options) {
        vol_opt = GF_CALLOC(1, sizeof(volume_opt_list_t),
                            gf_common_mt_volume_opt_list_t);
        if (!vol_opt) {
            goto out;
        }
        INIT_LIST_HEAD(&vol_opt->list);

        vol_opt->given_opt = xlapi->options;
        list_add_tail(&vol_opt->list, &xl->volume_options);
    }

    xl->id = xlapi->xlator_id;
    xl->flags = xlapi->flags;
    xl->identifier = xlapi->identifier;
    xl->category = xlapi->category;

    memcpy(xl->op_version, xlapi->op_version,
           sizeof(uint32_t) * GF_MAX_RELEASES);

    for (i = 0; i < GF_FOP_MAXVALUE; i++) {
        gf_latency_reset(&xl->stats.interval.latencies[i]);
    }

    ret = 0;
out:
    return ret;
}

int
xlator_dynload(xlator_t *xl)
{
    int ret = -1;
    char *name = NULL;
    void *handle = NULL;

    GF_VALIDATE_OR_GOTO("xlator", xl, out);

    INIT_LIST_HEAD(&xl->volume_options);

    ret = gf_asprintf(&name, "%s/%s.so", XLATORDIR, xl->type);
    if (-1 == ret) {
        goto out;
    }

    ret = -1;

    gf_msg_trace("xlator", 0, "attempt to load file %s", name);

    handle = dlopen(name, RTLD_NOW);
    if (!handle) {
        gf_smsg("xlator", GF_LOG_WARNING, 0, LG_MSG_DLOPEN_FAILED, "error=%s",
                dlerror(), NULL);
        goto out;
    }
    xl->dlhandle = handle;

    ret = xlator_dynload_apis(xl);
    if (-1 == ret)
        goto out;

    fill_defaults(xl);

    ret = 0;

out:
    GF_FREE(name);
    return ret;
}

int
xlator_set_type(xlator_t *xl, const char *type)
{
    int ret = 0;

    /* Handle 'global' translator differently */
    if (!strncmp(GF_GLOBAL_XLATOR_NAME, type, SLEN(GF_GLOBAL_XLATOR_NAME))) {
        volume_opt_list_t *vol_opt = NULL;

        /* set the required values from Global xlator */
        xl->type = gf_strdup(GF_GLOBAL_XLATOR_NAME);
        xl->cbks = global_xlator.cbks;
        xl->fops = global_xlator.fops;
        xl->init = global_xlator.init;
        xl->fini = global_xlator.fini;
        xl->reconfigure = global_xlator.reconfigure;

        vol_opt = GF_CALLOC(1, sizeof(volume_opt_list_t),
                            gf_common_mt_volume_opt_list_t);
        if (!vol_opt) {
            ret = -1;
            goto out;
        }

        vol_opt->given_opt = global_xl_options;

        INIT_LIST_HEAD(&xl->volume_options);
        INIT_LIST_HEAD(&vol_opt->list);
        list_add_tail(&vol_opt->list, &xl->volume_options);

        fill_defaults(xl);
        ret = 0;
        goto out;
    }

    ret = xlator_set_type_virtual(xl, type);
    if (!ret)
        ret = xlator_dynload(xl);
out:
    return ret;
}

void
xlator_set_inode_lru_limit(xlator_t *this, void *data)
{
    int inode_lru_limit = 0;

    if (this->itable) {
        if (!data) {
            gf_smsg(this->name, GF_LOG_WARNING, 0, LG_MSG_INPUT_DATA_NULL,
                    NULL);
            goto out;
        }
        inode_lru_limit = *(int *)data;
        inode_table_set_lru_limit(this->itable, inode_lru_limit);
    }

out:
    return;
}

void
xlator_foreach(xlator_t *this, void (*fn)(xlator_t *each, void *data),
               void *data)
{
    xlator_t *first = NULL;
    xlator_t *old_THIS = NULL;

    GF_VALIDATE_OR_GOTO("xlator", this, out);
    GF_VALIDATE_OR_GOTO("xlator", fn, out);

    first = this;

    while (first->prev)
        first = first->prev;

    while (first) {
        old_THIS = THIS;
        THIS = first;

        fn(first, data);

        THIS = old_THIS;
        first = first->next;
    }

out:
    return;
}

void
xlator_foreach_depth_first(xlator_t *this,
                           void (*fn)(xlator_t *each, void *data), void *data)
{
    xlator_list_t *subv = NULL;

    subv = this->children;

    while (subv) {
        xlator_foreach_depth_first(subv->xlator, fn, data);
        subv = subv->next;
    }

    fn(this, data);
}

xlator_t *
xlator_search_by_name(xlator_t *any, const char *name)
{
    xlator_t *search = NULL;

    GF_VALIDATE_OR_GOTO("xlator", any, out);
    GF_VALIDATE_OR_GOTO("xlator", name, out);

    search = any;

    while (search->prev)
        search = search->prev;

    while (search) {
        if (!strcmp(search->name, name))
            break;
        search = search->next;
    }

out:
    return search;
}

/*
 * With brick multiplexing, we sort of have multiple graphs, so
 * xlator_search_by_name might not find what we want.  Also, the translator
 * we're looking for might not be a direct child if something else was put in
 * between (as already happened with decompounder before that was fixed) and
 * it's hard to debug why our translator wasn't found.  Using a recursive tree
 * search instead of a linear search works around both problems.
 */
static xlator_t *
get_xlator_by_name_or_type(xlator_t *this, char *target, int is_name)
{
    xlator_list_t *trav;
    xlator_t *child_xl;
    char *value;

    for (trav = this->children; trav; trav = trav->next) {
        value = is_name ? trav->xlator->name : trav->xlator->type;
        if (!strcmp(value, target) && !trav->xlator->cleanup_starting) {
            return trav->xlator;
        }
        child_xl = get_xlator_by_name_or_type(trav->xlator, target, is_name);
        if (child_xl) {
            /*
             * If the xlator we're looking for is somewhere down
             * the stack, get_xlator_by_name expects to get a
             * pointer to the top of its subtree (child of "this")
             * while get_xlator_by_type expects a pointer to what
             * we actually found.  Handle both cases here.
             *
             * TBD: rename the functions and fix callers to better
             * reflect the difference in semantics.
             */
            return is_name ? trav->xlator : child_xl;
        }
    }

    return NULL;
}

xlator_t *
get_xlator_by_name(xlator_t *this, char *target)
{
    return get_xlator_by_name_or_type(this, target, 1);
}

xlator_t *
get_xlator_by_type(xlator_t *this, char *target)
{
    return get_xlator_by_name_or_type(this, target, 0);
}

static int
__xlator_init(xlator_t *xl)
{
    xlator_t *old_THIS = NULL;
    int ret = 0;
    int fop_idx = 0;

    old_THIS = THIS;
    THIS = xl;

    /* initialize the metrics related locks */
    for (fop_idx = 0; fop_idx < GF_FOP_MAXVALUE; fop_idx++) {
        GF_ATOMIC_INIT(xl->stats.total.metrics[fop_idx].fop, 0);
        GF_ATOMIC_INIT(xl->stats.total.metrics[fop_idx].cbk, 0);

        GF_ATOMIC_INIT(xl->stats.interval.metrics[fop_idx].fop, 0);
        GF_ATOMIC_INIT(xl->stats.interval.metrics[fop_idx].cbk, 0);
    }
    GF_ATOMIC_INIT(xl->stats.total.count, 0);
    GF_ATOMIC_INIT(xl->stats.interval.count, 0);

    xlator_init_lock();
    handle_default_options(xl, xl->options);
    ret = xl->init(xl);
    xlator_init_unlock();

    THIS = old_THIS;

    return ret;
}

int
xlator_init(xlator_t *xl)
{
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("xlator", xl, out);

    if (xl->mem_acct_init)
        xl->mem_acct_init(xl);

    xl->instance_name = NULL;
    GF_ATOMIC_INIT(xl->xprtrefcnt, 0);
    if (!xl->init) {
        gf_smsg(xl->name, GF_LOG_WARNING, 0, LG_MSG_INIT_FAILED, NULL);
        goto out;
    }

    ret = __xlator_init(xl);

    if (ret) {
        gf_smsg(xl->name, GF_LOG_ERROR, 0, LG_MSG_VOLUME_ERROR, "name=%s",
                xl->name, NULL);
        goto out;
    }

    xl->init_succeeded = 1;
    /*xl->cleanup_starting = 0;
      xl->call_cleanup = 0;
    */
    ret = 0;
out:
    return ret;
}

static void
xlator_fini_rec(xlator_t *xl)
{
    xlator_list_t *trav = NULL;
    xlator_t *old_THIS = NULL;

    GF_VALIDATE_OR_GOTO("xlator", xl, out);

    trav = xl->children;

    while (trav) {
        if (!trav->xlator->init_succeeded) {
            break;
        }

        xlator_fini_rec(trav->xlator);
        gf_msg_debug(trav->xlator->name, 0, "fini done");
        trav = trav->next;
    }

    xl->cleanup_starting = 1;
    if (xl->init_succeeded) {
        if (xl->fini) {
            old_THIS = THIS;
            THIS = xl;

            xl->fini(xl);

            if (xl->local_pool) {
                mem_pool_destroy(xl->local_pool);
                xl->local_pool = NULL;
            }
            if (xl->itable) {
                inode_table_destroy(xl->itable);
                xl->itable = NULL;
            }

            THIS = old_THIS;
        } else {
            gf_msg_debug(xl->name, 0, "No fini() found");
        }
        xl->init_succeeded = 0;
    }

out:
    return;
}

int
xlator_notify(xlator_t *xl, int event, void *data, ...)
{
    xlator_t *old_THIS = NULL;
    int ret = 0;

    old_THIS = THIS;
    THIS = xl;

    ret = xl->notify(xl, event, data);

    THIS = old_THIS;

    return ret;
}

int
xlator_mem_acct_init(xlator_t *xl, int num_types)
{
    int i = 0;
    int ret = 0;

    if (!xl)
        return -1;

    if (!xl->ctx)
        return -1;

    if (!xl->ctx->mem_acct_enable)
        return 0;

    xl->mem_acct = MALLOC(sizeof(struct mem_acct) +
                          sizeof(struct mem_acct_rec) * num_types);

    if (!xl->mem_acct) {
        return -1;
    }

    xl->mem_acct->num_types = num_types;
    GF_ATOMIC_INIT(xl->mem_acct->refcnt, 1);

    for (i = 0; i < num_types; i++) {
        memset(&xl->mem_acct->rec[i], 0, sizeof(struct mem_acct_rec));
        ret = LOCK_INIT(&(xl->mem_acct->rec[i].lock));
        if (ret) {
            fprintf(stderr, "Unable to lock..errno : %d", errno);
        }
#ifdef DEBUG
        INIT_LIST_HEAD(&(xl->mem_acct->rec[i].obj_list));
#endif
    }

    return 0;
}

void
xlator_mem_acct_unref(struct mem_acct *mem_acct)
{
    uint32_t i;

    if (GF_ATOMIC_DEC(mem_acct->refcnt) == 0) {
        for (i = 0; i < mem_acct->num_types; i++) {
            LOCK_DESTROY(&(mem_acct->rec[i].lock));
        }
        FREE(mem_acct);
    }
}

void
xlator_tree_fini(xlator_t *xl)
{
    xlator_t *top = NULL;

    GF_VALIDATE_OR_GOTO("xlator", xl, out);

    top = xl;
    xlator_fini_rec(top);

out:
    return;
}

int
xlator_list_destroy(xlator_list_t *list)
{
    xlator_list_t *next = NULL;

    while (list) {
        next = list->next;
        GF_FREE(list);
        list = next;
    }

    return 0;
}

int
xlator_memrec_free(xlator_t *xl)
{
    struct mem_acct *mem_acct = NULL;

    if (!xl) {
        return 0;
    }
    mem_acct = xl->mem_acct;

    if (mem_acct) {
        xlator_mem_acct_unref(mem_acct);
        xl->mem_acct = NULL;
    }

    return 0;
}

static int
xlator_members_free(xlator_t *xl)
{
    volume_opt_list_t *vol_opt = NULL;
    volume_opt_list_t *tmp = NULL;

    if (!xl)
        return 0;

    GF_FREE(xl->name);
    GF_FREE(xl->type);
    if (!(xl->ctx && xl->ctx->cmd_args.vgtool != _gf_none) && xl->dlhandle)
        dlclose(xl->dlhandle);
    if (xl->options)
        dict_unref(xl->options);

    xlator_list_destroy(xl->children);

    xlator_list_destroy(xl->parents);

    list_for_each_entry_safe(vol_opt, tmp, &xl->volume_options, list)
    {
        list_del_init(&vol_opt->list);
        GF_FREE(vol_opt);
    }

    return 0;
}

/* This function destroys all the xlator members except for the
 * xlator strcuture and its mem accounting field.
 *
 * If otherwise, it would destroy the primary xlator object as well
 * its mem accounting, which would mean after calling glusterfs_graph_destroy()
 * there cannot be any reference to GF_FREE() from the primary xlator, this is
 * not possible because of the following dependencies:
 * - glusterfs_ctx_t will have mem pools allocated by the primary xlators
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
 *
 * This two stages of destruction, is mainly required for glfs_fini().
 */

int
xlator_tree_free_members(xlator_t *tree)
{
    xlator_t *trav = tree;
    xlator_t *prev = tree;

    if (!tree) {
        gf_smsg("parser", GF_LOG_ERROR, 0, LG_MSG_TREE_NOT_FOUND, NULL);
        return -1;
    }

    while (prev) {
        trav = prev->next;
        xlator_members_free(prev);
        prev = trav;
    }

    return 0;
}

int
xlator_tree_free_memacct(xlator_t *tree)
{
    xlator_t *trav = tree;
    xlator_t *prev = tree;

    if (!tree) {
        gf_smsg("parser", GF_LOG_ERROR, 0, LG_MSG_TREE_NOT_FOUND, NULL);
        return -1;
    }

    while (prev) {
        trav = prev->next;
        xlator_memrec_free(prev);
        GF_FREE(prev);
        prev = trav;
    }

    return 0;
}

static int
xlator_mem_free(xlator_t *xl)
{
    volume_opt_list_t *vol_opt = NULL;
    volume_opt_list_t *tmp = NULL;

    if (!xl)
        return 0;

    if (xl->options) {
        dict_unref(xl->options);
        xl->options = NULL;
    }

    list_for_each_entry_safe(vol_opt, tmp, &xl->volume_options, list)
    {
        list_del_init(&vol_opt->list);
        GF_FREE(vol_opt);
    }

    xlator_memrec_free(xl);

    return 0;
}

static void
xlator_call_fini(xlator_t *this)
{
    if (!this || this->call_cleanup)
        return;
    this->cleanup_starting = 1;
    this->call_cleanup = 1;
    xlator_call_fini(this->next);
    this->fini(this);
}

void
xlator_mem_cleanup(xlator_t *this)
{
    xlator_list_t *list = this->children;
    xlator_t *trav = list->xlator;
    inode_table_t *inode_table = NULL;
    xlator_t *prev = trav;
    glusterfs_ctx_t *ctx = NULL;
    xlator_list_t **trav_p = NULL;
    xlator_t *top = NULL;
    xlator_t *victim = NULL;
    glusterfs_graph_t *graph = NULL;
    gf_boolean_t graph_cleanup = _gf_false;

    if (this->call_cleanup || !this->ctx)
        return;

    this->call_cleanup = 1;
    ctx = this->ctx;

    inode_table = this->itable;
    if (inode_table) {
        inode_table_destroy(inode_table);
        this->itable = NULL;
    }

    xlator_call_fini(trav);

    while (prev) {
        trav = prev->next;
        xlator_mem_free(prev);
        prev = trav;
    }

    if (this->fini) {
        this->fini(this);
    }

    xlator_mem_free(this);

    if (ctx->active) {
        top = ctx->active->first;
        LOCK(&ctx->volfile_lock);
        for (trav_p = &top->children; *trav_p; trav_p = &(*trav_p)->next) {
            victim = (*trav_p)->xlator;
            if (victim->call_cleanup && !strcmp(victim->name, this->name)) {
                graph_cleanup = _gf_true;
                (*trav_p) = (*trav_p)->next;
                break;
            }
        }
        UNLOCK(&ctx->volfile_lock);
    }

    if (graph_cleanup) {
        prev = this;
        graph = ctx->active;
        pthread_mutex_lock(&graph->mutex);
        while (prev) {
            trav = prev->next;
            GF_FREE(prev);
            prev = trav;
        }
        pthread_mutex_unlock(&graph->mutex);
    }
}

void
loc_wipe(loc_t *loc)
{
    if (loc->inode) {
        inode_unref(loc->inode);
        loc->inode = NULL;
    }
    if (loc->path) {
        GF_FREE((char *)loc->path);
        loc->path = NULL;
    }

    if (loc->parent) {
        inode_unref(loc->parent);
        loc->parent = NULL;
    }

    memset(loc, 0, sizeof(*loc));
}

int
loc_path(loc_t *loc, const char *bname)
{
    int ret = 0;

    if (loc->path)
        goto out;

    ret = -1;

    if (bname && !strlen(bname))
        bname = NULL;

    if (!bname)
        goto inode_path;

    if (loc->parent && !gf_uuid_is_null(loc->parent->gfid)) {
        ret = inode_path(loc->parent, bname, (char **)&loc->path);
    } else if (!gf_uuid_is_null(loc->pargfid)) {
        ret = gf_asprintf((char **)&loc->path, INODE_PATH_FMT "/%s",
                          uuid_utoa(loc->pargfid), bname);
    }

    if (loc->path)
        goto out;

inode_path:
    if (loc->inode && !gf_uuid_is_null(loc->inode->gfid)) {
        ret = inode_path(loc->inode, NULL, (char **)&loc->path);
    } else if (!gf_uuid_is_null(loc->gfid)) {
        ret = gf_asprintf((char **)&loc->path, INODE_PATH_FMT,
                          uuid_utoa(loc->gfid));
    }
out:
    return ret;
}

void
loc_gfid(loc_t *loc, uuid_t gfid)
{
    if (!gfid)
        goto out;
    gf_uuid_clear(gfid);

    if (!loc)
        goto out;
    else if (!gf_uuid_is_null(loc->gfid))
        gf_uuid_copy(gfid, loc->gfid);
    else if (loc->inode && (!gf_uuid_is_null(loc->inode->gfid)))
        gf_uuid_copy(gfid, loc->inode->gfid);
out:
    return;
}

void
loc_pargfid(loc_t *loc, uuid_t gfid)
{
    if (!gfid)
        goto out;
    gf_uuid_clear(gfid);

    if (!loc)
        goto out;
    else if (!gf_uuid_is_null(loc->pargfid))
        gf_uuid_copy(gfid, loc->pargfid);
    else if (loc->parent && (!gf_uuid_is_null(loc->parent->gfid)))
        gf_uuid_copy(gfid, loc->parent->gfid);
out:
    return;
}

char *
loc_gfid_utoa(loc_t *loc)
{
    uuid_t gfid = {
        0,
    };
    loc_gfid(loc, gfid);
    return uuid_utoa(gfid);
}

int
loc_touchup(loc_t *loc, const char *name)
{
    char *path = NULL;
    int ret = 0;

    if (loc->path)
        goto out;

    if (loc->parent && name && strlen(name)) {
        ret = inode_path(loc->parent, name, &path);
        if (path) /*Guaranteed to have trailing '/' */
            loc->name = strrchr(path, '/') + 1;

        if (gf_uuid_is_null(loc->pargfid))
            gf_uuid_copy(loc->pargfid, loc->parent->gfid);
    } else if (loc->inode) {
        ret = inode_path(loc->inode, 0, &path);
        if (gf_uuid_is_null(loc->gfid))
            gf_uuid_copy(loc->gfid, loc->inode->gfid);
    }

    if (ret < 0 || !path) {
        ret = -ENOMEM;
        goto out;
    }

    loc->path = path;
    ret = 0;
out:
    return ret;
}

int
loc_copy_overload_parent(loc_t *dst, loc_t *src, inode_t *parent)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("xlator", dst, err);
    GF_VALIDATE_OR_GOTO("xlator", src, err);
    GF_VALIDATE_OR_GOTO("xlator", parent, err);

    gf_uuid_copy(dst->gfid, src->gfid);
    gf_uuid_copy(dst->pargfid, parent->gfid);

    if (src->inode)
        dst->inode = inode_ref(src->inode);

    if (parent)
        dst->parent = inode_ref(parent);

    if (src->path) {
        dst->path = gf_strdup(src->path);

        if (!dst->path)
            goto out;

        if (src->name)
            dst->name = strrchr(dst->path, '/');
        if (dst->name)
            dst->name++;
    } else if (src->name) {
        dst->name = src->name;
    }

    ret = 0;
out:
    if (ret == -1)
        loc_wipe(dst);

err:
    return ret;
}

int
loc_copy(loc_t *dst, loc_t *src)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("xlator", dst, err);
    GF_VALIDATE_OR_GOTO("xlator", src, err);

    if (!gf_uuid_is_null(src->gfid))
        gf_uuid_copy(dst->gfid, src->gfid);
    else if (src->inode && !gf_uuid_is_null(src->inode->gfid))
        gf_uuid_copy(dst->gfid, src->inode->gfid);

    gf_uuid_copy(dst->pargfid, src->pargfid);

    if (src->inode)
        dst->inode = inode_ref(src->inode);

    if (src->parent)
        dst->parent = inode_ref(src->parent);

    if (src->path) {
        dst->path = gf_strdup(src->path);

        if (!dst->path)
            goto out;

        if (src->name)
            dst->name = strrchr(dst->path, '/');
        if (dst->name)
            dst->name++;
    } else if (src->name) {
        dst->name = src->name;
    }

    ret = 0;
out:
    if (ret == -1)
        loc_wipe(dst);

err:
    return ret;
}

gf_boolean_t
loc_is_root(loc_t *loc)
{
    if (loc && __is_root_gfid(loc->gfid)) {
        return _gf_true;
    } else if (loc && loc->inode && __is_root_gfid(loc->inode->gfid)) {
        return _gf_true;
    }

    return _gf_false;
}

int32_t
loc_build_child(loc_t *child, loc_t *parent, char *name)
{
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("xlator", child, out);
    GF_VALIDATE_OR_GOTO("xlator", parent, out);
    GF_VALIDATE_OR_GOTO("xlator", name, out);

    loc_gfid(parent, child->pargfid);

    if (strcmp(parent->path, "/") == 0)
        ret = gf_asprintf((char **)&child->path, "/%s", name);
    else
        ret = gf_asprintf((char **)&child->path, "%s/%s", parent->path, name);

    if (ret < 0 || !child->path) {
        ret = -1;
        goto out;
    }

    child->name = strrchr(child->path, '/') + 1;

    child->parent = inode_ref(parent->inode);
    child->inode = inode_new(parent->inode->table);

    if (!child->inode) {
        ret = -1;
        goto out;
    }

    ret = 0;

out:
    if ((ret < 0) && child)
        loc_wipe(child);

    return ret;
}

gf_boolean_t
loc_is_nameless(loc_t *loc)
{
    gf_boolean_t ret = _gf_false;

    GF_VALIDATE_OR_GOTO("xlator", loc, out);

    if ((!loc->parent && gf_uuid_is_null(loc->pargfid)) || !loc->name)
        ret = _gf_true;
out:
    return ret;
}

int
xlator_destroy(xlator_t *xl)
{
    if (!xl)
        return 0;

    xlator_members_free(xl);
    xlator_memrec_free(xl);
    GF_FREE(xl);

    return 0;
}

static int32_t
gf_bin_to_string(char *dst, size_t size, void *src, size_t len)
{
    if (len >= size) {
        return EINVAL;
    }

    memcpy(dst, src, len);
    dst[len] = 0;

    return 0;
}

int
is_gf_log_command(xlator_t *this, const char *name, char *value, size_t size)
{
    xlator_t *trav = NULL;
    char key[1024] = {
        0,
    };
    int ret = -1;
    int log_level = -1;
    gf_boolean_t syslog_flag = 0;
    glusterfs_ctx_t *ctx = NULL;

    if (!strcmp("trusted.glusterfs.syslog", name)) {
        ret = gf_bin_to_string(key, sizeof(key), value, size);
        if (ret != 0) {
            goto out;
        }
        ret = gf_string2boolean(key, &syslog_flag);
        if (ret) {
            ret = EOPNOTSUPP;
            goto out;
        }
        if (syslog_flag)
            gf_log_enable_syslog();
        else
            gf_log_disable_syslog();

        goto out;
    }

    if (fnmatch("trusted.glusterfs*set-log-level", name, FNM_NOESCAPE))
        goto out;

    ret = gf_bin_to_string(key, sizeof(key), value, size);
    if (ret != 0) {
        goto out;
    }

    log_level = glusterd_check_log_level(key);
    if (log_level == -1) {
        ret = EOPNOTSUPP;
        goto out;
    }

    /* Some crude way to change the log-level of process */
    if (!strcmp(name, "trusted.glusterfs.set-log-level")) {
        gf_smsg("glusterfs", gf_log_get_loglevel(), 0, LG_MSG_SET_LOG_LEVEL,
                "new-value=%d", log_level, "old-value=%d",
                gf_log_get_loglevel(), NULL);
        gf_log_set_loglevel(this->ctx, log_level);
        ret = 0;
        goto out;
    }

    if (!strcmp(name, "trusted.glusterfs.fuse.set-log-level")) {
        /* */
        gf_smsg(this->name, gf_log_get_xl_loglevel(this), 0,
                LG_MSG_SET_LOG_LEVEL, "new-value=%d", log_level, "old-value=%d",
                gf_log_get_xl_loglevel(this), NULL);
        gf_log_set_xl_loglevel(this, log_level);
        ret = 0;
        goto out;
    }

    ctx = this->ctx;
    if (!ctx)
        goto out;
    if (!ctx->active)
        goto out;
    trav = ctx->active->top;

    while (trav) {
        snprintf(key, 1024, "trusted.glusterfs.%s.set-log-level", trav->name);
        if (fnmatch(name, key, FNM_NOESCAPE) == 0) {
            gf_smsg(trav->name, gf_log_get_xl_loglevel(trav), 0,
                    LG_MSG_SET_LOG_LEVEL, "new-value%d", log_level,
                    "old-value=%d", gf_log_get_xl_loglevel(trav), NULL);
            gf_log_set_xl_loglevel(trav, log_level);
            ret = 0;
        }
        trav = trav->next;
    }
out:
    return ret;
}

int
glusterd_check_log_level(const char *value)
{
    int log_level = -1;

    if (!strcasecmp(value, "CRITICAL")) {
        log_level = GF_LOG_CRITICAL;
    } else if (!strcasecmp(value, "ERROR")) {
        log_level = GF_LOG_ERROR;
    } else if (!strcasecmp(value, "WARNING")) {
        log_level = GF_LOG_WARNING;
    } else if (!strcasecmp(value, "INFO")) {
        log_level = GF_LOG_INFO;
    } else if (!strcasecmp(value, "DEBUG")) {
        log_level = GF_LOG_DEBUG;
    } else if (!strcasecmp(value, "TRACE")) {
        log_level = GF_LOG_TRACE;
    } else if (!strcasecmp(value, "NONE")) {
        log_level = GF_LOG_NONE;
    }

    if (log_level == -1)
        gf_smsg(THIS->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_INIT, NULL);

    return log_level;
}

int
xlator_subvolume_count(xlator_t *this)
{
    int i = 0;
    xlator_list_t *list = NULL;

    for (list = this->children; list; list = list->next)
        i++;
    return i;
}

static int
_copy_opt_to_child(dict_t *options, char *key, data_t *value, void *data)
{
    xlator_t *child = data;

    gf_log(__func__, GF_LOG_DEBUG, "copying %s to child %s", key, child->name);
    dict_set(child->options, key, value);

    return 0;
}

int
copy_opts_to_child(xlator_t *src, xlator_t *dst, char *glob)
{
    return dict_foreach_fnmatch(src->options, glob, _copy_opt_to_child, dst);
}

int
glusterfs_delete_volfile_checksum(glusterfs_ctx_t *ctx, const char *volfile_id)
{
    gf_volfile_t *volfile_tmp = NULL;
    gf_volfile_t *volfile_obj = NULL;

    list_for_each_entry(volfile_tmp, &ctx->volfile_list, volfile_list)
    {
        if (!strcmp(volfile_id, volfile_tmp->vol_id)) {
            list_del_init(&volfile_tmp->volfile_list);
            volfile_obj = volfile_tmp;
            break;
        }
    }

    if (volfile_obj) {
        GF_FREE(volfile_obj);
    } else {
        gf_log(THIS->name, GF_LOG_ERROR,
               "failed to get volfile "
               "checksum for volfile id %s.",
               volfile_id);
    }

    return 0;
}

/*
   The function is required to take dict ref for every xlator at graph.
   At the time of compare graph topology create a graph and populate
   key values in the dictionary, after finished graph comparison we do destroy
   the new graph.At the time of construct graph we don't take any reference
   so to avoid dict leak at the of destroying graph due to ref counter underflow
   we need to call dict_ref here.

*/

void
gluster_graph_take_reference(xlator_t *tree)
{
    xlator_t *trav = tree;
    xlator_t *prev = tree;

    if (!tree) {
        gf_smsg("parser", GF_LOG_ERROR, 0, LG_MSG_TREE_NOT_FOUND, NULL);
        return;
    }

    while (prev) {
        trav = prev->next;
        if (prev->options)
            dict_ref(prev->options);
        prev = trav;
    }
    return;
}

gf_boolean_t
mgmt_is_multiplexed_daemon(char *name)
{
    const char *mux_daemons[] = {"glustershd", NULL};
    int i;

    if (!name)
        return _gf_false;

    for (i = 0; mux_daemons[i]; i++) {
        if (!strcmp(name, mux_daemons[i]))
            return _gf_true;
    }
    return _gf_false;
}

gf_boolean_t
xlator_is_cleanup_starting(xlator_t *this)
{
    gf_boolean_t cleanup = _gf_false;
    glusterfs_graph_t *graph = NULL;
    xlator_t *xl = NULL;

    if (!this) {
        gf_smsg("xlator", GF_LOG_WARNING, EINVAL, LG_MSG_OBJECT_NULL, "xlator",
                NULL);
        goto out;
    }

    graph = this->graph;
    if (!graph) {
        gf_smsg("xlator", GF_LOG_WARNING, EINVAL, LG_MSG_GRAPH_NOT_SET,
                "name=%s", this->name, NULL);
        goto out;
    }

    xl = graph->first;
    if (xl && xl->cleanup_starting)
        cleanup = _gf_true;
out:
    return cleanup;
}

int
graph_total_client_xlator(glusterfs_graph_t *graph)
{
    xlator_t *xl = NULL;
    int count = 0;

    if (!graph) {
        gf_smsg("xlator", GF_LOG_WARNING, EINVAL, LG_MSG_OBJECT_NULL, "graph",
                NULL);
        goto out;
    }

    xl = graph->first;
    if (!strcmp(xl->type, "protocol/server")) {
        gf_msg_debug(xl->name, 0, "Return because it is a server graph");
        return 0;
    }

    while (xl) {
        if (strcmp(xl->type, "protocol/client") == 0) {
            count++;
        }
        xl = xl->next;
    }
out:
    return count;
}
