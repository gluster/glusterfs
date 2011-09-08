/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
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

#include "common-utils.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"

#include <signal.h>

int
glusterd_handle_log_filename (rpcsvc_request_t *req)
{
        int32_t                   ret     = -1;
        gf1_cli_log_filename_req  cli_req = {0,};
        dict_t                   *dict    = NULL;
        glusterd_op_t             cli_op = GD_OP_LOG_FILENAME;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf1_cli_log_filename_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received log filename req "
                "for volume %s", cli_req.volname);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_dynmstr (dict, "volname", cli_req.volname);
        if (ret)
                goto out;
        ret = dict_set_dynmstr (dict, "brick", cli_req.brick);
        if (ret)
                goto out;
        ret = dict_set_dynmstr (dict, "path", cli_req.path);
        if (ret)
                goto out;

        ret = glusterd_op_begin (req, GD_OP_LOG_FILENAME, dict);

out:
        if (ret && dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret)
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     NULL, "operation failed");

        return ret;
}

int
glusterd_handle_log_locate (rpcsvc_request_t *req)
{
        int32_t                 ret     = -1;
        gf1_cli_log_locate_req  cli_req = {0,};
        gf1_cli_log_locate_rsp  rsp     = {0,};
        glusterd_volinfo_t     *volinfo = NULL;
        glusterd_brickinfo_t   *brickinfo = NULL;
        char                    tmp_str[PATH_MAX] = {0,};
        char                   *tmp_brick = NULL;
        uint32_t                found = 0;
        glusterd_brickinfo_t   *tmpbrkinfo = NULL;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf1_cli_log_locate_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received log locate req "
                "for volume %s", cli_req.volname);

        if (strchr (cli_req.brick, ':')) {
                /* TODO: need to get info of only that brick and then
                   tell what is the exact location */
                tmp_brick = gf_strdup (cli_req.brick);
                if (!tmp_brick)
                        goto out;

                gf_log ("", GF_LOG_DEBUG, "brick : %s", cli_req.brick);
                ret = glusterd_brickinfo_from_brick (tmp_brick, &tmpbrkinfo);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Cannot get brickinfo from the brick");
                        goto out;
                }
        }

        ret = glusterd_volinfo_find (cli_req.volname, &volinfo);
        if (ret) {
                rsp.path = "request sent on non-existent volume";
                goto out;
        }

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (tmpbrkinfo) {
                        ret = glusterd_resolve_brick (tmpbrkinfo);
                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR,
                                        "cannot resolve the brick");
                                goto out;
                        }
                        if (uuid_compare (tmpbrkinfo->uuid, brickinfo->uuid) || strcmp (brickinfo->path, tmpbrkinfo->path))
                                continue;
                }

                if (brickinfo->logfile) {
                        strcpy (tmp_str, brickinfo->logfile);
                        rsp.path = dirname (tmp_str);
                        found = 1;
                } else {
                        snprintf (tmp_str, PATH_MAX, "%s/bricks/",
                                  DEFAULT_LOG_FILE_DIRECTORY);
                        rsp.path = tmp_str;
                        found = 1;
                }
                break;
        }

        if (!found) {
                snprintf (tmp_str, PATH_MAX, "brick %s:%s does not exitst in the volume %s",
                          tmpbrkinfo->hostname, tmpbrkinfo->path, cli_req.volname);
                rsp.path = tmp_str;
        }

        ret = 0;
out:
        if (tmp_brick)
                GF_FREE (tmp_brick);
        if (tmpbrkinfo)
                glusterd_brickinfo_delete (tmpbrkinfo);
        rsp.op_ret = ret;
        if (!rsp.path)
                rsp.path = "Operation failed";

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf1_cli_log_locate_rsp);

        if (cli_req.brick)
                free (cli_req.brick); //its malloced by xdr
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_log_level (rpcsvc_request_t *req)
{
        int32_t                ret       = -1;
        dict_t                *dict      = NULL;
        gf1_cli_log_level_req  cli_req   = {0,};
        glusterd_op_t         cli_op    = GD_OP_LOG_LEVEL;

        GF_ASSERT(req);


        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf1_cli_log_level_req)) {
                gf_log ("glusterd", GF_LOG_ERROR, "Failed to decode rpc message");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_DEBUG, "Got log level request for: Volume [%s]"
                " Xlator [%s] LogLevel [\"%s\"]", cli_req.volname, cli_req.xlator, cli_req.loglevel);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_dynmstr (dict, "volname", cli_req.volname);
        if (ret)
                goto out;

        ret = dict_set_dynmstr (dict, "xlator", cli_req.xlator);
        if (ret)
                goto out;

        ret = dict_set_dynmstr (dict, "loglevel", cli_req.loglevel);
        if (ret)
                goto out;

        ret = glusterd_op_begin (req, cli_op, dict);

 out:
        if (ret && dict)
                dict_unref (dict);

        glusterd_friend_sm();
        glusterd_op_sm();

        if (ret)
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req, NULL,
                                                     "Operation failed");

        return ret;
}

int
glusterd_handle_log_rotate (rpcsvc_request_t *req)
{
        int32_t                 ret     = -1;
        gf1_cli_log_rotate_req  cli_req = {0,};
        dict_t                 *dict    = NULL;
        glusterd_op_t           cli_op = GD_OP_LOG_ROTATE;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf1_cli_log_rotate_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received log rotate req "
                "for volume %s", cli_req.volname);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_dynmstr (dict, "volname", cli_req.volname);
        if (ret)
                goto out;

        ret = dict_set_dynmstr (dict, "brick", cli_req.brick);
        if (ret)
                goto out;

        ret = dict_set_uint64 (dict, "rotate-key", (uint64_t)time (NULL));
        if (ret)
                goto out;

        ret = glusterd_op_begin (req, GD_OP_LOG_ROTATE, dict);

out:
        if (ret && dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret)
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     NULL, "operation failed");

        return ret;
}

/* op-sm */
int
glusterd_op_stage_log_filename (dict_t *dict, char **op_errstr)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        gf_boolean_t                            exists = _gf_false;
        char                                    msg[2048] = {0};
        char                                    *path = NULL;
        char                                    hostname[2048] = {0};
        char                                    *brick = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if (!exists || ret) {
                snprintf (msg, sizeof (msg), "Volume %s does not exist",
                          volname);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "brick", &brick);
        if (ret)
                goto out;

        if (strchr (brick, ':')) {
                ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                              NULL);
                if (ret) {
                        snprintf (msg, sizeof (msg), "Incorrect brick %s "
                                  "for volume %s", brick, volname);
                        gf_log ("", GF_LOG_ERROR, "%s", msg);
                        *op_errstr = gf_strdup (msg);
                        goto out;
                }
        }

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "path not found");
                goto out;
        }

        ret = gethostname (hostname, sizeof (hostname));
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get hostname, error:%s",
                strerror (errno));
                gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = glusterd_brick_create_path (hostname, path, volinfo->volume_id,
                                          0777, op_errstr);
        if (ret)
                goto out;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_op_stage_log_rotate (dict_t *dict, char **op_errstr)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        gf_boolean_t                            exists = _gf_false;
        char                                    msg[2048] = {0};
        char                                    *brick = NULL;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if (!exists) {
                snprintf (msg, sizeof (msg), "Volume %s does not exist",
                          volname);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        if (_gf_false == glusterd_is_volume_started (volinfo)) {
                snprintf (msg, sizeof (msg), "Volume %s needs to be started before"
                          " log rotate.", volname);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "brick", &brick);
        if (ret)
                goto out;

        if (strchr (brick, ':')) {
                ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                              NULL);
                if (ret) {
                        snprintf (msg, sizeof (msg), "Incorrect brick %s "
                                  "for volume %s", brick, volname);
                        gf_log ("", GF_LOG_ERROR, "%s", msg);
                        *op_errstr = gf_strdup (msg);
                        goto out;
                }
        }
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_op_stage_log_level (dict_t *dict, char **op_errstr)
{
        int                 ret            = -1;
        gf_boolean_t        exists         = _gf_false;
        dict_t             *val_dict       = NULL;
        char               *volname        = NULL;
        char               *xlator         = NULL;
        char               *loglevel       = NULL;
        glusterd_volinfo_t *volinfo        = NULL;
        glusterd_conf_t    *priv           = NULL;
        xlator_t           *this           = NULL;
        char msg[2048]                     = {0,};

        GF_ASSERT (dict);
        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT(priv);

        val_dict = dict_new ();
        if (!val_dict)
                goto out;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        /*
         * check for existence of the gieven volume
         */
        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if (!exists || ret) {
                snprintf (msg, sizeof(msg), "Volume %s does not exist", volname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);

                *op_errstr = gf_strdup(msg);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "xlator", &xlator);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get translator name");
                goto out;
        }

        ret = dict_get_str (dict, "loglevel", &loglevel);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get loglevel");
                goto out;
        }

        ret = 0;

 out:
        if (val_dict)
                dict_unref (val_dict);

        if (ret) {
                if (!(*op_errstr)) {
                        *op_errstr = gf_strdup ("Error, Validation Failed");
                        gf_log ("glusterd", GF_LOG_DEBUG, "Error, Cannot Validate option: %s",
                                *op_errstr);
                }
        }

        gf_log ("glusterd", GF_LOG_DEBUG, "Returning: %d", ret);
        return ret;
}

int
glusterd_op_log_filename (dict_t *dict)
{
        int                   ret                = -1;
        glusterd_conf_t      *priv               = NULL;
        glusterd_volinfo_t   *volinfo            = NULL;
        glusterd_brickinfo_t *brickinfo          = NULL;
        xlator_t             *this               = NULL;
        char                 *volname            = NULL;
        char                 *brick              = NULL;
        char                 *path               = NULL;
        char                  logfile[PATH_MAX]  = {0,};
        char                  exp_path[PATH_MAX] = {0,};
        struct stat           stbuf              = {0,};
        int                   valid_brick        = 0;
        glusterd_brickinfo_t *tmpbrkinfo         = NULL;
        char*                new_logdir         = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "volname not found");
                goto out;
        }

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "path not found");
                goto out;
        }

        ret = dict_get_str (dict, "brick", &brick);
        if (ret)
                goto out;

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret)
                goto out;

        if (!strchr (brick, ':')) {
                brick = NULL;
                ret = stat (path, &stbuf);
                if (ret || !S_ISDIR (stbuf.st_mode)) {
                        ret = -1;
                        gf_log ("", GF_LOG_ERROR, "not a directory");
                        goto out;
                }
                new_logdir = gf_strdup (path);
                if (!new_logdir) {
                        ret = -1;
                        gf_log ("", GF_LOG_ERROR, "Out of memory");
                        goto out;
                }
                if (volinfo->logdir)
                        GF_FREE (volinfo->logdir);
                volinfo->logdir = new_logdir;
        } else {
                ret = glusterd_brickinfo_from_brick (brick, &tmpbrkinfo);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "cannot get brickinfo from brick");
                        goto out;
                }
        }


        ret = -1;
        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {

                if (uuid_is_null (brickinfo->uuid)) {
                        ret = glusterd_resolve_brick (brickinfo);
                        if (ret)
                                goto out;
                }

                /* check if the brickinfo belongs to the 'this' machine */
                if (uuid_compare (brickinfo->uuid, priv->uuid))
                        continue;

                if (brick && strcmp (tmpbrkinfo->path,brickinfo->path))
                        continue;

                valid_brick = 1;

                /* If there are more than one brick in 'this' server, its an
                 * extra check, but it doesn't harm functionality
                 */
                ret = stat (path, &stbuf);
                if (ret || !S_ISDIR (stbuf.st_mode)) {
                        ret = -1;
                        gf_log ("", GF_LOG_ERROR, "not a directory");
                        goto out;
                }

                GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, exp_path);

                snprintf (logfile, PATH_MAX, "%s/%s.log", path, exp_path);

                if (brickinfo->logfile)
                        GF_FREE (brickinfo->logfile);
                brickinfo->logfile = gf_strdup (logfile);
                ret = 0;

                /* If request was for brick, only one iteration is enough */
                if (brick)
                        break;
        }

        if (ret && !valid_brick)
                ret = 0;
out:
        if (tmpbrkinfo)
                glusterd_brickinfo_delete (tmpbrkinfo);

        return ret;
}

int
glusterd_op_log_rotate (dict_t *dict)
{
        int                   ret                = -1;
        glusterd_conf_t      *priv               = NULL;
        glusterd_volinfo_t   *volinfo            = NULL;
        glusterd_brickinfo_t *brickinfo          = NULL;
        xlator_t             *this               = NULL;
        char                 *volname            = NULL;
        char                 *brick              = NULL;
        char                  path[PATH_MAX]     = {0,};
        char                  logfile[PATH_MAX]  = {0,};
        char                  pidfile[PATH_MAX]  = {0,};
        FILE                 *file               = NULL;
        pid_t                 pid                = 0;
        uint64_t              key                = 0;
        int                   valid_brick        = 0;
        glusterd_brickinfo_t *tmpbrkinfo         = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "volname not found");
                goto out;
        }

        ret = dict_get_uint64 (dict, "rotate-key", &key);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "rotate key not found");
                goto out;
        }

        ret = dict_get_str (dict, "brick", &brick);
        if (ret)
                goto out;

        if (!strchr (brick, ':'))
                brick = NULL;
        else {
                ret = glusterd_brickinfo_from_brick (brick, &tmpbrkinfo);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "cannot get brickinfo from brick");
                        goto out;
                }
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret)
                goto out;

        ret = -1;
        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (uuid_compare (brickinfo->uuid, priv->uuid))
                        continue;

                if (brick &&
                    (strcmp (tmpbrkinfo->hostname, brickinfo->hostname) ||
                     strcmp (tmpbrkinfo->path,brickinfo->path)))
                        continue;

                valid_brick = 1;

                GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);
                GLUSTERD_GET_BRICK_PIDFILE (pidfile, path, brickinfo->hostname,
                                            brickinfo->path);

                file = fopen (pidfile, "r+");
                if (!file) {
                        gf_log ("", GF_LOG_ERROR, "Unable to open pidfile: %s",
                                pidfile);
                        ret = -1;
                        goto out;
                }

                ret = fscanf (file, "%d", &pid);
                if (ret <= 0) {
                        gf_log ("", GF_LOG_ERROR, "Unable to read pidfile: %s",
                                pidfile);
                        ret = -1;
                        goto out;
                }
                fclose (file);
                file = NULL;

                snprintf (logfile, PATH_MAX, "%s.%"PRIu64,
                          brickinfo->logfile, key);

                ret = rename (brickinfo->logfile, logfile);
                if (ret)
                        gf_log ("", GF_LOG_WARNING, "rename failed");

                ret = kill (pid, SIGHUP);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to SIGHUP to %d", pid);
                        goto out;
                }
                ret = 0;

                /* If request was for brick, only one iteration is enough */
                if (brick)
                        break;
        }

        if (ret && !valid_brick)
                ret = 0;

out:
        if (tmpbrkinfo)
                glusterd_brickinfo_delete (tmpbrkinfo);

        return ret;
}

int
glusterd_op_log_level (dict_t *dict)
{
        int32_t             ret           = -1;
        glusterd_volinfo_t *volinfo       = NULL;
        char               *volname       = NULL;
        char               *xlator        = NULL;
        char               *loglevel      = NULL;
        xlator_t           *this          = NULL;
        glusterd_conf_t    *priv          = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = dict_get_str (dict, "xlator", &xlator);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get translator name");
                goto out;
        }

        ret = dict_get_str (dict, "loglevel", &loglevel);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get Loglevel to use");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Cannot find volume: %s", volname);
                goto out;
        }

        xlator = gf_strdup (xlator);

        ret = dict_set_dynstr (volinfo->dict, "xlator", xlator);
        if (ret)
                goto out;

        loglevel = gf_strdup (loglevel);

        ret = dict_set_dynstr (volinfo->dict, "loglevel", loglevel);
        if (ret)
                goto out;

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to create volfile for command"
                        " 'log level'");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        ret = 0;

 out:
        gf_log ("glusterd", GF_LOG_DEBUG, "(cli log level) Returning: %d", ret);
        return ret;
}

