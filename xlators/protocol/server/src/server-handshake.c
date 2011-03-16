/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "server.h"
#include "server-helpers.h"
#include "glusterfs3-xdr.h"
#include "compat-errno.h"
#include "glusterfs3.h"
#include "authenticate.h"

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
        if (glusterfs3_1_fop_prog.prognum == fop_prognum)
                ret = 0;

        return ret;
}

void __check_and_set (xlator_t *each, void *data)
{
        if (!strcmp (each->name,
                     ((struct __get_xl_struct *) data)->name))
                ((struct __get_xl_struct *) data)->reply = each;
}

static xlator_t *
get_xlator_by_name (xlator_t *some_xl, const char *name)
{
        struct __get_xl_struct get = {
                .name = name,
                .reply = NULL
        };

        xlator_foreach (some_xl, __check_and_set, &get);

        return get.reply;
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
                gf_log (this->name, GF_LOG_CRITICAL,
                        "the volume file got modified between earlier access "
                        "and now, this may lead to inconsistency between "
                        "clients, advised to remount client");
                temp_volfile->checksum  = checksum;
        }

out:
        return 0;
}


static size_t
getspec_build_volfile_path (xlator_t *this, const char *key, char *path,
                            size_t path_len)
{
        int              ret = -1;
        int              free_filename = 0;
        char            *filename = NULL;
        server_conf_t   *conf = NULL;
        char             data_key[256] = {0,};

        conf = this->private;

        /* Inform users that this option is changed now */
        ret = dict_get_str (this->options, "client-volume-filename",
                            &filename);
        if (ret == 0) {
                gf_log (this->name, GF_LOG_WARNING,
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
                                gf_log (this->name, GF_LOG_ERROR,
                                        "%s: invalid key", key);
                                goto out;
                        }
                }
        }

        if (!filename) {
                ret = dict_get_str (this->options,
                                    "volume-filename.default", &filename);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "no default volume filename given, "
                                "defaulting to %s", DEFAULT_VOLUME_FILE_PATH);
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
        char                 filename[ZR_PATH_MAX] = {0,};
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
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to open volume file (%s) : %s",
                                filename, strerror (errno));
                        goto out;
                }
                get_checksum_for_file (fd, &local_checksum);
                _volfile_update_checksum (this, key, local_checksum);
                close (fd);
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
        char                 filename[ZR_PATH_MAX]  = {0,};
        struct stat          stbuf                  = {0,};
        uint32_t             checksum               = 0;
        char                *key                    = NULL;
        server_conf_t       *conf                   = NULL;
        xlator_t            *this                   = NULL;
        gf_getspec_req       args                   = {0,};
        gf_getspec_rsp       rsp                    = {0,};
        server_connection_t *conn                   = NULL;

        conn = req->trans->private;
        this = req->svc->mydata;
        conf = this->private;
        if (xdr_to_glusterfs_req (req, &args, xdr_to_getspec_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                op_errno = EINVAL;
                goto fail;
        }

        ret = getspec_build_volfile_path (this, args.key,
                                          filename, sizeof (filename));
        if (ret > 0) {
                /* to allocate the proper buffer to hold the file data */
                ret = stat (filename, &stbuf);
                if (ret < 0){
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to stat %s (%s)",
                                filename, strerror (errno));
                        op_errno = errno;
                        goto fail;
                }

                spec_fd = open (filename, O_RDONLY);
                if (spec_fd < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to open %s (%s)",
                                filename, strerror (errno));
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
                ret = read (spec_fd, rsp.spec, file_len);

                close (spec_fd);
        }

        /* convert to XDR */
        op_errno = errno;
fail:
        if (!rsp.spec)
                rsp.spec = "";
        rsp.op_errno = gf_errno_to_error (op_errno);
        rsp.op_ret   = ret;

        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             (gfs_serialize_t)xdr_serialize_getspec_rsp);

        return 0;
}


int
server_setvolume (rpcsvc_request_t *req)
{
        gf_setvolume_req     args          = {{0,},};
        gf_setvolume_rsp     rsp           = {0,};
        server_connection_t *conn          = NULL;
        server_conf_t       *conf          = NULL;
        peer_info_t         *peerinfo      = NULL;
        dict_t              *reply         = NULL;
        dict_t              *config_params = NULL;
        dict_t              *params        = NULL;
        char                *name          = NULL;
        char                *process_uuid  = NULL;
        xlator_t            *xl            = NULL;
        char                *msg           = NULL;
        char                *volfile_key   = NULL;
        xlator_t            *this          = NULL;
        uint32_t             checksum      = 0;
        int32_t              ret           = -1;
        int32_t              op_ret        = -1;
        int32_t              op_errno      = EINVAL;
        int32_t              fop_version   = 0;
        int32_t              mgmt_version  = 0;
        char                *buf           = NULL;

        params = dict_new ();
        reply  = dict_new ();
        if (xdr_to_glusterfs_req (req, &args, xdr_to_setvolume_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        this = req->svc->mydata;

        config_params = dict_copy_with_ref (this->options, NULL);
        conf          = this->private;

        buf = memdup (args.dict.dict_val, args.dict.dict_len);
        if (buf == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
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
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set error msg \"%s\"",
                                "Internal error: failed to unserialize "
                                "request dictionary");

                op_ret = -1;
                op_errno = EINVAL;
                goto fail;
        }

        params->extra_free = buf;
        buf = NULL;

        ret = dict_get_str (params, "process-uuid", &process_uuid);
        if (ret < 0) {
                ret = dict_set_str (reply, "ERROR",
                                    "UUID not specified");
                if (ret < 0)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set error msg");

                op_ret = -1;
                op_errno = EINVAL;
                goto fail;
        }


        conn = server_connection_get (this, process_uuid);
        if (req->trans->xl_private != conn)
                req->trans->xl_private = conn;

        ret = dict_get_int32 (params, "fops-version", &fop_version);
        if (ret < 0) {
                ret = dict_set_str (reply, "ERROR",
                                    "No FOP version number specified");
                if (ret < 0)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set error msg");
        }

        ret = dict_get_int32 (params, "mgmt-version", &mgmt_version);
        if (ret < 0) {
                ret = dict_set_str (reply, "ERROR",
                                    "No MGMT version number specified");
                if (ret < 0)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set error msg");
        }

        ret = gf_compare_client_version (req, fop_version, mgmt_version);
        if (ret != 0) {
                ret = gf_asprintf (&msg, "version mismatch: client(%d)"
                                   " - client-mgmt(%d)",
                                   fop_version, mgmt_version);
                /* get_supported_version (req)); */
                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "asprintf failed while setting up error msg");
                        goto fail;
                }
                ret = dict_set_dynstr (reply, "ERROR", msg);
                if (ret < 0)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set error msg");

                op_ret = -1;
                op_errno = EINVAL;
                goto fail;
        }

        ret = dict_get_str (params, "remote-subvolume", &name);
        if (ret < 0) {
                ret = dict_set_str (reply, "ERROR",
                                    "No remote-subvolume option specified");
                if (ret < 0)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set error msg");

                op_ret = -1;
                op_errno = EINVAL;
                goto fail;
        }

        xl = get_xlator_by_name (this, name);
        if (xl == NULL) {
                ret = gf_asprintf (&msg, "remote-subvolume \"%s\" is not found",
                                   name);
                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "asprintf failed while setting error msg");
                        goto fail;
                }
                ret = dict_set_dynstr (reply, "ERROR", msg);
                if (ret < 0)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set error msg");

                op_ret = -1;
                op_errno = ENOENT;
                goto fail;
        }

        if (conf->verify_volfile) {
                ret = dict_get_uint32 (params, "volfile-checksum", &checksum);
                if (ret == 0) {
                        ret = dict_get_str (params, "volfile-key",
                                            &volfile_key);
                        if (ret)
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "failed to set 'volfile-key'");

                        ret = _validate_volfile_checksum (this, volfile_key,
                                                          checksum);
                        if (-1 == ret) {
                                ret = dict_set_str (reply, "ERROR",
                                                    "volume-file checksum "
                                                    "varies from earlier "
                                                    "access");
                                if (ret < 0)
                                        gf_log (this->name, GF_LOG_DEBUG,
                                                "failed to set error msg");

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
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set peer-info");
        }
        if (conf->auth_modules == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Authentication module not initialized");
        }

        ret = gf_authenticate (params, config_params,
                               conf->auth_modules);

        if (ret == AUTH_ACCEPT) {
                gf_log (this->name, GF_LOG_INFO,
                        "accepted client from %s",
                        (peerinfo)?peerinfo->identifier:"");
                op_ret = 0;
                conn->bound_xl = xl;
                ret = dict_set_str (reply, "ERROR", "Success");
                if (ret < 0)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set error msg");
        } else {
                gf_log (this->name, GF_LOG_ERROR,
                        "Cannot authenticate client from %s",
                        (peerinfo)? peerinfo->identifier:"<>");
                op_ret = -1;
                op_errno = EACCES;
                ret = dict_set_str (reply, "ERROR", "Authentication failed");
                if (ret < 0)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set error msg");

                goto fail;
        }

        if (conn->bound_xl == NULL) {
                ret = dict_set_str (reply, "ERROR",
                                    "Check volfile and handshake "
                                    "options in protocol/client");
                if (ret < 0)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set error msg");

                op_ret = -1;
                op_errno = EACCES;
                goto fail;
        }

        if ((conn->bound_xl != NULL) &&
            (ret >= 0)                   &&
            (conn->bound_xl->itable == NULL)) {
                /* create inode table for this bound_xl, if one doesn't
                   already exist */

                gf_log (this->name, GF_LOG_TRACE,
                        "creating inode table with lru_limit=%"PRId32", "
                        "xlator=%s", conf->inode_lru_limit,
                        conn->bound_xl->name);

                /* TODO: what is this ? */
                conn->bound_xl->itable = inode_table_new (conf->inode_lru_limit,
                                                          conn->bound_xl);
        }

        ret = dict_set_str (reply, "process-uuid",
                            this->ctx->process_uuid);
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to set 'process-uuid'");

        ret = dict_set_uint64 (reply, "transport-ptr",
                               ((uint64_t) (long) req->trans));
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to set 'transport-ptr'");

fail:
        rsp.dict.dict_len = dict_serialized_length (reply);
        if (rsp.dict.dict_len < 0) {
                gf_log ("server-handshake", GF_LOG_DEBUG,
                        "failed to get serialized length of reply dict");
                op_ret   = -1;
                op_errno = EINVAL;
                rsp.dict.dict_len = 0;
        }

        if (rsp.dict.dict_len) {
                rsp.dict.dict_val = GF_CALLOC (1, rsp.dict.dict_len,
                                               gf_server_mt_rsp_buf_t);
                if (rsp.dict.dict_val) {
                        ret = dict_serialize (reply, rsp.dict.dict_val);
                        if (ret < 0) {
                                gf_log ("server-handshake", GF_LOG_DEBUG,
                                        "failed to serialize reply dict");
                                op_ret = -1;
                                op_errno = -ret;
                        }
                }
        }
        rsp.op_ret   = op_ret;
        rsp.op_errno = gf_errno_to_error (op_errno);

        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             (gfs_serialize_t)xdr_serialize_setvolume_rsp);


        if (args.dict.dict_val)
                free (args.dict.dict_val);

        if (rsp.dict.dict_val)
                GF_FREE (rsp.dict.dict_val);

        dict_unref (params);
        dict_unref (reply);
        dict_unref (config_params);

        if (buf) {
                GF_FREE (buf);
        }

        return 0;
}


int
server_ping (rpcsvc_request_t *req)
{
        gf_common_rsp rsp = {0,};

        /* Accepted */
        rsp.op_ret = 0;

        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);

        return 0;
}


rpcsvc_actor_t gluster_handshake_actors[] = {
        [GF_HNDSK_NULL]      = {"NULL",      GF_HNDSK_NULL,      server_null, NULL, NULL },
        [GF_HNDSK_SETVOLUME] = {"SETVOLUME", GF_HNDSK_SETVOLUME, server_setvolume, NULL, NULL },
        [GF_HNDSK_GETSPEC]   = {"GETSPEC",   GF_HNDSK_GETSPEC,   server_getspec, NULL, NULL },
        [GF_HNDSK_PING]      = {"PING",      GF_HNDSK_PING,      server_ping, NULL, NULL },
};


struct rpcsvc_program gluster_handshake_prog = {
        .progname  = "GlusterFS Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .actors    = gluster_handshake_actors,
        .numactors = GF_HNDSK_MAXVALUE,
};
