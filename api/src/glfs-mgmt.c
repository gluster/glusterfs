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

#include "glusterfs.h"
#include "glfs.h"
#include "stack.h"
#include "dict.h"
#include "event.h"
#include "defaults.h"

#include "rpc-clnt.h"
#include "protocol-common.h"
#include "glusterfs3.h"
#include "portmap-xdr.h"
#include "xdr-common.h"
#include "xdr-generic.h"
#include "rpc-common-xdr.h"

#include "syncop.h"
#include "xlator.h"

#include "glfs-internal.h"
#include "glfs-mem-types.h"
#include "gfapi-messages.h"
#include "syscall.h"

int glfs_volfile_fetch (struct glfs *fs);
int32_t glfs_get_volume_info_rpc (call_frame_t *frame, xlator_t *this,
                                  struct glfs *fs);

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
		gf_msg ("glfs", GF_LOG_ERROR, errno,
                        API_MSG_GRAPH_CONSTRUCT_FAILED,
                        "failed to construct the graph");
		goto out;
	}

	for (trav = graph->first; trav; trav = trav->next) {
		if (strcmp (trav->type, "mount/fuse") == 0) {
			gf_msg ("glfs", GF_LOG_ERROR, EINVAL,
                                API_MSG_FUSE_XLATOR_ERROR,
				"fuse xlator cannot be specified "
				"in volume file");
			goto out;
		}
	}

	ret = glusterfs_graph_prepare (graph, ctx, fs->volname);
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

static int
mgmt_cbk_statedump (struct rpc_clnt *rpc, void *mydata, void *data)
{
        struct glfs      *fs          = NULL;
        xlator_t         *this        = NULL;
        gf_statedump      target_pid  = {0, };
        struct iovec     *iov         = NULL;
        int               ret         = -1;

        this = mydata;
        if (!this) {
                gf_msg ("glfs", GF_LOG_ERROR, EINVAL,
                        API_MSG_STATEDUMP_FAILED, "NULL mydata");
                errno = EINVAL;
                goto out;
        }

        fs = this->private;
        if (!fs) {
                gf_msg ("glfs", GF_LOG_ERROR, EINVAL,
                        API_MSG_STATEDUMP_FAILED, "NULL glfs");
                errno = EINVAL;
                goto out;
        }

        iov = (struct iovec *)data;
        if (!iov) {
                gf_msg ("glfs", GF_LOG_ERROR, EINVAL,
                        API_MSG_STATEDUMP_FAILED, "NULL iovec data");
                errno = EINVAL;
                goto out;
        }

        ret = xdr_to_generic (*iov, &target_pid,
                              (xdrproc_t)xdr_gf_statedump);
        if (ret < 0) {
                gf_msg ("glfs", GF_LOG_ERROR, EINVAL,
                        API_MSG_STATEDUMP_FAILED,
                        "Failed to decode xdr response for GF_CBK_STATEDUMP");
                goto out;
        }

        gf_msg_trace ("glfs", 0, "statedump requested for pid: %d",
                      target_pid.pid);

        if ((uint64_t)getpid() == target_pid.pid) {
                gf_msg_debug ("glfs", 0, "Taking statedump for pid: %d",
                              target_pid.pid);

                ret = glfs_sysrq (fs, GLFS_SYSRQ_STATEDUMP);
                if (ret < 0) {
                        gf_msg ("glfs", GF_LOG_INFO, 0,
                                API_MSG_STATEDUMP_FAILED,
                                "statedump failed");
                }
        }
out:
        return ret;
}

rpcclnt_cb_actor_t mgmt_cbk_actors[GF_CBK_MAXVALUE] = {
	[GF_CBK_FETCHSPEC] = {"FETCHSPEC", GF_CBK_FETCHSPEC, mgmt_cbk_spec },
	[GF_CBK_EVENT_NOTIFY] = {"EVENTNOTIFY", GF_CBK_EVENT_NOTIFY,
				 mgmt_cbk_event},
        [GF_CBK_STATEDUMP] = {"STATEDUMP", GF_CBK_STATEDUMP, mgmt_cbk_statedump},
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
        [GF_HNDSK_GET_VOLUME_INFO] = "GETVOLUMEINFO",
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
			gf_msg (THIS->name, GF_LOG_WARNING, 0,
                                API_MSG_XDR_PAYLOAD_FAILED,
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

/*
 * Callback routine for 'GF_HNDSK_GET_VOLUME_INFO' rpc request
 */
int
mgmt_get_volinfo_cbk (struct rpc_req *req, struct iovec *iov,
                      int count, void *myframe)
{
        int                        ret                  = 0;
        char                       *volume_id_str       = NULL;
        dict_t                     *dict                = NULL;
        char                       key[1024]            = {0};
        gf_get_volume_info_rsp     rsp                  = {0,};
        call_frame_t               *frame               = NULL;
        glusterfs_ctx_t            *ctx                 = NULL;
        struct glfs                *fs                  = NULL;
        struct syncargs            *args;

        frame = myframe;
        ctx = frame->this->ctx;
        args = frame->local;

        if (!ctx) {
                gf_msg (frame->this->name, GF_LOG_ERROR, EINVAL,
                        API_MSG_INVALID_ENTRY, "NULL context");
                errno = EINVAL;
                ret = -1;
                goto out;
        }

        fs = ((xlator_t *)ctx->master)->private;

        if (-1 == req->rpc_status) {
                gf_msg (frame->this->name, GF_LOG_ERROR, EINVAL,
                        API_MSG_INVALID_ENTRY,
                        "GET_VOLUME_INFO RPC call is not successful");
                errno = EINVAL;
                ret = -1;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_get_volume_info_rsp);

        if (ret < 0) {
                gf_msg (frame->this->name, GF_LOG_ERROR, 0,
                        API_MSG_XDR_RESPONSE_DECODE_FAILED,
                        "Failed to decode xdr response for GET_VOLUME_INFO");
                goto out;
        }

        gf_msg_debug (frame->this->name, 0, "Received resp to GET_VOLUME_INFO "
                      "RPC: %d", rsp.op_ret);

        if (rsp.op_ret == -1) {
                errno = rsp.op_errno;
                ret = -1;
                goto out;
        }

        if (!rsp.dict.dict_len) {
                gf_msg (frame->this->name, GF_LOG_ERROR, EINVAL,
                        API_MSG_INVALID_ENTRY, "Response received for "
                        "GET_VOLUME_INFO RPC call is not valid");
                ret = -1;
                errno = EINVAL;
                goto out;
        }

        dict = dict_new ();

        if (!dict) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        ret = dict_unserialize (rsp.dict.dict_val,
                                rsp.dict.dict_len,
                                &dict);

        if (ret) {
                errno = ENOMEM;
                goto out;
        }

        snprintf (key, sizeof (key), "volume_id");
        ret = dict_get_str (dict, key, &volume_id_str);
        if (ret) {
                errno = EINVAL;
                goto out;
        }

        ret = 0;
out:
        if (volume_id_str) {
                gf_msg_debug (frame->this->name, 0,
                              "Volume Id: %s", volume_id_str);
                pthread_mutex_lock (&fs->mutex);
                gf_uuid_parse (volume_id_str, fs->vol_uuid);
                pthread_mutex_unlock (&fs->mutex);
        }

        if (ret) {
                gf_msg (frame->this->name, GF_LOG_ERROR, errno,
                        API_MSG_GET_VOLINFO_CBK_FAILED, "In GET_VOLUME_INFO "
                        "cbk, received error: %s", strerror(errno));
        }

        if (dict)
                dict_unref (dict);

        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);

        if (rsp.op_errstr && *rsp.op_errstr)
                free (rsp.op_errstr);

        gf_msg_debug (frame->this->name, 0, "Returning: %d", ret);

        __wake (args);

        return ret;
}

int
pub_glfs_get_volumeid (struct glfs *fs, char *volid, size_t size)
{
        /* TODO: Define a global macro to store UUID size */
        size_t uuid_size = 16;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        pthread_mutex_lock (&fs->mutex);
        {
                /* check if the volume uuid is initialized */
                if (!gf_uuid_is_null (fs->vol_uuid)) {
                        pthread_mutex_unlock (&fs->mutex);
                        goto done;
                }
        }
        pthread_mutex_unlock (&fs->mutex);

        /* Need to fetch volume_uuid */
        glfs_get_volume_info (fs);

        if (gf_uuid_is_null (fs->vol_uuid)) {
                gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                        API_MSG_FETCH_VOLUUID_FAILED, "Unable to fetch "
                        "volume UUID");
                goto out;
        }

done:
        if (!volid || !size) {
                gf_msg_debug (THIS->name, 0, "volumeid/size is null");
                __GLFS_EXIT_FS;
                return uuid_size;
        }

        if (size < uuid_size) {
                gf_msg (THIS->name, GF_LOG_ERROR, ERANGE, API_MSG_INSUFF_SIZE,
                        "Insufficient size passed");
                errno = ERANGE;
                goto out;
        }

        memcpy (volid, fs->vol_uuid, uuid_size);

        __GLFS_EXIT_FS;

        return uuid_size;

out:
        __GLFS_EXIT_FS;

invalid_fs:
        return -1;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_get_volumeid, 3.5.0);

int
glfs_get_volume_info (struct glfs *fs)
{
        call_frame_t     *frame = NULL;
        glusterfs_ctx_t  *ctx   = NULL;
        struct syncargs  args   = {0, };
        int              ret    = 0;

        ctx = fs->ctx;
        frame = create_frame (THIS, ctx->pool);
        if (!frame) {
                gf_msg ("glfs", GF_LOG_ERROR, ENOMEM,
                        API_MSG_FRAME_CREAT_FAILED,
                        "failed to create the frame");
                ret = -1;
                goto out;
        }

        frame->local = &args;

        __yawn ((&args));

        ret = glfs_get_volume_info_rpc (frame, THIS, fs);
        if (ret)
                goto out;

        __yield ((&args));

        frame->local = NULL;
        STACK_DESTROY (frame->root);

out:
        return ret;
}

int32_t
glfs_get_volume_info_rpc (call_frame_t *frame, xlator_t *this,
                          struct glfs *fs)
{
        gf_get_volume_info_req  req       = {{0,}};
        int                     ret       = 0;
        glusterfs_ctx_t         *ctx      = NULL;
        dict_t                  *dict     = NULL;
        int32_t                 flags     = 0;

        if (!frame || !this || !fs) {
                ret = -1;
                goto out;
        }

        ctx = fs->ctx;

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        if (fs->volname) {
                ret = dict_set_str (dict, "volname", fs->volname);
                if (ret)
                        goto out;
        }

        // Set the flags for the fields which we are interested in
        flags = (int32_t)GF_GET_VOLUME_UUID; //ctx->flags;
        ret = dict_set_int32 (dict, "flags", flags);
        if (ret) {
                gf_msg (frame->this->name, GF_LOG_ERROR, EINVAL,
                        API_MSG_DICT_SET_FAILED, "failed to set flags");
                goto out;
        }

        ret = dict_allocate_and_serialize (dict, &req.dict.dict_val,
                                           &req.dict.dict_len);


        ret = mgmt_submit_request (&req, frame, ctx, &clnt_handshake_prog,
                                   GF_HNDSK_GET_VOLUME_INFO,
                                   mgmt_get_volinfo_cbk,
                                   (xdrproc_t)xdr_gf_get_volume_info_req);
out:
        if (dict) {
                dict_unref (dict);
        }

        GF_FREE (req.dict.dict_val);

        return ret;
}

static int
glusterfs_oldvolfile_update (struct glfs *fs, char *volfile, ssize_t size)
{
	int ret = -1;

        pthread_mutex_lock (&fs->mutex);

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

        pthread_mutex_unlock (&fs->mutex);

	return ret;
}


int
glfs_mgmt_getspec_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
	gf_getspec_rsp		 rsp   = {0,};
	call_frame_t		*frame = NULL;
	glusterfs_ctx_t		*ctx = NULL;
	int			 ret   = 0;
	ssize_t			 size = 0;
	FILE			*tmpfp = NULL;
	int			 need_retry = 0;
	struct glfs		*fs = NULL;

	frame = myframe;
	ctx = frame->this->ctx;

	if (!ctx) {
		gf_msg (frame->this->name, GF_LOG_ERROR, EINVAL,
                        API_MSG_INVALID_ENTRY, "NULL context");
		errno = EINVAL;
		ret = -1;
		goto out;
	}

	fs = ((xlator_t *)ctx->master)->private;

	if (-1 == req->rpc_status) {
		ret = -1;
		need_retry = 1;
		goto out;
	}

	ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_getspec_rsp);
	if (ret < 0) {
		gf_msg (frame->this->name, GF_LOG_ERROR, 0,
                        API_MSG_XDR_DECODE_FAILED, "XDR decoding error");
		ret   = -1;
		goto out;
	}

	if (-1 == rsp.op_ret) {
		gf_msg (frame->this->name, GF_LOG_ERROR, rsp.op_errno,
                        API_MSG_GET_VOLFILE_FAILED,
			"failed to get the 'volume file' from server");
		ret = -1;
		errno = rsp.op_errno;
		goto out;
	}

	ret = 0;
	size = rsp.op_ret;

	if ((size == fs->oldvollen) &&
	    (memcmp (fs->oldvolfile, rsp.spec, size) == 0)) {
		gf_msg (frame->this->name, GF_LOG_INFO, 0,
                        API_MSG_VOLFILE_INFO,
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

	ret = glusterfs_volfile_reconfigure (fs->oldvollen, tmpfp, fs->ctx,
					     fs->oldvolfile);
	if (ret == 0) {
		gf_msg_debug ("glusterfsd-mgmt", 0, "No need to re-load "
                              "volfile, reconfigure done");
		ret = glusterfs_oldvolfile_update (fs, rsp.spec, size);
		goto out;
	}

	if (ret < 0) {
		gf_msg_debug ("glusterfsd-mgmt", 0, "Reconfigure failed !!");
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
		gf_msg ("mgmt", GF_LOG_ERROR, ENOTSUP, API_MSG_WRONG_OPVERSION,
                        "Server is operating at an op-version which is not "
                        "supported");
		errno = ENOTSUP;
		glfs_init_done (fs, -1);
	}

	if (ret && ctx && !ctx->active) {
		/* Do it only for the first time */
		/* Failed to get the volume file, something wrong,
		   restart the process */
		gf_msg ("glfs-mgmt", GF_LOG_ERROR, EINVAL,
                        API_MSG_INVALID_ENTRY,
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
                gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                        API_MSG_DICT_SET_FAILED,
                        "Failed to set min-op-version in request dict");
                goto out;
        }

        ret = dict_set_int32 (dict, "max-op-version", GD_OP_VERSION_MAX);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                        API_MSG_DICT_SET_FAILED,
                        "Failed to set max-op-version in request dict");
                goto out;
        }

        ret = dict_allocate_and_serialize (dict, &req.xdata.xdata_val,
                                           &req.xdata.xdata_len);
        if (ret < 0) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        API_MSG_DICT_SERIALIZE_FAILED,
                        "Failed to serialize dictionary");
                goto out;
        }

	ret = mgmt_submit_request (&req, frame, ctx, &clnt_handshake_prog,
				   GF_HNDSK_GETSPEC, glfs_mgmt_getspec_cbk,
				   (xdrproc_t)xdr_gf_getspec_req);
out:
        if (dict)
                dict_unref (dict);

        return ret;
}


static int
mgmt_rpc_notify (struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
		 void *data)
{
	xlator_t	*this = NULL;
	glusterfs_ctx_t *ctx = NULL;
        server_cmdline_t *server = NULL;
        rpc_transport_t  *rpc_trans = NULL;
	struct glfs	 *fs = NULL;
	int		 ret = 0;
	struct dnscache6 *dnscache = NULL;

	this = mydata;
        rpc_trans = rpc->conn.trans;

	ctx = this->ctx;
	if (!ctx)
		goto out;

	fs = ((xlator_t *)ctx->master)->private;

	switch (event) {
	case RPC_CLNT_DISCONNECT:
		if (!ctx->active) {
                        gf_msg ("glfs-mgmt", GF_LOG_ERROR, errno,
                                API_MSG_REMOTE_HOST_CONN_FAILED,
                                "failed to connect with remote-host: %s (%s)",
                                ctx->cmd_args.volfile_server,
                                strerror (errno));

                        if (!rpc->disabled) {
                                /*
                                 * Check if dnscache is exhausted for current server
                                 * and continue until cache is exhausted
                                 */
                                dnscache = rpc_trans->dnscache;
                                if (dnscache && dnscache->next) {
                                        break;
                                }
                        }
                        server = ctx->cmd_args.curr_server;
                        if (server->list.next == &ctx->cmd_args.volfile_servers) {
                                errno = ENOTCONN;
                                gf_msg ("glfs-mgmt", GF_LOG_INFO, ENOTCONN,
                                        API_MSG_VOLFILE_SERVER_EXHAUST,
                                        "Exhausted all volfile servers");
                                glfs_init_done (fs, -1);
                                break;
                        }
                        server = list_entry (server->list.next, typeof(*server),
                                             list);
                        ctx->cmd_args.curr_server = server;
                        ctx->cmd_args.volfile_server_port = server->port;
                        ctx->cmd_args.volfile_server = server->volfile_server;
                        ctx->cmd_args.volfile_server_transport = server->transport;

                        ret = dict_set_str (rpc_trans->options,
                                            "transport-type",
                                            server->transport);
                        if (ret != 0) {
                                gf_msg ("glfs-mgmt", GF_LOG_ERROR, ENOTCONN,
                                        API_MSG_DICT_SET_FAILED,
                                        "failed to set transport-type: %s",
                                        server->transport);
                                errno = ENOTCONN;
                                glfs_init_done (fs, -1);
                                break;
                        }

                        if (strcmp(server->transport, "unix") == 0) {
                                ret = dict_set_str (rpc_trans->options,
                                                    "transport.socket.connect-path",
                                                    server->volfile_server);
                                if (ret != 0) {
                                        gf_msg ("glfs-mgmt", GF_LOG_ERROR,
                                                ENOTCONN,
                                                API_MSG_DICT_SET_FAILED,
                                                "failed to set socket.connect-path: %s",
                                                server->volfile_server);
                                        errno = ENOTCONN;
                                        glfs_init_done (fs, -1);
                                        break;
                                }
                                /* delete the remote-host and remote-port keys
                                 * in case they were set while looping through
                                 * list of volfile servers previously
                                 */
                                dict_del (rpc_trans->options, "remote-host");
                                dict_del (rpc_trans->options, "remote-port");
                        } else {
                                ret = dict_set_int32 (rpc_trans->options,
                                                      "remote-port",
                                                      server->port);
                                if (ret != 0) {
                                        gf_msg ("glfs-mgmt", GF_LOG_ERROR,
                                                ENOTCONN,
                                                API_MSG_DICT_SET_FAILED,
                                                "failed to set remote-port: %d",
                                                server->port);
                                        errno = ENOTCONN;
                                        glfs_init_done (fs, -1);
                                        break;
                                }

                                ret = dict_set_str (rpc_trans->options,
                                                    "remote-host",
                                                    server->volfile_server);
                                if (ret != 0) {
                                        gf_msg ("glfs-mgmt", GF_LOG_ERROR,
                                                ENOTCONN,
                                                API_MSG_DICT_SET_FAILED,
                                                "failed to set remote-host: %s",
                                                server->volfile_server);
                                        errno = ENOTCONN;
                                        glfs_init_done (fs, -1);
                                        break;
                                }
                                /* delete the "transport.socket.connect-path"
                                 * key in case if it was set while looping
                                 * through list of volfile servers previously
                                 */
                                dict_del (rpc_trans->options,
                                          "transport.socket.connect-path");
                        }

                        gf_msg ("glfs-mgmt", GF_LOG_INFO, 0,
                                API_MSG_VOLFILE_CONNECTING,
                                "connecting to next volfile server %s"
                                " at port %d with transport: %s",
                                server->volfile_server, server->port,
                                server->transport);
                }
                break;
	case RPC_CLNT_CONNECT:
		rpc_clnt_set_connected (&((struct rpc_clnt*)ctx->mgmt)->conn);

		ret = glfs_volfile_fetch (fs);
		if (ret && (ctx->active == NULL)) {
			/* Do it only for the first time */
			/* Exit the process.. there are some wrong options */
			gf_msg ("glfs-mgmt", GF_LOG_ERROR, EINVAL,
                                API_MSG_INVALID_ENTRY,
				"failed to fetch volume file (key:%s)",
                                ctx->cmd_args.volfile_id);
                        errno = EINVAL;
                        glfs_init_done (fs, -1);
                }

                break;
	default:
		break;
	}
out:
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

	if (cmd_args->volfile_server) {
		host = cmd_args->volfile_server;
        } else if (cmd_args->volfile_server_transport &&
                   !strcmp (cmd_args->volfile_server_transport, "unix")) {
                host = DEFAULT_GLUSTERD_SOCKFILE;
        } else {
                host = "localhost";
        }

        if (!strcmp (cmd_args->volfile_server_transport, "unix")) {
                ret = rpc_transport_unix_options_build (&options, host, 0);
        } else {
                ret = rpc_transport_inet_options_build (&options, host, port);
        }

	if (ret)
		goto out;

        if (sys_access (SECURE_ACCESS_FILE, F_OK) == 0) {
                ctx->secure_mgmt = 1;
        }

	rpc = rpc_clnt_new (options, THIS, THIS->name, 8);
	if (!rpc) {
		ret = -1;
		gf_msg (THIS->name, GF_LOG_WARNING, 0,
                        API_MSG_CREATE_RPC_CLIENT_FAILED,
			"failed to create rpc clnt");
		goto out;
	}

	ret = rpc_clnt_register_notify (rpc, mgmt_rpc_notify, THIS);
	if (ret) {
		gf_msg (THIS->name, GF_LOG_WARNING, 0,
                        API_MSG_REG_NOTIFY_FUNC_FAILED,
			"failed to register notify function");
		goto out;
	}

	ret = rpcclnt_cbk_program_register (rpc, &mgmt_cbk_prog, THIS);
	if (ret) {
		gf_msg (THIS->name, GF_LOG_WARNING, 0,
                        API_MSG_REG_CBK_FUNC_FAILED,
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
