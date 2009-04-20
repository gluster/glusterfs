/*
  Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <errno.h>
#include <libgen.h>
#include <stddef.h>

#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xlator.h>
#include <timer.h>
#include "defaults.h"
#include <time.h>
#include <poll.h>
#include "transport.h"
#include "event.h"
#include "libglusterfsclient.h"
#include "libglusterfsclient-internals.h"
#include "compat.h"
#include "compat-errno.h"
#include <sys/vfs.h>
#include <utime.h>
#include <sys/param.h>
#include <list.h>
#include <stdarg.h>
#include <sys/statvfs.h>

#define LIBGF_XL_NAME "libglusterfsclient"
#define LIBGLUSTERFS_INODE_TABLE_LRU_LIMIT 1000 //14057

static inline xlator_t *
libglusterfs_graph (xlator_t *graph);
int32_t libgf_client_readlink (libglusterfs_client_ctx_t *ctx, loc_t *loc,
                                        char *buf, size_t bufsize);

static int first_init = 1;
static int first_fini = 1;

/* The global list of virtual mount points. This is only the
 * head of list so will be empty.*/
struct vmp_entry vmplist;
int vmpentries = 0;

#define libgf_vmp_virtual_path(entry, path) ((char *)(path + (entry->vmplen-1)))

char *
zr_build_process_uuid ()
{
	char           tmp_str[1024] = {0,};
	char           hostname[256] = {0,};
	struct timeval tv = {0,};
	struct tm      now = {0, };
	char           now_str[32];

	if (-1 == gettimeofday(&tv, NULL)) {
		gf_log ("", GF_LOG_ERROR, 
			"gettimeofday: failed %s",
			strerror (errno));		
	}

	if (-1 == gethostname (hostname, 256)) {
		gf_log ("", GF_LOG_ERROR, 
			"gethostname: failed %s",
			strerror (errno));
	}

	localtime_r (&tv.tv_sec, &now);
	strftime (now_str, 32, "%Y/%m/%d-%H:%M:%S", &now);
	snprintf (tmp_str, 1024, "%s-%d-%s:%ld", 
		  hostname, getpid(), now_str, tv.tv_usec);
	
	return strdup (tmp_str);
}


int32_t
libgf_client_forget (xlator_t *this,
		     inode_t *inode)
{
        uint64_t ptr = 0;
	libglusterfs_client_inode_ctx_t *ctx = NULL;
	
	inode_ctx_del (inode, this, &ptr);
        ctx = (libglusterfs_client_inode_ctx_t *)(long) ptr;

	FREE (ctx);

        return 0;
}

xlator_t *
libgf_inode_to_xlator (inode_t *inode)
{
        if (!inode)
                return NULL;

        if (!inode->table)
                return NULL;

        if (!inode->table->xl)
                return NULL;

        if (!inode->table->xl->ctx)
                return NULL;

        return inode->table->xl->ctx->top;
}

libglusterfs_client_fd_ctx_t *
libgf_get_fd_ctx (fd_t *fd)
{
        uint64_t                        ctxaddr = 0;
        libglusterfs_client_fd_ctx_t    *ctx = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, fd, out);

        if (fd_ctx_get (fd, libgf_inode_to_xlator (fd->inode), &ctxaddr) == -1)
                goto out;

        ctx = (libglusterfs_client_fd_ctx_t *)(long)ctxaddr;

out:
        return ctx;
}

libglusterfs_client_fd_ctx_t *
libgf_alloc_fd_ctx (libglusterfs_client_ctx_t *ctx, fd_t *fd)
{
        libglusterfs_client_fd_ctx_t    *fdctx = NULL;
        uint64_t                        ctxaddr = 0;

        fdctx = CALLOC (1, sizeof (*fdctx));
        if (fdctx == NULL) {
                gf_log (LIBGF_XL_NAME, GF_LOG_ERROR,
                        "memory allocation failure");
                fdctx = NULL;
                goto out;
        }

        pthread_mutex_init (&fdctx->lock, NULL);
        fdctx->ctx = ctx;
        ctxaddr = (uint64_t) (long)fdctx;

        fd_ctx_set (fd, libgf_inode_to_xlator (fd->inode), ctxaddr);

out:
        return fdctx;
}

libglusterfs_client_fd_ctx_t *
libgf_del_fd_ctx (fd_t *fd)
{
        uint64_t                        ctxaddr = 0;
        libglusterfs_client_fd_ctx_t    *ctx = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, fd, out);

        if (fd_ctx_del (fd, libgf_inode_to_xlator (fd->inode) , &ctxaddr) == -1)
                goto out;

        ctx = (libglusterfs_client_fd_ctx_t *)(long)ctxaddr;

out:
        return ctx;
}

int32_t
libgf_client_release (xlator_t *this,
		      fd_t *fd)
{
	libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        fd_ctx = libgf_del_fd_ctx (fd);
        if (fd_ctx != NULL) {
                pthread_mutex_destroy (&fd_ctx->lock);
                FREE (fd_ctx);
        }

	return 0;
}

libglusterfs_client_inode_ctx_t *
libgf_get_inode_ctx (inode_t *inode)
{
        uint64_t                                ctxaddr = 0;
        libglusterfs_client_inode_ctx_t         *ictx = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, inode, out);
        if (inode_ctx_get (inode, libgf_inode_to_xlator (inode), &ctxaddr) < 0)
                goto out;

        ictx = (libglusterfs_client_inode_ctx_t *)(long)ctxaddr;

out:
        return ictx;
}

libglusterfs_client_inode_ctx_t *
libgf_del_inode_ctx (inode_t *inode)
{
        uint64_t                                ctxaddr = 0;
        libglusterfs_client_inode_ctx_t         *ictx = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, inode, out);
        if (inode_ctx_del (inode, libgf_inode_to_xlator (inode), &ctxaddr) < 0)
                goto out;

        ictx = (libglusterfs_client_inode_ctx_t *)(long)ctxaddr;

out:
        return ictx;
}

libglusterfs_client_inode_ctx_t *
libgf_alloc_inode_ctx (libglusterfs_client_ctx_t *ctx, inode_t *inode)
{
        uint64_t                                ctxaddr = 0;
        libglusterfs_client_inode_ctx_t         *ictx = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, inode, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        ictx = CALLOC (1, sizeof (libglusterfs_client_ctx_t));
        if (ictx == NULL) {
                gf_log (LIBGF_XL_NAME, GF_LOG_ERROR,
                                "memory allocation failure");
                goto out;
        }

        pthread_mutex_init (&ictx->lock, NULL);
        ctxaddr = (uint64_t) (long)ictx;
        if (inode_ctx_put (inode, libgf_inode_to_xlator (inode), ctxaddr) < 0){
                FREE (ictx);
                ictx = NULL;
        }

out:
        return ictx;
}

#define LIBGF_UPDATE_LOOKUP     0x1
#define LIBGF_UPDATE_STAT       0x2
#define LIBGF_UPDATE_ALL        (LIBGF_UPDATE_LOOKUP | LIBGF_UPDATE_STAT)

int
libgf_update_iattr_cache (inode_t *inode, int flags, struct stat *buf)
{
        libglusterfs_client_inode_ctx_t *inode_ctx = NULL;
        time_t                          current = 0;
        int                             op_ret = -1;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, inode, out);

        inode_ctx = libgf_get_inode_ctx (inode);
        if (!inode_ctx) {
                errno = EINVAL;
                op_ret = -1;
                goto out;
        }

        pthread_mutex_lock (&inode_ctx->lock);
        {
                /* Take a timestamp only after we've acquired the
                 * lock.
                 */
                current = time (NULL);
                if (flags & LIBGF_UPDATE_LOOKUP)
                        inode_ctx->previous_lookup_time = current;

                if (flags & LIBGF_UPDATE_STAT) {

                        /* Update the cached stat struct only if a new
                         * stat buf is given.
                         */
                        if (buf != NULL) {
                                inode_ctx->previous_stat_time = current;
                                memcpy (&inode_ctx->stbuf, buf,
                                                sizeof (inode_ctx->stbuf));
                        }
                }
        }
        pthread_mutex_unlock (&inode_ctx->lock);
        op_ret = 0;

out:
        return op_ret;
}

int32_t
libgf_client_releasedir (xlator_t *this,
			 fd_t *fd)
{
	libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        fd_ctx = libgf_del_fd_ctx (fd);
        if (fd_ctx != NULL) {
                pthread_mutex_destroy (&fd_ctx->lock);
                FREE (fd_ctx);
        }

	return 0;
}


void *poll_proc (void *ptr)
{
        glusterfs_ctx_t *ctx = ptr;

        event_dispatch (ctx->event_pool);

        return NULL;
}


int32_t
xlator_graph_init (xlator_t *xl)
{
        xlator_t *trav = xl;
        int32_t ret = -1;

        while (trav->prev)
                trav = trav->prev;

        while (trav) {
                if (!trav->ready) {
                        ret = xlator_tree_init (trav);
                        if (ret < 0)
                                break;
                }
                trav = trav->next;
        }

        return ret;
}


void
xlator_graph_fini (xlator_t *xl)
{
	xlator_t *trav = xl;
	while (trav->prev)
		trav = trav->prev;

	while (trav) {
		if (!trav->init_succeeded) {
			break;
		}

		xlator_tree_fini (trav);
		trav = trav->next;
	}
}


void 
libgf_client_loc_wipe (loc_t *loc)
{
	if (loc->path) {
		FREE (loc->path);
	}

	if (loc->parent) { 
		inode_unref (loc->parent);
		loc->parent = NULL;
	}

	if (loc->inode) {
		inode_unref (loc->inode);
		loc->inode = NULL;
	}

	loc->path = loc->name = NULL;
}


int32_t
libgf_client_loc_fill (loc_t *loc,
		       libglusterfs_client_ctx_t *ctx,
		       ino_t ino,
		       ino_t par,
		       const char *name)
{
        inode_t *inode = NULL, *parent = NULL;
	int32_t ret = -1;
	char *path = NULL;

        /* resistance against multiple invocation of loc_fill not to get
           reference leaks via inode_search() */

        inode = loc->inode;
	
        if (!inode) {
                if (ino)
                        inode = inode_search (ctx->itable, ino, NULL);
                if (par && name)
                        inode = inode_search (ctx->itable, par, name);

                loc->inode = inode;
        }

        if (inode)
                loc->ino = inode->ino;

        parent = loc->parent;
        if (!parent) {
                if (inode)
                        parent = inode_parent (inode, par, name);
                else
                        parent = inode_search (ctx->itable, par, NULL);
                loc->parent = parent;
        }
  
	if (!loc->path) {
		if (name && parent) {
			ret = inode_path (parent, name, &path);
			if (ret <= 0) {
				gf_log ("glusterfs-fuse", GF_LOG_ERROR,
					"inode_path failed for %"PRId64"/%s",
					parent->ino, name);
				goto fail;
			} else {
				loc->path = path;
			}
		} else 	if (inode) {
			ret = inode_path (inode, NULL, &path);
			if (ret <= 0) {
				gf_log ("glusterfs-fuse", GF_LOG_ERROR,
					"inode_path failed for %"PRId64,
					inode->ino);
				goto fail;
			} else {
				loc->path = path;
			}
		}
	}

	if (loc->path) {
		loc->name = strrchr (loc->path, '/');
		if (loc->name)
			loc->name++;
		else loc->name = "";
	}
	
	if ((ino != 1) &&
	    (parent == NULL)) {
		gf_log ("fuse-bridge", GF_LOG_ERROR,
			"failed to search parent for %"PRId64"/%s (%"PRId64")",
			(ino_t)par, name, (ino_t)ino);
		ret = -1;
		goto fail;
	}
	ret = 0;
fail:
	return ret;
}


static call_frame_t *
get_call_frame_for_req (libglusterfs_client_ctx_t *ctx, char d)
{
        call_pool_t  *pool = ctx->gf_ctx.pool;
        xlator_t     *this = ctx->gf_ctx.graph;
        call_frame_t *frame = NULL;
  

        frame = create_frame (this, pool);

        frame->root->uid = geteuid ();
        frame->root->gid = getegid ();
        frame->root->pid = getpid ();
        frame->root->unique = ctx->counter++;
  
        return frame;
}

void 
libgf_client_fini (xlator_t *this)
{
	FREE (this->private);
        return;
}


int32_t
libgf_client_notify (xlator_t *this, 
                     int32_t event,
                     void *data, 
                     ...)
{
        libglusterfs_client_private_t *priv = this->private;

        switch (event)
        {
        case GF_EVENT_CHILD_UP:
                pthread_mutex_lock (&priv->lock);
                {
                        priv->complete = 1;
                        pthread_cond_broadcast (&priv->init_con_established);
                }
                pthread_mutex_unlock (&priv->lock);
                break;

        default:
                default_notify (this, event, data);
        }

        return 0;
}

int32_t 
libgf_client_init (xlator_t *this)
{
        return 0;
}

glusterfs_handle_t 
glusterfs_init (glusterfs_init_params_t *init_ctx)
{
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_private_t *priv = NULL;
        FILE *specfp = NULL;
        xlator_t *graph = NULL, *trav = NULL;
        call_pool_t *pool = NULL;
        int32_t ret = 0;
        struct rlimit lim;
	uint32_t xl_count = 0;

        if (!init_ctx || (!init_ctx->specfile && !init_ctx->specfp)) {
                errno = EINVAL;
                return NULL;
        }

        ctx = CALLOC (1, sizeof (*ctx));
        if (!ctx) {
		fprintf (stderr, 
			 "libglusterfsclient: %s:%s():%d: out of memory\n",
			 __FILE__, __PRETTY_FUNCTION__, __LINE__);

                errno = ENOMEM;
                return NULL;
        }

        ctx->lookup_timeout = init_ctx->lookup_timeout;
        ctx->stat_timeout = init_ctx->stat_timeout;

        pthread_mutex_init (&ctx->gf_ctx.lock, NULL);
  
        pool = ctx->gf_ctx.pool = CALLOC (1, sizeof (call_pool_t));
        if (!pool) {
                errno = ENOMEM;
                FREE (ctx);
                return NULL;
        }

        LOCK_INIT (&pool->lock);
        INIT_LIST_HEAD (&pool->all_frames);

	/* FIXME: why is count hardcoded to 16384 */
        ctx->gf_ctx.event_pool = event_pool_new (16384);
        ctx->gf_ctx.page_size  = 128 * 1024;
        ctx->gf_ctx.iobuf_pool = iobuf_pool_new (8 * 1048576,
                                                 ctx->gf_ctx.page_size);

        lim.rlim_cur = RLIM_INFINITY;
        lim.rlim_max = RLIM_INFINITY;
        setrlimit (RLIMIT_CORE, &lim);
        setrlimit (RLIMIT_NOFILE, &lim);

        ctx->gf_ctx.cmd_args.log_level = GF_LOG_WARNING;

        if (init_ctx->logfile)
                ctx->gf_ctx.cmd_args.log_file = strdup (init_ctx->logfile);
        else
                ctx->gf_ctx.cmd_args.log_file = strdup ("/dev/stderr");

        if (init_ctx->loglevel) {
                if (!strncasecmp (init_ctx->loglevel, "DEBUG",
                                  strlen ("DEBUG"))) {
                        ctx->gf_ctx.cmd_args.log_level = GF_LOG_DEBUG;
                } else if (!strncasecmp (init_ctx->loglevel, "WARNING",
                                         strlen ("WARNING"))) {
                        ctx->gf_ctx.cmd_args.log_level = GF_LOG_WARNING;
                } else if (!strncasecmp (init_ctx->loglevel, "CRITICAL",
                                         strlen ("CRITICAL"))) {
                        ctx->gf_ctx.cmd_args.log_level = GF_LOG_CRITICAL;
                } else if (!strncasecmp (init_ctx->loglevel, "NONE",
                                         strlen ("NONE"))) {
                        ctx->gf_ctx.cmd_args.log_level = GF_LOG_NONE;
                } else if (!strncasecmp (init_ctx->loglevel, "ERROR",
                                         strlen ("ERROR"))) {
                        ctx->gf_ctx.cmd_args.log_level = GF_LOG_ERROR;
                } else {
			fprintf (stderr, 
				 "libglusterfsclient: %s:%s():%d: Unrecognized log-level \"%s\", possible values are \"DEBUG|WARNING|[ERROR]|CRITICAL|NONE\"\n",
                                 __FILE__, __PRETTY_FUNCTION__, __LINE__,
                                 init_ctx->loglevel);
			FREE (ctx->gf_ctx.cmd_args.log_file);
                        FREE (ctx->gf_ctx.pool);
                        FREE (ctx->gf_ctx.event_pool);
                        FREE (ctx);
                        errno = EINVAL;
                        return NULL;
                }
        }

	if (first_init)
        {
                ret = gf_log_init (ctx->gf_ctx.cmd_args.log_file);
                if (ret == -1) {
			fprintf (stderr, 
				 "libglusterfsclient: %s:%s():%d: failed to open logfile \"%s\"\n", 
				 __FILE__, __PRETTY_FUNCTION__, __LINE__, 
				 ctx->gf_ctx.cmd_args.log_file);
			FREE (ctx->gf_ctx.cmd_args.log_file);
                        FREE (ctx->gf_ctx.pool);
                        FREE (ctx->gf_ctx.event_pool);
                        FREE (ctx);
                        return NULL;
                }

                gf_log_set_loglevel (ctx->gf_ctx.cmd_args.log_level);
        }

        if (init_ctx->specfp) {
                specfp = init_ctx->specfp;
                if (fseek (specfp, 0L, SEEK_SET)) {
			fprintf (stderr, 
				 "libglusterfsclient: %s:%s():%d: fseek on volume file stream failed (%s)\n",
                                 __FILE__, __PRETTY_FUNCTION__, __LINE__,
                                 strerror (errno));
			FREE (ctx->gf_ctx.cmd_args.log_file);
                        FREE (ctx->gf_ctx.pool);
                        FREE (ctx->gf_ctx.event_pool);
                        FREE (ctx);
                        return NULL;
                }
        } else if (init_ctx->specfile) { 
                specfp = fopen (init_ctx->specfile, "r");
                ctx->gf_ctx.cmd_args.volume_file = strdup (init_ctx->specfile);
        }

        if (!specfp) {
		fprintf (stderr, 
			 "libglusterfsclient: %s:%s():%d: could not open volfile: %s\n", 
			 __FILE__, __PRETTY_FUNCTION__, __LINE__,
                         strerror (errno));
		FREE (ctx->gf_ctx.cmd_args.log_file);
                FREE (ctx->gf_ctx.cmd_args.volume_file);
                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                FREE (ctx);
                return NULL;
        }

        if (init_ctx->volume_name) {
                ctx->gf_ctx.cmd_args.volume_name = strdup (init_ctx->volume_name);
        }

	graph = file_to_xlator_tree (&ctx->gf_ctx, specfp);
        if (!graph) {
		fprintf (stderr, 
			 "libglusterfsclient: %s:%s():%d: cannot create configuration graph (%s)\n",
			 __FILE__, __PRETTY_FUNCTION__, __LINE__,
                         strerror (errno));

		FREE (ctx->gf_ctx.cmd_args.log_file);
                FREE (ctx->gf_ctx.cmd_args.volume_file);
                FREE (ctx->gf_ctx.cmd_args.volume_name);
                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                FREE (ctx);
                return NULL;
        }

        if (init_ctx->volume_name) {
                trav = graph;
                while (trav) {
                        if (strcmp (trav->name, init_ctx->volume_name) == 0) {
                                graph = trav;
                                break;
                        }
                        trav = trav->next;
                }
        }

        ctx->gf_ctx.graph = libglusterfs_graph (graph);
        if (!ctx->gf_ctx.graph) {
		fprintf (stderr, 
			 "libglusterfsclient: %s:%s():%d: graph creation failed (%s)\n",
			 __FILE__, __PRETTY_FUNCTION__, __LINE__,
                         strerror (errno));

		xlator_tree_free (graph);
		FREE (ctx->gf_ctx.cmd_args.log_file);
                FREE (ctx->gf_ctx.cmd_args.volume_file);
                FREE (ctx->gf_ctx.cmd_args.volume_name);
                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                FREE (ctx);
                return NULL;
        }
        graph = ctx->gf_ctx.graph;
        ctx->gf_ctx.top = graph;

	trav = graph;
	while (trav) {
		xl_count++;  /* Getting this value right is very important */
		trav = trav->next;
	}

	ctx->gf_ctx.xl_count = xl_count + 1;

        priv = CALLOC (1, sizeof (*priv));
        if (!priv) {
		fprintf (stderr, 
			 "libglusterfsclient: %s:%s():%d: cannot allocate memory (%s)\n",
			 __FILE__, __PRETTY_FUNCTION__, __LINE__,
                         strerror (errno));

		xlator_tree_free (graph);
		FREE (ctx->gf_ctx.cmd_args.log_file);
                FREE (ctx->gf_ctx.cmd_args.volume_file);
                FREE (ctx->gf_ctx.cmd_args.volume_name);
                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                /* inode_table_destroy (ctx->itable); */
                FREE (ctx);
         
                return NULL;
        }

        pthread_cond_init (&priv->init_con_established, NULL);
        pthread_mutex_init (&priv->lock, NULL);

        graph->private = priv;
        ctx->itable = inode_table_new (LIBGLUSTERFS_INODE_TABLE_LRU_LIMIT,
                                       graph);
        if (!ctx->itable) {
		fprintf (stderr, 
			 "libglusterfsclient: %s:%s():%d: cannot create inode table\n",
			 __FILE__, __PRETTY_FUNCTION__, __LINE__);
		xlator_tree_free (graph); 
		FREE (ctx->gf_ctx.cmd_args.log_file);
                FREE (ctx->gf_ctx.cmd_args.volume_file);
                FREE (ctx->gf_ctx.cmd_args.volume_name);

                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
		xlator_tree_free (graph); 
                /* TODO: destroy graph */
                /* inode_table_destroy (ctx->itable); */
                FREE (ctx);
         
                return NULL;
        }

	set_global_ctx_ptr (&ctx->gf_ctx);
	ctx->gf_ctx.process_uuid = zr_build_process_uuid ();

        if (xlator_graph_init (graph) == -1) {
		fprintf (stderr, 
			 "libglusterfsclient: %s:%s():%d: graph initialization failed\n",
			 __FILE__, __PRETTY_FUNCTION__, __LINE__);
		xlator_tree_free (graph);
		FREE (ctx->gf_ctx.cmd_args.log_file);
                FREE (ctx->gf_ctx.cmd_args.volume_file);
                FREE (ctx->gf_ctx.cmd_args.volume_name);
                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                /* TODO: destroy graph */
                /* inode_table_destroy (ctx->itable); */
                FREE (ctx);
                return NULL;
        }

	/* Send notify to all translator saying things are ready */
	graph->notify (graph, GF_EVENT_PARENT_UP, graph);

        if (gf_timer_registry_init (&ctx->gf_ctx) == NULL) {
		fprintf (stderr, 
			 "libglusterfsclient: %s:%s():%d: timer init failed (%s)\n", 
			 __FILE__, __PRETTY_FUNCTION__, __LINE__,
                         strerror (errno));

		xlator_graph_fini (graph);
		xlator_tree_free (graph);
		FREE (ctx->gf_ctx.cmd_args.log_file);
                FREE (ctx->gf_ctx.cmd_args.volume_file);
                FREE (ctx->gf_ctx.cmd_args.volume_name);

                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                /* TODO: destroy graph */
                /* inode_table_destroy (ctx->itable); */
                FREE (ctx);
                return NULL;
        }

        if ((ret = pthread_create (&ctx->reply_thread, NULL, poll_proc,
                                   (void *)&ctx->gf_ctx))) {
		fprintf (stderr, 
			 "libglusterfsclient: %s:%s():%d: reply thread creation failed\n", 
			 __FILE__, __PRETTY_FUNCTION__, __LINE__);
		xlator_graph_fini (graph);
		xlator_tree_free (graph);
		FREE (ctx->gf_ctx.cmd_args.log_file);
                FREE (ctx->gf_ctx.cmd_args.volume_file);
                FREE (ctx->gf_ctx.cmd_args.volume_name);

                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                /* TODO: destroy graph */
                /* inode_table_destroy (ctx->itable); */
                FREE (ctx);
                return NULL;
        }

        pthread_mutex_lock (&priv->lock); 
        {
                while (!priv->complete) {
                        pthread_cond_wait (&priv->init_con_established,
                                           &priv->lock);
                }
        }
        pthread_mutex_unlock (&priv->lock);

	first_init = 0;
 
        return ctx;
}

struct vmp_entry *
libgf_init_vmpentry (char *vmp, glusterfs_handle_t *vmphandle)
{
        struct vmp_entry        *entry = NULL;
        int                     vmplen = 0;
        int                     appendslash = 0;

        entry = CALLOC (1, sizeof (struct vmp_entry));
        if (!entry)
                return NULL;

        vmplen = strlen (vmp);
        if (vmp[vmplen - 1] != '/') {
                vmplen++;
                appendslash = 1;
        }

        entry->vmp = CALLOC (vmplen, sizeof (char));
        strcpy (entry->vmp, vmp);
        if (appendslash)
                entry->vmp[vmplen-1] = '/';
        entry->vmplen = vmplen;
        entry->handle = vmphandle;
        INIT_LIST_HEAD (&entry->list);
        return entry;
}

int
libgf_vmp_map_ghandle (char *vmp, glusterfs_handle_t *vmphandle)
{
        int                     ret = -1;
        struct vmp_entry        *vmpentry = NULL;

        /* FIXME: We dont check if the given vmp already exists
         * in the table, but we should add this check later.
         */
        vmpentry = libgf_init_vmpentry (vmp, vmphandle);
        if (!vmpentry)
                goto out;

        if (vmpentries == 0)
                INIT_LIST_HEAD (&vmplist.list);

        list_add_tail (&vmpentry->list, &vmplist.list);
        ++vmpentries;
        ret = 0;
out:

        return ret;
}

/* Returns the number of characters that match between an entry
 * and the path.
 */
int
libgf_strmatchcount (char *string1, int s1len, char *string2, int s2len)
{
        int     i = 0;
        int     tosearch = 0;
        int     matchcount = 0;

        s1len = strlen (string1);
        s2len = strlen (string2);

        if (s1len <= s2len)
                tosearch = s1len;
        else
                tosearch = s2len;

        for (;i < tosearch; i++) {
                if (string1[i] == string2[i])
                        matchcount++;
                else
                        break;
        }

        return matchcount;
}

int
libgf_vmp_entry_match (struct vmp_entry *entry, char *path)
{
        return libgf_strmatchcount (entry->vmp, entry->vmplen, path,
                                        strlen(path));
}

struct vmp_entry *
libgf_vmp_search_entry (char *path)
{
        struct vmp_entry        *entry = NULL;
        int                     matchcount = 0;
        struct vmp_entry        *maxentry = NULL;
        int                     maxcount = 0;

        if (!path)
                goto out;

        if (list_empty (&vmplist.list)) {
                gf_log (LIBGF_XL_NAME, GF_LOG_ERROR, "Virtual Mount Point "
                                "list is empty.");
                goto out;
        }

        list_for_each_entry(entry, &vmplist.list, list) {
                matchcount = libgf_vmp_entry_match (entry, path);
                if ((matchcount == entry->vmplen) && (matchcount > maxcount)) {
                        maxcount = matchcount;
                        maxentry = entry;
                }
        }

out:
        if (maxentry)
                gf_log (LIBGF_XL_NAME, GF_LOG_DEBUG, "VMP Entry found: %s: %s",
                                path, maxentry->vmp);
        else
                gf_log (LIBGF_XL_NAME, GF_LOG_DEBUG, "VMP Entry not found");

        return maxentry;
}

/* Path must be validated already. */
glusterfs_handle_t
libgf_vmp_get_ghandle (char * path)
{
        struct vmp_entry        *entry = NULL;

        entry = libgf_vmp_search_entry (path);

        if (entry == NULL)
                return NULL;

        return entry->handle;
}

int
glusterfs_mount (char *vmp, glusterfs_init_params_t *ipars)
{
        glusterfs_handle_t      vmphandle = NULL;
        int                     ret = -1;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, vmp, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ipars, out);

        vmphandle = glusterfs_init (ipars);
        if (!vmphandle) {
                errno = EINVAL;
                goto out;
        }

        ret = libgf_vmp_map_ghandle (vmp, vmphandle);

out:

        return ret;
}

void
glusterfs_reset (void)
{
	first_fini = first_init = 1;
}


void 
glusterfs_log_lock (void)
{
	gf_log_lock ();
}


void glusterfs_log_unlock (void)
{
	gf_log_unlock ();
}


int 
glusterfs_fini (glusterfs_handle_t handle)
{
	libglusterfs_client_ctx_t *ctx = handle;

	FREE (ctx->gf_ctx.cmd_args.log_file);
	FREE (ctx->gf_ctx.cmd_args.volume_file);
	FREE (ctx->gf_ctx.cmd_args.volume_name);
	FREE (ctx->gf_ctx.pool);
        FREE (ctx->gf_ctx.event_pool);
        ((gf_timer_registry_t *)ctx->gf_ctx.timer)->fin = 1;
        /* inode_table_destroy (ctx->itable); */

	xlator_graph_fini (ctx->gf_ctx.graph);
	xlator_tree_free (ctx->gf_ctx.graph);
	ctx->gf_ctx.graph = NULL;

        /* FREE (ctx->gf_ctx.specfile); */

        /* TODO complete cleanup of timer */
        /*TODO 
         * destroy the reply thread 
         * destroy inode table
         * FREE (ctx) 
         */

        FREE (ctx);

	if (first_fini) {
		;
		//gf_log_cleanup ();
	}

        return 0;
}


int32_t 
libgf_client_lookup_cbk (call_frame_t *frame,
                         void *cookie,
                         xlator_t *this,
                         int32_t op_ret,
                         int32_t op_errno,
                         inode_t *inode,
                         struct stat *buf,
                         dict_t *dict)
{
        libgf_client_local_t *local = frame->local;
        libglusterfs_client_ctx_t *ctx = frame->root->state;
	dict_t *xattr_req = NULL;

        if (op_ret == 0) {
		inode_t *parent = NULL;

		if (local->fop.lookup.loc->ino == 1) {
			buf->st_ino = 1;
		}

		parent = local->fop.lookup.loc->parent;
                inode_link (inode, parent, local->fop.lookup.loc->name, buf);
		/* inode_lookup (inode); */
        } else {
                if ((local->fop.lookup.is_revalidate == 0) 
                    && (op_errno == ENOENT)) {
                        gf_log ("libglusterfsclient", GF_LOG_DEBUG,
                                "%"PRId64": (op_num=%d) %s => -1 (%s)",
				frame->root->unique, frame->root->op,
				local->fop.lookup.loc->path,
				strerror (op_errno));
                } else {
                        gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "%"PRId64": (op_num=%d) %s => -1 (%s)",
				frame->root->unique, frame->root->op,
				local->fop.lookup.loc->path,
				strerror (op_errno));
                }

                if (local->fop.lookup.is_revalidate == 1) {
			int32_t ret = 0;
                        inode_unref (local->fop.lookup.loc->inode);
                        local->fop.lookup.loc->inode = inode_new (ctx->itable);
                        local->fop.lookup.is_revalidate = 2;

                        if (local->fop.lookup.size > 0) {
                                xattr_req = dict_new ();
                                ret = dict_set (xattr_req, "glusterfs.content",
                                                data_from_uint64 (local->fop.lookup.size));
                                if (ret == -1) {
                                        op_ret = -1;
                                        /* TODO: set proper error code */
                                        op_errno = errno;
                                        inode = NULL;
                                        buf = NULL;
                                        dict = NULL;
                                        dict_unref (xattr_req);
                                        goto out;
                                }
                        }

                        STACK_WIND (frame, libgf_client_lookup_cbk,
                                    FIRST_CHILD (this),
                                    FIRST_CHILD (this)->fops->lookup,
                                    local->fop.lookup.loc, xattr_req);

			if (xattr_req) {
				dict_unref (xattr_req);
				xattr_req = NULL;
			}

                        return 0;
                }
        }

out:
        local->reply_stub = fop_lookup_cbk_stub (frame, NULL, op_ret, op_errno,
                                                 inode, buf, dict);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int32_t
libgf_client_lookup (libglusterfs_client_ctx_t *ctx,
                     loc_t *loc,
                     struct stat *stbuf,
                     dict_t **dict,
		     dict_t *xattr_req)
{
        call_stub_t  *stub = NULL;
        int32_t op_ret;
        libgf_client_local_t *local = NULL;
        inode_t *inode = NULL;
        
        local = CALLOC (1, sizeof (*local));
        if (loc->inode) {
                local->fop.lookup.is_revalidate = 1;
                loc->ino = loc->inode->ino;
        }
        else
                loc->inode = inode_new (ctx->itable);

        local->fop.lookup.loc = loc;

        LIBGF_CLIENT_FOP(ctx, stub, lookup, local, loc, xattr_req);

        op_ret = stub->args.lookup_cbk.op_ret;
        errno = stub->args.lookup_cbk.op_errno;

        if (op_ret == -1)
                goto out;

        inode = stub->args.lookup_cbk.inode;
        if (!(libgf_get_inode_ctx (inode)))
                libgf_alloc_inode_ctx (ctx, inode);
        libgf_update_iattr_cache (inode, LIBGF_UPDATE_ALL,
                                        &stub->args.lookup_cbk.buf);
        if (stbuf)
                *stbuf = stub->args.lookup_cbk.buf;

        if (dict)
                *dict = dict_ref (stub->args.lookup_cbk.dict);
out:
	call_stub_destroy (stub);
        return op_ret;
}

int 
glusterfs_glh_get (glusterfs_handle_t handle, const char *path, void *buf,
                        size_t size, struct stat *stbuf)
{
        int32_t op_ret = -1;
        loc_t loc = {0, };
        libglusterfs_client_ctx_t *ctx = handle;
        dict_t *dict = NULL;
	dict_t *xattr_req = NULL;
	char *name = NULL, *pathname = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

	loc.path = strdup (path);
	op_ret = libgf_client_path_lookup (&loc, ctx, 0);
	if (op_ret == -1) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"path lookup failed for (%s)", path);
		goto out;
	}

	pathname = strdup (path);
	name = basename (pathname);

        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino, name);
        if (op_ret < 0) {
                gf_log ("libglusterfsclient",
                        GF_LOG_ERROR,
                        "libgf_client_loc_fill returned -1, returning EINVAL");
                errno = EINVAL;
                goto out;
        }

        if (size) { 
                xattr_req = dict_new ();
                op_ret = dict_set (xattr_req, "glusterfs.content",
                                   data_from_uint64 (size));
                if (op_ret < 0) {
                        gf_log ("libglusterfsclient",
                                GF_LOG_ERROR,
                                "setting requested content size dictionary failed");
                        goto out;
                }
        }

        op_ret = libgf_client_lookup (ctx, &loc, stbuf, &dict, xattr_req);
        if (!op_ret && stbuf && (stbuf->st_size <= size) && dict && buf) {
                data_t *mem_data = NULL;
                void *mem = NULL;
                
                mem_data = dict_get (dict, "glusterfs.content");
                if (mem_data) {
                        mem = data_to_ptr (mem_data);
                }
                        
                if (mem != NULL) { 
                        memcpy (buf, mem, stbuf->st_size);
                }
        }

out:
	if (xattr_req) {
		dict_unref (xattr_req);
	}
        
	if (dict) {
		dict_unref (dict);
	}

	if (pathname) {
		FREE (pathname);
	}
	libgf_client_loc_wipe (&loc);

        return op_ret;
}

int
glusterfs_get (const char *path, void *buf, size_t size, struct stat *stbuf)
{
        struct vmp_entry        *entry = NULL;
        int                     op_ret = -1;
        char                    *vpath = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, buf, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, stbuf, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        op_ret = glusterfs_glh_get (entry->handle, vpath, buf, size, stbuf);

out:
        return op_ret;
}

int
libgf_client_lookup_async_cbk (call_frame_t *frame,
                               void *cookie,
                               xlator_t *this,
                               int32_t op_ret,
                               int32_t op_errno,
                               inode_t *inode,
                               struct stat *stbuf,
                               dict_t *dict)
{
        libglusterfs_client_async_local_t *local = frame->local;
        glusterfs_get_cbk_t lookup_cbk = local->fop.lookup_cbk.cbk;
        libglusterfs_client_ctx_t *ctx = frame->root->state;
	glusterfs_iobuf_t *iobuf = NULL;
	dict_t *xattr_req = NULL;
        inode_t *parent = NULL;

        if (op_ret == 0) {
                parent = local->fop.lookup_cbk.loc->parent;
                inode_link (inode, parent, local->fop.lookup_cbk.loc->name,
                            stbuf);
                
                if (!(libgf_get_inode_ctx (inode)))
                        libgf_alloc_inode_ctx (ctx, inode);

                libgf_update_iattr_cache (inode, LIBGF_UPDATE_ALL, stbuf);
                /* inode_lookup (inode); */
        } else {
                if ((local->fop.lookup_cbk.is_revalidate == 0) 
                    && (op_errno == ENOENT)) {
                        gf_log ("libglusterfsclient", GF_LOG_DEBUG,
                                "%"PRId64": (op_num=%d) %s => -1 (%s)",
				frame->root->unique, frame->root->op,
				local->fop.lookup_cbk.loc->path,
				strerror (op_errno));
                } else {
                        gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "%"PRId64": (op_num=%d) %s => -1 (%s)",
				frame->root->unique, frame->root->op,
                                local->fop.lookup_cbk.loc->path,
				strerror (op_errno));
                }

                if (local->fop.lookup_cbk.is_revalidate == 1) {
			int32_t ret = 0;
                        inode_unref (local->fop.lookup_cbk.loc->inode);
                        local->fop.lookup_cbk.loc->inode = inode_new (ctx->itable);
                        local->fop.lookup_cbk.is_revalidate = 2;

                        if (local->fop.lookup_cbk.size > 0) {
                                xattr_req = dict_new ();
                                ret = dict_set (xattr_req, "glusterfs.content",
                                                data_from_uint64 (local->fop.lookup_cbk.size));
                                if (ret == -1) {
                                        op_ret = -1;
                                        /* TODO: set proper error code */
                                        op_errno = errno;
                                        inode = NULL;
                                        stbuf = NULL;
                                        dict = NULL;
                                        dict_unref (xattr_req);
                                        goto out;
                                }
                        }


                        STACK_WIND (frame, libgf_client_lookup_async_cbk,
                                    FIRST_CHILD (this),
                                    FIRST_CHILD (this)->fops->lookup,
                                    local->fop.lookup_cbk.loc, xattr_req);
			
			if (xattr_req) {
				dict_unref (xattr_req);
				xattr_req = NULL;
			}

                        return 0;
                }
        }

out:
        if (!op_ret && local->fop.lookup_cbk.size && dict) {
                data_t *mem_data = NULL;
                void *mem = NULL;
		struct iovec *vector = NULL;

                mem_data = dict_get (dict, "glusterfs.content");
                if (mem_data) {
                        mem = data_to_ptr (mem_data);
                }

                if (mem && stbuf->st_size <= local->fop.lookup_cbk.size) {
			iobuf = CALLOC (1, sizeof (*iobuf));
			ERR_ABORT (iobuf);

			vector = CALLOC (1, sizeof (*vector));
			ERR_ABORT (vector);
			vector->iov_base = mem;
			vector->iov_len = stbuf->st_size;  

			iobuf->vector = vector;
			iobuf->count = 1;
			iobuf->dictref = dict_ref (dict);
		}
	}
	
        lookup_cbk (op_ret, op_errno, iobuf, stbuf, local->cbk_data);

	libgf_client_loc_wipe (local->fop.lookup_cbk.loc);
        free (local->fop.lookup_cbk.loc);

        free (local);
        frame->local = NULL;
        STACK_DESTROY (frame->root);

        return 0;
}

/* TODO: implement async dentry lookup */

int
glusterfs_get_async (glusterfs_handle_t handle, 
		     const char *path,
		     size_t size, 
		     glusterfs_get_cbk_t cbk,
		     void *cbk_data)
{
        loc_t *loc = NULL;
        libglusterfs_client_ctx_t *ctx = handle;
        libglusterfs_client_async_local_t *local = NULL;
	int32_t op_ret = 0;
	dict_t *xattr_req = NULL;
	char *name = NULL, *pathname = NULL;

	if (!ctx || !path || path[0] != '/') {
		errno = EINVAL;
		op_ret = -1;
		goto out;
	}
        local = CALLOC (1, sizeof (*local));
        local->fop.lookup_cbk.is_revalidate = 1;

        loc = CALLOC (1, sizeof (*loc));
	loc->path = strdup (path);
	op_ret = libgf_client_path_lookup (loc, ctx, 1);
	if (op_ret == -1) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"path lookup failed for (%s)", path);
		goto out;
	}

	pathname = strdup (path);
	name = basename (pathname);
        op_ret = libgf_client_loc_fill (loc, ctx, 0, loc->parent->ino, name);
	if (op_ret < 0) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"libgf_client_loc_fill returned -1, returning EINVAL");
		errno = EINVAL;
		goto out;
	}

        if (!loc->inode) {
                loc->inode = inode_new (ctx->itable);
                local->fop.lookup_cbk.is_revalidate = 0;
        } 

        local->fop.lookup_cbk.cbk = cbk;
        local->fop.lookup_cbk.size = size;
        local->fop.lookup_cbk.loc = loc;
        local->cbk_data = cbk_data;

        if (size > 0) {
                xattr_req = dict_new ();
                op_ret = dict_set (xattr_req, "glusterfs.content",
                                   data_from_uint64 (size));
                if (op_ret < 0) {
                        dict_unref (xattr_req);
                        xattr_req = NULL;
                        goto out;
                }
        }

        LIBGF_CLIENT_FOP_ASYNC (ctx,
                                local,
                                libgf_client_lookup_async_cbk,
                                lookup,
                                loc,
                                xattr_req);
	if (xattr_req) {
		dict_unref (xattr_req);
		xattr_req = NULL;
	}

out:
	if (pathname) {
		FREE (pathname);
	}
 
        return op_ret;
}

int32_t
libgf_client_getxattr_cbk (call_frame_t *frame,
                           void *cookie,
                           xlator_t *this,
                           int32_t op_ret,
                           int32_t op_errno,
                           dict_t *dict)
{

        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_getxattr_cbk_stub (frame, NULL, op_ret,
                                                   op_errno, dict);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

size_t 
libgf_client_getxattr (libglusterfs_client_ctx_t *ctx, 
                       loc_t *loc,
                       const char *name,
                       void *value,
                       size_t size)
{
        call_stub_t  *stub = NULL;
        int32_t op_ret = 0;
        libgf_client_local_t *local = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, getxattr, local, loc, name);

        op_ret = stub->args.getxattr_cbk.op_ret;
        errno = stub->args.getxattr_cbk.op_errno;

        if (op_ret >= 0) {
                /*
                  gf_log ("LIBGF_CLIENT", GF_LOG_DEBUG,
                  "%"PRId64": %s => %d", frame->root->unique,
                  state->fuse_loc.loc.path, op_ret);
                */

                data_t *value_data = dict_get (stub->args.getxattr_cbk.dict,
                                               (char *)name);
    
                if (value_data) {
                        int32_t copy_len = 0;

                        /* Don't return the value for '\0' */
                        op_ret = value_data->len; 
                        copy_len = size < value_data->len ? 
                                size : value_data->len;
                        memcpy (value, value_data->data, copy_len);
                } else {
                        errno = ENODATA;
                        op_ret = -1;
                }
        }
	
	call_stub_destroy (stub);
        return op_ret;
}

ssize_t
glusterfs_glh_getxattr (glusterfs_handle_t handle, const char *path,
                                const char *name, void *value, size_t size)
{
        int32_t op_ret = -1;
        loc_t loc = {0, };
	dict_t *dict = NULL;
	libglusterfs_client_ctx_t *ctx = handle;
	char *file = NULL;
	dict_t *xattr_req = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, name, out);

        if (name[0] == '\0') {
		errno = EINVAL;
		goto out;
	}

	loc.path = strdup (path);
	op_ret = libgf_client_path_lookup (&loc, ctx, 1);
	if (op_ret == -1) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"path lookup failed for (%s)", path);
		goto out;
	}

	file = strdup (path);
	file = basename (file);
        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino, file);
	if (op_ret == -1) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"libgf_client_loc_fill returned -1, returning EINVAL");
		errno = EINVAL;
		goto out;
	}

	xattr_req = dict_new ();
	op_ret = dict_set (xattr_req, (char *)name,
			   data_from_uint64 (size));
	if (op_ret == -1) {
		/* TODO: set proper error code */
		goto out;
	}

	op_ret = libgf_client_lookup (ctx, &loc, NULL, &dict, xattr_req);
	if (op_ret == 0) {
		data_t *value_data = dict_get (dict, (char *)name);
			
		if (value_data) {
			int32_t copy_len = 0;

                        /* Don't return the value for '\0' */
			op_ret = value_data->len;
				
			copy_len = size < value_data->len ? 
                                size : value_data->len;
			memcpy (value, value_data->data, copy_len);
		} else {
			errno = ENODATA;
			op_ret = -1;
		}
	}

out:
	if (file) {
		FREE (file);
	}

	if (xattr_req) {
		dict_unref (xattr_req);
	}

	if (dict) {
		dict_unref (dict);
	}

        libgf_client_loc_wipe (&loc);

        return op_ret;
}

ssize_t
glusterfs_getxattr (const char *path, const char *name, void *value,
                        size_t size)
{
        int                     op_ret = -1;
        struct vmp_entry        *entry = NULL;
        char                    *vpath = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, name, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, value, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        op_ret = glusterfs_glh_getxattr (entry->handle, vpath, name, value,
                                                size);

out:
        return op_ret;
}

static int32_t
libgf_client_open_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno,
                       fd_t *fd)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_open_cbk_stub (frame, NULL, op_ret, op_errno,
                                               fd);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}


int 
libgf_client_open (libglusterfs_client_ctx_t *ctx, 
                   loc_t *loc, 
                   fd_t *fd, 
                   int flags)
{
        call_stub_t *stub = NULL;
        int32_t op_ret = 0;
        libgf_client_local_t *local = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, open, local, loc, flags, fd);

        op_ret = stub->args.open_cbk.op_ret;
        errno = stub->args.open_cbk.op_errno;

	call_stub_destroy (stub);
        return op_ret;
}

static int32_t
libgf_client_create_cbk (call_frame_t *frame,
                         void *cookie,
                         xlator_t *this,
                         int32_t op_ret,
                         int32_t op_errno,
                         fd_t *fd,
                         inode_t *inode,
                         struct stat *buf)     
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_create_cbk_stub (frame, NULL, op_ret, op_errno,
                                                 fd, inode, buf);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int 
libgf_client_creat (libglusterfs_client_ctx_t *ctx,
                    loc_t *loc,
                    fd_t *fd,
                    int flags,
                    mode_t mode)
{
        call_stub_t *stub = NULL;
        int32_t op_ret = 0;
        libgf_client_local_t *local = NULL;
        inode_t *libgf_inode = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, create, local, loc, flags, mode, fd);
  
        op_ret = stub->args.create_cbk.op_ret;
        errno = stub->args.create_cbk.op_errno;
        if (op_ret == -1)
                goto out;

	libgf_inode = stub->args.create_cbk.inode;
        inode_link (libgf_inode, loc->parent, loc->name,
                        &stub->args.create_cbk.buf);

        /* inode_lookup (libgf_inode); */

        libgf_alloc_inode_ctx (ctx, libgf_inode);
        libgf_update_iattr_cache (libgf_inode, LIBGF_UPDATE_ALL,
                                        &stub->args.create_cbk.buf);

out:
	call_stub_destroy (stub);
        return op_ret;
}

int32_t
libgf_client_opendir_cbk (call_frame_t *frame,
                          void *cookie,
                          xlator_t *this,
                          int32_t op_ret,
                          int32_t op_errno,
                          fd_t *fd)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_opendir_cbk_stub (frame, NULL, op_ret, op_errno,
                                                  fd);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int 
libgf_client_opendir (libglusterfs_client_ctx_t *ctx,
                      loc_t *loc,
                      fd_t *fd)
{
        call_stub_t *stub = NULL;
        int32_t op_ret = -1;
        libgf_client_local_t *local = NULL;

        if ((fd->flags & O_WRONLY) || (fd->flags & O_RDWR)) {
                errno = EISDIR;
                goto out;
        }
        LIBGF_CLIENT_FOP (ctx, stub, opendir, local, loc, fd);

        op_ret = stub->args.opendir_cbk.op_ret;
        errno = stub->args.opendir_cbk.op_errno;

	call_stub_destroy (stub);
out:
        return op_ret;
}

glusterfs_file_t 
glusterfs_glh_open (glusterfs_handle_t handle, const char *path, int flags,...)
{
        loc_t loc = {0, };
        long op_ret = -1;
        fd_t *fd = NULL;
	int32_t ret = -1;
	libglusterfs_client_ctx_t *ctx = handle;
	char *name = NULL, *pathname = NULL;
        libglusterfs_client_inode_ctx_t *inode_ctx = NULL;
        mode_t mode = 0;
        va_list ap;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

	loc.path = strdup (path);
	op_ret = libgf_client_path_lookup (&loc, ctx, 1);

        if ((op_ret == -1) && ((flags & O_CREAT) != O_CREAT)) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"path lookup failed for (%s)", path);
		goto out;
	}

        if (!op_ret && ((flags & O_CREAT) == O_CREAT) 
            && ((flags & O_EXCL) == O_EXCL)) {
                errno = EEXIST;
                op_ret = -1;
                goto out;
        }

        if ((op_ret == -1) && ((flags & O_CREAT) == O_CREAT)) {
                libgf_client_loc_wipe (&loc);
                loc.path = strdup (path);

                op_ret = libgf_client_path_lookup (&loc, ctx, 0);
                if (op_ret == -1) {
                        gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "path lookup failed for (%s) while trying to"
                                " create (%s)", dirname ((char *)path), path);
                        goto out;
                }

                loc.inode = inode_new (ctx->itable);
        }

	pathname = strdup (path);
	name = basename (pathname);

        ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino, name);
	if (ret == -1) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"libgf_client_loc_fill returned -1, returning EINVAL");
		errno = EINVAL;
		goto out;
	}

        fd = fd_create (loc.inode, 0);
        fd->flags = flags;

        if ((flags & O_CREAT) == O_CREAT) {
                /* If we have the st_mode for the basename, check if
                 * it is a directory here itself, rather than sending
                 * a network message through libgf_client_creat, and
                 * then receiving a EISDIR.
                 */
                if (S_ISDIR (loc.inode->st_mode)) {
                        errno = EISDIR;
                        op_ret = -1;
                        goto op_over;
                }
                va_start (ap, flags);
                mode = va_arg (ap, mode_t);
                va_end (ap);
                op_ret = libgf_client_creat (ctx, &loc, fd, flags, mode);
        } else {
                if (S_ISDIR (loc.inode->st_mode))
                        op_ret = libgf_client_opendir (ctx, &loc, fd);
                else
                        op_ret = libgf_client_open (ctx, &loc, fd, flags);
        }

op_over:
        if (op_ret == -1) {
                fd_unref (fd);
                fd = NULL;
                goto out;
        }

        if (!libgf_get_fd_ctx (fd)) {
                if (!libgf_alloc_fd_ctx (ctx, fd)) {
                        errno = EINVAL;
                        op_ret = -1;
                        goto out;
                }
        }

        if ((flags & O_TRUNC) && ((flags & O_RDWR) || (flags & O_WRONLY))) {
                inode_ctx = libgf_get_inode_ctx (fd->inode);
                if (S_ISREG (inode_ctx->stbuf.st_mode)) {
                                inode_ctx->stbuf.st_size = 0;
                                inode_ctx->stbuf.st_blocks = 0;
                }
        }

out:
        libgf_client_loc_wipe (&loc);

	if (pathname) {
		FREE (pathname);
	}

        return fd;
}

glusterfs_file_t
glusterfs_open (const char *path, int flags, ...)
{
        struct vmp_entry        *entry = NULL;
        char                    *vpath = NULL;
        glusterfs_file_t        fh = NULL;
        mode_t                  mode = 0;
        va_list                 ap;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        if (flags & O_CREAT) {
                va_start (ap, flags);
                mode = va_arg (ap, mode_t);
                va_end (ap);
                fh = glusterfs_glh_open (entry->handle, vpath, flags, mode);
        } else
                fh = glusterfs_glh_open (entry->handle, vpath, flags);
out:
        return fh;
}

glusterfs_file_t 
glusterfs_glh_creat (glusterfs_handle_t handle, const char *path, mode_t mode)
{
	return glusterfs_glh_open (handle, path,
			       (O_CREAT | O_WRONLY | O_TRUNC), mode);
}

glusterfs_file_t
glusterfs_creat (const char *path, mode_t mode)
{
        struct vmp_entry        *entry = NULL;
        char                    *vpath = NULL;
        glusterfs_file_t        fh = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        fh = glusterfs_glh_creat (entry->handle, vpath, mode);

out:
        return fh;
}

int32_t
libgf_client_flush_cbk (call_frame_t *frame,
                        void *cookie,
                        xlator_t *this,
                        int32_t op_ret,
                        int32_t op_errno)
{
        libgf_client_local_t *local = frame->local;
        
        local->reply_stub = fop_flush_cbk_stub (frame, NULL, op_ret, op_errno);
        
        LIBGF_REPLY_NOTIFY (local);
        return 0;
}


int 
libgf_client_flush (libglusterfs_client_ctx_t *ctx, fd_t *fd)
{
        call_stub_t *stub;
        int32_t op_ret;
        libgf_client_local_t *local = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, flush, local, fd);
        
        op_ret = stub->args.flush_cbk.op_ret;
        errno = stub->args.flush_cbk.op_errno;
        
	call_stub_destroy (stub);        
        return op_ret;
}


int 
glusterfs_close (glusterfs_file_t fd)
{
        int32_t op_ret = -1;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;

        if (!fd) {
                errno = EINVAL;
		goto out;
        }

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
                goto out;
        }
        ctx = fd_ctx->ctx;

        op_ret = libgf_client_flush (ctx, (fd_t *)fd);

        fd_unref ((fd_t *)fd);

out:
        return op_ret;
}

int32_t
libgf_client_setxattr_cbk (call_frame_t *frame,
                           void *cookie,
                           xlator_t *this,
                           int32_t op_ret,
                           int32_t op_errno)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_setxattr_cbk_stub (frame, NULL, op_ret,
                                                   op_errno);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int
libgf_client_setxattr (libglusterfs_client_ctx_t *ctx, 
                       loc_t *loc,
                       const char *name,
                       const void *value,
                       size_t size,
                       int flags)
{
        call_stub_t  *stub = NULL;
        int32_t op_ret = 0;
        dict_t *dict;
        libgf_client_local_t *local = NULL;

        dict = get_new_dict ();

        dict_set (dict, (char *)name,
                  bin_to_data ((void *)value, size));
        dict_ref (dict);


        LIBGF_CLIENT_FOP (ctx, stub, setxattr, local, loc, dict, flags);

        op_ret = stub->args.setxattr_cbk.op_ret;
        errno = stub->args.setxattr_cbk.op_errno;

        dict_unref (dict);
	call_stub_destroy (stub);
        return op_ret;
}

int 
glusterfs_glh_setxattr (glusterfs_handle_t handle, const char *path,
                                const char *name, const void *value,
                                size_t size, int flags)
{
        int32_t op_ret = -1;
        loc_t loc = {0, };
	libglusterfs_client_ctx_t *ctx = handle;
	char *file = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

	loc.path = strdup (path);
	op_ret = libgf_client_path_lookup (&loc, ctx, 1);
	if (op_ret == -1) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"path lookup failed for (%s)", path);
		goto out;
	}

	file = strdup (path);
	file = basename (file);

        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino, file);
	if (op_ret == -1) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"libgf_client_loc_fill returned -1, returning EINVAL");
		errno = EINVAL;
		goto out;
	}

        if (!op_ret)
                op_ret = libgf_client_setxattr (ctx, &loc, name, value, size,
                                                flags);

out:
	if (file) {
		FREE (file);
	}

        libgf_client_loc_wipe (&loc);
        return op_ret;
}

int 
glusterfs_setxattr (const char *path, const char *name, const void *value,
                        size_t size, int flags)
{
        struct vmp_entry        *entry = NULL;
        int                     op_ret = -1;
        char                    *vpath = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, name, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, value, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        op_ret = glusterfs_glh_setxattr (entry->handle, vpath, name, value,
                                                size, flags);
out:
        return op_ret;
}

int
glusterfs_lsetxattr (glusterfs_handle_t handle, 
                     const char *path, 
                     const char *name,
                     const void *value, 
                     size_t size, int flags)
{
        return ENOSYS;
}

int32_t
libgf_client_fsetxattr_cbk (call_frame_t *frame,
                            void *cookie,
                            xlator_t *this,
                            int32_t op_ret,
                            int32_t op_errno)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_fsetxattr_cbk_stub (frame, NULL, op_ret,
                                                    op_errno);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int
libgf_client_fsetxattr (libglusterfs_client_ctx_t *ctx, 
                        fd_t *fd,
                        const char *name,
                        const void *value,
                        size_t size,
                        int flags)
{
        call_stub_t  *stub = NULL;
        int32_t op_ret = 0;
        dict_t *dict;
        libgf_client_local_t *local = NULL;

        dict = get_new_dict ();

        dict_set (dict, (char *)name,
                  bin_to_data ((void *)value, size));
        dict_ref (dict);

        LIBGF_CLIENT_FOP (ctx, stub, fsetxattr, local, fd, dict, flags);

        op_ret = stub->args.fsetxattr_cbk.op_ret;
        errno = stub->args.fsetxattr_cbk.op_errno;

        dict_unref (dict);
	call_stub_destroy (stub);

        return op_ret;
}

int 
glusterfs_fsetxattr (glusterfs_file_t fd, 
                     const char *name,
                     const void *value, 
                     size_t size, 
                     int flags)
{
	int32_t op_ret = 0;
        fd_t *__fd = fd;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        libglusterfs_client_ctx_t *ctx = NULL;
        
        if (!fd) {
                errno = EINVAL;
                op_ret = -1;
                gf_log("libglusterfsclient",
                       GF_LOG_ERROR,
                       "invalid fd");
                goto out;
        }

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		op_ret = -1;
		goto out;
        }

        ctx = fd_ctx->ctx;
        op_ret = libgf_client_fsetxattr (ctx, __fd, name, value, size,
                                         flags);
        
out:
	return op_ret;
}

ssize_t 
glusterfs_lgetxattr (glusterfs_handle_t handle, 
                     const char *path, 
                     const char *name,
                     void *value, 
                     size_t size)
{
        return ENOSYS;
}

int32_t
libgf_client_fgetxattr_cbk (call_frame_t *frame,
                            void *cookie,
                            xlator_t *this,
                            int32_t op_ret,
                            int32_t op_errno,
                            dict_t *dict)
{

        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_fgetxattr_cbk_stub (frame, NULL, op_ret,
                                                    op_errno, dict);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

size_t 
libgf_client_fgetxattr (libglusterfs_client_ctx_t *ctx, 
                        fd_t *fd,
                        const char *name,
                        void *value,
                        size_t size)
{
        call_stub_t  *stub = NULL;
        int32_t op_ret = 0;
        libgf_client_local_t *local = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, fgetxattr, local, fd, name);

        op_ret = stub->args.fgetxattr_cbk.op_ret;
        errno = stub->args.fgetxattr_cbk.op_errno;

        if (op_ret >= 0) {
                /*
                  gf_log ("LIBGF_CLIENT", GF_LOG_DEBUG,
                  "%"PRId64": %s => %d", frame->root->unique,
                  state->fuse_loc.loc.path, op_ret);
                */

                data_t *value_data = dict_get (stub->args.fgetxattr_cbk.dict,
                                               (char *)name);
    
                if (value_data) {
                        int32_t copy_len = 0;

                        /* Don't return the value for '\0' */
                        op_ret = value_data->len; 
                        copy_len = size < value_data->len ? 
                                size : value_data->len;
                        memcpy (value, value_data->data, copy_len);
                } else {
                        errno = ENODATA;
                        op_ret = -1;
                }
        }
	
	call_stub_destroy (stub);
        return op_ret;
}

ssize_t 
glusterfs_fgetxattr (glusterfs_file_t fd, 
                     const char *name,
                     void *value, 
                     size_t size)
{
	int32_t op_ret = 0;
        libglusterfs_client_ctx_t *ctx;
        fd_t *__fd = (fd_t *)fd;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		op_ret = -1;
		goto out;
        }

        ctx = fd_ctx->ctx;
        op_ret = libgf_client_fgetxattr (ctx, __fd, name, value, size);
out:
	return op_ret;
}

ssize_t 
glusterfs_listxattr (glusterfs_handle_t handle,
                     const char *path, 
                     char *list,
                     size_t size)
{
        return ENOSYS;
}

ssize_t 
glusterfs_llistxattr (glusterfs_handle_t handle,
                      const char *path, 
                      char *list,
                      size_t size)
{
        return ENOSYS;
}

ssize_t 
glusterfs_flistxattr (glusterfs_file_t fd, 
                      char *list,
                      size_t size)
{
        return ENOSYS;
}

int 
glusterfs_removexattr (glusterfs_handle_t handle, 
                       const char *path, 
                       const char *name)
{
        return ENOSYS;
}

int 
glusterfs_lremovexattr (glusterfs_handle_t handle, 
                        const char *path, 
                        const char *name)
{
        return ENOSYS;
}

int 
glusterfs_fremovexattr (glusterfs_file_t fd, 
                        const char *name)
{
        return ENOSYS;
}

int32_t
libgf_client_readv_cbk (call_frame_t *frame,
                        void *cookie,
                        xlator_t *this,
                        int32_t op_ret,
                        int32_t op_errno,
                        struct iovec *vector,
                        int32_t count,
                        struct stat *stbuf,
                        struct iobref *iobref)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_readv_cbk_stub (frame, NULL, op_ret, op_errno,
                                                vector, count, stbuf, iobref);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int 
libgf_client_read (libglusterfs_client_ctx_t *ctx, 
                   fd_t *fd,
                   void *buf, 
                   size_t size, 
                   off_t offset)
{
        call_stub_t *stub;
        struct iovec *vector;
        int32_t op_ret = -1;
        int count = 0;
        libgf_client_local_t *local = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, readv, local, fd, size, offset);

        op_ret = stub->args.readv_cbk.op_ret;
        errno = stub->args.readv_cbk.op_errno;
        count = stub->args.readv_cbk.count;
        vector = stub->args.readv_cbk.vector;
        if (op_ret > 0) {
                int i = 0;
                op_ret = 0;
                while (size && (i < count)) {
                        int len = (size < vector[i].iov_len) ? 
                                size : vector[i].iov_len;
                        memcpy (buf, vector[i++].iov_base, len);
                        buf += len;
                        size -= len;
                        op_ret += len;
                }
        }

	call_stub_destroy (stub);
        return op_ret;
}

ssize_t 
glusterfs_read (glusterfs_file_t fd, 
                void *buf, 
                size_t nbytes)
{
        int32_t op_ret = -1;
        off_t offset = 0;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;

        if (fd == 0) {
                errno = EINVAL;
		goto out;
        }

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		goto out;
        }

        pthread_mutex_lock (&fd_ctx->lock);
        {
                ctx = fd_ctx->ctx;
                offset = fd_ctx->offset;
        }
        pthread_mutex_unlock (&fd_ctx->lock);

        op_ret = libgf_client_read (ctx, (fd_t *)fd, buf, nbytes, offset);

        if (op_ret > 0) {
                offset += op_ret;
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        fd_ctx->offset = offset;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

out:
        return op_ret;
}


ssize_t
libgf_client_readv (libglusterfs_client_ctx_t *ctx, 
                    fd_t *fd,
                    const struct iovec *dst_vector,
                    int dst_count,
                    off_t offset)
{
        call_stub_t *stub = NULL;
        struct iovec *src_vector;
        int src_count = 0;
        int32_t op_ret = -1;
        libgf_client_local_t *local = NULL;
        size_t size = 0;
        int32_t i = 0;

        for (i = 0; i < dst_count; i++)
        {
                size += dst_vector[i].iov_len;
        }

        LIBGF_CLIENT_FOP (ctx, stub, readv, local, fd, size, offset);

        op_ret = stub->args.readv_cbk.op_ret;
        errno = stub->args.readv_cbk.op_errno;
        src_count = stub->args.readv_cbk.count;
        src_vector = stub->args.readv_cbk.vector;
        if (op_ret > 0) {
                int src = 0, dst = 0;
                off_t src_offset = 0, dst_offset = 0;
    
                while ((size != 0) && (dst < dst_count) && (src < src_count)) {
                        int len = 0, src_len, dst_len;
   
                        src_len = src_vector[src].iov_len - src_offset;
                        dst_len = dst_vector[dst].iov_len - dst_offset;

                        len = (src_len < dst_len) ? src_len : dst_len;
                        if (len > size) {
                                len = size;
                        }

                        memcpy (dst_vector[dst].iov_base + dst_offset, 
				src_vector[src].iov_base + src_offset, len);

                        size -= len;
                        src_offset += len;
                        dst_offset += len;

                        if (src_offset == src_vector[src].iov_len) {
                                src_offset = 0;
                                src++;
                        }

                        if (dst_offset == dst_vector[dst].iov_len) {
                                dst_offset = 0;
                                dst++;
                        }
                }
        }
 
	call_stub_destroy (stub);
        return op_ret;
}


ssize_t 
glusterfs_readv (glusterfs_file_t fd, const struct iovec *vec, int count)
{
        int32_t op_ret = -1;
        off_t offset = 0;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;

        if (!fd) {
                errno = EINVAL;
		goto out;
        }

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		goto out;
        }

        pthread_mutex_lock (&fd_ctx->lock);
        {
                ctx = fd_ctx->ctx;
                offset = fd_ctx->offset;
        }
        pthread_mutex_unlock (&fd_ctx->lock);

        op_ret = libgf_client_readv (ctx, (fd_t *)fd, vec, count, offset);

        if (op_ret > 0) {
                offset += op_ret;
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        fd_ctx->offset = offset;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

out:
        return op_ret;
}


ssize_t 
glusterfs_pread (glusterfs_file_t fd, 
                 void *buf, 
                 size_t count, 
                 off_t offset)
{
        int32_t op_ret = -1;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;

        if (!fd) {
                errno = EINVAL;
		goto out;
        }

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		goto out;
        }

        ctx = fd_ctx->ctx;

        op_ret = libgf_client_read (ctx, (fd_t *)fd, buf, count, offset);

out:
        return op_ret;
}


int
libgf_client_writev_cbk (call_frame_t *frame,
                         void *cookie,
                         xlator_t *this,
                         int32_t op_ret,
                         int32_t op_errno,
                         struct stat *stbuf)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_writev_cbk_stub (frame, NULL, op_ret, op_errno,
                                                 stbuf);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int
libgf_client_writev (libglusterfs_client_ctx_t *ctx, 
                     fd_t *fd, 
                     struct iovec *vector, 
                     int count, 
                     off_t offset)
{
        call_stub_t *stub = NULL;
        int op_ret = -1;
        libgf_client_local_t *local = NULL;
        struct iobref *iobref = NULL;

        iobref = iobref_new ();
        LIBGF_CLIENT_FOP (ctx, stub, writev, local, fd, vector, count, offset,
                          iobref);

        op_ret = stub->args.writev_cbk.op_ret;
        errno = stub->args.writev_cbk.op_errno;

        iobref_unref (iobref);
	call_stub_destroy (stub);
        return op_ret;
}


ssize_t 
glusterfs_write (glusterfs_file_t fd, 
                 const void *buf, 
                 size_t n)
{
        int32_t op_ret = -1;
        off_t offset = 0;
        struct iovec vector;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;

        if (!fd) {
                errno = EINVAL;
		goto out;
        }

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		goto out;
        }

        ctx = fd_ctx->ctx;

        pthread_mutex_lock (&fd_ctx->lock);
        {
                offset = fd_ctx->offset;
        }
        pthread_mutex_unlock (&fd_ctx->lock);

        vector.iov_base = (void *)buf;
        vector.iov_len = n;

        op_ret = libgf_client_writev (ctx,
                                      (fd_t *)fd, 
                                      &vector, 
                                      1, 
                                      offset);

        if (op_ret >= 0) {
                offset += op_ret;
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        fd_ctx->offset = offset;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

out:
        return op_ret;
}

ssize_t 
glusterfs_writev (glusterfs_file_t fd, 
                  const struct iovec *vector,
                  int count)
{
        int32_t op_ret = -1;
        off_t offset = 0;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;

        if (!fd) {
                errno = EINVAL;
		goto out;
        }

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		goto out;
        }

        ctx = fd_ctx->ctx;

        pthread_mutex_lock (&fd_ctx->lock);
        {
                offset = fd_ctx->offset;
        }
        pthread_mutex_unlock (&fd_ctx->lock);


        op_ret = libgf_client_writev (ctx,
                                      (fd_t *)fd, 
                                      (struct iovec *)vector, 
                                      count,
                                      offset);

        if (op_ret >= 0) {
                offset += op_ret;
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        fd_ctx->offset = offset;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

out:
        return op_ret;
}


ssize_t 
glusterfs_pwrite (glusterfs_file_t fd, 
                  const void *buf, 
                  size_t count, 
                  off_t offset)
{
        int32_t op_ret = -1;
        struct iovec vector;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;

        if (!fd) {
                errno = EINVAL;
		goto out;
        }

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		goto out;
        }

        ctx = fd_ctx->ctx;

        vector.iov_base = (void *)buf;
        vector.iov_len = count;

        op_ret = libgf_client_writev (ctx,
                                      (fd_t *)fd, 
                                      &vector, 
                                      1, 
                                      offset);

out:
        return op_ret;
}


int32_t
libgf_client_readdir_cbk (call_frame_t *frame,
                          void *cookie,
                          xlator_t *this,
                          int32_t op_ret,
                          int32_t op_errno,
                          gf_dirent_t *entries)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_readdir_cbk_stub (frame, NULL, op_ret, op_errno,
                                                  entries);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int 
libgf_client_readdir (libglusterfs_client_ctx_t *ctx, 
                      fd_t *fd, 
                      struct dirent *dirp, 
                      size_t size, 
                      off_t *offset,
		      int32_t num_entries)
{  
        call_stub_t *stub = NULL;
        int op_ret = -1;
        libgf_client_local_t *local = NULL;
	gf_dirent_t *entry = NULL;
	int32_t count = 0; 
	size_t entry_size = 0;

        LIBGF_CLIENT_FOP (ctx, stub, readdir, local, fd, size, *offset);

        op_ret = stub->args.readdir_cbk.op_ret;
        errno = stub->args.readdir_cbk.op_errno;

        if (op_ret > 0) {
		list_for_each_entry (entry, 
                                     &stub->args.readdir_cbk.entries.list,
                                     list) {
			entry_size = offsetof (struct dirent, d_name) 
                                + strlen (entry->d_name) + 1;
			
			if ((size < entry_size) || (count == num_entries)) {
				break;
			}

			size -= entry_size;

			dirp->d_ino = entry->d_ino;
			/*
			  #ifdef GF_DARWIN_HOST_OS
			  dirp->d_off = entry->d_seekoff;
			  #endif
			  #ifdef GF_LINUX_HOST_OS
			  dirp->d_off = entry->d_off;
			  #endif
			*/
			
			*offset = dirp->d_off = entry->d_off;
			/* dirp->d_type = entry->d_type; */
			dirp->d_reclen = entry->d_len;
			strncpy (dirp->d_name, entry->d_name, dirp->d_reclen);
			dirp->d_name[dirp->d_reclen] = '\0';

			dirp = (struct dirent *) (((char *) dirp) + entry_size);
			count++;
		}
        }

	call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_readdir (glusterfs_dir_t fd, 
                   struct dirent *dirp, 
                   unsigned int count)
{
        int op_ret = -1;
        libglusterfs_client_ctx_t *ctx = NULL;
        off_t offset = 0;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		goto out;
        }

        pthread_mutex_lock (&fd_ctx->lock);
        {
                ctx = fd_ctx->ctx;
                offset = fd_ctx->offset;
        }
        pthread_mutex_unlock (&fd_ctx->lock);

        op_ret = libgf_client_readdir (ctx, (fd_t *)fd, dirp, sizeof (*dirp),
                                       &offset, 1);

        if (op_ret > 0) {
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        fd_ctx->offset = offset;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
		op_ret = 1;
        }

out:
        return op_ret;
}


int
glusterfs_getdents (glusterfs_file_t fd, struct dirent *dirp,
                    unsigned int count)
{
        int op_ret = -1;
        libglusterfs_client_ctx_t *ctx = NULL;
        off_t offset = 0;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		goto out;
        }

        pthread_mutex_lock (&fd_ctx->lock);
        {
                ctx = fd_ctx->ctx;
                offset = fd_ctx->offset;
        }
        pthread_mutex_unlock (&fd_ctx->lock);

        op_ret = libgf_client_readdir (ctx, (fd_t *)fd, dirp, count, &offset,
                                       -1);

        if (op_ret > 0) {
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        fd_ctx->offset = offset;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

out:
        return op_ret;
}


static int32_t
libglusterfs_readv_async_cbk (call_frame_t *frame,
                              void *cookie,
                              xlator_t *this,
                              int32_t op_ret,
                              int32_t op_errno,
                              struct iovec *vector,
                              int32_t count,
                              struct stat *stbuf,
                              struct iobref *iobref)
{
        glusterfs_iobuf_t *buf;
        libglusterfs_client_async_local_t *local = frame->local;
        fd_t *__fd = local->fop.readv_cbk.fd;
        glusterfs_readv_cbk_t readv_cbk = local->fop.readv_cbk.cbk;

        buf = CALLOC (1, sizeof (*buf));
        ERR_ABORT (buf);

	if (vector) {
		buf->vector = iov_dup (vector, count);
	}

        buf->count = count;

	if (iobref) {
		buf->iobref = iobref_ref (iobref);
	}

        if (op_ret > 0) {
                libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
                fd_ctx = libgf_get_fd_ctx (__fd);
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        fd_ctx->offset += op_ret;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

        readv_cbk (op_ret, op_errno, buf, local->cbk_data); 

	FREE (local);
	frame->local = NULL;
        STACK_DESTROY (frame->root);

        return 0;
}

void 
glusterfs_free (glusterfs_iobuf_t *buf)
{
        //iov_free (buf->vector, buf->count);
        FREE (buf->vector);
        if (buf->iobref)
                iobref_unref ((struct iobref *) buf->iobref);
        if (buf->dictref)
                dict_unref ((dict_t *) buf->dictref);
        FREE (buf);
}

int
glusterfs_read_async (glusterfs_file_t fd, 
                      size_t nbytes, 
                      off_t offset,
                      glusterfs_readv_cbk_t readv_cbk,
                      void *cbk_data)
{
        libglusterfs_client_ctx_t *ctx;
        fd_t *__fd = (fd_t *)fd;
        libglusterfs_client_async_local_t *local = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
	int32_t op_ret = 0;

        local = CALLOC (1, sizeof (*local));
        ERR_ABORT (local);
        local->fop.readv_cbk.fd = __fd;
        local->fop.readv_cbk.cbk = readv_cbk;
        local->cbk_data = cbk_data;

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		op_ret = -1;
		goto out;
        }

        ctx = fd_ctx->ctx;

        if (offset < 0) {
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        offset = fd_ctx->offset;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

        LIBGF_CLIENT_FOP_ASYNC (ctx,
                                local,
                                libglusterfs_readv_async_cbk,
                                readv,
                                __fd,
                                nbytes,
                                offset);

out:
        return op_ret;
}

static int32_t
libglusterfs_writev_async_cbk (call_frame_t *frame,
                               void *cookie,
                               xlator_t *this,
                               int32_t op_ret,
                               int32_t op_errno,
                               struct stat *stbuf)
{
        libglusterfs_client_async_local_t *local = frame->local;
        fd_t *fd = NULL;
        glusterfs_write_cbk_t write_cbk;

        write_cbk = local->fop.write_cbk.cbk;
        fd = local->fop.write_cbk.fd;

        if (op_ret > 0) {
                libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
                fd_ctx = libgf_get_fd_ctx (fd);
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        fd_ctx->offset += op_ret;  
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

        write_cbk (op_ret, op_errno, local->cbk_data);

        STACK_DESTROY (frame->root);
        return 0;
}

int32_t
glusterfs_write_async (glusterfs_file_t fd, 
                       const void *buf, 
                       size_t nbytes, 
                       off_t offset,
                       glusterfs_write_cbk_t write_cbk,
                       void *cbk_data)
{
        fd_t *__fd = (fd_t *)fd;
        struct iovec vector;
        off_t __offset = offset;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_async_local_t *local = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
	int32_t op_ret = 0;
        struct iobref *iobref = NULL;

        local = CALLOC (1, sizeof (*local));
        ERR_ABORT (local);
        local->fop.write_cbk.fd = __fd;
        local->fop.write_cbk.cbk = write_cbk;
        local->cbk_data = cbk_data;

        vector.iov_base = (void *)buf;
        vector.iov_len = nbytes;
  
        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		op_ret = -1;
		goto out;
        }

        ctx = fd_ctx->ctx;
 
        if (offset < 0) {
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        __offset = fd_ctx->offset;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

        iobref = iobref_new ();
        LIBGF_CLIENT_FOP_ASYNC (ctx,
                                local,
                                libglusterfs_writev_async_cbk,
                                writev,
                                __fd,
                                &vector,
                                1,
                                __offset,
                                iobref);
        iobref_unref (iobref);

out:
        return op_ret;
}

off_t
glusterfs_lseek (glusterfs_file_t fd, off_t offset, int whence)
{
        off_t __offset = 0;
	int32_t op_ret = -1;
        fd_t *__fd = (fd_t *)fd;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
	libglusterfs_client_inode_ctx_t *inode_ctx = NULL;
	libglusterfs_client_ctx_t *ctx = NULL; 

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		__offset = -1;
		goto out;
        }

	ctx = fd_ctx->ctx;

        switch (whence)
        {
        case SEEK_SET:
                __offset = offset;
                break;

        case SEEK_CUR:
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        __offset = fd_ctx->offset;
                }
                pthread_mutex_unlock (&fd_ctx->lock);

                __offset += offset;
                break;

        case SEEK_END:
	{
		char cache_valid = 0;
		off_t end = 0;
		time_t prev, current;
		loc_t loc = {0, };
		struct stat stbuf = {0, };

                if ((inode_ctx = libgf_get_inode_ctx (__fd->inode))) {
			memset (&current, 0, sizeof (current));
			current = time (NULL);
			
			pthread_mutex_lock (&inode_ctx->lock);
			{
				prev = inode_ctx->previous_lookup_time;
			}
			pthread_mutex_unlock (&inode_ctx->lock);
			
			if ((prev >= 0)
                            && (ctx->lookup_timeout >= (current - prev))) {
				cache_valid = 1;
			} 
		}

		if (cache_valid) {
			end = inode_ctx->stbuf.st_size;
		} else {
			op_ret = libgf_client_loc_fill (&loc, ctx,
                                                        __fd->inode->ino, 0,
                                                        NULL);
			if (op_ret == -1) {
				gf_log ("libglusterfsclient",
					GF_LOG_ERROR,
					"libgf_client_loc_fill returned -1, returning EINVAL");
				errno = EINVAL;
				libgf_client_loc_wipe (&loc);
				__offset = -1;
				goto out;
			}
			
			op_ret = libgf_client_lookup (ctx, &loc, &stbuf, NULL,
                                                      NULL);
			if (op_ret < 0) {
				__offset = -1;
				libgf_client_loc_wipe (&loc);
				goto out;
			}

			end = stbuf.st_size;
		}

                __offset = end + offset; 
		libgf_client_loc_wipe (&loc);
	}
	break;

	default:
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"invalid value for whence");
		__offset = -1;
		errno = EINVAL;
		goto out;
        }

        pthread_mutex_lock (&fd_ctx->lock);
        {
                fd_ctx->offset = __offset;
        }
        pthread_mutex_unlock (&fd_ctx->lock);
 
out: 
        return __offset;
}


int32_t
libgf_client_stat_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno,
                       struct stat *buf)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_stat_cbk_stub (frame, 
                                               NULL, 
                                               op_ret, 
                                               op_errno, 
                                               buf);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int32_t 
libgf_client_stat (libglusterfs_client_ctx_t *ctx, 
                   loc_t *loc,
                   struct stat *stbuf)
{
        call_stub_t *stub = NULL;
        int32_t op_ret = 0;
        time_t prev, current;
        libglusterfs_client_inode_ctx_t *inode_ctx = NULL;
        libgf_client_local_t *local = NULL;

        inode_ctx = libgf_get_inode_ctx (loc->inode);
        if (!inode_ctx) {
                errno = EINVAL;
                op_ret = -1;
                goto out;
        }
        current = time (NULL);
        pthread_mutex_lock (&inode_ctx->lock);
        {
                prev = inode_ctx->previous_lookup_time;
        }
        pthread_mutex_unlock (&inode_ctx->lock);

        if ((current - prev) <= ctx->stat_timeout) {
                pthread_mutex_lock (&inode_ctx->lock);
                {
                        memcpy (stbuf, &inode_ctx->stbuf, sizeof (*stbuf));
                }
                pthread_mutex_unlock (&inode_ctx->lock);
		op_ret = 0;
		goto out;
        }
    
        LIBGF_CLIENT_FOP (ctx, stub, stat, local, loc);
 
        op_ret = stub->args.stat_cbk.op_ret;
        errno = stub->args.stat_cbk.op_errno;
        if (stbuf)
                *stbuf = stub->args.stat_cbk.buf;

        libgf_update_iattr_cache (loc->inode, LIBGF_UPDATE_STAT,
                                        &stub->args.stat_cbk.buf);
	call_stub_destroy (stub);

out:
        return op_ret;
}

int
libgf_readlink_loc_fill (libglusterfs_client_ctx_t *ctx, loc_t *linkloc,
                                loc_t *targetloc)
{
        char            targetpath[PATH_MAX];
        int             op_ret = -1;
        char            *target = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, linkloc, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, targetloc, out);

        op_ret = libgf_client_readlink (ctx, linkloc, targetpath, PATH_MAX);
        if (op_ret == -1)
                goto out;

        targetloc->path = strdup (targetpath);
        op_ret = libgf_client_path_lookup (targetloc, ctx, 1);
        if (op_ret == -1)
                goto out;

        target = strdup (targetpath);
        op_ret = libgf_client_loc_fill (targetloc, ctx, 0,
                                               targetloc->parent->ino,
                                                basename (target));
        if (op_ret == -1) {
                errno = EINVAL;
                goto out;
        }

out:
        if (target)
                FREE (target);

        return op_ret;
}

#define LIBGF_DO_LSTAT  0x01
#define LIBGF_DO_STAT   0x02

int
__glusterfs_stat (glusterfs_handle_t handle, const char *path,
                        struct stat *buf, int whichstat)
{
        int32_t op_ret = -1;
        loc_t loc = {0, };
        libglusterfs_client_ctx_t *ctx = handle;
	char *name = NULL, *pathname = NULL;
        loc_t targetloc = {0, };
        loc_t *real_loc = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

	loc.path = strdup (path);
	op_ret = libgf_client_path_lookup (&loc, ctx, 1);
	if (op_ret == -1) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"path lookup failed for (%s)", path);
		goto out;
	}

	pathname = strdup (path);
	name = basename (pathname);

        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino, name);
	if (op_ret == -1) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"libgf_client_loc_fill returned -1, returning EINVAL");
		errno = EINVAL;
		goto out;
	}
        real_loc = &loc;
        /* The stat fop in glusterfs calls lstat. So we have to
         * provide the POSIX compatible stat fop. To do so, we need to ensure
         * that if the @path is a symlink, we must perform a stat on the
         * target of that symlink that the symlink itself(..because if
         * do a stat on the symlink, we're actually doing what lstat
         * should do.
         */
        if (whichstat & LIBGF_DO_LSTAT)
                goto lstat_fop;

        if (!S_ISLNK (loc.inode->st_mode))
                goto lstat_fop;

        op_ret = libgf_readlink_loc_fill (ctx, &loc, &targetloc);
        if (op_ret == -1)
                goto out;
        real_loc = &targetloc;

lstat_fop:

        if (!op_ret) {
                op_ret = libgf_client_stat (ctx, real_loc, buf);
        }

out:
	if (pathname) {
		FREE (pathname);
	}

        libgf_client_loc_wipe (&loc);
        libgf_client_loc_wipe (&targetloc);

        return op_ret;
}

int
glusterfs_glh_stat (glusterfs_handle_t handle, const char *path,
                        struct stat *buf)
{
        return __glusterfs_stat (handle, path, buf, LIBGF_DO_STAT);
}

int
glusterfs_stat (const char *path, struct stat *buf)
{
        struct vmp_entry        *entry = NULL;
        int                     op_ret = -1;
        char                    *vpath = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, buf, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        op_ret = glusterfs_glh_stat (entry->handle, vpath, buf);
out:
        return op_ret;
}

int
glusterfs_glh_lstat (glusterfs_handle_t handle, const char *path, struct stat *buf)
{
        return __glusterfs_stat (handle, path, buf, LIBGF_DO_LSTAT);
}

int
glusterfs_lstat (const char *path, struct stat *buf)
{
        struct vmp_entry        *entry = NULL;
        int                     op_ret = -1;
        char                    *vpath = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, buf, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        op_ret = glusterfs_glh_lstat (entry->handle, vpath, buf);
out:
        return op_ret;
}

static int32_t
libgf_client_fstat_cbk (call_frame_t *frame,
                        void *cookie,
                        xlator_t *this,
                        int32_t op_ret,
                        int32_t op_errno,
                        struct stat *buf)
{  
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_fstat_cbk_stub (frame, 
                                                NULL, 
                                                op_ret, 
                                                op_errno, 
                                                buf);

        LIBGF_REPLY_NOTIFY (local);
        return 0;

}

int32_t
libgf_client_fstat (libglusterfs_client_ctx_t *ctx, 
                    fd_t *fd, 
                    struct stat *buf)
{
        call_stub_t *stub = NULL;
        int32_t op_ret = 0;
        fd_t *__fd = fd;
        time_t current, prev;
        libglusterfs_client_inode_ctx_t *inode_ctx = NULL;
        libgf_client_local_t *local = NULL;

        current = time (NULL);

        inode_ctx = libgf_get_inode_ctx (__fd->inode);
        if (!inode_ctx) {
                errno = EINVAL;
                op_ret = -1;
                goto out;
        }

        pthread_mutex_lock (&inode_ctx->lock);
        {
                prev = inode_ctx->previous_stat_time;
        }
        pthread_mutex_unlock (&inode_ctx->lock);

        if ((current - prev) <= ctx->stat_timeout) {
                pthread_mutex_lock (&inode_ctx->lock);
                {
                        memcpy (buf, &inode_ctx->stbuf, sizeof (*buf));
                }
                pthread_mutex_unlock (&inode_ctx->lock);
		op_ret = 0;
		goto out;
        }

        LIBGF_CLIENT_FOP (ctx, stub, fstat, local, __fd);
 
        op_ret = stub->args.fstat_cbk.op_ret;
        errno = stub->args.fstat_cbk.op_errno;
        if (buf)
                *buf = stub->args.fstat_cbk.buf;

        libgf_update_iattr_cache (fd->inode, LIBGF_UPDATE_STAT,
                                        &stub->args.fstat_cbk.buf);
	call_stub_destroy (stub);

out:
        return op_ret;
}

int32_t 
glusterfs_fstat (glusterfs_file_t fd, struct stat *buf) 
{
        libglusterfs_client_ctx_t *ctx;
        fd_t *__fd = (fd_t *)fd;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
	int32_t op_ret = -1;

        fd_ctx = libgf_get_fd_ctx (fd);
        if (!fd_ctx) {
                errno = EBADF;
		op_ret = -1;
		goto out;
        }

        ctx = fd_ctx->ctx;

	op_ret = libgf_client_fstat (ctx, __fd, buf);

out:
	return op_ret;
}


static int32_t
libgf_client_mkdir_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			inode_t *inode,
			struct stat *buf)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_mkdir_cbk_stub (frame, NULL, op_ret, op_errno,
                                                inode, buf);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}


static int32_t
libgf_client_mkdir (libglusterfs_client_ctx_t *ctx,
		    loc_t *loc,
		    mode_t mode)
{
	int32_t op_ret = -1;
        call_stub_t *stub = NULL;
        libgf_client_local_t *local = NULL;
        inode_t *libgf_inode = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, mkdir, local, loc, mode);
        op_ret = stub->args.mkdir_cbk.op_ret;
        errno = stub->args.mkdir_cbk.op_errno;

        if (op_ret == -1)
                goto out;

	libgf_inode = stub->args.mkdir_cbk.inode;
        inode_link (libgf_inode, loc->parent, loc->name,
                        &stub->args.mkdir_cbk.buf);

        /* inode_lookup (libgf_inode); */

        libgf_alloc_inode_ctx (ctx, libgf_inode);
        libgf_update_iattr_cache (libgf_inode, LIBGF_UPDATE_ALL,
                                        &stub->args.mkdir_cbk.buf);

out:
	call_stub_destroy (stub);

	return op_ret;
}


int32_t
glusterfs_glh_mkdir (glusterfs_handle_t handle, const char *path, mode_t mode)
{
	libglusterfs_client_ctx_t *ctx = handle;
	loc_t loc = {0, };
	char *pathname = NULL, *name = NULL;
	int32_t op_ret = -1;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

	loc.path = strdup (path);
	op_ret = libgf_client_path_lookup (&loc, ctx, 1);
	if (op_ret == 0) {
                op_ret = -1;
                errno = EEXIST;
		goto out;
	}

        op_ret = libgf_client_path_lookup (&loc, ctx, 0);
        if (op_ret == -1) {
                errno = ENOENT;
                goto out;
        }

	pathname = strdup (path);
	name = basename (pathname);

        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino, name);
	if (op_ret == -1) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"libgf_client_loc_fill returned -1, returning EINVAL");
		errno = EINVAL;
		goto out;
	}

        loc.inode = inode_new (ctx->itable);
	op_ret = libgf_client_mkdir (ctx, &loc, mode); 
	if (op_ret == -1) {
		goto out;
	}

out:
	libgf_client_loc_wipe (&loc);
	if (pathname) {
		free (pathname);
		pathname = NULL;
	}

	return op_ret;
}

int32_t
glusterfs_mkdir (const char *path, mode_t mode)
{
        struct vmp_entry        *entry = NULL;
        int                     op_ret = -1;
        char                    *vpath = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        op_ret = glusterfs_glh_mkdir (entry->handle, vpath, mode);
out:
        return op_ret;
}

static int32_t
libgf_client_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
        int32_t op_ret, int32_t op_errno)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_rmdir_cbk_stub (frame, NULL, op_ret, op_errno);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

static int32_t
libgf_client_rmdir (libglusterfs_client_ctx_t *ctx, loc_t *loc)
{
        int32_t op_ret = -1;
        call_stub_t *stub = NULL;
        libgf_client_local_t *local = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, rmdir, local, loc);

        op_ret = stub->args.rmdir_cbk.op_ret;
        errno = stub->args.rmdir_cbk.op_errno;

        if (stub->args.rmdir_cbk.op_ret != 0)
                goto out;

        inode_unlink (loc->inode, loc->parent, loc->name);

out:
	call_stub_destroy (stub);

	return op_ret;
}

int32_t
glusterfs_glh_rmdir (glusterfs_handle_t handle, const char *path)
{
	libglusterfs_client_ctx_t *ctx = handle;
	loc_t loc = {0, };
	char *pathname = NULL, *name = NULL;
	int32_t op_ret = -1;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

	loc.path = strdup (path);
	op_ret = libgf_client_path_lookup (&loc, ctx, 1);
	if (op_ret == -1) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"path lookup failed for (%s)", path);
		goto out;
	}

	pathname = strdup (path);
	name = basename (pathname);

        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino, name);
	if (op_ret == -1) {
		gf_log ("libglusterfsclient",
			GF_LOG_ERROR,
			"libgf_client_loc_fill returned -1, returning EINVAL");
		errno = EINVAL;
		goto out;
	}

	op_ret = libgf_client_rmdir (ctx, &loc);
	if (op_ret == -1) {
		goto out;
	}

out:
	libgf_client_loc_wipe (&loc);

	if (pathname) {
		free (pathname);
		pathname = NULL;
	}

	return op_ret;
}

int32_t
glusterfs_rmdir (const char *path)
{
        struct vmp_entry        *entry = NULL;
        int                     op_ret = -1;
        char                    *vpath = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        op_ret = glusterfs_glh_rmdir (entry->handle, vpath);
out:
        return op_ret;
}

int
libgf_client_chmod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct stat *buf)
{
        libgf_client_local_t    *local = frame->local;

        local->reply_stub = fop_chmod_cbk_stub (frame, NULL, op_ret, op_errno,
                                                        buf);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int
libgf_client_chmod (libglusterfs_client_ctx_t *ctx, loc_t * loc, mode_t mode)
{
        int                             op_ret = -1;
        libgf_client_local_t            *local = NULL;
        call_stub_t                     *stub = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, chmod, local, loc, mode);

        op_ret = stub->args.chmod_cbk.op_ret;
        errno = stub->args.chmod_cbk.op_errno;

        if (op_ret == -1)
                goto out;

        libgf_update_iattr_cache (loc->inode, LIBGF_UPDATE_STAT,
                                        &stub->args.chmod_cbk.buf);
out:
        call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_glh_chmod (glusterfs_handle_t handle, const char *path, mode_t mode)
{
        int                             op_ret = -1;
        libglusterfs_client_ctx_t       *ctx = handle;
        loc_t                           loc = {0, };
        char                            *name = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

        loc.path = strdup(path);
        op_ret = libgf_client_path_lookup (&loc, ctx, 1);
        if (op_ret == -1)
                goto out;

        name = strdup (path);
        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino,
                                                basename (name));
        if (op_ret == -1) {
                errno = EINVAL;
                goto out;
        }

        op_ret = libgf_client_chmod (ctx, &loc, mode);

out:
        if (name)
                FREE (name);

        libgf_client_loc_wipe (&loc);
        return op_ret;
}

int
glusterfs_chmod (const char *path, mode_t mode)
{
        struct vmp_entry        *entry = NULL;
        int                     op_ret = -1;
        char                    *vpath = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        op_ret = glusterfs_glh_chmod (entry->handle, vpath, mode);
out:
        return op_ret;
}

int
libgf_client_chown_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct stat *sbuf)
{
        libgf_client_local_t    *local = frame->local;

        local->reply_stub = fop_chown_cbk_stub (frame, NULL, op_ret, op_errno,
                                                sbuf);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int
libgf_client_chown (libglusterfs_client_ctx_t *ctx, loc_t *loc, uid_t uid,
                gid_t gid)
{
        call_stub_t             *stub = NULL;
        libgf_client_local_t    *local = NULL;
        int32_t                 op_ret = -1;

        LIBGF_CLIENT_FOP (ctx, stub, chown, local, loc, uid, gid);

        op_ret = stub->args.chown_cbk.op_ret;
        errno = stub->args.chown_cbk.op_errno;

        if (op_ret == -1)
                goto out;

        libgf_update_iattr_cache (loc->inode, LIBGF_UPDATE_STAT,
                                        &stub->args.chown_cbk.buf);
out:
        call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_glh_chown (glusterfs_handle_t handle, const char *path, uid_t owner,
                        gid_t group)
{
        int                             op_ret = -1;
        libglusterfs_client_ctx_t       *ctx = handle;
        loc_t                           loc = {0, };
        char                            *name = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

        loc.path = strdup (path);
        op_ret = libgf_client_path_lookup (&loc, ctx, 1);
        if (op_ret == -1)
                goto out;

        name = strdup (path);
        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino,
                        basename ((char *)name));
        if (op_ret == -1) {
                errno = EINVAL;
                goto out;
        }

        op_ret = libgf_client_chown (ctx, &loc, owner, group);
out:
        if (name)
                FREE (name);
        libgf_client_loc_wipe (&loc);
        return op_ret;
}

int
glusterfs_chown (const char *path, uid_t owner, gid_t group)
{
        struct vmp_entry        *entry = NULL;
        int                     op_ret = -1;
        char                    *vpath = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        op_ret = glusterfs_glh_chown (entry->handle, vpath, owner, group);
out:
        return op_ret;
}

glusterfs_dir_t
glusterfs_glh_opendir (glusterfs_handle_t handle, const char *path)
{
        int                             op_ret = -1;
        libglusterfs_client_ctx_t       *ctx = handle;
        loc_t                           loc = {0, };
        fd_t                            *dirfd = NULL;
        char                            *name = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

        loc.path = strdup (path);
        op_ret = libgf_client_path_lookup (&loc, ctx, 1);

        if (op_ret == -1)
                goto out;

        name = strdup (path);
        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino,
                        basename (name));
        if (op_ret == -1) {
                errno = EINVAL;
                goto out;
        }

        if (!S_ISDIR (loc.inode->st_mode)) {
                errno = ENOTDIR;
                op_ret = -1;
                goto out;
        }

        dirfd = fd_create (loc.inode, 0);
        op_ret = libgf_client_opendir (ctx, &loc, dirfd);

        if (op_ret == -1) {
                fd_unref (dirfd);
                dirfd = NULL;
                goto out;
        }

        if (libgf_get_fd_ctx (dirfd))
                goto out;

        if (!(libgf_alloc_fd_ctx (ctx, dirfd))) {
                op_ret = -1;
                errno = EINVAL;
                fd_unref (dirfd);
                dirfd = NULL;
        }

out:
        if (name)
                FREE (name);
        libgf_client_loc_wipe (&loc);
        return dirfd;
}

glusterfs_dir_t
glusterfs_opendir (const char *path)
{
        struct vmp_entry        *entry = NULL;
        char                    *vpath = NULL;
        glusterfs_dir_t         dir = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        dir = glusterfs_glh_opendir (entry->handle, vpath);
out:
        return dir;
}

int
glusterfs_closedir (glusterfs_dir_t dirfd)
{
        int                             op_ret = -1;
        libglusterfs_client_fd_ctx_t    *fdctx = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, dirfd, out);
        fdctx = libgf_get_fd_ctx (dirfd);

        if (fdctx == NULL) {
                errno = EBADF;
                op_ret = -1;
                goto out;
        }

        op_ret = libgf_client_flush (fdctx->ctx, (fd_t *)dirfd);
        fd_unref ((fd_t *)dirfd);

out:
        return op_ret;
}

int
libgf_client_fchmod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                struct stat *buf)
{
        libgf_client_local_t    *local = frame->local;

        local->reply_stub = fop_fchmod_cbk_stub (frame, NULL, op_ret, op_errno,
                                                        buf);
        LIBGF_REPLY_NOTIFY (local);

        return 0;
}

int
libgf_client_fchmod (libglusterfs_client_ctx_t *ctx, fd_t *fd, mode_t mode)
{
        int                     op_ret = -1;
        libgf_client_local_t    *local = NULL;
        call_stub_t             *stub = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, fchmod, local, fd, mode);

        op_ret = stub->args.fchmod_cbk.op_ret;
        errno = stub->args.fchmod_cbk.op_errno;

        if (op_ret == -1)
                goto out;

        libgf_update_iattr_cache (fd->inode, LIBGF_UPDATE_STAT,
                                        &stub->args.fchmod_cbk.buf);
out:
        call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_fchmod (glusterfs_file_t fd, mode_t mode)
{
        libglusterfs_client_fd_ctx_t    *fdctx = NULL;
        int                             op_ret = -1;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, fd, out);
        fdctx = libgf_get_fd_ctx (fd);

        if (!fdctx) {
                errno = EBADF;
                goto out;
        }

        op_ret = libgf_client_fchmod (fdctx->ctx, fd, mode);

out:
        return op_ret;
}

int
libgf_client_fchown_cbk (call_frame_t *frame, void *cookie, xlator_t *xlator,
                                int32_t op_ret, int32_t op_errno,
                                struct stat *buf)
{
        libgf_client_local_t            *local = frame->local;

        local->reply_stub = fop_fchown_cbk_stub (frame, NULL, op_ret, op_errno,
                                                        buf);
        LIBGF_REPLY_NOTIFY (local);

        return 0;
}

int
libgf_client_fchown (libglusterfs_client_ctx_t *ctx, fd_t *fd, uid_t uid,
                        gid_t gid)
{
        call_stub_t             *stub  = NULL;
        libgf_client_local_t    *local = NULL;
        int                     op_ret = -1;

        LIBGF_CLIENT_FOP (ctx, stub, fchown, local, fd, uid, gid);

        op_ret = stub->args.fchown_cbk.op_ret;
        errno = stub->args.fchown_cbk.op_errno;

        if (op_ret == -1)
                goto out;

        libgf_update_iattr_cache (fd->inode, LIBGF_UPDATE_STAT,
                                        &stub->args.fchown_cbk.buf);
out:
        call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_fchown (glusterfs_file_t fd, uid_t uid, gid_t gid)
{
        int                             op_ret = -1;
        libglusterfs_client_fd_ctx_t    *fdctx = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, fd, out);

        fdctx = libgf_get_fd_ctx (fd);
        if (!fd) {
                errno = EBADF;
                goto out;
        }

        op_ret = libgf_client_fchown (fdctx->ctx, fd, uid, gid);

out:
        return op_ret;
}

int
libgf_client_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *xlator,
                                int32_t op_ret, int32_t op_errno)
{
        libgf_client_local_t    *local = frame->local;

        local->reply_stub = fop_fsync_cbk_stub (frame, NULL, op_ret, op_errno);

        LIBGF_REPLY_NOTIFY (local);

        return 0;
}

int
libgf_client_fsync (libglusterfs_client_ctx_t *ctx, fd_t *fd)
{
        libgf_client_local_t    *local = NULL;
        call_stub_t             *stub = NULL;
        int                     op_ret = -1;

        LIBGF_CLIENT_FOP (ctx, stub, fsync, local, fd, 0);

        op_ret = stub->args.fsync_cbk.op_ret;
        errno = stub->args.fsync_cbk.op_errno;

        call_stub_destroy (stub);

        return op_ret;
}

int
glusterfs_fsync (glusterfs_file_t *fd)
{
        libglusterfs_client_fd_ctx_t    *fdctx = NULL;
        int                             op_ret = -1;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, fd, out);

        fdctx = libgf_get_fd_ctx ((fd_t *)fd);
        if (!fd) {
                errno = EBADF;
                goto out;
        }

        op_ret = libgf_client_fsync (fdctx->ctx, (fd_t *)fd);

out:
        return op_ret;
}

int
libgf_client_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *xlator
                                ,int32_t op_ret, int32_t op_errno,
                                struct stat *buf)
{
        libgf_client_local_t    *local = frame->local;

        local->reply_stub = fop_ftruncate_cbk_stub (frame, NULL, op_ret,
                                                        op_errno, buf);

        LIBGF_REPLY_NOTIFY (local);

        return 0;
}

int
libgf_client_ftruncate (libglusterfs_client_ctx_t *ctx, fd_t *fd,
                                off_t length)
{
        libgf_client_local_t            *local = NULL;
        call_stub_t                     *stub = NULL;
        int                             op_ret = -1;
        libglusterfs_client_fd_ctx_t    *fdctx = NULL;

        if (!(fd->flags & O_RDWR) && (!(fd->flags & O_WRONLY))) {
                errno = EBADF;
                goto out;
        }

        LIBGF_CLIENT_FOP (ctx, stub, ftruncate, local, fd, length);

        op_ret = stub->args.ftruncate_cbk.op_ret;
        errno = stub->args.ftruncate_cbk.op_errno;

        if (op_ret == -1)
                goto out;
        libgf_update_iattr_cache (fd->inode, LIBGF_UPDATE_STAT,
                                        &stub->args.ftruncate_cbk.buf);

        fdctx = libgf_get_fd_ctx (fd);
        if (!fd) {
                errno = EINVAL;
                op_ret = -1;
                goto out;
        }

        pthread_mutex_lock (&fdctx->lock);
        {
                fdctx->offset = stub->args.ftruncate_cbk.buf.st_size;
        }
        pthread_mutex_lock (&fdctx->lock);

out:
        call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_ftruncate (glusterfs_file_t fd, off_t length)
{
        libglusterfs_client_fd_ctx_t    *fdctx = NULL;
        int                             op_ret = -1;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, fd, out);

        fdctx = libgf_get_fd_ctx (fd);
        if (!fdctx) {
                errno = EBADF;
                goto out;
        }

        op_ret = libgf_client_ftruncate (fdctx->ctx, fd, length);

out:
        return op_ret;
}

int
libgf_client_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                inode_t *inode, struct stat *buf)
{
        libgf_client_local_t            *local = frame->local;

        local->reply_stub = fop_link_cbk_stub (frame, NULL, op_ret, op_errno,
                                                inode, buf);

        LIBGF_REPLY_NOTIFY (local);

        return 0;
}

int
libgf_client_link (libglusterfs_client_ctx_t *ctx, loc_t *old, loc_t *new)
{
        call_stub_t                     *stub = NULL;
        libgf_client_local_t            *local = NULL;
        int                             op_ret = -1;
        inode_t                         *inode = NULL;
        struct stat                     *sbuf = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, link, local, old, new);

        op_ret = stub->args.link_cbk.op_ret;
        errno = stub->args.link_cbk.op_errno;

        if (op_ret == -1)
                goto out;

        inode = stub->args.link_cbk.inode;
        sbuf = &stub->args.link_cbk.buf;
        inode_link (inode, new->parent, basename ((char *)new->path), sbuf);
        libgf_update_iattr_cache (inode, LIBGF_UPDATE_STAT, sbuf);

out:
        call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_glh_link (glusterfs_handle_t handle, const char *oldpath,
                        const char *newpath)
{
        libglusterfs_client_ctx_t       *ctx = handle;
        int                             op_ret = -1;
        loc_t                           old = {0,};
        loc_t                           new = {0,};
        char                            *oldname = NULL;
        char                            *newname = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, oldpath, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, newpath, out);

        old.path = strdup (oldpath);
        op_ret = libgf_client_path_lookup (&old, ctx, 1);
        if (op_ret == -1) {
                errno = ENOENT;
                goto out;
        }

        oldname = strdup (oldpath);
        op_ret = libgf_client_loc_fill (&old, ctx, 0, old.parent->ino,
                                                basename (oldname));
        if (op_ret == -1) {
                errno = EINVAL;
                goto out;
        }

        if (S_ISDIR (old.inode->st_mode)) {
                errno = EPERM;
                op_ret = -1;
                goto out;
        }

        new.path = strdup (newpath);
        op_ret = libgf_client_path_lookup (&new, ctx, 1);
        if (op_ret == 0) {
                errno = EEXIST;
                op_ret = -1;
                goto out;
        }

        newname = strdup (newpath);
        libgf_client_loc_fill (&new, ctx, 0, new.parent->ino,
                        basename (newname));
        op_ret = libgf_client_link (ctx, &old, &new);

out:
        if (oldname)
                FREE (oldname);
        if (newname)
                FREE (newname);
        libgf_client_loc_wipe (&old);
        libgf_client_loc_wipe (&new);

        return op_ret;
}

int
glusterfs_link (const char *oldpath, const char *newpath)
{
        struct vmp_entry        *oldentry = NULL;
        struct vmp_entry        *newentry = NULL;
        char                    *oldvpath = NULL;
        char                    *newvpath = NULL;
        int                     op_ret = -1;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, oldpath, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, newpath, out);

        oldentry = libgf_vmp_search_entry ((char *)oldpath);
        if (!oldentry) {
                errno = ENODEV;
                goto out;
        }

        newentry = libgf_vmp_search_entry ((char *)newpath);
        if (!newentry) {
                errno =  ENODEV;
                goto out;
        }

        /* Cannot hard link across glusterfs mounts. */
        if (newentry != oldentry) {
                errno = EXDEV;
                goto out;
        }

        newvpath = libgf_vmp_virtual_path (newentry, newpath);
        oldvpath = libgf_vmp_virtual_path (oldentry, oldpath);
        op_ret = glusterfs_glh_link (newentry->handle, oldvpath, newvpath);
out:
        return op_ret;
}

int32_t
libgf_client_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct statvfs *buf)
{
        libgf_client_local_t    *local = frame->local;

        local->reply_stub = fop_statfs_cbk_stub (frame, NULL, op_ret, op_errno,
                                                 buf);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int32_t
libgf_client_statvfs (libglusterfs_client_ctx_t *ctx, loc_t *loc,
                      struct statvfs *buf)
{
        call_stub_t             *stub = NULL;
        libgf_client_local_t    *local = NULL;
        int32_t                 op_ret = -1;

        /* statfs fop receives struct statvfs as an argument */

        /* libgf_client_statfs_cbk will be the callback, not
           libgf_client_statvfs_cbk. see definition of LIBGF_CLIENT_FOP
        */
        LIBGF_CLIENT_FOP (ctx, stub, statfs, local, loc);

        op_ret = stub->args.statfs_cbk.op_ret;
        errno = stub->args.statfs_cbk.op_errno;
        if (op_ret == -1)
                goto out;

        if (buf)
                memcpy (buf, &stub->args.statfs_cbk.buf, sizeof (*buf));
out:
        call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_glh_statfs (glusterfs_handle_t handle, const char *path,
                        struct statfs *buf)
{
        struct statvfs                  stvfs = {0, };
        int32_t                         op_ret = -1;
        loc_t                           loc = {0, };
        libglusterfs_client_ctx_t       *ctx = handle;
        char                            *name = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

        loc.path = strdup (path);
        op_ret = libgf_client_path_lookup (&loc, ctx, 1);
        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "path lookup failed for (%s)", path);
                goto out;
        }

        name = strdup (path);
        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino,
                                        basename (name));
        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "libgf_client_loc_fill returned -1, "
                                "returning EINVAL");
                        errno = EINVAL;
                goto out;
        }

        op_ret = libgf_client_statvfs (ctx, &loc, &stvfs);
        if (op_ret == 0) {
                buf->f_type = 0;
                buf->f_bsize = stvfs.f_bsize;
                buf->f_blocks = stvfs.f_blocks;
                buf->f_bfree = stvfs.f_bfree;
                buf->f_bavail = stvfs.f_bavail;
                buf->f_files = stvfs.f_bavail;
                buf->f_ffree = stvfs.f_ffree;
                /* FIXME: buf->f_fsid has either "val" or "__val" as member
                   based on conditional macro expansion. see definition of
                   fsid_t - Raghu
                   It seems have different structure member names on
                   different archs, so I am stepping down to doing a struct
                   to struct copy. :Shehjar
                */
                memcpy (&buf->f_fsid, &stvfs.f_fsid, sizeof (stvfs.f_fsid));
                buf->f_namelen = stvfs.f_namemax;
        }

out:
        if (name)
                FREE (name);
        libgf_client_loc_wipe (&loc);
        return op_ret;
}

int
glusterfs_statfs (const char *path, struct statfs *buf)
{
        struct vmp_entry        *entry = NULL;
        char                    *vpath = NULL;
        int                     op_ret = -1;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, buf, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        op_ret = glusterfs_glh_statfs (entry->handle, vpath, buf);
out:
        return op_ret;
}

int
glusterfs_glh_statvfs (glusterfs_handle_t handle, const char *path,
                                struct statvfs *buf)
{
        int32_t                         op_ret = -1;
        loc_t                           loc = {0, };
        libglusterfs_client_ctx_t       *ctx = handle;
        char                            *name = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

        loc.path = strdup (path);
        op_ret = libgf_client_path_lookup (&loc, ctx, 1);
        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "path lookup failed for (%s)", path);
                goto out;
        }

        name = strdup (path);
        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino,
                                                basename (name));
	if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "libgf_client_loc_fill returned -1, returning"
                                " EINVAL");
                errno = EINVAL;
                goto out;
	}

        op_ret = libgf_client_statvfs (ctx, &loc, buf);
out:
        if (name)
                FREE (name);
        libgf_client_loc_wipe (&loc);
        return op_ret;
}

int
glusterfs_statvfs (const char *path, struct statvfs *buf)
{
        struct vmp_entry        *entry = NULL;
        char                    *vpath = NULL;
        int                     op_ret = -1;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, path, out);
        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, buf, out);

        entry = libgf_vmp_search_entry ((char *)path);
        if (!entry) {
                errno = ENODEV;
                goto out;
        }

        vpath = libgf_vmp_virtual_path (entry, path);
        op_ret = glusterfs_glh_statvfs (entry->handle, vpath, buf);
out:
        return op_ret;
}

int32_t
libgf_client_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                struct stat *buf)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_rename_cbk_stub (frame, NULL, op_ret, op_errno,
                                                 buf);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int32_t
libgf_client_rename (libglusterfs_client_ctx_t *ctx, loc_t *oldloc,
                     loc_t *newloc)
{
        int                             op_ret = -1;
        libgf_client_local_t            *local = NULL;
        call_stub_t                     *stub = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, rename, local, oldloc, newloc);

        op_ret = stub->args.rename_cbk.op_ret;
        errno = stub->args.rename_cbk.op_errno;

        if (op_ret == -1)
                goto out;

        if (!libgf_get_inode_ctx (newloc->inode))
                libgf_alloc_inode_ctx (ctx, newloc->inode);

        libgf_update_iattr_cache (newloc->inode, LIBGF_UPDATE_STAT,
                                        &stub->args.rename_cbk.buf);

        inode_unlink (oldloc->inode, oldloc->parent, oldloc->name);
out:
        call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_rename (glusterfs_handle_t handle, const char *oldpath,
                  const char *newpath)
{
        int32_t                         op_ret = -1;
        loc_t                           oldloc = {0, }, newloc = {0, };
        libglusterfs_client_ctx_t       *ctx = handle;
        char                            *newname = NULL;
        char                            *oldname = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, oldpath, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, newpath, out);

        oldloc.path = strdup (oldpath);
        op_ret = libgf_client_path_lookup (&oldloc, ctx, 1);
        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "path lookup failed for (%s)", oldpath);
                goto out;
        }

        newloc.path = strdup (newpath);
        op_ret = libgf_client_path_lookup (&newloc, ctx, 1);
        if (op_ret == 0) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "newpath (%s) already exists, returning"
                                " EEXIST", newpath);
                errno = EEXIST;
                op_ret = -1;
                goto out;
        }

        oldname = strdup (oldpath);
        op_ret = libgf_client_loc_fill (&oldloc, ctx, 0, oldloc.parent->ino,
                                        basename (oldname));
	if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "libgf_client_loc_fill returned -1,"
                                " returning EINVAL");
                errno = EINVAL;
                goto out;
        }

        newname = strdup (newpath);
        op_ret = libgf_client_loc_fill (&newloc, ctx, 0, newloc.parent->ino,
                                        basename (newname));
	if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "libgf_client_loc_fill returned -1,"
                                " returning EINVAL");
                errno = EINVAL;
                goto out;
        }
        op_ret = libgf_client_rename (ctx, &oldloc, &newloc);

out:
        if (oldname)
                FREE (oldname);
        if (newname)
                FREE (newname);
        libgf_client_loc_wipe (&newloc);
        libgf_client_loc_wipe (&oldloc);

        return op_ret;
}

int32_t
libgf_client_utimens_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                struct stat *buf)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_utimens_cbk_stub (frame, NULL, op_ret,
                                                        op_errno, buf);

        LIBGF_REPLY_NOTIFY (local);

        return 0;
}

int32_t
libgf_client_utimens (libglusterfs_client_ctx_t *ctx, loc_t *loc,
                      struct timespec ts[2])
{
        int                             op_ret = -1;
        libgf_client_local_t            *local = NULL;
        call_stub_t                     *stub = NULL;
        struct stat                     *stbuf = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, utimens, local, loc, ts);

        op_ret = stub->args.utimens_cbk.op_ret;
        errno = stub->args.utimens_cbk.op_errno;
        stbuf = &stub->args.utimens_cbk.buf;

        if (op_ret == -1)
                goto out;

        libgf_update_iattr_cache (loc->inode, LIBGF_UPDATE_STAT, stbuf);

out:
        call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_utimes (glusterfs_handle_t handle, const char *path,
                  const struct timeval times[2])
{
        int32_t                         op_ret = -1;
        loc_t                           loc = {0, };
        libglusterfs_client_ctx_t       *ctx = handle;
        struct timespec                 ts[2] = {{0,},{0,}};
        char                            *name = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

        loc.path = strdup (path);
        op_ret = libgf_client_path_lookup (&loc, ctx, 1);
        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "path lookup failed for (%s)", path);
                goto out;
        }

        name = strdup (path);
        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino,
                                                basename (name));
        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "libgf_client_loc_fill returned -1"
                                " returning EINVAL");
                errno = EINVAL;
                goto out;
        }

        ts[0].tv_sec = times[0].tv_sec;
        ts[0].tv_nsec = times[0].tv_usec * 1000;
        ts[1].tv_sec = times[1].tv_sec;
        ts[1].tv_nsec = times[1].tv_usec * 1000;

        op_ret = libgf_client_utimens (ctx, &loc, ts);
out:
        if (name)
                FREE (name);
        libgf_client_loc_wipe (&loc);
        return op_ret;
}

int
glusterfs_utime (glusterfs_handle_t handle, const char *path,
                 const struct utimbuf *buf)
{
        int32_t                         op_ret = -1;
        loc_t                           loc = {0, };
        libglusterfs_client_ctx_t       *ctx = handle;
        struct timespec                 ts[2] = {{0,},{0,}};
        char                            *name = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

        loc.path = strdup (path);
        op_ret = libgf_client_path_lookup (&loc, ctx, 1);
        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "path lookup failed for (%s)", path);
                goto out;
        }

        name = strdup (path);
        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino,
                                                basename (name));
        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "libgf_client_loc_fill returned -1,"
                                " returning EINVAL");
                errno = EINVAL;
                goto out;
        }

        ts[0].tv_sec = buf->actime;
        ts[0].tv_nsec = 0;

        ts[1].tv_sec = buf->modtime;
        ts[1].tv_nsec = 0;

        op_ret = libgf_client_utimens (ctx, &loc, ts);
out:
        if (name)
                FREE (name);
        libgf_client_loc_wipe (&loc);
        return op_ret;
}

static int32_t
libgf_client_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                inode_t *inode, struct stat *buf)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_mknod_cbk_stub (frame, NULL, op_ret, op_errno,
                                                inode, buf);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

static int32_t
libgf_client_mknod (libglusterfs_client_ctx_t *ctx, loc_t *loc, mode_t mode,
                    dev_t rdev)
{
        int32_t                 op_ret = -1;
        call_stub_t             *stub = NULL;
        libgf_client_local_t    *local = NULL;
        inode_t                 *inode = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, mknod, local, loc, mode, rdev);

        op_ret = stub->args.mknod_cbk.op_ret;
        errno = stub->args.mknod_cbk.op_errno;
        if (op_ret == -1)
                goto out;

        inode = stub->args.mknod_cbk.inode;
        inode_link (inode, loc->parent, loc->name, &stub->args.mknod_cbk.buf);

        if (!libgf_alloc_inode_ctx (ctx, inode))
                libgf_alloc_inode_ctx (ctx, inode);

        libgf_update_iattr_cache (inode, LIBGF_UPDATE_STAT,
                                        &stub->args.mknod_cbk.buf);

out:
	call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_mknod(glusterfs_handle_t handle, const char *path, mode_t mode,
                dev_t dev)
{
        libglusterfs_client_ctx_t       *ctx = handle;
        loc_t                           loc = {0, };
        char                            *name = NULL;
        int32_t                         op_ret = -1;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

        loc.path = strdup (path);
        op_ret = libgf_client_path_lookup (&loc, ctx, 1);
        if (op_ret == 0) {
                op_ret = -1;
                errno = EEXIST;
                goto out;
        }

        op_ret = libgf_client_path_lookup (&loc, ctx, 0);
        if (op_ret == -1) {
                errno = ENOENT;
                goto out;
        }

        name = strdup (path);
        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino,
                                                basename (name));
	if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "libgf_client_loc_fill returned -1, "
                                " returning EINVAL");
                errno = EINVAL;
                goto out;
        }

        loc.inode = inode_new (ctx->itable);
        op_ret = libgf_client_mknod (ctx, &loc, mode, dev);

out:
	libgf_client_loc_wipe (&loc);
	if (name)
                FREE (name);

	return op_ret;
}

int
glusterfs_mkfifo (glusterfs_handle_t handle, const char *path, mode_t mode)
{

        libglusterfs_client_ctx_t       *ctx = handle;
        loc_t                           loc = {0, };
        char                            *name = NULL;
        int32_t                         op_ret = -1;
        dev_t                           dev = 0;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

        loc.path = strdup (path);
        op_ret = libgf_client_path_lookup (&loc, ctx, 1);
        if (op_ret == 0) {
                op_ret = -1;
                errno = EEXIST;
                goto out;
        }

        op_ret = libgf_client_path_lookup (&loc, ctx, 0);
        if (op_ret == -1) {
                errno = ENOENT;
                goto out;
        }

        name = strdup (path);
        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino,
                                                basename (name));
        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "libgf_client_loc_fill returned -1, "
                                "returning EINVAL");
                errno = EINVAL;
                goto out;
        }

        loc.inode = inode_new (ctx->itable);
        op_ret = libgf_client_mknod (ctx, &loc, mode | S_IFIFO, dev);

out:
	libgf_client_loc_wipe (&loc);
        if (name)
                free (name);

        return op_ret;
}

int32_t
libgf_client_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                                int32_t op_ret, int32_t op_errno)
{
        libgf_client_local_t    *local = frame->local;

        local->reply_stub = fop_unlink_cbk_stub (frame, NULL, op_ret,
                                                        op_errno);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int
libgf_client_unlink (libglusterfs_client_ctx_t *ctx, loc_t *loc)
{
        int                             op_ret = -1;
        libgf_client_local_t            *local = NULL;
        call_stub_t                     *stub = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, unlink, local, loc);

        op_ret = stub->args.unlink_cbk.op_ret;
        errno = stub->args.unlink_cbk.op_errno;

        if (op_ret == -1)
                goto out;

        inode_unlink (loc->inode, loc->parent, loc->name);

out:
        call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_unlink (glusterfs_handle_t handle, const char *path)
{
        int32_t                         op_ret = -1;
        loc_t                           loc = {0, };
        libglusterfs_client_ctx_t       *ctx = handle;
        char                            *name = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

        loc.path = strdup (path);
        op_ret = libgf_client_path_lookup (&loc, ctx, 1);
        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "path lookup failed for (%s)", path);
                goto out;
        }

        name = strdup (path);
        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino,
                                                basename (name));
	if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "libgf_client_loc_fill returned -1, "
                                " returning EINVAL");
                errno = EINVAL;
                goto out;
        }

        op_ret = libgf_client_unlink (ctx, &loc);

out:
        if (name)
                FREE (name);
        libgf_client_loc_wipe (&loc);
        return op_ret;
}

static int32_t
libgf_client_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                inode_t *inode, struct stat *buf)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_symlink_cbk_stub (frame, NULL, op_ret,
                                                  op_errno, inode, buf);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int32_t
libgf_client_symlink (libglusterfs_client_ctx_t *ctx, const char *linkpath,
                      loc_t *loc)
{
        int                     op_ret = -1;
        libgf_client_local_t    *local = NULL;
        call_stub_t             *stub = NULL;
        inode_t                 *inode = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, symlink, local, linkpath, loc);

        op_ret = stub->args.symlink_cbk.op_ret;
        errno = stub->args.symlink_cbk.op_errno;
        if (op_ret == -1)
                goto out;

        inode = stub->args.symlink_cbk.inode;
        inode_link (inode, loc->parent, loc->name,
                        &stub->args.symlink_cbk.buf);

        if (!libgf_get_inode_ctx (inode))
                libgf_alloc_inode_ctx (ctx, inode);

        libgf_update_iattr_cache (inode, LIBGF_UPDATE_STAT,
                                        &stub->args.symlink_cbk.buf);
out:
        call_stub_destroy (stub);
        return op_ret;
}

int
glusterfs_symlink (glusterfs_handle_t handle, const char *oldpath,
                  const char *newpath)
{
        int32_t                         op_ret = -1;
        libglusterfs_client_ctx_t       *ctx = handle;
        loc_t                           oldloc = {0, };
        loc_t                           newloc = {0, };
        char                            *oldname = NULL;
        char                            *newname = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, newpath, out);

        oldloc.path = strdup (oldpath);
        op_ret = libgf_client_path_lookup (&oldloc, ctx, 1);
        if (op_ret == -1) {
                errno = ENOENT;
                goto out;
        }

        oldname = strdup (oldpath);
        op_ret = libgf_client_loc_fill (&oldloc, ctx, 0, oldloc.parent->ino,
                                                basename (oldname));
        if (op_ret == -1) {
                errno = EINVAL;
                goto out;
        }

	newloc.path = strdup (newpath);
	op_ret = libgf_client_path_lookup (&newloc, ctx, 1);
	if (op_ret == 0) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "new path (%s) already exists, "
                                " returning EEXIST", newpath);
                op_ret = -1;
                errno = EEXIST;
                goto out;
        }

        op_ret = libgf_client_path_lookup (&newloc, ctx, 0);
        if (op_ret == -1) {
                errno = ENOENT;
                goto out;
        }

        newloc.inode = inode_new (ctx->itable);
        newname = strdup (newpath);
        op_ret = libgf_client_loc_fill (&newloc, ctx, 0, newloc.parent->ino,
                                                basename (newname));

        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "libgf_client_loc_fill returned -1, "
                                "returning EINVAL");
                errno = EINVAL;
                goto out;
        }

        op_ret = libgf_client_symlink (ctx, oldpath, &newloc);

out:
        if (newname)
                FREE (newname);

        if (oldname)
                FREE (oldname);
        libgf_client_loc_wipe (&oldloc);
        libgf_client_loc_wipe (&newloc);
        return op_ret;
}

int32_t
libgf_client_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                const char *path)
{
        libgf_client_local_t    *local = frame->local;

        local->reply_stub = fop_readlink_cbk_stub (frame, NULL, op_ret,
                                                   op_errno, path);

        LIBGF_REPLY_NOTIFY (local);
        return 0;
}

int32_t
libgf_client_readlink (libglusterfs_client_ctx_t *ctx, loc_t *loc, char *buf,
                       size_t bufsize)
{
        int                             op_ret = -1;
        libgf_client_local_t            *local = NULL;
        call_stub_t                     *stub = NULL;
        size_t                           cpy_size = 0;

        LIBGF_CLIENT_FOP (ctx, stub, readlink, local, loc, bufsize);

        op_ret = stub->args.readlink_cbk.op_ret;
        errno = stub->args.readlink_cbk.op_errno;

        if (op_ret != -1) {
                cpy_size = ((op_ret <= bufsize) ? op_ret : bufsize);
                memcpy (buf, stub->args.readlink_cbk.buf, cpy_size);
                op_ret = cpy_size;
        }

        call_stub_destroy (stub);
        return op_ret;
}

ssize_t
glusterfs_readlink (glusterfs_handle_t handle, const char *path, char *buf,
                    size_t bufsize)
{
        int32_t                         op_ret = -1;
        loc_t                           loc = {0, };
        libglusterfs_client_ctx_t       *ctx = handle;
        char                            *name = NULL;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

        loc.path = strdup (path);
        op_ret = libgf_client_path_lookup (&loc, ctx, 1);
        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "path lookup failed for (%s)", path);
                goto out;
        }

        name = strdup (path);
        op_ret = libgf_client_loc_fill (&loc, ctx, 0, loc.parent->ino,
                                                basename (name));
        if (op_ret == -1) {
                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "libgf_client_loc_fill returned -1, "
                                "returning EINVAL");
                errno = EINVAL;
                goto out;
        }

        op_ret = libgf_client_readlink (ctx, &loc, buf, bufsize);

out:
        if (name)
                FREE (name);

        libgf_client_loc_wipe (&loc);
        return op_ret;
}

char *
glusterfs_realpath (glusterfs_handle_t handle, const char *path,
                    char *resolved_path)
{
        char                            *buf = NULL, *extra_buf = NULL;
        char                            *rpath = NULL;
        char                            *start = NULL, *end = NULL;
        char                            *dest = NULL;
        libglusterfs_client_ctx_t       *ctx = handle;
        long int                        path_max = 0;
        char                            *ptr = NULL;
        struct stat                     stbuf = {0, };
        long int                        new_size = 0;
        char                            *new_rpath = NULL;
        int                             dest_offset = 0;
        char                            *rpath_limit = 0;
        int                             ret = 0, num_links = 0;
        size_t                          len = 0;

        GF_VALIDATE_OR_GOTO (LIBGF_XL_NAME, ctx, out);
        GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO (LIBGF_XL_NAME, path, out);

#ifdef PATH_MAX
        path_max = PATH_MAX;
#else
        path_max = pathconf (path, _PC_PATH_MAX);
        if (path_max <= 0) {
                path_max = 1024;
        }
#endif

        if (resolved_path == NULL) {
                rpath = CALLOC (1, path_max);
                if (rpath == NULL) {
                        errno = ENOMEM;
                        goto out;
                }
        } else {
                rpath = resolved_path;
        }

        rpath_limit = rpath + path_max;

        if (path[0] == '/') {
                rpath[0] = '/';
                dest = rpath + 1;
        } else {
                /*
                   FIXME: can $CWD be a valid path on glusterfs server? hence is
                   it better to handle this case or just return EINVAL for
                   relative paths?
                */
                ptr = getcwd (rpath, path_max);
                if (ptr == NULL) {
                        goto err;
                }
                dest = rpath + strlen (rpath);
        }

        for (start = end = (char *)path; *end; start = end) {
                if (dest[-1] != '/') {
                        *dest++ = '/';
                }

                while (*start == '/') {
                        start++;
                }

                for (end = start; *end && *end != '/'; end++);

                if ((end - start) == 0) {
                        break;
                }

                if ((end - start == 1) && (start[0] == '.')) {
                        /* do nothing */
                } else if (((end - start) == 2) && (start[0] == '.')
                           && (start[1] == '.')) {
                        if (dest > rpath + 1) {
                                while (--dest[-1] != '/');
                        }
                } else {
                        if ((dest + (end - start + 1)) >= rpath_limit) {
                                if (resolved_path == NULL) {
                                        errno = ENAMETOOLONG;
                                        if (dest > rpath + 1)
                                                dest--;
                                        *dest = '\0';
                                        goto err;
                                }

                                dest_offset = dest - rpath;
                                new_size = rpath_limit - rpath;
                                if ((end - start + 1) > path_max) {
                                        new_size = (end - start + 1);
                                } else {
                                        new_size = path_max;
                                }

                                new_rpath = realloc (rpath, new_size);
                                if (new_rpath == NULL) {
                                        goto err;
                                }


                                dest = new_rpath + dest_offset;
                                rpath = new_rpath;
                                rpath_limit = rpath + new_size;
                        }

                        memcpy (dest, start, end - start);
                        dest +=  end - start;
                        *dest = '\0';

                        /* posix_stat is implemented using lstat */
                        ret = glusterfs_glh_stat (handle, rpath, &stbuf);
                        if (ret == -1) {
                                gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                        "glusterfs_glh_stat returned -1 for"
                                        " path (%s):(%s)", rpath,
                                        strerror (errno));
                                goto err;
                        }

                        if (S_ISLNK (stbuf.st_mode)) {
                                buf = alloca (path_max);

                                if (++num_links > MAXSYMLINKS)
                                {
                                        errno = ELOOP;
                                        goto err;
                                }

                                ret = glusterfs_readlink (handle, rpath, buf,
                                                          path_max - 1);
                                if (ret < 0) {
                                        gf_log ("libglusterfsclient",
                                                GF_LOG_ERROR,
                                                "glusterfs_readlink returned %d"
                                                " for path (%s):(%s)",
                                                ret, rpath, strerror (errno));
                                        goto err;
                                }
                                buf[ret] = '\0';

                                if (extra_buf == NULL)
                                        extra_buf = alloca (path_max);

                                len = strlen (end);
                                if ((long int) (ret + len) >= path_max)
                                {
                                        errno = ENAMETOOLONG;
                                        goto err;
                                }

                                memmove (&extra_buf[ret], end, len + 1);
                                path = end = memcpy (extra_buf, buf, ret);

                                if (buf[0] == '/')
                                        dest = rpath + 1;
                                else
                                        if (dest > rpath + 1)
                                                while ((--dest)[-1] != '/');
                        } else if (!S_ISDIR (stbuf.st_mode) && *end != '\0') {
                                errno = ENOTDIR;
                                goto err;
                        }
                }
        }
        if (dest > rpath + 1 && dest[-1] == '/')
                --dest;
        *dest = '\0';

out:
        return rpath;

err:
        if (resolved_path == NULL) {
                FREE (rpath);
        }

        return NULL;
}

static struct xlator_fops libgf_client_fops = {
};

static struct xlator_mops libgf_client_mops = {
};

static struct xlator_cbks libgf_client_cbks = {
        .forget      = libgf_client_forget,
	.release     = libgf_client_release,
	.releasedir  = libgf_client_releasedir,
};

static inline xlator_t *
libglusterfs_graph (xlator_t *graph)
{
        xlator_t *top = NULL;
        xlator_list_t *xlchild, *xlparent;

        top = CALLOC (1, sizeof (*top));
        ERR_ABORT (top);

        xlchild = CALLOC (1, sizeof(*xlchild));
        ERR_ABORT (xlchild);
        xlchild->xlator = graph;
        top->children = xlchild;
        top->ctx = graph->ctx;
        top->next = graph;
        top->name = strdup (LIBGF_XL_NAME);

        xlparent = CALLOC (1, sizeof(*xlparent));
        xlparent->xlator = top;
        graph->parents = xlparent;
        asprintf (&top->type, LIBGF_XL_NAME);

        top->init = libgf_client_init;
        top->fops = &libgf_client_fops;
        top->mops = &libgf_client_mops;
        top->cbks = &libgf_client_cbks; 
        top->notify = libgf_client_notify;
        top->fini = libgf_client_fini;
        //  fill_defaults (top);

        return top;
}
