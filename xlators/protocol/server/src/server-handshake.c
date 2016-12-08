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
_volfile_update_checksum (xlator_t *this, char *key, uint32_t checksum)
{
        server_conf_t       *conf         = NULL;
        struct _volfile_ctx *temp_volfile = NULL;

        conf         = this->private;
        temp_volfile = conf->volfile;

        while (temp_volfile) {
                if ((NULL == key) && (NULL == temp_volfile->key))
                        break;
                if ((NULL == key) || (NULL == temp_volfile->key)) {
                        temp_volfile = temp_volfile->next;
                        continue;
                }
                if (strcmp (temp_volfile->key, key) == 0)
                        break;
                temp_volfile = temp_volfile->next;
        }

        if (!temp_volfile) {
                temp_volfile = GF_CALLOC (1, sizeof (struct _volfile_ctx),
                                          gf_server_mt_volfile_ctx_t);
                if (!temp_volfile)
                        goto out;
                temp_volfile->next  = conf->volfile;
                temp_volfile->key   = (key)? gf_strdup (key): NULL;
                temp_volfile->checksum = checksum;

                conf->volfile = temp_volfile;
                goto out;
        }

        if (temp_volfile->checksum != checksum) {
                gf_msg (this->name, GF_LOG_INFO, 0, PS_MSG_REMOUNT_CLIENT_REQD,
                        "the volume file was modified between a prior access "
                        "and now. This may lead to inconsistency between "
                        "clients, you are advised to remount client");
                temp_volfile->checksum  = checksum;
        }

out:
        return 0;
}


static size_t
getspec_build_volfile_path (xlator_t *this, const char *key, char *path,
                            size_t path_len)
{
        char            *filename      = NULL;
        server_conf_t   *conf          = NULL;
        int              ret           = -1;
        int              free_filename = 0;
        char             data_key[256] = {0,};

        conf = this->private;

        /* Inform users that this option is changed now */
        ret = dict_get_str (this->options, "client-volume-filename",
                            &filename);
        if (ret == 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PS_MSG_DEFAULTING_FILE,
                        "option 'client-volume-filename' is changed to "
                        "'volume-filename.<key>' which now takes 'key' as an "
                        "option to choose/fetch different files from server. "
                        "Refer documentation or contact developers for more "
                        "info. Currently defaulting to given file '%s'",
                        filename);
        }

        if (key && !filename) {
                sprintf (data_key, "volume-filename.%s", key);
                ret = dict_get_str (this->options, data_key, &filename);
                if (ret < 0) {
                        /* Make sure that key doesn't contain "../" in path */
                        if ((gf_strstr (key, "/", "..")) == -1) {
                                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                        PS_MSG_INVALID_ENTRY, "%s: invalid "
                                        "key", key);
                                goto out;
                        }
                }
        }

        if (!filename) {
                ret = dict_get_str (this->options,
                                    "volume-filename.default", &filename);
                if (ret < 0) {
                        gf_msg_debug (this->name, 0, "no default volume "
                                      "filename given, defaulting to %s",
                                      DEFAULT_VOLUME_FILE_PATH);
                }
        }

        if (!filename && key) {
                ret = gf_asprintf (&filename, "%s/%s.vol", conf->conf_dir, key);
                if (-1 == ret)
                        goto out;

                free_filename = 1;
        }
        if (!filename)
                filename = DEFAULT_VOLUME_FILE_PATH;

        ret = -1;

        if ((filename) && (path_len > strlen (filename))) {
                strcpy (path, filename);
                ret = strlen (filename);
        }

out:
        if (free_filename)
                GF_FREE (filename);

        return ret;
}

int
_validate_volfile_checksum (xlator_t *this, char *key,
                            uint32_t checksum)
{
        char                 filename[PATH_MAX] = {0,};
        server_conf_t       *conf         = NULL;
        struct _volfile_ctx *temp_volfile = NULL;
        int                  ret          = 0;
        int                  fd           = 0;
        uint32_t             local_checksum = 0;

        conf         = this->private;
        temp_volfile = conf->volfile;

        if (!checksum)
                goto out;

        if (!temp_volfile) {
                ret = getspec_build_volfile_path (this, key, filename,
                                                  sizeof (filename));
                if (ret <= 0)
                        goto out;
                fd = open (filename, O_RDONLY);
                if (-1 == fd) {
                        ret = 0;
                        gf_msg (this->name, GF_LOG_INFO, errno,
                                PS_MSG_VOL_FILE_OPEN_FAILED,
                                "failed to open volume file (%s) : %s",
                                filename, strerror (errno));
                        goto out;
                }
                get_checksum_for_file (fd, &local_checksum);
                _volfile_update_checksum (this, key, local_checksum);
                sys_close (fd);
        }

        temp_volfile = conf->volfile;
        while (temp_volfile) {
                if ((NULL == key) && (NULL == temp_volfile->key))
                        break;
                if ((NULL == key) || (NULL == temp_volfile->key)) {
                        temp_volfile = temp_volfile->next;
                        continue;
                }
                if (strcmp (temp_volfile->key, key) == 0)
                        break;
                temp_volfile = temp_volfile->next;
        }

        if (!temp_volfile)
                goto out;

        if ((temp_volfile->checksum) &&
            (checksum != temp_volfile->checksum))
                ret = -1;

out:
        return ret;
}


int
server_getspec (rpcsvc_request_t *req)
{
        int32_t              ret                    = -1;
        int32_t              op_errno               = ENOENT;
        int32_t              spec_fd                = -1;
        size_t               file_len               = 0;
        char                 filename[PATH_MAX]  = {0,};
        struct stat          stbuf                  = {0,};
        uint32_t             checksum               = 0;
        char                *key                    = NULL;
        server_conf_t       *conf                   = NULL;
        xlator_t            *this                   = NULL;
        gf_getspec_req       args                   = {0,};
        gf_getspec_rsp       rsp                    = {0,};

        this = req->svc->xl;
        conf = this->private;
        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_getspec_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                op_errno = EINVAL;
                goto fail;
        }

        ret = getspec_build_volfile_path (this, args.key,
                                          filename, sizeof (filename));
        if (ret > 0) {
                /* to allocate the proper buffer to hold the file data */
                ret = sys_stat (filename, &stbuf);
                if (ret < 0){
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                PS_MSG_STAT_ERROR, "Unable to stat %s (%s)",
                                filename, strerror (errno));
                        op_errno = errno;
                        goto fail;
                }

                spec_fd = open (filename, O_RDONLY);
                if (spec_fd < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                PS_MSG_FILE_OP_FAILED, "Unable to open %s "
                                "(%s)", filename, strerror (errno));
                        op_errno = errno;
                        goto fail;
                }
                ret = file_len = stbuf.st_size;

                if (conf->verify_volfile) {
                        get_checksum_for_file (spec_fd, &checksum);
                        _volfile_update_checksum (this, key, checksum);
                }
        } else {
                op_errno = ENOENT;
        }

        if (file_len) {
                rsp.spec = GF_CALLOC (file_len, sizeof (char),
                                      gf_server_mt_rsp_buf_t);
                if (!rsp.spec) {
                        ret = -1;
                        op_errno = ENOMEM;
                        goto fail;
                }
                ret = sys_read (spec_fd, rsp.spec, file_len);
        }

        /* convert to XDR */
        op_errno = errno;
fail:
        if (!rsp.spec)
                rsp.spec = "";
        rsp.op_errno = gf_errno_to_error (op_errno);
        rsp.op_ret   = ret;

        if (spec_fd != -1)
                sys_close (spec_fd);

        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gf_getspec_rsp);

        return 0;
}

void
server_first_lookup_done (rpcsvc_request_t *req, gf_setvolume_rsp *rsp) {

        server_submit_reply (NULL, req, rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gf_setvolume_rsp);

        GF_FREE (rsp->dict.dict_val);
        GF_FREE (rsp);
}


int
server_first_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       inode_t *inode, struct iatt *buf, dict_t *xattr,
                       struct iatt *postparent)
{
        rpcsvc_request_t     *req = NULL;
        gf_setvolume_rsp     *rsp = NULL;

        req = cookie;
        rsp = frame->local;
        frame->local = NULL;

        if (op_ret < 0 || buf == NULL)
                gf_log (this->name, GF_LOG_WARNING, "server first lookup failed"
                        " on root inode: %s", strerror (op_errno));

        /* Ignore error from lookup, don't set
         * failure in rsp->op_ret. lookup on a snapview-server
         * can fail with ESTALE
         */
        server_first_lookup_done (req, rsp);

        STACK_DESTROY (frame->root);

        return 0;
}

int
server_first_lookup (xlator_t *this, xlator_t *xl, rpcsvc_request_t *req,
                     gf_setvolume_rsp *rsp)
{
        call_frame_t       *frame  = NULL;
        loc_t               loc    = {0, };

        loc.path = "/";
        loc.name = "";
        loc.inode = xl->itable->root;
        loc.parent = NULL;
        gf_uuid_copy (loc.gfid, loc.inode->gfid);

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                gf_log ("fuse", GF_LOG_ERROR, "failed to create frame");
                goto err;
        }

        frame->local = (void *)rsp;
        frame->root->uid = frame->root->gid = 0;
        frame->root->pid = -1;
        frame->root->type = GF_OP_TYPE_FOP;

        STACK_WIND_COOKIE (frame, server_first_lookup_cbk, (void *)req, xl,
                           xl->fops->lookup, &loc, NULL);

        return 0;

err:
        rsp->op_ret = -1;
        rsp->op_errno = ENOMEM;
        server_first_lookup_done (req, rsp);

        frame->local = NULL;
        STACK_DESTROY (frame->root);

        return -1;
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
        char                *volfile_key   = NULL;
        xlator_t            *this          = NULL;
        uint32_t             checksum      = 0;
        int32_t              ret           = -1;
        int32_t              op_ret        = -1;
        int32_t              op_errno      = EINVAL;
        uint32_t             lk_version    = 0;
        char                *buf           = NULL;
        gf_boolean_t        cancelled      = _gf_false;
        uint32_t            opversion      = 0;
        rpc_transport_t     *xprt          = NULL;
        int32_t              fop_version   = 0;
        int32_t              mgmt_version  = 0;


        params = dict_new ();
        reply  = dict_new ();
        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_setvolume_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        this = req->svc->xl;

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

        xl = get_xlator_by_name (this, name);
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

        config_params = dict_copy_with_ref (xl->options, NULL);
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

        ret = dict_set_int32 (reply, "child_up", conf->child_up);
        if (ret < 0)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        PS_MSG_DICT_GET_FAILED, "Failed to set 'child_up' "
                        "in the reply dict");

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

        /*lk_verion :: [1..2^31-1]*/
        ret = dict_get_uint32 (params, "clnt-lk-version", &lk_version);
        if (ret < 0) {
                ret = dict_set_str (reply, "ERROR",
                                    "lock state version not supplied");
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set error "
                                      "msg");

                op_ret = -1;
                op_errno = EINVAL;
                goto fail;
        }

        client = gf_client_get (this, &req->cred, client_uid);
        if (client == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto fail;
        }

        gf_msg_debug (this->name, 0, "Connected to %s", client->client_uid);
        cancelled = server_cancel_grace_timer (this, client);
        if (cancelled)//Do gf_client_put on behalf of grace-timer-handler.
                gf_client_put (client, NULL);

        serv_ctx = server_ctx_get (client, client->this);
        if (serv_ctx == NULL) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PS_MSG_SERVER_CTX_GET_FAILED, "server_ctx_get() "
                        "failed");
                goto fail;
        }

        if (serv_ctx->lk_version != 0 &&
            serv_ctx->lk_version != lk_version) {
                (void) server_connection_cleanup (this, client,
                                                  INTERNAL_LOCKS | POSIX_LOCKS);
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

        if (conf->verify_volfile) {
                ret = dict_get_uint32 (params, "volfile-checksum", &checksum);
                if (ret == 0) {
                        ret = dict_get_str (params, "volfile-key",
                                            &volfile_key);
                        if (ret)
                                gf_msg_debug (this->name, 0, "failed to set "
                                              "'volfile-key'");

                        ret = _validate_volfile_checksum (this, volfile_key,
                                                          checksum);
                        if (-1 == ret) {
                                ret = dict_set_str (reply, "ERROR",
                                                    "volume-file checksum "
                                                    "varies from earlier "
                                                    "access");
                                if (ret < 0)
                                        gf_msg_debug (this->name, 0, "failed "
                                                      "to set error msg");

                                op_ret   = -1;
                                op_errno = ESTALE;
                                goto fail;
                        }
                }
        }


        peerinfo = &req->trans->peerinfo;
        if (peerinfo) {
                ret = dict_set_static_ptr (params, "peer-info", peerinfo);
                if (ret < 0)
                        gf_msg_debug (this->name, 0, "failed to set "
                                      "peer-info");
        }

        ret = dict_get_uint32 (params, "opversion", &opversion);
        if (ret)
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PS_MSG_CLIENT_OPVERSION_GET_FAILED,
                        "Failed to get client opversion");

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
                          "brick_path=%s",
                          client->client_uid,
                          req->trans->peerinfo.identifier,
                          req->trans->myinfo.identifier,
                          name);

                op_ret = 0;
                client->bound_xl = xl;
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

        ret = dict_set_uint32 (reply, "clnt-lk-version", serv_ctx->lk_version);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        PS_MSG_CLIENT_LK_VERSION_ERROR, "failed to set "
                        "'clnt-lk-version'");

        ret = dict_set_uint64 (reply, "transport-ptr",
                               ((uint64_t) (long) req->trans));
        if (ret)
                gf_msg_debug (this->name, 0, "failed to set 'transport-ptr'");

fail:
        rsp = GF_CALLOC (1, sizeof (gf_setvolume_rsp),
                         gf_server_mt_setvolume_rsp_t);
        GF_ASSERT (rsp);

        rsp->op_ret = 0;
        rsp->dict.dict_len = dict_serialized_length (reply);
        if (rsp->dict.dict_len > UINT_MAX) {
                gf_msg_debug ("server-handshake", 0, "failed to get serialized"
                               " length of reply dict");
                op_ret   = -1;
                op_errno = EINVAL;
                rsp->dict.dict_len = 0;
        }

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

        if (op_ret >= 0 && client->bound_xl->itable)
                server_first_lookup (this, client->bound_xl, req, rsp);
        else
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
        int                 op_ret   = -1;
        int                 op_errno = EINVAL;
        gf_set_lk_ver_req   args     = {0,};
        gf_set_lk_ver_rsp   rsp      = {0,};
        client_t           *client   = NULL;
        server_ctx_t       *serv_ctx = NULL;
        xlator_t           *this     = NULL;

        this = req->svc->xl;
        //TODO: Decide on an appropriate errno for the error-path
        //below
        if (!this)
                goto fail;

        op_ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_set_lk_ver_req);
        if (op_ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        client = gf_client_get (this, &req->cred, args.uid);
        serv_ctx = server_ctx_get (client, client->this);
        if (serv_ctx == NULL) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PS_MSG_SERVER_CTX_GET_FAILED, "server_ctx_get() "
                        "failed");
                goto fail;
        }

        serv_ctx->lk_version = args.lk_ver;
        rsp.lk_ver   = args.lk_ver;

        op_ret = 0;
fail:
        if (client)
                gf_client_put (client, NULL);

        rsp.op_ret   = op_ret;
        rsp.op_errno = op_errno;
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
