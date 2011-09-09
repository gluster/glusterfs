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

int
glusterd_handle_add_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_add_brick_req          cli_req = {0,};
        dict_t                          *dict = NULL;
        glusterd_brickinfo_t            *brickinfo = NULL;
        char                            *brick = NULL;
        char                            *bricks = NULL;
        char                            *volname = NULL;
        int                             brick_count = 0;
        char                            *tmpptr = NULL;
        int                             i = 0;
        char                            *brick_list = NULL;
        void                            *cli_rsp = NULL;
        char                            err_str[2048] = {0,};
        gf1_cli_add_brick_rsp           rsp = {0,};
        glusterd_volinfo_t              *volinfo = NULL;
        xlator_t                        *this = NULL;
        char                            *free_ptr = NULL;
        glusterd_brickinfo_t            *tmpbrkinfo = NULL;
        glusterd_volinfo_t              tmpvolinfo = {{0},};
        int                             total_bricks = 0;

        this = THIS;
        GF_ASSERT(this);

        GF_ASSERT (req);

        INIT_LIST_HEAD (&tmpvolinfo.bricks);

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf1_cli_add_brick_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                snprintf (err_str, sizeof (err_str), "Garbage args received");
                goto out;
        }

        gf_cmd_log ("Volume add-brick", "on volname: %s attempted",
                    cli_req.volname);
        gf_log ("glusterd", GF_LOG_INFO, "Received add brick req");

        if (cli_req.bricks.bricks_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.bricks.bricks_val,
                                        cli_req.bricks.bricks_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the buffer");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.bricks.bricks_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "name");
                goto out;
        }

        if (!(ret = glusterd_check_volume_exists (volname))) {
                ret = -1;
                snprintf(err_str, 2048, "Volume %s does not exist", volname);
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

        if (!(ret = glusterd_volinfo_find (volname, &volinfo))) {
                if (volinfo->type == GF_CLUSTER_TYPE_NONE)
                        goto brick_val;
                if (!brick_count || !volinfo->sub_count)
                        goto brick_val;

                total_bricks = volinfo->brick_count + brick_count;
		/* If the brick count is less than sub_count then, allow add-brick only for
		   plain replicate volume since in plain stripe brick_count becoming less than
		   the sub_count is not allowed */
                if (volinfo->brick_count < volinfo->sub_count &&
                    (volinfo->type == GF_CLUSTER_TYPE_REPLICATE)) {
                        if (total_bricks <= volinfo->sub_count)
                                goto brick_val;
                }

                if ((brick_count % volinfo->sub_count) != 0) {
                        snprintf(err_str, 2048, "Incorrect number of bricks"
                                " supplied %d for type %s with count %d",
                                brick_count, (volinfo->type == 1)? "STRIPE":
                                "REPLICATE", volinfo->sub_count);
                        gf_log("glusterd", GF_LOG_ERROR, "%s", err_str);
                        ret = -1;
                        goto out;
                }
        } else {
                snprintf (err_str, sizeof (err_str), "Unable to get volinfo "
                          "for volume name %s", volname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

brick_val:
        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "bricks");
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        if (bricks)
                brick_list = gf_strdup (bricks);
        if (!brick_list) {
                ret = -1;
                snprintf (err_str, sizeof (err_str), "Out of memory");
                goto out;
        } else {
                free_ptr = brick_list;
        }

        gf_cmd_log ("Volume add-brick", "volname: %s type %s count:%d bricks:%s"
                    ,volname, ((volinfo->type == 0)? "DEFAULT" : ((volinfo->type
                    == 1)? "STRIPE": "REPLICATE")), brick_count, brick_list);


        while ( i < brick_count) {
                i++;
                brick= strtok_r (brick_list, " \n", &tmpptr);
                brick_list = tmpptr;
                brickinfo = NULL;
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
                                                     &tmpvolinfo, &tmpbrkinfo);
                if (!ret) {
                        ret = -1;
                        snprintf (err_str, sizeof (err_str), "Brick: %s:%s, %s"
                                  " one of the arguments contain the other",
                                  tmpbrkinfo->hostname, tmpbrkinfo->path, brick);
                        goto out;
                }
                list_add_tail (&brickinfo->brick_list, &tmpvolinfo.bricks);
                brickinfo = NULL;
        }

        ret = glusterd_op_begin (req, GD_OP_ADD_BRICK, dict);
        gf_cmd_log ("Volume add-brick","on volname: %s %s", volname,
                   (ret != 0)? "FAILED" : "SUCCESS");

out:
        if (ret) {
                if (dict)
                        dict_unref (dict);
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                rsp.volname = "";
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str), "Operation failed");
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_submit_reply(req, cli_rsp, NULL, 0, NULL,
                                      (xdrproc_t)xdr_gf1_cli_add_brick_rsp);
                ret = 0; //sent error to cli, prevent second reply
        }

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (free_ptr)
                GF_FREE (free_ptr);
        glusterd_volume_brickinfos_delete (&tmpvolinfo);
        if (brickinfo)
                glusterd_brickinfo_delete (brickinfo);
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr

        return ret;
}

int
glusterd_handle_remove_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_remove_brick_req        cli_req = {0,};
        dict_t                          *dict = NULL;
        int32_t                         count = 0;
        char                            *brick = NULL;
        char                            key[256] = {0,};
        char                            *brick_list = NULL;
        int                             i = 1;
        glusterd_volinfo_t              *volinfo = NULL;
        glusterd_brickinfo_t            *brickinfo = NULL;
        int32_t                         pos = 0;
        int32_t                         sub_volume = 0;
        int32_t                         sub_volume_start = 0;
        int32_t                         sub_volume_end = 0;
        glusterd_brickinfo_t            *tmp = NULL;
        char                            err_str[2048] = {0};
        gf1_cli_remove_brick_rsp        rsp = {0,};
        void                            *cli_rsp = NULL;
        char                            vol_type[256] = {0,};

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf1_cli_remove_brick_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_cmd_log ("Volume remove-brick","on volname: %s attempted",cli_req.volname);
        gf_log ("glusterd", GF_LOG_INFO, "Received rem brick req");

        if (cli_req.bricks.bricks_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.bricks.bricks_val,
                                        cli_req.bricks.bricks_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.bricks.bricks_val;
                }
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }

        ret = glusterd_volinfo_find (cli_req.volname, &volinfo);
        if (ret) {
                 snprintf (err_str, 2048, "Volume %s does not exist",
                          cli_req.volname);
                 gf_log ("", GF_LOG_ERROR, "%s", err_str);
                 goto out;
        }

        if (volinfo->type == GF_CLUSTER_TYPE_REPLICATE)
                strcpy (vol_type, "replica");
        else if (volinfo->type == GF_CLUSTER_TYPE_STRIPE)
                strcpy (vol_type, "stripe");
        else
                strcpy (vol_type, "distribute");

	/* Do not allow remove-brick if the volume is plain stripe */
	if ((volinfo->type == GF_CLUSTER_TYPE_STRIPE) &&
            (volinfo->brick_count == volinfo->sub_count)) {
                snprintf (err_str, 2048, "Removing brick from a plain stripe is not allowed");
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                ret = -1;
                goto out;
	}

	/* Do not allow remove-brick if the bricks given is less than the replica count
	   or stripe count */
        if ((volinfo->type != GF_CLUSTER_TYPE_NONE) &&
            !(volinfo->brick_count <= volinfo->sub_count)) {
                if (volinfo->sub_count && (count % volinfo->sub_count != 0)) {
                        snprintf (err_str, 2048, "Remove brick incorrect"
                                  " brick count of %d for %s %d",
                                  count, vol_type, volinfo->sub_count);
                        gf_log ("", GF_LOG_ERROR, "%s", err_str);
                        ret = -1;
                        goto out;
                }
        }

        brick_list = GF_MALLOC (120000 * sizeof(*brick_list),gf_common_mt_char);

        if (!brick_list) {
                ret = -1;
                goto out;
        }

        strcpy (brick_list, " ");
        while ( i <= count) {
                snprintf (key, 256, "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to get %s", key);
                        goto out;
                }
                gf_log ("", GF_LOG_DEBUG, "Remove brick count %d brick: %s",
                        i, brick);

                ret = glusterd_volume_brickinfo_get_by_brick(brick, volinfo, &brickinfo);
                if (ret) {
                        snprintf(err_str, 2048,"Incorrect brick %s for volume"
                                " %s", brick, cli_req.volname);
                        gf_log ("", GF_LOG_ERROR, "%s", err_str);
                        goto out;
                }
                strcat(brick_list, brick);
                strcat(brick_list, " ");

                i++;
                if ((volinfo->type == GF_CLUSTER_TYPE_NONE) ||
                    (volinfo->brick_count <= volinfo->sub_count))
                        continue;

                pos = 0;
                list_for_each_entry (tmp, &volinfo->bricks, brick_list) {

                        if ((!strcmp (tmp->hostname,brickinfo->hostname)) &&
                            !strcmp (tmp->path, brickinfo->path)) {
                                gf_log ("", GF_LOG_INFO, "Found brick");
                                if (!sub_volume && volinfo->sub_count) {
                                        sub_volume = (pos / volinfo->
                                                      sub_count) + 1;
                                        sub_volume_start = volinfo->sub_count *
                                                           (sub_volume - 1);
                                        sub_volume_end = (volinfo->sub_count *
                                                          sub_volume) -1 ;
                                } else {
                                        if (pos < sub_volume_start ||
                                            pos >sub_volume_end) {
                                                ret = -1;
                                                snprintf(err_str, 2048,"Bricks"
                                                         " not from same subvol"
                                                         " for %s", vol_type);
                                                gf_log ("",GF_LOG_ERROR,
                                                        "%s", err_str);
                                                goto out;
                                        }
                                }
                                break;
                        }
                        pos++;
                }
        }
        gf_cmd_log ("Volume remove-brick","volname: %s count:%d bricks:%s",
                    cli_req.volname, count, brick_list);

        ret = glusterd_op_begin (req, GD_OP_REMOVE_BRICK, dict);
        gf_cmd_log ("Volume remove-brick","on volname: %s %s",cli_req.volname,
                    (ret) ? "FAILED" : "SUCCESS");

out:
        if (ret) {
                if (dict)
                        dict_unref (dict);
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                rsp.volname = "";
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str), "Operation failed");
                gf_log ("", GF_LOG_ERROR, "%s", err_str);
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_submit_reply(req, cli_rsp, NULL, 0, NULL,
                                      (xdrproc_t)xdr_gf1_cli_remove_brick_rsp);

                ret = 0; //sent error to cli, prevent second reply

        }
        if (brick_list)
                GF_FREE (brick_list);
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}


/* op-sm */

int
glusterd_op_perform_add_bricks (glusterd_volinfo_t  *volinfo, int32_t count,
                                char  *bricks)
{
        glusterd_brickinfo_t                    *brickinfo = NULL;
        char                                    *brick = NULL;
        int32_t                                 i = 1;
        char                                    *brick_list = NULL;
        char                                    *free_ptr1  = NULL;
        char                                    *free_ptr2  = NULL;
        char                                    *saveptr = NULL;
        int32_t                                 ret = -1;

        GF_ASSERT (volinfo);

        if (bricks) {
                brick_list = gf_strdup (bricks);
                free_ptr1 = brick_list;
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
                volinfo->brick_count++;

        }

        brick_list = gf_strdup (bricks);
        free_ptr2 = brick_list;
        i = 1;

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret)
                goto out;

        while (i <= count) {

                ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                              &brickinfo);
                if (ret)
                        goto out;

                if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                        ret = glusterd_brick_start (volinfo, brickinfo);
                        if (ret)
                                goto out;
                }
                i++;
                brick = strtok_r (NULL, " \n", &saveptr);
        }

out:
        if (free_ptr1)
                GF_FREE (free_ptr1);
        if (free_ptr2)
                GF_FREE (free_ptr2);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int
glusterd_op_perform_remove_brick (glusterd_volinfo_t  *volinfo, char *brick,
                                  int force, int *need_migrate)
{
        glusterd_brickinfo_t    *brickinfo = NULL;
        char                    *dup_brick = NULL;
        int32_t                  ret = -1;
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (brick);

        priv = THIS->private;
        GF_ASSERT (priv);

        dup_brick = gf_strdup (brick);
        if (!dup_brick)
                goto out;

        ret = glusterd_volume_brickinfo_get_by_brick (dup_brick, volinfo,  &brickinfo);
        if (ret)
                goto out;

        ret = glusterd_resolve_brick (brickinfo);
        if (ret)
                goto out;

        if (!uuid_compare (brickinfo->uuid, priv->uuid)) {
                /* Only if the brick is in this glusterd, do the rebalance */
                if (need_migrate)
                        *need_migrate = 1;
        }

        if (force) {
                if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                        ret = glusterd_brick_stop (volinfo, brickinfo);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_ERROR, "Unable to stop "
                                        "glusterfs, ret: %d", ret);
                                goto out;
                        }
                }
                glusterd_delete_brick (volinfo, brickinfo);
                goto out;
        }

        brickinfo->decommissioned = 1;
out:
        if (dup_brick)
                GF_FREE (dup_brick);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_op_stage_add_brick (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        int                                     count = 0;
        int                                     i = 0;
        char                                    *bricks    = NULL;
        char                                    *brick_list = NULL;
        char                                    *saveptr = NULL;
        char                                    *free_ptr = NULL;
        char                                    *brick = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        char                                    cmd_str[1024];
        glusterd_conf_t                         *priv = NULL;
        char                                    msg[2048] = {0,};
        gf_boolean_t                            brick_alloc = _gf_false;
        char                                    *all_bricks = NULL;
        char                                    *str_ret = NULL;

        priv = THIS->private;
        if (!priv)
                goto out;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to find volume: %s", volname);
                goto out;
        }

        if (glusterd_is_defrag_on(volinfo)) {
                snprintf (msg, sizeof(msg), "Volume name %s rebalance is in "
                          "progress. Please retry after completion", volname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }
        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }

        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get bricks");
                goto out;
        }

        if (bricks) {
                brick_list = gf_strdup (bricks);
                all_bricks = gf_strdup (bricks);
                free_ptr = brick_list;
        }

        /* Check whether any of the bricks given is the destination brick of the
           replace brick running */

        str_ret = glusterd_check_brick_rb_part (all_bricks, count, volinfo);
        if (str_ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "%s", str_ret);
                *op_errstr = gf_strdup (str_ret);
                ret = -1;
                goto out;
        }

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);


        while ( i < count) {
                if (!glusterd_store_is_valid_brickpath (volname, brick) ||
                        !glusterd_is_valid_volfpath (volname, brick)) {
                        snprintf (msg, sizeof (msg), "brick path %s is too "
                                  "long.", brick);
                        gf_log ("", GF_LOG_ERROR, "%s", msg);
                        *op_errstr = gf_strdup (msg);

                        ret = -1;
                        goto out;

                }

                ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                              &brickinfo);
                if (!ret) {
                        gf_log ("", GF_LOG_ERROR, "Adding duplicate brick: %s",
                                brick);
                        ret = -1;
                        goto out;
                } else {
                        ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, "Add-brick: Unable"
                                        " to get brickinfo");
                                goto out;
                        }
                        brick_alloc = _gf_true;
                }

                snprintf (cmd_str, 1024, "%s", brickinfo->path);
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "resolve brick failed");
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

                glusterd_brickinfo_delete (brickinfo);
                brick_alloc = _gf_false;
                brickinfo = NULL;
                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
        }

out:
        if (free_ptr)
                GF_FREE (free_ptr);
        if (brick_alloc && brickinfo)
                glusterd_brickinfo_delete (brickinfo);
        if (str_ret)
                GF_FREE (str_ret);
        if (all_bricks)
                GF_FREE (all_bricks);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_op_stage_remove_brick (dict_t *dict, char **op_errstr)
{
        int                 ret         = -1;
        char               *volname     = NULL;
        glusterd_volinfo_t *volinfo     = NULL;
        char               *errstr      = NULL;
        int32_t             brick_count = 0;
        char                msg[2048]   = {0,};
        int32_t             flag        = 0;
        gf1_op_commands     cmd         = GF_OP_CMD_NONE;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Volume %s does not exist", volname);
                goto out;
        }

        ret = dict_get_int32 (dict, "command", &flag);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get brick count");
                goto out;
        }
        cmd = flag;

        ret = -1;
        switch (cmd) {
        case GF_OP_CMD_NONE:
                errstr = gf_strdup ("no remove-brick command issued");
                goto out;

        case GF_OP_CMD_STATUS:
                ret = 0;
                goto out;

        case GF_OP_CMD_START:
        {
                if (GLUSTERD_STATUS_STARTED != volinfo->status) {
                        snprintf (msg, sizeof (msg), "Volume %s needs to be started "
                                  "before remove-brick (you can use 'force' or "
                                  "'commit' to override this behavior)",
                                  volinfo->volname);
                        errstr = gf_strdup (msg);
                        gf_log (THIS->name, GF_LOG_ERROR, "%s", errstr);
                        goto out;
                }
                if (glusterd_is_defrag_on(volinfo)) {
                        errstr = gf_strdup("Rebalance is in progress. Please retry"
                                           " after completion");
                        gf_log ("glusterd", GF_LOG_ERROR, "%s", errstr);
                        goto out;
                }
                break;
        }

        case GF_OP_CMD_PAUSE:
        case GF_OP_CMD_ABORT:
        {
                if (!volinfo->decommission_in_progress) {
                        errstr = gf_strdup("remove-brick is not in progress");
                        gf_log ("glusterd", GF_LOG_ERROR, "%s", errstr);
                        goto out;
                }
                break;
        }

        case GF_OP_CMD_COMMIT:
                if (volinfo->decommission_in_progress) {
                        errstr = gf_strdup ("use 'force' option as migration "
                                            "is in progress");
                        goto out;
                }
                break;

        case GF_OP_CMD_COMMIT_FORCE:
                break;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get brick count");
                goto out;
        }

        ret = 0;
        if (volinfo->brick_count == brick_count) {
                errstr = gf_strdup ("Deleting all the bricks of the "
                                    "volume is not allowed");
                ret = -1;
                goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        if (ret && errstr) {
                if (op_errstr)
                        *op_errstr = errstr;
        }

        return ret;
}

int
glusterd_remove_brick_migrate_cbk (glusterd_volinfo_t *volinfo,
                                   gf_defrag_status_t status)
{
        int                   ret = 0;
        glusterd_brickinfo_t *brickinfo = NULL;
        glusterd_brickinfo_t *tmp = NULL;

        switch (status) {
        case GF_DEFRAG_STATUS_PAUSED:
        case GF_DEFRAG_STATUS_FAILED:
                /* No changes required in the volume file.
                   everything should remain as is */
                break;
        case GF_DEFRAG_STATUS_STOPPED:
                /* Fall back to the old volume file */
                list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks, brick_list) {
                        if (!brickinfo->decommissioned)
                                continue;
                        brickinfo->decommissioned = 0;
                }
                break;

        case GF_DEFRAG_STATUS_COMPLETE:
                /* Done with the task, you can remove the brick from the
                   volume file */
                list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks, brick_list) {
                        if (!brickinfo->decommissioned)
                                continue;
                        gf_log (THIS->name, GF_LOG_INFO, "removing the brick %s",
                                brickinfo->path);
                        brickinfo->decommissioned = 0;
                        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                                ret = glusterd_brick_stop (volinfo, brickinfo);
                                if (ret) {
                                        gf_log (THIS->name, GF_LOG_ERROR,
                                                "Unable to stop glusterfs (%d)", ret);
                                }
                        }
                        glusterd_delete_brick (volinfo, brickinfo);
                }
                break;

        default:
                GF_ASSERT (!"cbk function called with wrong status");
                break;
        }

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Unable to write volume files (%d)", ret);

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Unable to store volume info (%d)", ret);


        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                ret = glusterd_check_generate_start_nfs ();
                if (ret)
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "Unable to start nfs process (%d)", ret);
        }

        volinfo->decommission_in_progress = 0;
        return 0;
}


int
glusterd_op_add_brick (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        xlator_t                                *this = NULL;
        char                                    *bricks = NULL;
        int32_t                                 count = 0;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }


        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get bricks");
                goto out;
        }

        ret = glusterd_op_perform_add_bricks (volinfo, count, bricks);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to add bricks");
                goto out;
        }

        volinfo->defrag_status = 0;

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_check_generate_start_nfs ();

out:
        return ret;
}

int
glusterd_op_remove_brick (dict_t *dict, char **op_errstr)
{
        int                 ret            = -1;
        char               *volname        = NULL;
        glusterd_volinfo_t *volinfo        = NULL;
        char               *brick          = NULL;
        int32_t             count          = 0;
        int32_t             i              = 1;
        char                key[256]       = {0,};
        int32_t             flag           = 0;
        char                err_str[4096]  = {0,};
        int                 need_rebalance = 0;
        int                 force          = 0;
        gf1_op_commands     cmd            = 0;

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = dict_get_int32 (dict, "command", &flag);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get brick count");
                goto out;
        }
        cmd = flag;

        ret = -1;
        switch (cmd) {
        case GF_OP_CMD_NONE:
                goto out;

        case GF_OP_CMD_STATUS:
                ret = 0;
                goto out;

        case GF_OP_CMD_PAUSE:
        {
                if (volinfo->decommission_in_progress) {
                        if (volinfo->defrag == (void *)1)
                                volinfo->defrag = NULL;

                        if (volinfo->defrag) {
                                LOCK (&volinfo->defrag->lock);

                                volinfo->defrag_status = GF_DEFRAG_STATUS_PAUSED;

                                UNLOCK (&volinfo->defrag->lock);
                        }
                }

                /* rebalance '_cbk()' will take care of volume file updates */
                ret = 0;
                goto out;
        }

        case GF_OP_CMD_ABORT:
        {
                if (volinfo->decommission_in_progress) {
                        if (volinfo->defrag == (void *)1)
                                volinfo->defrag = NULL;

                        if (volinfo->defrag) {
                                LOCK (&volinfo->defrag->lock);

                                volinfo->defrag_status = GF_DEFRAG_STATUS_STOPPED;

                                UNLOCK (&volinfo->defrag->lock);
                        }
                }

                /* rebalance '_cbk()' will take care of volume file updates */
                ret = 0;
                goto out;
        }

        case GF_OP_CMD_START:
                force = 0;
                break;

        case GF_OP_CMD_COMMIT:
                force = 1;
                break;

        case GF_OP_CMD_COMMIT_FORCE:

                if (volinfo->decommission_in_progress) {
                        if (volinfo->defrag == (void *)1)
                                volinfo->defrag = NULL;

                        if (volinfo->defrag) {
                                LOCK (&volinfo->defrag->lock);
                                /* Fake 'rebalance-complete' so the graph change
                                   happens right away */
                                volinfo->defrag_status = GF_DEFRAG_STATUS_COMPLETE;

                                UNLOCK (&volinfo->defrag->lock);
                        }
                        ret = 0;
                        /* Graph change happens in rebalance _cbk function,
                           no need to do anything here */
                        goto out;
                }

                force = 1;
                break;
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }


        while ( i <= count) {
                snprintf (key, 256, "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to get %s", key);
                        goto out;
                }

                ret = glusterd_op_perform_remove_brick (volinfo, brick, force,
                                                        (i == 1) ? &need_rebalance : NULL);
                if (ret)
                        goto out;
                i++;
        }

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING, "failed to create volfiles");
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING, "failed to store volinfo");
                goto out;
        }

        volinfo->defrag_status = 0;
        if (!force && need_rebalance) {
                /* perform the rebalance operations */
                ret = glusterd_handle_defrag_start (volinfo, err_str, 4096,
                                                    GF_DEFRAG_CMD_START_FORCE,
                                                    glusterd_remove_brick_migrate_cbk);
                if (!ret)
                        volinfo->decommission_in_progress = 1;

                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "failed to start the rebalance");
                }
        } else {
                if (GLUSTERD_STATUS_STARTED == volinfo->status)
                        ret = glusterd_check_generate_start_nfs ();
        }

out:
        if (ret && err_str[0] && op_errstr)
                *op_errstr = gf_strdup (err_str);

        return ret;
}
