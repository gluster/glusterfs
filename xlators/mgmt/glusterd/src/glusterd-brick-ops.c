/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include "common-utils.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "run.h"
#include <sys/signal.h>

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
                        *type = GF_CLUSTER_TYPE_STRIPE;
                        gf_log (THIS->name, GF_LOG_INFO,
                                "Changing the type of volume %s from "
                                "'distribute' to 'stripe'", volinfo->volname);
                        ret = 0;
                        goto out;
                } else {
                        snprintf (err_str, err_len, "Incorrect number of "
                                  "bricks (%d) supplied for stripe count (%d).",
                                  (total_bricks - volinfo->brick_count),
                                  stripe_count);
                        gf_log (THIS->name, GF_LOG_ERROR, "%s", err_str);
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
                } else {
                        snprintf (err_str, err_len, "Incorrect number of "
                                  "bricks (%d) supplied for changing volume's "
                                  "stripe count to %d, need at least %d bricks",
                                  (total_bricks - volinfo->brick_count),
                                  stripe_count,
                                  (volinfo->replica_count * stripe_count));
                        gf_log (THIS->name, GF_LOG_ERROR, "%s", err_str);
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
                        gf_log (THIS->name, GF_LOG_ERROR, "%s", err_str);
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
                                 int total_bricks, int *type, char *err_str,
                                 int err_len)
{
        int ret = -1;

        /* replica count is set */
        switch (volinfo->type) {
        case GF_CLUSTER_TYPE_NONE:
                if ((volinfo->brick_count * replica_count) == total_bricks) {
                        /* Change the volume type */
                        *type = GF_CLUSTER_TYPE_REPLICATE;
                        gf_log (THIS->name, GF_LOG_INFO,
                                "Changing the type of volume %s from "
                                "'distribute' to 'replica'", volinfo->volname);
                        ret = 0;
                        goto out;

                } else {
                        snprintf (err_str, err_len, "Incorrect number of "
                                  "bricks (%d) supplied for replica count (%d).",
                                  (total_bricks - volinfo->brick_count),
                                  replica_count);
                        gf_log (THIS->name, GF_LOG_ERROR, "%s", err_str);
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
                } else {
                        snprintf (err_str, err_len, "Incorrect number of "
                                  "bricks (%d) supplied for changing volume's "
                                  "replica count to %d, need at least %d "
                                  "bricks",
                                  (total_bricks - volinfo->brick_count),
                                  replica_count, (volinfo->dist_leaf_count *
                                                  replica_count));
                        gf_log (THIS->name, GF_LOG_ERROR, "%s", err_str);
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
                        gf_log (THIS->name, GF_LOG_ERROR, "%s", err_str);
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
gd_rmbr_validate_replica_count (glusterd_volinfo_t *volinfo,
                                int32_t replica_count,
                                int32_t brick_count, char *err_str,
                                size_t err_len)
{
        int ret = -1;
        int replica_nodes = 0;

        switch (volinfo->type) {
        case GF_CLUSTER_TYPE_NONE:
        case GF_CLUSTER_TYPE_STRIPE:
                snprintf (err_str, err_len,
                          "replica count (%d) option given for non replicate "
                          "volume %s", replica_count, volinfo->volname);
                gf_log (THIS->name, GF_LOG_WARNING, "%s", err_str);
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
                        gf_log (THIS->name, GF_LOG_WARNING, "%s", err_str);
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
                                gf_log (THIS->name, GF_LOG_WARNING, "%s",
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
        int32_t                         stripe_count = 0;
        int                             type = 0;

        this = THIS;
        GF_ASSERT(this);

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                snprintf (err_str, sizeof (err_str), "Garbage args received");
                goto out;
        }

        gf_log (this->name, GF_LOG_INFO, "Received add brick req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
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
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        if (!(ret = glusterd_check_volume_exists (volname))) {
                ret = -1;
                snprintf (err_str, sizeof (err_str), "Volume %s does not exist",
                          volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "brick count");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (!ret) {
                gf_log (this->name, GF_LOG_INFO, "replica-count is %d",
                        replica_count);
        }

        ret = dict_get_int32 (dict, "stripe-count", &stripe_count);
        if (!ret) {
                gf_log (this->name, GF_LOG_INFO, "stripe-count is %d",
                        stripe_count);
        }

        if (!dict_get (dict, "force")) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get flag");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volinfo "
                          "for volume name %s", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
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
                        snprintf (err_str, sizeof (err_str), "Incorrect number "
                                  "of bricks supplied %d with count %d",
                                 brick_count, volinfo->dist_leaf_count);
                        gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
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
                        gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                        goto out;
                }

                /* if stripe count is same as earlier, set it back to 0 */
                if (ret == 1)
                        stripe_count = 0;

                ret = dict_set_int32 (dict, "stripe-count", stripe_count);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to set the stripe-count in dict");
                        goto out;
                }
                goto brick_val;
        }

        ret = gd_addbr_validate_replica_count (volinfo, replica_count,
                                               total_bricks,
                                               &type, err_str,
                                               sizeof (err_str));
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        /* if replica count is same as earlier, set it back to 0 */
        if (ret == 1)
                replica_count = 0;

        ret = dict_set_int32 (dict, "replica-count", replica_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to set the replica-count in dict");
                goto out;
        }

brick_val:
        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "bricks");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        if (type != volinfo->type) {
                ret = dict_set_int32 (dict, "type", type);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to set the new type in dict");
        }

        ret = glusterd_op_begin_synctask (req, GD_OP_ADD_BRICK, dict);

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

        list_for_each_entry (tmp, &volinfo->bricks, brick_list) {

                if (strcmp (tmp->hostname, brickinfo->hostname) ||
                    strcmp (tmp->path, brickinfo->path)) {
                        pos++;
                        continue;
                }
                gf_log (THIS->name, GF_LOG_DEBUG, LOGSTR_FOUND_BRICK,
                                        brickinfo->hostname, brickinfo->path,
                                        volinfo->volname);
                sub_volume = (pos / volinfo->dist_leaf_count);
                subvols[sub_volume]++;
                break;
        }

}

static int
subvol_matcher_verify (int *subvols, glusterd_volinfo_t *volinfo, char *err_str,
                       size_t err_len, char *vol_type)
{
        int i = 0;
        int ret = 0;

       do {

                if (subvols[i] % volinfo->dist_leaf_count == 0) {
                        continue;
                } else {
                        ret = -1;
                        snprintf (err_str, err_len,
                                "Bricks not from same subvol for %s", vol_type);
                        gf_log (THIS->name, GF_LOG_ERROR, "%s", err_str);
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
__glusterd_handle_remove_brick (rpcsvc_request_t *req)
{
        int32_t                   ret              = -1;
        gf_cli_req                cli_req          = {{0,}};
        dict_t                   *dict             = NULL;
        int32_t                   count            = 0;
        char                     *brick            = NULL;
        char                      key[256]         = {0,};
        char                     *brick_list       = NULL;
        int                       i                = 1;
        glusterd_volinfo_t       *volinfo          = NULL;
        glusterd_brickinfo_t     *brickinfo        = NULL;
        int                      *subvols          = NULL;
        glusterd_brickinfo_t     *tmp              = NULL;
        char                      err_str[2048]    = {0};
        gf_cli_rsp                rsp              = {0,};
        void                     *cli_rsp          = NULL;
        char                      vol_type[256]    = {0,};
        int32_t                   replica_count    = 0;
        int32_t                   brick_index      = 0;
        int32_t                   tmp_brick_idx    = 0;
        int                       found            = 0;
        int                       diff_count       = 0;
        char                     *volname          = 0;
        xlator_t                 *this             = NULL;

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


        gf_log (this->name, GF_LOG_INFO, "Received rem brick req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
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
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get brick "
                          "count");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                 snprintf (err_str, sizeof (err_str),"Volume %s does not exist",
                           volname);
                 gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                 goto out;
        }

        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (!ret) {
                gf_log (this->name, GF_LOG_INFO,
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
                                gf_log (this->name, GF_LOG_WARNING,
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
        } else {
                strcpy (vol_type, "distribute");
        }

	/* Do not allow remove-brick if the volume is a stripe volume*/
	if ((volinfo->type == GF_CLUSTER_TYPE_STRIPE) &&
            (volinfo->brick_count == volinfo->stripe_count)) {
                snprintf (err_str, sizeof (err_str),
                          "Removing brick from a stripe volume is not allowed");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
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
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
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
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                ret = -1;
                goto out;
        }

	/* Do not allow remove-brick if the bricks given is less than
           the replica count or stripe count */
        if (!replica_count && (volinfo->type != GF_CLUSTER_TYPE_NONE)) {
                if (volinfo->dist_leaf_count &&
                    (count % volinfo->dist_leaf_count)) {
                        snprintf (err_str, sizeof (err_str), "Remove brick "
                                  "incorrect brick count of %d for %s %d",
                                  count, vol_type, volinfo->dist_leaf_count);
                        gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
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

        if ((volinfo->type != GF_CLUSTER_TYPE_NONE) &&
            (volinfo->subvol_count > 1)) {
                ret = subvol_matcher_init (&subvols, volinfo->subvol_count);
                if (ret)
                        goto out;
        }

        while ( i <= count) {
                snprintf (key, sizeof (key), "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        snprintf (err_str, sizeof (err_str), "Unable to get %s",
                                  key);
                        gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                        goto out;
                }
                gf_log (this->name, GF_LOG_DEBUG, "Remove brick count %d brick:"
                        " %s", i, brick);

                ret = glusterd_volume_brickinfo_get_by_brick(brick, volinfo,
                                                             &brickinfo);
                if (ret) {
                        snprintf (err_str, sizeof (err_str), "Incorrect brick "
                                  "%s for volume %s", brick, volname);
                        gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
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
                                gf_log (this->name, GF_LOG_TRACE,
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

                        snprintf (err_str, sizeof (err_str), "Bricks are from "
                                  "same subvol");
                        gf_log (this->name, GF_LOG_INFO,
                                "failed to validate brick %s:%s (%d %d %d)",
                                tmp->hostname, tmp->path, tmp_brick_idx,
                                brick_index, volinfo->replica_count);
                        ret = -1;
                        /* brick order is not valid */
                        goto out;
                }

                /* Find which subvolume the brick belongs to */
                subvol_matcher_update (subvols, volinfo, brickinfo);
        }

        /* Check if the bricks belong to the same subvolumes.*/
        if ((volinfo->type != GF_CLUSTER_TYPE_NONE) &&
            (volinfo->subvol_count > 1)) {
                ret = subvol_matcher_verify (subvols, volinfo,
                                             err_str, sizeof(err_str),
                                             vol_type);
                if (ret)
                        goto out;
        }

        ret = glusterd_op_begin_synctask (req, GD_OP_REMOVE_BRICK, dict);

out:
        if (ret) {
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_to_cli (req, cli_rsp, NULL, 0, NULL,
                                 (xdrproc_t)xdr_gf_cli_rsp, dict);

                ret = 0; //sent error to cli, prevent second reply

        }

        GF_FREE (brick_list);
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
        char                          *slave_ip   = NULL;
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
                        gf_log ("", GF_LOG_ERROR,
                                "Failed to gf_strdup");
                        ret = -1;
                        goto out;
                }
        }
        else
                return 0;

        ret = dict_set_dynstr (param->rsp_dict, "slave", slave_buf);
        if (ret) {
                gf_log ("", GF_LOG_ERROR,
                        "Unable to store slave");
                if (slave_buf)
                        GF_FREE(slave_buf);
                goto out;
        }

        ret = glusterd_get_slave_details_confpath (param->volinfo,
                                                   param->rsp_dict,
                                                   &slave_ip, &slave_vol,
                                                   &conf_path, errmsg);
        if (ret) {
                if (*errmsg)
                        gf_log ("", GF_LOG_ERROR, "%s", *errmsg);
                else
                        gf_log ("", GF_LOG_ERROR,
                                "Unable to fetch slave or confpath details.");
                goto out;
        }

        /* In cases that gsyncd is not running, we will not invoke it
         * because of add-brick. */
        ret = glusterd_check_gsync_running_local (param->volinfo->volname,
                                                  slave, conf_path,
                                                  &is_running);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "gsync running validation failed.");
                goto out;
        }
        if (_gf_false == is_running) {
                gf_log ("", GF_LOG_DEBUG, "gsync session for %s and %s is"
                        " not running on this node. Hence not restarting.",
                        param->volinfo->volname, slave);
                ret = 0;
                goto out;
        }

        ret = glusterd_get_local_brickpaths (param->volinfo, &path_list);
        if (!path_list) {
                gf_log ("", GF_LOG_DEBUG, "This node not being part of"
                        " volume should not be running gsyncd. Hence"
                        " no gsyncd process to restart.");
                ret = 0;
                goto out;
        }

        ret = glusterd_check_restart_gsync_session (param->volinfo, slave,
                                                    param->rsp_dict, path_list,
                                                    conf_path, 0);
        if (ret)
                gf_log ("", GF_LOG_ERROR,
                        "Unable to restart gsync session.");

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d.", ret);
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
        int32_t                       type           = 0;
        glusterd_brickinfo_t         *brickinfo      = NULL;
        glusterd_gsync_status_temp_t  param          = {0, };
        gf_boolean_t                  restart_needed = 0;
        char                          msg[1024] __attribute__((unused)) = {0, };
        int                           caps           = 0;

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
                ret = glusterd_brickinfo_new_from_brick (brick, &brickinfo);
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
        volinfo->dist_leaf_count = glusterd_get_dist_leaf_count (volinfo);

        /* backward compatibility */
        volinfo->sub_count = ((volinfo->dist_leaf_count == 1) ? 0:
                              volinfo->dist_leaf_count);

        volinfo->subvol_count = (volinfo->brick_count /
                                 volinfo->dist_leaf_count);

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret)
                goto out;

        ret = 0;
        if (GLUSTERD_STATUS_STARTED != volinfo->status)
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

        while (i <= count) {
                ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                              &brickinfo);
                if (ret)
                        goto out;
#ifdef HAVE_BD_XLATOR
                /* Check for VG/thin pool if its BD volume */
                if (brickinfo->vg[0]) {
                        ret = glusterd_is_valid_vg (brickinfo, 0, msg);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_CRITICAL, "%s", msg);
                                goto out;
                        }
                        /* if anyone of the brick does not have thin support,
                           disable it for entire volume */
                        caps &= brickinfo->caps;
                } else
                        caps = 0;
#endif

                if (uuid_is_null (brickinfo->uuid)) {
                        ret = glusterd_resolve_brick (brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, FMTSTR_RESOLVE_BRICK,
                                        brickinfo->hostname, brickinfo->path);
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
                if ((!uuid_compare (brickinfo->uuid, MY_UUID)) &&
                    !restart_needed) {
                        restart_needed = 1;
                        gf_log ("", GF_LOG_DEBUG,
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
out:
        GF_FREE (free_ptr1);
        GF_FREE (free_ptr2);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
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
                                                      &brickinfo);
        if (ret)
                goto out;

        ret = glusterd_resolve_brick (brickinfo);
        if (ret)
                goto out;

        glusterd_volinfo_reset_defrag_stats (volinfo);

        if (!uuid_compare (brickinfo->uuid, MY_UUID)) {
                /* Only if the brick is in this glusterd, do the rebalance */
                if (need_migrate)
                        *need_migrate = 1;
        }

        if (force) {
                ret = glusterd_brick_stop (volinfo, brickinfo,
                                           _gf_true);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR, "Unable to stop "
                                "glusterfs, ret: %d", ret);
                }
                goto out;
        }

        brickinfo->decommissioned = 1;
        ret = 0;
out:
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
        glusterd_conf_t                         *priv = NULL;
        char                                    msg[2048] = {0,};
        gf_boolean_t                            brick_alloc = _gf_false;
        char                                    *all_bricks = NULL;
        char                                    *str_ret = NULL;
        gf_boolean_t                            is_force = _gf_false;

        priv = THIS->private;
        if (!priv)
                goto out;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Unable to find volume: %s", volname);
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        if (glusterd_is_rb_ongoing (volinfo)) {
                snprintf (msg, sizeof (msg), "Replace brick is in progress on "
                          "volume %s. Please retry after replace-brick "
                          "operation is committed or aborted", volname);
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        if (glusterd_is_defrag_on(volinfo)) {
                snprintf (msg, sizeof(msg), "Volume name %s rebalance is in "
                          "progress. Please retry after completion", volname);
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
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
                gf_log (THIS->name, GF_LOG_ERROR, "Unable to get bricks");
                goto out;
        }

        is_force = dict_get_str_boolean (dict, "force", _gf_false);

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
                        gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                        *op_errstr = gf_strdup (msg);

                        ret = -1;
                        goto out;

                }

                ret = glusterd_brickinfo_new_from_brick (brick, &brickinfo);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "Add-brick: Unable"
                                " to get brickinfo");
                        goto out;
                }
                brick_alloc = _gf_true;

                ret = glusterd_new_brick_validate (brick, brickinfo, msg,
                                                   sizeof (msg));
                if (ret) {
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }

                if (!uuid_compare (brickinfo->uuid, MY_UUID)) {
#ifdef HAVE_BD_XLATOR
                        if (brickinfo->vg[0]) {
                                ret = glusterd_is_valid_vg (brickinfo, 1, msg);
                                if (ret) {
                                        gf_log (THIS->name, GF_LOG_ERROR, "%s",
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
                }

                glusterd_brickinfo_delete (brickinfo);
                brick_alloc = _gf_false;
                brickinfo = NULL;
                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
        }

out:
        GF_FREE (free_ptr);
        if (brick_alloc && brickinfo)
                glusterd_brickinfo_delete (brickinfo);
        GF_FREE (str_ret);
        GF_FREE (all_bricks);

        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

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
        char               *task_id_str = NULL;
        xlator_t           *this        = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Volume %s does not exist", volname);
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        if (glusterd_is_rb_ongoing (volinfo)) {
                snprintf (msg, sizeof (msg), "Replace brick is in progress on "
                          "volume %s. Please retry after replace-brick "
                          "operation is committed or aborted", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        ret = dict_get_int32 (dict, "command", &flag);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get brick count");
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
                if ((volinfo->type == GF_CLUSTER_TYPE_REPLICATE) &&
                    dict_get (dict, "replica-count")) {
                        snprintf (msg, sizeof(msg), "Migration of data is not "
                                  "needed when reducing replica count. Use the"
                                  " 'force' option");
                        errstr = gf_strdup (msg);
                        gf_log (this->name, GF_LOG_ERROR, "%s", errstr);
                        goto out;
                }

                if (GLUSTERD_STATUS_STARTED != volinfo->status) {
                        snprintf (msg, sizeof (msg), "Volume %s needs to be "
                                  "started before remove-brick (you can use "
                                  "'force' or 'commit' to override this "
                                  "behavior)", volinfo->volname);
                        errstr = gf_strdup (msg);
                        gf_log (this->name, GF_LOG_ERROR, "%s", errstr);
                        goto out;
                }
                if (!gd_is_remove_brick_committed (volinfo)) {
                        snprintf (msg, sizeof (msg), "An earlier remove-brick "
                                  "task exists for volume %s. Either commit it"
                                  " or stop it before starting a new task.",
                                  volinfo->volname);
                        errstr = gf_strdup (msg);
                        gf_log (this->name, GF_LOG_ERROR, "Earlier remove-brick"
                                " task exists for volume %s.",
                                volinfo->volname);
                        goto out;
                }
                if (glusterd_is_defrag_on(volinfo)) {
                        errstr = gf_strdup("Rebalance is in progress. Please "
                                           "retry after completion");
                        gf_log (this->name, GF_LOG_ERROR, "%s", errstr);
                        goto out;
                }

                if (is_origin_glusterd ()) {
                        ret = glusterd_generate_and_set_task_id
                                (dict, GF_REMOVE_BRICK_TID_KEY);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to generate task-id");
                                goto out;
                        }
                } else {
                        ret = dict_get_str (dict, GF_REMOVE_BRICK_TID_KEY,
                                            &task_id_str);
                        if (ret) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Missing remove-brick-id");
                                ret = 0;
                        }
                }
                break;
        }

        case GF_OP_CMD_STOP:
                ret = 0;
                break;

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
                gf_log (this->name, GF_LOG_ERROR, "Unable to get brick count");
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
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
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

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = dict_get_int32 (dict, "command", &flag);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get command");
                goto out;
        }
        cmd = flag;

        /* Set task-id, if available, in ctx dict for operations other than
         * start
         */
        if (is_origin_glusterd () && (cmd != GF_OP_CMD_START)) {
                if (!uuid_is_null (volinfo->rebal.rebalance_id)) {
                        ret = glusterd_copy_uuid_to_dict
                                (volinfo->rebal.rebalance_id, dict,
                                 GF_REMOVE_BRICK_TID_KEY);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set remove-brick-id");
                                goto out;
                        }
                }
        }

        /* Clear task-id, rebal.op and stored bricks on commmitting/stopping
         * remove-brick */
        if ((cmd != GF_OP_CMD_START) || (cmd != GF_OP_CMD_STATUS)) {
                uuid_clear (volinfo->rebal.rebalance_id);
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
        {
                /* Fall back to the old volume file */
                list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks,
                                          brick_list) {
                        if (!brickinfo->decommissioned)
                                continue;
                        brickinfo->decommissioned = 0;
                }
                ret = glusterd_create_volfiles_and_notify_services (volinfo);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to create volfiles");
                        goto out;
                }

                ret = glusterd_store_volinfo (volinfo,
                                             GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to store volinfo");
                        goto out;
                }

                ret = 0;
                goto out;
        }

        case GF_OP_CMD_START:
                ret = dict_get_str (dict, GF_REMOVE_BRICK_TID_KEY, &task_id_str);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Missing remove-brick-id");
                        ret = 0;
                } else {
                        uuid_parse (task_id_str, volinfo->rebal.rebalance_id) ;
                        volinfo->rebal.op = GD_OP_REMOVE_BRICK;
                }
                force = 0;
                break;

        case GF_OP_CMD_COMMIT:
                force = 1;
                break;

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
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }

        /* Save the list of bricks for later usage only on starting a
         * remove-brick. Right now this is required for displaying the task
         * parameters with task status in volume status.
         */
        if (GF_OP_CMD_START == cmd) {
                bricks_dict = dict_new ();
                if (!bricks_dict) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_int32 (bricks_dict, "count", count);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to save remove-brick count");
                        goto out;
                }
        }
        while ( i <= count) {
                snprintf (key, 256, "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Unable to get %s",
                                key);
                        goto out;
                }

                if (GF_OP_CMD_START == cmd) {
                        brick_tmpstr = gf_strdup (brick);
                        if (!brick_tmpstr) {
                                ret = -1;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to duplicate brick name");
                                goto out;
                        }
                        ret = dict_set_dynstr (bricks_dict, key, brick_tmpstr);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
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
        if (GF_OP_CMD_START == cmd)
                volinfo->rebal.dict = dict_ref (bricks_dict);

        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (!ret) {
                gf_log (this->name, GF_LOG_INFO,
                        "changing replica count %d to %d on volume %s",
                        volinfo->replica_count, replica_count,
                        volinfo->volname);
                volinfo->replica_count = replica_count;
                volinfo->sub_count = replica_count;
                volinfo->dist_leaf_count = glusterd_get_dist_leaf_count (volinfo);
                volinfo->subvol_count = (volinfo->brick_count /
                                         volinfo->dist_leaf_count);

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
                gf_log (this->name, GF_LOG_WARNING, "failed to create volfiles");
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "failed to store volinfo");
                goto out;
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
                /* perform the rebalance operations */
                ret = glusterd_handle_defrag_start
                        (volinfo, err_str, sizeof (err_str),
                         GF_DEFRAG_CMD_START_FORCE,
                         glusterd_remove_brick_migrate_cbk, GD_OP_REMOVE_BRICK);

                if (!ret)
                        volinfo->decommission_in_progress = 1;

                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to start the rebalance");
                }
        } else {
                if (GLUSTERD_STATUS_STARTED == volinfo->status)
                        ret = glusterd_nodesvcs_handle_graph_change (volinfo);
        }

out:
        if (ret && err_str[0] && op_errstr)
                *op_errstr = gf_strdup (err_str);

        GF_FREE (brick_tmpstr);
        if (bricks_dict)
                dict_unref (bricks_dict);

        return ret;
}
