/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
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
#include "glusterd-store.h"

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

/* Get and store op-versions of the clients sending the getspec request
 * Clients of versions <= 3.3, don't send op-versions, their op-versions are
 * defaulted to 1
 */
static int
_get_client_op_versions (gf_getspec_req *args, peer_info_t *peerinfo)
{
        int    ret                   = 0;
        int    client_max_op_version = 1;
        int    client_min_op_version = 1;
        dict_t *dict                 = NULL;

        GF_ASSERT (args);
        GF_ASSERT (peerinfo);

        if (args->xdata.xdata_len) {
                dict = dict_new ();
                if (!dict) {
                        ret = -1;
                        goto out;
                }

                ret = dict_unserialize (args->xdata.xdata_val,
                                        args->xdata.xdata_len, &dict);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Failed to unserialize request dictionary");
                        goto out;
                }

                ret = dict_get_int32 (dict, "min-op-version",
                                      &client_min_op_version);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Failed to get client-min-op-version");
                        goto out;
                }

                ret = dict_get_int32 (dict, "max-op-version",
                                      &client_max_op_version);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Failed to get client-max-op-version");
                        goto out;
                }
        }

        peerinfo->max_op_version = client_max_op_version;
        peerinfo->min_op_version = client_min_op_version;

out:
        return ret;
}

/* Checks if the client supports the volume, ie. client can understand all the
 * options in the volfile
 */
static gf_boolean_t
_client_supports_volume (peer_info_t *peerinfo, int32_t *op_errno)
{
        gf_boolean_t       ret       = _gf_true;
        glusterd_volinfo_t *volinfo  = NULL;

        GF_ASSERT (peerinfo);
        GF_ASSERT (op_errno);


        /* Only check when the volfile being requested is a volume. Not finding
         * a volinfo implies that the volfile requested for is not of a gluster
         * volume. A non volume volfile is requested by the local gluster
         * services like shd and nfs-server. These need not be checked as they
         * will be running at the same op-version as glusterd and will be able
         * to support all the features
         */
        if ((glusterd_volinfo_find (peerinfo->volname, &volinfo) == 0) &&
            ((peerinfo->min_op_version > volinfo->client_op_version) ||
             (peerinfo->max_op_version < volinfo->client_op_version))) {
                ret = _gf_false;
                *op_errno = ENOTSUP;
                gf_log ("glusterd", GF_LOG_INFO,
                        "Client %s (%d -> %d) doesn't support required "
                        "op-version (%d). Rejecting volfile request.",
                        peerinfo->identifier, peerinfo->min_op_version,
                        peerinfo->max_op_version, volinfo->client_op_version);
        }

        return ret;
}

int
__server_getspec (rpcsvc_request_t *req)
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
        peer_info_t          *peerinfo               = NULL;

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_getspec_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        peerinfo = &req->trans->peerinfo;

        volume = args.key;
        /* Need to strip leading '/' from volnames. This was introduced to
         * support nfs style mount parameters for native gluster mount
         */
        if (volume[0] == '/')
                strncpy (peerinfo->volname, &volume[1], strlen(&volume[1]));
        else
                strncpy (peerinfo->volname, volume, strlen(volume));

        ret = _get_client_op_versions (&args, peerinfo);
        if (ret)
                goto fail;

        if (!_client_supports_volume (peerinfo, &op_errno)) {
                ret = -1;
                goto fail;
        }

        trans = req->trans;
        /* addrstr will be empty for cli socket connections */
        ret = rpcsvc_transport_peername (trans, (char *)&addrstr,
                                         sizeof (addrstr));
        if (ret)
                goto fail;

        tmp  = strrchr (addrstr, ':');
        if (tmp)
                *tmp = '\0';

        /* The trusted volfiles are given to the glusterd owned process like NFS
         * server, self-heal daemon etc., so that they are not inadvertently
         * blocked by a auth.{allow,reject} setting. The trusted volfile is not
         * meant for external users.
         */
        if (strlen (addrstr) && gf_is_local_addr (addrstr)) {

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

int
server_getspec (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __server_getspec);
}

int32_t
__server_event_notify (rpcsvc_request_t *req)
{
        int32_t                 ret             = -1;
        int32_t                 op_errno        =  0;
        gf_event_notify_req     args            = {0,};
        gf_event_notify_rsp     rsp             = {0,};
        dict_t                 *dict            = NULL;
        gf_boolean_t            need_rsp        = _gf_true;

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_event_notify_req);
        if (ret < 0) {
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
                        "received defrag status updated");
                if (dict) {
                        glusterd_defrag_event_notify_handle (dict);
                        need_rsp = _gf_false;
                }
                break;
        default:
                gf_log ("", GF_LOG_ERROR, "Unknown op received in event "
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

int32_t
server_event_notify (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __server_event_notify);
}

int
gd_validate_cluster_op_version (xlator_t *this, int cluster_op_version,
                                char *peerid)
{
        int              ret  = -1;
        glusterd_conf_t *conf = NULL;

        conf = this->private;

        if (cluster_op_version > GD_OP_VERSION_MAX) {
                gf_log (this->name, GF_LOG_ERROR,
                        "operating version %d is more than the maximum "
                        "supported (%d) on the machine (as per peer request "
                        "from %s)", cluster_op_version, GD_OP_VERSION_MAX,
                        peerid);
                goto out;
        }

        /* The peer can only reduce its op-version when it doesn't have any
         * volumes. Reducing op-version when it already contains volumes can
         * lead to inconsistencies in the cluster
         */
        if ((cluster_op_version < conf->op_version) &&
            !list_empty (&conf->volumes)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot reduce operating version to %d from current "
                        "version %d as volumes exist (as per peer request from "
                        "%s)", cluster_op_version, conf->op_version, peerid);
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
__glusterd_mgmt_hndsk_versions (rpcsvc_request_t *req)
{
        dict_t            *dict            = NULL;
        xlator_t          *this            = NULL;
        glusterd_conf_t   *conf            = NULL;
        int                ret             = -1;
        int                op_errno        = EINVAL;
        gf_mgmt_hndsk_req  args            = {{0,},};
        gf_mgmt_hndsk_rsp  rsp             = {0,};

        this = THIS;
        conf = this->private;

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_mgmt_hndsk_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_int32 (dict, GD_OP_VERSION_KEY, conf->op_version);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set operating version");
                rsp.op_ret = ret;
                goto out;
        }

        ret = dict_set_int32 (dict, GD_MIN_OP_VERSION_KEY, GD_OP_VERSION_MIN);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set %s", GD_MIN_OP_VERSION_KEY);
                rsp.op_ret = ret;
                goto out;
        }

        ret = dict_set_int32 (dict, GD_MAX_OP_VERSION_KEY, GD_OP_VERSION_MAX);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set %s", GD_MAX_OP_VERSION_KEY);
                rsp.op_ret = ret;
                goto out;
        }

        ret = 0;

        GF_PROTOCOL_DICT_SERIALIZE (this, dict, (&rsp.hndsk.hndsk_val),
                                    rsp.hndsk.hndsk_len, op_errno, out);
out:

        rsp.op_ret = ret;
        rsp.op_errno = op_errno;

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf_mgmt_hndsk_rsp);

        ret = 0;

        if (dict)
                dict_unref (dict);

        if (args.hndsk.hndsk_val)
                free (args.hndsk.hndsk_val);

        if (rsp.hndsk.hndsk_val)
                GF_FREE (rsp.hndsk.hndsk_val);

        return ret;
}

int
glusterd_mgmt_hndsk_versions (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_mgmt_hndsk_versions);
}

int
__glusterd_mgmt_hndsk_versions_ack (rpcsvc_request_t *req)
{
        dict_t            *clnt_dict       = NULL;
        xlator_t          *this            = NULL;
        glusterd_conf_t   *conf            = NULL;
        int                ret             = -1;
        int                op_errno        = EINVAL;
        int                peer_op_version = 0;
        gf_mgmt_hndsk_req  args            = {{0,},};
        gf_mgmt_hndsk_rsp  rsp             = {0,};

        this = THIS;
        conf = this->private;

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_mgmt_hndsk_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, clnt_dict, args.hndsk.hndsk_val,
                                      (args.hndsk.hndsk_len), ret, op_errno,
                                      out);

        ret = dict_get_int32 (clnt_dict, GD_OP_VERSION_KEY, &peer_op_version);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to get the op-version key peer=%s",
                        req->trans->peerinfo.identifier);
                goto out;
        }

        ret = gd_validate_cluster_op_version (this, peer_op_version,
                                              req->trans->peerinfo.identifier);
        if (ret)
                goto out;


        /* As this is ACK from the Cluster for the versions supported,
           can set the op-version of 'this' glusterd to the one
           received. */
        gf_log (this->name, GF_LOG_INFO, "using the op-version %d",
                peer_op_version);
        conf->op_version = peer_op_version;
        ret = glusterd_store_global_info (this);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Failed to store op-version");

out:
        rsp.op_ret = ret;
        rsp.op_errno = op_errno;

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf_mgmt_hndsk_rsp);

        ret = 0;

        if (clnt_dict)
                dict_unref (clnt_dict);

        if (args.hndsk.hndsk_val)
                free (args.hndsk.hndsk_val);

        return ret;
}

int
glusterd_mgmt_hndsk_versions_ack (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_mgmt_hndsk_versions_ack);
}

int
__server_get_volume_info (rpcsvc_request_t *req)
{
        int                     ret             = -1;
        int32_t                 op_errno        = ENOENT;
        gf_get_volume_info_req	vol_info_req    = {{0,}};
        gf_get_volume_info_rsp	vol_info_rsp    = {0,};
        char                    *volname        = NULL;
        glusterd_volinfo_t      *volinfo        = NULL;
        dict_t                  *dict           = NULL;
        dict_t                  *dict_rsp       = NULL;
        char                    *volume_id_str  = NULL;
        int32_t                 flags           = 0;

        ret = xdr_to_generic (req->msg[0], &vol_info_req,
                             (xdrproc_t)xdr_gf_get_volume_info_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        gf_log ("glusterd", GF_LOG_INFO, "Received get volume info req");

        if (vol_info_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();
                if (!dict) {
                        gf_log ("", GF_LOG_WARNING, "Out of Memory");
                        op_errno = ENOMEM;
                        ret = -1;
                        goto out;
                }

                ret = dict_unserialize (vol_info_req.dict.dict_val,
                                        vol_info_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        op_errno = -ret;
                        ret = -1;
                        goto out;
                } else {
                        dict->extra_stdfree = vol_info_req.dict.dict_val;
                }
        }

        ret = dict_get_int32 (dict, "flags", &flags);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get flags");
                op_errno = -ret;
                ret = -1;
                goto out;
        }

        if (!flags) {
                //Nothing to query about. Just return success
                gf_log (THIS->name, GF_LOG_ERROR, "No flags set");
                ret = 0;
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                op_errno = EINVAL;
                ret = -1;
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                op_errno = EINVAL;
                ret = -1;
                goto out;
        }

        if (flags | (int32_t)GF_GET_VOLUME_UUID) {
                volume_id_str = gf_strdup (uuid_utoa (volinfo->volume_id));
                if (!volume_id_str) {
                        op_errno = ENOMEM;
                        ret = -1;
                        goto out;
                }

                dict_rsp = dict_new ();
                if (!dict_rsp) {
                        gf_log ("", GF_LOG_WARNING, "Out of Memory");
                        op_errno = ENOMEM;
                        ret = -1;
                        goto out;
                }
                ret = dict_set_dynstr (dict_rsp, "volume_id", volume_id_str);
                if (ret) {
                        op_errno = -ret;
                        ret = -1;
                        goto out;
                }
        }
        ret = dict_allocate_and_serialize (dict_rsp, &vol_info_rsp.dict.dict_val,
                                           &vol_info_rsp.dict.dict_len);
        if (ret) {
                op_errno = -ret;
                ret = -1;
                goto out;
        }

out:
        vol_info_rsp.op_ret = ret;
        vol_info_rsp.op_errno = op_errno;
        vol_info_rsp.op_errstr = "";
        glusterd_submit_reply (req, &vol_info_rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf_get_volume_info_rsp);
        ret = 0;

        if (dict) {
                dict_unref (dict);
        }

        if (dict_rsp) {
                dict_unref (dict_rsp);
        }

        if (vol_info_rsp.dict.dict_val) {
                GF_FREE (vol_info_rsp.dict.dict_val);
        }
        return ret;
}

int
server_get_volume_info (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __server_get_volume_info);
}

rpcsvc_actor_t gluster_handshake_actors[GF_HNDSK_MAXVALUE] = {
        [GF_HNDSK_NULL]         = {"NULL",        GF_HNDSK_NULL,         NULL,                NULL, 0, DRC_NA},
        [GF_HNDSK_GETSPEC]      = {"GETSPEC",     GF_HNDSK_GETSPEC,      server_getspec,      NULL, 0, DRC_NA},
        [GF_HNDSK_EVENT_NOTIFY] = {"EVENTNOTIFY", GF_HNDSK_EVENT_NOTIFY, server_event_notify, NULL, 0, DRC_NA},
        [GF_HNDSK_GET_VOLUME_INFO] = {"GETVOLUMEINFO", GF_HNDSK_GET_VOLUME_INFO, server_get_volume_info, NULL, 0, DRC_NA},
};


struct rpcsvc_program gluster_handshake_prog = {
        .progname  = "Gluster Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .actors    = gluster_handshake_actors,
        .numactors = GF_HNDSK_MAXVALUE,
};

/* A minimal RPC program just for the cli getspec command */
rpcsvc_actor_t gluster_cli_getspec_actors[GF_HNDSK_MAXVALUE] = {
        [GF_HNDSK_GETSPEC]      = {"GETSPEC",     GF_HNDSK_GETSPEC,      server_getspec,      NULL, 0, DRC_NA},
};

struct rpcsvc_program gluster_cli_getspec_prog = {
        .progname  = "Gluster Handshake (CLI Getspec)",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .actors    = gluster_cli_getspec_actors,
        .numactors = GF_HNDSK_MAXVALUE,
};


char *glusterd_dump_proc[GF_DUMP_MAXVALUE] = {
        [GF_DUMP_NULL] = "NULL",
        [GF_DUMP_DUMP] = "DUMP",
};

rpc_clnt_prog_t glusterd_dump_prog = {
        .progname       = "GLUSTERD-DUMP",
        .prognum        = GLUSTER_DUMP_PROGRAM,
        .progver        = GLUSTER_DUMP_VERSION,
        .procnames      = glusterd_dump_proc,
};


rpcsvc_actor_t glusterd_mgmt_hndsk_actors[GD_MGMT_HNDSK_MAXVALUE] = {
        [GD_MGMT_HNDSK_NULL]            = {"NULL", GD_MGMT_HNDSK_NULL, NULL,
                                           NULL, 0, DRC_NA},
        [GD_MGMT_HNDSK_VERSIONS]        = {"MGMT-VERS", GD_MGMT_HNDSK_VERSIONS,
                                           glusterd_mgmt_hndsk_versions, NULL,
                                           0, DRC_NA},
        [GD_MGMT_HNDSK_VERSIONS_ACK]    = {"MGMT-VERS-ACK",
                                           GD_MGMT_HNDSK_VERSIONS_ACK,
                                           glusterd_mgmt_hndsk_versions_ack,
                                           NULL, 0, DRC_NA},
};

struct rpcsvc_program glusterd_mgmt_hndsk_prog = {
        .progname       = "Gluster MGMT Handshake",
        .prognum        = GD_MGMT_HNDSK_PROGRAM,
        .progver        = GD_MGMT_HNDSK_VERSION,
        .actors         = glusterd_mgmt_hndsk_actors,
        .numactors      = GD_MGMT_HNDSK_MAXVALUE,
};

char *glusterd_mgmt_hndsk_proc[GD_MGMT_HNDSK_MAXVALUE] = {
        [GD_MGMT_HNDSK_NULL]          = "NULL",
        [GD_MGMT_HNDSK_VERSIONS] = "MGMT-VERS",
        [GD_MGMT_HNDSK_VERSIONS_ACK] = "MGMT-VERS-ACK",
};

rpc_clnt_prog_t gd_clnt_mgmt_hndsk_prog = {
        .progname  = "Gluster MGMT Handshake",
        .prognum   = GD_MGMT_HNDSK_PROGRAM,
        .progver   = GD_MGMT_HNDSK_VERSION,
        .procnames = glusterd_mgmt_hndsk_proc,
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
        ctx->dict = peerctx->args.dict;

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
gd_validate_peer_op_version (xlator_t *this, glusterd_peerinfo_t *peerinfo,
                             dict_t *dict, char **errstr)
{
        int              ret  = -1;
        glusterd_conf_t *conf = NULL;
        int32_t          peer_op_version = 0;
        int32_t          peer_min_op_version = 0;
        int32_t          peer_max_op_version = 0;

        if (!dict && !this && !peerinfo)
                goto out;

        conf = this->private;

        ret = dict_get_int32 (dict, GD_OP_VERSION_KEY, &peer_op_version);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, GD_MAX_OP_VERSION_KEY,
                              &peer_max_op_version);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, GD_MIN_OP_VERSION_KEY,
                              &peer_min_op_version);
        if (ret)
                goto out;

        ret = -1;
        /* Check if peer can support our op_version */
        if ((peer_max_op_version < conf->op_version) ||
            (peer_min_op_version > conf->op_version)) {
                ret = gf_asprintf (errstr, "Peer %s does not support required "
                                   "op-version", peerinfo->hostname);
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        gf_log (this->name , GF_LOG_DEBUG, "Peer %s %s", peerinfo->hostname,
                ((ret < 0) ? "rejected" : "accepted"));
        return ret;
}

int
__glusterd_mgmt_hndsk_version_ack_cbk (struct rpc_req *req, struct iovec *iov,
                                     int count, void *myframe)
{
        int                  ret      = -1;
        int                  op_errno = EINVAL;
        gf_mgmt_hndsk_rsp    rsp      = {0,};
        xlator_t            *this     = NULL;
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

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_mgmt_hndsk_rsp);
        if (ret < 0) {
                snprintf (msg, sizeof (msg), "Failed to decode XDR");
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        op_errno = rsp.op_errno;
        if (-1 == rsp.op_ret) {
                ret = -1;
                snprintf (msg, sizeof (msg),
                          "Failed to get handshake ack from remote server");
                gf_log (frame->this->name, GF_LOG_ERROR, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        /* TODO: this is hardcoded as of now, but I don't forsee any problems
         * with this as long as we are properly handshaking operating versions
         */
        peerinfo->mgmt = &gd_mgmt_prog;
        peerinfo->peer = &gd_peer_prog;

        ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);

        if (GD_MODE_ON == peerctx->args.mode) {
                ret = glusterd_event_connected_inject (peerctx);
                peerctx->args.req = NULL;
        } else if (GD_MODE_SWITCH_ON == peerctx->args.mode) {
                peerctx->args.mode = GD_MODE_ON;
        } else {
                gf_log (this->name, GF_LOG_WARNING, "unknown mode %d",
                        peerctx->args.mode);
        }

        glusterd_friend_sm ();

        ret = 0;
out:

        frame->local = NULL;
        STACK_DESTROY (frame->root);

        if (ret != 0)
                rpc_transport_disconnect (peerinfo->rpc->conn.trans);

        if (rsp.hndsk.hndsk_val)
                free (rsp.hndsk.hndsk_val);

        return 0;
}

int
glusterd_mgmt_hndsk_version_ack_cbk (struct rpc_req *req, struct iovec *iov,
                                     int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_mgmt_hndsk_version_ack_cbk);
}

int
__glusterd_mgmt_hndsk_version_cbk (struct rpc_req *req, struct iovec *iov,
                                 int count, void *myframe)
{
        int                  ret       = -1;
        int                  op_errno  = EINVAL;
        gf_mgmt_hndsk_rsp    rsp       = {0,};
        gf_mgmt_hndsk_req    arg       = {{0,}};
        xlator_t            *this      = NULL;
        call_frame_t        *frame     = NULL;
        glusterd_peerinfo_t *peerinfo  = NULL;
        glusterd_peerctx_t  *peerctx   = NULL;
        dict_t              *dict      = NULL;
        dict_t              *rsp_dict  = NULL;
        glusterd_conf_t     *conf      = NULL;
        char                 msg[1024] = {0,};

        this = THIS;
        conf = this->private;
        frame = myframe;
        peerctx = frame->local;
        peerinfo = peerctx->peerinfo;

        if (-1 == req->rpc_status) {
                ret = -1;
                snprintf (msg, sizeof (msg),
                          "Error through RPC layer, retry again later");
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_mgmt_hndsk_rsp);
        if (ret < 0) {
                snprintf (msg, sizeof (msg), "Failed to decode management "
                          "handshake response");
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, dict, rsp.hndsk.hndsk_val,
                                      rsp.hndsk.hndsk_len, ret, op_errno,
                                      out);

        op_errno = rsp.op_errno;
        if (-1 == rsp.op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get the 'versions' from peer (%s)",
                        req->conn->trans->peerinfo.identifier);
                goto out;
        }

        /* Check if peer can be part of cluster */
        ret = gd_validate_peer_op_version (this, peerinfo, dict,
                                           &peerctx->errstr);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to validate the operating version of peer (%s)",
                        peerinfo->hostname);
                goto out;
        }

        rsp_dict = dict_new ();
        if (!rsp_dict)
                goto out;

        ret = dict_set_int32 (rsp_dict, GD_OP_VERSION_KEY, conf->op_version);
        if (ret) {
                gf_log(this->name, GF_LOG_ERROR,
                       "failed to set operating version in dict");
                goto out;
        }

        GF_PROTOCOL_DICT_SERIALIZE (this, rsp_dict, (&arg.hndsk.hndsk_val),
                                    arg.hndsk.hndsk_len, op_errno, out);

        ret = glusterd_submit_request (peerctx->peerinfo->rpc, &arg, frame,
                                       &gd_clnt_mgmt_hndsk_prog,
                                       GD_MGMT_HNDSK_VERSIONS_ACK, NULL, this,
                                       glusterd_mgmt_hndsk_version_ack_cbk,
                                       (xdrproc_t)xdr_gf_mgmt_hndsk_req);

out:
        if (ret) {
                frame->local = NULL;
                STACK_DESTROY (frame->root);
                rpc_transport_disconnect (peerinfo->rpc->conn.trans);
        }

        if (rsp.hndsk.hndsk_val)
                free (rsp.hndsk.hndsk_val);

        if (arg.hndsk.hndsk_val)
                GF_FREE (arg.hndsk.hndsk_val);

        if (dict)
                dict_unref (dict);

        if (rsp_dict)
                dict_unref (rsp_dict);

        return 0;
}

int
glusterd_mgmt_hndsk_version_cbk (struct rpc_req *req, struct iovec *iov,
                                     int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_mgmt_hndsk_version_cbk);
}

int
glusterd_mgmt_handshake (xlator_t *this, glusterd_peerctx_t *peerctx)
{
        call_frame_t        *frame    = NULL;
        gf_mgmt_hndsk_req    req      = {{0,},};
        int                  ret      = -1;

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        frame->local = peerctx;

        ret = glusterd_submit_request (peerctx->peerinfo->rpc, &req, frame,
                                       &gd_clnt_mgmt_hndsk_prog,
                                       GD_MGMT_HNDSK_VERSIONS, NULL, this,
                                       glusterd_mgmt_hndsk_version_cbk,
                                       (xdrproc_t)xdr_gf_mgmt_hndsk_req);
        ret = 0;
out:
        if (ret && frame)
                STACK_DESTROY (frame->root);

        return ret;
}

int
glusterd_set_clnt_mgmt_program (glusterd_peerinfo_t *peerinfo,
                                gf_prog_detail *prog)
{
        gf_prog_detail  *trav   = NULL;
        int             ret     = -1;

        if (!peerinfo || !prog)
                goto out;

        trav = prog;

        while (trav) {
                ret = -1;
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
        ret = 0;
out:
        return ret;

}

static gf_boolean_t
_mgmt_hndsk_prog_present (gf_prog_detail *prog) {
        gf_boolean_t    ret = _gf_false;
        gf_prog_detail  *trav = NULL;

        GF_ASSERT (prog);

        trav = prog;

        while (trav) {
                if ((trav->prognum == GD_MGMT_HNDSK_PROGRAM) &&
                    (trav->progver == GD_MGMT_HNDSK_VERSION)) {
                        ret = _gf_true;
                        goto out;
                }
                trav = trav->next;
        }
out:
        return ret;
}

int
__glusterd_peer_dump_version_cbk (struct rpc_req *req, struct iovec *iov,
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
        glusterd_conf_t     *conf     = NULL;
        char                msg[1024] = {0,};

        this = THIS;
        conf = this->private;
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

        if (_mgmt_hndsk_prog_present (rsp.prog)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Proceeding to op-version handshake with peer %s",
                        peerinfo->hostname);
                ret = glusterd_mgmt_handshake (this, peerctx);
                goto out;
        } else if (conf->op_version > 1) {
                ret = -1;
                snprintf (msg, sizeof (msg),
                          "Peer %s does not support required op-version",
                          peerinfo->hostname);
                peerctx->errstr = gf_strdup (msg);
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
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

        glusterd_friend_sm();
        glusterd_op_sm();

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
glusterd_peer_dump_version_cbk (struct rpc_req *req, struct iovec *iov,
                                int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_peer_dump_version_cbk);
}

int
glusterd_peer_dump_version (xlator_t *this, struct rpc_clnt *rpc,
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
