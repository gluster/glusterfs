/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "snapview-server.h"
#include "snapview-server-mem-types.h"
#include <pthread.h>

int
mgmt_cbk_snap (struct rpc_clnt *rpc, void *mydata, void *data)
{
        xlator_t *this = NULL;

        this = mydata;
        GF_ASSERT (this);

        gf_log ("mgmt", GF_LOG_INFO, "list of snapshots changed");

        svs_get_snapshot_list (this);
        return 0;
}

rpcclnt_cb_actor_t svs_cbk_actors[GF_CBK_MAXVALUE] = {
        [GF_CBK_GET_SNAPS] = {"GETSNAPS", GF_CBK_GET_SNAPS, mgmt_cbk_snap},
};

struct rpcclnt_cb_program svs_cbk_prog = {
        .progname  = "GlusterFS Callback",
        .prognum   = GLUSTER_CBK_PROGRAM,
        .progver   = GLUSTER_CBK_VERSION,
        .actors    = svs_cbk_actors,
        .numactors = GF_CBK_MAXVALUE,
};

char *clnt_handshake_procs[GF_HNDSK_MAXVALUE] = {
        [GF_HNDSK_NULL]         = "NULL",
        [GF_HNDSK_EVENT_NOTIFY] = "EVENTNOTIFY",
};

rpc_clnt_prog_t svs_clnt_handshake_prog = {
        .progname  = "GlusterFS Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .procnames = clnt_handshake_procs,
};

int
svs_mgmt_init (xlator_t *this)
{
        int              ret      = -1;
        svs_private_t   *priv     = NULL;
        dict_t          *options  = NULL;
        int              port     = GF_DEFAULT_BASE_PORT;
        char            *host     = NULL;
        cmd_args_t      *cmd_args = NULL;
        glusterfs_ctx_t *ctx      = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, this->ctx, out);

        priv = this->private;

        ctx = this->ctx;
        cmd_args = &ctx->cmd_args;

        host = "localhost";
        if (cmd_args->volfile_server)
                host = cmd_args->volfile_server;

        ret = rpc_transport_inet_options_build (&options, host, port);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to build the "
                        "transport options");
                goto out;
        }

        priv->rpc = rpc_clnt_new (options, this, this->name, 8);
        if (!priv->rpc) {
                gf_log (this->name, GF_LOG_ERROR, "failed to initialize RPC");
                goto out;
        }

        ret = rpcclnt_cbk_program_register (priv->rpc, &svs_cbk_prog,
                                            this);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to register callback program");
                goto out;
        }

        ret = rpc_clnt_start (priv->rpc);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to start the rpc "
                        "client");
                goto out;
        }

        ret = 0;

        gf_log (this->name, GF_LOG_DEBUG, "svs mgmt init successful");

out:
        if (ret)
                if (priv) {
                        rpc_clnt_connection_cleanup (&priv->rpc->conn);
                        rpc_clnt_unref (priv->rpc);
                        priv->rpc = NULL;
                }

        return ret;
}

int
svs_mgmt_submit_request (void *req, call_frame_t *frame,
                         glusterfs_ctx_t *ctx,
                         rpc_clnt_prog_t *prog, int procnum,
                         fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
        int                     ret        = -1;
        int                     count      = 0;
        struct iovec            iov        = {0, };
        struct iobuf            *iobuf     = NULL;
        struct iobref           *iobref    = NULL;
        ssize_t                 xdr_size   = 0;

        GF_VALIDATE_OR_GOTO ("snapview-server", frame, out);
        GF_VALIDATE_OR_GOTO ("snapview-server", req, out);
        GF_VALIDATE_OR_GOTO ("snapview-server", ctx, out);
        GF_VALIDATE_OR_GOTO ("snapview-server", prog, out);

        GF_ASSERT (frame->this);

        iobref = iobref_new ();
        if (!iobref) {
                goto out;
        }

        if (req) {
                xdr_size = xdr_sizeof (xdrproc, req);

                iobuf = iobuf_get2 (ctx->iobuf_pool, xdr_size);
                if (!iobuf) {
                        goto out;
                }

                iobref_add (iobref, iobuf);

                iov.iov_base = iobuf->ptr;
                iov.iov_len  = iobuf_pagesize (iobuf);

                /* Create the xdr payload */
                ret = xdr_serialize_generic (iov, req, xdrproc);
                if (ret == -1) {
                        gf_log (frame->this->name, GF_LOG_WARNING,
                                "Failed to create XDR payload");
                        goto out;
                }
                iov.iov_len = ret;
                count = 1;
        }

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


int
mgmt_get_snapinfo_cbk (struct rpc_req *req, struct iovec *iov,
                       int count, void *myframe)
{
        gf_getsnap_name_uuid_rsp        rsp             = {0,};
        call_frame_t                    *frame          = NULL;
        glusterfs_ctx_t                 *ctx            = NULL;
        int                             ret             = -1;
        dict_t                          *dict           = NULL;
        char                            key[1024]       = {0};
        int                             snapcount       = 0;
        svs_private_t                   *priv           = NULL;
        xlator_t                        *this           = NULL;
        int                             i               = 0;
        int                             j               = 0;
        char                            *value          = NULL;
        snap_dirent_t                   *dirents        = NULL;
        snap_dirent_t                   *old_dirents    = NULL;
        int                             oldcount        = 0;

        GF_VALIDATE_OR_GOTO ("snapview-server", req, error_out);
        GF_VALIDATE_OR_GOTO ("snapview-server", myframe, error_out);
        GF_VALIDATE_OR_GOTO ("snapview-server", iov, error_out);

        frame       = myframe;
        this        = frame->this;
        ctx         = frame->this->ctx;
        priv        = this->private;
        old_dirents = priv->dirents;

        if (!ctx) {
                gf_log (frame->this->name, GF_LOG_ERROR, "NULL context");
                errno = EINVAL;
                goto out;
        }

        if (-1 == req->rpc_status) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "RPC call is not successful");
                errno = EINVAL;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gf_getsnap_name_uuid_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response, rsp.op_ret = %d",
                        rsp.op_ret);
                goto out;
        }

        if (rsp.op_ret == -1) {
                errno = rsp.op_errno;
                ret = -1;
                goto out;
        }

        if (!rsp.dict.dict_len) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Response dict is not populated");
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

        ret = dict_unserialize (rsp.dict.dict_val, rsp.dict.dict_len, &dict);
        if (ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to unserialize dictionary");
                errno = EINVAL;
                goto out;
        }

        ret = dict_get_int32 (dict, "snap-count", (int32_t*)&snapcount);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error retrieving snapcount");
                        errno = EINVAL;
                        ret = -1;
                        goto out;
        }

        if (snapcount > 0) {
                /* first time we are fetching snap list */
                dirents = GF_CALLOC (snapcount, sizeof (snap_dirent_t),
                                     gf_svs_mt_dirents_t);
                if (!dirents) {
                        gf_log (frame->this->name, GF_LOG_ERROR,
                                "Unable to allocate memory");
                        errno = ENOMEM;
                        ret = -1;
                        goto out;
                }
        }

        for (i = 0; i < snapcount; i++) {
                snprintf (key, sizeof (key), "snap-volname.%d", i+1);
                ret = dict_get_str (dict, key, &value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Error retrieving snap volname %d",
                                i+1);
                        errno = EINVAL;
                        ret = -1;
                        goto out;
                }

                strncpy (dirents[i].snap_volname, value,
                         sizeof (dirents[i].snap_volname));

                snprintf (key, sizeof (key), "snap-id.%d", i+1);
                ret = dict_get_str (dict, key, &value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Error retrieving snap uuid %d", i+1);
                        errno = EINVAL;
                        ret = -1;
                        goto out;
                }
                strncpy (dirents[i].uuid, value,
                         sizeof (dirents[i].uuid));

                snprintf (key, sizeof (key), "snapname.%d", i+1);
                ret = dict_get_str (dict, key, &value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Error retrieving snap name %d", i+1);
                        errno = EINVAL;
                        ret = -1;
                        goto out;
                }
                strncpy (dirents[i].name, value,
                         sizeof (dirents[i].name));
        }

        /*
         * Got the new snap list populated in dirents
         * The new snap list is either a subset or a superset of
         * the existing snaplist old_dirents which has priv->num_snaps
         * number of entries.
         *
         * If subset, then clean up the fs for entries which are
         * no longer relevant.
         *
         * For other overlapping entries set the fs for new dirents
         * entries which have a fs assigned already in old_dirents
         *
         * We do this as we don't want to do new glfs_init()s repeatedly
         * as the dirents entries for snapshot volumes get repatedly
         * cleaned up and allocated. And if we don't then that will lead
         * to memleaks
         */

        LOCK (&priv->snaplist_lock);
        {
                oldcount = priv->num_snaps;
                for (i = 0; i < priv->num_snaps; i++) {
                        for (j = 0; j < snapcount; j++) {
                                if ((!strcmp (old_dirents[i].name,
                                              dirents[j].name)) &&
                                    (!strcmp (old_dirents[i].uuid,
                                              dirents[j].uuid)))    {
                                        dirents[j].fs = old_dirents[i].fs;
                                        old_dirents[i].fs = NULL;
                                        break;
                                }
                        }
                }

                priv->dirents = dirents;
                priv->num_snaps = snapcount;
        }
        UNLOCK (&priv->snaplist_lock);

        if (old_dirents) {
                for (i = 0; i < oldcount; i++) {
                        if (old_dirents[i].fs)
                                glfs_fini (old_dirents[i].fs);
                }
        }

        GF_FREE (old_dirents);

        ret = 0;

out:
        if (dict) {
                dict_unref (dict);
        }
        free (rsp.dict.dict_val);
        free (rsp.op_errstr);

        if (ret && dirents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Could not update dirents with refreshed snap list");
                GF_FREE (dirents);
        }

        if (myframe)
                SVS_STACK_DESTROY (myframe);

error_out:
        return ret;
}

int
svs_get_snapshot_list (xlator_t *this)
{
        gf_getsnap_name_uuid_req        req             = {{0,}};
        int                             ret             = -1;
        dict_t                          *dict           = NULL;
        glusterfs_ctx_t                 *ctx            = NULL;
        call_frame_t                    *frame          = NULL;
        svs_private_t                   *priv           = NULL;
        gf_boolean_t                    frame_cleanup   = _gf_true;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);

        ctx  = this->ctx;
        if (!ctx) {
                gf_log (this->name, GF_LOG_ERROR,
                        "ctx is NULL");
                goto out;
        }

        frame = create_frame (this, ctx->pool);
        if (!frame) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error allocating frame");
                goto out;
        }

        priv = this->private;

        dict = dict_new ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error allocating dictionary");
                goto out;
        }

        ret = dict_set_str (dict, "volname", priv->volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error setting volname in dict");
                goto out;
        }

        ret = dict_allocate_and_serialize (dict, &req.dict.dict_val,
                                           &req.dict.dict_len);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to serialize dictionary");
                ret = -1;
                goto out;
        }

        ret = svs_mgmt_submit_request (&req, frame, ctx,
                                       &svs_clnt_handshake_prog,
                                       GF_HNDSK_GET_SNAPSHOT_INFO,
                                       mgmt_get_snapinfo_cbk,
                                       (xdrproc_t)xdr_gf_getsnap_name_uuid_req);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error sending snapshot names RPC request");
        }

        frame_cleanup = _gf_false;

out:
        if (dict) {
                dict_unref (dict);
        }
        GF_FREE (req.dict.dict_val);

        if (frame_cleanup && frame) {
                /*
                 * Destroy the frame if we encountered an error
                 * Else we need to clean it up in
                 * mgmt_get_snapinfo_cbk
                 */
                SVS_STACK_DESTROY (frame);
        }

        return ret;
}
