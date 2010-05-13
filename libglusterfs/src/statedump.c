/*
  Copyright (c) 2009 Gluster, Inc. <http://www.gluster.com>
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

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif /* MALLOC_H */


#define GF_PROC_DUMP_IS_OPTION_ENABLED(opt) \
        (dump_options.dump_##opt == _gf_true)

#define GF_PROC_DUMP_IS_XL_OPTION_ENABLED(opt)\
        (dump_options.xl_options.dump_##opt == _gf_true)

static pthread_mutex_t  gf_proc_dump_mutex;
static int gf_dump_fd = -1;
static gf_dump_options_t dump_options;

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

        memset (path, 0, sizeof(path));
        snprintf (path, sizeof (path), "%s.%d",GF_DUMP_LOGFILE_ROOT,
                  getpid ());

        dump_fd = open (path, O_CREAT|O_RDWR|O_TRUNC|O_APPEND, 0600);
        if (dump_fd < 0) {
                gf_log ("", GF_LOG_ERROR, "Unable to open file: %s"
                " errno: %d", path, errno);
                return -1;
        }

	gf_dump_fd = dump_fd;
        return 0;
}

static int
gf_proc_dump_parse_set_option (char *key, char *value)
{
        gf_boolean_t    *opt_key = NULL;
        gf_boolean_t    opt_value = _gf_false;


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
        }

        if (!opt_key) {
                //None of dump options match the key, return back
                gf_log ("", GF_LOG_WARNING, "None of the options matched key"
                        ": %s", key);
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

        return 0;
}

static int
gf_proc_dump_options_init ()
{
        int     ret = -1;
        FILE    *fp = NULL;
        char    buf[256];
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

                gf_log ("", GF_LOG_DEBUG, "key = %s, value = %s",
                        key, value);

                gf_proc_dump_parse_set_option (key, value);

        }

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

        char    buf[GF_DUMP_MAX_BUF_LEN];
        va_list ap;
        int     ret = -1;

        assert (key);
        memset (buf, 0, sizeof(buf));
        snprintf (buf, GF_DUMP_MAX_BUF_LEN, "\n[");
        va_start (ap, key);
        vsnprintf (buf + strlen (buf),
                        GF_DUMP_MAX_BUF_LEN - strlen (buf), key, ap);
        va_end (ap);
        snprintf (buf + strlen (buf),
                        GF_DUMP_MAX_BUF_LEN - strlen (buf),  "]\n");
        ret = write (gf_dump_fd, buf, strlen (buf));
}

void
gf_proc_dump_write (char *key, char *value,...)
{

        char         buf[GF_DUMP_MAX_BUF_LEN];
        int          offset = 0;
        va_list      ap;
        int          ret = -1;

        assert (key);

        offset = strlen (key);

        memset (buf, 0, GF_DUMP_MAX_BUF_LEN);
        snprintf (buf, GF_DUMP_MAX_BUF_LEN, "%s",key);
        snprintf (buf + offset, GF_DUMP_MAX_BUF_LEN - offset, "=");
        offset += 1;
        va_start (ap, value);
        vsnprintf (buf + offset, GF_DUMP_MAX_BUF_LEN - offset, value, ap);
        va_end (ap);

        offset = strlen (buf);
        snprintf (buf + offset, GF_DUMP_MAX_BUF_LEN - offset, "\n");
        ret = write (gf_dump_fd, buf, strlen (buf));
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
        gf_proc_dump_write ("mallinfo_smblks","%d", info.smblks);
        gf_proc_dump_write ("mallinfo_hblks","%d", info.hblks);
        gf_proc_dump_write ("mallinfo_hblkhd", "%d", info.hblkhd);
        gf_proc_dump_write ("mallinfo_usmblks","%d", info.usmblks);
        gf_proc_dump_write ("mallinfo_fsmblks","%d", info.fsmblks);
        gf_proc_dump_write ("mallinfo_uordblks","%d", info.uordblks);
        gf_proc_dump_write ("mallinfo_fordblks", "%d", info.fordblks);
        gf_proc_dump_write ("mallinfo_keepcost", "%d", info.keepcost);
#endif

}


void
gf_proc_dump_xlator_info (xlator_t *this_xl)
{

        if (!this_xl)
                return;

        while (this_xl) {
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

        ctx = get_global_ctx_ptr();
        if (ctx) {
                if (GF_PROC_DUMP_IS_OPTION_ENABLED (iobuf))
                        iobuf_stats_dump (ctx->iobuf_pool);
                if (GF_PROC_DUMP_IS_OPTION_ENABLED (callpool))
                        gf_proc_dump_pending_frames (ctx->pool);
                gf_proc_dump_xlator_info (ctx->graph);
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

