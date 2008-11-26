/*
  Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#define XLATOR_NAME "libglusterfsclient"
#define LIBGLUSTERFS_INODE_TABLE_LRU_LIMIT 14057

typedef struct {
        pthread_cond_t init_con_established;
        pthread_mutex_t lock;
        char complete;
}libglusterfs_client_private_t;

typedef struct {
        pthread_mutex_t lock;
        uint32_t previous_lookup_time;
        uint32_t previous_stat_time;
        struct stat stbuf;
} libglusterfs_client_inode_ctx_t;

typedef struct {
        pthread_mutex_t lock;
        off_t offset;
        libglusterfs_client_ctx_t *ctx;
} libglusterfs_client_fd_ctx_t;

typedef struct libglusterfs_client_async_local {
        void *cbk_data;
        union {
                struct {
                        fd_t *fd;
                        glusterfs_readv_cbk_t cbk;
                }readv_cbk;
    
                struct {
                        fd_t *fd;
                        glusterfs_writev_cbk_t cbk;
                }writev_cbk;

                struct {
                        fd_t *fd;
                }close_cbk;

                struct {
                        void *buf;
                        size_t size;
                        loc_t *loc;
                        char is_revalidate;
                        glusterfs_lookup_cbk_t cbk;
                }lookup_cbk;
        }fop;
}libglusterfs_client_async_local_t;

/* static pthread_mutex_t libgf_client_lock; */
    
static inline xlator_t *
libglusterfs_graph (xlator_t *graph);

/*
void 
glusterfs_lock (void)
{
	pthread_mutex_lock (&libgf_client_lock);
}

void 
glusterfs_unlock (void)
{
	pthread_mutex_unlock (&libgf_client_lock);
}
*/

static int first_init = 1;
static int first_fini = 1;

int32_t
libgf_client_forget (xlator_t *this,
		     inode_t *inode)
{
        return 0;
}


int32_t
libgf_client_release (xlator_t *this,
		      fd_t *fd)
{
	libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
	data_t *fd_ctx_data = NULL;

        fd_ctx_data = dict_get (fd->ctx, XLATOR_NAME);

        fd_ctx = data_to_ptr (fd_ctx_data);
	pthread_mutex_destroy (&fd_ctx->lock);

	return 0;
}


int32_t
libgf_client_releasedir (xlator_t *this,
			 fd_t *fd)
{
	libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
	data_t *fd_ctx_data = NULL;

        fd_ctx_data = dict_get (fd->ctx, XLATOR_NAME);

        fd_ctx = data_to_ptr (fd_ctx_data);
	pthread_mutex_destroy (&fd_ctx->lock);

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


static void 
libgf_client_loc_wipe (loc_t *loc)
{
        FREE (loc->path);
        inode_unref (loc->parent);
	inode_unref (loc->inode);
        /*TODO: add inode_unref (loc->inode) in a generic way to satisfy all the procedures using loc_fill */
}


static void
libgf_client_loc_fill (loc_t *loc, const char *path, libglusterfs_client_ctx_t *ctx)
{
        loc->path = strdup (path);
        loc->inode = inode_search (ctx->itable, 1, loc->path);
        loc->parent = inode_ref (ctx->itable->root);

        if (path) {
                loc->name = strrchr (loc->path, '/');
                if (loc->name)
                        loc->name++;
        }

        if (loc->inode) {
                loc->ino = loc->inode->ino;
        }
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
  
        if (d) {
                frame->root->req_refs = dict_ref (get_new_dict ());
                /*
                  TODO
                  dict_set (frame->root->req_refs, NULL, priv->buf);
                */
        }

        return frame;
}

void 
libgf_client_fini (xlator_t *this)
{
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


libglusterfs_handle_t 
glusterfs_init (glusterfs_init_ctx_t *init_ctx)
{
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_private_t *priv = NULL;
        FILE *specfp = NULL;
        xlator_t *graph = NULL, *trav = NULL;
        call_pool_t *pool = NULL;
        int32_t ret = 0;
        struct rlimit lim;

        if (!init_ctx || (!init_ctx->specfile && !init_ctx->specfp)) {
                errno = EINVAL;
                return NULL;
        }

        ctx = calloc (1, sizeof (*ctx));
        if (!ctx) {
                fprintf (stderr, "libglusterfs-client: out of memory, gf_init failed\n");
                errno = ENOMEM;
                return NULL;
        }

        ctx->lookup_timeout = init_ctx->lookup_timeout;
        ctx->stat_timeout = init_ctx->stat_timeout;

        pthread_mutex_init (&ctx->gf_ctx.lock, NULL);
  
        pool = ctx->gf_ctx.pool = calloc (1, sizeof (call_pool_t));
        if (!pool) {
                errno = ENOMEM;
                FREE (ctx);
                return NULL;
        }

        LOCK_INIT (&pool->lock);
        INIT_LIST_HEAD (&pool->all_frames);

        ctx->gf_ctx.event_pool = event_pool_new (16384);

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
                if (!strncasecmp (init_ctx->loglevel, "DEBUG", strlen ("DEBUG"))) {
                        ctx->gf_ctx.cmd_args.log_level = GF_LOG_DEBUG;
                } else if (!strncasecmp (init_ctx->loglevel, "WARNING", strlen ("WARNING"))) {
                        ctx->gf_ctx.cmd_args.log_level = GF_LOG_WARNING;
                } else if (!strncasecmp (init_ctx->loglevel, "CRITICAL", strlen ("CRITICAL"))) {
                        ctx->gf_ctx.cmd_args.log_level = GF_LOG_CRITICAL;
                } else if (!strncasecmp (init_ctx->loglevel, "NONE", strlen ("NONE"))) {
                        ctx->gf_ctx.cmd_args.log_level = GF_LOG_NONE;
                } else if (!strncasecmp (init_ctx->loglevel, "ERROR", strlen ("ERROR"))) {
                        ctx->gf_ctx.cmd_args.log_level = GF_LOG_ERROR;
                } else {
                        fprintf (stderr, "glusterfs: Unrecognized log-level \"%s\", possible values are \"DEBUG|WARNING|[ERROR]|CRITICAL|NONE\"\n", init_ctx->loglevel);
                        FREE (ctx->gf_ctx.pool);
                        FREE (ctx->gf_ctx.event_pool);
                        FREE (ctx);
                        errno = EINVAL;
                        return NULL;
                }
        }

/*        pthread_mutex_lock (&libgf_client_lock); */
	if (first_init)
        {
                ret = gf_log_init (ctx->gf_ctx.cmd_args.log_file);
                if (ret == -1) {
                        fprintf (stderr,
                                 "glusterfs: failed to open logfile \"%s\"\n",
                                 ctx->gf_ctx.cmd_args.log_file);
                        FREE (ctx->gf_ctx.pool);
                        FREE (ctx->gf_ctx.event_pool);
                        FREE (ctx);
/*                        pthread_mutex_unlock (&libgf_client_lock);*/
                        return NULL;
                }

                gf_log_set_loglevel (ctx->gf_ctx.cmd_args.log_level);
        }
/*        pthread_mutex_unlock (&libgf_client_lock); */

        /*  ctx->gf_ctx.specfile = strdup (specfile); */

        if (init_ctx->specfp) {
                specfp = init_ctx->specfp;
                if (fseek (specfp, 0L, SEEK_SET)) {
                        fprintf (stderr,
                                 "glusterfs: fseek on volume specification file stream failed (%s)", 
                                 strerror (errno));
                        FREE (ctx->gf_ctx.pool);
                        FREE (ctx->gf_ctx.event_pool);
                        FREE (ctx);
                        return NULL;
                }
        } else if (init_ctx->specfile) { 
                specfp = fopen (init_ctx->specfile, "r");
                ctx->gf_ctx.cmd_args.volume_specfile = strdup (init_ctx->specfile);
        }

        if (!specfp) {
                fprintf (stderr,
                         "glusterfs: could not open specfile: %s\n", strerror (errno));
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
                         "glusterfs: cannot create configuration graph (%s)\n", strerror (errno));
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

        graph = libglusterfs_graph (graph);
        if (!graph) {
                fprintf (stderr,
                         "glusterfs: graph creation failed (%s)\n", strerror (errno));
                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                FREE (ctx);
                return NULL;
        }
        ctx->gf_ctx.graph = graph;

        priv = calloc (1, sizeof (*priv));
        if (!priv) {
                fprintf (stderr,
                         "glusterfs: cannot allocate memory (%s)\n", strerror (errno));
                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                /* TODO: destroy graph */
                /* inode_table_destroy (ctx->itable); */
                FREE (ctx);
         
                return NULL;
        }

        pthread_cond_init (&priv->init_con_established, NULL);
        pthread_mutex_init (&priv->lock, NULL);

        graph->private = priv;
        ctx->itable = inode_table_new (LIBGLUSTERFS_INODE_TABLE_LRU_LIMIT, graph);
        if (!ctx->itable) {
                fprintf (stderr,
                         "glusterfs: cannot create configuration graph (%s)\n", strerror (errno));
                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                FREE (graph->private);
                /* TODO: destroy graph */
                /* inode_table_destroy (ctx->itable); */
                FREE (ctx);
         
                return NULL;
        }

        if (xlator_graph_init (graph) == -1) {
                fprintf (stderr, "glusterfs: configuration graph initialization failed (%s)\n", strerror (errno));
                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                FREE (graph->private);
                /* TODO: destroy graph */
                /* inode_table_destroy (ctx->itable); */
                FREE (ctx);
                return NULL;
        }

	/* Send notify to all translator saying things are ready */
	graph->notify (graph, GF_EVENT_PARENT_UP, graph);

        if (gf_timer_registry_init (&ctx->gf_ctx) == NULL) {
                fprintf (stderr, "glusterfs: timer init failed (%s)\n", strerror (errno));
                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                FREE (graph->private);
                /* TODO: destroy graph */
                /* inode_table_destroy (ctx->itable); */
                FREE (ctx);
                return NULL;
        }

        if ((ret = pthread_create (&ctx->reply_thread, NULL, poll_proc, (void *)&ctx->gf_ctx))) {
                fprintf (stderr, "glusterfs: reply thread creation failed (%s)\n", strerror (errno));
                FREE (ctx->gf_ctx.pool);
                FREE (ctx->gf_ctx.event_pool);
                FREE (graph->private);
                /* TODO: destroy graph */
                /* inode_table_destroy (ctx->itable); */
                FREE (ctx);
                return NULL;
        }

        pthread_mutex_lock (&priv->lock); 
        {
                while (!priv->complete) {
                        pthread_cond_wait (&priv->init_con_established, &priv->lock);
                }
        }
        pthread_mutex_unlock (&priv->lock);

	first_init = 0;
 
        return ctx;
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
glusterfs_fini (libglusterfs_client_ctx_t *ctx)
{
        FREE (ctx->gf_ctx.cmd_args.log_file);
        FREE (ctx->gf_ctx.pool);
        FREE (ctx->gf_ctx.event_pool);
        ((gf_timer_registry_t *)ctx->gf_ctx.timer)->fin = 1;
        /* TODO: destroy graph */
        /* inode_table_destroy (ctx->itable); */
        FREE (ctx);

	if (first_fini) {
		;
		//gf_log_cleanup ();
	}

        /* FREE (ctx->gf_ctx.specfile); */

        /* TODO complete cleanup of timer */
        /*TODO 
         * destroy the reply thread 
         * destroy inode table
         * destroy the graph
         * FREE (ctx) 
         */

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

        if (op_ret == 0) {
                /* flat directory structure */
                inode_t *parent = inode_search (ctx->itable, 1, NULL);

                inode_link (inode, parent, local->fop.lookup.loc->path, buf);
		inode_lookup (inode);
		inode_unref (parent);
        } else {
                if (local->fop.lookup.is_revalidate == 0 && op_errno == ENOENT) {
                        gf_log ("libglusterfsclient", GF_LOG_DEBUG,
                                "%"PRId64": (op_num=%d) %s => -1 (%s)", frame->root->unique,
                                frame->op, local->fop.lookup.loc->path, strerror(op_errno));
                } else {
                        gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "%"PRId64": (op_num=%d) %s => -1 (%s)", frame->root->unique,
                                frame->op, local->fop.lookup.loc->path, strerror(op_errno));
                }

                if (local->fop.lookup.is_revalidate == 1) {
                        inode_unref (local->fop.lookup.loc->inode);
                        local->fop.lookup.loc->inode = inode_new (ctx->itable);
                        local->fop.lookup.is_revalidate = 2;

                        STACK_WIND (frame, libgf_client_lookup_cbk,
                                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->lookup,
                                    local->fop.lookup.loc, local->fop.lookup.size);

                        return 0;
                }
        }

        local->reply_stub = fop_lookup_cbk_stub (frame, NULL, op_ret, op_errno, inode, buf, dict);

        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);

        return 0;
}

int32_t
libgf_client_lookup (libglusterfs_client_ctx_t *ctx,
                     loc_t *loc,
                     struct stat *stbuf,
                     dict_t **dict,
                     int32_t need_xattr)
{
        call_stub_t  *stub = NULL;
        int32_t op_ret;
        libgf_client_local_t *local = NULL;
        /* Directory structure is flat. i.e., all the files and directories are immediate children of root directory */

        local = calloc (1, sizeof (*local));
        if (loc->inode) {
                local->fop.lookup.is_revalidate = 1;
                loc->ino = loc->inode->ino;
        }
        else
                loc->inode = inode_new (ctx->itable);

        local->fop.lookup.loc = loc;

        LIBGF_CLIENT_FOP(ctx, stub, lookup, local, loc, need_xattr);

        op_ret = stub->args.lookup_cbk.op_ret;
        errno = stub->args.lookup_cbk.op_errno;

        if (!op_ret) {
                time_t current = 0;
		data_t *inode_ctx_data = NULL;
                libglusterfs_client_inode_ctx_t *inode_ctx = NULL;
		inode_t *inode = stub->args.lookup_cbk.inode;

		inode_ctx_data = dict_get (inode->ctx, XLATOR_NAME);
		if (!inode_ctx_data) {
			inode_ctx = calloc (1, sizeof (*inode_ctx));
			ERR_ABORT (inode_ctx);
			pthread_mutex_init (&inode_ctx->lock, NULL);
		} else {
			inode_ctx = data_to_ptr (inode_ctx_data);
		}

                current = time (NULL);

		pthread_mutex_lock (&inode_ctx->lock); 
		{
			inode_ctx->previous_lookup_time = current;
			inode_ctx->previous_stat_time = current;
			memcpy (&inode_ctx->stbuf, &stub->args.lookup_cbk.buf, 
				sizeof (inode_ctx->stbuf));
		}
		pthread_mutex_unlock (&inode_ctx->lock);

		if (!inode_ctx_data) {
			dict_set (inode->ctx, XLATOR_NAME, 
				  data_from_dynptr (inode_ctx, sizeof (*inode_ctx)));
		}

                if (stbuf)
                        *stbuf = stub->args.lookup_cbk.buf; 

                if (dict)
                        *dict = dict_ref (stub->args.lookup_cbk.dict);
        }

	call_stub_destroy (stub);
        return op_ret;
}

/* TODO: check inode_ref/inode_unref  */

int 
glusterfs_lookup (libglusterfs_handle_t handle, 
                  const char *path, 
                  void *buf, 
                  size_t size, 
                  struct stat *stbuf)
{
        int32_t op_ret = 0;
        loc_t loc = {0, };
        libglusterfs_client_ctx_t *ctx = handle;
        dict_t *dict = NULL;

        libgf_client_loc_fill (&loc, path, ctx);

        if (size < 0)
                size = 0;

        op_ret = libgf_client_lookup (ctx, &loc, stbuf, &dict, (int32_t)size);

        if (!op_ret && size && stbuf && stbuf->st_size && dict && buf) {
                data_t *mem_data = NULL;
                void *mem = NULL;

                mem_data = dict_get (dict, "glusterfs.content");
                if (mem_data) {
                        mem = data_to_ptr (mem_data);
                }

                if (mem && stbuf->st_size <= size) {
                        memcpy (buf, mem, stbuf->st_size);
                }
        }

        if (dict) {
                dict_unref (dict);
        }

        libgf_client_loc_wipe (&loc);
        return op_ret;
}

int
libgf_client_lookup_async_cbk (call_frame_t *frame,
                               void *cookie,
                               xlator_t *this,
                               int32_t op_ret,
                               int32_t op_errno,
                               inode_t *inode,
                               struct stat *buf,
                               dict_t *dict)
{
        libglusterfs_client_async_local_t *local = frame->local;
        glusterfs_lookup_cbk_t lookup_cbk = local->fop.lookup_cbk.cbk;
        libglusterfs_client_ctx_t *ctx = frame->root->state;

        if (op_ret == 0) {
                inode_t *libgf_inode = NULL;
                time_t current = 0;
                data_t *inode_ctx_data = NULL;
                libglusterfs_client_inode_ctx_t *inode_ctx = NULL;

                /* flat directory structure */
                inode_t *parent = inode_search (ctx->itable, 1, NULL);

                inode_link (inode, parent, local->fop.lookup_cbk.loc->path, buf);
                libgf_inode = inode_ref (inode);

    
                inode_ctx_data = dict_get (libgf_inode->ctx, XLATOR_NAME);
                if (inode_ctx_data) {
                        inode_ctx = data_to_ptr (inode_ctx_data);
                }

                if (!inode_ctx) {
                        inode_ctx = calloc (1, sizeof (*inode_ctx));
                        pthread_mutex_init (&inode_ctx->lock, NULL);
                }

                current = time (NULL);

                pthread_mutex_lock (&inode_ctx->lock);
                {
                        inode_ctx->previous_lookup_time = current;
                        inode_ctx->previous_stat_time = current;
                        memcpy (&inode_ctx->stbuf, buf, sizeof (inode_ctx->stbuf));
                }
                pthread_mutex_unlock (&inode_ctx->lock);

		if (!inode_ctx_data) {
                        dict_set (libgf_inode->ctx, XLATOR_NAME, 
				  data_from_dynptr (inode_ctx, sizeof (*inode_ctx)));
		}


                inode_lookup (libgf_inode);
                inode_unref (libgf_inode);
                inode_unref (parent);

                /* loc->inode = libgf_inode; */
        } else {
                if (local->fop.lookup_cbk.is_revalidate == 0 && op_errno == ENOENT) {
                        gf_log ("libglusterfsclient", GF_LOG_DEBUG,
                                "%"PRId64": (op_num=%d) %s => -1 (%s)", frame->root->unique,
                                frame->op, local->fop.lookup_cbk.loc->path, strerror(op_errno));
                } else {
                        gf_log ("libglusterfsclient", GF_LOG_ERROR,
                                "%"PRId64": (op_num=%d) %s => -1 (%s)", frame->root->unique,
                                frame->op, local->fop.lookup_cbk.loc->path, strerror(op_errno));
                }

                if (local->fop.lookup_cbk.is_revalidate == 1) {
                        inode_unref (local->fop.lookup_cbk.loc->inode);
                        local->fop.lookup_cbk.loc->inode = inode_new (ctx->itable);
                        local->fop.lookup_cbk.is_revalidate = 2;

                        STACK_WIND (frame, libgf_client_lookup_async_cbk,
                                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->lookup,
                                    local->fop.lookup_cbk.loc, local->fop.lookup_cbk.size);

                        return 0;
                }

        }

        if (!op_ret && local->fop.lookup_cbk.size && dict && local->fop.lookup_cbk.buf) {
                data_t *mem_data = NULL;
                void *mem = NULL;

                mem_data = dict_get (dict, "glusterfs.content");
                if (mem_data) {
                        mem = data_to_ptr (mem_data);
                }

                if (mem && buf->st_size <= local->fop.lookup_cbk.size) {
                        memcpy (local->fop.lookup_cbk.buf, mem, buf->st_size);
                }
        }

        lookup_cbk(op_ret, op_errno, local->fop.lookup_cbk.buf, buf, local->cbk_data);

        inode_unref (local->fop.lookup_cbk.loc->inode);
        free ((void *)local->fop.lookup_cbk.loc->path);
        free (local->fop.lookup_cbk.loc);

        free (local);
        frame->local = NULL;
        STACK_DESTROY (frame->root);

        return 0;
}

int
glusterfs_lookup_async (libglusterfs_handle_t handle, 
                        const char *path,
                        void *buf,
                        size_t size, 
                        glusterfs_lookup_cbk_t cbk,
                        void *cbk_data)
{
        loc_t *loc;
        libglusterfs_client_ctx_t *ctx = handle;
        libglusterfs_client_async_local_t *local = NULL;

        local = calloc (1, sizeof (*local));
        local->fop.lookup_cbk.is_revalidate = 1;

        loc = calloc (1, sizeof (*loc));
        loc->path = strdup (path);
        loc->inode = inode_search (ctx->itable, 1, path);

        if (!loc->inode) {
                loc->inode = inode_new (ctx->itable);
                local->fop.lookup_cbk.is_revalidate = 0;
        } else {
                loc->ino = loc->inode->ino;
        }

        local->fop.lookup_cbk.cbk = cbk;
        local->fop.lookup_cbk.buf = buf;
        local->fop.lookup_cbk.size = size;
        local->fop.lookup_cbk.loc = loc;
        local->cbk_data = cbk_data;

        if (size < 0)
                size = 0;

        LIBGF_CLIENT_FOP_ASYNC (ctx,
                                local,
                                libgf_client_lookup_async_cbk,
                                lookup,
                                loc,
                                size);

        return 0;
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

        local->reply_stub = fop_getxattr_cbk_stub (frame, NULL, op_ret, op_errno, dict);

        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);

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

                data_t *value_data = dict_get (stub->args.getxattr_cbk.dict, (char *)name);
    
                if (value_data) {
                        int32_t copy_len = 0;
                        op_ret = value_data->len; /* Don't return the value for '\0' */

                        copy_len = size < value_data->len ? size : value_data->len;
                        /*FIXME: where is this freed? */
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
glusterfs_getxattr (libglusterfs_client_ctx_t *ctx, 
                    const char *path, 
                    const char *name,
                    void *value, 
                    size_t size)
{
        int32_t op_ret = 0;
        char lookup_required = 1;
        loc_t loc = {0, };
        /*   list_head_t signal_handlers; */

        libgf_client_loc_fill (&loc, path, ctx);

        if (loc.inode && loc.inode->ctx) {
                data_t *inode_ctx_data = NULL;
                libglusterfs_client_inode_ctx_t *inode_ctx = NULL;
                time_t current, prev;

                inode_ctx_data = dict_get (loc.inode->ctx, XLATOR_NAME);
                if (inode_ctx_data) {
                        inode_ctx = data_to_ptr (inode_ctx_data);

                        if (inode_ctx) {
                                memset (&current, 0, sizeof (current));
                                pthread_mutex_lock (&inode_ctx->lock);
                                {
                                        prev = inode_ctx->previous_lookup_time;
                                }
                                pthread_mutex_unlock (&inode_ctx->lock);

                                current = time (NULL);
                                if (prev >= 0 && ctx->lookup_timeout >= (current - prev)) {
                                        lookup_required = 0;
                                } 
                        }
                }
        }

        if (lookup_required) {
                /*TODO: use need_lookup to fetch xattrs also */
                op_ret = libgf_client_lookup (ctx, &loc, NULL, NULL, 0);
        }

        if (!op_ret)
                op_ret = libgf_client_getxattr (ctx, &loc, name, value, size);

        libgf_client_loc_wipe (&loc);
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

        local->reply_stub = fop_open_cbk_stub (frame, NULL, op_ret, op_errno, fd);

        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);

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

        local->reply_stub = fop_create_cbk_stub (frame, NULL, op_ret, op_errno, fd, inode, buf);

        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);

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

        LIBGF_CLIENT_FOP (ctx, stub, create, local, loc, flags, mode, fd);
  
        if (stub->args.create_cbk.op_ret == 0) {
                inode_t *libgf_inode = NULL;
    
                /* flat directory structure */
                inode_t *parent = inode_search (ctx->itable, 1, NULL);
                inode_link (stub->args.create_cbk.inode, parent,
                            loc->path, &stub->args.create_cbk.buf);

                libgf_inode = inode_ref (stub->args.create_cbk.inode);

                inode_lookup (libgf_inode);

                inode_unref (loc->inode);
                loc->inode = libgf_inode;

                /*
                  if (stbuf)
                  *stbuf = stub->args.lookup_cbk.buf; 
                  */
                inode_unref (parent);
        }

        op_ret = stub->args.create_cbk.op_ret;
        errno = stub->args.create_cbk.op_errno;

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

        local->reply_stub = fop_opendir_cbk_stub (frame, NULL, op_ret, op_errno, fd);

        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);

        return 0;
}

int 
libgf_client_opendir (libglusterfs_client_ctx_t *ctx,
                      loc_t *loc,
                      fd_t *fd)
{
        call_stub_t *stub = NULL;
        int32_t op_ret = 0;
        libgf_client_local_t *local = NULL;

        LIBGF_CLIENT_FOP (ctx, stub, opendir, local, loc, fd);

        op_ret = stub->args.opendir_cbk.op_ret;
        errno = stub->args.opendir_cbk.op_errno;

	call_stub_destroy (stub);
        return 0;
}

unsigned long 
glusterfs_open (libglusterfs_client_ctx_t *ctx, 
                const char *path, 
                int flags, 
                mode_t mode)
{
        loc_t loc = {0, };
        long op_ret = 0;
        fd_t *fd = NULL;
        struct stat stbuf; 
	char lookup_required = 1;
  
        if (!ctx || !path) {
                errno = EINVAL;
                return 0;
        }

        libgf_client_loc_fill (&loc, path, ctx);

        if (!(flags & O_APPEND) && loc.inode && loc.inode->ctx) {
                data_t *inode_ctx_data = NULL;
                libglusterfs_client_inode_ctx_t *inode_ctx = NULL;
                time_t current, prev;
 
                inode_ctx_data = dict_get (loc.inode->ctx, XLATOR_NAME);
                if (inode_ctx_data) {
                        inode_ctx = data_to_ptr (inode_ctx_data);

                        if (inode_ctx) {
                                memset (&current, 0, sizeof (current));

                                pthread_mutex_lock (&inode_ctx->lock);
                                {
                                        prev = inode_ctx->previous_lookup_time;
                                }
                                pthread_mutex_unlock (&inode_ctx->lock);

                                current = time (NULL);
                                if (prev >= 0 && ctx->lookup_timeout >= (current - prev)) {
                                        lookup_required = 0;
                                } 
                        }
                }
        }

        if (lookup_required) {
                op_ret = libgf_client_lookup (ctx, &loc, &stbuf, NULL, 0);
                if (!op_ret && ((flags & O_CREAT) == O_CREAT) && ((flags & O_EXCL) == O_EXCL)) {
                        errno = EEXIST;
                        op_ret = -1;
                }
        }

        /*
          if (op_ret == -1)
          lookup_required = 0;
        */

        if (!op_ret || (op_ret == -1 && errno == ENOENT && ((flags & O_CREAT) == O_CREAT))) {
                fd = fd_create (loc.inode, 0);
                fd->flags = flags;

                if (!op_ret) {
                        if (S_ISDIR (loc.inode->st_mode)) {
                                if ((flags & O_RDONLY) == O_RDONLY)
                                        op_ret = libgf_client_opendir (ctx, &loc, fd);
                                else {
                                        op_ret = -1;
                                        errno = EEXIST;
                                }
                        }
                        else
                                op_ret = libgf_client_open (ctx, &loc, fd, flags);
                }
                else {
                        /*  loc.inode = inode_new (ctx->itable); */
                        op_ret = libgf_client_creat (ctx, &loc, fd, flags, mode);
                        /*  inode_unref (loc.inode); */
                }

                if (op_ret == -1) {
                        fd_unref (fd);
                        fd = NULL;
                } else {
                        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
			data_t *fd_ctx_data = NULL;
                        off_t offset = 0;
      
                        if ((flags & O_APPEND) == O_APPEND)
                                offset = stbuf.st_size;
      
			fd_ctx_data = dict_get (fd->ctx, XLATOR_NAME);
			if (!fd_ctx_data) {
				fd_ctx = calloc (1, sizeof (*fd_ctx));
				ERR_ABORT (fd_ctx);
				pthread_mutex_init (&fd_ctx->lock, NULL);
			}

			pthread_mutex_lock (&fd_ctx->lock);
			{
				fd_ctx->offset = offset;
				fd_ctx->ctx = ctx;
			}
			pthread_mutex_unlock (&fd_ctx->lock);

			if (!fd_ctx_data) {
				dict_set (fd->ctx, XLATOR_NAME, data_from_dynptr (fd_ctx, sizeof (*fd_ctx)));
			}
                }
        }

        libgf_client_loc_wipe (&loc);
        return (long)fd;
}

unsigned long 
glusterfs_creat (libglusterfs_client_ctx_t *ctx, 
                 const char *path, 
                 mode_t mode)
{
        loc_t loc = {0, };
        long op_ret = -1;
        fd_t *fd = NULL;

        if (!ctx || !path) {
                errno = EINVAL;
                return 0;
        }

        libgf_client_loc_fill (&loc, path, ctx);

        /*TODO: send create only if file does not exist, otherwise send open */
        /*  libgf_client_lookup (ctx, &loc, NULL); */

        fd = fd_create (loc.inode, 0);
        fd->flags = O_CREAT|O_WRONLY|O_TRUNC;

        op_ret = libgf_client_creat (ctx, &loc, fd, O_CREAT|O_WRONLY|O_TRUNC, mode);

        if (op_ret == -1) {
                fd_unref (fd);
                fd = NULL;
        } else {
                libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
		data_t *fd_ctx_data = NULL;
                off_t offset = 0;
      
                op_ret = (long) fd;
    
		fd_ctx_data = dict_get (fd->ctx, XLATOR_NAME);
		if (!fd_ctx_data) {
			fd_ctx = calloc (1, sizeof (*fd_ctx));
			ERR_ABORT (fd_ctx);
			pthread_mutex_init (&fd_ctx->lock, NULL);
		}

		pthread_mutex_lock (&fd_ctx->lock);
		{
			fd_ctx->offset = offset;
			fd_ctx->ctx = ctx;
		}
		pthread_mutex_unlock (&fd_ctx->lock);

		if (!fd_ctx_data) {
			dict_set (fd->ctx, XLATOR_NAME, data_from_dynptr (fd_ctx, sizeof (*fd_ctx)));
		}
        }

        libgf_client_loc_wipe (&loc);
        return (long)fd;
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
        
        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);
        
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
glusterfs_close (unsigned long fd)
{
        int32_t ret = -1;
        data_t *fd_ctx_data = NULL;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;

        if (!fd) {
                errno = EINVAL;
                return -1;
        }

        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);
        ctx = fd_ctx->ctx;

        ret = libgf_client_flush (ctx, (fd_t *)fd);

        fd_unref ((fd_t *)fd);

        return ret;
}

int32_t
libgf_client_setxattr_cbk (call_frame_t *frame,
                           void *cookie,
                           xlator_t *this,
                           int32_t op_ret,
                           int32_t op_errno)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_setxattr_cbk_stub (frame, NULL, op_ret, op_errno);

        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);

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
glusterfs_setxattr (libglusterfs_client_ctx_t *ctx, 
                    const char *path, 
                    const char *name,
                    const void *value, 
                    size_t size, 
                    int flags)
{
        int32_t op_ret = 0;
        loc_t loc = {0, };
        char lookup_required = 1;
        /*   list_head_t signal_handlers; */

        libgf_client_loc_fill (&loc, path, ctx);

        if (loc.inode && loc.inode->ctx) {
                time_t current, prev;
                libglusterfs_client_inode_ctx_t *inode_ctx = NULL;
                data_t *inode_ctx_data = NULL;

                inode_ctx_data = dict_get (loc.inode->ctx, XLATOR_NAME);
                if (!inode_ctx_data) 
                        return -1;

                inode_ctx = data_to_ptr (inode_ctx_data);

                memset (&current, 0, sizeof (current));
                current = time (NULL);

                pthread_mutex_lock (&inode_ctx->lock);
                {
                        prev = inode_ctx->previous_lookup_time;
                }
                pthread_mutex_unlock (&inode_ctx->lock);
    
                if ( (prev >= 0) && ctx->lookup_timeout >= (current - prev)) {
                        lookup_required = 0;
                } 
        }

        if (lookup_required) {
                op_ret = libgf_client_lookup (ctx, &loc, NULL, NULL, 0);
        }

        if (!op_ret)
                op_ret = libgf_client_setxattr (ctx, &loc, name, value, size, flags);

        libgf_client_loc_wipe (&loc);
        return op_ret;
}

int 
glusterfs_lsetxattr (libglusterfs_client_ctx_t *ctx, 
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

        local->reply_stub = fop_setxattr_cbk_stub (frame, NULL, op_ret, op_errno);

        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);

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
        /*
          call_stub_t  *stub = NULL;
          int32_t op_ret = 0;
          dict_t *dict;

          dict = get_new_dict ();

          dict_set (dict, (char *)name,
          bin_to_data ((void *)value, size));
          dict_ref (dict);

          LIBGF_CLIENT_FOP (ctx, stub, fsetxattr, fd, dict, flags);

          op_ret = stub->args.fsetxattr_cbk.op_ret;
          errno = stub->args.fsetxattr_cbk.op_errno;

          dict_unref (dict);
	  call_stub_destroy (stub);
          return op_ret;
        */
        return 0;
}

int 
glusterfs_fsetxattr (unsigned long fd, 
                     const char *name,
                     const void *value, 
                     size_t size, 
                     int flags)
{
        fd_t *__fd = (fd_t *)fd;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;
        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);
        ctx = fd_ctx->ctx;

        return libgf_client_fsetxattr (ctx, __fd, name, value, size, flags);
}

ssize_t 
glusterfs_lgetxattr (libglusterfs_client_ctx_t *ctx, 
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
        /*
          libgf_client_local_t *local = frame->local;

          local->reply_stub = fop_fgetxattr_cbk_stub (frame, NULL, op_ret, op_errno, dict);

          pthread_mutex_lock (&local->lock);
          {
          local->complete = 1;
          pthread_cond_broadcast (&local->reply_cond);
          }
          pthread_mutex_unlock (&local->lock);
        */
        return 0;
}

ssize_t 
libgf_client_fgetxattr (libglusterfs_client_ctx_t *ctx, 
                        fd_t *fd, 
                        const char *name,
                        void *value, 
                        size_t size)
{
#if 0
        call_stub_t  *stub = NULL;
        int32_t op_ret = 0;

        LIBGF_CLIENT_FOP (ctx, stub, fgetxattr, NULL, fd, name, value, size);

        op_ret = stub->args.fgetxattr_cbk.op_ret;
        errno = stub->args.fgetxattr_cbk.op_errno;

        if (op_ret >= 0) {
                /*
                  gf_log ("LIBGF_CLIENT", GF_LOG_DEBUG,
                  "%"PRId64": %s => %d", frame->root->unique,
                  state->fuse_loc.loc.path, op_ret);
                */

                data_t *value_data = dict_get (stub->args.getxattr_cbk.dict, (char *)name);
    
                if (value_data) {
                        int32_t copy_len = 0;
                        op_ret = value_data->len; /* Don't return the value for '\0' */

                        copy_len = size < value_data->len ? size : value_data->len;
                        /*FIXME: where is this freed? */
                        memcpy (value, value_data->data, copy_len);
                } else {
                        errno = ENODATA;
                        op_ret = -1;
                }
        }

	call_stub_destroy (stub);
#endif
        return 0;
}

ssize_t 
glusterfs_fgetxattr (unsigned long fd, 
                     const char *name,
                     void *value, 
                     size_t size)
{
        libglusterfs_client_ctx_t *ctx;
        fd_t *__fd = (fd_t *)fd;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;
        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);
        ctx = fd_ctx->ctx;

        return libgf_client_fgetxattr (ctx, __fd, name, value, size);
}

ssize_t 
glusterfs_listxattr (libglusterfs_client_ctx_t *ctx, 
                     const char *path, 
                     char *list,
                     size_t size)
{
        return ENOSYS;
}

ssize_t 
glusterfs_llistxattr (libglusterfs_client_ctx_t *ctx, 
                      const char *path, 
                      char *list,
                      size_t size)
{
        return ENOSYS;
}

ssize_t 
glusterfs_flistxattr (unsigned long fd, 
                      char *list,
                      size_t size)
{
        return ENOSYS;
}

int 
glusterfs_removexattr (libglusterfs_client_ctx_t *ctx, 
                       const char *path, 
                       const char *name)
{
        return ENOSYS;
}

int 
glusterfs_lremovexattr (libglusterfs_client_ctx_t *ctx, 
                        const char *path, 
                        const char *name)
{
        return ENOSYS;
}

int 
glusterfs_fremovexattr (unsigned long fd, 
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
                        struct stat *stbuf)
{
        libgf_client_local_t *local = frame->local;

        local->reply_stub = fop_readv_cbk_stub (frame, NULL, op_ret, op_errno, vector, count, stbuf);

        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);

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
                        int len = size < vector[i].iov_len ? size : vector[i].iov_len;
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
glusterfs_read (unsigned long fd, 
                void *buf, 
                size_t nbytes)
{
        int32_t op_ret = -1;
        off_t offset = 0;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;

        if (!fd) {
                errno = EINVAL;
                return -1;
        }

        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);

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
                op_ret = 0;
    
                while ((size) && (dst < dst_count) && (src < src_count)) {
                        int len = 0, src_len, dst_len;
   
                        src_len = src_vector[src].iov_len - src_offset;
                        dst_len = dst_vector[dst].iov_len - dst_offset;

                        len = (src_len < dst_len) ? src_len : dst_len;
                        if (len > size) {
                                len = size;
                        }

                        memcpy (dst_vector[dst].iov_base + dst_offset, src_vector[src].iov_base + src_offset, len);
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
glusterfs_readv (unsigned long fd, const struct iovec *vec, int count)
{
        int32_t op_ret = -1;
        off_t offset = 0;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;

        if (!fd) {
                errno = EINVAL;
                return -1;
        }

        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);

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

        return op_ret;
}


ssize_t 
glusterfs_pread (unsigned long fd, 
                 void *buf, 
                 size_t count, 
                 off_t offset)
{
        int32_t op_ret = -1;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;

        if (!fd) {
                errno = EINVAL;
                return -1;
        }

        /*  gf_log ("libglusterfsclient",
            GF_LOG_DEBUG,
            "glusterfs_pread with offset:%"PRId64, offset);*/

        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);

        ctx = fd_ctx->ctx;

        op_ret = libgf_client_read (ctx, (fd_t *)fd, buf, count, offset);

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

        local->reply_stub = fop_writev_cbk_stub (frame, NULL, op_ret, op_errno, stbuf);

        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);
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

        LIBGF_CLIENT_FOP (ctx, stub, writev, local, fd, vector, count, offset);

        op_ret = stub->args.writev_cbk.op_ret;
        errno = stub->args.writev_cbk.op_errno;

	call_stub_destroy (stub);
        return op_ret;
}


ssize_t 
glusterfs_write (unsigned long fd, 
                 const void *buf, 
                 size_t n)
{
        int32_t op_ret = -1;
        off_t offset = 0;
        struct iovec vector;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;

        if (!fd) {
                errno = EINVAL;
                return -1;
        }

        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);

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

        return op_ret;
}

ssize_t 
glusterfs_writev (unsigned long fd, 
                  const struct iovec *vector,
                  size_t count)
{
        int32_t op_ret = -1;
        off_t offset = 0;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;

        if (!fd) {
                errno = EINVAL;
                return -1;
        }


        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);

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

        return op_ret;
}


ssize_t 
glusterfs_pwrite (unsigned long fd, 
                  const void *buf, 
                  size_t count, 
                  off_t offset)
{
        int32_t op_ret = -1;
        struct iovec vector;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;

        if (!fd) {
                errno = EINVAL;
                return -1;
        }

        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);

        ctx = fd_ctx->ctx;

        vector.iov_base = (void *)buf;
        vector.iov_len = count;

        op_ret = libgf_client_writev (ctx,
                                      (fd_t *)fd, 
                                      &vector, 
                                      1, 
                                      offset);

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

        local->reply_stub = fop_readdir_cbk_stub (frame, NULL, op_ret, op_errno, entries);

        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);
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
		list_for_each_entry (entry, &stub->args.readdir_cbk.entries.list, list) {
			entry_size = offsetof (struct dirent, d_name) + strlen (entry->d_name) + 1;
			
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
glusterfs_readdir (unsigned long fd, 
                   struct dirent *dirp, 
                   unsigned int count)
{
        int op_ret = -1;
        libglusterfs_client_ctx_t *ctx = NULL;
        off_t offset = 0;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;

        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);

        pthread_mutex_lock (&fd_ctx->lock);
        {
                ctx = fd_ctx->ctx;
                offset = fd_ctx->offset;
        }
        pthread_mutex_unlock (&fd_ctx->lock);

        op_ret = libgf_client_readdir (ctx, (fd_t *)fd, dirp, sizeof (*dirp), &offset, 1);

        if (op_ret > 0) {
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        fd_ctx->offset = offset;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
		op_ret = 1;
        }

        return op_ret;
}


int
glusterfs_getdents (unsigned long fd, struct dirent *dirp, unsigned int count)
{
        int op_ret = -1;
        libglusterfs_client_ctx_t *ctx = NULL;
        off_t offset = 0;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;

        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);

        pthread_mutex_lock (&fd_ctx->lock);
        {
                ctx = fd_ctx->ctx;
                offset = fd_ctx->offset;
        }
        pthread_mutex_unlock (&fd_ctx->lock);

        op_ret = libgf_client_readdir (ctx, (fd_t *)fd, dirp, count, &offset, -1);

        if (op_ret > 0) {
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        fd_ctx->offset = offset;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

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
                              struct stat *stbuf)
{
        glusterfs_read_buf_t *buf;
        libglusterfs_client_async_local_t *local = frame->local;
        fd_t *__fd = local->fop.readv_cbk.fd;
        glusterfs_readv_cbk_t readv_cbk = local->fop.readv_cbk.cbk;

        buf = calloc (1, sizeof (*buf));
        ERR_ABORT (buf);
        buf->vector = iov_dup (vector, count);
        buf->count = count;
        buf->op_ret = op_ret;
        buf->op_errno = op_errno;
        buf->ref = dict_ref (frame->root->rsp_refs);

        if (op_ret > 0) {
                libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
                data_t *fd_ctx_data = NULL;

                fd_ctx_data = dict_get (__fd->ctx, XLATOR_NAME);
                if (!fd_ctx_data) {
                        errno = EBADF;
                        return -1;
                }

                fd_ctx = data_to_ptr (fd_ctx_data);
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        fd_ctx->offset += op_ret;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

        readv_cbk (buf, local->cbk_data); 
        STACK_DESTROY (frame->root);

        return 0;
}

void 
glusterfs_free (glusterfs_read_buf_t *buf)
{
        //iov_free (buf->vector, buf->count);
        FREE (buf->vector);
        dict_unref ((dict_t *) buf->ref);
        FREE (buf);
}

int 
glusterfs_read_async (unsigned long fd, 
                      size_t nbytes, 
                      off_t offset,
                      glusterfs_readv_cbk_t readv_cbk,
                      void *cbk_data)
{
        libglusterfs_client_ctx_t *ctx;
        fd_t *__fd = (fd_t *)fd;
        libglusterfs_client_async_local_t *local = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;

        local = calloc (1, sizeof (*local));
        ERR_ABORT (local);
        local->fop.readv_cbk.fd = __fd;
        local->fop.readv_cbk.cbk = readv_cbk;
        local->cbk_data = cbk_data;

        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);
  
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
        return 0;
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
        glusterfs_writev_cbk_t writev_cbk;

        writev_cbk = local->fop.writev_cbk.cbk;
        fd = local->fop.writev_cbk.fd;

        if (op_ret > 0) {
                libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
                data_t *fd_ctx_data = NULL;

                fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
                if (!fd_ctx_data) {
                        errno = EBADF;
                        return -1;
                }

                fd_ctx = data_to_ptr (fd_ctx_data);

                pthread_mutex_lock (&fd_ctx->lock);
                {
                        fd_ctx->offset += op_ret;  
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

        writev_cbk (op_ret, op_errno, local->cbk_data);

        STACK_DESTROY (frame->root);
        return 0;
}

int32_t
glusterfs_write_async (unsigned long fd, 
                       const void *buf, 
                       size_t nbytes, 
                       off_t offset,
                       glusterfs_writev_cbk_t writev_cbk,
                       void *cbk_data)
{
        fd_t *__fd = (fd_t *)fd;
        struct iovec vector;
        off_t __offset = offset;
        libglusterfs_client_ctx_t *ctx = NULL;
        libglusterfs_client_async_local_t *local = NULL;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;

        local = calloc (1, sizeof (*local));
        ERR_ABORT (local);
        local->fop.writev_cbk.fd = __fd;
        local->fop.writev_cbk.cbk = writev_cbk;
        local->cbk_data = cbk_data;

        vector.iov_base = (void *)buf;
        vector.iov_len = nbytes;
  
        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);
        ctx = fd_ctx->ctx;
 
        if (offset < 0) {
                pthread_mutex_lock (&fd_ctx->lock);
                {
                        __offset = fd_ctx->offset;
                }
                pthread_mutex_unlock (&fd_ctx->lock);
        }

        LIBGF_CLIENT_FOP_ASYNC (ctx,
                                local,
                                libglusterfs_writev_async_cbk,
                                writev,
                                __fd,
                                &vector,
                                1,
                                __offset);

        return 0;
}

off_t
glusterfs_lseek (unsigned long fd, off_t offset, int whence)
{
        fd_t *__fd = (fd_t *)fd;
        off_t __offset = 0;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;
  
        fd_ctx_data = dict_get (__fd->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADFD;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);

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
                /* TODO: inode has stbuf struct commented out */
                /*__offset = fd->inode->stbuf.st_size + offset; */
                break;
        }

        pthread_mutex_lock (&fd_ctx->lock);
        {
                fd_ctx->offset = __offset;
        }
        pthread_mutex_unlock (&fd_ctx->lock);
  
        /* TODO: check whether this is the one to be returned */
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

        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);

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
        data_t *inode_ctx_data = NULL;
        libgf_client_local_t *local = NULL;

        inode_ctx_data = dict_get (loc->inode->ctx, XLATOR_NAME);
        if (!inode_ctx_data) 
                return -1;

        inode_ctx = data_to_ptr (inode_ctx_data);

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
                return 0;
        }
    
        LIBGF_CLIENT_FOP (ctx, stub, stat, local, loc);
 
        op_ret = stub->args.stat_cbk.op_ret;
        errno = stub->args.stat_cbk.op_errno;
        *stbuf = stub->args.stat_cbk.buf;

        pthread_mutex_lock (&inode_ctx->lock);
        {
                memcpy (&inode_ctx->stbuf, stbuf, sizeof (*stbuf));
                current = time (NULL);
                inode_ctx->previous_stat_time = current;
        }
        pthread_mutex_unlock (&inode_ctx->lock);

	call_stub_destroy (stub);
        return op_ret;
}

int32_t  
glusterfs_stat (libglusterfs_handle_t handle, 
                const char *path, 
                struct stat *buf)
{
        int32_t op_ret = 0;
        loc_t loc = {0, };
        char lookup_required = 1;
        libglusterfs_client_ctx_t *ctx = handle;

        libgf_client_loc_fill (&loc, path, ctx);

        if (loc.inode && loc.inode->ctx) {
                time_t current, prev;
                libglusterfs_client_inode_ctx_t *inode_ctx = NULL;
                data_t *inode_ctx_data = NULL;

                inode_ctx_data = dict_get (loc.inode->ctx, XLATOR_NAME);
                if (!inode_ctx_data) {
                        inode_unref (loc.inode);
                        errno = EINVAL;
                        return -1;
                }

                inode_ctx = data_to_ptr (inode_ctx_data);

                memset (&current, 0, sizeof (current));
                current = time (NULL);

                pthread_mutex_lock (&inode_ctx->lock);
                {
                        prev = inode_ctx->previous_lookup_time;
                }
                pthread_mutex_unlock (&inode_ctx->lock);

                if (prev >= 0 && ctx->lookup_timeout >= (current - prev)) {
                        lookup_required = 0;
                } 
        }

        if (lookup_required) {
                op_ret = libgf_client_lookup (ctx, &loc, buf, NULL, 0);
        }

        if (!op_ret) {
                op_ret = libgf_client_stat (ctx, &loc, buf);
        }

        libgf_client_loc_wipe (&loc);
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

        pthread_mutex_lock (&local->lock);
        {
                local->complete = 1;
                pthread_cond_broadcast (&local->reply_cond);
        }
        pthread_mutex_unlock (&local->lock);

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
        data_t *inode_ctx_data = NULL;
        libgf_client_local_t *local = NULL;

        current = time (NULL);
        inode_ctx_data = dict_get (fd->inode->ctx, XLATOR_NAME);
        if (!inode_ctx_data) {
                errno = EINVAL;
                return -1;
        }

        inode_ctx = data_to_ptr (inode_ctx_data);

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
                return 0;
        }

        LIBGF_CLIENT_FOP (ctx, stub, fstat, local, __fd);
 
        op_ret = stub->args.fstat_cbk.op_ret;
        errno = stub->args.fstat_cbk.op_errno;
        *buf = stub->args.fstat_cbk.buf;

        pthread_mutex_lock (&inode_ctx->lock);
        {
                memcpy (&inode_ctx->stbuf, buf, sizeof (*buf));
                current = time (NULL);
                inode_ctx->previous_stat_time = current;
        }
        pthread_mutex_unlock (&inode_ctx->lock);

	call_stub_destroy (stub);
        return op_ret;
}

int32_t 
glusterfs_fstat (unsigned long fd, struct stat *buf) 
{
        libglusterfs_client_ctx_t *ctx;
        fd_t *__fd = (fd_t *)fd;
        libglusterfs_client_fd_ctx_t *fd_ctx = NULL;
        data_t *fd_ctx_data = NULL;

        fd_ctx_data = dict_get (((fd_t *) fd)->ctx, XLATOR_NAME);
        if (!fd_ctx_data) {
                errno = EBADF;
                return -1;
        }

        fd_ctx = data_to_ptr (fd_ctx_data);
        ctx = fd_ctx->ctx;

        return libgf_client_fstat (ctx, __fd, buf);
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

        top = calloc (1, sizeof (*top));
        ERR_ABORT (top);

        xlchild = calloc (1, sizeof(*xlchild));
        ERR_ABORT (xlchild);
        xlchild->xlator = graph;
        top->children = xlchild;
        top->ctx = graph->ctx;
        top->next = graph;
        top->name = strdup (XLATOR_NAME);

        xlparent = calloc (1, sizeof(*xlparent));
        xlparent->xlator = top;
        graph->parents = xlparent;
        asprintf (&top->type, XLATOR_NAME);

        top->init = libgf_client_init;
        top->fops = &libgf_client_fops;
        top->mops = &libgf_client_mops;
        top->cbks = &libgf_client_cbks; 
        top->notify = libgf_client_notify;
        top->fini = libgf_client_fini;
        //  fill_defaults (top);

        return top;
}

/*
void
__attribute__ ((constructor)) init (void) 
{
        pthread_mutex_init (&libgf_client_lock, NULL);
}

void
__attribute__ ((destructor)) fini (void) 
{
        pthread_mutex_destroy (&libgf_client_lock);
}
*/


