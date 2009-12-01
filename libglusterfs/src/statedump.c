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

static pthread_mutex_t  gf_proc_dump_mutex;
static int gf_dump_fd = -1;

static void 
gf_proc_dump_lock (void)
{
	pthread_mutex_lock(&gf_proc_dump_mutex);
}

static void 
gf_proc_dump_unlock (void)
{
	pthread_mutex_unlock(&gf_proc_dump_mutex);
} 

static int
gf_proc_dump_open (void)
{
        char path[256];
        int  dump_fd = -1;

        memset(path, 0, sizeof(path));
        snprintf(path, sizeof(path), "%s.%d",GF_DUMP_LOGFILE_ROOT, getpid());

        dump_fd = open(path, O_CREAT|O_RDWR|O_TRUNC|O_APPEND, 0600);
        if (dump_fd < 0) {
                gf_log("", GF_LOG_ERROR, "Unable to open file: %s"
                " errno: %d", path, errno);
                return -1;
        }

	gf_dump_fd = dump_fd;
        return 0;
}

static void
gf_proc_dump_close (void)
{
        close(gf_dump_fd);
        gf_dump_fd = -1;
}

void
gf_proc_dump_add_section (char *key, ...) 
{
		
        char buf[GF_DUMP_MAX_BUF_LEN];
        va_list ap;
        int     ret;

	assert(key);
                
        memset(buf, 0, sizeof(buf));
        snprintf(buf, GF_DUMP_MAX_BUF_LEN, "\n[");
        va_start(ap, key);
        vsnprintf(buf + strlen(buf), 
                        GF_DUMP_MAX_BUF_LEN - strlen(buf), key, ap);
        va_end(ap);
        snprintf(buf + strlen(buf),
                        GF_DUMP_MAX_BUF_LEN - strlen(buf),  "]\n");
        ret = write(gf_dump_fd, buf, strlen(buf));
}

void
gf_proc_dump_write (char *key, char *value,...) 
{ 
    
        char buf[GF_DUMP_MAX_BUF_LEN];
        int  offset = 0;
	va_list      ap;
        int          ret;
    
        offset = strlen(key);
                
        memset(buf, 0, GF_DUMP_MAX_BUF_LEN);
        snprintf(buf, GF_DUMP_MAX_BUF_LEN, "%s",key);
        snprintf(buf + offset, GF_DUMP_MAX_BUF_LEN - offset, "=");
        offset += 1;
        va_start(ap, value);
        vsnprintf(buf + offset, GF_DUMP_MAX_BUF_LEN - offset, value, ap); 
        va_end(ap);

        offset = strlen(buf);
        snprintf(buf + offset, GF_DUMP_MAX_BUF_LEN - offset, "\n");
        ret = write(gf_dump_fd, buf, strlen(buf));
}


/* Currently this dumps only mallinfo. More can be built on here */
void
gf_proc_dump_mem_info ()
{
#ifdef HAVE_MALLOC_STATS
        struct mallinfo info;

        memset(&info, 0, sizeof(struct mallinfo));
        info = mallinfo();

        gf_proc_dump_add_section("mallinfo");
        gf_proc_dump_write("mallinfo_arena", "%d", info.arena);
        gf_proc_dump_write("mallinfo_ordblks", "%d", info.ordblks);
        gf_proc_dump_write("mallinfo_smblks","%d", info.smblks);
        gf_proc_dump_write("mallinfo_hblks","%d", info.hblks);
        gf_proc_dump_write("mallinfo_hblkhd", "%d", info.hblkhd);
        gf_proc_dump_write("mallinfo_usmblks","%d", info.usmblks);
        gf_proc_dump_write("mallinfo_fsmblks","%d", info.fsmblks);
        gf_proc_dump_write("mallinfo_uordblks","%d", info.uordblks);
        gf_proc_dump_write("mallinfo_fordblks", "%d", info.fordblks);
        gf_proc_dump_write("mallinfo_keepcost", "%d", info.keepcost);
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
                if (this_xl->dumpops->priv)
                        this_xl->dumpops->priv(this_xl);
                if (this_xl->dumpops->inode)
                        this_xl->dumpops->inode(this_xl);
                if (this_xl->dumpops->fd)
                        this_xl->dumpops->fd(this_xl);
                this_xl = this_xl->next;
        }

        return;
}


void
gf_proc_dump_info (int signum)
{
        int               ret = -1;
        glusterfs_ctx_t   *ctx = NULL;
    
        gf_proc_dump_lock();
        ret = gf_proc_dump_open();
        if (ret < 0) 
                goto out;
        gf_proc_dump_mem_info();
	ctx = get_global_ctx_ptr();
        if (ctx) {
                iobuf_stats_dump(ctx->iobuf_pool);
                gf_proc_dump_pending_frames(ctx->pool);
                gf_proc_dump_xlator_info(ctx->graph);
        }
        
        gf_proc_dump_close();
out:
        gf_proc_dump_unlock();

        return;
}

void 
gf_proc_dump_fini (void)
{
	pthread_mutex_destroy(&gf_proc_dump_mutex);
}


void
gf_proc_dump_init ()
{
	pthread_mutex_init(&gf_proc_dump_mutex, NULL);

	return;
}

void
gf_proc_dump_cleanup (void)
{
	pthread_mutex_destroy(&gf_proc_dump_mutex);
}

