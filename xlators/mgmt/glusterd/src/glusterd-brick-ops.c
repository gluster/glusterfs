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

/* misc */

/* In this function, we decide, based on the 'count' of the brick,
   where to add it in the current volume. 'count' tells us already
   how many of the given bricks are added. other argument are self-
   descriptive. */
int
add_brick_at_right_order (glusterd_brickinfo_t *brickinfo,
                          glusterd_volinfo_t *volinfo, int count,
                          int32_t stripe_cnt, int32_t replica_cnt)
{
        int                   idx     = 0;
        int                   i       = 0;
        int                   sub_cnt = 0;
        glusterd_brickinfo_t *brick   = NULL;

        /* The complexity of the function is in deciding at which index
           to add new brick. Even though it can be defined with a complex
           single formula for all volume, it is seperated out to make it
           more readable */
        if (stripe_cnt) {
                /* common formula when 'stripe_count' is set */
                /* idx = ((count / ((stripe_cnt * volinfo->replica_count) -
                   volinfo->dist_leaf_count)) * volinfo->dist_leaf_count) +
                   (count + volinfo->dist_leaf_count);
                */

                sub_cnt = volinfo->dist_leaf_count;

                idx = ((count / ((stripe_cnt * volinfo->replica_count) -
                                 sub_cnt)) * sub_cnt) +
                        (count + sub_cnt);

                goto insert_brick;
        }

        /* replica count is set */
        /* common formula when 'replica_count' is set */
        /* idx = ((count / (replica_cnt - existing_replica_count)) *
           existing_replica_count) +
           (count + existing_replica_count);
        */

        sub_cnt = volinfo->replica_count;
        idx = (count / (replica_cnt - sub_cnt) * sub_cnt) +
                (count + sub_cnt);

insert_brick:
        i = 0;
        list_for_each_entry (brick, &volinfo->bricks, brick_list) {
                i++;
                if (i < idx)
                        continue;
                gf_log (THIS->name, GF_LOG_DEBUG, "brick:%s index=%d, count=%d",
                        brick->path, idx, count);

                list_add (&brickinfo->brick_list, &brick->brick_list);
                break;
        }

        return 0;
}


static int
gd_addbr_validate_stripe_count (glusterd_volinfo_t *volinfo, int stripe_count,
                                int total_bricks, int *type, char *err_str,
                                size_t err_len)
{
        int ret = -1;

        switch (volinfo->type) {
        case GF_CLUSTER_TYPE_NONE:
                if ((volinfo->brick_count * stripe_count) == total_bricks) {
                        /* Change the volume type */
                        gf_log (THIS->name, GF_LOG_INFO,
                                "Changing the type of volume %s from "
                                "None to 'stripe'", volinfo->volname);
                        *type = GF_CLUSTER_TYPE_STRIPE;
                        ret = 0;
                        goto out;
                }
                break;
        case GF_CLUSTER_TYPE_REPLICATE:
                if (!(total_bricks % (volinfo->replica_count * stripe_count))) {
                        /* Change the volume type */
                        *type = GF_CLUSTER_TYPE_STRIPE_REPLICATE;
                        gf_log (THIS->name, GF_LOG_INFO,
                                "Changing the type of volume %s from "
                                "'replicate' to 'replicate-stripe'",
                                volinfo->volname);
                        ret = 0;
                        goto out;
                }
                break;
        case GF_CLUSTER_TYPE_STRIPE:
        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                if (stripe_count < volinfo->stripe_count) {
                        snprintf (err_str, sizeof (err_str),
                                  "wrong stripe count (%d) given. "
                                  "already have %d",
                                  stripe_count, volinfo->stripe_count);
                        gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                        goto out;
                }
                if (stripe_count == volinfo->stripe_count) {
                        if (!(total_bricks % volinfo->dist_leaf_count)) {
                                /* its same as the one which exists */
                                ret = 1;
                                goto out;
                        }
                }
                if (stripe_count > volinfo->stripe_count) {
                        /* We have to make sure before and after 'add-brick',
                           the number or subvolumes for distribute will remain
                           same, when stripe count is given */
                        if ((volinfo->brick_count * (stripe_count *
                                                     volinfo->replica_count)) ==
                            (total_bricks * volinfo->dist_leaf_count)) {
                                /* Change the dist_leaf_count */
                                gf_log (THIS->name, GF_LOG_INFO,
                                        "Changing the stripe count of "
                                        "volume %s from %d to %d",
                                        volinfo->volname,
                                        volinfo->stripe_count, stripe_count);
                                ret = 0;
                                goto out;
                        }
                }
                break;
        }

out:
        return ret;
}

static int
gd_addbr_validate_replica_count (glusterd_volinfo_t *volinfo, int replica_count,
                                 int total_bricks, int *type, char *err_str, int err_len)
{
        int ret = -1;

        /* replica count is set */
        switch (volinfo->type) {
        case GF_CLUSTER_TYPE_NONE:
                if ((volinfo->brick_count * replica_count) == total_bricks) {
                        /* Change the volume type */
                        gf_log (THIS->name, GF_LOG_INFO,
                                "Changing the type of volume %s from "
                                "None to 'replica'", volinfo->volname);
                        *type = GF_CLUSTER_TYPE_REPLICATE;
                        ret = 0;
                        goto out;
                }
                break;
        case GF_CLUSTER_TYPE_STRIPE:
                if (!(total_bricks % (volinfo->dist_leaf_count * replica_count))) {
                        /* Change the volume type */
                        *type = GF_CLUSTER_TYPE_STRIPE_REPLICATE;
                        gf_log (THIS->name, GF_LOG_INFO,
                                "Changing the type of volume %s from "
                                "'stripe' to 'replicate-stripe'",
                                volinfo->volname);
                        ret = 0;
                        goto out;
                }
                break;
        case GF_CLUSTER_TYPE_REPLICATE:
        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                if (replica_count < volinfo->replica_count) {
                        snprintf (err_str, sizeof (err_str),
                                  "wrong replica count (%d) given. "
                                  "already have %d",
                                  replica_count, volinfo->replica_count);
                        gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                        goto out;
                }
                if (replica_count == volinfo->replica_count) {
                        if (!(total_bricks % volinfo->dist_leaf_count)) {
                                ret = 1;
                                goto out;
                        }
                }
                if (replica_count > volinfo->replica_count) {
                        /* We have to make sure before and after 'add-brick',
                           the number or subvolumes for distribute will remain
                           same, when replica count is given */
                        if ((total_bricks * volinfo->dist_leaf_count) ==
                            (volinfo->brick_count * (replica_count *
                                                     volinfo->stripe_count))) {
                                /* Change the dist_leaf_count */
                                gf_log (THIS->name, GF_LOG_INFO,
                                        "Changing the replica count of "
                                        "volume %s from %d to %d",
                                        volinfo->volname, volinfo->replica_count,
                                        replica_count);
                                ret = 0;
                                goto out;
                        }
                }
                break;
        }
out:
        return ret;
}

static int
gd_rmbr_validate_replica_count (glusterd_volinfo_t *volinfo, int32_t replica_count,
                                int32_t brick_count, char *err_str)
{
        int ret = -1;
        int replica_nodes = 0;

        switch (volinfo->type) {
        case GF_CLUSTER_TYPE_NONE:
        case GF_CLUSTER_TYPE_STRIPE:
                snprintf (err_str, 2048,
                          "replica count (%d) option given for non replicate "
                          "volume %s", replica_count, volinfo->volname);
                gf_log (THIS->name, GF_LOG_WARNING, "%s", err_str);
                goto out;

        case GF_CLUSTER_TYPE_REPLICATE:
        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                /* in remove brick, you can only reduce the replica count */
                if (replica_count > volinfo->replica_count) {
                        snprintf (err_str, 2048,
                                  "given replica count (%d) option is more "
                                  "than volume %s's replica count (%d)",
                                  replica_count, volinfo->volname,
                                  volinfo->replica_count);
                        gf_log (THIS->name, GF_LOG_WARNING, "%s", err_str);
                        goto out;
                }
                if (replica_count == volinfo->replica_count) {
                        ret = 1;
                        goto out;
                }

                replica_nodes = ((volinfo->brick_count / volinfo->replica_count) *
                                 (volinfo->replica_count - replica_count));

                if (brick_count % replica_nodes) {
                        snprintf (err_str, 2048,
                                  "need %d(xN) bricks for reducing replica "
                                  "count of the volume from %d to %d",
                                  replica_nodes, volinfo->replica_count,
                                  replica_count);
                        goto out;
                }
                break;
        }

        ret = 0;
out:
        return ret;
}



/* Handler functions */
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
        int32_t                         replica_count = 0;
        int32_t                         stripe_count = 0;
        int                             count = 0;
        int                             type = 0;

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

        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (!ret) {
                gf_log (THIS->name, GF_LOG_INFO, "replica-count is %d",
                        replica_count);
        }

        ret = dict_get_int32 (dict, "stripe-count", &stripe_count);
        if (!ret) {
                gf_log (THIS->name, GF_LOG_INFO, "stripe-count is %d",
                        stripe_count);
        }


        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volinfo "
                          "for volume name %s", volname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                goto out;

        }

        if (volinfo->type == GF_CLUSTER_TYPE_NONE)
                goto brick_val;

        /* If any of this is true, some thing is wrong */
        if (!brick_count || !volinfo->sub_count) {
                ret = -1;
                snprintf (err_str, sizeof (err_str), "number of brick count "
                          "for volume name %s is wrong", volname);
                goto out;
        }

        total_bricks = volinfo->brick_count + brick_count;

        if (!stripe_count && !replica_count) {
                if (volinfo->type == GF_CLUSTER_TYPE_NONE)
                        goto brick_val;

                if ((volinfo->brick_count < volinfo->dist_leaf_count) &&
                    (total_bricks <= volinfo->dist_leaf_count))
                        goto brick_val;

                if ((brick_count % volinfo->dist_leaf_count) != 0) {
                        snprintf(err_str, 2048, "Incorrect number of bricks"
                                 " supplied %d with count %d",
                                 brick_count, volinfo->dist_leaf_count);
                        gf_log("glusterd", GF_LOG_ERROR, "%s", err_str);
                        ret = -1;
                        goto out;
                }
                goto brick_val;
                /* done with validation.. below section is if stripe|replica
                   count is given */
        }

        /* These bricks needs to be added one per a replica or stripe volume */
        if (stripe_count) {
                ret = gd_addbr_validate_stripe_count (volinfo, stripe_count,
                                                      total_bricks, &type,
                                                      err_str,
                                                      sizeof (err_str));
                if (ret == -1) {
                        gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                        goto out;
                }

                /* if stripe count is same as earlier, set it back to 0 */
                if (ret == 1)
                        stripe_count = 0;

                goto brick_val;
        }

        ret = gd_addbr_validate_replica_count (volinfo, replica_count,
                                               total_bricks,
                                               &type, err_str,
                                               sizeof (err_str));
        if (ret == -1) {
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        /* if replica count is same as earlier, set it back to 0 */
        if (ret == 1)
                replica_count = 0;

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

        gf_cmd_log ("Volume add-brick", "volname: %s type %d count:%d bricks:%s"
                    ,volname, volinfo->type, brick_count, brick_list);

        count = 0;
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
                                                     &tmpvolinfo, &tmpbrkinfo,
                                                     GF_PATH_PARTIAL);
                if (!ret) {
                        ret = -1;
                        snprintf (err_str, sizeof (err_str), "Brick: %s:%s, %s"
                                  " one of the bricks contain the other",
                                  tmpbrkinfo->hostname, tmpbrkinfo->path, brick);
                        goto out;
                }

                if (stripe_count || replica_count)
                        add_brick_at_right_order (brickinfo, &tmpvolinfo, count,
                                                  stripe_count, replica_count);
                else
                        list_add_tail (&brickinfo->brick_list, &tmpvolinfo.bricks);

                count++;
                brickinfo = NULL;
        }

        if (stripe_count) {
                dict_del (dict, "stripe-count");
                ret = dict_set_int32 (dict, "stripe-count", stripe_count);
                if (ret)
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "failed to set the stripe-count in dict");
        }
        if (replica_count) {
                dict_del (dict, "replica-count");
                ret = dict_set_int32 (dict, "replica-count", replica_count);
                if (ret)
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "failed to set the replica-count in dict");
        }
        if (type != volinfo->type) {
                ret = dict_set_int32 (dict, "type", type);
                if (ret)
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "failed to set the new type in dict");
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
        int32_t                   ret              = -1;
        gf1_cli_remove_brick_req  cli_req          = {0,};
        dict_t                   *dict             = NULL;
        int32_t                   count            = 0;
        char                     *brick            = NULL;
        char                      key[256]         = {0,};
        char                     *brick_list       = NULL;
        int                       i                = 1;
        glusterd_volinfo_t       *volinfo          = NULL;
        glusterd_brickinfo_t     *brickinfo        = NULL;
        int32_t                   pos              = 0;
        int32_t                   sub_volume       = 0;
        int32_t                   sub_volume_start = 0;
        int32_t                   sub_volume_end   = 0;
        glusterd_brickinfo_t     *tmp              = NULL;
        char                      err_str[2048]    = {0};
        gf1_cli_remove_brick_rsp  rsp              = {0,};
        void                     *cli_rsp          = NULL;
        char                      vol_type[256]    = {0,};
        int32_t                   replica_count    = 0;
        int32_t                   brick_index      = 0;
        int32_t                   tmp_brick_idx    = 0;
        int                       found            = 0;
        int                       diff_count       = 0;

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

        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (!ret) {
                gf_log (THIS->name, GF_LOG_INFO,
                        "request to change replica-count to %d", replica_count);
                ret = gd_rmbr_validate_replica_count (volinfo, replica_count,
                                                      count, err_str);
                if (ret < 0) {
                        /* logging and error msg are done in above function itself */
                        goto out;
                }
                dict_del (dict, "replica-count");
                if (ret) {
                        replica_count = 0;
                } else {
                        ret = dict_set_int32 (dict, "replica-count", replica_count);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_WARNING,
                                        "failed to set the replica_count in dict");
                                goto out;
                        }
                }
        }

        /* 'vol_type' is used for giving the meaning full error msg for user */
        if (volinfo->type == GF_CLUSTER_TYPE_REPLICATE) {
                strcpy (vol_type, "replica");
        } else if (volinfo->type == GF_CLUSTER_TYPE_STRIPE) {
                strcpy (vol_type, "stripe");
        } else if (volinfo->type == GF_CLUSTER_TYPE_STRIPE_REPLICATE) {
                strcpy (vol_type, "stripe-replicate");
        } else {
                strcpy (vol_type, "distribute");
        }

	/* Do not allow remove-brick if the volume is plain stripe */
	if ((volinfo->type == GF_CLUSTER_TYPE_STRIPE) &&
            (volinfo->brick_count == volinfo->stripe_count)) {
                snprintf (err_str, 2048, "Removing brick from a plain stripe is not allowed");
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                ret = -1;
                goto out;
	}

	/* Do not allow remove-brick if the bricks given is less than the replica count
	   or stripe count */
        if (!replica_count && (volinfo->type != GF_CLUSTER_TYPE_NONE) &&
            !(volinfo->brick_count <= volinfo->dist_leaf_count)) {
                if (volinfo->dist_leaf_count &&
                    (count % volinfo->dist_leaf_count)) {
                        snprintf (err_str, 2048, "Remove brick incorrect"
                                  " brick count of %d for %s %d",
                                  count, vol_type, volinfo->dist_leaf_count);
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

                ret = glusterd_volume_brickinfo_get_by_brick(brick, volinfo,
                                                             &brickinfo,
                                                             GF_PATH_COMPLETE);
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
                    (volinfo->brick_count <= volinfo->dist_leaf_count))
                        continue;

                if (replica_count) {
                        /* do the validation of bricks here */
                        /* -2 because i++ is already done, and i starts with 1,
                           instead of 0 */
                        diff_count = (volinfo->replica_count - replica_count);
                        brick_index = (((i -2) / diff_count) * volinfo->replica_count);
                        tmp_brick_idx = 0;
                        found = 0;
                        list_for_each_entry (tmp, &volinfo->bricks, brick_list) {
                                tmp_brick_idx++;
                                gf_log (THIS->name, GF_LOG_TRACE,
                                        "validate brick %s:%s (%d %d %d)",
                                        tmp->hostname, tmp->path, tmp_brick_idx,
                                        brick_index, volinfo->replica_count);
                                if (tmp_brick_idx <= brick_index)
                                        continue;
                                if (tmp_brick_idx >
                                    (brick_index + volinfo->replica_count))
                                        break;
                                if ((!strcmp (tmp->hostname,brickinfo->hostname)) &&
                                    !strcmp (tmp->path, brickinfo->path)) {
                                        found = 1;
                                        break;
                                }
                        }
                        if (found)
                                continue;

                        snprintf(err_str, 2048,"Bricks are from same subvol");
                        gf_log (THIS->name, GF_LOG_INFO,
                                "failed to validate brick %s:%s (%d %d %d)",
                                tmp->hostname, tmp->path, tmp_brick_idx,
                                brick_index, volinfo->replica_count);
                        ret = -1;
                        /* brick order is not valid */
                        goto out;
                }

                pos = 0;
                list_for_each_entry (tmp, &volinfo->bricks, brick_list) {

                        if (strcmp (tmp->hostname,brickinfo->hostname) ||
                            strcmp (tmp->path, brickinfo->path)) {
                                pos++;
                                continue;
                        }

                        gf_log ("", GF_LOG_INFO, "Found brick");
                        if (!sub_volume && (volinfo->dist_leaf_count > 1)) {
                                sub_volume = (pos / volinfo->dist_leaf_count) + 1;
                                sub_volume_start = (volinfo->dist_leaf_count *
                                                    (sub_volume - 1));
                                sub_volume_end = (volinfo->dist_leaf_count *
                                                  sub_volume) - 1;
                        } else {
                                if (pos < sub_volume_start ||
                                    pos >sub_volume_end) {
                                        ret = -1;
                                        snprintf(err_str, 2048,"Bricks not from"
                                                 " same subvol for %s",
                                                 vol_type);
                                        gf_log ("", GF_LOG_ERROR,
                                                "%s", err_str);
                                        goto out;
                                }
                        }
                        break;
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
glusterd_op_perform_add_bricks (glusterd_volinfo_t *volinfo, int32_t count,
                                char  *bricks, dict_t *dict)
{
        glusterd_brickinfo_t *brickinfo     = NULL;
        char                 *brick         = NULL;
        int32_t               i             = 1;
        char                 *brick_list    = NULL;
        char                 *free_ptr1     = NULL;
        char                 *free_ptr2     = NULL;
        char                 *saveptr       = NULL;
        int32_t               ret           = -1;
        int32_t               stripe_count  = 0;
        int32_t               replica_count = 0;
        int32_t               type          = 0;

        GF_ASSERT (volinfo);

        if (bricks) {
                brick_list = gf_strdup (bricks);
                free_ptr1 = brick_list;
        }

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);

        if (dict) {
                ret = dict_get_int32 (dict, "stripe-count", &stripe_count);
                if (!ret)
                        gf_log (THIS->name, GF_LOG_INFO,
                                "stripe-count is set %d", stripe_count);

                ret = dict_get_int32 (dict, "replica-count", &replica_count);
                if (!ret)
                        gf_log (THIS->name, GF_LOG_INFO,
                                "replica-count is set %d", replica_count);
                ret = dict_get_int32 (dict, "type", &type);
                if (!ret)
                        gf_log (THIS->name, GF_LOG_INFO,
                                "type is set %d, need to change it", type);
        }

        while ( i <= count) {
                ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
                if (ret)
                        goto out;

                ret = glusterd_resolve_brick (brickinfo);
                if (ret)
                        goto out;
                if (stripe_count || replica_count) {
                        add_brick_at_right_order (brickinfo, volinfo, (i - 1),
                                                  stripe_count, replica_count);
                } else {
                        list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                }
                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
                volinfo->brick_count++;

        }


        /* Gets changed only if the options are given in add-brick cli */
        if (type)
                volinfo->type = type;
        if (replica_count) {
                volinfo->replica_count = replica_count;
        }
        if (stripe_count) {
                volinfo->stripe_count = stripe_count;
        }
        volinfo->dist_leaf_count = (volinfo->stripe_count *
                                    volinfo->replica_count);

        /* backward compatibility */
        volinfo->sub_count = ((volinfo->dist_leaf_count == 1) ? 0:
                              volinfo->dist_leaf_count);

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
                                                              &brickinfo,
                                                              GF_PATH_PARTIAL);
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

        ret = glusterd_volume_brickinfo_get_by_brick (dup_brick, volinfo,
                                                      &brickinfo, GF_PATH_COMPLETE);
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
                                                              &brickinfo,
                                                              GF_PATH_PARTIAL);
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

#if 0  /* TODO: enable this behavior once cluster-wide awareness comes for
          defrag cbk function */
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

#endif

        volinfo->decommission_in_progress = 0;
        return ret;
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

        ret = glusterd_op_perform_add_bricks (volinfo, count, bricks, dict);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to add bricks");
                goto out;
        }

        /* Need to reset the defrag/rebalance status accordingly */
        switch (volinfo->defrag_status) {
        case GF_DEFRAG_STATUS_FAILED:
        case GF_DEFRAG_STATUS_COMPLETE:
        case GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE:
        case GF_DEFRAG_STATUS_MIGRATE_DATA_COMPLETE:
                volinfo->defrag_status = 0;
        default:
                break;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_nodesvcs_handle_graph_change (volinfo);

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
        int32_t             replica_count  = 0;
        glusterd_brickinfo_t *brickinfo    = NULL;
        glusterd_brickinfo_t *tmp          = NULL;

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
                        if (volinfo->defrag) {
                                LOCK (&volinfo->defrag->lock);

                                volinfo->defrag_status = GF_DEFRAG_STATUS_PAUSED;

                                UNLOCK (&volinfo->defrag->lock);
                        }
                }

                /* no need to update anything */
                ret = 0;
                goto out;
        }

        case GF_OP_CMD_ABORT:
        {
                if (volinfo->decommission_in_progress) {
                        if (volinfo->defrag) {
                                LOCK (&volinfo->defrag->lock);

                                volinfo->defrag_status = GF_DEFRAG_STATUS_STOPPED;

                                UNLOCK (&volinfo->defrag->lock);
                        }
                }

                /* Fall back to the old volume file */
                list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks, brick_list) {
                        if (!brickinfo->decommissioned)
                                continue;
                        brickinfo->decommissioned = 0;
                }
                ret = 0;
                break;
        }

        case GF_OP_CMD_START:
                force = 0;
                break;

        case GF_OP_CMD_COMMIT:
                force = 1;
                break;

        case GF_OP_CMD_COMMIT_FORCE:

                if (volinfo->decommission_in_progress) {
                        if (volinfo->defrag) {
                                LOCK (&volinfo->defrag->lock);
                                /* Fake 'rebalance-complete' so the graph change
                                   happens right away */
                                volinfo->defrag_status = GF_DEFRAG_STATUS_COMPLETE;

                                UNLOCK (&volinfo->defrag->lock);
                        }
                        /* Graph change happens in rebalance _cbk function,
                           no need to do anything here */
                        /* TODO: '_cbk' function is not doing anything for now */
                }

                ret = 0;
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
        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (!ret) {
                gf_log (THIS->name, GF_LOG_INFO,
                        "changing replica count %d to %d on volume %s",
                        volinfo->replica_count, replica_count,
                        volinfo->volname);
                volinfo->replica_count = replica_count;
                volinfo->dist_leaf_count = (volinfo->stripe_count *
                                            replica_count);
                if (replica_count == 1) {
                        if (volinfo->type == GF_CLUSTER_TYPE_REPLICATE) {
                                volinfo->type = GF_CLUSTER_TYPE_NONE;
                                /* backward compatibility */
                                volinfo->sub_count = 0;
                        } else {
                                volinfo->type = GF_CLUSTER_TYPE_STRIPE;
                                /* backward compatibility */
                                volinfo->sub_count = volinfo->dist_leaf_count;
                        }
                }
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

        /* Need to reset the defrag/rebalance status accordingly */
        switch (volinfo->defrag_status) {
        case GF_DEFRAG_STATUS_FAILED:
        case GF_DEFRAG_STATUS_COMPLETE:
        case GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE:
        case GF_DEFRAG_STATUS_MIGRATE_DATA_COMPLETE:
                volinfo->defrag_status = 0;
        default:
                break;
        }
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
                        ret = glusterd_nodesvcs_handle_graph_change (volinfo);
        }

out:
        if (ret && err_str[0] && op_errstr)
                *op_errstr = gf_strdup (err_str);

        return ret;
}
