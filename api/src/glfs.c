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
  - set proper pid/lk_owner to call frames (currently buried in syncop)
  - fix logging.c/h to store logfp and loglevel in glusterfs_ctx_t and
    reach it via THIS.
  - fd migration on graph switch.
  - update syncop functions to accept/return xdata. ???
  - syncop_readv to not touch params if args.op_ret < 0.
  - protocol/client to reconnect immediately after portmap disconnect.
  - handle SEEK_END failure in _lseek()
  - handle umask (per filesystem?)
  - implement glfs_set_xlator_option(), like --xlator-option
  - make itables LRU based
  - implement glfs_fini()
  - modify syncop_fsync() to accept 'dataonly' flag
  - 0-copy for readv/writev
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

	xlator_mem_acct_init (THIS, glfs_mt_end);

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

	ctx->env = syncenv_new (0);
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

	INIT_LIST_HEAD (&pool->all_frames);
	INIT_LIST_HEAD (&ctx->cmd_args.xlator_options);
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
	int	     ret = 0;
	cmd_args_t  *cmd_args = NULL;
	FILE	    *specfp = NULL;
	struct stat  statbuf;

	cmd_args = &fs->ctx->cmd_args;

	ret = lstat (cmd_args->volfile, &statbuf);
	if (ret == -1) {
		gf_log ("glfs", GF_LOG_ERROR,
			"%s: %s", cmd_args->volfile, strerror (errno));
		return NULL;
	}

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


struct glfs *
glfs_from_glfd (struct glfs_fd *glfd)
{
	return 	((xlator_t *)glfd->fd->inode->table->xl->ctx->master)->private;
}


void
glfs_fd_destroy (struct glfs_fd *glfd)
{
	if (!glfd)
		return;
	if (glfd->fd)
		fd_unref (glfd->fd);
	GF_FREE (glfd);
}


xlator_t *
glfs_fd_subvol (struct glfs_fd *glfd)
{
	xlator_t    *subvol = NULL;

	if (!glfd)
		return NULL;

	subvol = glfd->fd->inode->table->xl;

	return subvol;
}


xlator_t *
glfs_active_subvol (struct glfs *fs)
{
	xlator_t      *subvol = NULL;
	inode_table_t *itable = NULL;

	pthread_mutex_lock (&fs->mutex);
	{
		while (!fs->init)
			pthread_cond_wait (&fs->cond, &fs->mutex);

		subvol = fs->active_subvol;
	}
	pthread_mutex_unlock (&fs->mutex);

	if (!subvol)
		return NULL;

	if (!subvol->itable) {
		itable = inode_table_new (0, subvol);
		if (!itable) {
			errno = ENOMEM;
			return NULL;
		}

		subvol->itable = itable;

		glfs_first_lookup (subvol);
	}

	return subvol;
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
glfs_new (const char *volname)
{
	struct glfs     *fs = NULL;
	int              ret = -1;
	glusterfs_ctx_t *ctx = NULL;

	/* first globals init, for gf_mem_acct_enable_set () */
	ret = glusterfs_globals_init ();
	if (ret)
		return NULL;

	ctx = glusterfs_ctx_new ();
	if (!ctx) {
		return NULL;
	}
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

	return fs;
}


int
glfs_set_volfile (struct glfs *fs, const char *volfile)
{
	cmd_args_t  *cmd_args = NULL;

	cmd_args = &fs->ctx->cmd_args;

	if (vol_assigned (cmd_args))
		return -1;

	cmd_args->volfile = gf_strdup (volfile);

	return 0;
}


int
glfs_set_volfile_server (struct glfs *fs, const char *transport,
			 const char *host, int port)
{
	cmd_args_t  *cmd_args = NULL;

	cmd_args = &fs->ctx->cmd_args;

	if (vol_assigned (cmd_args))
		return -1;

	cmd_args->volfile_server = gf_strdup (host);
	cmd_args->volfile_server_transport = gf_strdup (transport);
	cmd_args->volfile_server_port = port;
	cmd_args->max_connect_attempts = 2;

	return 0;
}


int
glfs_set_logging (struct glfs *fs, const char *logfile, int loglevel)
{
	int  ret = -1;

	ret = gf_log_init (logfile);
	if (ret)
		return ret;

	gf_log_set_loglevel (loglevel);

	return ret;
}


int
glfs_init_wait (struct glfs *fs)
{
	int   ret = -1;

	pthread_mutex_lock (&fs->mutex);
	{
		while (!fs->init)
			pthread_cond_wait (&fs->cond,
					   &fs->mutex);
		ret = fs->ret;
		errno = fs->err;
	}
	pthread_mutex_unlock (&fs->mutex);

	return ret;
}


void
glfs_init_done (struct glfs *fs, int ret)
{
	if (fs->init_cbk) {
		fs->init_cbk (fs, ret);
		return;
	}

	pthread_mutex_lock (&fs->mutex);
	{
		fs->init = 1;
		fs->ret = ret;
		fs->err = errno;

		pthread_cond_broadcast (&fs->cond);
	}
	pthread_mutex_unlock (&fs->mutex);
}


int
glfs_init_common (struct glfs *fs)
{
	int  ret = -1;

	ret = create_master (fs);
	if (ret)
		return ret;

	ret = pthread_create (&fs->poller, NULL, glfs_poller, fs);
	if (ret)
		return ret;

	ret = glfs_volumes_init (fs);
	if (ret)
		return ret;

	return ret;
}


int
glfs_init_async (struct glfs *fs, glfs_init_cbk cbk)
{
	int  ret = -1;

	fs->init_cbk = cbk;

	ret = glfs_init_common (fs);

	return ret;
}


int
glfs_init (struct glfs *fs)
{
	int  ret = -1;

	ret = glfs_init_common (fs);
	if (ret)
		return ret;

	ret = glfs_init_wait (fs);

	return ret;
}


int
glfs_fini (struct glfs *fs)
{
	int  ret = -1;

	return ret;
}
