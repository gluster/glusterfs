/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "defaults.h"
#include "glusterfs.h"
#include "compat-errno.h"

#include "glusterd.h"
#include "glusterd-utils.h"
#include "glusterd-op-sm.h"

#include "glusterfs3.h"
#include "protocol-common.h"
#include "rpcsvc.h"
#include "rpc-common-xdr.h"

extern struct rpc_clnt_program gd_peer_prog;
extern struct rpc_clnt_program gd_mgmt_prog;

#define TRUSTED_PREFIX         "trusted-"

typedef ssize_t (*gfs_serialize_t) (struct iovec outmsg, void *data);

static size_t
build_volfile_path (const char *volname, char *path,
                    size_t path_len, char *trusted_str)
{
        struct stat         stbuf       = {0,};
        int32_t             ret         = -1;
        glusterd_conf_t    *priv        = NULL;
        char               *vol         = NULL;
        char               *dup_volname = NULL;
        char               *free_ptr    = NULL;
        char               *tmp         = NULL;
        glusterd_volinfo_t *volinfo     = NULL;
        char               *server      = NULL;

        priv    = THIS->private;

        if (strstr (volname, "gluster/")) {
                server = strchr (volname, '/') + 1;
                glusterd_get_nodesvc_volfile (server, priv->workdir,
                                              path, path_len);
                ret = 1;
                goto out;
        } else if (volname[0] != '/') {
                /* Normal behavior */
                dup_volname = gf_strdup (volname);
        } else {
                /* Bringing in NFS like behavior for mount command,    */
                /* With this, one can mount a volume with below cmd    */
                /* bash# mount -t glusterfs server:/volume /mnt/pnt    */
                dup_volname = gf_strdup (&volname[1]);
        }

        free_ptr = dup_volname;

        ret = glusterd_volinfo_find (dup_volname, &volinfo);
        if (ret) {
                /* Split the volume name */
                vol = strtok_r (dup_volname, ".", &tmp);
                if (!vol)
                        goto out;
                ret = glusterd_volinfo_find (vol, &volinfo);
                if (ret)
                        goto out;
        }

        if (!glusterd_auth_get_username (volinfo))
                trusted_str = NULL;

        ret = snprintf (path, path_len, "%s/vols/%s/%s.vol",
                        priv->workdir, volinfo->volname, volname);
        if (ret == -1)
                goto out;

        ret = stat (path, &stbuf);

        if ((ret == -1) && (errno == ENOENT)) {
                snprintf (path, path_len, "%s/vols/%s/%s%s-fuse.vol",
                          priv->workdir, volinfo->volname,
                          (trusted_str ? trusted_str : ""), dup_volname);
                ret = stat (path, &stbuf);
        }

        if ((ret == -1) && (errno == ENOENT)) {
                snprintf (path, path_len, "%s/vols/%s/%s-tcp.vol",
                          priv->workdir, volinfo->volname, volname);
        }

        ret = 1;
out:
        GF_FREE (free_ptr);
        return ret;
}

int
server_getspec (rpcsvc_request_t *req)
{
        int32_t               ret                    = -1;
        int32_t               op_errno               = 0;
        int32_t               spec_fd                = -1;
        size_t                file_len               = 0;
        char                  filename[PATH_MAX]  = {0,};
        struct stat           stbuf                  = {0,};
        char                 *volume                 = NULL;
        char                 *tmp                    = NULL;
        int                   cookie                 = 0;
        rpc_transport_t      *trans                  = NULL;
        gf_getspec_req        args                   = {0,};
        gf_getspec_rsp        rsp                    = {0,};
        char                  addrstr[RPCSVC_PEER_STRLEN] = {0};


        if (!xdr_to_generic (req->msg[0], &args,
                             (xdrproc_t)xdr_gf_getspec_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        volume = args.key;

        trans = req->trans;
        ret = rpcsvc_transport_peername (trans, (char *)&addrstr,
                                         sizeof (addrstr));
        if (ret)
                goto fail;

        tmp = strrchr (addrstr, ':');
        *tmp = '\0';

        /* we trust the local admin */
        if (!glusterd_is_local_addr (addrstr)) {

                ret = build_volfile_path (volume, filename,
                                          sizeof (filename),
                                          TRUSTED_PREFIX);
        } else {
                ret = build_volfile_path (volume, filename,
                                          sizeof (filename), NULL);
        }

        if (ret > 0) {
                /* to allocate the proper buffer to hold the file data */
                ret = stat (filename, &stbuf);
                if (ret < 0){
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Unable to stat %s (%s)",
                                filename, strerror (errno));
                        goto fail;
                }

                spec_fd = open (filename, O_RDONLY);
                if (spec_fd < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Unable to open %s (%s)",
                                filename, strerror (errno));
                        goto fail;
                }
                ret = file_len = stbuf.st_size;
        } else {
                op_errno = ENOENT;
        }

        if (file_len) {
                rsp.spec = CALLOC (file_len+1, sizeof (char));
                if (!rsp.spec) {
                        ret = -1;
                        op_errno = ENOMEM;
                        goto fail;
                }
                ret = read (spec_fd, rsp.spec, file_len);

                close (spec_fd);
        }

        /* convert to XDR */
fail:
        rsp.op_ret   = ret;

        if (op_errno)
                rsp.op_errno = gf_errno_to_error (op_errno);
        if (cookie)
                rsp.op_errno = cookie;

        if (!rsp.spec)
                rsp.spec = strdup ("");

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf_getspec_rsp);
        free (args.key);//malloced by xdr
        free (rsp.spec);

        return 0;
}

int32_t
server_event_notify (rpcsvc_request_t *req)
{
        int32_t                 ret             = -1;
        int32_t                 op_errno        =  0;
        gf_event_notify_req     args            = {0,};
        gf_event_notify_rsp     rsp             = {0,};
        dict_t                 *dict            = NULL;
        gf_boolean_t            need_rsp        = _gf_true;

        if (!xdr_to_generic (req->msg[0], &args,
            (xdrproc_t)xdr_gf_event_notify_req)) {
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        if (args.dict.dict_len) {
                dict = dict_new ();
                if (!dict)
                        return ret;
                ret = dict_unserialize (args.dict.dict_val,
                                        args.dict.dict_len, &dict);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Failed to unserialize req");
                        goto fail;
                }
        }

        switch (args.op) {
        case GF_EN_DEFRAG_STATUS:
                gf_log ("", GF_LOG_INFO,
                        "recieved defrag status updated");
                if (dict) {
                        glusterd_defrag_event_notify_handle (dict);
                        need_rsp = _gf_false;
                }
                break;
        default:
                gf_log ("", GF_LOG_ERROR, "Unkown op recieved in in event "
                        "notify");
                ret = -1;
                break;
        }

fail:
        rsp.op_ret   = ret;

        if (op_errno)
                rsp.op_errno = gf_errno_to_error (op_errno);

        if (need_rsp)
                glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                       (xdrproc_t)xdr_gf_event_notify_rsp);
        if (dict)
                dict_unref (dict);
        free (args.dict.dict_val);//malloced by xdr

        return 0;
}
rpcsvc_actor_t gluster_handshake_actors[] = {
        [GF_HNDSK_NULL]      = {"NULL",      GF_HNDSK_NULL,      NULL, NULL, NULL, 0},
        [GF_HNDSK_GETSPEC]   = {"GETSPEC",   GF_HNDSK_GETSPEC,   server_getspec, NULL, NULL, 0},
        [GF_HNDSK_EVENT_NOTIFY] = {"EVENTNOTIFY", GF_HNDSK_EVENT_NOTIFY, server_event_notify,
                                    NULL, NULL, 0},
};


struct rpcsvc_program gluster_handshake_prog = {
        .progname  = "GlusterFS Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .actors    = gluster_handshake_actors,
        .numactors = GF_HNDSK_MAXVALUE,
};

char *glusterd_dump_proc[GF_DUMP_MAXVALUE] = {
        [GF_DUMP_NULL] = "NULL",
        [GF_DUMP_DUMP] = "DUMP",
};

rpc_clnt_prog_t glusterd_dump_prog = {
        .progname  = "GLUSTERD-DUMP",
        .prognum   = GLUSTER_DUMP_PROGRAM,
        .progver   = GLUSTER_DUMP_VERSION,
        .procnames = glusterd_dump_proc,
};

static int
glusterd_event_connected_inject (glusterd_peerctx_t *peerctx)
{
        GF_ASSERT (peerctx);

        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_probe_ctx_t            *ctx = NULL;
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;


        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_CONNECTED, &event);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get new event");
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof(*ctx), gf_gld_mt_probe_ctx_t);

        if (!ctx) {
                ret = -1;
                gf_log ("", GF_LOG_ERROR, "Memory not available");
                goto out;
        }

        peerinfo = peerctx->peerinfo;
        ctx->hostname = gf_strdup (peerinfo->hostname);
        ctx->port = peerinfo->port;
        ctx->req = peerctx->args.req;

        event->peerinfo = peerinfo;
        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject "
                        "EVENT_CONNECTED ret = %d", ret);
                goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_set_clnt_mgmt_program (glusterd_peerinfo_t *peerinfo,
                                gf_prog_detail *prog)
{
        gf_prog_detail *trav     = NULL;
        int             ret      = -1;

        if (!peerinfo || !prog)
                goto out;

        trav = prog;

        while (trav) {
                /* Select 'programs' */
                if ((gd_mgmt_prog.prognum == trav->prognum) &&
                    (gd_mgmt_prog.progver == trav->progver)) {
                        peerinfo->mgmt = &gd_mgmt_prog;
                        ret = 0;
                }
                if ((gd_peer_prog.prognum == trav->prognum) &&
                    (gd_peer_prog.progver == trav->progver)) {
                        peerinfo->peer = &gd_peer_prog;
                        ret = 0;
                }
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "%s (%"PRId64":%"PRId64") not supported",
                                trav->progname, trav->prognum,
                                trav->progver);
                }
                trav = trav->next;
        }

        if (peerinfo->mgmt) {
                gf_log ("", GF_LOG_INFO,
                        "Using Program %s, Num (%d), Version (%d)",
                        peerinfo->mgmt->progname, peerinfo->mgmt->prognum,
                        peerinfo->mgmt->progver);
        }
        if (peerinfo->peer) {
                gf_log ("", GF_LOG_INFO,
                        "Using Program %s, Num (%d), Version (%d)",
                        peerinfo->peer->progname, peerinfo->peer->prognum,
                        peerinfo->peer->progver);
        }

out:
        return ret;
}

int
glusterd_peer_dump_version_cbk (struct rpc_req *req, struct iovec *iov,
                                int count, void *myframe)
{
        int                  ret      = -1;
        gf_dump_rsp          rsp      = {0,};
        xlator_t            *this     = NULL;
        gf_prog_detail      *trav     = NULL;
        gf_prog_detail      *next     = NULL;
        call_frame_t        *frame    = NULL;
        glusterd_peerinfo_t *peerinfo = NULL;
        glusterd_peerctx_t  *peerctx  = NULL;
        char                msg[1024] = {0,};

        this = THIS;
        frame = myframe;
        peerctx = frame->local;
        peerinfo = peerctx->peerinfo;

        if (-1 == req->rpc_status) {
                snprintf (msg, sizeof (msg),
                          "Error through RPC layer, retry again later");
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_dump_rsp);
        if (ret < 0) {
                snprintf (msg, sizeof (msg), "Failed to decode XDR");
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }
        if (-1 == rsp.op_ret) {
                snprintf (msg, sizeof (msg),
                          "Failed to get the 'versions' from remote server");
                gf_log (frame->this->name, GF_LOG_ERROR, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        /* Make sure we assign the proper program to peer */
        ret = glusterd_set_clnt_mgmt_program (peerinfo, rsp.prog);
        if (ret) {
                gf_log ("", GF_LOG_WARNING, "failed to set the mgmt program");
                goto out;
        }

        ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);

        if (GD_MODE_ON == peerctx->args.mode) {
                ret = glusterd_event_connected_inject (peerctx);
                peerctx->args.req = NULL;
        } else if (GD_MODE_SWITCH_ON == peerctx->args.mode) {
                peerctx->args.mode = GD_MODE_ON;
        } else {
                gf_log ("", GF_LOG_WARNING, "unknown mode %d",
                        peerctx->args.mode);
        }

        glusterd_friend_sm ();
        glusterd_op_sm ();

        ret = 0;
out:

        /* don't use GF_FREE, buffer was allocated by libc */
        if (rsp.prog) {
                trav = rsp.prog;
                while (trav) {
                        next = trav->next;
                        free (trav->progname);
                        free (trav);
                        trav = next;
                }
        }

        frame->local = NULL;
        STACK_DESTROY (frame->root);

        if (ret != 0)
                rpc_transport_disconnect (peerinfo->rpc->conn.trans);

        return 0;
}


int
glusterd_peer_handshake (xlator_t *this, struct rpc_clnt *rpc,
                         glusterd_peerctx_t *peerctx)
{
        call_frame_t        *frame    = NULL;
        gf_dump_req          req      = {0,};
        int                  ret      = -1;

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        frame->local = peerctx;

        req.gfs_id = 0xcafe;

        ret = glusterd_submit_request (peerctx->peerinfo->rpc, &req, frame,
                                       &glusterd_dump_prog, GF_DUMP_DUMP,
                                       NULL, this,
                                       glusterd_peer_dump_version_cbk,
                                       (xdrproc_t)xdr_gf_dump_req);
out:
        return ret;
}
