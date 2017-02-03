/*
   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
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
#include "glusterd-store.h"
#include "glusterd-geo-rep.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "run.h"
#include "syscall.h"
#include "byte-order.h"
#include "glusterd-svc-helper.h"
#include "compat-errno.h"
#include "glusterd-tierd-svc.h"
#include "glusterd-tierd-svc-helper.h"
#include "glusterd-messages.h"
#include "glusterd-mgmt.h"
#include "glusterd-syncop.h"

#include <sys/wait.h>
#include <dlfcn.h>

extern struct rpc_clnt_program gd_brick_prog;

const char *gd_tier_op_list[GF_DEFRAG_CMD_TYPE_MAX] = {
        [GF_DEFRAG_CMD_START_TIER]            = "start",
        [GF_DEFRAG_CMD_STOP_TIER]             = "stop",
};

int
__glusterd_handle_tier (rpcsvc_request_t *req)
{
        int32_t                         ret             = -1;
        gf_cli_req                      cli_req         = { {0,} };
        dict_t                         *dict            = NULL;
        glusterd_op_t                   cli_op          = GD_OP_TIER_START_STOP;
        char                           *volname         = NULL;
        int32_t                         cmd             = 0;
        char                            msg[2048]       = {0,};
        xlator_t                       *this            = NULL;
        glusterd_conf_t                *conf            = NULL;
        glusterd_volinfo_t              *volinfo        = NULL;
        char                            err_str[2048]   = {0};


        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, req, out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL, "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (msg, sizeof (msg), "Unable to decode the "
                                  "command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (msg, sizeof (msg), "Unable to get volume name");
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name, "
                        "while handling tier command");
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
        if (ret) {
                snprintf (msg, sizeof (msg), "Unable to get the command");
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get the cmd");
                goto out;
        }

        if (conf->op_version < GD_OP_VERSION_3_7_0) {
                snprintf (msg, sizeof (msg), "Cannot execute command. The "
                          "cluster is operating at version %d. Tier command "
                          "%s is unavailable in this version", conf->op_version,
                          gd_tier_op_list[cmd]);
                ret = -1;
                goto out;
        }

        if (conf->op_version < GD_OP_VERSION_3_10_0) {
                gf_msg_debug (this->name, 0, "The cluster is operating at "
                              "version less than or equal to %d. Falling back "
                              "to syncop framework.",
                              GD_OP_VERSION_3_7_5);
                switch (cmd) {
                case GF_DEFRAG_CMD_DETACH_STOP:
                        ret = dict_set_int32 (dict, "rebalance-command",
                                              GF_DEFRAG_CMD_STOP_DETACH_TIER);
                        break;

                case GF_DEFRAG_CMD_DETACH_COMMIT:
                        ret = glusterd_volinfo_find (volname, &volinfo);
                        if (ret) {
                                snprintf (err_str, sizeof (err_str), "Volume "
                                          "%s does not exist", volname);
                                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                        GD_MSG_VOL_NOT_FOUND, "%s", err_str);
                                goto out;
                        }
                        ret = glusterd_set_detach_bricks (dict, volinfo);
                        ret = dict_set_int32 (dict, "command",
                                              GF_OP_CMD_DETACH_COMMIT);
                        break;
                case GF_DEFRAG_CMD_DETACH_COMMIT_FORCE:
                        ret = glusterd_volinfo_find (volname, &volinfo);
                        if (ret) {
                                snprintf (err_str, sizeof (err_str), "Volume "
                                          "%s does not exist", volname);
                                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                        GD_MSG_VOL_NOT_FOUND, "%s", err_str);
                                goto out;
                        }
                        ret = glusterd_set_detach_bricks (dict, volinfo);
                        ret = dict_set_int32 (dict, "command",
                                              GF_OP_CMD_DETACH_COMMIT_FORCE);
                        break;
                case GF_DEFRAG_CMD_DETACH_START:
                        ret = glusterd_volinfo_find (volname, &volinfo);
                        if (ret) {
                                snprintf (err_str, sizeof (err_str), "Volume "
                                          "%s does not exist", volname);
                                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                        GD_MSG_VOL_NOT_FOUND, "%s", err_str);
                                goto out;
                        }
                        ret = glusterd_set_detach_bricks (dict, volinfo);
                        ret = dict_set_int32 (dict, "command",
                                              GF_OP_CMD_DETACH_START);
                        break;

                default:
                        break;

                }
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to set dict");
                        goto out;
                }
                if ((cmd == GF_DEFRAG_CMD_STATUS_TIER) ||
                    (cmd == GF_DEFRAG_CMD_DETACH_STATUS) ||
                    (cmd == GF_DEFRAG_CMD_START_TIER) ||
                    (cmd == GF_DEFRAG_CMD_DETACH_STOP)) {
                        ret = glusterd_op_begin (req, GD_OP_DEFRAG_BRICK_VOLUME,
                                                 dict, msg, sizeof (msg));
                } else
                        ret = glusterd_op_begin (req, GD_OP_REMOVE_BRICK, dict,
                                                 msg, sizeof (msg));

                glusterd_friend_sm ();
                glusterd_op_sm ();

        } else {
                switch (cmd) {
                case GF_DEFRAG_CMD_STATUS_TIER:
                        cli_op = GD_OP_TIER_STATUS;
                        break;

                case GF_DEFRAG_CMD_DETACH_STATUS:
                        cli_op = GD_OP_DETACH_TIER_STATUS;
                        break;

                case GF_DEFRAG_CMD_DETACH_STOP:
                        cli_op = GD_OP_REMOVE_TIER_BRICK;
                        break;

                case GF_DEFRAG_CMD_DETACH_COMMIT:
                case GF_DEFRAG_CMD_DETACH_COMMIT_FORCE:
                case GF_DEFRAG_CMD_DETACH_START:
                        cli_op = GD_OP_REMOVE_TIER_BRICK;
                        ret = glusterd_volinfo_find (volname, &volinfo);
                        if (ret) {
                                snprintf (err_str, sizeof (err_str), "Volume "
                                          "%s does not exist", volname);
                                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                        GD_MSG_VOL_NOT_FOUND, "%s", err_str);
                                goto out;
                        }
                        ret = glusterd_set_detach_bricks (dict, volinfo);
                        break;

                default:
                        break;
                }
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED, "dict set failed");
                        goto out;
                }
                ret = glusterd_mgmt_v3_initiate_all_phases (req,
                                                            cli_op,
                                                            dict);
        }

out:
        if (ret) {
                if (msg[0] == '\0')
                        snprintf (msg, sizeof (msg), "Tier operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, msg);
        }

        return ret;
}

int
glusterd_handle_tier (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_tier);
}


static int
glusterd_manage_tier (glusterd_volinfo_t *volinfo, int opcode)
{
        int              ret   = -1;
        xlator_t         *this = NULL;
        glusterd_conf_t  *priv = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        switch (opcode) {
        case GF_DEFRAG_CMD_START_TIER:
        case GF_DEFRAG_CMD_STOP_TIER:
                ret = volinfo->tierd.svc.manager (&(volinfo->tierd.svc),
                                                volinfo, PROC_START_NO_WAIT);
                break;
        default:
                ret = 0;
                break;
        }

out:
        return ret;

}

static int
glusterd_tier_enable (glusterd_volinfo_t *volinfo, char **op_errstr)
{
        int32_t                 ret                     = -1;
        xlator_t                *this                   = NULL;
        int32_t                 tier_online             = -1;
        char                    pidfile[PATH_MAX]       = {0};
        int32_t                 pid                     = -1;
        glusterd_conf_t         *priv                   = NULL;

        this = THIS;

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errstr, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        if (glusterd_is_volume_started (volinfo) == 0) {
                *op_errstr = gf_strdup ("Volume is stopped, start volume "
                                        "to enable tier.");
                ret = -1;
                goto out;
        }

        GLUSTERD_GET_TIER_PID_FILE(pidfile, volinfo, priv);
        tier_online = gf_is_service_running (pidfile, &pid);

        if (tier_online) {
                *op_errstr = gf_strdup ("tier is already enabled");
                ret = -1;
                goto out;
        }

        volinfo->is_tier_enabled = _gf_true;

        ret = 0;
out:
        if (ret && op_errstr && !*op_errstr)
                gf_asprintf (op_errstr, "Enabling tier on volume %s has been "
                             "unsuccessful", volinfo->volname);
        return ret;
}

static int
glusterd_tier_disable (glusterd_volinfo_t *volinfo, char **op_errstr)
{
        int32_t                 ret                     = -1;
        xlator_t                *this                   = NULL;
        int32_t                 tier_online             = -1;
        char                    pidfile[PATH_MAX]       = {0};
        int32_t                 pid                     = -1;
        glusterd_conf_t         *priv                   = NULL;

        this = THIS;

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errstr, out);
        priv = this->private;

        GLUSTERD_GET_TIER_PID_FILE(pidfile, volinfo, priv);
        tier_online = gf_is_service_running (pidfile, &pid);

        if (!tier_online) {
                *op_errstr = gf_strdup ("tier is already disabled");
                ret = -1;
                goto out;
        }

        volinfo->is_tier_enabled = _gf_false;

        ret = 0;
out:
        if (ret && op_errstr && !*op_errstr)
                gf_asprintf (op_errstr, "Disabling tier volume %s has "
                             "been unsuccessful", volinfo->volname);
        return ret;
}

int
glusterd_op_remove_tier_brick (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        glusterd_conf_t        *priv           = NULL;
        xlator_t               *this           = NULL;
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
        int32_t                 cmd            = 0;
        int32_t                 replica_count  = 0;
        glusterd_brickinfo_t    *brickinfo     = NULL;
        glusterd_brickinfo_t    *tmp           = NULL;
        char                    *task_id_str   = NULL;
        dict_t                  *bricks_dict   = NULL;
        char                    *brick_tmpstr  = NULL;
        uint32_t                 commit_hash   = 0;
        int                      detach_commit = 0;
        void                    *tier_info     = NULL;
        char                    *cold_shd_key  = NULL;
        char                    *hot_shd_key   = NULL;
        int                      delete_key    = 1;
        glusterd_svc_t          *svc           = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errstr, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_VOL_NOT_FOUND, "Unable to get volinfo");
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "cmd not found");
                goto out;
        }

        if (is_origin_glusterd (dict) &&
                        (cmd != GF_DEFRAG_CMD_DETACH_START)) {
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
        /*check only if a tierd is supposed to be running
         * if no brick in the tierd volume is a local brick
         * skip it */
        cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                 brick_list) {
                if (glusterd_is_local_brick (this, volinfo,
                                             brickinfo)) {
                        flag = _gf_true;
                        break;
                }
        }
        if (!flag)
                goto out;


        ret = -1;

        switch (cmd) {
        case GF_DEFRAG_CMD_DETACH_STOP:
                        /* Fall back to the old volume file */
                        cds_list_for_each_entry_safe (brickinfo, tmp,
                                        &volinfo->bricks,
                                        brick_list) {
                                if (!brickinfo->decommissioned)
                                        continue;
                                brickinfo->decommissioned = 0;
                        }
                        ret = glusterd_create_volfiles_and_notify_services
                                (volinfo);

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
                        ret = glusterd_tierdsvc_restart ();
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_TIERD_START_FAIL,
                                        "Couldn't restart tierd for "
                                        "vol: %s", volinfo->volname);
                                goto out;
                        }

                        volinfo->tier.op = GD_OP_DETACH_NOT_STARTED;
                        ret = 0;
                        goto out;



        case GF_DEFRAG_CMD_DETACH_START:
                        ret = dict_get_str (dict, GF_REMOVE_BRICK_TID_KEY,
                                            &task_id_str);
                        if (ret) {
                                gf_msg_debug (this->name, errno,
                                              "Missing remove-brick-id");
                                ret = 0;
                        } else {
                                ret = dict_set_str (rsp_dict,
                                                    GF_REMOVE_BRICK_TID_KEY,
                                                    task_id_str);
                                gf_uuid_parse (task_id_str,
                                               volinfo->tier.rebalance_id);
                        }
                        force = 0;

                        volinfo->tier.op = GD_OP_DETACH_TIER;
                        volinfo->tier.defrag_status = GF_DEFRAG_STATUS_STARTED;
                        break;

        case GF_DEFRAG_CMD_DETACH_COMMIT:
                        if (volinfo->decommission_in_progress) {
                                gf_asprintf (op_errstr, "use 'force' option as "
                                             "migration is in progress");
                                goto out;
                        }
                        if (volinfo->rebal.defrag_status ==
                                        GF_DEFRAG_STATUS_FAILED) {
                                gf_asprintf (op_errstr, "use 'force' option as "
                                             "migration has failed");
                                goto out;
                        }

        case GF_DEFRAG_CMD_DETACH_COMMIT_FORCE:
                        glusterd_op_perform_detach_tier (volinfo);
                        detach_commit = 1;

                        /* Disabling ctr when detaching a tier, since
                         * currently tier is the only consumer of ctr.
                         * Revisit this code when this constraint no
                         * longer exist.
                         */
                        dict_del (volinfo->dict, "features.ctr-enabled");
                        dict_del (volinfo->dict, "cluster.tier-mode");

                        hot_shd_key = gd_get_shd_key
                                (volinfo->tier_info.hot_type);
                        cold_shd_key = gd_get_shd_key
                                (volinfo->tier_info.cold_type);
                        if (hot_shd_key) {
                                /*
                                 * Since post detach, shd graph will not
                                 * contain hot tier. So we need to clear
                                 * option set for hot tier. For a tiered
                                 * volume there can be different key
                                 * for both hot and cold. If hot tier is
                                 * shd compatible then we need to remove
                                 * the configured value when detaching a tier,
                                 * only if the key's are different or
                                 * cold key is NULL. So we will set
                                 * delete_key first, and if cold key is not
                                 * null and they are equal then we will clear
                                 * the flag. Otherwise we will delete the
                                 * key.
                                 */

                                if (cold_shd_key)
                                        delete_key = strcmp (hot_shd_key,
                                                        cold_shd_key);
                                if (delete_key)
                                        dict_del (volinfo->dict, hot_shd_key);
                        }
                        /* fall through */

                        if (volinfo->decommission_in_progress) {
                                if (volinfo->tier.defrag) {
                                        LOCK (&volinfo->rebal.defrag->lock);
                                        /* Fake 'rebalance-complete' so the
                                         * graph change
                                         * happens right away */
                                        volinfo->tier.defrag_status =
                                                GF_DEFRAG_STATUS_COMPLETE;

                                        UNLOCK (&volinfo->tier.defrag->lock);
                                }
                        }

                        volinfo->tier.op = GD_OP_DETACH_NOT_STARTED;
                        ret = 0;
                        force = 1;
                        break;
        default:
                gf_asprintf (op_errstr, "tier command failed. Invalid "
                             "opcode");
                ret = -1;
                goto out;
        }

        count = glusterd_set_detach_bricks(dict, volinfo);

        if (cmd == GF_DEFRAG_CMD_DETACH_START) {
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

        while (i <= count) {
                snprintf (key, 256, "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_GET_FAILED, "Unable to get %s",
                                key);
                        goto out;
                }

                if (cmd == GF_DEFRAG_CMD_DETACH_START) {
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

        if (cmd == GF_DEFRAG_CMD_DETACH_START)
                volinfo->tier.dict = dict_ref (bricks_dict);

        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (!ret) {
                gf_msg (this->name, GF_LOG_INFO, errno,
                        GD_MSG_DICT_GET_FAILED,
                        "changing replica count %d to %d on volume %s",
                        volinfo->replica_count, replica_count,
                        volinfo->volname);
                volinfo->replica_count = replica_count;
                volinfo->sub_count = replica_count;
                volinfo->dist_leaf_count = glusterd_get_dist_leaf_count
                        (volinfo);

                /*
                 * volinfo->type and sub_count have already been set for
                 * volumes undergoing a detach operation, they should not
                 * be modified here.
                 */
                if ((replica_count == 1) && (cmd != GF_DEFRAG_CMD_DETACH_COMMIT)
                                && (cmd != GF_DEFRAG_CMD_DETACH_COMMIT_FORCE)) {
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
                        GD_MSG_VOLFILE_CREATE_FAIL, "failed to create"
                        "volfiles");
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo,
                        GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_VOLINFO_STORE_FAIL, "failed to store volinfo");
                goto out;
        }

        if (cmd == GF_DEFRAG_CMD_DETACH_START &&
                        volinfo->status == GLUSTERD_STATUS_STARTED) {

                svc = &(volinfo->tierd.svc);
                ret = svc->reconfigure (volinfo);
                if (ret)
                        goto out;

                ret = glusterd_svcs_reconfigure ();
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_NFS_RECONF_FAIL,
                                "Unable to reconfigure NFS-Server");
                        goto out;
                }
        }
        /* Need to reset the defrag/rebalance status accordingly */
        switch (volinfo->tier.defrag_status) {
        case GF_DEFRAG_STATUS_FAILED:
        case GF_DEFRAG_STATUS_COMPLETE:
                volinfo->tier.defrag_status = 0;
        default:
                break;
        }
        if (!force && need_rebalance) {
                if (dict_get_uint32(dict, "commit-hash", &commit_hash) == 0) {
                        volinfo->tier.commit_hash = commit_hash;
                }
                /* perform the rebalance operations */
                ret = glusterd_handle_defrag_start
                        (volinfo, err_str, sizeof (err_str),
                         GF_DEFRAG_CMD_START_DETACH_TIER,
                         /*change this label to GF_DEFRAG_CMD_DETACH_START
                          * while removing old code
                          */
                         glusterd_remove_brick_migrate_cbk, GD_OP_REMOVE_BRICK);

                if (!ret)
                        volinfo->decommission_in_progress = 1;

                else if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_REBALANCE_START_FAIL,
                                "failed to start the rebalance");
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
glusterd_op_tier_start_stop (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        glusterd_volinfo_t      *volinfo                = NULL;
        int32_t                 ret                     = -1;
        char                    *volname                = NULL;
        int                     cmd                     = -1;
        xlator_t                *this                   = NULL;
        glusterd_brickinfo_t    *brick                  = NULL;
        gf_boolean_t            retval                  = _gf_false;
        glusterd_conf_t         *priv                   = NULL;
        int32_t                 pid                     = -1;
        char                    pidfile[PATH_MAX]       = {0};

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errstr, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_asprintf (op_errstr, FMTSTR_CHECK_VOL_EXISTS, volname);
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get cmd from "
                        "dict");
                goto out;
        }

        cds_list_for_each_entry (brick, &volinfo->bricks, brick_list) {
                if (gf_uuid_compare (MY_UUID, brick->uuid) == 0) {
                        retval = _gf_true;
                        break;
                }
        }
                /*check if this node needs tierd*/

        if (!retval)
                goto out;

        switch (cmd) {
        case GF_DEFRAG_CMD_START_TIER:
                GLUSTERD_GET_TIER_PID_FILE(pidfile, volinfo, priv);
                /* we check if its running and skip so that we dont get a
                 * failure during force start
                 */
                if (gf_is_service_running (pidfile, &pid))
                        goto out;
                ret = glusterd_tier_enable (volinfo, op_errstr);
                if (ret < 0)
                        goto out;
                glusterd_store_perform_node_state_store (volinfo);
                break;

        case GF_DEFRAG_CMD_STOP_TIER:
                ret = glusterd_tier_disable (volinfo, op_errstr);
                if (ret < 0)
                        goto out;
                break;
        default:
                gf_asprintf (op_errstr, "tier command failed. Invalid "
                             "opcode");
                ret = -1;
                goto out;
        }

        ret = glusterd_manage_tier (volinfo, cmd);
        if (ret)
                goto out;

        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_STORE_FAIL,
                        "Failed to store volinfo for tier");
                goto out;
        }

out:
        return ret;
}

int
glusterd_op_stage_tier (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        char                    *volname             = NULL;
        int                     ret                  = -1;
        int32_t                 cmd                  = 0;
        char                    msg[2048]            = {0};
        glusterd_volinfo_t      *volinfo             = NULL;
        char                    *task_id_str         = NULL;
        xlator_t                *this                = 0;
        int32_t                 is_force             = 0;
        char                    pidfile[PATH_MAX]    = {0};
        int32_t                 tier_online          = -1;
        int32_t                 pid                  = -1;
        int32_t                 brick_count          = 0;
        gsync_status_param_t    param                = {0,};
        glusterd_conf_t         *priv                = NULL;
        gf_boolean_t            flag                 = _gf_false;
        glusterd_brickinfo_t    *brickinfo           = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errstr, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "volname not found");
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "cmd not found");
                goto out;
        }

        ret = glusterd_rebalance_cmd_validate (cmd, volname, &volinfo,
                                               msg, sizeof (msg));
        if (ret) {
                gf_msg_debug (this->name, 0, "cmd validate failed");
                goto out;
        }

        if (volinfo->type != GF_CLUSTER_TYPE_TIER) {
                snprintf (msg, sizeof(msg), "volume %s is not a tier "
                          "volume", volinfo->volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_TIER, "volume: %s is not a tier "
                        "volume", volinfo->volname);
                ret = -1;
                goto out;
        }
        /* Check if the connected clients are all of version
         * glusterfs-3.6 and higher. This is needed to prevent some data
         * loss issues that could occur when older clients are connected
         * when rebalance is run. This check can be bypassed by using
         * 'force'
         */
        ret = glusterd_check_client_op_version_support
                (volname, GD_OP_VERSION_3_6_0, NULL);
        if (ret) {
                ret = gf_asprintf (op_errstr, "Volume %s has one or "
                                   "more connected clients of a version"
                                   " lower than GlusterFS-v3.6.0. "
                                   "Tier operations not supported in"
                                   " below this version", volname);
                goto out;
        }
        /*check only if a tierd is supposed to be running
         * if no brick in the tierd volume is a local brick
         * skip it */
        cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                 brick_list) {
                if (glusterd_is_local_brick (this, volinfo,
                                             brickinfo)) {
                        flag = _gf_true;
                        break;
                }
        }
        if (!flag)
                goto out;

        GLUSTERD_GET_TIER_PID_FILE(pidfile, volinfo, priv);
        tier_online = gf_is_service_running (pidfile, &pid);

        switch (cmd) {
        case GF_DEFRAG_CMD_START_TIER:
                ret = dict_get_int32 (dict, "force", &is_force);
                if (ret)
                        is_force = 0;

                if (brickinfo->status != GF_BRICK_STARTED) {
                        gf_asprintf (op_errstr, "Received"
                                     " tier start on volume "
                                     "with  stopped brick %s",
                                     brickinfo->path);
                        ret = -1;
                        goto out;
                }
                if ((!is_force) && tier_online) {
                        ret = gf_asprintf (op_errstr, "Tier daemon is "
                                           "already running on volume %s",
                                           volname);
                        ret = -1;
                        goto out;
                }
                ret = glusterd_defrag_start_validate (volinfo, msg,
                                                      sizeof (msg),
                                                      GD_OP_REBALANCE);
                if (ret) {
                        gf_msg (this->name, 0, GF_LOG_ERROR,
                                GD_MSG_REBALANCE_START_FAIL,
                                "start validate failed");
                        goto out;
                }
                break;

        case GF_DEFRAG_CMD_STOP_TIER:

                if (!tier_online) {
                        ret = gf_asprintf (op_errstr, "Tier daemon is "
                                           "not running on volume %s",
                                           volname);
                        ret = -1;
                        goto out;
                }
                break;

        case GF_DEFRAG_CMD_DETACH_START:


                ret = dict_get_int32 (dict, "count", &brick_count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to get brick count");
                        goto out;
                }

                if (!tier_online) {
                        ret = gf_asprintf (op_errstr, "Tier daemon is "
                                           "not running on volume %s",
                                           volname);
                        ret = -1;
                        goto out;
                }
                if (volinfo->tier.op == GD_OP_DETACH_TIER) {
                        snprintf (msg, sizeof (msg), "An earlier detach tier "
                                  "task exists for volume %s. Either commit it"
                                  " or stop it before starting a new task.",
                                  volinfo->volname);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_OLD_REMOVE_BRICK_EXISTS,
                                "Earlier remove-brick"
                                " task exists for volume %s.",
                                volinfo->volname);
                        ret = -1;
                        goto out;
                }
                if (glusterd_is_defrag_on(volinfo)) {
                        snprintf (msg, sizeof (msg), "Migration is in progress."
                                  " Please retry after completion");
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_OIP_RETRY_LATER, "Migration is"
                                "in progress");
                        goto out;
                }

                ret = glusterd_remove_brick_validate_bricks (GF_OP_CMD_NONE,
                                                             brick_count,
                                                             dict, volinfo,
                                                             op_errstr, cmd);
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

        case GF_DEFRAG_CMD_DETACH_STOP:
                if (volinfo->tier.op != GD_OP_DETACH_TIER) {
                        snprintf (msg, sizeof(msg), "Detach-tier "
                                  "not started");
                        ret = -1;
                        goto out;
                }
                ret = 0;
                break;

        case GF_DEFRAG_CMD_STATUS_TIER:

                if (!tier_online) {
                        ret = gf_asprintf (op_errstr, "Tier daemon is "
                                           "not running on volume %s",
                                           volname);
                        ret = -1;
                        goto out;
                }
                break;

        case GF_DEFRAG_CMD_DETACH_COMMIT:

                if (volinfo->tier.op != GD_OP_DETACH_TIER) {
                        snprintf (msg, sizeof(msg), "Detach-tier "
                                  "not started");
                        ret = -1;
                        goto out;
                }
                if ((volinfo->rebal.defrag_status == GF_DEFRAG_STATUS_STARTED)
                                && (volinfo->tier.op == GD_OP_DETACH_TIER)) {
                        ret = -1;
                        snprintf (msg, sizeof (msg), "Detach is in progress. "
                                  "Please retry after completion");
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_OIP_RETRY_LATER, "Detach is in "
                                "progress");
                        goto out;
                }

                ret = dict_get_int32 (dict, "count", &brick_count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to get brick count");
                        goto out;
                }

                ret = glusterd_remove_brick_validate_bricks (GF_OP_CMD_NONE,
                                                             brick_count,
                                                             dict, volinfo,
                                                             op_errstr, cmd);
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
        case GF_DEFRAG_CMD_DETACH_STATUS:
                if (volinfo->tier.op != GD_OP_DETACH_TIER) {
                        snprintf (msg, sizeof(msg), "Detach-tier "
                                  "not started");
                        ret = -1;
                        goto out;
                }
                break;

        case GF_DEFRAG_CMD_DETACH_COMMIT_FORCE:
        default:
                break;

        }

        ret = 0;
out:
        if (ret && op_errstr && msg[0])
                *op_errstr = gf_strdup (msg);

        return ret;
}

int32_t
glusterd_add_tierd_to_dict (glusterd_volinfo_t *volinfo,
                            dict_t  *dict, int32_t count)
{

        int             ret                   = -1;
        int32_t         pid                   = -1;
        int32_t         brick_online          = -1;
        char            key[1024]             = {0};
        char            base_key[1024]        = {0};
        char            pidfile[PATH_MAX]     = {0};
        xlator_t        *this                 = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);

        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);

        snprintf (base_key, sizeof (base_key), "brick%d", count);
        snprintf (key, sizeof (key), "%s.hostname", base_key);
        ret = dict_set_str (dict, key, "Tier Daemon");
        if (ret)
                goto out;

        snprintf (key, sizeof (key), "%s.path", base_key);
        ret = dict_set_dynstr (dict, key, gf_strdup (uuid_utoa (MY_UUID)));
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.port", base_key);
        ret = dict_set_int32 (dict, key, volinfo->tierd.port);
        if (ret)
                goto out;

        glusterd_svc_build_tierd_pidfile (volinfo, pidfile, sizeof (pidfile));

        brick_online = gf_is_service_running (pidfile, &pid);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.pid", base_key);
        ret = dict_set_int32 (dict, key, pid);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.status", base_key);
        ret = dict_set_int32 (dict, key, brick_online);

out:
        if (ret)
                gf_msg (this ? this->name : "glusterd",
                        GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Returning %d. adding values to dict failed", ret);

        return ret;
}

int32_t
__glusterd_tier_status_cbk (struct rpc_req *req, struct iovec *iov,
                            int count, void *myframe)
{
        gd1_mgmt_brick_op_rsp         rsp       = {0};
        int                           ret       = -1;
        call_frame_t                  *frame    = NULL;
        xlator_t                      *this     = NULL;
        glusterd_conf_t               *priv     = NULL;
        struct syncargs               *args     = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, req, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        frame = myframe;
        args = frame->local;

        if (-1 == req->rpc_status) {
                args->op_errno = ENOTCONN;
                goto out;
        }

        ret =  xdr_to_generic (*iov, &rsp,
                        (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RES_DECODE_FAIL,
                        "Failed to decode brick op "
                        "response received");
                goto out;
        }

        if (rsp.output.output_len) {
                args->dict  = dict_new ();
                if (!args->dict) {
                        ret = -1;
                        args->op_errno = ENOMEM;
                        goto out;
                }

                ret = dict_unserialize (rsp.output.output_val,
                                        rsp.output.output_len,
                                        &args->dict);
                if (ret < 0)
                        goto out;
        }
        args->op_ret = rsp.op_ret;
        args->op_errno = rsp.op_errno;
        args->errstr = gf_strdup (rsp.op_errstr);

out:
        if ((rsp.op_errstr) && (strcmp (rsp.op_errstr, "") != 0))
                free (rsp.op_errstr);
        free (rsp.output.output_val);
        if (req->rpc_status != -1)
                GLUSTERD_STACK_DESTROY(frame);
        __wake (args);

        return ret;

}

int32_t
glusterd_tier_status_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_tier_status_cbk);
}

int
glusterd_op_tier_status (dict_t *dict, char **op_errstr, dict_t *rsp_dict,
                         glusterd_op_t op)
{
        int                             ret       = -1;
        xlator_t                        *this     = NULL;
        struct syncargs                 args = {0, };
        glusterd_req_ctx_t              *data   = NULL;
        gd1_mgmt_brick_op_req           *req = NULL;
        glusterd_conf_t                 *priv = NULL;
        int                             pending_bricks = 0;
        glusterd_pending_node_t         *pending_node;
        glusterd_req_ctx_t              *req_ctx = NULL;
        struct rpc_clnt                 *rpc = NULL;
        uuid_t                          *txn_id = NULL;
        extern                          glusterd_op_info_t opinfo;

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);
        GF_VALIDATE_OR_GOTO (this->name, rsp_dict, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);
        args.op_ret = -1;
        args.op_errno = ENOTCONN;
        data = GF_CALLOC (1, sizeof (*data),
                        gf_gld_mt_op_allack_ctx_t);

        gf_uuid_copy (data->uuid, MY_UUID);

        /* we are printing the detach status for issue of detach start
         * by then we need the op to be GD_OP_DETACH_TIER_STATUS for it to
         * get the status. ad for the rest of the condition it can go as such.
         */

        if (op == GD_OP_REMOVE_TIER_BRICK)
                data->op = GD_OP_DETACH_TIER_STATUS;
        else
                data->op = op;
        data->dict = dict;

        txn_id = &priv->global_txn_id;

        req_ctx = data;
        GF_VALIDATE_OR_GOTO (this->name, req_ctx, out);
        CDS_INIT_LIST_HEAD (&opinfo.pending_bricks);

        ret = dict_get_bin (req_ctx->dict, "transaction_id", (void **)&txn_id);
        gf_msg_debug (this->name, 0, "transaction ID = %s",
                      uuid_utoa (*txn_id));

        ret = glusterd_op_bricks_select (req_ctx->op, req_ctx->dict, op_errstr,
                                         &opinfo.pending_bricks, NULL);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_SELECT_FAIL, "Failed to select bricks");
                opinfo.op_errstr = *op_errstr;
                goto out;
        }

        cds_list_for_each_entry (pending_node, &opinfo.pending_bricks, list) {
                ret = glusterd_brick_op_build_payload
                        (req_ctx->op, pending_node->node,
                         (gd1_mgmt_brick_op_req **)&req,
                         req_ctx->dict);

                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_OP_PAYLOAD_BUILD_FAIL,
                                "Failed to build brick op payload during "
                                "'Volume %s'", gd_op_list[req_ctx->op]);
                        goto out;
                }


                rpc = glusterd_pending_node_get_rpc (pending_node);
                if (!rpc) {
                        opinfo.brick_pending_count = 0;
                        ret = 0;
                        if (req) {
                                GF_FREE (req);
                                req = NULL;
                        }
                        glusterd_defrag_volume_node_rsp (req_ctx->dict,
                                                         NULL, rsp_dict);

                        goto out;
                }

                GD_SYNCOP (rpc, (&args), NULL, glusterd_tier_status_cbk, req,
                           &gd_brick_prog, req->op, xdr_gd1_mgmt_brick_op_req);

                if (req) {
                        GF_FREE (req);
                        req = NULL;
                }
                if (!ret)
                        pending_bricks++;

                glusterd_pending_node_put_rpc (pending_node);
        }
        glusterd_handle_node_rsp (req_ctx->dict, pending_node->node,
                                  req_ctx->op, args.dict, rsp_dict, op_errstr,
                                  pending_node->type);
        gf_msg_trace (this->name, 0, "Sent commit op req for operation "
                      "'Volume %s' to %d bricks", gd_op_list[req_ctx->op],
                      pending_bricks);
        opinfo.brick_pending_count = pending_bricks;

out:

        if (ret)
                opinfo.op_ret = ret;

        ret = glusterd_set_txn_opinfo (txn_id, &opinfo);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_TRANS_OPINFO_SET_FAIL,
                        "Unable to set transaction's opinfo");

        gf_msg_debug (this ? this->name : "glusterd", 0,
                      "Returning %d. Failed to get tier status", ret);
        return ret;

}
