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

#include <stdarg.h>
#include "glusterfs.h"
#include "logging.h"
#include "iobuf.h"
#include "statedump.h"
#include "stack.h"
#include "common-utils.h"

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif /* MALLOC_H */

/* We don't want gf_log in this function because it may cause
   'deadlock' with statedump */
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
gf_proc_dump_open (char *dump_dir, char *brickname)
{
        char path[PATH_MAX] = {0,};
        int  dump_fd = -1;

        snprintf (path, sizeof (path), "%s/%s.%d.dump", (dump_dir ?
                  dump_dir : "/tmp"), brickname, getpid());

        dump_fd = open (path, O_CREAT|O_RDWR|O_TRUNC|O_APPEND, 0600);
        if (dump_fd < 0)
                return -1;

        gf_dump_fd = dump_fd;
        return 0;
}


static void
gf_proc_dump_close (void)
{
        close (gf_dump_fd);
        gf_dump_fd = -1;
}


int
gf_proc_dump_add_section (char *key, ...)
{

        char buf[GF_DUMP_MAX_BUF_LEN];
        va_list ap;

        GF_ASSERT(key);

        memset (buf, 0, sizeof(buf));
        snprintf (buf, GF_DUMP_MAX_BUF_LEN, "\n[");
        va_start (ap, key);
        vsnprintf (buf + strlen(buf),
                   GF_DUMP_MAX_BUF_LEN - strlen (buf), key, ap);
        va_end (ap);
        snprintf (buf + strlen(buf),
                  GF_DUMP_MAX_BUF_LEN - strlen (buf),  "]\n");
        return write (gf_dump_fd, buf, strlen (buf));
}


int
gf_proc_dump_write (char *key, char *value,...)
{

        char         buf[GF_DUMP_MAX_BUF_LEN];
        int          offset = 0;
        va_list      ap;

        GF_ASSERT (key);

        offset = strlen (key);

        memset (buf, 0, GF_DUMP_MAX_BUF_LEN);
        snprintf (buf, GF_DUMP_MAX_BUF_LEN, "%s", key);
        snprintf (buf + offset, GF_DUMP_MAX_BUF_LEN - offset, "=");
        offset += 1;
        va_start (ap, value);
        vsnprintf (buf + offset, GF_DUMP_MAX_BUF_LEN - offset, value, ap);
        va_end (ap);

        offset = strlen (buf);
        snprintf (buf + offset, GF_DUMP_MAX_BUF_LEN - offset, "\n");
        return write (gf_dump_fd, buf, strlen (buf));
}

static void
gf_proc_dump_xlator_mem_info (xlator_t *xl)
{
        int     i = 0;
        struct mem_acct rec = {0,};

        if (!xl)
                return;

        if (!xl->mem_acct.rec)
                return;

        gf_proc_dump_add_section ("%s.%s - Memory usage", xl->type, xl->name);
        gf_proc_dump_write ("num_types", "%d", xl->mem_acct.num_types);

        for (i = 0; i < xl->mem_acct.num_types; i++) {
                if (!(memcmp (&xl->mem_acct.rec[i], &rec,
                              sizeof (struct mem_acct))))
                        continue;

                gf_proc_dump_add_section ("%s.%s - usage-type %d memusage",
                                          xl->type, xl->name, i);
                gf_proc_dump_write ("size", "%u", xl->mem_acct.rec[i].size);
                gf_proc_dump_write ("num_allocs", "%u",
                                    xl->mem_acct.rec[i].num_allocs);
                gf_proc_dump_write ("max_size", "%u",
                                    xl->mem_acct.rec[i].max_size);
                gf_proc_dump_write ("max_num_allocs", "%u",
                                    xl->mem_acct.rec[i].max_num_allocs);
                gf_proc_dump_write ("total_allocs", "%u",
                                    xl->mem_acct.rec[i].total_allocs);
        }

        return;
}

static void
gf_proc_dump_xlator_mem_info_only_in_use (xlator_t *xl)
{
        int     i = 0;

        if (!xl)
                return;

        if (!xl->mem_acct.rec)
                return;

        gf_proc_dump_add_section ("%s.%s - Memory usage", xl->type, xl->name);
        gf_proc_dump_write ("num_types", "%d", xl->mem_acct.num_types);

        for (i = 0; i < xl->mem_acct.num_types; i++) {
                if (!xl->mem_acct.rec[i].size)
                        continue;

                gf_proc_dump_add_section ("%s.%s - usage-type %d", xl->type,
                                          xl->name,i);

                gf_proc_dump_write ("size", "%u",
                                    xl->mem_acct.rec[i].size);
                gf_proc_dump_write ("max_size", "%u",
                                    xl->mem_acct.rec[i].max_size);
                gf_proc_dump_write ("num_allocs", "%u",
                                    xl->mem_acct.rec[i].num_allocs);
                gf_proc_dump_write ("max_num_allocs", "%u",
                                    xl->mem_acct.rec[i].max_num_allocs);
                gf_proc_dump_write ("total_allocs", "%u",
                                    xl->mem_acct.rec[i].total_allocs);
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
gf_proc_dump_mempool_info (glusterfs_ctx_t *ctx)
{
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
        }
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

        ctx = glusterfs_ctx_get ();

        trav = top;
        while (trav) {

                if (ctx->measure_latency)
                        gf_proc_dump_latency_info (trav);

                gf_proc_dump_xlator_mem_info(trav);

                if (GF_PROC_DUMP_IS_XL_OPTION_ENABLED (inode) &&
                    (trav->itable)) {
                        snprintf (itable_key, 1024, "%d.%s.itable",
                                  ctx->graph_id, trav->name);

                        inode_table_dump (trav->itable, itable_key);
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

                trav = trav->next;
        }

        return;
}

static void
gf_proc_dump_oldgraph_xlator_info (xlator_t *top)
{
        xlator_t        *trav = NULL;
        glusterfs_ctx_t *ctx = NULL;
        char             itable_key[1024] = {0,};

        if (!top)
                return;

        ctx = glusterfs_ctx_get ();

        trav = top;
        while (trav) {
                gf_proc_dump_xlator_mem_info_only_in_use (trav);

                if (GF_PROC_DUMP_IS_XL_OPTION_ENABLED (inode) &&
                    (trav->itable)) {
                        snprintf (itable_key, 1024, "%d.%s.itable",
                                  ctx->graph_id, trav->name);

                        inode_table_dump (trav->itable, itable_key);
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

        return 0;
}

static int
gf_proc_dump_parse_set_option (char *key, char *value)
{
        gf_boolean_t    *opt_key = NULL;
        gf_boolean_t    opt_value = _gf_false;
        char buf[GF_DUMP_MAX_BUF_LEN];
        int ret = -1;

        if (!strncasecmp (key, "all", 3)) {
                (void)gf_proc_dump_enable_all_options ();
                return 0;
        } else if (!strncasecmp (key, "mem", 3)) {
                opt_key = &dump_options.dump_mem;
        } else if (!strncasecmp (key, "iobuf", 5)) {
                opt_key = &dump_options.dump_iobuf;
        } else if (!strncasecmp (key, "callpool", 8)) {
                opt_key = &dump_options.dump_callpool;
        } else if (!strncasecmp (key, "priv", 4)) {
                opt_key = &dump_options.xl_options.dump_priv;
        } else if (!strncasecmp (key, "fd", 2)) {
                opt_key = &dump_options.xl_options.dump_fd;
        } else if (!strncasecmp (key, "inode", 5)) {
                opt_key = &dump_options.xl_options.dump_inode;
        } else if (!strncasecmp (key, "inodectx", strlen ("inodectx"))) {
                opt_key = &dump_options.xl_options.dump_inodectx;
        } else if (!strncasecmp (key, "fdctx", strlen ("fdctx"))) {
                opt_key = &dump_options.xl_options.dump_fdctx;
        }

        if (!opt_key) {
                //None of dump options match the key, return back
                snprintf (buf, sizeof (buf), "[Warning]:None of the options "
                          "matched key : %s\n", key);
                ret = write (gf_dump_fd, buf, strlen (buf));

                /* warning suppression */
                if (ret >= 0) {
                        ret = -1;
                        goto out;
                }

        }

        opt_value = (strncasecmp (value, "yes", 3) ?
                     _gf_false: _gf_true);

        GF_PROC_DUMP_SET_OPTION (*opt_key, opt_value);

        ret = 0;
out:
        return ret;
}

static int
gf_proc_dump_options_init (char *dump_name)
{
        int     ret = -1;
        FILE    *fp = NULL;
        char    buf[256];
        char    dumpbuf[GF_DUMP_MAX_BUF_LEN];
        char    *key = NULL, *value = NULL;
        char    *saveptr = NULL;
        char    dump_option_file[PATH_MAX];

        snprintf (dump_option_file, sizeof (dump_option_file),
                  "/tmp/glusterdump.%d.options", getpid ());

        fp = fopen (dump_option_file, "r");

        if (!fp) {
                //ENOENT, return success
                (void) gf_proc_dump_enable_all_options ();
                return 0;
        }

        (void) gf_proc_dump_disable_all_options ();

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

                snprintf (dumpbuf, sizeof (dumpbuf), "[Debug]:key=%s, value=%s\n",key,value);
                ret = write (gf_dump_fd, dumpbuf, strlen (dumpbuf));

                gf_proc_dump_parse_set_option (key, value);

        }

        return 0;
}

void
gf_proc_dump_info (int signum)
{
        int                i    = 0;
        int                ret  = -1;
        glusterfs_ctx_t   *ctx  = NULL;
        glusterfs_graph_t *trav = NULL;
        char               brick_name[PATH_MAX] = {0,};

        gf_proc_dump_lock ();

        ctx = glusterfs_ctx_get ();
        if (!ctx)
                goto out;

        if (ctx->cmd_args.brick_name) {
                GF_REMOVE_SLASH_FROM_PATH (ctx->cmd_args.brick_name, brick_name);
        } else
                strncpy (brick_name, "glusterdump", sizeof (brick_name));

        ret = gf_proc_dump_options_init (brick_name);
        if (ret < 0)
                goto out;

        ret = gf_proc_dump_open (ctx->statedump_path, brick_name);
        if (ret < 0)
                goto out;

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

        gf_proc_dump_close ();
out:
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
