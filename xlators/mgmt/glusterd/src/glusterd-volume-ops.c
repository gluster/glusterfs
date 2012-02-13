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
#include "run.h"

#define glusterd_op_start_volume_args_get(dict, volname, flags) \
        glusterd_op_stop_volume_args_get (dict, volname, flags)

int
glusterd_handle_create_volume (rpcsvc_request_t *req)
{
        int32_t                 ret         = -1;
        gf_cli_req              cli_req     = {{0,}};
        dict_t                 *dict        = NULL;
        glusterd_brickinfo_t   *brickinfo   = NULL;
        char                   *brick       = NULL;
        char                   *bricks      = NULL;
        char                   *volname     = NULL;
        int                    brick_count = 0;
        char                   *tmpptr      = NULL;
        int                    i           = 0;
        char                   *brick_list  = NULL;
        void                   *cli_rsp     = NULL;
        char                    err_str[2048] = {0,};
        gf_cli_rsp              rsp         = {0,};
        xlator_t               *this        = NULL;
        char                   *free_ptr    = NULL;
        char                   *trans_type  = NULL;
        uuid_t                  volume_id   = {0,};
        glusterd_brickinfo_t    *tmpbrkinfo = NULL;
        glusterd_volinfo_t      tmpvolinfo = {{0},};
        int32_t                 type       = 0;

        GF_ASSERT (req);

        INIT_LIST_HEAD (&tmpvolinfo.bricks);

        this = THIS;
        GF_ASSERT(this);

        ret = -1;
        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf_cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                snprintf (err_str, sizeof (err_str), "Garbage args received");
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received create volume req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the buffer");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "name");
                goto out;
        }
        gf_cmd_log ("Volume create", "on volname: %s attempted", volname);

        if ((ret = glusterd_check_volume_exists (volname))) {
                snprintf(err_str, 2048, "Volume %s already exists", volname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "brick count");
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);
        /*if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get type");
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "type");
                goto out;
        }*/

        ret = dict_get_str (dict, "transport", &trans_type);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get transport-type");
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "transport-type");
                goto out;
        }
        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get bricks");
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "bricks");
                goto out;
        }

        uuid_generate (volume_id);
        free_ptr = gf_strdup (uuid_utoa (volume_id));
        ret = dict_set_dynstr (dict, "volume-id", free_ptr);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "unable to set volume-id");
                snprintf (err_str, sizeof (err_str), "Unable to set volume "
                          "id");
                goto out;
        }
        free_ptr = NULL;

        if (bricks) {
                brick_list = gf_strdup (bricks);
                free_ptr = brick_list;
        }

        gf_cmd_log ("Volume create", "on volname: %s type:%s count:%d bricks:%s",
                    volname, ((type == 0)? "DEFAULT":
                    ((type == 1)? "STRIPE":"REPLICATE")), brick_count, bricks);


        while ( i < brick_count) {
                i++;
                brick= strtok_r (brick_list, " \n", &tmpptr);
                brick_list = tmpptr;
                ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
                if (ret) {
                        snprintf (err_str, sizeof (err_str), "Unable to get "
                                  "brick info from brick %s", brick);
                        goto out;
                }

                ret = glusterd_new_brick_validate (brick, brickinfo, err_str,
                                                   sizeof (err_str));
                if (ret)
                        goto out;
                ret = glusterd_volume_brickinfo_get (brickinfo->uuid,
                                                     brickinfo->hostname,
                                                     brickinfo->path,
                                                     &tmpvolinfo, &tmpbrkinfo,
                                                     GF_PATH_PARTIAL);
                if (!ret) {
                        ret = -1;
                        snprintf (err_str, sizeof (err_str), "Brick: %s:%s, %s"
                                  " one of the bricks contain the other",
                                  tmpbrkinfo->hostname, tmpbrkinfo->path, brick);
                        goto out;
                }
                list_add_tail (&brickinfo->brick_list, &tmpvolinfo.bricks);
                brickinfo = NULL;
        }

        ret = glusterd_op_begin (req, GD_OP_CREATE_VOLUME, dict);
        gf_cmd_log ("Volume create", "on volname: %s %s", volname,
                    (ret != 0) ? "FAILED": "SUCCESS");

out:
        if (ret) {
                if (dict)
                        dict_unref (dict);
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str), "Operation failed");
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_submit_reply(req, cli_rsp, NULL, 0, NULL,
                                      (xdrproc_t)xdr_gf_cli_rsp);

                ret = 0; //Client response sent, prevent second response
        }

        if (free_ptr)
                GF_FREE(free_ptr);

        glusterd_volume_brickinfos_delete (&tmpvolinfo);
        if (brickinfo)
                glusterd_brickinfo_delete (brickinfo);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_start_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        char                            *dup_volname = NULL;
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_START_VOLUME;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf_cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &dup_volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "dict get failed");
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received start vol req"
                "for volume %s", dup_volname);
        ret = glusterd_op_begin (req, GD_OP_START_VOLUME, dict);

        gf_cmd_log ("volume start","on volname: %s %s", dup_volname,
                    ((ret == 0) ? "SUCCESS": "FAILED"));

out:
        if (ret && dict)
                dict_unref (dict);
        if (cli_req.dict.dict_val)
                free (cli_req.dict.dict_val); //its malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret)
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     NULL, "operation failed");

        return ret;
}


int
glusterd_handle_cli_stop_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        char                            *dup_volname = NULL;
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_STOP_VOLUME;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf_cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &dup_volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received stop vol req"
                "for volume %s", dup_volname);

        ret = glusterd_op_begin (req, GD_OP_STOP_VOLUME, dict);
        gf_cmd_log ("Volume stop","on volname: %s %s", dup_volname,
                    ((ret)?"FAILED":"SUCCESS"));

out:
        if (cli_req.dict.dict_val)
                free (cli_req.dict.dict_val); //its malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                if (dict)
                        dict_unref (dict);
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     NULL, "operation failed");
        }

        return ret;
}

int
glusterd_handle_cli_delete_volume (rpcsvc_request_t *req)
{
        int32_t        ret         = -1;
        gf_cli_req     cli_req     = {{0,},};
        glusterd_op_t  cli_op      = GD_OP_DELETE_VOLUME;
        dict_t        *dict        = NULL;
        char          *volname     = NULL;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf_cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to get volname");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_cmd_log ("Volume delete","on volname: %s attempted", volname);

        gf_log ("glusterd", GF_LOG_INFO, "Received delete vol req"
                "for volume %s", volname);

        ret = glusterd_op_begin (req, GD_OP_DELETE_VOLUME, dict);
        gf_cmd_log ("Volume delete", "on volname: %s %s", volname,
                   ((ret) ? "FAILED" : "SUCCESS"));

out:
        if (cli_req.dict.dict_val)
                free (cli_req.dict.dict_val); //its malloced by xdr
        if (ret && dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     NULL, "operation failed");
        }

        return ret;
}

int
glusterd_handle_cli_heal_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_HEAL_VOLUME;
        char                            *volname = NULL;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf_cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get volname");
                goto out;
        }
        gf_log ("glusterd", GF_LOG_INFO, "Received heal vol req"
                "for volume %s", volname);

        ret = glusterd_op_begin (req, GD_OP_HEAL_VOLUME, dict);

        gf_cmd_log ("volume heal","on volname: %s %s", volname,
                    ((ret == 0) ? "SUCCESS": "FAILED"));

out:
        if (ret && dict)
                dict_unref (dict);
        if (cli_req.dict.dict_val)
                free (cli_req.dict.dict_val); //its malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret)
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     NULL, "operation failed");

        return ret;
}

int
glusterd_handle_cli_statedump_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        char                            *volname = NULL;
        char                            *options = NULL;
        dict_t                          *dict = NULL;
        int32_t                         option_cnt = 0;

        GF_ASSERT (req);

        ret = -1;
        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf_cli_req)) {
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }
        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get volname");
                goto out;
        }

        ret = dict_get_str (dict, "options", &options);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get options");
                goto out;
        }

        ret = dict_get_int32 (dict, "option_cnt", &option_cnt);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get option cnt");
                goto out;
        }


        gf_log ("glusterd", GF_LOG_INFO, "Recieved statedump request for "
                "volume %s with options %s", volname, options);

        ret = glusterd_op_begin (req, GD_OP_STATEDUMP_VOLUME, dict);

        gf_cmd_log ("statedump", "on volume %s %s", volname,
                    ((0 == ret) ? "SUCCEEDED" : "FAILED"));

out:
        if (ret && dict)
                dict_unref (dict);
        if (cli_req.dict.dict_val)
                free (cli_req.dict.dict_val);
        glusterd_friend_sm ();
        glusterd_op_sm();

        return ret;
}

/* op-sm */
int
glusterd_op_stage_create_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        gf_boolean_t                            exists = _gf_false;
        char                                    *bricks = NULL;
        char                                    *brick_list = NULL;
        char                                    *free_ptr = NULL;
        glusterd_brickinfo_t                    *brick_info = NULL;
        int32_t                                 brick_count = 0;
        int32_t                                 i = 0;
        char                                    *brick = NULL;
        char                                    *tmpptr = NULL;
        char                                    cmd_str[1024];
        xlator_t                                *this = NULL;
        glusterd_conf_t                         *priv = NULL;
        char                                    msg[2048] = {0};
        uuid_t                                  volume_uuid;
        char                                    *volume_uuid_str;

        this = THIS;
        if (!this) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "this is NULL");
                goto out;
        }

        priv = this->private;
        if (!priv) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "priv is NULL");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);

        if (exists) {
                snprintf (msg, sizeof (msg), "Volume %s already exists",
                          volname);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        } else {
                ret = 0;
        }
        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }
        ret = dict_get_str (dict, "volume-id", &volume_uuid_str);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume id");
                goto out;
        }
        ret = uuid_parse (volume_uuid_str, volume_uuid);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to parse volume id");
                goto out;
        }

        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get bricks");
                goto out;
        }

        if (bricks) {
                brick_list = gf_strdup (bricks);
                if (!brick_list) {
                        ret = -1;
                        gf_log ("", GF_LOG_ERROR, "Out of memory");
                        goto out;
                } else {
                        free_ptr = brick_list;
                }
        }

        while ( i < brick_count) {
                i++;
                brick= strtok_r (brick_list, " \n", &tmpptr);
                brick_list = tmpptr;

                if (!glusterd_store_is_valid_brickpath (volname, brick) ||
                        !glusterd_is_valid_volfpath (volname, brick)) {
                        snprintf (msg, sizeof (msg), "brick path %s is too "
                                  "long.", brick);
                        gf_log ("", GF_LOG_ERROR, "%s", msg);
                        *op_errstr = gf_strdup (msg);

                        ret = -1;
                        goto out;
                }

                ret = glusterd_brickinfo_from_brick (brick, &brick_info);
                if (ret)
                        goto out;
                snprintf (cmd_str, 1024, "%s", brick_info->path);
                ret = glusterd_resolve_brick (brick_info);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "cannot resolve "
                                "brick: %s:%s", brick_info->hostname,
                                brick_info->path);
                        goto out;
                }

                if (!uuid_compare (brick_info->uuid, priv->uuid)) {
                        ret = glusterd_brick_create_path (brick_info->hostname,
                                                          brick_info->path,
                                                          volume_uuid,
                                                          0777, op_errstr);
                        if (ret)
                                goto out;
                        brick_list = tmpptr;
                }
                glusterd_brickinfo_delete (brick_info);
                brick_info = NULL;
        }
out:
        if (free_ptr)
                GF_FREE (free_ptr);
        if (brick_info)
                glusterd_brickinfo_delete (brick_info);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_op_stop_volume_args_get (dict_t *dict, char** volname, int *flags)
{
        int ret = -1;

        if (!dict || !volname || !flags)
                goto out;

        ret = dict_get_str (dict, "volname", volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = dict_get_int32 (dict, "flags", flags);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get flags");
                goto out;
        }
out:
        return ret;
}

int
glusterd_op_statedump_volume_args_get (dict_t *dict, char **volname,
                                       char **options, int *option_cnt)
{
        int ret = -1;

        if (!dict || !volname || !options || !option_cnt)
                goto out;

        ret = dict_get_str (dict, "volname", volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volname");
                goto out;
        }

        ret = dict_get_str (dict, "options", options);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get options");
                goto out;
        }

        ret = dict_get_int32 (dict, "option_cnt", option_cnt);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get option count");
                goto out;
        }

out:
        return ret;
}

int
glusterd_op_stage_start_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        int                                     flags = 0;
        gf_boolean_t                            exists = _gf_false;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        char                                    msg[2048];
        glusterd_conf_t                         *priv = NULL;

        priv = THIS->private;
        if (!priv) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "priv is NULL");
                ret = -1;
                goto out;
        }

        ret = glusterd_op_start_volume_args_get (dict, &volname, &flags);
        if (ret)
                goto out;

        exists = glusterd_check_volume_exists (volname);

        if (!exists) {
                snprintf (msg, sizeof (msg), "Volume %s does not exist", volname);
                gf_log ("", GF_LOG_ERROR, "%s",
                        msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
        } else {
                ret = 0;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR,
                                "Unable to resolve brick %s:%s",
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }

                if (!uuid_compare (brickinfo->uuid, priv->uuid)) {
                        ret = glusterd_brick_create_path (brickinfo->hostname,
                                                          brickinfo->path,
                                                          volinfo->volume_id,
                                                          0777, op_errstr);
                        if (ret)
                                goto out;
                }

                if (!(flags & GF_CLI_FLAG_OP_FORCE)) {
                        if (glusterd_is_volume_started (volinfo)) {
                                snprintf (msg, sizeof (msg), "Volume %s already"
                                          " started", volname);
                                gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);
                                *op_errstr = gf_strdup (msg);
                                ret = -1;
                                goto out;
                        }
                }
        }

        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_op_stage_stop_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        int                                     flags = 0;
        gf_boolean_t                            exists = _gf_false;
        gf_boolean_t                            is_run = _gf_false;
        glusterd_volinfo_t                      *volinfo = NULL;
        char                                    msg[2048] = {0};


        ret = glusterd_op_stop_volume_args_get (dict, &volname, &flags);
        if (ret)
                goto out;

        exists = glusterd_check_volume_exists (volname);

        if (!exists) {
                snprintf (msg, sizeof (msg), "Volume %s does not exist", volname);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        } else {
                ret = 0;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret)
                goto out;

        /* If 'force' flag is given, no check is required */
        if (flags & GF_CLI_FLAG_OP_FORCE)
                goto out;

        if (_gf_false == glusterd_is_volume_started (volinfo)) {
                snprintf (msg, sizeof(msg), "Volume %s "
                          "is not in the started state", volname);
                gf_log ("", GF_LOG_ERROR, "Volume %s "
                        "has not been started", volname);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }
        ret = glusterd_check_gsync_running (volinfo, &is_run);
        if (ret && (is_run == _gf_false))
                gf_log ("", GF_LOG_WARNING, "Unable to get the status"
                        " of active "GEOREP" session");
        if (is_run) {
                gf_log ("", GF_LOG_WARNING, GEOREP" sessions active"
                        "for the volume %s ", volname);
                snprintf (msg, sizeof(msg), GEOREP" sessions are active "
                          "for the volume '%s'.\nUse 'volume "GEOREP" "
                          "status' command for more info. Use 'force'"
                          "option to ignore and stop stop the volume",
                          volname);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        if (glusterd_is_rb_ongoing (volinfo)) {
                snprintf (msg, sizeof (msg), "Replace brick is in progress on "
                          "volume %s. Please retry after replace-brick "
                          "operation is committed or aborted", volname);
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        if (glusterd_is_defrag_on (volinfo)) {
                snprintf (msg, sizeof(msg), "rebalance session is "
                          "in progress for the volume '%s'", volname);
                gf_log (THIS->name, GF_LOG_WARNING, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }
        if (volinfo->rb_status != GF_RB_STATUS_NONE) {
                snprintf (msg, sizeof(msg), "replace-brick session is "
                          "in progress for the volume '%s'", volname);
                gf_log (THIS->name, GF_LOG_WARNING, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

out:

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_op_stage_delete_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        gf_boolean_t                            exists = _gf_false;
        glusterd_volinfo_t                      *volinfo = NULL;
        char                                    msg[2048] = {0};

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);

        if (!exists) {
                snprintf (msg, sizeof (msg), "Volume %s does not exist",
                          volname);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        } else {
                ret = 0;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        if (glusterd_is_volume_started (volinfo)) {
                snprintf (msg, sizeof (msg), "Volume %s has been started."
                          "Volume needs to be stopped before deletion.",
                          volname);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_op_stage_heal_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        gf_boolean_t                            enabled = _gf_false;
        glusterd_volinfo_t                      *volinfo = NULL;
        char                                    msg[2048];
        glusterd_conf_t                         *priv = NULL;
        dict_t                                  *opt_dict = NULL;

        priv = THIS->private;
        if (!priv) {
                ret = -1;
                gf_log (THIS->name, GF_LOG_ERROR,
                        "priv is NULL");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                ret = -1;
                snprintf (msg, sizeof (msg), "Volume %s does not exist", volname);
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        if (!glusterd_is_volume_replicate (volinfo)) {
                ret = -1;
                snprintf (msg, sizeof (msg), "Volume %s is not a replicate "
                          "type volume", volname);
                *op_errstr = gf_strdup (msg);
                gf_log (THIS->name, GF_LOG_WARNING, "%s", msg);
                goto out;
        }

        if (!glusterd_is_volume_started (volinfo)) {
                ret = -1;
                snprintf (msg, sizeof (msg), "Volume %s is not started.",
                          volname);
                gf_log (THIS->name, GF_LOG_WARNING, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        opt_dict = volinfo->dict;
        if (!opt_dict) {
                ret = 0;
                goto out;
        }

        enabled = dict_get_str_boolean (opt_dict, "cluster.self-heal-daemon",
                                        1);
        if (!enabled) {
                ret = -1;
                snprintf (msg, sizeof (msg), "Self-heal-daemon is "
                          "disabled. Heal will not be triggered on volume %s",
                          volname);
                gf_log (THIS->name, GF_LOG_WARNING, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        if (!glusterd_shd_is_running ()) {
                ret = -1;
                snprintf (msg, sizeof (msg), "Self-heal daemon is not "
                          "running. Check self-heal daemon log file.");
                *op_errstr = gf_strdup (msg);
                gf_log (THIS->name, GF_LOG_WARNING, "%s", msg);
                goto out;
        }

        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_op_stage_statedump_volume (dict_t *dict, char **op_errstr)
{
        int                     ret = -1;
        char                    *volname = NULL;
        char                    *options = NULL;
        int                     option_cnt = 0;
        gf_boolean_t            is_running = _gf_false;
        glusterd_volinfo_t      *volinfo = NULL;
        char                    msg[2408] = {0,};

        ret = glusterd_op_statedump_volume_args_get (dict, &volname, &options,
                                                     &option_cnt);
        if (ret)
                goto out;

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof(msg), "Volume %s does not exist",
                          volname);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        is_running = glusterd_is_volume_started (volinfo);
        if (!is_running) {
                snprintf (msg, sizeof(msg), "Volume %s is not in a started"
                          " state", volname);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_op_create_volume (dict_t *dict, char **op_errstr)
{
        int                   ret        = 0;
        char                 *volname    = NULL;
        glusterd_conf_t      *priv       = NULL;
        glusterd_volinfo_t   *volinfo    = NULL;
        gf_boolean_t          vol_added = _gf_false;
        glusterd_brickinfo_t *brickinfo  = NULL;
        xlator_t             *this       = NULL;
        char                 *brick      = NULL;
        int32_t               count      = 0;
        int32_t               i          = 1;
        char                 *bricks     = NULL;
        char                 *brick_list = NULL;
        char                 *free_ptr   = NULL;
        char                 *saveptr    = NULL;
        char                 *trans_type = NULL;
        char                 *str        = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_volinfo_new (&volinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        strncpy (volinfo->volname, volname, GLUSTERD_MAX_VOLUME_NAME);
        GF_ASSERT (volinfo->volname);

        ret = dict_get_int32 (dict, "type", &volinfo->type);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get type");
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &volinfo->brick_count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }

        ret = dict_get_int32 (dict, "port", &volinfo->port);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get port");
                goto out;
        }

        count = volinfo->brick_count;

        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get bricks");
                goto out;
        }

        /* replica-count 1 means, no replication, file is in one brick only */
        volinfo->replica_count = 1;
        /* stripe-count 1 means, no striping, file is present as a whole */
        volinfo->stripe_count = 1;

        if (GF_CLUSTER_TYPE_REPLICATE == volinfo->type) {
                ret = dict_get_int32 (dict, "replica-count",
                                      &volinfo->replica_count);
                if (ret)
                        goto out;
        } else if (GF_CLUSTER_TYPE_STRIPE == volinfo->type) {
                ret = dict_get_int32 (dict, "stripe-count",
                                      &volinfo->stripe_count);
                if (ret)
                        goto out;
        } else if (GF_CLUSTER_TYPE_STRIPE_REPLICATE == volinfo->type) {
                ret = dict_get_int32 (dict, "stripe-count",
                                      &volinfo->stripe_count);
                if (ret)
                        goto out;
                ret = dict_get_int32 (dict, "replica-count",
                                      &volinfo->replica_count);
                if (ret)
                        goto out;
        }

        /* dist-leaf-count is the count of brick nodes for a given
           subvolume of distribute */
        volinfo->dist_leaf_count = (volinfo->stripe_count *
                                    volinfo->replica_count);

        /* Keep sub-count same as earlier, for the sake of backward
           compatibility */
        if (volinfo->dist_leaf_count > 1)
                volinfo->sub_count = volinfo->dist_leaf_count;

        ret = dict_get_str (dict, "transport", &trans_type);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get transport");
                goto out;
        }

        ret = dict_get_str (dict, "volume-id", &str);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume-id");
                goto out;
        }
        ret = uuid_parse (str, volinfo->volume_id);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "unable to parse uuid %s", str);
                goto out;
        }

        if (strcasecmp (trans_type, "rdma") == 0) {
                volinfo->transport_type = GF_TRANSPORT_RDMA;
                volinfo->nfs_transport_type = GF_TRANSPORT_RDMA;
        } else if (strcasecmp (trans_type, "tcp") == 0) {
                volinfo->transport_type = GF_TRANSPORT_TCP;
                volinfo->nfs_transport_type = GF_TRANSPORT_TCP;
        } else {
                volinfo->transport_type = GF_TRANSPORT_BOTH_TCP_RDMA;
                volinfo->nfs_transport_type = GF_DEFAULT_NFS_TRANSPORT;
        }

        if (bricks) {
                brick_list = gf_strdup (bricks);
                free_ptr = brick_list;
        }

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);

        while ( i <= count) {
                ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
                if (ret)
                        goto out;

                ret = glusterd_resolve_brick (brickinfo);
                if (ret)
                        goto out;
                list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                glusterd_store_delete_volume (volinfo);
                *op_errstr = gf_strdup ("Failed to store the Volume information");
                goto out;
        }

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                *op_errstr = gf_strdup ("Failed to create volume files");
                goto out;
        }

        ret = glusterd_volume_compute_cksum (volinfo);
        if (ret) {
                *op_errstr = gf_strdup ("Failed to compute checksum of volume");
                goto out;
        }

        volinfo->defrag_status = 0;
        list_add_tail (&volinfo->vol_list, &priv->volumes);
        vol_added = _gf_true;
out:
        if (free_ptr)
                GF_FREE(free_ptr);
        if (!vol_added && volinfo)
                glusterd_volinfo_delete (volinfo);
        return ret;
}

int
glusterd_op_start_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        int                                     flags = 0;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;

        ret = glusterd_op_start_volume_args_get (dict, &volname, &flags);
        if (ret)
                goto out;

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;
        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_brick_start (volinfo, brickinfo);
                if (ret)
                        goto out;
        }

        glusterd_set_volume_status (volinfo, GLUSTERD_STATUS_STARTED);

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        ret = glusterd_nodesvcs_handle_graph_change (volinfo);

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d ", ret);
        return ret;
}


int
glusterd_op_stop_volume (dict_t *dict)
{
        int                                     ret = 0;
        int                                     flags = 0;
        char                                    *volname = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;

        ret = glusterd_op_stop_volume_args_get (dict, &volname, &flags);
        if (ret)
                goto out;

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_brick_stop (volinfo, brickinfo);
                if (ret)
                        goto out;
        }

        glusterd_set_volume_status (volinfo, GLUSTERD_STATUS_STOPPED);

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        ret = glusterd_nodesvcs_handle_graph_change (volinfo);
out:
        return ret;
}

int
glusterd_op_delete_volume (dict_t *dict)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        xlator_t                                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        ret = glusterd_delete_volume (volinfo);
out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_op_heal_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        /* Necessary subtasks of heal are completed in brick op */

        return ret;
}

int
glusterd_op_statedump_volume (dict_t *dict, char **op_errstr)
{
        int                     ret = 0;
        char                    *volname = NULL;
        char                    *options = NULL;
        int                     option_cnt = 0;
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;

        ret = glusterd_op_statedump_volume_args_get (dict, &volname, &options,
                                                     &option_cnt);
        if (ret)
                goto out;

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret)
                goto out;
        gf_log ("", GF_LOG_DEBUG, "Performing statedump on volume %s", volname);
        if (strstr (options, "nfs") != NULL) {
                ret = glusterd_nfs_statedump (options, option_cnt, op_errstr);
                if (ret)
                        goto out;
        } else {
                list_for_each_entry (brickinfo, &volinfo->bricks,
                                                        brick_list) {
                        ret = glusterd_brick_statedump (volinfo, brickinfo,
                                                        options, option_cnt,
                                                        op_errstr);
                        if (ret)
                                goto out;
                }
        }

out:
        return ret;
}

