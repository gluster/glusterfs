/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <stdarg.h>
#include "glusterfs.h"
#include "logging.h"
#include "iobuf.h"
#include "statedump.h"
#include "stack.h"
#include "common-utils.h"
#include "syscall.h"


#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif /* MALLOC_H */

/* We don't want gf_log in this function because it may cause
   'deadlock' with statedump. This is because statedump happens
   inside a signal handler and cannot afford to block on a lock.*/
#ifdef gf_log
# undef gf_log
#endif

#define GF_PROC_DUMP_IS_OPTION_ENABLED(opt)     \
        (dump_options.dump_##opt == _gf_true)

#define GF_PROC_DUMP_IS_XL_OPTION_ENABLED(opt)                  \
        (dump_options.xl_options.dump_##opt == _gf_true)

extern xlator_t global_xlator;

static pthread_mutex_t  gf_proc_dump_mutex;
static int gf_dump_fd = -1;
gf_dump_options_t dump_options;

static strfd_t *gf_dump_strfd = NULL;

static void
gf_proc_dump_lock (void)
{
        pthread_mutex_lock (&gf_proc_dump_mutex);
}


static void
gf_proc_dump_unlock (void)
{
        pthread_mutex_unlock (&gf_proc_dump_mutex);
}

static int
gf_proc_dump_open (char *tmpname)
{
        int  dump_fd = -1;

        mode_t mask = umask(S_IRWXG | S_IRWXO);
        dump_fd = mkstemp (tmpname);
        umask(mask);
        if (dump_fd < 0)
                return -1;

        gf_dump_fd = dump_fd;
        return 0;
}

static void
gf_proc_dump_close (void)
{
        sys_close (gf_dump_fd);
        gf_dump_fd = -1;
}

static int
gf_proc_dump_set_path (char *dump_options_file)
{
        int     ret = -1;
        FILE    *fp = NULL;
        char    buf[256];
        char    *key = NULL, *value = NULL;
        char    *saveptr = NULL;

        fp = fopen (dump_options_file, "r");
        if (!fp)
                goto out;

        ret = fscanf (fp, "%s", buf);

        while (ret != EOF) {
                key = strtok_r (buf, "=", &saveptr);
                if (!key) {
                        ret = fscanf (fp, "%s", buf);
                        continue;
                }

                value = strtok_r (NULL, "=", &saveptr);

                if (!value) {
                        ret = fscanf (fp, "%s", buf);
                        continue;
                }
                if (!strcmp (key, "path")) {
                        dump_options.dump_path = gf_strdup (value);
                        break;
                }
        }

out:
        if (fp)
                fclose (fp);
        return ret;
}

int
gf_proc_dump_add_section_fd (char *key, va_list ap)
{

        char buf[GF_DUMP_MAX_BUF_LEN];

        GF_ASSERT(key);

        memset (buf, 0, sizeof(buf));
        snprintf (buf, GF_DUMP_MAX_BUF_LEN, "\n[");
        vsnprintf (buf + strlen(buf),
                   GF_DUMP_MAX_BUF_LEN - strlen (buf), key, ap);
        snprintf (buf + strlen(buf),
                  GF_DUMP_MAX_BUF_LEN - strlen (buf),  "]\n");
        return sys_write (gf_dump_fd, buf, strlen (buf));
}


int
gf_proc_dump_add_section_strfd (char *key, va_list ap)
{
	int ret = 0;

        ret += strprintf (gf_dump_strfd, "[");
	ret += strvprintf (gf_dump_strfd, key, ap);
	ret += strprintf (gf_dump_strfd,  "]\n");

	return ret;
}


int
gf_proc_dump_add_section (char *key, ...)
{
	va_list ap;
	int ret = 0;

	va_start (ap, key);
	if (gf_dump_strfd)
		ret = gf_proc_dump_add_section_strfd (key, ap);
	else
		ret = gf_proc_dump_add_section_fd (key, ap);
	va_end (ap);

	return ret;
}


int
gf_proc_dump_write_fd (char *key, char *value, va_list ap)
{

        char         buf[GF_DUMP_MAX_BUF_LEN];
        int          offset = 0;

        GF_ASSERT (key);

        offset = strlen (key);

        memset (buf, 0, GF_DUMP_MAX_BUF_LEN);
        snprintf (buf, GF_DUMP_MAX_BUF_LEN, "%s", key);
        snprintf (buf + offset, GF_DUMP_MAX_BUF_LEN - offset, "=");
        offset += 1;
        vsnprintf (buf + offset, GF_DUMP_MAX_BUF_LEN - offset, value, ap);

        offset = strlen (buf);
        snprintf (buf + offset, GF_DUMP_MAX_BUF_LEN - offset, "\n");
        return sys_write (gf_dump_fd, buf, strlen (buf));
}


int
gf_proc_dump_write_strfd (char *key, char *value, va_list ap)
{
	int ret = 0;

	ret += strprintf (gf_dump_strfd, "%s = ", key);
	ret += strvprintf (gf_dump_strfd, value, ap);
	ret += strprintf (gf_dump_strfd, "\n");

	return ret;
}


int
gf_proc_dump_write (char *key, char *value, ...)
{
	int ret = 0;
	va_list ap;

	va_start (ap, value);
	if (gf_dump_strfd)
		ret = gf_proc_dump_write_strfd (key, value, ap);
	else
		ret = gf_proc_dump_write_fd (key, value, ap);
	va_end (ap);

	return ret;
}


static void
gf_proc_dump_xlator_mem_info (xlator_t *xl)
{
        int     i = 0;

        if (!xl)
                return;

        if (!xl->mem_acct)
                return;

        gf_proc_dump_add_section ("%s.%s - Memory usage", xl->type, xl->name);
        gf_proc_dump_write ("num_types", "%d", xl->mem_acct->num_types);

        for (i = 0; i < xl->mem_acct->num_types; i++) {
                if (xl->mem_acct->rec[i].num_allocs == 0)
                        continue;

                gf_proc_dump_add_section ("%s.%s - usage-type %s memusage",
                                          xl->type, xl->name,
                                          xl->mem_acct->rec[i].typestr);
                gf_proc_dump_write ("size", "%u", xl->mem_acct->rec[i].size);
                gf_proc_dump_write ("num_allocs", "%u",
                                    xl->mem_acct->rec[i].num_allocs);
                gf_proc_dump_write ("max_size", "%u",
                                    xl->mem_acct->rec[i].max_size);
                gf_proc_dump_write ("max_num_allocs", "%u",
                                    xl->mem_acct->rec[i].max_num_allocs);
                gf_proc_dump_write ("total_allocs", "%u",
                                    xl->mem_acct->rec[i].total_allocs);
        }

        return;
}

static void
gf_proc_dump_xlator_mem_info_only_in_use (xlator_t *xl)
{
        int     i = 0;

        if (!xl)
                return;

        if (!xl->mem_acct->rec)
                return;

        gf_proc_dump_add_section ("%s.%s - Memory usage", xl->type, xl->name);
        gf_proc_dump_write ("num_types", "%d", xl->mem_acct->num_types);

        for (i = 0; i < xl->mem_acct->num_types; i++) {
                if (!xl->mem_acct->rec[i].size)
                        continue;

                gf_proc_dump_add_section ("%s.%s - usage-type %d", xl->type,
                                          xl->name,i);

                gf_proc_dump_write ("size", "%u",
                                    xl->mem_acct->rec[i].size);
                gf_proc_dump_write ("max_size", "%u",
                                    xl->mem_acct->rec[i].max_size);
                gf_proc_dump_write ("num_allocs", "%u",
                                    xl->mem_acct->rec[i].num_allocs);
                gf_proc_dump_write ("max_num_allocs", "%u",
                                    xl->mem_acct->rec[i].max_num_allocs);
                gf_proc_dump_write ("total_allocs", "%u",
                                    xl->mem_acct->rec[i].total_allocs);
        }

        return;
}



/* Currently this dumps only mallinfo. More can be built on here */
void
gf_proc_dump_mem_info ()
{
#ifdef HAVE_MALLOC_STATS
        struct mallinfo info;

        memset (&info, 0, sizeof (struct mallinfo));
        info = mallinfo ();

        gf_proc_dump_add_section ("mallinfo");
        gf_proc_dump_write ("mallinfo_arena", "%d", info.arena);
        gf_proc_dump_write ("mallinfo_ordblks", "%d", info.ordblks);
        gf_proc_dump_write ("mallinfo_smblks", "%d", info.smblks);
        gf_proc_dump_write ("mallinfo_hblks", "%d", info.hblks);
        gf_proc_dump_write ("mallinfo_hblkhd", "%d", info.hblkhd);
        gf_proc_dump_write ("mallinfo_usmblks", "%d", info.usmblks);
        gf_proc_dump_write ("mallinfo_fsmblks", "%d", info.fsmblks);
        gf_proc_dump_write ("mallinfo_uordblks", "%d", info.uordblks);
        gf_proc_dump_write ("mallinfo_fordblks", "%d", info.fordblks);
        gf_proc_dump_write ("mallinfo_keepcost", "%d", info.keepcost);
#endif
        gf_proc_dump_xlator_mem_info(&global_xlator);

}

void
gf_proc_dump_mem_info_to_dict (dict_t *dict)
{
        if (!dict)
                return;
#ifdef HAVE_MALLOC_STATS
        struct  mallinfo info;
        int     ret = -1;

        memset (&info, 0, sizeof(struct mallinfo));
        info = mallinfo ();

        ret = dict_set_int32 (dict, "mallinfo.arena", info.arena);
        if (ret)
                return;

        ret = dict_set_int32 (dict, "mallinfo.ordblks", info.ordblks);
        if (ret)
                return;

        ret = dict_set_int32 (dict, "mallinfo.smblks", info.smblks);
        if (ret)
                return;

        ret = dict_set_int32 (dict, "mallinfo.hblks", info.hblks);
        if (ret)
                return;

        ret = dict_set_int32 (dict, "mallinfo.hblkhd", info.hblkhd);
        if (ret)
                return;

        ret = dict_set_int32 (dict, "mallinfo.usmblks", info.usmblks);
        if (ret)
                return;

        ret = dict_set_int32 (dict, "mallinfo.fsmblks", info.fsmblks);
        if (ret)
                return;

        ret = dict_set_int32 (dict, "mallinfo.uordblks", info.uordblks);
        if (ret)
                return;

        ret = dict_set_int32 (dict, "mallinfo.fordblks", info.fordblks);
        if (ret)
                return;

        ret = dict_set_int32 (dict, "mallinfo.keepcost", info.keepcost);
        if (ret)
                return;
#endif
        return;
}

void
gf_proc_dump_mempool_info (glusterfs_ctx_t *ctx)
{
#if defined(OLD_MEM_POOLS)
        struct mem_pool *pool = NULL;

        gf_proc_dump_add_section ("mempool");

        list_for_each_entry (pool, &ctx->mempool_list, global_list) {
                gf_proc_dump_write ("-----", "-----");
                gf_proc_dump_write ("pool-name", "%s", pool->name);
                gf_proc_dump_write ("hot-count", "%d", pool->hot_count);
                gf_proc_dump_write ("cold-count", "%d", pool->cold_count);
                gf_proc_dump_write ("padded_sizeof", "%lu",
                                    pool->padded_sizeof_type);
                gf_proc_dump_write ("alloc-count", "%"PRIu64, pool->alloc_count);
                gf_proc_dump_write ("max-alloc", "%d", pool->max_alloc);

                gf_proc_dump_write ("pool-misses", "%"PRIu64, pool->pool_misses);
                gf_proc_dump_write ("cur-stdalloc", "%d", pool->curr_stdalloc);
                gf_proc_dump_write ("max-stdalloc", "%d", pool->max_stdalloc);
        }
#endif
}

void
gf_proc_dump_mempool_info_to_dict (glusterfs_ctx_t *ctx, dict_t *dict)
{
#if defined(OLD_MEM_POOLS)
        struct mem_pool *pool = NULL;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};
        int             count = 0;
        int             ret = -1;

        if (!ctx || !dict)
                return;

        list_for_each_entry (pool, &ctx->mempool_list, global_list) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "pool%d.name", count);
                ret = dict_set_str (dict, key, pool->name);
                if (ret)
                        return;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "pool%d.hotcount", count);
                ret = dict_set_int32 (dict, key, pool->hot_count);
                if (ret)
                        return;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "pool%d.coldcount", count);
                ret = dict_set_int32 (dict, key, pool->cold_count);
                if (ret)
                        return;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "pool%d.paddedsizeof", count);
                ret = dict_set_uint64 (dict, key, pool->padded_sizeof_type);
                if (ret)
                        return;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "pool%d.alloccount", count);
                ret = dict_set_uint64 (dict, key, pool->alloc_count);
                if (ret)
                        return;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "pool%d.max_alloc", count);
                ret = dict_set_int32 (dict, key, pool->max_alloc);
                if (ret)
                        return;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "pool%d.max-stdalloc", count);
                ret = dict_set_int32 (dict, key, pool->max_stdalloc);
                if (ret)
                        return;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "pool%d.pool-misses", count);
                ret = dict_set_uint64 (dict, key, pool->pool_misses);
                if (ret)
                        return;
                count++;
        }
        ret = dict_set_int32 (dict, "mempool-count", count);
#endif
}

void gf_proc_dump_latency_info (xlator_t *xl);

void
gf_proc_dump_xlator_info (xlator_t *top)
{
        xlator_t        *trav = NULL;
        glusterfs_ctx_t *ctx = NULL;
        char             itable_key[1024] = {0,};

        if (!top)
                return;

        ctx = top->ctx;

        trav = top;
        while (trav) {

                if (ctx->measure_latency)
                        gf_proc_dump_latency_info (trav);

                gf_proc_dump_xlator_mem_info(trav);

                if (GF_PROC_DUMP_IS_XL_OPTION_ENABLED (inode) &&
                    (trav->itable)) {
                        snprintf (itable_key, 1024, "%d.%s.itable",
                                  ctx->graph_id, trav->name);
                }

                if (!trav->dumpops) {
                        trav = trav->next;
                        continue;
                }

                if (trav->dumpops->priv &&
                    GF_PROC_DUMP_IS_XL_OPTION_ENABLED (priv))
                        trav->dumpops->priv (trav);

                if (GF_PROC_DUMP_IS_XL_OPTION_ENABLED (inode) &&
                    (trav->dumpops->inode))
                        trav->dumpops->inode (trav);

                if (trav->dumpops->fd &&
                    GF_PROC_DUMP_IS_XL_OPTION_ENABLED (fd))
                        trav->dumpops->fd (trav);

                if (trav->dumpops->history &&
                    GF_PROC_DUMP_IS_XL_OPTION_ENABLED (history))
                        trav->dumpops->history (trav);

                trav = trav->next;
        }

        return;
}

static void
gf_proc_dump_oldgraph_xlator_info (xlator_t *top)
{
        xlator_t        *trav = NULL;

        if (!top)
                return;

        trav = top;
        while (trav) {
                gf_proc_dump_xlator_mem_info_only_in_use (trav);

                if (GF_PROC_DUMP_IS_XL_OPTION_ENABLED (inode) &&
                    (trav->itable)) {
                        /*TODO: dump inode table info if necessary by
                          printing the graph id (taken by glusterfs_cbtx_t)
                          in the key
                        */
                }

                if (!trav->dumpops) {
                        trav = trav->next;
                        continue;
                }

                if (GF_PROC_DUMP_IS_XL_OPTION_ENABLED (inode) &&
                    (trav->dumpops->inode))
                        trav->dumpops->inode (trav);

                if (trav->dumpops->fd &&
                    GF_PROC_DUMP_IS_XL_OPTION_ENABLED (fd))
                        trav->dumpops->fd (trav);

                trav = trav->next;
        }

        return;
}

static int
gf_proc_dump_enable_all_options ()
{

        GF_PROC_DUMP_SET_OPTION (dump_options.dump_mem, _gf_true);
        GF_PROC_DUMP_SET_OPTION (dump_options.dump_iobuf, _gf_true);
        GF_PROC_DUMP_SET_OPTION (dump_options.dump_callpool, _gf_true);
        GF_PROC_DUMP_SET_OPTION (dump_options.xl_options.dump_priv, _gf_true);
        GF_PROC_DUMP_SET_OPTION (dump_options.xl_options.dump_inode, _gf_true);
        GF_PROC_DUMP_SET_OPTION (dump_options.xl_options.dump_fd, _gf_true);
        GF_PROC_DUMP_SET_OPTION (dump_options.xl_options.dump_inodectx,
                                 _gf_true);
        GF_PROC_DUMP_SET_OPTION (dump_options.xl_options.dump_fdctx, _gf_true);
        GF_PROC_DUMP_SET_OPTION (dump_options.xl_options.dump_history,
                                 _gf_true);

        return 0;
}

gf_boolean_t
is_gf_proc_dump_all_disabled ()
{
        gf_boolean_t all_disabled = _gf_true;

        GF_CHECK_DUMP_OPTION_ENABLED (dump_options.dump_mem, all_disabled, out);
        GF_CHECK_DUMP_OPTION_ENABLED (dump_options.dump_iobuf, all_disabled, out);
        GF_CHECK_DUMP_OPTION_ENABLED (dump_options.dump_callpool, all_disabled,
                                   out);
        GF_CHECK_DUMP_OPTION_ENABLED (dump_options.xl_options.dump_priv,
                                   all_disabled, out);
        GF_CHECK_DUMP_OPTION_ENABLED (dump_options.xl_options.dump_inode,
                                   all_disabled, out);
        GF_CHECK_DUMP_OPTION_ENABLED (dump_options.xl_options.dump_fd,
                                   all_disabled, out);
        GF_CHECK_DUMP_OPTION_ENABLED (dump_options.xl_options.dump_inodectx,
                                   all_disabled, out);
        GF_CHECK_DUMP_OPTION_ENABLED (dump_options.xl_options.dump_fdctx,
                                   all_disabled, out);
        GF_CHECK_DUMP_OPTION_ENABLED (dump_options.xl_options.dump_history,
                                   all_disabled, out);

out:
        return all_disabled;
}

/* These options are dumped by default if glusterdump.options
   file exists and it is emtpty
*/
static int
gf_proc_dump_enable_default_options ()
{
        GF_PROC_DUMP_SET_OPTION (dump_options.dump_mem, _gf_true);
        GF_PROC_DUMP_SET_OPTION (dump_options.dump_callpool, _gf_true);

        return 0;
}

static int
gf_proc_dump_disable_all_options ()
{

        GF_PROC_DUMP_SET_OPTION (dump_options.dump_mem, _gf_false);
        GF_PROC_DUMP_SET_OPTION (dump_options.dump_iobuf, _gf_false);
        GF_PROC_DUMP_SET_OPTION (dump_options.dump_callpool, _gf_false);
        GF_PROC_DUMP_SET_OPTION (dump_options.xl_options.dump_priv, _gf_false);
        GF_PROC_DUMP_SET_OPTION (dump_options.xl_options.dump_inode,
                                 _gf_false);
        GF_PROC_DUMP_SET_OPTION (dump_options.xl_options.dump_fd, _gf_false);
        GF_PROC_DUMP_SET_OPTION (dump_options.xl_options.dump_inodectx,
                                 _gf_false);
        GF_PROC_DUMP_SET_OPTION (dump_options.xl_options.dump_fdctx, _gf_false);
        GF_PROC_DUMP_SET_OPTION (dump_options.xl_options.dump_history,
                                 _gf_false);
        return 0;
}

static int
gf_proc_dump_parse_set_option (char *key, char *value)
{
        gf_boolean_t    *opt_key = NULL;
        gf_boolean_t    opt_value = _gf_false;
        char buf[GF_DUMP_MAX_BUF_LEN];
        int ret = -1;

        if (!strcasecmp (key, "all")) {
                (void)gf_proc_dump_enable_all_options ();
                return 0;
        } else if (!strcasecmp (key, "mem")) {
                opt_key = &dump_options.dump_mem;
        } else if (!strcasecmp (key, "iobuf")) {
                opt_key = &dump_options.dump_iobuf;
        } else if (!strcasecmp (key, "callpool")) {
                opt_key = &dump_options.dump_callpool;
        } else if (!strcasecmp (key, "priv")) {
                opt_key = &dump_options.xl_options.dump_priv;
        } else if (!strcasecmp (key, "fd")) {
                opt_key = &dump_options.xl_options.dump_fd;
        } else if (!strcasecmp (key, "inode")) {
                opt_key = &dump_options.xl_options.dump_inode;
        } else if (!strcasecmp (key, "inodectx")) {
                opt_key = &dump_options.xl_options.dump_inodectx;
        } else if (!strcasecmp (key, "fdctx")) {
                opt_key = &dump_options.xl_options.dump_fdctx;
        } else if (!strcasecmp (key, "history")) {
                opt_key = &dump_options.xl_options.dump_history;
        }

        if (!opt_key) {
                //None of dump options match the key, return back
                snprintf (buf, sizeof (buf), "[Warning]:None of the options "
                          "matched key : %s\n", key);
                ret = sys_write (gf_dump_fd, buf, strlen (buf));

                if (ret >= 0)
                        ret = -1;
                goto out;

        }

        opt_value = (strncasecmp (value, "yes", 3) ?
                     _gf_false: _gf_true);

        GF_PROC_DUMP_SET_OPTION (*opt_key, opt_value);

        ret = 0;
out:
        return ret;
}

static int
gf_proc_dump_options_init ()
{
        int     ret = -1;
        FILE    *fp = NULL;
        char    buf[256];
        char    *key = NULL, *value = NULL;
        char    *saveptr = NULL;
        char    dump_option_file[PATH_MAX];

        /* glusterd will create a file glusterdump.<pid>.options and
           sets the statedump options for the process and the file is removed
           after the statedump is taken. Direct issue of SIGUSR1 does not have
           mechanism for considering the statedump options. So to have a way
           of configuring the statedump of all the glusterfs processes through
           both cli command and SIGUSR1, glusterdump.options file is searched
           and the options mentioned in it are given the higher priority.
        */
        snprintf (dump_option_file, sizeof (dump_option_file),
                  DEFAULT_VAR_RUN_DIRECTORY
                  "/glusterdump.options");
        fp = fopen (dump_option_file, "r");
        if (!fp) {
                snprintf (dump_option_file, sizeof (dump_option_file),
                          DEFAULT_VAR_RUN_DIRECTORY
                          "/glusterdump.%d.options", getpid ());

                fp = fopen (dump_option_file, "r");

                if (!fp) {
                        //ENOENT, return success
                        (void) gf_proc_dump_enable_all_options ();
                        return 0;
                }
        }

        (void) gf_proc_dump_disable_all_options ();

        // swallow the errors if setting statedump file path is failed.
        ret = gf_proc_dump_set_path (dump_option_file);

        ret = fscanf (fp, "%s", buf);

        while (ret != EOF) {
                key = strtok_r (buf, "=", &saveptr);
                if (!key) {
                        ret = fscanf (fp, "%s", buf);
                        continue;
                }

                value = strtok_r (NULL, "=", &saveptr);

                if (!value) {
                        ret = fscanf (fp, "%s", buf);
                        continue;
                }

                gf_proc_dump_parse_set_option (key, value);
        }

        if (is_gf_proc_dump_all_disabled ())
                (void) gf_proc_dump_enable_default_options ();

        if (fp)
                fclose (fp);

        return 0;
}

void
gf_proc_dump_info (int signum, glusterfs_ctx_t *ctx)
{
        int                i                       = 0;
        int                ret                     = -1;
        glusterfs_graph_t *trav                    = NULL;
        char               brick_name[PATH_MAX]    = {0,};
        char               timestr[256]            = {0,};
        char               sign_string[512]        = {0,};
        char               tmp_dump_name[PATH_MAX] = {0,};
        char               path[PATH_MAX]          = {0,};
        struct timeval     tv                      = {0,};

        gf_proc_dump_lock ();

        if (!ctx)
                goto out;

        if (ctx->cmd_args.brick_name) {
                GF_REMOVE_SLASH_FROM_PATH (ctx->cmd_args.brick_name, brick_name);
        } else
                strncpy (brick_name, "glusterdump", sizeof (brick_name));

        ret = gf_proc_dump_options_init ();
        if (ret < 0)
                goto out;

        snprintf (path, sizeof (path), "%s/%s.%d.dump.%"PRIu64,
                  ((dump_options.dump_path != NULL)?dump_options.dump_path:
                   ((ctx->statedump_path != NULL)?ctx->statedump_path:
                    DEFAULT_VAR_RUN_DIRECTORY)), brick_name, getpid(),
                  (uint64_t) time (NULL));

        snprintf (tmp_dump_name, PATH_MAX, "%s/dumpXXXXXX",
                  ((dump_options.dump_path != NULL)?dump_options.dump_path:
                   ((ctx->statedump_path != NULL)?ctx->statedump_path:
                    DEFAULT_VAR_RUN_DIRECTORY)));

        ret = gf_proc_dump_open (tmp_dump_name);
        if (ret < 0)
                goto out;

        //continue even though gettimeofday() has failed
        ret = gettimeofday (&tv, NULL);
        if (0 == ret) {
                gf_time_fmt (timestr, sizeof timestr, tv.tv_sec, gf_timefmt_FT);
                snprintf (timestr + strlen (timestr),
                          sizeof timestr - strlen (timestr),
                          ".%"GF_PRI_SUSECONDS, tv.tv_usec);
        }

        snprintf (sign_string, sizeof (sign_string), "DUMP-START-TIME: %s\n",
                  timestr);

        //swallow the errors of write for start and end marker
        ret = sys_write (gf_dump_fd, sign_string, strlen (sign_string));

        memset (sign_string, 0, sizeof (sign_string));
        memset (timestr, 0, sizeof (timestr));
        memset (&tv, 0, sizeof (tv));

        if (GF_PROC_DUMP_IS_OPTION_ENABLED (mem)) {
                gf_proc_dump_mem_info ();
                gf_proc_dump_mempool_info (ctx);
        }

        if (GF_PROC_DUMP_IS_OPTION_ENABLED (iobuf))
                iobuf_stats_dump (ctx->iobuf_pool);
        if (GF_PROC_DUMP_IS_OPTION_ENABLED (callpool))
                gf_proc_dump_pending_frames (ctx->pool);

        if (ctx->master) {
                gf_proc_dump_add_section ("fuse");
                gf_proc_dump_xlator_info (ctx->master);
        }

        if (ctx->active) {
                gf_proc_dump_add_section ("active graph - %d", ctx->graph_id);
                gf_proc_dump_xlator_info (ctx->active->top);
        }

        i = 0;
        list_for_each_entry (trav, &ctx->graphs, list) {
                if (trav == ctx->active)
                        continue;

                gf_proc_dump_add_section ("oldgraph[%d]", i);

                gf_proc_dump_oldgraph_xlator_info (trav->top);
                i++;
        }

        ret = gettimeofday (&tv, NULL);
        if (0 == ret) {
                gf_time_fmt (timestr, sizeof timestr, tv.tv_sec, gf_timefmt_FT);
                snprintf (timestr + strlen (timestr),
                          sizeof timestr - strlen (timestr),
                          ".%"GF_PRI_SUSECONDS, tv.tv_usec);
        }

        snprintf (sign_string, sizeof (sign_string), "\nDUMP-END-TIME: %s",
                  timestr);
        ret = sys_write (gf_dump_fd, sign_string, strlen (sign_string));

out:
        if (gf_dump_fd != -1)
                gf_proc_dump_close ();
        sys_rename (tmp_dump_name, path);
        GF_FREE (dump_options.dump_path);
        dump_options.dump_path = NULL;
        gf_proc_dump_unlock ();

        return;
}


void
gf_proc_dump_fini (void)
{
        pthread_mutex_destroy (&gf_proc_dump_mutex);
}


void
gf_proc_dump_init ()
{
        pthread_mutex_init (&gf_proc_dump_mutex, NULL);

        return;
}


void
gf_proc_dump_cleanup (void)
{
        pthread_mutex_destroy (&gf_proc_dump_mutex);
}


void
gf_proc_dump_xlator_private (xlator_t *this, strfd_t *strfd)
{
        gf_proc_dump_lock ();
	{
		gf_dump_strfd = strfd;

		if (this->dumpops && this->dumpops->priv)
			this->dumpops->priv (this);

		gf_dump_strfd = NULL;
	}
	gf_proc_dump_unlock ();
}


void
gf_proc_dump_mallinfo (strfd_t *strfd)
{
        gf_proc_dump_lock ();
	{
		gf_dump_strfd = strfd;

		gf_proc_dump_mem_info ();

		gf_dump_strfd = NULL;
	}
	gf_proc_dump_unlock ();
}


void
gf_proc_dump_xlator_history (xlator_t *this, strfd_t *strfd)
{
        gf_proc_dump_lock ();
	{
		gf_dump_strfd = strfd;

		if (this->dumpops && this->dumpops->history)
			this->dumpops->history (this);

		gf_dump_strfd = NULL;
	}
	gf_proc_dump_unlock ();
}


void
gf_proc_dump_xlator_itable (xlator_t *this, strfd_t *strfd)
{
        gf_proc_dump_lock ();
	{
		gf_dump_strfd = strfd;


		gf_dump_strfd = NULL;
	}
	gf_proc_dump_unlock ();
}


void
gf_proc_dump_xlator_meminfo (xlator_t *this, strfd_t *strfd)
{
        gf_proc_dump_lock ();
	{
		gf_dump_strfd = strfd;

		gf_proc_dump_xlator_mem_info (this);

		gf_dump_strfd = NULL;
	}
	gf_proc_dump_unlock ();
}


void
gf_proc_dump_xlator_profile (xlator_t *this, strfd_t *strfd)
{
        gf_proc_dump_lock ();
	{
		gf_dump_strfd = strfd;

		gf_proc_dump_latency_info (this);

		gf_dump_strfd = NULL;
	}
	gf_proc_dump_unlock ();
}
