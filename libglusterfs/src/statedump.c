/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <stdarg.h>
#include "glusterfs/glusterfs.h"
#include "glusterfs/logging.h"
#include "glusterfs/statedump.h"
#include "glusterfs/stack.h"
#include "glusterfs/syscall.h"

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif /* MALLOC_H */

/* We don't want gf_log in this function because it may cause
   'deadlock' with statedump. This is because statedump happens
   inside a signal handler and cannot afford to block on a lock.*/
#ifdef gf_log
#undef gf_log
#endif

#define GF_PROC_DUMP_IS_OPTION_ENABLED(opt)                                    \
    (dump_options.dump_##opt == _gf_true)

#define GF_PROC_DUMP_IS_XL_OPTION_ENABLED(opt)                                 \
    (dump_options.xl_options.dump_##opt == _gf_true)

extern xlator_t global_xlator;

static pthread_mutex_t gf_proc_dump_mutex;
static int gf_dump_fd = -1;
gf_dump_options_t dump_options;

static strfd_t *gf_dump_strfd = NULL;

static void
gf_proc_dump_lock(void)
{
    pthread_mutex_lock(&gf_proc_dump_mutex);
}

static void
gf_proc_dump_unlock(void)
{
    pthread_mutex_unlock(&gf_proc_dump_mutex);
}

static int
gf_proc_dump_open(char *tmpname)
{
    int dump_fd = -1;

    mode_t mask = umask(S_IRWXG | S_IRWXO);
    dump_fd = mkstemp(tmpname);
    umask(mask);
    if (dump_fd < 0)
        return -1;

    gf_dump_fd = dump_fd;
    return 0;
}

static void
gf_proc_dump_close(void)
{
    sys_close(gf_dump_fd);
    gf_dump_fd = -1;
}

static int
gf_proc_dump_set_path(char *dump_options_file)
{
    int ret = -1;
    FILE *fp = NULL;
    char buf[256];
    char *key = NULL, *value = NULL;
    char *saveptr = NULL;

    fp = fopen(dump_options_file, "r");
    if (!fp)
        goto out;

    ret = fscanf(fp, "%255s", buf);

    while (ret != EOF) {
        key = strtok_r(buf, "=", &saveptr);
        if (!key) {
            ret = fscanf(fp, "%255s", buf);
            continue;
        }

        value = strtok_r(NULL, "=", &saveptr);

        if (!value) {
            ret = fscanf(fp, "%255s", buf);
            continue;
        }
        if (!strcmp(key, "path")) {
            dump_options.dump_path = gf_strdup(value);
            break;
        }
    }

out:
    if (fp)
        fclose(fp);
    return ret;
}

static int
gf_proc_dump_add_section_fd(char *key, va_list ap)
{
    char buf[GF_DUMP_MAX_BUF_LEN];
    int len;

    GF_ASSERT(key);

    len = snprintf(buf, GF_DUMP_MAX_BUF_LEN, "\n[");
    len += vsnprintf(buf + len, GF_DUMP_MAX_BUF_LEN - len, key, ap);
    len += snprintf(buf + len, GF_DUMP_MAX_BUF_LEN - len, "]\n");
    return sys_write(gf_dump_fd, buf, len);
}

static int
gf_proc_dump_add_section_strfd(char *key, va_list ap)
{
    int ret = 0;

    ret += strprintf(gf_dump_strfd, "[");
    ret += strvprintf(gf_dump_strfd, key, ap);
    ret += strprintf(gf_dump_strfd, "]\n");

    return ret;
}

int
gf_proc_dump_add_section(char *key, ...)
{
    va_list ap;
    int ret = 0;

    va_start(ap, key);
    if (gf_dump_strfd)
        ret = gf_proc_dump_add_section_strfd(key, ap);
    else
        ret = gf_proc_dump_add_section_fd(key, ap);
    va_end(ap);

    return ret;
}

static int
gf_proc_dump_write_fd(char *key, char *value, va_list ap)
{
    char buf[GF_DUMP_MAX_BUF_LEN];
    int len = 0;

    GF_ASSERT(key);

    len = snprintf(buf, GF_DUMP_MAX_BUF_LEN, "%s=", key);
    len += vsnprintf(buf + len, GF_DUMP_MAX_BUF_LEN - len, value, ap);

    len += snprintf(buf + len, GF_DUMP_MAX_BUF_LEN - len, "\n");
    return sys_write(gf_dump_fd, buf, len);
}

static int
gf_proc_dump_write_strfd(char *key, char *value, va_list ap)
{
    int ret = 0;

    ret += strprintf(gf_dump_strfd, "%s = ", key);
    ret += strvprintf(gf_dump_strfd, value, ap);
    ret += strprintf(gf_dump_strfd, "\n");

    return ret;
}

int
gf_proc_dump_write(char *key, char *value, ...)
{
    int ret = 0;
    va_list ap;

    va_start(ap, value);
    if (gf_dump_strfd)
        ret = gf_proc_dump_write_strfd(key, value, ap);
    else
        ret = gf_proc_dump_write_fd(key, value, ap);
    va_end(ap);

    return ret;
}

void
gf_latency_statedump_and_reset(char *key, gf_latency_t *lat)
{
    /* Doesn't make sense to continue if there are no fops
       came in the given interval */
    if (!lat || !lat->count)
        return;
    gf_proc_dump_write(key,
                       "AVG:%lf CNT:%" PRIu64 " TOTAL:%" PRIu64 " MIN:%" PRIu64
                       " MAX:%" PRIu64,
                       (((double)lat->total) / lat->count), lat->count,
                       lat->total, lat->min, lat->max);
    gf_latency_reset(lat);
}

void
gf_proc_dump_xl_latency_info(xlator_t *xl)
{
    char key_prefix[GF_DUMP_MAX_BUF_LEN];
    char key[GF_DUMP_MAX_BUF_LEN];
    int i;

    snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.latency", xl->name);
    gf_proc_dump_add_section("%s", key_prefix);

    for (i = 0; i < GF_FOP_MAXVALUE; i++) {
        gf_proc_dump_build_key(key, key_prefix, "%s", (char *)gf_fop_list[i]);

        gf_latency_t *lat = &xl->stats.interval.latencies[i];

        gf_latency_statedump_and_reset(key, lat);
    }
}

static void
gf_proc_dump_xlator_mem_info(xlator_t *xl)
{
    int i = 0;

    if (!xl)
        return;

    if (!xl->mem_acct)
        return;

    gf_proc_dump_add_section("%s.%s - Memory usage", xl->type, xl->name);
    gf_proc_dump_write("num_types", "%d", xl->mem_acct->num_types);

    for (i = 0; i < xl->mem_acct->num_types; i++) {
        if (xl->mem_acct->rec[i].num_allocs == 0)
            continue;

        gf_proc_dump_add_section("%s.%s - usage-type %s memusage", xl->type,
                                 xl->name, xl->mem_acct->rec[i].typestr);
        gf_proc_dump_write("size", "%" PRIu64, xl->mem_acct->rec[i].size);
        gf_proc_dump_write("num_allocs", "%u", xl->mem_acct->rec[i].num_allocs);
        gf_proc_dump_write("max_size", "%" PRIu64,
                           xl->mem_acct->rec[i].max_size);
        gf_proc_dump_write("max_num_allocs", "%u",
                           xl->mem_acct->rec[i].max_num_allocs);
        gf_proc_dump_write("total_allocs", "%" PRIu64,
                           xl->mem_acct->rec[i].total_allocs);
    }

    return;
}

static void
gf_proc_dump_xlator_mem_info_only_in_use(xlator_t *xl)
{
    int i = 0;

    if (!xl)
        return;

    if (!xl->mem_acct)
        return;

    gf_proc_dump_add_section("%s.%s - Memory usage", xl->type, xl->name);
    gf_proc_dump_write("num_types", "%d", xl->mem_acct->num_types);

    for (i = 0; i < xl->mem_acct->num_types; i++) {
        if (!xl->mem_acct->rec[i].size)
            continue;

        gf_proc_dump_add_section("%s.%s - usage-type %d", xl->type, xl->name,
                                 i);

        gf_proc_dump_write("size", "%" PRIu64, xl->mem_acct->rec[i].size);
        gf_proc_dump_write("max_size", "%" PRIu64,
                           xl->mem_acct->rec[i].max_size);
        gf_proc_dump_write("num_allocs", "%u", xl->mem_acct->rec[i].num_allocs);
        gf_proc_dump_write("max_num_allocs", "%u",
                           xl->mem_acct->rec[i].max_num_allocs);
        gf_proc_dump_write("total_allocs", "%" PRIu64,
                           xl->mem_acct->rec[i].total_allocs);
    }

    return;
}

/* Currently this dumps only mallinfo. More can be built on here */
void
gf_proc_dump_mem_info()
{
#ifdef HAVE_MALLINFO
    struct mallinfo info;

    memset(&info, 0, sizeof(struct mallinfo));
    info = mallinfo();

    gf_proc_dump_add_section("mallinfo");
    gf_proc_dump_write("mallinfo_arena", "%d", info.arena);
    gf_proc_dump_write("mallinfo_ordblks", "%d", info.ordblks);
    gf_proc_dump_write("mallinfo_smblks", "%d", info.smblks);
    gf_proc_dump_write("mallinfo_hblks", "%d", info.hblks);
    gf_proc_dump_write("mallinfo_hblkhd", "%d", info.hblkhd);
    gf_proc_dump_write("mallinfo_usmblks", "%d", info.usmblks);
    gf_proc_dump_write("mallinfo_fsmblks", "%d", info.fsmblks);
    gf_proc_dump_write("mallinfo_uordblks", "%d", info.uordblks);
    gf_proc_dump_write("mallinfo_fordblks", "%d", info.fordblks);
    gf_proc_dump_write("mallinfo_keepcost", "%d", info.keepcost);
#endif
    gf_proc_dump_xlator_mem_info(&global_xlator);
}

void
gf_proc_dump_mem_info_to_dict(dict_t *dict)
{
    if (!dict)
        return;
#ifdef HAVE_MALLINFO
    struct mallinfo info;
    int ret = -1;

    memset(&info, 0, sizeof(struct mallinfo));
    info = mallinfo();

    ret = dict_set_int32(dict, "mallinfo.arena", info.arena);
    if (ret)
        return;

    ret = dict_set_int32(dict, "mallinfo.ordblks", info.ordblks);
    if (ret)
        return;

    ret = dict_set_int32(dict, "mallinfo.smblks", info.smblks);
    if (ret)
        return;

    ret = dict_set_int32(dict, "mallinfo.hblks", info.hblks);
    if (ret)
        return;

    ret = dict_set_int32(dict, "mallinfo.hblkhd", info.hblkhd);
    if (ret)
        return;

    ret = dict_set_int32(dict, "mallinfo.usmblks", info.usmblks);
    if (ret)
        return;

    ret = dict_set_int32(dict, "mallinfo.fsmblks", info.fsmblks);
    if (ret)
        return;

    ret = dict_set_int32(dict, "mallinfo.uordblks", info.uordblks);
    if (ret)
        return;

    ret = dict_set_int32(dict, "mallinfo.fordblks", info.fordblks);
    if (ret)
        return;

    ret = dict_set_int32(dict, "mallinfo.keepcost", info.keepcost);
    if (ret)
        return;
#endif
    return;
}

void
gf_proc_dump_mempool_info(glusterfs_ctx_t *ctx)
{
#ifdef GF_DISABLE_MEMPOOL
    gf_proc_dump_write("built with --disable-mempool", " so no memory pools");
#else
    struct mem_pool *pool = NULL;

    gf_proc_dump_add_section("mempool");

    LOCK(&ctx->lock);
    {
        list_for_each_entry(pool, &ctx->mempool_list, owner)
        {
            int64_t active = GF_ATOMIC_GET(pool->active);

            gf_proc_dump_write("-----", "-----");
            gf_proc_dump_write("pool-name", "%s", pool->name);
            gf_proc_dump_write("xlator-name", "%s", pool->xl_name);
            gf_proc_dump_write("active-count", "%" GF_PRI_ATOMIC, active);
            gf_proc_dump_write("sizeof-type", "%lu", pool->sizeof_type);
            gf_proc_dump_write("padded-sizeof", "%d",
                               1 << pool->pool->power_of_two);
            gf_proc_dump_write("size", "%" PRId64,
                               (1 << pool->pool->power_of_two) * active);
            gf_proc_dump_write("shared-pool", "%p", pool->pool);
        }
    }
    UNLOCK(&ctx->lock);
#endif /* GF_DISABLE_MEMPOOL */
}

void
gf_proc_dump_mempool_info_to_dict(glusterfs_ctx_t *ctx, dict_t *dict)
{
#ifndef GF_DISABLE_MEMPOOL
    struct mem_pool *pool = NULL;
    char key[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };
    int count = 0;
    int ret = -1;

    if (!ctx || !dict)
        return;

    LOCK(&ctx->lock);
    {
        list_for_each_entry(pool, &ctx->mempool_list, owner)
        {
            int64_t active = GF_ATOMIC_GET(pool->active);

            snprintf(key, sizeof(key), "pool%d.name", count);
            ret = dict_set_str(dict, key, pool->name);
            if (ret)
                goto out;

            snprintf(key, sizeof(key), "pool%d.active-count", count);
            ret = dict_set_uint64(dict, key, active);
            if (ret)
                goto out;

            snprintf(key, sizeof(key), "pool%d.sizeof-type", count);
            ret = dict_set_uint64(dict, key, pool->sizeof_type);
            if (ret)
                goto out;

            snprintf(key, sizeof(key), "pool%d.padded-sizeof", count);
            ret = dict_set_uint64(dict, key, 1 << pool->pool->power_of_two);
            if (ret)
                goto out;

            snprintf(key, sizeof(key), "pool%d.size", count);
            ret = dict_set_uint64(dict, key,
                                  (1 << pool->pool->power_of_two) * active);
            if (ret)
                goto out;

            snprintf(key, sizeof(key), "pool%d.shared-pool", count);
            ret = dict_set_static_ptr(dict, key, pool->pool);
            if (ret)
                goto out;
        }
    }
out:
    UNLOCK(&ctx->lock);
#endif /* !GF_DISABLE_MEMPOOL */
}

void
gf_proc_dump_latency_info(xlator_t *xl);

void
gf_proc_dump_dict_info(glusterfs_ctx_t *ctx)
{
    int64_t total_dicts = 0;
    int64_t total_pairs = 0;

    total_dicts = GF_ATOMIC_GET(ctx->stats.total_dicts_used);
    total_pairs = GF_ATOMIC_GET(ctx->stats.total_pairs_used);

    gf_proc_dump_write("max-pairs-per-dict", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(ctx->stats.max_dict_pairs));
    gf_proc_dump_write("total-pairs-used", "%" PRId64, total_pairs);
    gf_proc_dump_write("total-dicts-used", "%" PRId64, total_dicts);
    gf_proc_dump_write("average-pairs-per-dict", "%" PRId64,
                       (total_pairs / total_dicts));
}

static void
gf_proc_dump_single_xlator_info(xlator_t *trav)
{
    glusterfs_ctx_t *ctx = trav->ctx;
    char itable_key[1024] = {
        0,
    };

    if (trav->cleanup_starting)
        return;

    if (ctx->measure_latency)
        gf_proc_dump_xl_latency_info(trav);

    gf_proc_dump_xlator_mem_info(trav);

    if (GF_PROC_DUMP_IS_XL_OPTION_ENABLED(inode) && (trav->itable)) {
        snprintf(itable_key, sizeof(itable_key), "%d.%s.itable", ctx->graph_id,
                 trav->name);
    }

    if (!trav->dumpops) {
        return;
    }

    if (trav->dumpops->priv && GF_PROC_DUMP_IS_XL_OPTION_ENABLED(priv))
        trav->dumpops->priv(trav);

    if (GF_PROC_DUMP_IS_XL_OPTION_ENABLED(inode) && (trav->dumpops->inode))
        trav->dumpops->inode(trav);
    if (trav->dumpops->fd && GF_PROC_DUMP_IS_XL_OPTION_ENABLED(fd))
        trav->dumpops->fd(trav);

    if (trav->dumpops->history && GF_PROC_DUMP_IS_XL_OPTION_ENABLED(history))
        trav->dumpops->history(trav);
}

static void
gf_proc_dump_per_xlator_info(xlator_t *top)
{
    xlator_t *trav = top;

    while (trav && !trav->cleanup_starting) {
        gf_proc_dump_single_xlator_info(trav);
        trav = trav->next;
    }
}

void
gf_proc_dump_xlator_info(xlator_t *top, gf_boolean_t brick_mux)
{
    xlator_t *trav = NULL;
    xlator_list_t **trav_p = NULL;

    if (!top)
        return;

    trav = top;
    gf_proc_dump_per_xlator_info(trav);

    if (brick_mux) {
        trav_p = &top->children;
        while (*trav_p) {
            trav = (*trav_p)->xlator;
            gf_proc_dump_per_xlator_info(trav);
            trav_p = &(*trav_p)->next;
        }
    }

    return;
}

static void
gf_proc_dump_oldgraph_xlator_info(xlator_t *top)
{
    xlator_t *trav = NULL;

    if (!top)
        return;

    trav = top;
    while (trav) {
        gf_proc_dump_xlator_mem_info_only_in_use(trav);

        if (GF_PROC_DUMP_IS_XL_OPTION_ENABLED(inode) && (trav->itable)) {
            /*TODO: dump inode table info if necessary by
              printing the graph id (taken by glusterfs_cbtx_t)
              in the key
            */
        }

        if (!trav->dumpops) {
            trav = trav->next;
            continue;
        }

        if (GF_PROC_DUMP_IS_XL_OPTION_ENABLED(inode) && (trav->dumpops->inode))
            trav->dumpops->inode(trav);

        if (trav->dumpops->fd && GF_PROC_DUMP_IS_XL_OPTION_ENABLED(fd))
            trav->dumpops->fd(trav);

        trav = trav->next;
    }

    return;
}

static int
gf_proc_dump_enable_all_options()
{
    GF_PROC_DUMP_SET_OPTION(dump_options.dump_mem, _gf_true);
    GF_PROC_DUMP_SET_OPTION(dump_options.dump_iobuf, _gf_true);
    GF_PROC_DUMP_SET_OPTION(dump_options.dump_callpool, _gf_true);
    GF_PROC_DUMP_SET_OPTION(dump_options.xl_options.dump_priv, _gf_true);
    GF_PROC_DUMP_SET_OPTION(dump_options.xl_options.dump_inode, _gf_true);
    GF_PROC_DUMP_SET_OPTION(dump_options.xl_options.dump_fd, _gf_true);
    GF_PROC_DUMP_SET_OPTION(dump_options.xl_options.dump_inodectx, _gf_true);
    GF_PROC_DUMP_SET_OPTION(dump_options.xl_options.dump_fdctx, _gf_true);
    GF_PROC_DUMP_SET_OPTION(dump_options.xl_options.dump_history, _gf_true);

    return 0;
}

gf_boolean_t
is_gf_proc_dump_all_disabled()
{
    gf_boolean_t all_disabled = _gf_true;

    GF_CHECK_DUMP_OPTION_ENABLED(dump_options.dump_mem, all_disabled, out);
    GF_CHECK_DUMP_OPTION_ENABLED(dump_options.dump_iobuf, all_disabled, out);
    GF_CHECK_DUMP_OPTION_ENABLED(dump_options.dump_callpool, all_disabled, out);
    GF_CHECK_DUMP_OPTION_ENABLED(dump_options.xl_options.dump_priv,
                                 all_disabled, out);
    GF_CHECK_DUMP_OPTION_ENABLED(dump_options.xl_options.dump_inode,
                                 all_disabled, out);
    GF_CHECK_DUMP_OPTION_ENABLED(dump_options.xl_options.dump_fd, all_disabled,
                                 out);
    GF_CHECK_DUMP_OPTION_ENABLED(dump_options.xl_options.dump_inodectx,
                                 all_disabled, out);
    GF_CHECK_DUMP_OPTION_ENABLED(dump_options.xl_options.dump_fdctx,
                                 all_disabled, out);
    GF_CHECK_DUMP_OPTION_ENABLED(dump_options.xl_options.dump_history,
                                 all_disabled, out);

out:
    return all_disabled;
}

/* These options are dumped by default if glusterdump.options
   file exists and it is emtpty
*/
static int
gf_proc_dump_enable_default_options()
{
    GF_PROC_DUMP_SET_OPTION(dump_options.dump_mem, _gf_true);
    GF_PROC_DUMP_SET_OPTION(dump_options.dump_callpool, _gf_true);

    return 0;
}

static int
gf_proc_dump_disable_all_options()
{
    GF_PROC_DUMP_SET_OPTION(dump_options.dump_mem, _gf_false);
    GF_PROC_DUMP_SET_OPTION(dump_options.dump_iobuf, _gf_false);
    GF_PROC_DUMP_SET_OPTION(dump_options.dump_callpool, _gf_false);
    GF_PROC_DUMP_SET_OPTION(dump_options.xl_options.dump_priv, _gf_false);
    GF_PROC_DUMP_SET_OPTION(dump_options.xl_options.dump_inode, _gf_false);
    GF_PROC_DUMP_SET_OPTION(dump_options.xl_options.dump_fd, _gf_false);
    GF_PROC_DUMP_SET_OPTION(dump_options.xl_options.dump_inodectx, _gf_false);
    GF_PROC_DUMP_SET_OPTION(dump_options.xl_options.dump_fdctx, _gf_false);
    GF_PROC_DUMP_SET_OPTION(dump_options.xl_options.dump_history, _gf_false);
    return 0;
}

static int
gf_proc_dump_parse_set_option(char *key, char *value)
{
    gf_boolean_t *opt_key = NULL;
    gf_boolean_t opt_value = _gf_false;
    char buf[GF_DUMP_MAX_BUF_LEN];
    int ret = -1;
    int len;

    if (!strcasecmp(key, "all")) {
        (void)gf_proc_dump_enable_all_options();
        return 0;
    } else if (!strcasecmp(key, "mem")) {
        opt_key = &dump_options.dump_mem;
    } else if (!strcasecmp(key, "iobuf")) {
        opt_key = &dump_options.dump_iobuf;
    } else if (!strcasecmp(key, "callpool")) {
        opt_key = &dump_options.dump_callpool;
    } else if (!strcasecmp(key, "priv")) {
        opt_key = &dump_options.xl_options.dump_priv;
    } else if (!strcasecmp(key, "fd")) {
        opt_key = &dump_options.xl_options.dump_fd;
    } else if (!strcasecmp(key, "inode")) {
        opt_key = &dump_options.xl_options.dump_inode;
    } else if (!strcasecmp(key, "inodectx")) {
        opt_key = &dump_options.xl_options.dump_inodectx;
    } else if (!strcasecmp(key, "fdctx")) {
        opt_key = &dump_options.xl_options.dump_fdctx;
    } else if (!strcasecmp(key, "history")) {
        opt_key = &dump_options.xl_options.dump_history;
    }

    if (!opt_key) {
        // None of dump options match the key, return back
        len = snprintf(buf, sizeof(buf),
                       "[Warning]:None of the options "
                       "matched key : %s\n",
                       key);
        if (len < 0)
            ret = -1;
        else {
            ret = sys_write(gf_dump_fd, buf, len);
            if (ret >= 0)
                ret = -1;
        }
        goto out;
    }

    opt_value = (strncasecmp(value, "yes", 3) ? _gf_false : _gf_true);

    GF_PROC_DUMP_SET_OPTION(*opt_key, opt_value);

    ret = 0;
out:
    return ret;
}

static int
gf_proc_dump_options_init()
{
    int ret = -1;
    FILE *fp = NULL;
    char buf[256];
    char *key = NULL, *value = NULL;
    char *saveptr = NULL;
    char dump_option_file[PATH_MAX];

    /* glusterd will create a file glusterdump.<pid>.options and
       sets the statedump options for the process and the file is removed
       after the statedump is taken. Direct issue of SIGUSR1 does not have
       mechanism for considering the statedump options. So to have a way
       of configuring the statedump of all the glusterfs processes through
       both cli command and SIGUSR1, glusterdump.options file is searched
       and the options mentioned in it are given the higher priority.
    */
    snprintf(dump_option_file, sizeof(dump_option_file),
             DEFAULT_VAR_RUN_DIRECTORY "/glusterdump.options");
    fp = fopen(dump_option_file, "r");
    if (!fp) {
        snprintf(dump_option_file, sizeof(dump_option_file),
                 DEFAULT_VAR_RUN_DIRECTORY "/glusterdump.%d.options", getpid());

        fp = fopen(dump_option_file, "r");

        if (!fp) {
            // ENOENT, return success
            (void)gf_proc_dump_enable_all_options();
            return 0;
        }
    }

    (void)gf_proc_dump_disable_all_options();

    // swallow the errors if setting statedump file path is failed.
    (void)gf_proc_dump_set_path(dump_option_file);

    ret = fscanf(fp, "%255s", buf);

    while (ret != EOF) {
        key = strtok_r(buf, "=", &saveptr);
        if (!key) {
            ret = fscanf(fp, "%255s", buf);
            continue;
        }

        value = strtok_r(NULL, "=", &saveptr);

        if (!value) {
            ret = fscanf(fp, "%255s", buf);
            continue;
        }

        gf_proc_dump_parse_set_option(key, value);
    }

    if (is_gf_proc_dump_all_disabled())
        (void)gf_proc_dump_enable_default_options();

    if (fp)
        fclose(fp);

    return 0;
}

void
gf_proc_dump_info(int signum, glusterfs_ctx_t *ctx)
{
    int i = 0;
    int ret = -1;
    glusterfs_graph_t *trav = NULL;
    char brick_name[PATH_MAX] = {
        0,
    };
    char timestr[GF_TIMESTR_SIZE] = {
        0,
    };
    char sign_string[512] = {
        0,
    };
    char tmp_dump_name[PATH_MAX] = {
        0,
    };
    char path[PATH_MAX] = {
        0,
    };
    struct timeval tv = {
        0,
    };
    gf_boolean_t is_brick_mux = _gf_false;
    xlator_t *top = NULL;
    xlator_list_t **trav_p = NULL;
    int brick_count = 0;
    int len = 0;

    gf_msg_trace("dump", 0, "received statedump request (sig:USR1)");

    if (!ctx)
        goto out;

    /*
     * Multiplexed daemons can change the active graph when attach/detach
     * is called. So this has to be protected with the cleanup lock.
     */
    if (mgmt_is_multiplexed_daemon(ctx->cmd_args.process_name))
        pthread_mutex_lock(&ctx->cleanup_lock);
    gf_proc_dump_lock();

    if (!mgmt_is_multiplexed_daemon(ctx->cmd_args.process_name) &&
        (ctx && ctx->active)) {
        top = ctx->active->first;
        for (trav_p = &top->children; *trav_p; trav_p = &(*trav_p)->next) {
            brick_count++;
        }

        if (brick_count > 1)
            is_brick_mux = _gf_true;
    }

    if (ctx->cmd_args.brick_name) {
        GF_REMOVE_SLASH_FROM_PATH(ctx->cmd_args.brick_name, brick_name);
    } else
        snprintf(brick_name, sizeof(brick_name), "glusterdump");

    ret = gf_proc_dump_options_init();
    if (ret < 0)
        goto out;

    ret = snprintf(
        path, sizeof(path), "%s/%s.%d.dump.%" PRIu64,
        ((dump_options.dump_path != NULL)
             ? dump_options.dump_path
             : ((ctx->statedump_path != NULL) ? ctx->statedump_path
                                              : DEFAULT_VAR_RUN_DIRECTORY)),
        brick_name, getpid(), (uint64_t)gf_time());
    if ((ret < 0) || (ret >= sizeof(path))) {
        goto out;
    }

    snprintf(
        tmp_dump_name, PATH_MAX, "%s/dumpXXXXXX",
        ((dump_options.dump_path != NULL)
             ? dump_options.dump_path
             : ((ctx->statedump_path != NULL) ? ctx->statedump_path
                                              : DEFAULT_VAR_RUN_DIRECTORY)));

    ret = gf_proc_dump_open(tmp_dump_name);
    if (ret < 0)
        goto out;

    // continue even though gettimeofday() has failed
    ret = gettimeofday(&tv, NULL);
    if (0 == ret) {
        gf_time_fmt_tv(timestr, sizeof timestr, &tv, gf_timefmt_FT);
    }

    len = snprintf(sign_string, sizeof(sign_string), "DUMP-START-TIME: %s\n",
                   timestr);

    // swallow the errors of write for start and end marker
    (void)sys_write(gf_dump_fd, sign_string, len);

    memset(timestr, 0, sizeof(timestr));

    if (GF_PROC_DUMP_IS_OPTION_ENABLED(mem)) {
        gf_proc_dump_mem_info();
        gf_proc_dump_mempool_info(ctx);
    }

    if (GF_PROC_DUMP_IS_OPTION_ENABLED(iobuf))
        iobuf_stats_dump(ctx->iobuf_pool);
    if (GF_PROC_DUMP_IS_OPTION_ENABLED(callpool))
        gf_proc_dump_pending_frames(ctx->pool);

    /* dictionary stats */
    gf_proc_dump_add_section("dict");
    gf_proc_dump_dict_info(ctx);

    if (ctx->primary) {
        gf_proc_dump_add_section("fuse");
        gf_proc_dump_single_xlator_info(ctx->primary);
    }

    if (ctx->active) {
        gf_proc_dump_add_section("active graph - %d", ctx->graph_id);
        gf_proc_dump_xlator_info(ctx->active->top, is_brick_mux);
    }

    i = 0;
    list_for_each_entry(trav, &ctx->graphs, list)
    {
        if (trav == ctx->active)
            continue;

        gf_proc_dump_add_section("oldgraph[%d]", i);

        gf_proc_dump_oldgraph_xlator_info(trav->top);
        i++;
    }

    ret = gettimeofday(&tv, NULL);
    if (0 == ret) {
        gf_time_fmt_tv(timestr, sizeof timestr, &tv, gf_timefmt_FT);
    }

    len = snprintf(sign_string, sizeof(sign_string), "\nDUMP-END-TIME: %s",
                   timestr);
    (void)sys_write(gf_dump_fd, sign_string, len);

    if (gf_dump_fd != -1)
        gf_proc_dump_close();
    sys_rename(tmp_dump_name, path);
out:
    GF_FREE(dump_options.dump_path);
    dump_options.dump_path = NULL;
    if (ctx) {
        gf_proc_dump_unlock();
        if (mgmt_is_multiplexed_daemon(ctx->cmd_args.process_name))
            pthread_mutex_unlock(&ctx->cleanup_lock);
    }

    return;
}

void
gf_proc_dump_fini(void)
{
    pthread_mutex_destroy(&gf_proc_dump_mutex);
}

void
gf_proc_dump_init()
{
    pthread_mutex_init(&gf_proc_dump_mutex, NULL);

    return;
}

void
gf_proc_dump_cleanup(void)
{
    pthread_mutex_destroy(&gf_proc_dump_mutex);
}

void
gf_proc_dump_xlator_private(xlator_t *this, strfd_t *strfd)
{
    gf_proc_dump_lock();
    {
        gf_dump_strfd = strfd;

        if (this->dumpops && this->dumpops->priv)
            this->dumpops->priv(this);

        gf_dump_strfd = NULL;
    }
    gf_proc_dump_unlock();
}

void
gf_proc_dump_mallinfo(strfd_t *strfd)
{
    gf_proc_dump_lock();
    {
        gf_dump_strfd = strfd;

        gf_proc_dump_mem_info();

        gf_dump_strfd = NULL;
    }
    gf_proc_dump_unlock();
}

void
gf_proc_dump_xlator_history(xlator_t *this, strfd_t *strfd)
{
    gf_proc_dump_lock();
    {
        gf_dump_strfd = strfd;

        if (this->dumpops && this->dumpops->history)
            this->dumpops->history(this);

        gf_dump_strfd = NULL;
    }
    gf_proc_dump_unlock();
}

void
gf_proc_dump_xlator_itable(xlator_t *this, strfd_t *strfd)
{
    gf_proc_dump_lock();
    {
        gf_dump_strfd = strfd;

        gf_dump_strfd = NULL;
    }
    gf_proc_dump_unlock();
}

void
gf_proc_dump_xlator_meminfo(xlator_t *this, strfd_t *strfd)
{
    gf_proc_dump_lock();
    {
        gf_dump_strfd = strfd;

        gf_proc_dump_xlator_mem_info(this);

        gf_dump_strfd = NULL;
    }
    gf_proc_dump_unlock();
}

void
gf_proc_dump_xlator_profile(xlator_t *this, strfd_t *strfd)
{
    gf_proc_dump_lock();
    {
        gf_dump_strfd = strfd;

        gf_proc_dump_xl_latency_info(this);

        gf_dump_strfd = NULL;
    }
    gf_proc_dump_unlock();
}
