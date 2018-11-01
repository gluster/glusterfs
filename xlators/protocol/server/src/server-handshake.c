/*
  Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "server.h"
#include "server-helpers.h"
#include "rpc-common-xdr.h"
#include "glusterfs3-xdr.h"
#include "compat-errno.h"
#include "glusterfs3.h"
#include "authenticate.h"
#include "server-messages.h"
#include "syscall.h"
#include "events.h"
#include "syncop.h"

struct __get_xl_struct {
        const char *name;
        xlator_t *reply;
};
int
gf_compare_client_version (rpcsvc_request_t *req, int fop_prognum,
                           int mgmt_prognum)
{
        int ret = -1;
        /* TODO: think.. */
        if (glusterfs3_3_fop_prog.prognum == fop_prognum)
                ret = 0;

        return ret;
}

int
server_getspec (rpcsvc_request_t *req)
{
        int32_t              ret                    = 0;
        int32_t              op_errno               = ENOENT;
        gf_getspec_req       args                   = {0,};
        gf_getspec_rsp       rsp                    = {0,};

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_getspec_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                op_errno = EINVAL;
                goto fail;
        }

        op_errno = ENOSYS;
fail:
        rsp.spec = "<this method is not in use, use glusterd for getspec>";
        rsp.op_errno = gf_errno_to_error (op_errno);
        rsp.op_ret   = -1;

        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gf_getspec_rsp);

        return 0;
}

static void
server_first_lookup_done (rpcsvc_request_t *req, gf_setvolume_rsp *rsp) {

        server_submit_reply (NULL, req, rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gf_setvolume_rsp);

        GF_FREE (rsp->dict.dict_val);
        GF_FREE (rsp);
}

static inode_t *
do_path_lookup (xlator_t *xl, dict_t *dict, inode_t *parinode, char *basename)
{
        int ret = 0;
        loc_t loc = {0,};
        uuid_t gfid = {0,};
        struct iatt iatt = {0,};
        inode_t *inode = NULL;

        loc.parent = parinode;
        loc_touchup (&loc, basename);
        loc.inode = inode_new (xl->itable);

        gf_uuid_generate (gfid);
        ret = dict_set_gfuuid (dict, "gfid-req", gfid, true);
        if (ret) {
                gf_log (xl->name, GF_LOG_ERROR,
                        "failed to set 'gfid-req' for subdir");
                goto out;
        }

        ret = syncop_lookup (xl, &loc, &iatt, NULL, dict, NULL);
        if (ret < 0) {
                gf_log (xl->name, GF_LOG_ERROR,
                        "first lookup on subdir (%s) failed: %s",
                        basename, strerror (errno));
        }


        /* Inode linking is required so that the
           resolution happens all fine for future fops */
        inode = inode_link (loc.inode, loc.parent, loc.name, &iatt);

        /* Extra ref so the pointer is valid till client is valid */
        /* FIXME: not a priority, but this can lead to some inode
           leaks if subdir is more than 1 level depth. Leak is only
           per subdir entry, and not dependent on number of
           connections, so it should be fine for now */
        inode_ref (inode);

out:
        return inode;
}

int
server_first_lookup (xlator_t *this, client_t *client, dict_t *reply)
{
        loc_t               loc    = {0, };
        struct iatt         iatt   = {0,};
        dict_t             *dict   = NULL;
        int                 ret    = 0;
        xlator_t           *xl     = client->bound_xl;
        char               *msg    = NULL;
        inode_t *inode = NULL;
        char *bname = NULL;
        char *str = NULL;
        char *tmp = NULL;
        char *saveptr = NULL;

        loc.path = "/";
        loc.name = "";
        loc.inode = xl->itable->root;
        loc.parent = NULL;
        gf_uuid_copy (loc.gfid, loc.inode->gfid);

        ret = syncop_lookup (xl, &loc, &iatt, NULL, NULL, NULL);
        if (ret < 0)
                gf_log (xl->name, GF_LOG_ERROR, "lookup on root failed: %s",
                        strerror (errno));
        /* Ignore error from lookup, don't set
         * failure in rsp->op_ret. lookup on a snapview-server
         * can fail with ESTALE
         */
        /* TODO-SUBDIR-MOUNT: validate above comment with respect to subdir lookup */

        if (client->subdir_mount) {
                str = tmp = gf_strdup (client->subdir_mount);
                dict = dict_new ();
                inode = xl->itable->root;
                bname = strtok_r (str, "/", &saveptr);
                while (bname != NULL) {
                        inode = do_path_lookup (xl, dict, inode, bname);
                        if (inode == NULL) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "first lookup on subdir (%s) failed: %s",
                                        client->subdir_mount, strerror (errno));
                                ret = -1;
                                goto fail;
                        }
                        bname = strtok_r (NULL, "/", &saveptr);
                }

                /* Can be used in server_resolve() */
                gf_uuid_copy (client->subdir_gfid, inode->gfid);
                client->subdir_inode = inode;
        }

        ret = 0;
        goto out;

fail:
        /* we should say to client, it is not possible
           to connect */
        ret = gf_asprintf (&msg, "subdirectory for mount \"%s\" is not found",
                           client->subdir_mount);
        if (-1 == ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        PS_MSG_ASPRINTF_FAILED,
                        "asprintf failed while setting error msg");
        }
        ret = dict_set_dynstr (reply, "ERROR", msg);
        if (ret < 0)
                gf_msg_debug (this->name, 0, "failed to set error "
                              "msg");

        ret = -1;
out:
        if (dict)
                dict_unref (dict);

        inode_unref (loc.inode);

        if (tmp)
                GF_FREE (tmp);

        return ret;
}

int
server_setvolume (rpcsvc_request_t *req)
{
        gf_setvolume_req     args          = {{0,},};
        gf_setvolume_rsp    *rsp           = NULL;
        client_t            *client        = NULL;
        server_ctx_t        *serv_ctx      = NULL;
        server_conf_t       *conf          = NULL;
        peer_info_t         *peerinfo      = NULL;
        dict_t              *reply         = NULL;
        dict_t              *config_params = NULL;
        dict_t              *params        = NULL;
        char                *name          = NULL;
        char                *client_uid    = NULL;
        char                *clnt_version  = NULL;
        xlator_t            *xl            = NULL;
        char                *msg           = NULL;
        xlator_t            *this          = NULL;
        int32_t              ret           = -1;
        int32_t              op_ret        = -1;
        int32_t              op_errno      = EINVAL;
        char                *buf           = NULL;
        uint32_t            opversion      = 0;
        rpc_transport_t     *xprt          = NULL;
        int32_t              fop_version   = 0;
        int32_t              mgmt_version  = 0;
        glusterfs_ctx_t     *ctx           = NULL;
        struct  _child_status *tmp         = NULL;
        char                *subdir_mount  = NULL;
        char                *client_name   = NULL;

        params = dict_new ();
        reply  = dict_new ();
        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_setvolume_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }
        ctx = THIS->ctx;

        this = req->svc->xl;
        /* this is to ensure config_params is populated with the first brick
         * details at first place if brick multiplexing is enabled
         */
        config_params = dict_copy_with_ref (this->options, NULL);

        buf = memdup (args.dict.dict_val, args.dict.dict_len);
        if (buf == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto fail;
        }

        ret = dict_unserialize (buf, args.dict.dict_len, &params);
        if (ret < 0) {
                ret = dict_set_str (reply, "ERROR",
                                    "Internal error: failed to unserialize "
                                    "request dictionary");
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set error "
                                      "msg \"%s\"", "Internal error: failed "
                                      "to unserialize request dictionary");

                op_ret = -1;
                op_errno = EINVAL;
                goto fail;
        }

        params->extra_free = buf;
        buf = NULL;

        ret = dict_get_str (params, "remote-subvolume", &name);
        if (ret < 0) {
                ret = dict_set_str (reply, "ERROR",
                                    "No remote-subvolume option specified");
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set error "
                                      "msg");

                op_ret = -1;
                op_errno = EINVAL;
                goto fail;
        }

        LOCK (&ctx->volfile_lock);
        {
                xl = get_xlator_by_name (this, name);
                if (!xl)
                        xl = this;
        }
        UNLOCK (&ctx->volfile_lock);
        if (xl == NULL) {
                ret = gf_asprintf (&msg, "remote-subvolume \"%s\" is not found",
                                   name);
                if (-1 == ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                PS_MSG_ASPRINTF_FAILED,
                                "asprintf failed while setting error msg");
                        goto fail;
                }
                ret = dict_set_dynstr (reply, "ERROR", msg);
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set error "
                                      "msg");

                op_ret = -1;
                op_errno = ENOENT;
                goto fail;
        }

        config_params = dict_copy_with_ref (xl->options, config_params);
        conf          = this->private;

        if (conf->parent_up == _gf_false) {
                /* PARENT_UP indicates that all xlators in graph are inited
                 * successfully
                 */
                op_ret = -1;
                op_errno = EAGAIN;

                ret = dict_set_str (reply, "ERROR",
                                    "xlator graph in server is not initialised "
                                    "yet. Try again later");
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set error: "
                                      "xlator graph in server is not "
                                      "initialised yet. Try again later");
                goto fail;
        }
        list_for_each_entry (tmp, &conf->child_status->status_list,
                                                                  status_list) {
                if (strcmp (tmp->name, name) == 0)
                        break;
        }
        if (!tmp->name) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        PS_MSG_CHILD_STATUS_FAILED,
                        "No xlator %s is found in "
                        "child status list", name);
        } else {
                ret = dict_set_int32 (reply, "child_up", tmp->child_up);
                if (ret < 0)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                PS_MSG_DICT_GET_FAILED,
                                "Failed to set 'child_up' for xlator %s "
                                "in the reply dict", tmp->name);
        }
        ret = dict_get_str (params, "process-uuid", &client_uid);
        if (ret < 0) {
                ret = dict_set_str (reply, "ERROR",
                                    "UUID not specified");
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set error "
                                      "msg");

                op_ret = -1;
                op_errno = EINVAL;
                goto fail;
        }

        ret = dict_get_str (params, "subdir-mount", &subdir_mount);
        if (ret < 0) {
                /* Not a problem at all as the key is optional */
        }
        ret = dict_get_str (params, "process-name", &client_name);
        if (ret < 0) {
                client_name = "unknown";
        }

        client = gf_client_get (this, &req->cred, client_uid, subdir_mount);
        if (client == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto fail;
        }

        client->client_name = gf_strdup(client_name);

        gf_msg_debug (this->name, 0, "Connected to %s", client->client_uid);

        serv_ctx = server_ctx_get (client, client->this);
        if (serv_ctx == NULL) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PS_MSG_SERVER_CTX_GET_FAILED, "server_ctx_get() "
                        "failed");
                goto fail;
        }

        if (req->trans->xl_private != client)
                req->trans->xl_private = client;

        auth_set_username_passwd (params, config_params, client);
        if (req->trans->ssl_name) {
                if (dict_set_str(params,"ssl-name",req->trans->ssl_name) != 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                PS_MSG_SSL_NAME_SET_FAILED, "failed to set "
                                "ssl_name %s", req->trans->ssl_name);
                        /* Not fatal, auth will just fail. */
                }
        }

        ret = dict_get_int32 (params, "fops-version", &fop_version);
        if (ret < 0) {
                ret = dict_set_str (reply, "ERROR",
                                    "No FOP version number specified");
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set error "
                                      "msg");
        }

        ret = dict_get_int32 (params, "mgmt-version", &mgmt_version);
        if (ret < 0) {
                ret = dict_set_str (reply, "ERROR",
                                    "No MGMT version number specified");
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set error "
                                      "msg");
        }

        ret = gf_compare_client_version (req, fop_version, mgmt_version);
        if (ret != 0) {
                ret = gf_asprintf (&msg, "version mismatch: client(%d)"
                                   " - client-mgmt(%d)",
                                   fop_version, mgmt_version);
                /* get_supported_version (req)); */
                if (-1 == ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                PS_MSG_ASPRINTF_FAILED, "asprintf failed while"
                                "setting up error msg");
                        goto fail;
                }
                ret = dict_set_dynstr (reply, "ERROR", msg);
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set error "
                                      "msg");

                op_ret = -1;
                op_errno = EINVAL;
                goto fail;
        }

        peerinfo = &req->trans->peerinfo;
        if (peerinfo) {
                ret = dict_set_static_ptr (params, "peer-info", peerinfo);
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set "
                                      "peer-info");
        }

        ret = dict_get_uint32 (params, "opversion", &opversion);
        if (ret) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PS_MSG_CLIENT_OPVERSION_GET_FAILED,
                        "Failed to get client opversion");
        }
        client->opversion = opversion;
        /* Assign op-version value to the client */
        pthread_mutex_lock (&conf->mutex);
        list_for_each_entry (xprt, &conf->xprt_list, list) {
                if (strcmp (peerinfo->identifier, xprt->peerinfo.identifier))
                        continue;
                xprt->peerinfo.max_op_version = opversion;
        }
        pthread_mutex_unlock (&conf->mutex);

        if (conf->auth_modules == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0, PS_MSG_AUTH_INIT_FAILED,
                        "Authentication module not initialized");
        }

        ret = dict_get_str (params, "client-version", &clnt_version);
        if (ret)
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PS_MSG_CLIENT_VERSION_NOT_SET,
                        "client-version not set, may be of older version");

        ret = gf_authenticate (params, config_params,
                               conf->auth_modules);

        if (ret == AUTH_ACCEPT) {
                /* Store options received from client side */
                req->trans->clnt_options = dict_ref(params);

                gf_msg (this->name, GF_LOG_INFO, 0, PS_MSG_CLIENT_ACCEPTED,
                        "accepted client from %s (version: %s)",
                        client->client_uid,
                        (clnt_version) ? clnt_version : "old");

                gf_event (EVENT_CLIENT_CONNECT, "client_uid=%s;"
                          "client_identifier=%s;server_identifier=%s;"
                          "brick_path=%s;subdir_mount=%s",
                          client->client_uid,
                          req->trans->peerinfo.identifier,
                          req->trans->myinfo.identifier,
                          name, subdir_mount);

                op_ret = 0;
                client->bound_xl = xl;

                /* Don't be confused by the below line (like how ERROR can
                   be Success), key checked on client is 'ERROR' and hence
                   we send 'Success' in this key */
                ret = dict_set_str (reply, "ERROR", "Success");
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set error "
                                      "msg");
        } else {
                gf_event (EVENT_CLIENT_AUTH_REJECT, "client_uid=%s;"
                          "client_identifier=%s;server_identifier=%s;"
                          "brick_path=%s",
                          client->client_uid,
                          req->trans->peerinfo.identifier,
                          req->trans->myinfo.identifier,
                          name);
                gf_msg (this->name, GF_LOG_ERROR, EACCES,
                        PS_MSG_AUTHENTICATE_ERROR, "Cannot authenticate client"
                        " from %s %s", client->client_uid,
                        (clnt_version) ? clnt_version : "old");

                op_ret = -1;
                op_errno = EACCES;
                ret = dict_set_str (reply, "ERROR", "Authentication failed");
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set error "
                                      "msg");
                goto fail;
        }

        if (client->bound_xl == NULL) {
                ret = dict_set_str (reply, "ERROR",
                                    "Check volfile and handshake "
                                    "options in protocol/client");
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set error "
                                      "msg");

                op_ret = -1;
                op_errno = EACCES;
                goto fail;
        }

        LOCK (&conf->itable_lock);
        {
                if (client->bound_xl->itable == NULL) {
                        /* create inode table for this bound_xl, if one doesn't
                           already exist */

                        gf_msg_trace (this->name, 0, "creating inode table with"
                                      " lru_limit=%"PRId32", xlator=%s",
                                      conf->inode_lru_limit,
                                      client->bound_xl->name);

                        /* TODO: what is this ? */
                        client->bound_xl->itable =
                                inode_table_new (conf->inode_lru_limit,
                                                 client->bound_xl);
                }
        }
        UNLOCK (&conf->itable_lock);

        ret = dict_set_str (reply, "process-uuid",
                            this->ctx->process_uuid);
        if (ret)
                gf_msg_debug (this->name, 0, "failed to set 'process-uuid'");

        /* Insert a dummy key value pair to avoid failure at client side for
         * clnt-lk-version with older clients.
         */
        ret = dict_set_uint32 (reply, "clnt-lk-version", 0);
        if (ret) {
               gf_msg (this->name, GF_LOG_WARNING, 0,
                       PS_MSG_CLIENT_LK_VERSION_ERROR, "failed to set "
                       "'clnt-lk-version'");
        }

        ret = dict_set_uint64 (reply, "transport-ptr",
                               ((uint64_t) (long) req->trans));
        if (ret)
                gf_msg_debug (this->name, 0, "failed to set 'transport-ptr'");

fail:
        /* It is important to validate the lookup on '/' as part of handshake,
           because if lookup itself can't succeed, we should communicate this
           to client. Very important in case of subdirectory mounts, where if
           client is trying to mount a non-existing directory */
        if (op_ret >= 0 && client->bound_xl->itable) {
                op_ret = server_first_lookup (this, client, reply);
                if (op_ret == -1)
                        op_errno = ENOENT;
        }

        rsp = GF_CALLOC (1, sizeof (gf_setvolume_rsp),
                         gf_server_mt_setvolume_rsp_t);
        GF_ASSERT (rsp);

        rsp->op_ret = 0;
        rsp->dict.dict_len = dict_serialized_length (reply);
        if (rsp->dict.dict_len) {
                rsp->dict.dict_val = GF_CALLOC (1, rsp->dict.dict_len,
                                                gf_server_mt_rsp_buf_t);
                if (rsp->dict.dict_val) {
                        ret = dict_serialize (reply, rsp->dict.dict_val);
                        if (ret < 0) {
                                gf_msg_debug ("server-handshake", 0, "failed "
                                              "to serialize reply dict");
                                op_ret = -1;
                                op_errno = -ret;
                        }
                }
        }
        rsp->op_ret   = op_ret;
        rsp->op_errno = gf_errno_to_error (op_errno);

        /* if bound_xl is NULL or something fails, then put the connection
         * back. Otherwise the connection would have been added to the
         * list of connections the server is maintaining and might segfault
         * during statedump when bound_xl of the connection is accessed.
         */
        if (op_ret && !xl && (client != NULL)) {
                /* We would have set the xl_private of the transport to the
                 * @conn. But if we have put the connection i.e shutting down
                 * the connection, then we should set xl_private to NULL as it
                 * would be pointing to a freed memory and would segfault when
                 * accessed upon getting DISCONNECT.
                 */
                gf_client_put (client, NULL);
                req->trans->xl_private = NULL;
        }

        /* Send the response properly */
        server_first_lookup_done (req, rsp);

        free (args.dict.dict_val);

        dict_unref (params);
        dict_unref (reply);
        if (config_params) {
                /*
                 * This might be null if we couldn't even find the translator
                 * (brick) to copy it from.
                 */
                dict_unref (config_params);
        }

        GF_FREE (buf);

        return 0;
}


int
server_ping (rpcsvc_request_t *req)
{
        gf_common_rsp rsp = {0,};

        /* Accepted */
        rsp.op_ret = 0;

        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gf_common_rsp);

        return 0;
}

int
server_set_lk_version (rpcsvc_request_t *req)
{
        int                 ret      = -1;
        gf_set_lk_ver_req   args     = {0,};
        gf_set_lk_ver_rsp   rsp      = {0,};

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_set_lk_ver_req);
        if (ret < 0) {
                /* failed to decode msg */
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        rsp.lk_ver   = args.lk_ver;
fail:
        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gf_set_lk_ver_rsp);

        free (args.uid);

        return 0;
}

rpcsvc_actor_t gluster_handshake_actors[GF_HNDSK_MAXVALUE] = {
        [GF_HNDSK_NULL]       = {"NULL",       GF_HNDSK_NULL,       server_null,           NULL, 0, DRC_NA},
        [GF_HNDSK_SETVOLUME]  = {"SETVOLUME",  GF_HNDSK_SETVOLUME,  server_setvolume,      NULL, 0, DRC_NA},
        [GF_HNDSK_GETSPEC]    = {"GETSPEC",    GF_HNDSK_GETSPEC,    server_getspec,        NULL, 0, DRC_NA},
        [GF_HNDSK_PING]       = {"PING",       GF_HNDSK_PING,       server_ping,           NULL, 0, DRC_NA},
        [GF_HNDSK_SET_LK_VER] = {"SET_LK_VER", GF_HNDSK_SET_LK_VER, server_set_lk_version, NULL, 0, DRC_NA},
};


struct rpcsvc_program gluster_handshake_prog = {
        .progname  = "GlusterFS Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .actors    = gluster_handshake_actors,
        .numactors = GF_HNDSK_MAXVALUE,
};
