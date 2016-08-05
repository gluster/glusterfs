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
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "run.h"
#include "syscall.h"
#include "byte-order.h"
#include "compat-errno.h"
#include "glusterd-scrub-svc.h"
#include "glusterd-messages.h"

#include <sys/wait.h>
#include <dlfcn.h>

const char *gd_bitrot_op_list[GF_BITROT_OPTION_TYPE_MAX] = {
        [GF_BITROT_OPTION_TYPE_NONE]            = "none",
        [GF_BITROT_OPTION_TYPE_ENABLE]          = "enable",
        [GF_BITROT_OPTION_TYPE_DISABLE]         = "disable",
        [GF_BITROT_OPTION_TYPE_SCRUB_THROTTLE]  = "scrub-throttle",
        [GF_BITROT_OPTION_TYPE_SCRUB_FREQ]      = "scrub-frequency",
        [GF_BITROT_OPTION_TYPE_SCRUB]           = "scrub",
        [GF_BITROT_OPTION_TYPE_EXPIRY_TIME]     = "expiry-time",
};

int
__glusterd_handle_bitrot (rpcsvc_request_t *req)
{
        int32_t                         ret       = -1;
        gf_cli_req                      cli_req   = { {0,} };
        dict_t                         *dict      = NULL;
        glusterd_op_t                   cli_op    = GD_OP_BITROT;
        char                           *volname   = NULL;
        char                           *scrub     = NULL;
        int32_t                         type      = 0;
        char                            msg[2048] = {0,};
        xlator_t                       *this      = NULL;
        glusterd_conf_t                *conf      = NULL;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);

        conf = this->private;
        GF_ASSERT (conf);

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
                        "while handling bitrot command");
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                snprintf (msg, sizeof (msg), "Unable to get type of command");
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get type of cmd, "
                        "while handling bitrot command");
                goto out;
        }

        if (conf->op_version < GD_OP_VERSION_3_7_0) {
                snprintf (msg, sizeof (msg), "Cannot execute command. The "
                          "cluster is operating at version %d. Bitrot command "
                          "%s is unavailable in this version", conf->op_version,
                          gd_bitrot_op_list[type]);
                ret = -1;
                goto out;
        }

        if (type == GF_BITROT_CMD_SCRUB_STATUS) {
                /* Backward compatibility handling for scrub status command*/
                if (conf->op_version < GD_OP_VERSION_3_7_7) {
                        snprintf (msg, sizeof (msg), "Cannot execute command. "
                                  "The cluster is operating at version %d. "
                                  "Bitrot scrub status command unavailable in "
                                  "this version", conf->op_version);
                        ret = -1;
                        goto out;
                }

                ret = dict_get_str (dict, "scrub-value", &scrub);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Failed to get scrub value.");
                        ret = -1;
                        goto out;
                }

                if (!strncmp (scrub, "status", strlen ("status"))) {
                        ret = glusterd_op_begin_synctask (req,
                                                          GD_OP_SCRUB_STATUS,
                                                          dict);
                        goto out;
                }
        }

        if (type == GF_BITROT_CMD_SCRUB_ONDEMAND) {
                /* Backward compatibility handling for scrub status command*/
                if (conf->op_version < GD_OP_VERSION_3_9_0) {
                        snprintf (msg, sizeof (msg), "Cannot execute command. "
                                  "The cluster is operating at version %d. "
                                  "Bitrot scrub ondemand command unavailable in "
                                  "this version", conf->op_version);
                        ret = -1;
                        goto out;
                }

                ret = dict_get_str (dict, "scrub-value", &scrub);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Failed to get scrub value.");
                        ret = -1;
                        goto out;
                }

                if (!strncmp (scrub, "ondemand", strlen ("ondemand"))) {
                        ret = glusterd_op_begin_synctask (req,
                                                          GD_OP_SCRUB_ONDEMAND,
                                                          dict);
                        goto out;
                }
        }

        ret = glusterd_op_begin_synctask (req, GD_OP_BITROT, dict);

out:
        if (ret) {
                if (msg[0] == '\0')
                        snprintf (msg, sizeof (msg), "Bitrot operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, msg);
        }

        return ret;
}

int
glusterd_handle_bitrot (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_bitrot);
}

static int
glusterd_bitrot_scrub_throttle (glusterd_volinfo_t *volinfo, dict_t *dict,
                                char *key, char **op_errstr)
{
        int32_t        ret                  = -1;
        char           *scrub_throttle      = NULL;
        char           *option              = NULL;
        xlator_t       *this                = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "scrub-throttle-value", &scrub_throttle);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to fetch scrub-"
                        "throttle value");
                goto out;
        }

        option = gf_strdup (scrub_throttle);
        ret = dict_set_dynstr (volinfo->dict, key, option);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "Failed to set option %s",
                        key);
                goto out;
        }

        ret = glusterd_scrubsvc_reconfigure ();
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SCRUBSVC_RECONF_FAIL,
                        "Failed to reconfigure scrub "
                        "services");
                goto out;
        }

out:
        return ret;
}

static int
glusterd_bitrot_scrub_freq (glusterd_volinfo_t *volinfo, dict_t *dict,
                            char *key, char **op_errstr)
{
        int32_t        ret                  = -1;
        char           *scrub_freq          = NULL;
        xlator_t       *this                = NULL;
        char           *option              = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "scrub-frequency-value", &scrub_freq);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to fetch scrub-"
                        "freq value");
                goto out;
        }

        option = gf_strdup (scrub_freq);
        ret = dict_set_dynstr (volinfo->dict, key, option);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "Failed to set option %s",
                        key);
                goto out;
        }

        ret = glusterd_scrubsvc_reconfigure ();
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SCRUBSVC_RECONF_FAIL,
                        "Failed to reconfigure scrub "
                        "services");
                goto out;
        }

out:
        return ret;
}

static int
glusterd_bitrot_scrub (glusterd_volinfo_t *volinfo, dict_t *dict,
                       char *key, char **op_errstr)
{
        int32_t        ret                  = -1;
        char           *scrub_value         = NULL;
        xlator_t       *this                = NULL;
        char           *option              = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "scrub-value", &scrub_value);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to fetch scrub"
                        "value");
                goto out;
        }

        if (!strcmp (scrub_value, "resume")) {
                option = gf_strdup ("Active");
        } else {
                option = gf_strdup (scrub_value);
        }

        ret = dict_set_dynstr (volinfo->dict, key, option);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "Failed to set option %s",
                        key);
                goto out;
        }

        ret = glusterd_scrubsvc_reconfigure ();
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SCRUBSVC_RECONF_FAIL,
                        "Failed to reconfigure scrub "
                        "services");
                goto out;
        }

out:
        return ret;
}

static int
glusterd_bitrot_expiry_time (glusterd_volinfo_t *volinfo, dict_t *dict,
                             char *key, char **op_errstr)
{
        int32_t        ret                  = -1;
        uint32_t       expiry_time          = 0;
        xlator_t       *this                = NULL;
        char           dkey[1024]           = {0,};

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_uint32 (dict, "expiry-time", &expiry_time);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get bitrot expiry"
                        " timer value.");
                goto out;
        }

        snprintf (dkey, sizeof (dkey), "%d", expiry_time);

        ret = dict_set_dynstr_with_alloc (volinfo->dict, key, dkey);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "Failed to set option %s",
                        key);
                goto out;
        }

        ret = glusterd_bitdsvc_reconfigure ();
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BITDSVC_RECONF_FAIL,
                        "Failed to reconfigure bitrot"
                         "services");
                goto out;
        }
out:
        return ret;
}

static int
glusterd_bitrot_enable (glusterd_volinfo_t *volinfo, char **op_errstr)
{
        int32_t         ret             = -1;
        xlator_t        *this           = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errstr, out);

        if (glusterd_is_volume_started (volinfo) == 0) {
                *op_errstr = gf_strdup ("Volume is stopped, start volume "
                                        "to enable bitrot.");
                ret = -1;
                goto out;
        }

        ret = glusterd_is_bitrot_enabled (volinfo);
        if (ret) {
                *op_errstr = gf_strdup ("Bitrot is already enabled");
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (volinfo->dict, VKEY_FEATURES_BITROT,
                                          "on");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "dict set failed");
                goto out;
        }

        /*Once bitrot is enable scrubber should be in Active state*/
        ret = dict_set_dynstr_with_alloc (volinfo->dict, "features.scrub",
                                          "Active");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "Failed to set option "
                        "features.scrub value");
                goto out;
        }

        ret = 0;
out:
        if (ret && op_errstr && !*op_errstr)
                gf_asprintf (op_errstr, "Enabling bitrot on volume %s has been "
                             "unsuccessful", volinfo->volname);
        return ret;
}

static int
glusterd_bitrot_disable (glusterd_volinfo_t *volinfo, char **op_errstr)
{
        int32_t           ret            = -1;
        xlator_t          *this          = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errstr, out);

        ret = dict_set_dynstr_with_alloc (volinfo->dict, VKEY_FEATURES_BITROT,
                                          "off");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "dict set failed");
                goto out;
        }

        /*Once bitrot disabled scrubber should be Inactive state*/
        ret = dict_set_dynstr_with_alloc (volinfo->dict, "features.scrub",
                                          "Inactive");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "Failed to set "
                        "features.scrub value");
                goto out;
        }

        ret = 0;
out:
        if (ret && op_errstr && !*op_errstr)
                gf_asprintf (op_errstr, "Disabling bitrot on volume %s has "
                             "been unsuccessful", volinfo->volname);
        return ret;
}

gf_boolean_t
glusterd_should_i_stop_bitd ()
{
        glusterd_conf_t       *conf       = THIS->private;
        glusterd_volinfo_t    *volinfo    = NULL;
        gf_boolean_t           stopped    = _gf_true;
        glusterd_brickinfo_t  *brickinfo  = NULL;
        xlator_t              *this       = NULL;

        this = THIS;
        GF_ASSERT (this);

        cds_list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                if (!glusterd_is_bitrot_enabled (volinfo))
                        continue;
                else if (volinfo->status != GLUSTERD_STATUS_STARTED)
                        continue;
                else {
                        cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                                 brick_list) {
                                if (!glusterd_is_local_brick (this, volinfo,
                                                              brickinfo))
                                        continue;
                                stopped = _gf_false;
                                return stopped;
                        }

                        /* Before stoping bitrot/scrubber daemon check
                         * other volume also whether respective volume
                         * host a brick from this node or not.*/
                        continue;
                }
        }

        return stopped;
}

static int
glusterd_manage_bitrot (int opcode)
{
        int              ret   = -1;
        xlator_t         *this = NULL;
        glusterd_conf_t  *priv = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        switch (opcode) {
        case GF_BITROT_OPTION_TYPE_ENABLE:
        case GF_BITROT_OPTION_TYPE_DISABLE:
                ret = priv->bitd_svc.manager (&(priv->bitd_svc),
                                                NULL, PROC_START_NO_WAIT);
                if (ret)
                        break;
                ret = priv->scrub_svc.manager (&(priv->scrub_svc), NULL,
                                               PROC_START_NO_WAIT);
                break;
        default:
                ret = 0;
                break;
        }

        return ret;

}

int
glusterd_op_bitrot (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        glusterd_volinfo_t     *volinfo      = NULL;
        int32_t                 ret          = -1;
        char                   *volname      = NULL;
        int                     type         = -1;
        glusterd_conf_t        *priv         = NULL;
        xlator_t               *this         = NULL;

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

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

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get type from "
                        "dict");
                goto out;
        }

        switch (type) {
        case GF_BITROT_OPTION_TYPE_ENABLE:
                ret = glusterd_bitrot_enable (volinfo, op_errstr);
                if (ret < 0)
                        goto out;
                break;

        case GF_BITROT_OPTION_TYPE_DISABLE:
                ret = glusterd_bitrot_disable (volinfo, op_errstr);
                if (ret < 0)
                        goto out;

                break;

        case GF_BITROT_OPTION_TYPE_SCRUB_THROTTLE:
                ret = glusterd_bitrot_scrub_throttle (volinfo, dict,
                                                      "features.scrub-throttle",
                                                      op_errstr);
                if (ret)
                        goto out;
                break;

        case GF_BITROT_OPTION_TYPE_SCRUB_FREQ:
                ret = glusterd_bitrot_scrub_freq (volinfo, dict,
                                                  "features.scrub-freq",
                                                  op_errstr);
                if (ret)
                        goto out;
                break;

        case GF_BITROT_OPTION_TYPE_SCRUB:
                ret = glusterd_bitrot_scrub (volinfo, dict, "features.scrub",
                                             op_errstr);
                if (ret)
                        goto out;
                break;

        case GF_BITROT_OPTION_TYPE_EXPIRY_TIME:
                ret = glusterd_bitrot_expiry_time (volinfo, dict,
                                                   "features.expiry-time",
                                                   op_errstr);
                if (ret)
                        goto out;
        case GF_BITROT_CMD_SCRUB_STATUS:
        case GF_BITROT_CMD_SCRUB_ONDEMAND:
                break;

        default:
                gf_asprintf (op_errstr, "Bitrot command failed. Invalid "
                             "opcode");
                ret = -1;
                goto out;
        }

        ret = glusterd_manage_bitrot (type);
        if (ret)
                goto out;

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL, "Unable to re-create "
                        "volfiles");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed to store volinfo for "
                        "bitrot");
                goto out;
        }

out:
        return ret;
}

int
glusterd_op_stage_bitrot (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        int                  ret                      = 0;
        char                *volname                  = NULL;
        char                *scrub_cmd                = NULL;
        char                *scrub_cmd_from_dict      = NULL;
        char                 msg[2048]                = {0,};
        int                  type                     = 0;
        xlator_t            *this                     = NULL;
        glusterd_conf_t     *priv                     = NULL;
        glusterd_volinfo_t  *volinfo                  = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

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

        if (!glusterd_is_volume_started (volinfo)) {
                *op_errstr = gf_strdup ("Volume is stopped, start volume "
                                        "before executing bit rot command.");
                ret = -1;
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get type for "
                        "operation");

                *op_errstr = gf_strdup ("Staging stage failed for bitrot "
                                        "operation.");
                goto out;
        }


        if ((GF_BITROT_OPTION_TYPE_ENABLE != type) &&
            (glusterd_is_bitrot_enabled (volinfo) == 0)) {
                ret = -1;
                gf_asprintf (op_errstr, "Bitrot is not enabled on volume %s",
                             volname);
                goto out;
        }

        if ((GF_BITROT_OPTION_TYPE_SCRUB == type)) {
                ret = dict_get_str (volinfo->dict, "features.scrub",
                                    &scrub_cmd_from_dict);
                if (!ret) {
                        ret = dict_get_str (dict, "scrub-value", &scrub_cmd);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        GD_MSG_DICT_GET_FAILED, "Unable to "
                                        "get scrub-value");
                                *op_errstr = gf_strdup ("Staging failed for "
                                                        "bitrot operation. "
                                                        "Please check log file"
                                                        " for more details.");
                                goto out;
                        }
                        /* If scrubber is resume then value of scrubber will be
                         * "Active" in the dictionary. */
                        if (!strcmp (scrub_cmd_from_dict, scrub_cmd) ||
                            (!strncmp ("Active", scrub_cmd_from_dict,
                                       strlen("Active")) && !strncmp ("resume",
                                       scrub_cmd, strlen("resume")))) {
                                snprintf (msg, sizeof (msg), "Scrub is already"
                                          " %sd for volume %s", scrub_cmd,
                                          volinfo->volname);
                                *op_errstr = gf_strdup (msg);
                                ret = -1;
                                goto out;
                        }
                }
                ret = 0;
        }

 out:
        if (ret && op_errstr && *op_errstr)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_STAGE_BITROT_FAIL, "%s", *op_errstr);
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}
