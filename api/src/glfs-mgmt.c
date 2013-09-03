/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif /* _CONFIG_H */

#include "glusterfs.h"
#include "stack.h"
#include "dict.h"
#include "event.h"
#include "defaults.h"

#include "rpc-clnt.h"
#include "protocol-common.h"
#include "glusterfs3.h"
#include "portmap-xdr.h"
#include "xdr-generic.h"

#include "syncop.h"
#include "xlator.h"

#include "glfs-internal.h"
#include "glfs-mem-types.h"


int glfs_volfile_fetch (struct glfs *fs);

int
glfs_process_volfp (struct glfs *fs, FILE *fp)
{
	glusterfs_graph_t  *graph = NULL;
	int		    ret = -1;
	xlator_t	   *trav = NULL;
	glusterfs_ctx_t	   *ctx = NULL;

	ctx = fs->ctx;
	graph = glusterfs_graph_construct (fp);
	if (!graph) {
		gf_log ("glfs", GF_LOG_ERROR, "failed to construct the graph");
		goto out;
	}

	for (trav = graph->first; trav; trav = trav->next) {
		if (strcmp (trav->type, "mount/fuse") == 0) {
			gf_log ("glfs", GF_LOG_ERROR,
				"fuse xlator cannot be specified "
				"in volume file");
			goto out;
		}
	}

	ret = glusterfs_graph_prepare (graph, ctx);
	if (ret) {
		glusterfs_graph_destroy (graph);
		goto out;
	}

	ret = glusterfs_graph_activate (graph, ctx);

	if (ret) {
		glusterfs_graph_destroy (graph);
		goto out;
	}

	ret = 0;
out:
	if (fp)
		fclose (fp);

	if (!ctx->active) {
		ret = -1;
	}

	return ret;
}


int
mgmt_cbk_spec (struct rpc_clnt *rpc, void *mydata, void *data)
{
	struct glfs *fs = NULL;
	xlator_t    *this = NULL;

	this = mydata;
	fs = this->private;

	glfs_volfile_fetch (fs);

	return 0;
}


int
mgmt_cbk_event (struct rpc_clnt *rpc, void *mydata, void *data)
{
	return 0;
}


rpcclnt_cb_actor_t mgmt_cbk_actors[] = {
	[GF_CBK_FETCHSPEC] = {"FETCHSPEC", GF_CBK_FETCHSPEC, mgmt_cbk_spec },
	[GF_CBK_EVENT_NOTIFY] = {"EVENTNOTIFY", GF_CBK_EVENT_NOTIFY,
				 mgmt_cbk_event},
};


struct rpcclnt_cb_program mgmt_cbk_prog = {
	.progname  = "GlusterFS Callback",
	.prognum   = GLUSTER_CBK_PROGRAM,
	.progver   = GLUSTER_CBK_VERSION,
	.actors	   = mgmt_cbk_actors,
	.numactors = GF_CBK_MAXVALUE,
};

char *clnt_handshake_procs[GF_HNDSK_MAXVALUE] = {
	[GF_HNDSK_NULL]		= "NULL",
	[GF_HNDSK_SETVOLUME]	= "SETVOLUME",
	[GF_HNDSK_GETSPEC]	= "GETSPEC",
	[GF_HNDSK_PING]		= "PING",
	[GF_HNDSK_EVENT_NOTIFY] = "EVENTNOTIFY",
};

rpc_clnt_prog_t clnt_handshake_prog = {
	.progname  = "GlusterFS Handshake",
	.prognum   = GLUSTER_HNDSK_PROGRAM,
	.progver   = GLUSTER_HNDSK_VERSION,
	.procnames = clnt_handshake_procs,
};


int
mgmt_submit_request (void *req, call_frame_t *frame,
		     glusterfs_ctx_t *ctx,
		     rpc_clnt_prog_t *prog, int procnum,
		     fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
	int			ret	    = -1;
	int			count	   = 0;
	struct iovec		iov	    = {0, };
	struct iobuf		*iobuf = NULL;
	struct iobref		*iobref = NULL;
	ssize_t			xdr_size = 0;

	iobref = iobref_new ();
	if (!iobref) {
		goto out;
	}

	if (req) {
		xdr_size = xdr_sizeof (xdrproc, req);

		iobuf = iobuf_get2 (ctx->iobuf_pool, xdr_size);
		if (!iobuf) {
			goto out;
		};

		iobref_add (iobref, iobuf);

		iov.iov_base = iobuf->ptr;
		iov.iov_len  = iobuf_pagesize (iobuf);

		/* Create the xdr payload */
		ret = xdr_serialize_generic (iov, req, xdrproc);
		if (ret == -1) {
			gf_log (THIS->name, GF_LOG_WARNING,
				"failed to create XDR payload");
			goto out;
		}
		iov.iov_len = ret;
		count = 1;
	}

	/* Send the msg */
	ret = rpc_clnt_submit (ctx->mgmt, prog, procnum, cbkfn,
			       &iov, count,
			       NULL, 0, iobref, frame, NULL, 0, NULL, 0, NULL);

out:
	if (iobref)
		iobref_unref (iobref);

	if (iobuf)
		iobuf_unref (iobuf);
	return ret;
}


static int
xlator_equal_rec (xlator_t *xl1, xlator_t *xl2)
{
	xlator_list_t *trav1 = NULL;
	xlator_list_t *trav2 = NULL;
	int	       ret   = 0;

	if (xl1 == NULL || xl2 == NULL) {
		gf_log ("xlator", GF_LOG_DEBUG, "invalid argument");
		return -1;
	}

	trav1 = xl1->children;
	trav2 = xl2->children;

	while (trav1 && trav2) {
		ret = xlator_equal_rec (trav1->xlator, trav2->xlator);
		if (ret) {
			gf_log ("glfs-mgmt", GF_LOG_DEBUG,
				"xlators children not equal");
			goto out;
		}

		trav1 = trav1->next;
		trav2 = trav2->next;
	}

	if (trav1 || trav2) {
		ret = -1;
		goto out;
	}

	if (strcmp (xl1->name, xl2->name)) {
		ret = -1;
		goto out;
	}
out :
	return ret;
}


static gf_boolean_t
is_graph_topology_equal (glusterfs_graph_t *graph1,
			 glusterfs_graph_t *graph2)
{
	xlator_t    *trav1    = NULL;
	xlator_t    *trav2    = NULL;
	gf_boolean_t ret      = _gf_true;

	trav1 = graph1->first;
	trav2 = graph2->first;

	ret = xlator_equal_rec (trav1, trav2);

	if (ret) {
		gf_log ("glfs-mgmt", GF_LOG_DEBUG,
			"graphs are not equal");
		ret = _gf_false;
		goto out;
	}

	ret = _gf_true;
	gf_log ("glfs-mgmt", GF_LOG_DEBUG,
		"graphs are equal");

out:
	return ret;
}


/* Function has 3types of return value 0, -ve , 1
 *   return 0	       =======> reconfiguration of options has succeeded
 *   return 1	       =======> the graph has to be reconstructed and all the xlators should be inited
 *   return -1(or -ve) =======> Some Internal Error occurred during the operation
 */
static int
glusterfs_volfile_reconfigure (struct glfs *fs, FILE *newvolfile_fp)
{
	glusterfs_graph_t *oldvolfile_graph = NULL;
	glusterfs_graph_t *newvolfile_graph = NULL;
	FILE		  *oldvolfile_fp    = NULL;
	glusterfs_ctx_t	  *ctx		    = NULL;

	int ret = -1;

	oldvolfile_fp = tmpfile ();
	if (!oldvolfile_fp)
		goto out;

	if (!fs->oldvollen) {
		ret = 1; // Has to call INIT for the whole graph
		goto out;
	}
	fwrite (fs->oldvolfile, fs->oldvollen, 1, oldvolfile_fp);
	fflush (oldvolfile_fp);
	if (ferror (oldvolfile_fp)) {
		goto out;
	}

	oldvolfile_graph = glusterfs_graph_construct (oldvolfile_fp);
	if (!oldvolfile_graph) {
		goto out;
	}

	newvolfile_graph = glusterfs_graph_construct (newvolfile_fp);
	if (!newvolfile_graph) {
		goto out;
	}

	if (!is_graph_topology_equal (oldvolfile_graph,
				      newvolfile_graph)) {

		ret = 1;
		gf_log ("glfs-mgmt", GF_LOG_DEBUG,
			"Graph topology not equal(should call INIT)");
		goto out;
	}

	gf_log ("glfs-mgmt", GF_LOG_DEBUG,
		"Only options have changed in the new "
		"graph");

	ctx = fs->ctx;

	if (!ctx) {
		gf_log ("glfs-mgmt", GF_LOG_ERROR,
			"glusterfs_ctx_get() returned NULL");
		goto out;
	}

	oldvolfile_graph = ctx->active;

	if (!oldvolfile_graph) {
		gf_log ("glfs-mgmt", GF_LOG_ERROR,
			"glusterfs_ctx->active is NULL");
		goto out;
	}

	/* */
	ret = glusterfs_graph_reconfigure (oldvolfile_graph,
					   newvolfile_graph);
	if (ret) {
		gf_log ("glfs-mgmt", GF_LOG_DEBUG,
			"Could not reconfigure new options in old graph");
		goto out;
	}

	ret = 0;
out:
	if (oldvolfile_fp)
		fclose (oldvolfile_fp);

	return ret;
}


static int
glusterfs_oldvolfile_update (struct glfs *fs, char *volfile, ssize_t size)
{
	int ret = -1;

	fs->oldvollen = size;
	if (!fs->oldvolfile) {
		fs->oldvolfile = GF_CALLOC (1, size+1, glfs_mt_volfile_t);
	} else {
		fs->oldvolfile = GF_REALLOC (fs->oldvolfile, size+1);
	}

	if (!fs->oldvolfile) {
		fs->oldvollen = 0;
	} else {
		memcpy (fs->oldvolfile, volfile, size);
		fs->oldvollen = size;
		ret = 0;
	}

	return ret;
}


int
mgmt_getspec_cbk (struct rpc_req *req, struct iovec *iov, int count,
		  void *myframe)
{
	gf_getspec_rsp		 rsp   = {0,};
	call_frame_t		*frame = NULL;
	glusterfs_ctx_t		*ctx = NULL;
	int			 ret   = 0;
	ssize_t			 size = 0;
	FILE			*tmpfp = NULL;
	int                      need_retry = 0;
	struct glfs		*fs = NULL;

	frame = myframe;
	ctx = frame->this->ctx;
	fs = ((xlator_t *)ctx->master)->private;

	if (-1 == req->rpc_status) {
		ret = -1;
		need_retry = 1;
		goto out;
	}

	ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_getspec_rsp);
	if (ret < 0) {
		gf_log (frame->this->name, GF_LOG_ERROR, "XDR decoding error");
		ret   = -1;
		goto out;
	}

	if (-1 == rsp.op_ret) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"failed to get the 'volume file' from server");
		ret = -1;
                errno = rsp.op_errno;
		goto out;
	}

	ret = 0;
	size = rsp.op_ret;

	if ((size == fs->oldvollen) &&
	    (memcmp (fs->oldvolfile, rsp.spec, size) == 0)) {
		gf_log (frame->this->name, GF_LOG_INFO,
			"No change in volfile, continuing");
		goto out;
	}

	tmpfp = tmpfile ();
	if (!tmpfp) {
		ret = -1;
		goto out;
	}

	fwrite (rsp.spec, size, 1, tmpfp);
	fflush (tmpfp);
	if (ferror (tmpfp)) {
		ret = -1;
		goto out;
	}

	/*  Check if only options have changed. No need to reload the
	*  volfile if topology hasn't changed.
	*  glusterfs_volfile_reconfigure returns 3 possible return states
	*  return 0	     =======> reconfiguration of options has succeeded
	*  return 1	     =======> the graph has to be reconstructed and all the xlators should be inited
	*  return -1(or -ve) =======> Some Internal Error occurred during the operation
	*/

	ret = glusterfs_volfile_reconfigure (fs, tmpfp);
	if (ret == 0) {
		gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
			"No need to re-load volfile, reconfigure done");
		ret = glusterfs_oldvolfile_update (fs, rsp.spec, size);
		goto out;
	}

	if (ret < 0) {
		gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
			"Reconfigure failed !!");
		goto out;
	}

	ret = glfs_process_volfp (fs, tmpfp);
	/* tmpfp closed */
	tmpfp = NULL;
	if (ret)
		goto out;

	ret = glusterfs_oldvolfile_update (fs, rsp.spec, size);
out:
	STACK_DESTROY (frame->root);

	if (rsp.spec)
		free (rsp.spec);

        // Stop if server is running at an unsupported op-version
        if (ENOTSUP == ret) {
                gf_log ("mgmt", GF_LOG_ERROR, "Server is operating at an "
                        "op-version which is not supported");
                errno = ENOTSUP;
                glfs_init_done (fs, -1);
        }

	if (ret && ctx && !ctx->active) {
		/* Do it only for the first time */
		/* Failed to get the volume file, something wrong,
		   restart the process */
		gf_log ("glfs-mgmt", GF_LOG_ERROR,
			"failed to fetch volume file (key:%s)",
			ctx->cmd_args.volfile_id);
		if (!need_retry) {
                        if (!errno)
                                errno = EINVAL;
			glfs_init_done (fs, -1);
                }
	}

	if (tmpfp)
		fclose (tmpfp);

	return 0;
}


int
glfs_volfile_fetch (struct glfs *fs)
{
	cmd_args_t	 *cmd_args = NULL;
	gf_getspec_req	  req = {0, };
	int		  ret = 0;
	call_frame_t	 *frame = NULL;
	glusterfs_ctx_t	 *ctx = NULL;
        dict_t           *dict = NULL;

	ctx = fs->ctx;
	cmd_args = &ctx->cmd_args;

	frame = create_frame (THIS, ctx->pool);

	req.key = cmd_args->volfile_id;
	req.flags = 0;

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        // Set the supported min and max op-versions, so glusterd can make a
        // decision
        ret = dict_set_int32 (dict, "min-op-version", GD_OP_VERSION_MIN);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to set min-op-version"
                        " in request dict");
                goto out;
        }

        ret = dict_set_int32 (dict, "max-op-version", GD_OP_VERSION_MAX);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to set max-op-version"
                        " in request dict");
                goto out;
        }

        ret = dict_allocate_and_serialize (dict, &req.xdata.xdata_val,
                                           &req.xdata.xdata_len);
        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to serialize dictionary");
                goto out;
        }

	ret = mgmt_submit_request (&req, frame, ctx, &clnt_handshake_prog,
				   GF_HNDSK_GETSPEC, mgmt_getspec_cbk,
				   (xdrproc_t)xdr_gf_getspec_req);
out:
        return ret;
}


static int
mgmt_rpc_notify (struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
		 void *data)
{
	xlator_t	*this = NULL;
	cmd_args_t	*cmd_args = NULL;
	glusterfs_ctx_t *ctx = NULL;
	struct glfs	 *fs = NULL;
	int		 ret = 0;

	this = mydata;
	ctx = this->ctx;
	fs = ((xlator_t *)ctx->master)->private;
	cmd_args = &ctx->cmd_args;

	switch (event) {
	case RPC_CLNT_DISCONNECT:
		if (!ctx->active) {
			cmd_args->max_connect_attempts--;
			gf_log ("glfs-mgmt", GF_LOG_ERROR,
				"failed to connect with remote-host: %s",
				strerror (errno));
			gf_log ("glfs-mgmt", GF_LOG_INFO,
				"%d connect attempts left",
				cmd_args->max_connect_attempts);
			if (0 >= cmd_args->max_connect_attempts) {
                                errno = ENOTCONN;
				glfs_init_done (fs, -1);
                        }
		}
		break;
	case RPC_CLNT_CONNECT:
		rpc_clnt_set_connected (&((struct rpc_clnt*)ctx->mgmt)->conn);

		ret = glfs_volfile_fetch (fs);
		if (ret && ctx && (ctx->active == NULL)) {
			/* Do it only for the first time */
			/* Exit the process.. there are some wrong options */
			gf_log ("glfs-mgmt", GF_LOG_ERROR,
				"failed to fetch volume file (key:%s)",
				ctx->cmd_args.volfile_id);
                        errno = EINVAL;
			glfs_init_done (fs, -1);
		}

		break;
	default:
		break;
	}

	return 0;
}


int
glusterfs_mgmt_notify (int32_t op, void *data, ...)
{
	int ret = 0;

	switch (op)
	{
		case GF_EN_DEFRAG_STATUS:
			break;

		default:
			break;
	}

	return ret;
}


int
glfs_mgmt_init (struct glfs *fs)
{
	cmd_args_t		*cmd_args = NULL;
	struct rpc_clnt		*rpc = NULL;
	dict_t			*options = NULL;
	int			ret = -1;
	int			port = GF_DEFAULT_BASE_PORT;
	char			*host = NULL;
	glusterfs_ctx_t		*ctx = NULL;

	ctx = fs->ctx;
	cmd_args = &ctx->cmd_args;

	if (ctx->mgmt)
		return 0;

	if (cmd_args->volfile_server_port)
		port = cmd_args->volfile_server_port;

	host = "localhost";
	if (cmd_args->volfile_server)
		host = cmd_args->volfile_server;

	ret = rpc_transport_inet_options_build (&options, host, port);
	if (ret)
		goto out;

	rpc = rpc_clnt_new (options, THIS->ctx, THIS->name, 8);
	if (!rpc) {
		ret = -1;
		gf_log (THIS->name, GF_LOG_WARNING,
			"failed to create rpc clnt");
		goto out;
	}

	ret = rpc_clnt_register_notify (rpc, mgmt_rpc_notify, THIS);
	if (ret) {
		gf_log (THIS->name, GF_LOG_WARNING,
			"failed to register notify function");
		goto out;
	}

	ret = rpcclnt_cbk_program_register (rpc, &mgmt_cbk_prog, THIS);
	if (ret) {
		gf_log (THIS->name, GF_LOG_WARNING,
			"failed to register callback function");
		goto out;
	}

	ctx->notify = glusterfs_mgmt_notify;

	/* This value should be set before doing the 'rpc_clnt_start()' as
	   the notify function uses this variable */
	ctx->mgmt = rpc;

	ret = rpc_clnt_start (rpc);
out:
	return ret;
}

