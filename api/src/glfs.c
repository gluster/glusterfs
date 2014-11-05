/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


/*
  TODO:
  - merge locks in glfs_posix_lock for lock self-healing
  - set proper pid/lk_owner to call frames (currently buried in syncop)
  - fix logging.c/h to store logfp and loglevel in glusterfs_ctx_t and
    reach it via THIS.
  - update syncop functions to accept/return xdata. ???
  - protocol/client to reconnect immediately after portmap disconnect.
  - handle SEEK_END failure in _lseek()
  - handle umask (per filesystem?)
  - make itables LRU based
  - 0-copy for readv/writev
  - reconcile the open/creat mess
*/

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "stack.h"
#include "event.h"
#include "glfs-mem-types.h"
#include "common-utils.h"
#include "syncop.h"
#include "call-stub.h"

#include "glfs.h"
#include "glfs-internal.h"
#include "hashfn.h"
#include "rpc-clnt.h"


static gf_boolean_t
vol_assigned (cmd_args_t *args)
{
	return args->volfile || args->volfile_server;
}


static int
glusterfs_ctx_defaults_init (glusterfs_ctx_t *ctx)
{
	call_pool_t   *pool = NULL;
	int	       ret = -1;

	xlator_mem_acct_init (THIS, glfs_mt_end + 1);

	if (!ctx) {
		goto err;
	}

	ctx->process_uuid = generate_glusterfs_ctx_id ();
	if (!ctx->process_uuid) {
		goto err;
	}

	ctx->page_size	= 128 * GF_UNIT_KB;

	ctx->iobuf_pool = iobuf_pool_new ();
	if (!ctx->iobuf_pool) {
		goto err;
	}

	ctx->event_pool = event_pool_new (DEFAULT_EVENT_POOL_SIZE);
	if (!ctx->event_pool) {
		goto err;
	}

	ctx->env = syncenv_new (0, 0, 0);
	if (!ctx->env) {
		goto err;
	}

	pool = GF_CALLOC (1, sizeof (call_pool_t),
			  glfs_mt_call_pool_t);
	if (!pool) {
		goto err;
	}

	/* frame_mem_pool size 112 * 4k */
	pool->frame_mem_pool = mem_pool_new (call_frame_t, 4096);
	if (!pool->frame_mem_pool) {
		goto err;
	}
	/* stack_mem_pool size 256 * 1024 */
	pool->stack_mem_pool = mem_pool_new (call_stack_t, 1024);
	if (!pool->stack_mem_pool) {
		goto err;
	}

	ctx->stub_mem_pool = mem_pool_new (call_stub_t, 1024);
	if (!ctx->stub_mem_pool) {
		goto err;
	}

	ctx->dict_pool = mem_pool_new (dict_t, GF_MEMPOOL_COUNT_OF_DICT_T);
	if (!ctx->dict_pool)
		goto err;

	ctx->dict_pair_pool = mem_pool_new (data_pair_t,
					    GF_MEMPOOL_COUNT_OF_DATA_PAIR_T);
	if (!ctx->dict_pair_pool)
		goto err;

	ctx->dict_data_pool = mem_pool_new (data_t, GF_MEMPOOL_COUNT_OF_DATA_T);
	if (!ctx->dict_data_pool)
		goto err;

        ctx->logbuf_pool = mem_pool_new (log_buf_t,
                                         GF_MEMPOOL_COUNT_OF_LRU_BUF_T);
        if (!ctx->logbuf_pool)
                goto err;

	INIT_LIST_HEAD (&pool->all_frames);
	INIT_LIST_HEAD (&ctx->cmd_args.xlator_options);
        INIT_LIST_HEAD (&ctx->cmd_args.volfile_servers);

	LOCK_INIT (&pool->lock);
	ctx->pool = pool;

	pthread_mutex_init (&(ctx->lock), NULL);

	ret = 0;
err:
	if (ret && pool) {
		if (pool->frame_mem_pool)
			mem_pool_destroy (pool->frame_mem_pool);
		if (pool->stack_mem_pool)
			mem_pool_destroy (pool->stack_mem_pool);
		GF_FREE (pool);
	}

	if (ret && ctx) {
		if (ctx->stub_mem_pool)
			mem_pool_destroy (ctx->stub_mem_pool);
		if (ctx->dict_pool)
			mem_pool_destroy (ctx->dict_pool);
		if (ctx->dict_data_pool)
			mem_pool_destroy (ctx->dict_data_pool);
		if (ctx->dict_pair_pool)
			mem_pool_destroy (ctx->dict_pair_pool);
                if (ctx->logbuf_pool)
                        mem_pool_destroy (ctx->logbuf_pool);
	}

	return ret;
}


static int
create_master (struct glfs *fs)
{
	int		 ret = 0;
	xlator_t	*master = NULL;

	master = GF_CALLOC (1, sizeof (*master),
			    glfs_mt_xlator_t);
	if (!master)
		goto err;

	master->name = gf_strdup ("gfapi");
	if (!master->name)
		goto err;

	if (xlator_set_type (master, "mount/api") == -1) {
		gf_log ("glfs", GF_LOG_ERROR,
			"master xlator for %s initialization failed",
			fs->volname);
		goto err;
	}

	master->ctx	 = fs->ctx;
	master->private	 = fs;
	master->options	 = get_new_dict ();
	if (!master->options)
		goto err;


	ret = xlator_init (master);
	if (ret) {
		gf_log ("glfs", GF_LOG_ERROR,
			"failed to initialize gfapi translator");
		goto err;
	}

	fs->ctx->master = master;
	THIS = master;

	return 0;

err:
	if (master) {
		xlator_destroy (master);
	}

	return -1;
}


static FILE *
get_volfp (struct glfs *fs)
{
	cmd_args_t  *cmd_args = NULL;
	FILE	    *specfp = NULL;

	cmd_args = &fs->ctx->cmd_args;

	if ((specfp = fopen (cmd_args->volfile, "r")) == NULL) {
		gf_log ("glfs", GF_LOG_ERROR,
			"volume file %s: %s",
			cmd_args->volfile,
			strerror (errno));
		return NULL;
	}

	gf_log ("glfs", GF_LOG_DEBUG,
		"loading volume file %s", cmd_args->volfile);

	return specfp;
}


int
glfs_volumes_init (struct glfs *fs)
{
	FILE		   *fp = NULL;
	cmd_args_t	   *cmd_args = NULL;
	int		    ret = 0;

	cmd_args = &fs->ctx->cmd_args;

	if (!vol_assigned (cmd_args))
		return -1;

	if (cmd_args->volfile_server) {
		ret = glfs_mgmt_init (fs);
		goto out;
	}

	fp = get_volfp (fs);

	if (!fp) {
		gf_log ("glfs", GF_LOG_ERROR,
			"Cannot reach volume specification file");
		ret = -1;
		goto out;
	}

	ret = glfs_process_volfp (fs, fp);
	if (ret)
		goto out;

out:
	return ret;
}


///////////////////////////////////////////////////////////////////////////////


int
pub_glfs_set_xlator_option (struct glfs *fs, const char *xlator,
                            const char *key, const char *value)
{
	xlator_cmdline_option_t *option = NULL;

	option = GF_CALLOC (1, sizeof (*option),
			    glfs_mt_xlator_cmdline_option_t);
	if (!option)
		goto enomem;

	INIT_LIST_HEAD (&option->cmd_args);

	option->volume = gf_strdup (xlator);
	if (!option->volume)
		goto enomem;
	option->key = gf_strdup (key);
	if (!option->key)
		goto enomem;
	option->value = gf_strdup (value);
	if (!option->value)
		goto enomem;

	list_add (&option->cmd_args, &fs->ctx->cmd_args.xlator_options);

	return 0;
enomem:
	errno = ENOMEM;

	if (!option)
		return -1;

	GF_FREE (option->volume);
	GF_FREE (option->key);
	GF_FREE (option->value);
	GF_FREE (option);

	return -1;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_set_xlator_option, 3.4.0);


int
pub_glfs_unset_volfile_server (struct glfs *fs, const char *transport,
                               const char *host, const int port)
{
        cmd_args_t       *cmd_args = NULL;
        server_cmdline_t *server = NULL;
        int               ret = -1;

        if (!transport || !host || !port) {
                errno = EINVAL;
                return ret;
        }

        cmd_args = &fs->ctx->cmd_args;
        list_for_each_entry(server, &cmd_args->curr_server->list, list) {
                if ((!strcmp(server->volfile_server, host) &&
                     !strcmp(server->transport, transport) &&
                     (server->port == port))) {
                        list_del (&server->list);
                        ret = 0;
                        goto out;
                }
        }

out:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_unset_volfile_server, 3.5.1);


int
pub_glfs_set_volfile_server (struct glfs *fs, const char *transport,
                             const char *host, int port)
{
        cmd_args_t            *cmd_args = NULL;
        server_cmdline_t      *server = NULL;
        server_cmdline_t      *tmp = NULL;
        int                    ret = -1;

        if (!transport || !host) {
                errno = EINVAL;
                return ret;
        }

        cmd_args = &fs->ctx->cmd_args;

        cmd_args->max_connect_attempts = 1;

        server = GF_CALLOC (1, sizeof (server_cmdline_t),
                            glfs_mt_server_cmdline_t);

        if (!server) {
                errno = ENOMEM;
                goto out;
        }

        INIT_LIST_HEAD (&server->list);

        server->volfile_server = gf_strdup (host);
        if (!server->volfile_server) {
                errno = ENOMEM;
                goto out;
        }

        server->transport = gf_strdup (transport);
        if (!server->transport) {
                errno = ENOMEM;
                goto out;
        }

        server->port = port;

        if (!cmd_args->volfile_server) {
                cmd_args->volfile_server = server->volfile_server;
                cmd_args->volfile_server_transport = server->transport;
                cmd_args->volfile_server_port = server->port;
                cmd_args->curr_server = server;
        }

        list_for_each_entry(tmp, &cmd_args->volfile_servers, list) {
                if ((!strcmp(tmp->volfile_server, host) &&
                     !strcmp(tmp->transport, transport) &&
                     (tmp->port == port))) {
                        errno = EEXIST;
                        ret = -1;
                        goto out;
                }
        }

        list_add_tail (&server->list, &cmd_args->volfile_servers);

        ret = 0;
out:
        if (ret == -1) {
                if (server) {
                        GF_FREE (server->volfile_server);
                        GF_FREE (server->transport);
                        GF_FREE (server);
                }
        }

        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_set_volfile_server, 3.4.0);


int
pub_glfs_setfsuid (uid_t fsuid)
{
	return syncopctx_setfsuid (&fsuid);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_setfsuid, 3.4.2);


int
pub_glfs_setfsgid (gid_t fsgid)
{
	return syncopctx_setfsgid (&fsgid);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_setfsgid, 3.4.2);


int
pub_glfs_setfsgroups (size_t size, const gid_t *list)
{
	return syncopctx_setfsgroups(size, list);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_setfsgroups, 3.4.2);


struct glfs *
pub_glfs_from_glfd (struct glfs_fd *glfd)
{
	return glfd->fs;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_from_glfd, 3.4.0);


struct glfs_fd *
glfs_fd_new (struct glfs *fs)
{
	struct glfs_fd  *glfd = NULL;

	glfd = GF_CALLOC (1, sizeof (*glfd), glfs_mt_glfs_fd_t);
	if (!glfd)
		return NULL;

	glfd->fs = fs;

	INIT_LIST_HEAD (&glfd->openfds);

	return glfd;
}


void
glfs_fd_bind (struct glfs_fd *glfd)
{
	struct glfs *fs = NULL;

	fs = glfd->fs;

	glfs_lock (fs);
	{
		list_add_tail (&glfd->openfds, &fs->openfds);
	}
	glfs_unlock (fs);
}

void
glfs_fd_destroy (struct glfs_fd *glfd)
{
	if (!glfd)
		return;

	glfs_lock (glfd->fs);
	{
		list_del_init (&glfd->openfds);
	}
	glfs_unlock (glfd->fs);

	if (glfd->fd)
		fd_unref (glfd->fd);

	GF_FREE (glfd->readdirbuf);

	GF_FREE (glfd);
}


static void *
glfs_poller (void *data)
{
	struct glfs  *fs = NULL;

	fs = data;

	event_dispatch (fs->ctx->event_pool);

	return NULL;
}


struct glfs *
pub_glfs_new (const char *volname)
{
	struct glfs     *fs = NULL;
	int              ret = -1;
	glusterfs_ctx_t *ctx = NULL;

	ctx = glusterfs_ctx_new ();
	if (!ctx) {
		return NULL;
	}

	/* first globals init, for gf_mem_acct_enable_set () */
	ret = glusterfs_globals_init (ctx);
	if (ret)
		return NULL;

	THIS->ctx = ctx;

	/* then ctx_defaults_init, for xlator_mem_acct_init(THIS) */
	ret = glusterfs_ctx_defaults_init (ctx);
	if (ret)
		return NULL;

	fs = GF_CALLOC (1, sizeof (*fs), glfs_mt_glfs_t);
	if (!fs)
		return NULL;
	fs->ctx = ctx;

	glfs_set_logging (fs, "/dev/null", 0);

	fs->ctx->cmd_args.volfile_id = gf_strdup (volname);

	fs->volname = gf_strdup (volname);

	pthread_mutex_init (&fs->mutex, NULL);
	pthread_cond_init (&fs->cond, NULL);

	INIT_LIST_HEAD (&fs->openfds);

	return fs;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_new, 3.4.0);


int
pub_glfs_set_volfile (struct glfs *fs, const char *volfile)
{
	cmd_args_t  *cmd_args = NULL;

	cmd_args = &fs->ctx->cmd_args;

	if (vol_assigned (cmd_args))
		return -1;

	cmd_args->volfile = gf_strdup (volfile);

	return 0;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_set_volfile, 3.4.0);


int
pub_glfs_set_logging (struct glfs *fs, const char *logfile, int loglevel)
{
        int  ret = 0;
        char *tmplog = NULL;

        if (!logfile) {
                ret = gf_set_log_file_path (&fs->ctx->cmd_args);
                if (ret)
                        goto out;
                tmplog = fs->ctx->cmd_args.log_file;
        } else {
                tmplog = (char *)logfile;
        }

        /* finish log set parameters before init */
        if (loglevel >= 0)
                gf_log_set_loglevel (loglevel);

        ret = gf_log_init (fs->ctx, tmplog, NULL);
        if (ret)
                goto out;

        ret = gf_log_inject_timer_event (fs->ctx);
        if (ret)
                goto out;

out:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_set_logging, 3.4.0);


int
glfs_init_wait (struct glfs *fs)
{
	int   ret = -1;

	/* Always a top-down call, use glfs_lock() */
	glfs_lock (fs);
	{
		while (!fs->init)
			pthread_cond_wait (&fs->cond,
					   &fs->mutex);
		ret = fs->ret;
		errno = fs->err;
	}
	glfs_unlock (fs);

	return ret;
}


void
priv_glfs_init_done (struct glfs *fs, int ret)
{
	glfs_init_cbk init_cbk;

	if (!fs) {
		gf_log ("glfs", GF_LOG_ERROR,
			"fs is NULL");
		goto out;
	}

	init_cbk = fs->init_cbk;

	/* Always a bottom-up call, use mutex_lock() */
	pthread_mutex_lock (&fs->mutex);
	{
		fs->init = 1;
		fs->ret = ret;
		fs->err = errno;

		if (!init_cbk)
			pthread_cond_broadcast (&fs->cond);
	}
	pthread_mutex_unlock (&fs->mutex);

	if (init_cbk)
		init_cbk (fs, ret);
out:
	return;
}

GFAPI_SYMVER_PRIVATE_DEFAULT(glfs_init_done, 3.4.0);


int
glfs_init_common (struct glfs *fs)
{
	int  ret = -1;

	ret = create_master (fs);
	if (ret)
		return ret;

	ret = gf_thread_create (&fs->poller, NULL, glfs_poller, fs);
	if (ret)
		return ret;

	ret = glfs_volumes_init (fs);
	if (ret)
		return ret;

	fs->dev_id = gf_dm_hashfn (fs->volname, strlen (fs->volname));
	return ret;
}


int
glfs_init_async (struct glfs *fs, glfs_init_cbk cbk)
{
	int  ret = -1;

	if (!fs || !fs->ctx) {
		gf_log ("glfs", GF_LOG_ERROR,
			"fs is not properly initialized.");
		errno = EINVAL;
		return ret;
	}

	fs->init_cbk = cbk;

	ret = glfs_init_common (fs);

	return ret;
}


int
pub_glfs_init (struct glfs *fs)
{
	int  ret = -1;

	if (!fs || !fs->ctx) {
		gf_log ("glfs", GF_LOG_ERROR,
			"fs is not properly initialized.");
		errno = EINVAL;
		return ret;
	}

	ret = glfs_init_common (fs);
	if (ret)
		return ret;

	ret = glfs_init_wait (fs);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_init, 3.4.0);


extern xlator_t *
priv_glfs_active_subvol (struct glfs *);

int
pub_glfs_fini (struct glfs *fs)
{
        int             ret = -1;
        int             countdown = 100;
        xlator_t        *subvol = NULL;
        glusterfs_ctx_t *ctx = NULL;
        call_pool_t     *call_pool = NULL;
        int             fs_init = 0;

        ctx = fs->ctx;

        if (ctx->mgmt) {
                rpc_clnt_disable (ctx->mgmt);
                ctx->mgmt = NULL;
        }

        __glfs_entry_fs (fs);

        call_pool = fs->ctx->pool;

        while (countdown--) {
                /* give some time for background frames to finish */
                if (!call_pool->cnt)
                        break;
                usleep (100000);
        }
        /* leaked frames may exist, we ignore */

        /*We deem glfs_fini as successful if there are no pending frames in the call
         *pool*/
        ret = (call_pool->cnt == 0)? 0: -1;

        pthread_mutex_lock (&fs->mutex);
        {
                fs_init = fs->init;
        }
        pthread_mutex_unlock (&fs->mutex);

        if (fs_init != 0) {
                subvol = priv_glfs_active_subvol (fs);
                if (subvol) {
                        /* PARENT_DOWN within priv_glfs_subvol_done() is issued only
                           on graph switch (new graph should activiate and
                           decrement the extra @winds count taken in glfs_graph_setup()

                           Since we are explicitly destroying, PARENT_DOWN is necessary
                        */
                        xlator_notify (subvol, GF_EVENT_PARENT_DOWN, subvol, 0);
                        /* TBD: wait for CHILD_DOWN before exiting, in case of
                           asynchronous cleanup like graceful socket
                           disconnection in the future.
                        */
                }
                priv_glfs_subvol_done (fs, subvol);
        }

        if (gf_log_fini(ctx) != 0)
                ret = -1;

        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fini, 3.4.0);


ssize_t
pub_glfs_get_volfile (struct glfs *fs, void *buf, size_t len)
{
        ssize_t         res;

        glfs_lock(fs);
        if (len >= fs->oldvollen) {
                gf_log ("glfs", GF_LOG_TRACE, "copying %lu to %p", len, buf);
                memcpy(buf,fs->oldvolfile,len);
                res = len;
        }
        else {
                res = len - fs->oldvollen;
                gf_log ("glfs", GF_LOG_TRACE, "buffer is %ld too short", -res);
        }
        glfs_unlock(fs);

        return res;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_get_volfile, 3.6.0);

