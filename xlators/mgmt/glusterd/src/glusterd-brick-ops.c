/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "common-utils.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-geo-rep.h"
#include "glusterd-store.h"
#include "glusterd-mgmt.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "glusterd-svc-helper.h"
#include "glusterd-messages.h"
#include "glusterd-server-quorum.h"
#include "run.h"
#include "glusterd-volgen.h"
#include <sys/signal.h>

/* misc */

gf_boolean_t
glusterd_is_tiering_supported (char *op_errstr)
{
        xlator_t           *this        = NULL;
        glusterd_conf_t    *conf        = NULL;
        gf_boolean_t        supported   = _gf_false;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        if (conf->op_version < GD_OP_VERSION_3_7_0)
                goto out;

        supported = _gf_true;

out:
        if (!supported && op_errstr != NULL && conf)
                sprintf (op_errstr, "Tier operation failed. The cluster is "
                         "operating at version %d. Tiering"
                         " is unavailable in this version.",
                         conf->op_version);

        return supported;
}

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
           single formula for all volume, it is separated out to make it
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
        cds_list_for_each_entry (brick, &volinfo->bricks, brick_list) {
                i++;
                if (i < idx)
                        continue;
                gf_msg_debug (THIS->name, 0, "brick:%s index=%d, count=%d",
                        brick->path, idx, count);

                cds_list_add (&brickinfo->brick_list, &brick->brick_list);
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
                        *type = GF_CLUSTER_TYPE_STRIPE;
                        gf_msg (THIS->name, GF_LOG_INFO, 0,
                                GD_MSG_VOL_TYPE_CHANGING_INFO,
                                "Changing the type of volume %s from "
                                "'distribute' to 'stripe'", volinfo->volname);
                        ret = 0;
                        goto out;
                } else {
                        snprintf (err_str, err_len, "Incorrect number of "
                                  "bricks (%d) supplied for stripe count (%d).",
                                  (total_bricks - volinfo->brick_count),
                                  stripe_count);
                        gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INVALID_ENTRY, "%s", err_str);
                        goto out;
                }
                break;
        case GF_CLUSTER_TYPE_REPLICATE:
                if (!(total_bricks % (volinfo->replica_count * stripe_count))) {
                        /* Change the volume type */
                        *type = GF_CLUSTER_TYPE_STRIPE_REPLICATE;
                        gf_msg (THIS->name, GF_LOG_INFO, 0,
                                GD_MSG_VOL_TYPE_CHANGING_INFO,
                                "Changing the type of volume %s from "
                                "'replicate' to 'replicate-stripe'",
                                volinfo->volname);
                        ret = 0;
                        goto out;
                } else {
                        snprintf (err_str, err_len, "Incorrect number of "
                                  "bricks (%d) supplied for changing volume's "
                                  "stripe count to %d, need at least %d bricks",
                                  (total_bricks - volinfo->brick_count),
                                  stripe_count,
                                  (volinfo->replica_count * stripe_count));
                        gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INVALID_ENTRY, "%s", err_str);
                        goto out;
                }
                break;
        case GF_CLUSTER_TYPE_STRIPE:
        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                if (stripe_count < volinfo->stripe_count) {
                        snprintf (err_str, err_len,
                                  "Incorrect stripe count (%d) supplied. "
                                  "Volume already has stripe count (%d)",
                                  stripe_count, volinfo->stripe_count);
                        gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INVALID_ENTRY, "%s", err_str);
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
                                gf_msg (THIS->name, GF_LOG_INFO, 0,
                                        GD_MSG_STRIPE_COUNT_CHANGE_INFO,
                                        "Changing the stripe count of "
                                        "volume %s from %d to %d",
                                        volinfo->volname,
                                        volinfo->stripe_count, stripe_count);
                                ret = 0;
                                goto out;
                        }
                }
                break;
        case GF_CLUSTER_TYPE_DISPERSE:
                snprintf (err_str, err_len, "Volume %s cannot be converted "
                                            "from dispersed to striped-"
                                            "dispersed", volinfo->volname);
                gf_msg(THIS->name, GF_LOG_ERROR, EPERM,
                       GD_MSG_OP_NOT_PERMITTED, "%s", err_str);
                goto out;
        }

out:
        return ret;
}

static int
gd_addbr_validate_replica_count (glusterd_volinfo_t *volinfo, int replica_count,
                                 int arbiter_count, int total_bricks, int *type,
                                 char *err_str, int err_len)
{
        int ret = -1;

        /* replica count is set */
        switch (volinfo->type) {
        case GF_CLUSTER_TYPE_NONE:
                if ((volinfo->brick_count * replica_count) == total_bricks) {
                        /* Change the volume type */
                        *type = GF_CLUSTER_TYPE_REPLICATE;
                        gf_msg (THIS->name, GF_LOG_INFO, 0,
                                GD_MSG_VOL_TYPE_CHANGING_INFO,
                                "Changing the type of volume %s from "
                                "'distribute' to 'replica'", volinfo->volname);
                        ret = 0;
                        goto out;

                } else {
                        snprintf (err_str, err_len, "Incorrect number of "
                                  "bricks (%d) supplied for replica count (%d).",
                                  (total_bricks - volinfo->brick_count),
                                  replica_count);
                        gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INVALID_ENTRY, "%s", err_str);
                        goto out;
                }
                break;
        case GF_CLUSTER_TYPE_STRIPE:
                if (!(total_bricks % (volinfo->dist_leaf_count * replica_count))) {
                        /* Change the volume type */
                        *type = GF_CLUSTER_TYPE_STRIPE_REPLICATE;
                        gf_msg (THIS->name, GF_LOG_INFO, 0,
                                GD_MSG_VOL_TYPE_CHANGING_INFO,
                                "Changing the type of volume %s from "
                                "'stripe' to 'replicate-stripe'",
                                volinfo->volname);
                        ret = 0;
                        goto out;
                } else {
                        snprintf (err_str, err_len, "Incorrect number of "
                                  "bricks (%d) supplied for changing volume's "
                                  "replica count to %d, need at least %d "
                                  "bricks",
                                  (total_bricks - volinfo->brick_count),
                                  replica_count, (volinfo->dist_leaf_count *
                                                  replica_count));
                        gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INVALID_ENTRY, "%s", err_str);
                        goto out;
                }
                break;
        case GF_CLUSTER_TYPE_REPLICATE:
        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                if (replica_count < volinfo->replica_count) {
                        snprintf (err_str, err_len,
                                  "Incorrect replica count (%d) supplied. "
                                  "Volume already has (%d)",
                                  replica_count, volinfo->replica_count);
                        gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INVALID_ENTRY, "%s", err_str);
                        goto out;
                }
                if (replica_count == volinfo->replica_count) {
                        if (arbiter_count && !volinfo->arbiter_count) {
                                snprintf (err_str, err_len,
                                          "Cannot convert replica 3 volume "
                                          "to arbiter volume.");
                                gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                                        GD_MSG_INVALID_ENTRY, "%s", err_str);
                                goto out;
                        }
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
                                gf_msg (THIS->name, GF_LOG_INFO, 0,
                                        GD_MSG_REPLICA_COUNT_CHANGE_INFO,
                                        "Changing the replica count of "
                                        "volume %s from %d to %d",
                                        volinfo->volname, volinfo->replica_count,
                                        replica_count);
                                ret = 0;
                                goto out;
                        }
                }
                break;
        case GF_CLUSTER_TYPE_DISPERSE:
                snprintf (err_str, err_len, "Volume %s cannot be converted "
                                            "from dispersed to replicated-"
                                            "dispersed", volinfo->volname);
                gf_msg(THIS->name, GF_LOG_ERROR, EPERM,
                       GD_MSG_OP_NOT_PERMITTED, "%s", err_str);
                goto out;
        }
out:
        return ret;
}

static int
gd_rmbr_validate_replica_count (glusterd_volinfo_t *volinfo,
                                int32_t replica_count,
                                int32_t brick_count, char *err_str,
                                size_t err_len)
{
        int ret = -1;
        int replica_nodes = 0;

        switch (volinfo->type) {
        case GF_CLUSTER_TYPE_TIER:
                ret = 1;
                goto out;

        case GF_CLUSTER_TYPE_NONE:
        case GF_CLUSTER_TYPE_STRIPE:
        case GF_CLUSTER_TYPE_DISPERSE:
                snprintf (err_str, err_len,
                          "replica count (%d) option given for non replicate "
                          "volume %s", replica_count, volinfo->volname);
                gf_msg (THIS->name, GF_LOG_WARNING, 0,
                        GD_MSG_VOL_NOT_REPLICA, "%s", err_str);
                goto out;

        case GF_CLUSTER_TYPE_REPLICATE:
        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                /* in remove brick, you can only reduce the replica count */
                if (replica_count > volinfo->replica_count) {
                        snprintf (err_str, err_len,
                                  "given replica count (%d) option is more "
                                  "than volume %s's replica count (%d)",
                                  replica_count, volinfo->volname,
                                  volinfo->replica_count);
                        gf_msg (THIS->name, GF_LOG_WARNING, EINVAL,
                                GD_MSG_INVALID_ENTRY, "%s", err_str);
                        goto out;
                }
                if (replica_count == volinfo->replica_count) {
                        /* This means the 'replica N' option on CLI was
                           redundant. Check if the total number of bricks given
                           for removal is same as 'dist_leaf_count' */
                        if (brick_count % volinfo->dist_leaf_count) {
                                snprintf (err_str, err_len,
                                          "number of bricks provided (%d) is "
                                          "not valid. need at least %d "
                                          "(or %dxN)", brick_count,
                                          volinfo->dist_leaf_count,
                                          volinfo->dist_leaf_count);
                                gf_msg (THIS->name, GF_LOG_WARNING, EINVAL,
                                        GD_MSG_INVALID_ENTRY, "%s",
                                        err_str);
                                goto out;
                        }
                        ret = 1;
                        goto out;
                }

                replica_nodes = ((volinfo->brick_count /
                                  volinfo->replica_count) *
                                 (volinfo->replica_count - replica_count));

                if (brick_count % replica_nodes) {
                        snprintf (err_str, err_len,
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
__glusterd_handle_add_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        char                            *bricks = NULL;
        char                            *volname = NULL;
        int                             brick_count = 0;
        void                            *cli_rsp = NULL;
        char                            err_str[2048] = {0,};
        gf_cli_rsp                      rsp = {0,};
        glusterd_volinfo_t              *volinfo = NULL;
        xlator_t                        *this = NULL;
        int                             total_bricks = 0;
        int32_t                         replica_count = 0;
        int32_t                         arbiter_count = 0;
        int32_t                         stripe_count = 0;
        int                             type = 0;
        glusterd_conf_t                 *conf = NULL;

        this = THIS;
        GF_ASSERT(this);

        GF_ASSERT (req);

        conf = this->private;
        GF_ASSERT (conf);

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                snprintf (err_str, sizeof (err_str), "Garbage args received");
                goto out;
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_ADD_BRICK_REQ_RECVD, "Received add brick req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "name");
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        if (!(ret = glusterd_check_volume_exists (volname))) {
                ret = -1;
                snprintf (err_str, sizeof (err_str), "Volume %s does not exist",
                          volname);
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_VOL_NOT_FOUND, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "brick count");
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (!ret) {
                gf_msg (this->name, GF_LOG_INFO, errno,
                        GD_MSG_DICT_GET_SUCCESS, "replica-count is %d",
                        replica_count);
        }

        ret = dict_get_int32 (dict, "arbiter-count", &arbiter_count);
        if (!ret) {
                gf_msg (this->name, GF_LOG_INFO, errno,
                        GD_MSG_DICT_GET_SUCCESS, "arbiter-count is %d",
                        arbiter_count);
        }

        ret = dict_get_int32 (dict, "stripe-count", &stripe_count);
        if (!ret) {
                gf_msg (this->name, GF_LOG_INFO, errno,
                        GD_MSG_DICT_GET_SUCCESS, "stripe-count is %d",
                        stripe_count);
        }

        if (!dict_get (dict, "force")) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Failed to get flag");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volinfo "
                          "for volume name %s", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_GET_FAIL, "%s", err_str);
                goto out;

        }

        total_bricks = volinfo->brick_count + brick_count;

        if (dict_get (dict, "attach-tier")) {
                if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                        snprintf (err_str, sizeof (err_str),
                                  "Volume %s is already a tier.", volname);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOL_ALREADY_TIER, "%s", err_str);
                        ret = -1;
                        goto out;
                }

                if (glusterd_is_tiering_supported(err_str) == _gf_false) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VERSION_UNSUPPORTED,
                                "Tiering not supported at this version");
                        ret = -1;
                        goto out;
                }

                ret = dict_get_int32 (dict, "hot-type", &type);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_GET_FAILED,
                                "failed to get type from dictionary");
                        goto out;
                }

                goto brick_val;
        }

        ret = glusterd_disallow_op_for_tier (volinfo, GD_OP_ADD_BRICK, -1);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Add-brick operation is "
                          "not supported on a tiered volume %s", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_UNSUPPORTED, "%s", err_str);
                goto out;
        }

        if (!stripe_count && !replica_count) {
                if (volinfo->type == GF_CLUSTER_TYPE_NONE)
                        goto brick_val;

                if ((volinfo->brick_count < volinfo->dist_leaf_count) &&
                    (total_bricks <= volinfo->dist_leaf_count))
                        goto brick_val;

                if ((brick_count % volinfo->dist_leaf_count) != 0) {
                        snprintf (err_str, sizeof (err_str), "Incorrect number "
                                  "of bricks supplied %d with count %d",
                                 brick_count, volinfo->dist_leaf_count);
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_VOL_NOT_REPLICA, "%s", err_str);
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_COUNT_VALIDATE_FAILED, "%s", err_str);
                        goto out;
                }

                /* if stripe count is same as earlier, set it back to 0 */
                if (ret == 1)
                        stripe_count = 0;

                ret = dict_set_int32 (dict, "stripe-count", stripe_count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_GET_FAILED,
                                "failed to set the stripe-count in dict");
                        goto out;
                }
                goto brick_val;
        }

        ret = gd_addbr_validate_replica_count (volinfo, replica_count,
                                               arbiter_count, total_bricks,
                                               &type, err_str,
                                               sizeof (err_str));
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_COUNT_VALIDATE_FAILED, "%s", err_str);
                goto out;
        }

        /* if replica count is same as earlier, set it back to 0 */
        if (ret == 1)
                replica_count = 0;

        ret = dict_set_int32 (dict, "replica-count", replica_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED,
                        "failed to set the replica-count in dict");
                goto out;
        }

brick_val:
        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "bricks");
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        if (type != volinfo->type) {
                ret = dict_set_int32 (dict, "type", type);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set the new type in dict");
                        goto out;
                }
        }

        if (conf->op_version <= GD_OP_VERSION_3_7_5) {
                gf_msg_debug (this->name, 0, "The cluster is operating at "
                          "version less than or equal to %d. Falling back "
                          "to syncop framework.",
                          GD_OP_VERSION_3_7_5);
                ret = glusterd_op_begin_synctask (req, GD_OP_ADD_BRICK, dict);
        } else {
                ret = glusterd_mgmt_v3_initiate_all_phases (req,
                                                            GD_OP_ADD_BRICK,
                                                            dict);
        }

out:
        if (ret) {
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str), "Operation failed");
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_to_cli (req, cli_rsp, NULL, 0, NULL,
                                 (xdrproc_t)xdr_gf_cli_rsp, dict);
                ret = 0; //sent error to cli, prevent second reply
        }

        free (cli_req.dict.dict_val); //its malloced by xdr

        return ret;
}

int
glusterd_handle_add_brick (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_add_brick);
}

static int
subvol_matcher_init (int **subvols, int count)
{
        int ret = -1;

        *subvols = GF_CALLOC (count, sizeof(int), gf_gld_mt_int);
        if (*subvols)
                ret = 0;

        return ret;
}

static void
subvol_matcher_update (int *subvols, glusterd_volinfo_t *volinfo,
                       glusterd_brickinfo_t *brickinfo)
{
        glusterd_brickinfo_t *tmp        = NULL;
        int32_t               sub_volume = 0;
        int                   pos        = 0;

        cds_list_for_each_entry (tmp, &volinfo->bricks, brick_list) {

                if (strcmp (tmp->hostname, brickinfo->hostname) ||
                    strcmp (tmp->path, brickinfo->path)) {
                        pos++;
                        continue;
                }
                gf_msg_debug (THIS->name, 0, LOGSTR_FOUND_BRICK,
                                        brickinfo->hostname, brickinfo->path,
                                        volinfo->volname);
                sub_volume = (pos / volinfo->dist_leaf_count);
                subvols[sub_volume]++;
                break;
        }

}

static int
subvol_matcher_verify (int *subvols, glusterd_volinfo_t *volinfo, char *err_str,
                       size_t err_len, char *vol_type, int replica_count)
{
        int i = 0;
        int ret = 0;
        int count = volinfo->replica_count-replica_count;

        if (replica_count) {
                for (i = 0; i < volinfo->subvol_count; i++) {
                        if (subvols[i] != count) {
                                ret = -1;
                                snprintf (err_str, err_len, "Remove exactly %d"
                                " brick(s) from each subvolume.", count);
                                break;
                        }
                }
                return ret;
        }

       do {

                if (subvols[i] % volinfo->dist_leaf_count == 0) {
                        continue;
                } else {
                        ret = -1;
                        snprintf (err_str, err_len,
                                "Bricks not from same subvol for %s", vol_type);
                        break;
                }
        } while (++i < volinfo->subvol_count);

        return ret;
}

static void
subvol_matcher_destroy (int *subvols)
{
        GF_FREE (subvols);
}

int
glusterd_set_detach_bricks(dict_t *dict, glusterd_volinfo_t *volinfo)
{
        char key[256] = {0,};
        char value[256] = {0,};
        int brick_num = 0;
        int hot_brick_num = 0;
        glusterd_brickinfo_t *brickinfo;
        int ret = 0;

        /* cold tier bricks at tail of list so use reverse iteration */
        cds_list_for_each_entry_reverse (brickinfo, &volinfo->bricks,
                                         brick_list) {
                brick_num++;
                if (brick_num > volinfo->tier_info.cold_brick_count) {
                        hot_brick_num++;
                        sprintf (key, "brick%d", hot_brick_num);
                        snprintf (value, 256, "%s:%s",
                                  brickinfo->hostname,
                                  brickinfo->path);

                        ret = dict_set_str (dict, key, strdup(value));
                        if (ret)
                                break;
                }
        }

        ret = dict_set_int32(dict, "count", hot_brick_num);
        if (ret)
                return -1;

        return hot_brick_num;
}

static int
glusterd_remove_brick_validate_arbiters (glusterd_volinfo_t *volinfo,
                                         int32_t count, int32_t replica_count,
                                         glusterd_brickinfo_t **brickinfo_list,
                                         char *err_str, size_t err_len)
{
        int i = 0;
        int ret = 0;
        glusterd_brickinfo_t *brickinfo = NULL;
        glusterd_brickinfo_t *last = NULL;
        char *arbiter_array = NULL;

        if ((volinfo->type != GF_CLUSTER_TYPE_REPLICATE) &&
            (volinfo->type != GF_CLUSTER_TYPE_STRIPE_REPLICATE))
                goto out;

        if (!replica_count || !volinfo->arbiter_count)
                goto out;

        if (replica_count == 2) {
                /* If it is an arbiter to replica 2 conversion, only permit
                *  removal of the arbiter brick.*/
                for (i = 0; i < count; i++) {
                        brickinfo = brickinfo_list[i];
                        last = get_last_brick_of_brick_group (volinfo,
                                                              brickinfo);
                        if (last != brickinfo) {
                                snprintf (err_str, err_len, "Remove arbiter "
                                          "brick(s) only when converting from "
                                           "arbiter to replica 2 subvolume.");
                                ret = -1;
                                goto out;
                        }
                }
        } else if (replica_count == 1) {
                /* If it is an arbiter to plain distribute conversion, in every
                 * replica subvol, the arbiter has to be one of the bricks that
                 * are removed. */
                arbiter_array = GF_CALLOC (volinfo->subvol_count,
                                           sizeof (*arbiter_array),
                                           gf_common_mt_char);
                if (!arbiter_array)
                        return -1;
                for (i = 0; i < count; i++) {
                        brickinfo = brickinfo_list[i];
                        last = get_last_brick_of_brick_group (volinfo,
                                                              brickinfo);
                        if (last == brickinfo)
                                arbiter_array[brickinfo->group] = 1;
                }
                for (i = 0; i < volinfo->subvol_count; i++)
                        if (!arbiter_array[i]) {
                                snprintf (err_str, err_len, "Removed bricks "
                                          "must contain arbiter when converting"
                                           " to plain distrubute.");
                                ret = -1;
                                break;
                        }
                GF_FREE (arbiter_array);
        }

out:
        return ret;
}

int
__glusterd_handle_remove_brick (rpcsvc_request_t *req)
{
        int32_t                   ret              = -1;
        gf_cli_req                cli_req          = {{0,}};
        dict_t                   *dict             = NULL;
        int32_t                   count            = 0;
        char                     *brick            = NULL;
        char                      key[256]         = {0,};
        int                       i                = 1;
        glusterd_volinfo_t       *volinfo          = NULL;
        glusterd_brickinfo_t     *brickinfo        = NULL;
        glusterd_brickinfo_t     **brickinfo_list  = NULL;
        int                      *subvols          = NULL;
        char                      err_str[2048]    = {0};
        gf_cli_rsp                rsp              = {0,};
        void                     *cli_rsp          = NULL;
        char                      vol_type[256]    = {0,};
        int32_t                   replica_count    = 0;
        char                     *volname          = 0;
        xlator_t                 *this             = NULL;
        int                       cmd              = -1;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                snprintf (err_str, sizeof (err_str), "Received garbage args");
                goto out;
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_REM_BRICK_REQ_RECVD,
                "Received rem brick req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "name");
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get brick "
                          "count");
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                 snprintf (err_str, sizeof (err_str),"Volume %s does not exist",
                           volname);
                 gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                         GD_MSG_VOL_NOT_FOUND, "%s", err_str);
                 goto out;
        }

        if ((volinfo->type == GF_CLUSTER_TYPE_TIER) &&
            (glusterd_is_tiering_supported(err_str) == _gf_false)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VERSION_UNSUPPORTED,
                        "Tiering not supported at this version");
                ret = -1;
                goto out;
        }

        ret = dict_get_int32 (dict, "command", &cmd);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get cmd "
                          "ccommand");
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        ret = glusterd_disallow_op_for_tier (volinfo, GD_OP_REMOVE_BRICK, cmd);
        if (ret) {
                snprintf (err_str, sizeof (err_str),
                          "Removing brick from a Tier volume is not allowed");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_UNSUPPORTED, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (!ret) {
                gf_msg (this->name, GF_LOG_INFO, errno,
                        GD_MSG_DICT_GET_FAILED,
                        "request to change replica-count to %d", replica_count);
                ret = gd_rmbr_validate_replica_count (volinfo, replica_count,
                                                      count, err_str,
                                                      sizeof (err_str));
                if (ret < 0) {
                        /* logging and error msg are done in above function
                           itself */
                        goto out;
                }
                dict_del (dict, "replica-count");
                if (ret) {
                        replica_count = 0;
                } else {
                        ret = dict_set_int32 (dict, "replica-count",
                                              replica_count);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_WARNING, errno,
                                        GD_MSG_DICT_SET_FAILED,
                                        "failed to set the replica_count "
                                        "in dict");
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
        } else if (volinfo->type == GF_CLUSTER_TYPE_DISPERSE) {
                strcpy (vol_type, "disperse");
        } else {
                strcpy (vol_type, "distribute");
        }

	/* Do not allow remove-brick if the volume is a stripe volume*/
	if ((volinfo->type == GF_CLUSTER_TYPE_STRIPE) &&
            (volinfo->brick_count == volinfo->stripe_count)) {
                snprintf (err_str, sizeof (err_str),
                          "Removing brick from a stripe volume is not allowed");
                gf_msg (this->name, GF_LOG_ERROR, EPERM,
                        GD_MSG_OP_NOT_PERMITTED, "%s", err_str);
                ret = -1;
                goto out;
	}

	if (!replica_count &&
            (volinfo->type == GF_CLUSTER_TYPE_STRIPE_REPLICATE) &&
            (volinfo->brick_count == volinfo->dist_leaf_count)) {
                snprintf (err_str, sizeof(err_str),
                          "Removing bricks from stripe-replicate"
                          " configuration is not allowed without reducing "
                          "replica or stripe count explicitly.");
                gf_msg (this->name, GF_LOG_ERROR, EPERM,
                        GD_MSG_OP_NOT_PERMITTED_AC_REQD, "%s", err_str);
                ret = -1;
                goto out;
        }

	if (!replica_count &&
            (volinfo->type == GF_CLUSTER_TYPE_REPLICATE) &&
            (volinfo->brick_count == volinfo->dist_leaf_count)) {
                snprintf (err_str, sizeof (err_str),
                          "Removing bricks from replicate configuration "
                          "is not allowed without reducing replica count "
                          "explicitly.");
                gf_msg (this->name, GF_LOG_ERROR, EPERM,
                        GD_MSG_OP_NOT_PERMITTED_AC_REQD, "%s", err_str);
                ret = -1;
                goto out;
        }

	/* Do not allow remove-brick if the bricks given is less than
           the replica count or stripe count */
        if (!replica_count && (volinfo->type != GF_CLUSTER_TYPE_NONE) &&
            (volinfo->type != GF_CLUSTER_TYPE_TIER))  {
                if (volinfo->dist_leaf_count &&
                    (count % volinfo->dist_leaf_count)) {
                        snprintf (err_str, sizeof (err_str), "Remove brick "
                                  "incorrect brick count of %d for %s %d",
                                  count, vol_type, volinfo->dist_leaf_count);
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INVALID_ENTRY, "%s", err_str);
                        ret = -1;
                        goto out;
                }
        }

        /* subvol match is not required for tiered volume*/
        if ((volinfo->type != GF_CLUSTER_TYPE_NONE) &&
             (volinfo->type != GF_CLUSTER_TYPE_TIER) &&
             (volinfo->subvol_count > 1)) {
                ret = subvol_matcher_init (&subvols, volinfo->subvol_count);
                if (ret)
                        goto out;
        }

        if (volinfo->type == GF_CLUSTER_TYPE_TIER)
                count = glusterd_set_detach_bricks(dict, volinfo);

        brickinfo_list = GF_CALLOC (count, sizeof (*brickinfo_list),
                                    gf_common_mt_pointer);
        if (!brickinfo_list) {
                ret = -1;
                goto out;
        }

        while ( i <= count) {
                snprintf (key, sizeof (key), "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        snprintf (err_str, sizeof (err_str), "Unable to get %s",
                                  key);
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_GET_FAILED, "%s", err_str);
                        goto out;
                }
                gf_msg_debug (this->name, 0, "Remove brick count %d brick:"
                        " %s", i, brick);

                ret = glusterd_volume_brickinfo_get_by_brick(brick, volinfo,
                                                             &brickinfo,
                                                             _gf_false);

                if (ret) {
                        snprintf (err_str, sizeof (err_str), "Incorrect brick "
                                  "%s for volume %s", brick, volname);
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_BRICK_NOT_FOUND, "%s", err_str);
                        goto out;
                }
                brickinfo_list[i-1] = brickinfo;

                i++;
                if ((volinfo->type == GF_CLUSTER_TYPE_NONE) ||
                    (volinfo->brick_count <= volinfo->dist_leaf_count))
                        continue;

                /* Find which subvolume the brick belongs to.
                 * subvol match is not required for tiered volume
                 *
                 */
                if (volinfo->type != GF_CLUSTER_TYPE_TIER)
                        subvol_matcher_update (subvols, volinfo, brickinfo);
        }

        /* Check if the bricks belong to the same subvolumes.*/
        /* subvol match is not required for tiered volume*/
         if ((volinfo->type != GF_CLUSTER_TYPE_NONE) &&
             (volinfo->type != GF_CLUSTER_TYPE_TIER) &&
             (volinfo->subvol_count > 1)) {
                ret = subvol_matcher_verify (subvols, volinfo,
                                             err_str, sizeof(err_str),
                                             vol_type, replica_count);
                if (ret)
                        goto out;
        }

        ret = glusterd_remove_brick_validate_arbiters (volinfo, count,
                                                       replica_count,
                                                       brickinfo_list,
                                                       err_str,
                                                       sizeof (err_str));
        if (ret)
                goto out;

        ret = glusterd_op_begin_synctask (req, GD_OP_REMOVE_BRICK, dict);

out:
        if (ret) {
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_OP_FAILED, "%s", err_str);
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_to_cli (req, cli_rsp, NULL, 0, NULL,
                                 (xdrproc_t)xdr_gf_cli_rsp, dict);

                ret = 0; //sent error to cli, prevent second reply

        }

        if (brickinfo_list)
                GF_FREE (brickinfo_list);
        subvol_matcher_destroy (subvols);
        free (cli_req.dict.dict_val); //its malloced by xdr

        return ret;
}

int
glusterd_handle_remove_brick (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_remove_brick);
}

static int
_glusterd_restart_gsync_session (dict_t *this, char *key,
                                 data_t *value, void *data)
{
        char                          *slave      = NULL;
        char                          *slave_buf  = NULL;
        char                          *path_list  = NULL;
        char                          *slave_vol  = NULL;
        char                          *slave_host = NULL;
        char                          *slave_url  = NULL;
        char                          *conf_path  = NULL;
        char                         **errmsg     = NULL;
        int                            ret        = -1;
        glusterd_gsync_status_temp_t  *param      = NULL;
        gf_boolean_t                   is_running = _gf_false;

        param = (glusterd_gsync_status_temp_t *)data;

        GF_ASSERT (param);
        GF_ASSERT (param->volinfo);

        slave = strchr(value->data, ':');
        if (slave) {
                slave++;
                slave_buf = gf_strdup (slave);
                if (!slave_buf) {
                        gf_msg ("glusterd", GF_LOG_ERROR, ENOMEM,
                                GD_MSG_NO_MEMORY,
                                "Failed to gf_strdup");
                        ret = -1;
                        goto out;
                }
        }
        else
                return 0;

        ret = dict_set_dynstr (param->rsp_dict, "slave", slave_buf);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to store slave");
                if (slave_buf)
                        GF_FREE(slave_buf);
                goto out;
        }

        ret = glusterd_get_slave_details_confpath (param->volinfo,
                                                   param->rsp_dict, &slave_url,
                                                   &slave_host, &slave_vol,
                                                   &conf_path, errmsg);
        if (ret) {
                if (*errmsg)
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_SLAVE_CONFPATH_DETAILS_FETCH_FAIL,
                                "%s", *errmsg);
                else
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_SLAVE_CONFPATH_DETAILS_FETCH_FAIL,
                                "Unable to fetch slave or confpath details.");
                goto out;
        }

        /* In cases that gsyncd is not running, we will not invoke it
         * because of add-brick. */
        ret = glusterd_check_gsync_running_local (param->volinfo->volname,
                                                  slave, conf_path,
                                                  &is_running);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_GSYNC_VALIDATION_FAIL, "gsync running validation failed.");
                goto out;
        }
        if (_gf_false == is_running) {
                gf_msg_debug ("glusterd", 0, "gsync session for %s and %s is"
                        " not running on this node. Hence not restarting.",
                        param->volinfo->volname, slave);
                ret = 0;
                goto out;
        }

        ret = glusterd_get_local_brickpaths (param->volinfo, &path_list);
        if (!path_list) {
                gf_msg_debug ("glusterd", 0, "This node not being part of"
                        " volume should not be running gsyncd. Hence"
                        " no gsyncd process to restart.");
                ret = 0;
                goto out;
        }

        ret = glusterd_check_restart_gsync_session (param->volinfo, slave,
                                                    param->rsp_dict, path_list,
                                                    conf_path, 0);
        if (ret)
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_GSYNC_RESTART_FAIL,
                        "Unable to restart gsync session.");

out:
        gf_msg_debug ("glusterd", 0, "Returning %d.", ret);
        return ret;
}

/* op-sm */

int
glusterd_op_perform_add_bricks (glusterd_volinfo_t *volinfo, int32_t count,
                                char  *bricks, dict_t *dict)
{
        char                         *brick          = NULL;
        int32_t                       i              = 1;
        char                         *brick_list     = NULL;
        char                         *free_ptr1      = NULL;
        char                         *free_ptr2      = NULL;
        char                         *saveptr        = NULL;
        int32_t                       ret            = -1;
        int32_t                       stripe_count   = 0;
        int32_t                       replica_count  = 0;
        int32_t                       arbiter_count  = 0;
        int32_t                       type           = 0;
        glusterd_brickinfo_t         *brickinfo      = NULL;
        glusterd_gsync_status_temp_t  param          = {0, };
        gf_boolean_t                  restart_needed = 0;
        char                          msg[1024] __attribute__((unused)) = {0, };
        int                           caps           = 0;
        int                           brickid        = 0;
        char                          key[PATH_MAX]  = "";
        char                         *brick_mount_dir  = NULL;
        xlator_t                     *this           = NULL;
        glusterd_conf_t              *conf           = NULL;
        gf_boolean_t                  is_valid_add_brick = _gf_false;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (volinfo);

        conf = this->private;
        GF_ASSERT (conf);

        if (bricks) {
                brick_list = gf_strdup (bricks);
                free_ptr1 = brick_list;
        }

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);

        if (dict) {
                ret = dict_get_int32 (dict, "stripe-count", &stripe_count);
                if (!ret)
                        gf_msg (THIS->name, GF_LOG_INFO, errno,
                                GD_MSG_DICT_GET_SUCCESS,
                                "stripe-count is set %d", stripe_count);

                ret = dict_get_int32 (dict, "replica-count", &replica_count);
                if (!ret)
                        gf_msg (THIS->name, GF_LOG_INFO, errno,
                                GD_MSG_DICT_GET_SUCCESS,
                                "replica-count is set %d", replica_count);
                ret = dict_get_int32 (dict, "arbiter-count", &arbiter_count);
                if (!ret)
                        gf_msg (THIS->name, GF_LOG_INFO, errno,
                                GD_MSG_DICT_GET_SUCCESS,
                                "arbiter-count is set %d", arbiter_count);
                ret = dict_get_int32 (dict, "type", &type);
                if (!ret)
                        gf_msg (THIS->name, GF_LOG_INFO, errno,
                                GD_MSG_DICT_GET_SUCCESS,
                                "type is set %d, need to change it", type);
        }

        brickid = glusterd_get_next_available_brickid (volinfo);
        if (brickid < 0)
                goto out;
        while ( i <= count) {
                ret = glusterd_brickinfo_new_from_brick (brick, &brickinfo,
                                                         _gf_true, NULL);
                if (ret)
                        goto out;

                GLUSTERD_ASSIGN_BRICKID_TO_BRICKINFO (brickinfo, volinfo,
                                                      brickid++);

                /* A bricks mount dir is required only by snapshots which were
                 * introduced in gluster-3.6.0
                 */
                if (conf->op_version >= GD_OP_VERSION_3_6_0) {
                        brick_mount_dir = NULL;

                        snprintf (key, sizeof(key), "brick%d.mount_dir", i);
                        ret = dict_get_str (dict, key, &brick_mount_dir);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        GD_MSG_DICT_GET_FAILED,
                                        "%s not present", key);
                                goto out;
                        }
                        strncpy (brickinfo->mount_dir, brick_mount_dir,
                                 sizeof(brickinfo->mount_dir));
                }

                ret = glusterd_resolve_brick (brickinfo);
                if (ret)
                        goto out;

                /* hot tier bricks are added to head of brick list */
                if (dict_get (dict, "attach-tier")) {
                        cds_list_add (&brickinfo->brick_list, &volinfo->bricks);
                } else if (stripe_count || replica_count) {
                        add_brick_at_right_order (brickinfo, volinfo, (i - 1),
                                                  stripe_count, replica_count);
                } else {
                        cds_list_add_tail (&brickinfo->brick_list,
                                           &volinfo->bricks);
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
        if (arbiter_count) {
                volinfo->arbiter_count = arbiter_count;
        }
        if (stripe_count) {
                volinfo->stripe_count = stripe_count;
        }
        volinfo->dist_leaf_count = glusterd_get_dist_leaf_count (volinfo);

        /* backward compatibility */
        volinfo->sub_count = ((volinfo->dist_leaf_count == 1) ? 0:
                              volinfo->dist_leaf_count);

        volinfo->subvol_count = (volinfo->brick_count /
                                 volinfo->dist_leaf_count);

        ret = 0;
        if (GLUSTERD_STATUS_STARTED != volinfo->status)
                goto generate_volfiles;

        ret = generate_brick_volfiles (volinfo);
        if (ret)
                goto out;

        brick_list = gf_strdup (bricks);
        free_ptr2 = brick_list;
        i = 1;

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);
#ifdef HAVE_BD_XLATOR
        if (brickinfo->vg[0])
                caps = CAPS_BD | CAPS_THIN |
                        CAPS_OFFLOAD_COPY | CAPS_OFFLOAD_SNAPSHOT;
#endif

        /* This check needs to be added to distinguish between
         * attach-tier commands and add-brick commands.
         * When a tier is attached, adding is done via add-brick
         * and setting of pending xattrs shouldn't be done for
         * attach-tiers as they are virtually new volumes.
         */
        if (glusterd_is_volume_replicate (volinfo)) {
                if (replica_count &&
                    !dict_get (dict, "attach-tier") &&
                    conf->op_version >= GD_OP_VERSION_3_7_10) {
                        is_valid_add_brick = _gf_true;
                        ret = generate_dummy_client_volfiles (volinfo);
                        if (ret) {
                                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                        GD_MSG_VOLFILE_CREATE_FAIL,
                                        "Failed to create volfile.");
                                goto out;
                                }
                        }
        }

        while (i <= count) {
                ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                              &brickinfo,
                                                             _gf_true);
                if (ret)
                        goto out;
#ifdef HAVE_BD_XLATOR
                /* Check for VG/thin pool if its BD volume */
                if (brickinfo->vg[0]) {
                        ret = glusterd_is_valid_vg (brickinfo, 0, msg);
                        if (ret) {
                                gf_msg (THIS->name, GF_LOG_CRITICAL, 0,
                                        GD_MSG_INVALID_VG, "%s", msg);
                                goto out;
                        }
                        /* if anyone of the brick does not have thin support,
                           disable it for entire volume */
                        caps &= brickinfo->caps;
                } else
                        caps = 0;
#endif

                if (gf_uuid_is_null (brickinfo->uuid)) {
                        ret = glusterd_resolve_brick (brickinfo);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_RESOLVE_BRICK_FAIL,
                                        FMTSTR_RESOLVE_BRICK,
                                        brickinfo->hostname, brickinfo->path);
                                goto out;
                        }
                }

                /* if the volume is a replicate volume, do: */
                if (is_valid_add_brick) {
                        if (!gf_uuid_compare (brickinfo->uuid, MY_UUID)) {
                                ret = glusterd_handle_replicate_brick_ops (
                                                           volinfo, brickinfo,
                                                           GD_OP_ADD_BRICK);
                                if (ret < 0)
                                        goto out;
                        }
                }
                ret = glusterd_brick_start (volinfo, brickinfo,
                                            _gf_true);
                if (ret)
                        goto out;
                i++;
                brick = strtok_r (NULL, " \n", &saveptr);

                /* Check if the brick is added in this node, and set
                 * the restart_needed flag. */
                if ((!gf_uuid_compare (brickinfo->uuid, MY_UUID)) &&
                    !restart_needed) {
                        restart_needed = 1;
                        gf_msg_debug ("glusterd", 0,
                                "Restart gsyncd session, if it's already "
                                "running.");
                }
        }

        /* If the restart_needed flag is set, restart gsyncd sessions for that
         * particular master with all the slaves. */
        if (restart_needed) {
                param.rsp_dict = dict;
                param.volinfo = volinfo;
                dict_foreach (volinfo->gsync_slaves,
                              _glusterd_restart_gsync_session, &param);
        }
        volinfo->caps = caps;

generate_volfiles:
        if (conf->op_version <= GD_OP_VERSION_3_7_5) {
               ret = glusterd_create_volfiles_and_notify_services (volinfo);
        } else {
                /*
                 * The cluster is operating at version greater than
                 * gluster-3.7.5. So no need to sent volfile fetch
                 * request in commit phase, the same will be done
                 * in post validate phase with v3 framework.
                 */
        }

out:
        GF_FREE (free_ptr1);
        GF_FREE (free_ptr2);

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}


int
glusterd_op_perform_remove_brick (glusterd_volinfo_t  *volinfo, char *brick,
                                  int force, int *need_migrate)
{
        glusterd_brickinfo_t    *brickinfo = NULL;
        int32_t                  ret = -1;
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (brick);

        priv = THIS->private;
        GF_ASSERT (priv);

        ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                      &brickinfo,
                                                      _gf_false);
        if (ret)
                goto out;

        ret = glusterd_resolve_brick (brickinfo);
        if (ret)
                goto out;

        glusterd_volinfo_reset_defrag_stats (volinfo);

        if (!gf_uuid_compare (brickinfo->uuid, MY_UUID)) {
                /* Only if the brick is in this glusterd, do the rebalance */
                if (need_migrate)
                        *need_migrate = 1;
        }

        if (force) {
                ret = glusterd_brick_stop (volinfo, brickinfo,
                                           _gf_true);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_STOP_FAIL, "Unable to stop "
                                "glusterfs, ret: %d", ret);
                }
                goto out;
        }

        brickinfo->decommissioned = 1;
        ret = 0;
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int
glusterd_op_stage_add_brick (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        int                                     count = 0;
        int                                     replica_count = 0;
        int                                     arbiter_count = 0;
        int                                     i = 0;
        int32_t                                 local_brick_count = 0;
        char                                    *bricks    = NULL;
        char                                    *brick_list = NULL;
        char                                    *saveptr = NULL;
        char                                    *free_ptr = NULL;
        char                                    *brick = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        xlator_t                                *this = NULL;
        char                                    msg[2048] = {0,};
        char                                    key[PATH_MAX] = "";
        gf_boolean_t                            brick_alloc = _gf_false;
        char                                    *all_bricks = NULL;
        char                                    *str_ret = NULL;
        gf_boolean_t                            is_force = _gf_false;
        glusterd_conf_t                         *conf = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND,
                        "Unable to find volume: %s", volname);
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (ret) {
                gf_msg_debug (THIS->name, 0,
                        "Unable to get replica count");
        }

        ret = dict_get_int32 (dict, "arbiter-count", &arbiter_count);
        if (ret) {
                gf_msg_debug (THIS->name, 0,
                        "No arbiter count present in the dict");
        }

        if (replica_count > 0) {
                ret = op_version_check (this, GD_OP_VER_PERSISTENT_AFR_XATTRS,
                                        msg, sizeof(msg));
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_OP_VERSION_MISMATCH, "%s", msg);
                        *op_errstr = gf_strdup (msg);
                        goto out;
                }
        }

        if (glusterd_is_volume_replicate (volinfo)) {
                /* Do not allow add-brick for stopped volumes when replica-count
                 * is being increased.
                 */
                if (conf->op_version >= GD_OP_VERSION_3_7_10 &&
                    !dict_get (dict, "attach-tier") &&
                    replica_count &&
                    GLUSTERD_STATUS_STOPPED == volinfo->status) {
                        ret = -1;
                        snprintf (msg, sizeof (msg), " Volume must not be in"
                                  " stopped state when replica-count needs to "
                                  " be increased.");
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_ADD_FAIL, "%s", msg);
                        *op_errstr = gf_strdup (msg);
                        goto out;
                }
                /* op-version check for replica 2 to arbiter conversion. If we
                * dont have this check, an older peer added as arbiter brick
                * will not have the  arbiter xlator in its volfile. */
                if ((conf->op_version < GD_OP_VERSION_3_8_0) &&
                    (arbiter_count == 1) && (replica_count == 3)) {
                        ret = -1;
                        snprintf (msg, sizeof (msg), "Cluster op-version must "
                                  "be >= 30800 to add arbiter brick to a "
                                  "replica 2 volume.");
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_ADD_FAIL, "%s", msg);
                        *op_errstr = gf_strdup (msg);
                        goto out;
                }
        }

        is_force = dict_get_str_boolean (dict, "force", _gf_false);

        if (volinfo->replica_count < replica_count && !is_force) {
                cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                         brick_list) {
                        if (gf_uuid_compare (brickinfo->uuid, MY_UUID))
                                continue;
                        if (brickinfo->status == GF_BRICK_STOPPED) {
                                ret = -1;
                                snprintf (msg, sizeof (msg), "Brick %s is down,"
                                          " changing replica count needs all "
                                          "the bricks to be up to avoid data "
                                          "loss", brickinfo->path);
                                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                        GD_MSG_BRICK_ADD_FAIL, "%s", msg);
                                *op_errstr = gf_strdup (msg);
                                goto out;
                        }
                }
        }

        if (conf->op_version > GD_OP_VERSION_3_7_5 &&
            is_origin_glusterd (dict)) {
                ret = glusterd_validate_quorum (this, GD_OP_ADD_BRICK, dict,
                                                op_errstr);
                if (ret) {
                        gf_msg (this->name, GF_LOG_CRITICAL, 0,
                                GD_MSG_SERVER_QUORUM_NOT_MET,
                                "Server quorum not met. Rejecting operation.");
                        goto out;
                }
        } else {
                /* Case 1: conf->op_version <= GD_OP_VERSION_3_7_5
                 *         in this case the add-brick is running
                 *         syncop framework that will do a quorum
                 *         check by default
                 * Case 2: We don't need to do quorum check on every
                 *         node, only originator glusterd need to
                 *         check for quorum
                 * So nothing need to be done in else
                 */
        }

        if (glusterd_is_defrag_on(volinfo)) {
                snprintf (msg, sizeof(msg), "Volume name %s rebalance is in "
                          "progress. Please retry after completion", volname);
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_OIP_RETRY_LATER, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        if (dict_get(dict, "attach-tier")) {

                /*
                 * This check is needed because of add/remove brick
                 * is not supported on a tiered volume. So once a tier
                 * is attached we cannot commit or stop the remove-brick
                 * task. Please change this comment once we start supporting
                 * add/remove brick on a tiered volume.
                 */
                if (!gd_is_remove_brick_committed (volinfo)) {

                        snprintf (msg, sizeof (msg), "An earlier remove-brick "
                                  "task exists for volume %s. Either commit it"
                                  " or stop it before attaching a tier.",
                                  volinfo->volname);
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_OLD_REMOVE_BRICK_EXISTS, "%s", msg);
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get count");
                goto out;
        }

        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get bricks");
                goto out;
        }

        if (bricks) {
                brick_list = gf_strdup (bricks);
                all_bricks = gf_strdup (bricks);
                free_ptr = brick_list;
        }

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);


        while ( i < count) {
                if (!glusterd_store_is_valid_brickpath (volname, brick) ||
                    !glusterd_is_valid_volfpath (volname, brick)) {
                        snprintf (msg, sizeof (msg), "brick path %s is "
                                  "too long", brick);
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRKPATH_TOO_LONG, "%s", msg);
                        *op_errstr = gf_strdup (msg);

                        ret = -1;
                        goto out;

                }

                ret = glusterd_brickinfo_new_from_brick (brick, &brickinfo,
                                                         _gf_true, NULL);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_NOT_FOUND,
                                "Add-brick: Unable"
                                " to get brickinfo");
                        goto out;
                }
                brick_alloc = _gf_true;

                ret = glusterd_new_brick_validate (brick, brickinfo, msg,
                                                   sizeof (msg), NULL);
                if (ret) {
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }

                if (!gf_uuid_compare (brickinfo->uuid, MY_UUID)) {
#ifdef HAVE_BD_XLATOR
                        if (brickinfo->vg[0]) {
                                ret = glusterd_is_valid_vg (brickinfo, 1, msg);
                                if (ret) {
                                        gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                                               GD_MSG_INVALID_VG, "%s",
                                                msg);
                                        *op_errstr = gf_strdup (msg);
                                        goto out;
                                }
                        }
#endif

                        ret = glusterd_validate_and_create_brickpath (brickinfo,
                                                          volinfo->volume_id,
                                                          op_errstr, is_force);
                        if (ret)
                                goto out;

                        /* A bricks mount dir is required only by snapshots which were
                         * introduced in gluster-3.6.0
                         */
                        if (conf->op_version >= GD_OP_VERSION_3_6_0) {
                                ret = glusterd_get_brick_mount_dir
                                        (brickinfo->path, brickinfo->hostname,
                                         brickinfo->mount_dir);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_BRICK_MOUNTDIR_GET_FAIL,
                                                "Failed to get brick mount_dir");
                                        goto out;
                                }

                                snprintf (key, sizeof(key), "brick%d.mount_dir",
                                          i + 1);
                                ret = dict_set_dynstr_with_alloc
                                        (rsp_dict, key, brickinfo->mount_dir);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                                GD_MSG_DICT_SET_FAILED,
                                                "Failed to set %s", key);
                                        goto out;
                                }
                        }

                        local_brick_count = i + 1;
                }

                glusterd_brickinfo_delete (brickinfo);
                brick_alloc = _gf_false;
                brickinfo = NULL;
                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
        }

        ret = dict_set_int32 (rsp_dict, "brick_count",
                              local_brick_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set local_brick_count");
                goto out;
        }

out:
        GF_FREE (free_ptr);
        if (brick_alloc && brickinfo)
                glusterd_brickinfo_delete (brickinfo);
        GF_FREE (str_ret);
        GF_FREE (all_bricks);

        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}

int
glusterd_remove_brick_validate_bricks (gf1_op_commands cmd, int32_t brick_count,
                                       dict_t *dict,
                                       glusterd_volinfo_t *volinfo,
                                       char **errstr,
                                       gf_cli_defrag_type cmd_defrag)
{
        char                   *brick       = NULL;
        char                    msg[2048]   = {0,};
        char                    key[256]    = {0,};
        glusterd_brickinfo_t   *brickinfo   = NULL;
        glusterd_peerinfo_t    *peerinfo    = NULL;
        int                     i           = 0;
        int                     ret         = -1;
        char                    pidfile[PATH_MAX+1] = {0,};
        glusterd_conf_t        *priv        = THIS->private;

        /* Check whether all the nodes of the bricks to be removed are
        * up, if not fail the operation */
        for (i = 1; i <= brick_count; i++) {
                snprintf (key, sizeof (key), "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        snprintf (msg, sizeof (msg),
                                  "Unable to get %s", key);
                        *errstr = gf_strdup (msg);
                        goto out;
                }

                ret =
                glusterd_volume_brickinfo_get_by_brick(brick, volinfo,
                                                       &brickinfo,
                                                       _gf_false);
                if (ret) {
                        snprintf (msg, sizeof (msg), "Incorrect brick "
                                  "%s for volume %s", brick, volinfo->volname);
                        *errstr = gf_strdup (msg);
                        goto out;
                }
                /* Do not allow commit if the bricks are not decommissioned
                 * if its a remove brick commit or detach-tier commit
                 */
                if (!brickinfo->decommissioned) {
                        if (cmd == GF_OP_CMD_COMMIT) {
                                snprintf (msg, sizeof (msg), "Brick %s "
                                          "is not decommissioned. "
                                          "Use start or force option", brick);
                                *errstr = gf_strdup (msg);
                                ret = -1;
                                goto out;
                        }

                        if (cmd == GF_OP_CMD_DETACH_COMMIT ||
                            cmd_defrag == GF_DEFRAG_CMD_DETACH_COMMIT) {
                                snprintf (msg, sizeof (msg), "Bricks in Hot "
                                          "tier are not decommissioned yet. Use "
                                          "gluster volume tier <VOLNAME> "
                                          "detach start to start the decommission process");
                                *errstr = gf_strdup (msg);
                                ret = -1;
                                goto out;
                        }
                } else {
                        if ((cmd == GF_OP_CMD_DETACH_COMMIT ||
                            (cmd_defrag == GF_DEFRAG_CMD_DETACH_COMMIT)) &&
                            (volinfo->rebal.defrag_status == GF_DEFRAG_STATUS_STARTED)) {
                                        snprintf (msg, sizeof (msg), "Bricks in Hot "
                                                  "tier are not decommissioned yet. Wait for "
                                                  "the detach to complete using gluster volume "
                                                  "tier <VOLNAME> status.");
                                        *errstr = gf_strdup (msg);
                                        ret = -1;
                                        goto out;
                            }
                }

                if (glusterd_is_local_brick (THIS, volinfo, brickinfo)) {
                        switch (cmd) {
                        case GF_OP_CMD_START:
                        case GF_OP_CMD_DETACH_START:
                                goto check;
                        case GF_OP_CMD_NONE:
                        default:
                                break;
                        }

                        switch (cmd_defrag) {
                        case GF_DEFRAG_CMD_DETACH_START:
                                break;
                        case GF_DEFRAG_CMD_NONE:
                        default:
                                continue;
                        }
check:
                        if (brickinfo->status != GF_BRICK_STARTED) {
                                snprintf (msg, sizeof (msg), "Found stopped "
                                          "brick %s", brick);
                                *errstr = gf_strdup (msg);
                                ret = -1;
                                goto out;
                        }
                        GLUSTERD_GET_BRICK_PIDFILE (pidfile, volinfo,
                                                    brickinfo, priv);
                        if (!gf_is_service_running (pidfile, NULL)) {
                                snprintf (msg, sizeof (msg), "Found dead "
                                          "brick %s", brick);
                                *errstr = gf_strdup (msg);
                                ret = -1;
                                goto out;
                        }
                        continue;
                }

                rcu_read_lock ();
                peerinfo = glusterd_peerinfo_find_by_uuid
                                                (brickinfo->uuid);
                if (!peerinfo) {
                        snprintf (msg, sizeof(msg), "Host node of the "
                                  "brick %s is not in cluster", brick);
                        *errstr = gf_strdup (msg);
                        ret = -1;
                        rcu_read_unlock ();
                        goto out;
                }
                if (!peerinfo->connected) {
                        snprintf (msg, sizeof(msg), "Host node of the "
                                  "brick %s is down", brick);
                        *errstr = gf_strdup (msg);
                        ret = -1;
                        rcu_read_unlock ();
                        goto out;
                }
                rcu_read_unlock ();
        }

out:
        return ret;
}

int
glusterd_op_stage_remove_brick (dict_t *dict, char **op_errstr)
{
        int                     ret         = -1;
        char                   *volname     = NULL;
        glusterd_volinfo_t     *volinfo     = NULL;
        char                   *errstr      = NULL;
        int32_t                 brick_count = 0;
        char                    msg[2048]   = {0,};
        int32_t                 flag        = 0;
        gf1_op_commands         cmd         = GF_OP_CMD_NONE;
        char                   *task_id_str = NULL;
        xlator_t               *this        = NULL;
        gsync_status_param_t    param       = {0,};

        this = THIS;
        GF_ASSERT (this);

        ret = op_version_check (this, GD_OP_VER_PERSISTENT_AFR_XATTRS,
                                msg, sizeof(msg));
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_VERSION_MISMATCH, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, "Volume %s does not exist", volname);
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "command", &flag);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get brick command");
                goto out;
        }
        cmd = flag;

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get brick count");
                goto out;
        }

        ret = 0;
        if (volinfo->brick_count == brick_count) {
                errstr = gf_strdup ("Deleting all the bricks of the "
                                    "volume is not allowed");
                ret = -1;
                goto out;
        }

        ret = -1;
        switch (cmd) {
        case GF_OP_CMD_NONE:
                errstr = gf_strdup ("no remove-brick command issued");
                goto out;

        case GF_OP_CMD_STATUS:
                ret = 0;
                goto out;

        case GF_OP_CMD_DETACH_START:
                if (volinfo->type != GF_CLUSTER_TYPE_TIER) {
                        snprintf (msg, sizeof(msg), "volume %s is not a tier "
                                  "volume", volinfo->volname);
                        errstr = gf_strdup (msg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOL_NOT_TIER, "%s", errstr);
                        goto out;
                }

        case GF_OP_CMD_START:
        {
                if ((volinfo->type == GF_CLUSTER_TYPE_REPLICATE) &&
                    dict_get (dict, "replica-count")) {
                        snprintf (msg, sizeof(msg), "Migration of data is not "
                                  "needed when reducing replica count. Use the"
                                  " 'force' option");
                        errstr = gf_strdup (msg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_USE_THE_FORCE, "%s", errstr);
                        goto out;
                }

                if (GLUSTERD_STATUS_STARTED != volinfo->status) {
                        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                                snprintf (msg, sizeof (msg), "Volume %s needs "
                                          "to be started before detach-tier "
                                          "(you can use 'force' or 'commit' "
                                          "to override this behavior)",
                                          volinfo->volname);
                        } else {
                                snprintf (msg, sizeof (msg), "Volume %s needs "
                                          "to be started before remove-brick "
                                          "(you can use 'force' or 'commit' "
                                          "to override this behavior)",
                                          volinfo->volname);
                        }
                        errstr = gf_strdup (msg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOL_NOT_STARTED, "%s", errstr);
                        goto out;
                }
                if (!gd_is_remove_brick_committed (volinfo)) {
                        snprintf (msg, sizeof (msg), "An earlier remove-brick "
                                  "task exists for volume %s. Either commit it"
                                  " or stop it before starting a new task.",
                                  volinfo->volname);
                        errstr = gf_strdup (msg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_OLD_REMOVE_BRICK_EXISTS, "Earlier remove-brick"
                                " task exists for volume %s.",
                                volinfo->volname);
                        goto out;
                }
                if (glusterd_is_defrag_on(volinfo)) {
                        errstr = gf_strdup("Rebalance is in progress. Please "
                                           "retry after completion");
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_OIP_RETRY_LATER, "%s", errstr);
                        goto out;
                }

                /* Check if the connected clients are all of version
                 * glusterfs-3.6 and higher. This is needed to prevent some data
                 * loss issues that could occur when older clients are connected
                 * when rebalance is run.
                 */
                ret = glusterd_check_client_op_version_support
                        (volname, GD_OP_VERSION_3_6_0, NULL);
                if (ret) {
                        ret = gf_asprintf (op_errstr, "Volume %s has one or "
                                           "more connected clients of a version"
                                           " lower than GlusterFS-v3.6.0. "
                                           "Starting remove-brick in this state "
                                           "could lead to data loss.\nPlease "
                                           "disconnect those clients before "
                                           "attempting this command again.",
                                           volname);
                        goto out;
                }

                ret = glusterd_remove_brick_validate_bricks (cmd, brick_count,
                                                             dict, volinfo,
                                                             &errstr,
                                                             GF_DEFRAG_CMD_NONE);
                if (ret)
                        goto out;

                if (is_origin_glusterd (dict)) {
                        ret = glusterd_generate_and_set_task_id
                                (dict, GF_REMOVE_BRICK_TID_KEY);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_TASKID_GEN_FAIL,
                                        "Failed to generate task-id");
                                goto out;
                        }
                } else {
                        ret = dict_get_str (dict, GF_REMOVE_BRICK_TID_KEY,
                                            &task_id_str);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_WARNING, errno,
                                        GD_MSG_DICT_GET_FAILED,
                                        "Missing remove-brick-id");
                                ret = 0;
                        }
                }
                break;
        }

        case GF_OP_CMD_STOP:
        case GF_OP_CMD_STOP_DETACH_TIER:
                ret = 0;
                break;

        case GF_OP_CMD_DETACH_COMMIT:
                if (volinfo->type != GF_CLUSTER_TYPE_TIER) {
                        snprintf (msg, sizeof(msg), "volume %s is not a tier "
                                  "volume", volinfo->volname);
                        errstr = gf_strdup (msg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOL_NOT_TIER, "%s", errstr);
                        goto out;
                }
                if (volinfo->decommission_in_progress) {
                        errstr = gf_strdup ("use 'force' option as migration "
                                            "is in progress");
                        goto out;
                }
                if (volinfo->rebal.defrag_status == GF_DEFRAG_STATUS_FAILED) {
                        errstr = gf_strdup ("use 'force' option as migration "
                                            "has failed");
                        goto out;
                }

                ret = glusterd_remove_brick_validate_bricks (cmd, brick_count,
                                                             dict, volinfo,
                                                             &errstr,
                                                             GF_DEFRAG_CMD_NONE);
                if (ret)
                        goto out;

                /* If geo-rep is configured, for this volume, it should be
                 * stopped.
                 */
                param.volinfo = volinfo;
                ret = glusterd_check_geo_rep_running (&param, op_errstr);
                if (ret || param.is_active) {
                        ret = -1;
                        goto out;
                }
                break;

        case GF_OP_CMD_COMMIT:
                if (volinfo->decommission_in_progress) {
                        errstr = gf_strdup ("use 'force' option as migration "
                                            "is in progress");
                        goto out;
                }

                if (volinfo->rebal.defrag_status == GF_DEFRAG_STATUS_FAILED) {
                        errstr = gf_strdup ("use 'force' option as migration "
                                            "has failed");
                        goto out;
                }

                ret = glusterd_remove_brick_validate_bricks (cmd, brick_count,
                                                             dict, volinfo,
                                                             &errstr,
                                                             GF_DEFRAG_CMD_NONE);
                if (ret)
                        goto out;

                /* If geo-rep is configured, for this volume, it should be
                 * stopped.
                 */
                param.volinfo = volinfo;
                ret = glusterd_check_geo_rep_running (&param, op_errstr);
                if (ret || param.is_active) {
                        ret = -1;
                        goto out;
                }

                break;

        case GF_OP_CMD_DETACH_COMMIT_FORCE:
                if (volinfo->type != GF_CLUSTER_TYPE_TIER) {
                        snprintf (msg, sizeof(msg), "volume %s is not a tier "
                                  "volume", volinfo->volname);
                        errstr = gf_strdup (msg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOL_NOT_TIER, "%s", errstr);
                        goto out;
                }
        case GF_OP_CMD_COMMIT_FORCE:
                break;
        }
        ret = 0;

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
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
                cds_list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks,
                                              brick_list) {
                        if (!brickinfo->decommissioned)
                                continue;
                        brickinfo->decommissioned = 0;
                }
                break;

        case GF_DEFRAG_STATUS_COMPLETE:
                /* Done with the task, you can remove the brick from the
                   volume file */
                cds_list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks,
                                              brick_list) {
                        if (!brickinfo->decommissioned)
                                continue;
                        gf_log (THIS->name, GF_LOG_INFO, "removing the brick %s",
                                brickinfo->path);
                        brickinfo->decommissioned = 0;
                        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                            /*TODO: use the 'atomic' flavour of brick_stop*/
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

static int
glusterd_op_perform_attach_tier (dict_t *dict,
                                 glusterd_volinfo_t *volinfo,
                                 int count,
                                 char *bricks)
{
        int                                     ret = 0;
        int                                     replica_count = 0;
        int                                     type = 0;

        /*
         * Store the new (cold) tier's structure until the graph is generated.
         * If there is a failure before the graph is generated the
         * structure will revert to its original state.
         */
        volinfo->tier_info.cold_dist_leaf_count = volinfo->dist_leaf_count;
        volinfo->tier_info.cold_type           = volinfo->type;
        volinfo->tier_info.cold_brick_count    = volinfo->brick_count;
        volinfo->tier_info.cold_replica_count  = volinfo->replica_count;
        volinfo->tier_info.cold_disperse_count = volinfo->disperse_count;
        volinfo->tier_info.cold_redundancy_count = volinfo->redundancy_count;

        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (!ret)
                volinfo->tier_info.hot_replica_count  = replica_count;
        else
                volinfo->tier_info.hot_replica_count  = 1;
        volinfo->tier_info.hot_brick_count     = count;
        ret = dict_get_int32 (dict, "hot-type", &type);
        volinfo->tier_info.hot_type      = type;
        ret = dict_set_int32 (dict, "type", GF_CLUSTER_TYPE_TIER);

        if (!ret)
                ret = dict_set_str (volinfo->dict, "features.ctr-enabled", "on");

        if (!ret)
                ret = dict_set_str (volinfo->dict, "cluster.tier-mode", "cache");

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
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, EINVAL,
                        GD_MSG_VOL_NOT_FOUND, "Unable to allocate memory");
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get count");
                goto out;
        }


        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get bricks");
                goto out;
        }

        if (dict_get(dict, "attach-tier")) {
                gf_msg_debug (THIS->name, 0, "Adding tier");
                glusterd_op_perform_attach_tier (dict, volinfo, count, bricks);
        }

        ret = glusterd_op_perform_add_bricks (volinfo, count, bricks, dict);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_ADD_FAIL, "Unable to add bricks");
                goto out;
        }
        if (priv->op_version <= GD_OP_VERSION_3_7_5) {
               ret = glusterd_store_volinfo (volinfo,
                                             GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret)
                        goto out;
        } else {
                 /*
                 * The cluster is operating at version greater than
                 * gluster-3.7.5. So no need to store volfiles
                 * in commit phase, the same will be done
                 * in post validate phase with v3 framework.
                 */
        }

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_svcs_manager (volinfo);

out:
        return ret;
}

void
glusterd_op_perform_detach_tier (glusterd_volinfo_t *volinfo)
{
        volinfo->type             = volinfo->tier_info.cold_type;
        volinfo->replica_count    = volinfo->tier_info.cold_replica_count;
        volinfo->disperse_count   = volinfo->tier_info.cold_disperse_count;
        volinfo->redundancy_count = volinfo->tier_info.cold_redundancy_count;
        volinfo->dist_leaf_count  = volinfo->tier_info.cold_dist_leaf_count;
}

int
glusterd_op_remove_brick (dict_t *dict, char **op_errstr)
{
        int                     ret            = -1;
        char                    *volname       = NULL;
        glusterd_volinfo_t      *volinfo       = NULL;
        char                    *brick         = NULL;
        int32_t                 count          = 0;
        int32_t                 i              = 1;
        char                    key[256]       = {0,};
        int32_t                 flag           = 0;
        char                    err_str[4096]  = {0,};
        int                     need_rebalance = 0;
        int                     force          = 0;
        gf1_op_commands         cmd            = 0;
        int32_t                 replica_count  = 0;
        glusterd_brickinfo_t    *brickinfo     = NULL;
        glusterd_brickinfo_t    *tmp           = NULL;
        char                    *task_id_str   = NULL;
        xlator_t                *this          = NULL;
        dict_t                  *bricks_dict   = NULL;
        char                    *brick_tmpstr  = NULL;
        int                      start_remove  = 0;
        uint32_t                 commit_hash   = 0;
        int                      defrag_cmd    = 0;
        int                      detach_commit = 0;
        void                    *tier_info     = NULL;
        char                    *cold_shd_key  = NULL;
        char                    *hot_shd_key   = NULL;
        int                      delete_key    = 1;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_ADD_FAIL, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_VOL_NOT_FOUND, "Unable to allocate memory");
                goto out;
        }

        ret = dict_get_int32 (dict, "command", &flag);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get command");
                goto out;
        }
        cmd = flag;

        if ((GF_OP_CMD_START == cmd) ||
            (GF_OP_CMD_DETACH_START == cmd))
                start_remove = 1;

        /* Set task-id, if available, in ctx dict for operations other than
         * start
         */

        if (is_origin_glusterd (dict) && (!start_remove)) {
                if (!gf_uuid_is_null (volinfo->rebal.rebalance_id)) {
                        ret = glusterd_copy_uuid_to_dict
                                (volinfo->rebal.rebalance_id, dict,
                                 GF_REMOVE_BRICK_TID_KEY);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_REMOVE_BRICK_ID_SET_FAIL,
                                        "Failed to set remove-brick-id");
                                goto out;
                        }
                }
        }

        /* Clear task-id, rebal.op and stored bricks on commmitting/stopping
         * remove-brick */
        if ((!start_remove) && (cmd != GF_OP_CMD_STATUS)) {
                gf_uuid_clear (volinfo->rebal.rebalance_id);
                volinfo->rebal.op = GD_OP_NONE;
                dict_unref (volinfo->rebal.dict);
                volinfo->rebal.dict = NULL;
        }

        ret = -1;
        switch (cmd) {
        case GF_OP_CMD_NONE:
                goto out;

        case GF_OP_CMD_STATUS:
                ret = 0;
                goto out;

        case GF_OP_CMD_STOP:
        case GF_OP_CMD_STOP_DETACH_TIER:
        {
                /* Fall back to the old volume file */
                cds_list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks,
                                              brick_list) {
                        if (!brickinfo->decommissioned)
                                continue;
                        brickinfo->decommissioned = 0;
                }
                ret = glusterd_create_volfiles_and_notify_services (volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_VOLFILE_CREATE_FAIL,
                                "failed to create volfiles");
                        goto out;
                }

                ret = glusterd_store_volinfo (volinfo,
                                             GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_VOLINFO_SET_FAIL,
                                "failed to store volinfo");
                        goto out;
                }

                ret = 0;
                goto out;
        }

        case GF_OP_CMD_DETACH_START:
        case GF_OP_CMD_START:
                /* Reset defrag status to 'NOT STARTED' whenever a
                 * remove-brick/rebalance command is issued to remove
                 * stale information from previous run.
                 * Update defrag_cmd as well or it will only be done
                 * for nodes on which the brick to be removed exists.
                 */
                volinfo->rebal.defrag_cmd = cmd;
                volinfo->rebal.defrag_status = GF_DEFRAG_STATUS_NOT_STARTED;
                ret = dict_get_str (dict, GF_REMOVE_BRICK_TID_KEY, &task_id_str);
                if (ret) {
                        gf_msg_debug (this->name, errno,
                                "Missing remove-brick-id");
                        ret = 0;
                } else {
                        gf_uuid_parse (task_id_str, volinfo->rebal.rebalance_id) ;
                        volinfo->rebal.op = GD_OP_REMOVE_BRICK;
                }
                force = 0;
                break;

        case GF_OP_CMD_COMMIT:
                force = 1;
                break;

        case GF_OP_CMD_DETACH_COMMIT:
        case GF_OP_CMD_DETACH_COMMIT_FORCE:
                glusterd_op_perform_detach_tier (volinfo);
                detach_commit = 1;

                /* Disabling ctr when detaching a tier, since
                 * currently tier is the only consumer of ctr.
                 * Revisit this code when this constraint no
                 * longer exist.
                 */
                dict_del (volinfo->dict, "features.ctr-enabled");
                dict_del (volinfo->dict, "cluster.tier-mode");

                hot_shd_key = gd_get_shd_key (volinfo->tier_info.hot_type);
                cold_shd_key = gd_get_shd_key (volinfo->tier_info.cold_type);
                if (hot_shd_key) {
                        /*
                         * Since post detach, shd graph will not contain hot
                         * tier. So we need to clear option set for hot tier.
                         * For a tiered volume there can be different key
                         * for both hot and cold. If hot tier is shd compatible
                         * then we need to remove the configured value when
                         * detaching a tier, only if the key's are different or
                         * cold key is NULL. So we will set delete_key first,
                         * and if cold key is not null and they are equal then
                         * we will clear the flag. Otherwise we will delete the
                         * key.
                         */
                        if (cold_shd_key)
                                delete_key = strcmp (hot_shd_key, cold_shd_key);
                        if (delete_key)
                                dict_del (volinfo->dict, hot_shd_key);
               }
                /* fall through */

        case GF_OP_CMD_COMMIT_FORCE:

                if (volinfo->decommission_in_progress) {
                        if (volinfo->rebal.defrag) {
                                LOCK (&volinfo->rebal.defrag->lock);
                                /* Fake 'rebalance-complete' so the graph change
                                   happens right away */
                                volinfo->rebal.defrag_status =
                                                GF_DEFRAG_STATUS_COMPLETE;

                                UNLOCK (&volinfo->rebal.defrag->lock);
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
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get count");
                goto out;
        }

        if (volinfo->type == GF_CLUSTER_TYPE_TIER)
                count = glusterd_set_detach_bricks(dict, volinfo);

        /* Save the list of bricks for later usage only on starting a
         * remove-brick. Right now this is required for displaying the task
         * parameters with task status in volume status.
         */

        if (start_remove) {
                bricks_dict = dict_new ();
                if (!bricks_dict) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_int32 (bricks_dict, "count", count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to save remove-brick count");
                        goto out;
                }
        }

        while ( i <= count) {
                snprintf (key, 256, "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_GET_FAILED, "Unable to get %s",
                                key);
                        goto out;
                }

                if (start_remove) {
                        brick_tmpstr = gf_strdup (brick);
                        if (!brick_tmpstr) {
                                ret = -1;
                                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                        GD_MSG_NO_MEMORY,
                                        "Failed to duplicate brick name");
                                goto out;
                        }
                        ret = dict_set_dynstr (bricks_dict, key, brick_tmpstr);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        GD_MSG_DICT_SET_FAILED,
                                        "Failed to add brick to dict");
                                goto out;
                        }
                        brick_tmpstr = NULL;
                }

                ret = glusterd_op_perform_remove_brick (volinfo, brick, force,
                                                        &need_rebalance);
                if (ret)
                        goto out;
                i++;
        }

        if (detach_commit) {
                 /* Clear related information from volinfo */
                tier_info = ((void *)(&volinfo->tier_info));
                memset (tier_info, 0, sizeof (volinfo->tier_info));
        }

        if (start_remove)
                volinfo->rebal.dict = dict_ref (bricks_dict);

        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (!ret) {
                gf_msg (this->name, GF_LOG_INFO, errno,
                        GD_MSG_DICT_GET_FAILED,
                        "changing replica count %d to %d on volume %s",
                        volinfo->replica_count, replica_count,
                        volinfo->volname);
                volinfo->replica_count = replica_count;
                /* A reduction in replica count implies an arbiter volume
                 * earlier is now no longer one. */
                if (volinfo->arbiter_count)
                        volinfo->arbiter_count = 0;
                volinfo->sub_count = replica_count;
                volinfo->dist_leaf_count = glusterd_get_dist_leaf_count (volinfo);

                /*
                 * volinfo->type and sub_count have already been set for
                 * volumes undergoing a detach operation, they should not
                 * be modified here.
                 */
                if ((replica_count == 1) && (cmd != GF_OP_CMD_DETACH_COMMIT) &&
                    (cmd != GF_OP_CMD_DETACH_COMMIT_FORCE)) {
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
        volinfo->subvol_count = (volinfo->brick_count /
                                 volinfo->dist_leaf_count);

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL, "failed to create volfiles");
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_VOLINFO_STORE_FAIL, "failed to store volinfo");
                goto out;
        }

        if (start_remove &&
            volinfo->status == GLUSTERD_STATUS_STARTED) {
                ret = glusterd_svcs_reconfigure ();
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_NFS_RECONF_FAIL,
                               "Unable to reconfigure NFS-Server");
                goto out;
                }
        }

        /* Need to reset the defrag/rebalance status accordingly */
        switch (volinfo->rebal.defrag_status) {
        case GF_DEFRAG_STATUS_FAILED:
        case GF_DEFRAG_STATUS_COMPLETE:
                volinfo->rebal.defrag_status = 0;
        default:
                break;
        }
        if (!force && need_rebalance) {
                if (dict_get_uint32(dict, "commit-hash", &commit_hash) == 0) {
                        volinfo->rebal.commit_hash = commit_hash;
                }
                /* perform the rebalance operations */
                defrag_cmd = GF_DEFRAG_CMD_START_FORCE;
                if (cmd == GF_OP_CMD_DETACH_START)
                        defrag_cmd = GF_DEFRAG_CMD_START_DETACH_TIER;
                /*
                 * We need to set this *before* we issue commands to the
                 * bricks, or else we might end up setting it after the bricks
                 * have responded.  If we fail to send the request(s) we'll
                 * clear it ourselves because nobody else will.
                 */
                volinfo->decommission_in_progress = 1;
                ret = glusterd_handle_defrag_start
                        (volinfo, err_str, sizeof (err_str),
                         defrag_cmd,
                         glusterd_remove_brick_migrate_cbk, GD_OP_REMOVE_BRICK);

                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_REBALANCE_START_FAIL,
                                "failed to start the rebalance");
                        /* TBD: shouldn't we do more than print a message? */
                        volinfo->decommission_in_progress = 0;
                }
        } else {
                if (GLUSTERD_STATUS_STARTED == volinfo->status)
                        ret = glusterd_svcs_manager (volinfo);
        }
out:
       if (ret && err_str[0] && op_errstr)
                *op_errstr = gf_strdup (err_str);

        GF_FREE (brick_tmpstr);
        if (bricks_dict)
                dict_unref (bricks_dict);

        return ret;
}

int
glusterd_op_stage_barrier (dict_t *dict, char **op_errstr)
{
        int                  ret         = -1;
        xlator_t             *this       = NULL;
        char                 *volname    = NULL;
        glusterd_volinfo_t   *vol        = NULL;

        GF_ASSERT (dict);
        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Volname not present in "
                        "dict");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &vol);
        if (ret) {
                gf_asprintf (op_errstr, "Volume %s does not exist", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, "%s", *op_errstr);
                goto out;
        }

        if (!glusterd_is_volume_started (vol)) {
                gf_asprintf (op_errstr, "Volume %s is not started", volname);
                ret = -1;
                goto out;
        }

        ret = dict_get_str_boolean (dict, "barrier", -1);
        if (ret == -1) {
                gf_asprintf (op_errstr, "Barrier op for volume %s not present "
                             "in dict", volname);
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "%s", *op_errstr);
                goto out;
        }
        ret = 0;
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_op_barrier (dict_t *dict, char **op_errstr)
{
        int                  ret         = -1;
        xlator_t             *this       = NULL;
        char                 *volname    = NULL;
        glusterd_volinfo_t   *vol        = NULL;
        char                 *barrier_op = NULL;

        GF_ASSERT (dict);
        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Volname not present in "
                        "dict");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &vol);
        if (ret) {
                gf_asprintf (op_errstr, "Volume %s does not exist", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, "%s", *op_errstr);
                goto out;
        }

        ret = dict_get_str (dict, "barrier", &barrier_op);
        if (ret) {
                gf_asprintf (op_errstr, "Barrier op for volume %s not present "
                             "in dict", volname);
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "%s", *op_errstr);
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (vol->dict, "features.barrier",
                                          barrier_op);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "Failed to set barrier op in"
                        " volume option dict");
                goto out;
        }

        gd_update_volume_op_versions (vol);
        ret = glusterd_create_volfiles (vol);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL, "Failed to create volfiles");
                goto out;
        }
        ret = glusterd_store_volinfo (vol, GLUSTERD_VOLINFO_VER_AC_INCREMENT);

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_handle_attach_tier (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_add_brick);
}

int
glusterd_handle_detach_tier (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_remove_brick);
}
