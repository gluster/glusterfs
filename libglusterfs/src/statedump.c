/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
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

#include <stdarg.h>
#include "glusterfs.h"
#include "logging.h"
#include "iobuf.h"
#include "statedump.h"
#include "stack.h"

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
gf_proc_dump_open (void)
{
        char path[256];
        int  dump_fd = -1;

        memset (path, 0, sizeof (path));
        snprintf (path, sizeof (path), "%s.%d", GF_DUMP_LOGFILE_ROOT, getpid ());

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


void
gf_proc_dump_add_section (char *key, ...)
{

        char buf[GF_DUMP_MAX_BUF_LEN];
        va_list ap;
        int     ret;

        GF_ASSERT(key);

        memset (buf, 0, sizeof(buf));
        snprintf (buf, GF_DUMP_MAX_BUF_LEN, "\n[");
        va_start (ap, key);
        vsnprintf (buf + strlen(buf),
                   GF_DUMP_MAX_BUF_LEN - strlen (buf), key, ap);
        va_end (ap);
        snprintf (buf + strlen(buf),
                  GF_DUMP_MAX_BUF_LEN - strlen (buf),  "]\n");
        ret = write (gf_dump_fd, buf, strlen (buf));
}


void
gf_proc_dump_write (char *key, char *value,...)
{

        char         buf[GF_DUMP_MAX_BUF_LEN];
        int          offset = 0;
        va_list      ap;
        int          ret;

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
        ret = write (gf_dump_fd, buf, strlen (buf));
}

static void
gf_proc_dump_xlator_mem_info (xlator_t *xl)
{
        char    key[GF_DUMP_MAX_BUF_LEN];
        char    prefix[GF_DUMP_MAX_BUF_LEN];
        int     i = 0;
        struct mem_acct rec = {0,};

        if (!xl)
                return;

        if (!xl->mem_acct.rec)
                return;

        gf_proc_dump_add_section ("%s.%s - Memory usage", xl->type,xl->name);
        gf_proc_dump_write ("num_types", "%d", xl->mem_acct.num_types);

        for (i = 0; i < xl->mem_acct.num_types; i++) {
                if (!(memcmp (&xl->mem_acct.rec[i], &rec,
                              sizeof (struct mem_acct))))
                        continue;

                gf_proc_dump_add_section ("%s.%s - usage-type %d", xl->type,
                                          xl->name,i);
                gf_proc_dump_build_key (prefix, "memusage", "%s.%s.type.%d",
                                        xl->type, xl->name, i);
                gf_proc_dump_build_key (key, prefix, "size");
                gf_proc_dump_write (key, "%u", xl->mem_acct.rec[i].size);
                gf_proc_dump_build_key (key, prefix, "num_allocs");
                gf_proc_dump_write (key, "%u", xl->mem_acct.rec[i].num_allocs);
                gf_proc_dump_build_key (key, prefix, "max_size");
                gf_proc_dump_write (key, "%u", xl->mem_acct.rec[i].max_size);
                gf_proc_dump_build_key (key, prefix, "max_num_allocs");
                gf_proc_dump_write (key, "%u", xl->mem_acct.rec[i].max_num_allocs);
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

void gf_proc_dump_latency_info (xlator_t *xl);

void
gf_proc_dump_xlator_info (xlator_t *this_xl)
{
        glusterfs_ctx_t   *ctx = NULL;
        xlator_t *fuse_xlator, *this_xlator;

        if (!this_xl)
                return;

        ctx = glusterfs_ctx_get ();
        if (!ctx)
                return;

        if (ctx->master){

                fuse_xlator = (xlator_t *) ctx->master;

                if (!fuse_xlator->dumpops)
                        return;

                if (fuse_xlator->dumpops->priv &&
                    GF_PROC_DUMP_IS_XL_OPTION_ENABLED (priv))
                        fuse_xlator->dumpops->priv (fuse_xlator);

                if (fuse_xlator->dumpops->inode &&
                    GF_PROC_DUMP_IS_XL_OPTION_ENABLED (inode)) {

                        if (!ctx->active)
                                return;
                        this_xlator = (xlator_t *) ctx->active->top;

                        if (this_xlator && this_xlator->itable)
                                inode_table_dump (this_xlator->itable,
                                                  "xlator.mount.fuse.itable");
                        else
                                return;
                }

                if (fuse_xlator->dumpops->fd &&
                    GF_PROC_DUMP_IS_XL_OPTION_ENABLED (fd))
                        fuse_xlator->dumpops->fd (fuse_xlator);
        }


        while (this_xl) {

                if (ctx->measure_latency)
                        gf_proc_dump_latency_info (this_xl);

                gf_proc_dump_xlator_mem_info(this_xl);

                if (!this_xl->dumpops) {
                        this_xl = this_xl->next;
                        continue;
                }

                if (this_xl->dumpops->priv &&
                    GF_PROC_DUMP_IS_XL_OPTION_ENABLED (priv))
                        this_xl->dumpops->priv (this_xl);

                if (this_xl->dumpops->inode &&
                    GF_PROC_DUMP_IS_XL_OPTION_ENABLED (inode))
                        this_xl->dumpops->inode (this_xl);


                if (this_xl->dumpops->fd &&
                    GF_PROC_DUMP_IS_XL_OPTION_ENABLED (fd))
                        this_xl->dumpops->fd (this_xl);

                this_xl = this_xl->next;
        }

        return;
}
static int
gf_proc_dump_parse_set_option (char *key, char *value)
{
        gf_boolean_t    *opt_key = NULL;
        gf_boolean_t    opt_value = _gf_false;
        char buf[GF_DUMP_MAX_BUF_LEN];

        if (!strncasecmp (key, "mem", 3)) {
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
                write (gf_dump_fd, buf, strlen (buf));

                return -1;
        }

        opt_value = (strncasecmp (value, "yes", 3) ?
                     _gf_false: _gf_true);

        GF_PROC_DUMP_SET_OPTION (*opt_key, opt_value);

        return 0;
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
gf_proc_dump_options_init ()
{
        int     ret = -1;
        FILE    *fp = NULL;
        char    buf[256];
        char    dumpbuf[GF_DUMP_MAX_BUF_LEN];
        char    *key = NULL, *value = NULL;
        char    *saveptr = NULL;


        fp = fopen (GF_DUMP_OPTIONFILE, "r");

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
                write (gf_dump_fd, dumpbuf, strlen (dumpbuf));

                gf_proc_dump_parse_set_option (key, value);

        }

        return 0;
}

void
gf_proc_dump_info (int signum)
{
        int               ret = -1;
        glusterfs_ctx_t   *ctx = NULL;


        gf_proc_dump_lock ();
        ret = gf_proc_dump_open ();
        if (ret < 0)
                goto out;

        ret = gf_proc_dump_options_init ();

        if (ret < 0)
                goto out;

        if (GF_PROC_DUMP_IS_OPTION_ENABLED (mem))
                gf_proc_dump_mem_info ();

        ctx = glusterfs_ctx_get ();

        if (ctx) {
                if (GF_PROC_DUMP_IS_OPTION_ENABLED (iobuf))
                        iobuf_stats_dump (ctx->iobuf_pool);
                if (GF_PROC_DUMP_IS_OPTION_ENABLED (callpool))
                        gf_proc_dump_pending_frames (ctx->pool);
                gf_proc_dump_xlator_info (ctx->active->top);

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
