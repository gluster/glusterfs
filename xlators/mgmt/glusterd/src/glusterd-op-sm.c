/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
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
#include <time.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/mount.h>

#include <libgen.h>
#include "uuid.h"

#include "fnmatch.h"
#include "xlator.h"
#include "protocol-common.h"
#include "glusterd.h"
#include "call-stub.h"
#include "defaults.h"
#include "list.h"
#include "dict.h"
#include "compat.h"
#include "compat-errno.h"
#include "statedump.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include "glusterd-hooks.h"
#include "glusterd-volgen.h"
#include "syscall.h"
#include "cli1-xdr.h"
#include "common-utils.h"
#include "run.h"

#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>

#define ALL_VOLUME_OPTION_CHECK(volname, key, ret, op_errstr, label)           \
        do {                                                                   \
                gf_boolean_t    _all = !strcmp ("all", volname);               \
                gf_boolean_t    _ratio = !strcmp (key,                         \
                                                  GLUSTERD_QUORUM_RATIO_KEY);  \
                if (_all && !_ratio) {                                         \
                        ret = -1;                                              \
                        *op_errstr = gf_strdup ("Not a valid option for all "  \
                                                "volumes");                    \
                        goto label;                                            \
                } else if (!_all && _ratio) {                                  \
                        ret = -1;                                              \
                        *op_errstr = gf_strdup ("Not a valid option for "      \
                                                "single volume");              \
                        goto label;                                            \
                }                                                              \
         } while (0)

static struct list_head gd_op_sm_queue;
pthread_mutex_t       gd_op_sm_lock;
glusterd_op_info_t    opinfo = {{0},};
static int glusterfs_port = GLUSTERD_DEFAULT_PORT;
static char *glusterd_op_sm_state_names[] = {
        "Default",
        "Lock sent",
        "Locked",
        "Stage op sent",
        "Staged",
        "Commit op sent",
        "Committed",
        "Unlock sent",
        "Stage op failed",
        "Commit op failed",
        "Brick op sent",
        "Brick op failed",
        "Brick op Committed",
        "Brick op Commit failed",
        "Ack drain",
        "Invalid",
};

static char *glusterd_op_sm_event_names[] = {
        "GD_OP_EVENT_NONE",
        "GD_OP_EVENT_START_LOCK",
        "GD_OP_EVENT_LOCK",
        "GD_OP_EVENT_RCVD_ACC",
        "GD_OP_EVENT_ALL_ACC",
        "GD_OP_EVENT_STAGE_ACC",
        "GD_OP_EVENT_COMMIT_ACC",
        "GD_OP_EVENT_RCVD_RJT",
        "GD_OP_EVENT_STAGE_OP",
        "GD_OP_EVENT_COMMIT_OP",
        "GD_OP_EVENT_UNLOCK",
        "GD_OP_EVENT_START_UNLOCK",
        "GD_OP_EVENT_ALL_ACK",
        "GD_OP_EVENT_LOCAL_UNLOCK_NO_RESP",
        "GD_OP_EVENT_INVALID"
};

extern struct volopt_map_entry glusterd_volopt_map[];

char*
glusterd_op_sm_state_name_get (int state)
{
        if (state < 0 || state >= GD_OP_STATE_MAX)
                return glusterd_op_sm_state_names[GD_OP_STATE_MAX];
        return glusterd_op_sm_state_names[state];
}

char*
glusterd_op_sm_event_name_get (int event)
{
        if (event < 0 || event >= GD_OP_EVENT_MAX)
                return glusterd_op_sm_event_names[GD_OP_EVENT_MAX];
        return glusterd_op_sm_event_names[event];
}

void
glusterd_destroy_lock_ctx (glusterd_op_lock_ctx_t *ctx)
{
        if (!ctx)
                return;
        GF_FREE (ctx);
}

void
glusterd_set_volume_status (glusterd_volinfo_t  *volinfo,
                            glusterd_volume_status status)
{
        GF_ASSERT (volinfo);
        volinfo->status = status;
}

gf_boolean_t
glusterd_is_volume_started (glusterd_volinfo_t  *volinfo)
{
        GF_ASSERT (volinfo);
        return (volinfo->status == GLUSTERD_STATUS_STARTED);
}

static int
glusterd_op_sm_inject_all_acc ()
{
        int32_t                 ret = -1;
        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_ALL_ACC, NULL);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
glusterd_check_quota_cmd (char *key, char *value, char *errstr, size_t size)
{
        int                ret = -1;
        gf_boolean_t       b   = _gf_false;

        if ((strcmp (key, "quota") == 0) ||
           (strcmp (key, "features.quota") == 0)) {
                ret = gf_string2boolean (value, &b);
                if (ret)
                        goto out;
                if (b) {
                          snprintf (errstr, size," 'gluster "
                                    "volume set <VOLNAME> %s %s' is "
                                    "deprecated. Use 'gluster volume "
                                    "quota <VOLNAME> enable' instead.",
                                     key, value);
                          ret = -1;
                          goto out;
                } else {
                          snprintf (errstr, size, " 'gluster "
                                    "volume set <VOLNAME> %s %s' is "
                                    "deprecated. Use 'gluster volume "
                                    "quota <VOLNAME> disable' instead.",
                                     key, value);
                          ret = -1;
                          goto out;
                }
        }
        ret = 0;
out:
        return ret;
}

int
glusterd_brick_op_build_payload (glusterd_op_t op, glusterd_brickinfo_t *brickinfo,
                                 gd1_mgmt_brick_op_req **req, dict_t *dict)
{
        int                     ret = -1;
        gd1_mgmt_brick_op_req   *brick_req = NULL;
        char                    *volname = NULL;
        char                    name[1024] = {0,};
        gf_xl_afr_op_t          heal_op = GF_AFR_OP_INVALID;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);
        GF_ASSERT (req);


        switch (op) {
        case GD_OP_REMOVE_BRICK:
        case GD_OP_STOP_VOLUME:
                brick_req = GF_CALLOC (1, sizeof (*brick_req),
                                       gf_gld_mt_mop_brick_req_t);
                if (!brick_req)
                        goto out;
                brick_req->op = GLUSTERD_BRICK_TERMINATE;
                brick_req->name = "";
        break;
        case GD_OP_PROFILE_VOLUME:
                brick_req = GF_CALLOC (1, sizeof (*brick_req),
                                       gf_gld_mt_mop_brick_req_t);

                if (!brick_req)
                        goto out;

                brick_req->op = GLUSTERD_BRICK_XLATOR_INFO;
                brick_req->name = brickinfo->path;

                break;
        case GD_OP_HEAL_VOLUME:
        {
                brick_req = GF_CALLOC (1, sizeof (*brick_req),
                                       gf_gld_mt_mop_brick_req_t);
                if (!brick_req)
                        goto out;

                brick_req->op = GLUSTERD_BRICK_XLATOR_OP;
                brick_req->name = "";
                ret = dict_get_int32 (dict, "heal-op", (int32_t*)&heal_op);
                if (ret)
                        goto out;
                ret = dict_set_int32 (dict, "xl-op", heal_op);
        }
                break;
        case GD_OP_STATUS_VOLUME:
        {
                brick_req = GF_CALLOC (1, sizeof (*brick_req),
                                       gf_gld_mt_mop_brick_req_t);
                if (!brick_req)
                        goto out;
                brick_req->op = GLUSTERD_BRICK_STATUS;
                brick_req->name = "";
        }
                break;
        case GD_OP_REBALANCE:
        case GD_OP_DEFRAG_BRICK_VOLUME:
                brick_req = GF_CALLOC (1, sizeof (*brick_req),
                                       gf_gld_mt_mop_brick_req_t);
                if (!brick_req)
                        goto out;

                brick_req->op = GLUSTERD_BRICK_XLATOR_DEFRAG;
                ret = dict_get_str (dict, "volname", &volname);
                if (ret)
                        goto out;
                snprintf (name, 1024, "%s-dht",volname);
                brick_req->name = gf_strdup (name);

                break;

        default:
                goto out;
        break;
        }

        ret = dict_allocate_and_serialize (dict, &brick_req->input.input_val,
                                           &brick_req->input.input_len);
        if (ret)
                goto out;
        *req = brick_req;
        ret = 0;

out:
        if (ret && brick_req)
                GF_FREE (brick_req);
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_node_op_build_payload (glusterd_op_t op, gd1_mgmt_brick_op_req **req,
                                dict_t *dict)
{
        int                     ret = -1;
        gd1_mgmt_brick_op_req   *brick_req = NULL;

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);
        GF_ASSERT (req);

        switch (op) {
        case GD_OP_PROFILE_VOLUME:
                brick_req = GF_CALLOC (1, sizeof (*brick_req),
                                       gf_gld_mt_mop_brick_req_t);
                if (!brick_req)
                        goto out;

                brick_req->op = GLUSTERD_NODE_PROFILE;
                brick_req->name = "";

                break;

        case GD_OP_STATUS_VOLUME:
                brick_req = GF_CALLOC (1, sizeof (*brick_req),
                                       gf_gld_mt_mop_brick_req_t);
                if (!brick_req)
                        goto out;

                brick_req->op = GLUSTERD_NODE_STATUS;
                brick_req->name = "";

                break;

        default:
                goto out;
        }

        ret = dict_allocate_and_serialize (dict, &brick_req->input.input_val,
                                           &brick_req->input.input_len);

        if (ret)
                goto out;

        *req = brick_req;
        ret = 0;

out:
        if (ret && brick_req)
                GF_FREE (brick_req);
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
glusterd_validate_quorum_options (xlator_t *this, char *fullkey, char *value,
                                  char **op_errstr)
{
        int             ret = 0;
        char            *key = NULL;
        volume_option_t *opt = NULL;

        if (!glusterd_is_quorum_option (fullkey))
                goto out;
        key = strchr (fullkey, '.');
        key++;
        opt = xlator_volume_option_get (this, key);
        ret = xlator_option_validate (this, key, value, opt, op_errstr);
out:
        return ret;
}

static int
glusterd_check_client_op_version_support (char *volname, uint32_t op_version,
                                          char **op_errstr)
{
        int                     ret = 0;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        rpc_transport_t         *xprt = NULL;

        this = THIS;
        GF_ASSERT(this);
        priv = this->private;
        GF_ASSERT(priv);

        pthread_mutex_lock (&priv->xprt_lock);
        list_for_each_entry (xprt, &priv->xprt_list, list) {
                if ((!strcmp(volname, xprt->peerinfo.volname)) &&
                    ((op_version > xprt->peerinfo.max_op_version) ||
                     (op_version < xprt->peerinfo.min_op_version))) {
                        ret = -1;
                        break;
                }
        }
        pthread_mutex_unlock (&priv->xprt_lock);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "One or more clients "
                        "don't support the required op-version");
                ret = gf_asprintf (op_errstr, "One or more connected clients "
                                   "cannot support the feature being set. "
                                   "These clients need to be upgraded or "
                                   "disconnected before running this command"
                                   " again");
                return -1;
        }
        return 0;
}

static int
glusterd_op_stage_set_volume (dict_t *dict, char **op_errstr)
{
        int                             ret                     = -1;
        char                            *volname                = NULL;
        int                             exists                  = 0;
        char                            *key                    = NULL;
        char                            *key_fixed              = NULL;
        char                            *value                  = NULL;
        char                            str[100]                = {0, };
        int                             count                   = 0;
        int                             dict_count              = 0;
        char                            errstr[2048]            = {0, };
        glusterd_volinfo_t              *volinfo                = NULL;
        dict_t                          *val_dict               = NULL;
        gf_boolean_t                    global_opt              = _gf_false;
        glusterd_volinfo_t              *voliter                = NULL;
        glusterd_conf_t                 *priv                   = NULL;
        xlator_t                        *this                   = NULL;
        uint32_t                        new_op_version          = 0;
        uint32_t                        local_new_op_version    = 0;
        uint32_t                        key_op_version          = 0;
        uint32_t                        local_key_op_version    = 0;
        gf_boolean_t                    origin_glusterd         = _gf_true;
        gf_boolean_t                    check_op_version        = _gf_true;
        gf_boolean_t                    all_vol                 = _gf_false;
        struct volopt_map_entry         *vme                    = NULL;

        GF_ASSERT (dict);
        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        val_dict = dict_new();
        if (!val_dict)
                goto out;

        /* Check if we can support the required op-version
         * This check is not done on the originator glusterd. The originator
         * glusterd sets this value.
         */
        origin_glusterd = is_origin_glusterd ();

        if (!origin_glusterd) {
                /* Check for v3.3.x origin glusterd */
                check_op_version = dict_get_str_boolean (dict,
                                                         "check-op-version",
                                                         _gf_false);

                if (check_op_version) {
                        ret = dict_get_uint32 (dict, "new-op-version",
                                               &new_op_version);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get new_op_version");
                                goto out;
                        }

                        if ((new_op_version > GD_OP_VERSION_MAX) ||
                            (new_op_version < GD_OP_VERSION_MIN)) {
                                ret = -1;
                                snprintf (errstr, sizeof (errstr),
                                          "Required op_version (%d) is not "
                                          "supported", new_op_version);
                                gf_log (this->name, GF_LOG_ERROR, "%s", errstr);
                                goto out;
                        }
                }
        }

        ret = dict_get_int32 (dict, "count", &dict_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Count(dict),not set in Volume-Set");
                goto out;
        }

        if (dict_count == 0) {
        /*No options would be specified of volume set help */
                if (dict_get (dict, "help" ))  {
                        ret = 0;
                        goto out;
                }

                if (dict_get (dict, "help-xml" )) {
#if (HAVE_LIB_XML)
                        ret = 0;
                        goto out;
#else
                        ret  = -1;
                        gf_log (this->name, GF_LOG_ERROR,
                                "libxml not present in the system");
                        *op_errstr = gf_strdup ("Error: xml libraries not "
                                                "present to produce xml-output");
                        goto out;
#endif
                }
                gf_log (this->name, GF_LOG_ERROR, "No options received ");
                *op_errstr = gf_strdup ("Options not specified");
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        if (strcasecmp (volname, "all") != 0) {
                exists = glusterd_check_volume_exists (volname);
                if (!exists) {
                        snprintf (errstr, sizeof (errstr),
                                  FMTSTR_CHECK_VOL_EXISTS, volname);
                        gf_log (this->name, GF_LOG_ERROR, "%s", errstr);
                        ret = -1;
                        goto out;
                }

                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                FMTSTR_CHECK_VOL_EXISTS, volname);
                        goto out;
                }

                ret = glusterd_validate_volume_id (dict, volinfo);
                if (ret)
                        goto out;
        } else {
                all_vol = _gf_true;
        }

        local_new_op_version = priv->op_version;

        for ( count = 1; ret != 1 ; count++ ) {
                global_opt = _gf_false;
                sprintf (str, "key%d", count);
                ret = dict_get_str (dict, str, &key);
                if (ret)
                        break;

                sprintf (str, "value%d", count);
                ret = dict_get_str (dict, str, &value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "invalid key,value pair in 'volume set'");
                        ret = -1;
                        goto out;
                }

                if (strcmp (key, "config.memory-accounting") == 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "enabling memory accounting for volume %s",
                                volname);
                        ret = 0;
                }

                if (strcmp (key, "config.transport") == 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "changing transport-type for volume %s",
                                volname);
                        ret = 0;
                        /* if value is none of 'tcp/rdma/tcp,rdma' error out */
                        if (!((strcasecmp (value, "rdma") == 0) ||
                              (strcasecmp (value, "tcp") == 0) ||
                              (strcasecmp (value, "tcp,rdma") == 0) ||
                              (strcasecmp (value, "rdma,tcp") == 0))) {
                                ret = snprintf (errstr, sizeof (errstr),
                                                "transport-type %s does "
                                                "not exist", value);
                                /* lets not bother about above return value,
                                   its a failure anyways */
                                ret = -1;
                                goto out;
                        }
                }

                ret = glusterd_check_quota_cmd (key, value, errstr, sizeof (errstr));
                if (ret)
                        goto out;

                if (is_key_glusterd_hooks_friendly (key))
                        continue;

                for (vme = &glusterd_volopt_map[0]; vme->key; vme++) {
                        if ((vme->validate_fn) &&
                            ((!strcmp (key, vme->key)) ||
                             (!strcmp (key, strchr (vme->key, '.') + 1)))) {
                                ret = vme->validate_fn (dict, key, value,
                                                        op_errstr);
                                if (ret)
                                        goto out;
                                break;
                        }
                }

                exists = glusterd_check_option_exists (key, &key_fixed);
                if (exists == -1) {
                        ret = -1;
                        goto out;
                }

                if (!exists) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Option with name: %s does not exist", key);
                        ret = snprintf (errstr, sizeof (errstr),
                                       "option : %s does not exist",
                                       key);
                        if (key_fixed)
                                snprintf (errstr + ret, sizeof (errstr) - ret,
                                          "\nDid you mean %s?", key_fixed);
                        ret = -1;
                        goto out;
                }

                if (key_fixed)
                        key = key_fixed;
                ALL_VOLUME_OPTION_CHECK (volname, key, ret, op_errstr, out);
                ret = glusterd_validate_quorum_options (this, key, value,
                                                        op_errstr);
                if (ret)
                        goto out;

                local_key_op_version = glusterd_get_op_version_for_key (key);
                if (local_key_op_version > local_new_op_version)
                        local_new_op_version = local_key_op_version;

                sprintf (str, "op-version%d", count);
                if (origin_glusterd) {
                        ret = dict_set_uint32 (dict, str, local_key_op_version);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set key-op-version in dict");
                                goto out;
                        }
                } else if (check_op_version) {
                        ret = dict_get_uint32 (dict, str, &key_op_version);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get key-op-version from"
                                        " dict");
                                goto out;
                        }
                        if (local_key_op_version != key_op_version) {
                                ret = -1;
                                snprintf (errstr, sizeof (errstr),
                                          "option: %s op-version mismatch",
                                          key);
                                gf_log (this->name, GF_LOG_ERROR,
                                        "%s, required op-version = %"PRIu32", "
                                        "available op-version = %"PRIu32,
                                        errstr, key_op_version,
                                        local_key_op_version);
                                goto out;
                        }
                }

                if (glusterd_check_globaloption (key))
                        global_opt = _gf_true;

                ret = dict_set_str (val_dict, key, value);

                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to set the options in 'volume set'");
                        ret = -1;
                        goto out;
                }

                *op_errstr = NULL;
                if (!global_opt && !all_vol)
                        ret = glusterd_validate_reconfopts (volinfo, val_dict, op_errstr);
                else if (!all_vol) {
                        voliter = NULL;
                        list_for_each_entry (voliter, &priv->volumes, vol_list) {
                                ret = glusterd_validate_globalopts (voliter, val_dict, op_errstr);
                                if (ret)
                                        break;
                        }
                }

                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Could not create "
                                "temp volfile, some option failed: %s",
                                *op_errstr);
                        goto out;
                }
                dict_del (val_dict, key);

                if (key_fixed) {
                        GF_FREE (key_fixed);
                        key_fixed = NULL;
                }
        }

        // Check if all the connected clients support the new op-version
        ret = glusterd_check_client_op_version_support (volname,
                                                        local_new_op_version,
                                                        op_errstr);
        if (ret)
                goto out;

        if (origin_glusterd) {
                ret = dict_set_uint32 (dict, "new-op-version",
                                       local_new_op_version);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to set new-op-version in dict");
                        goto out;
                }
                /* Set this value in dict so other peers know to check for
                 * op-version. This is a hack for 3.3.x compatibility
                 *
                 * TODO: Remove this and the other places this is referred once
                 * 3.3.x compatibility is not required
                 */
                ret = dict_set_uint32 (dict, "check-op-version",
                                       _gf_true);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to set check-op-version in dict");
                        goto out;
                }
        }

        ret = 0;

out:
        if (val_dict)
                dict_unref (val_dict);

        GF_FREE (key_fixed);
        if (errstr[0] != '\0')
                *op_errstr = gf_strdup (errstr);

        if (ret) {
                if (!(*op_errstr)) {
                        *op_errstr = gf_strdup ("Error, Validation Failed");
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Error, Cannot Validate option :%s",
                                *op_errstr);
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Error, Cannot Validate option");
                }
        }
        return ret;
}

static int
glusterd_op_stage_reset_volume (dict_t *dict, char **op_errstr)
{
        int                                      ret           = 0;
        char                                    *volname       = NULL;
        gf_boolean_t                             exists        = _gf_false;
        char                                    msg[2048]      = {0};
        char                                    *key = NULL;
        char                                    *key_fixed = NULL;
        glusterd_volinfo_t                      *volinfo       = NULL;
        xlator_t                                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        if (strcasecmp (volname, "all") != 0) {
                exists = glusterd_check_volume_exists (volname);
                if (!exists) {
                        snprintf (msg, sizeof (msg), FMTSTR_CHECK_VOL_EXISTS,
                                  volname);
                        ret = -1;
                        goto out;
                }
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        snprintf (msg, sizeof (msg), FMTSTR_CHECK_VOL_EXISTS,
                                  volname);
                        goto out;
                }

                ret = glusterd_validate_volume_id (dict, volinfo);
                if (ret)
                        goto out;
        }

        ret = dict_get_str (dict, "key", &key);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get option key");
                goto out;
        }
        if (strcmp(key, "all")) {
                exists = glusterd_check_option_exists (key, &key_fixed);
                if (exists == -1) {
                        ret = -1;
                        goto out;
                }
                if (!exists) {
                        ret = snprintf (msg, sizeof (msg),
                                        "Option %s does not exist", key);
                        if (key_fixed)
                                snprintf (msg + ret, sizeof (msg) - ret,
                                          "\nDid you mean %s?", key_fixed);
                        ret = -1;
                        goto out;
                } else if (exists > 0) {
                        if (key_fixed)
                                key = key_fixed;
                        ALL_VOLUME_OPTION_CHECK (volname, key, ret,
                                                 op_errstr, out);
                }
        }

out:
        GF_FREE (key_fixed);

        if (msg[0] != '\0') {
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
        }

        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}



static int
glusterd_op_stage_sync_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        char                                    *hostname = NULL;
        gf_boolean_t                            exists = _gf_false;
        glusterd_peerinfo_t                     *peerinfo = NULL;
        char                                    msg[2048] = {0,};
        glusterd_volinfo_t                      *volinfo  = NULL;

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret) {
                snprintf (msg, sizeof (msg), "hostname couldn't be "
                          "retrieved from msg");
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        if (gf_is_local_addr (hostname)) {
                //volname is not present in case of sync all
                ret = dict_get_str (dict, "volname", &volname);
                if (!ret) {
                        exists = glusterd_check_volume_exists (volname);
                        if (!exists) {
                                snprintf (msg, sizeof (msg), "Volume %s "
                                         "does not exist", volname);
                                *op_errstr = gf_strdup (msg);
                                ret = -1;
                                goto out;
                        }
                        ret = glusterd_volinfo_find (volname, &volinfo);
                        if (ret)
                                goto out;

                } else {
                        ret = 0;
                }
         } else {
                ret = glusterd_friend_find (NULL, hostname, &peerinfo);
                if (ret) {
                        snprintf (msg, sizeof (msg), "%s, is not a friend",
                                  hostname);
                        *op_errstr = gf_strdup (msg);
                        goto out;
                }

                if (!peerinfo->connected) {
                        snprintf (msg, sizeof (msg), "%s, is not connected at "
                                  "the moment", hostname);
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }

        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_stage_status_volume (dict_t *dict, char **op_errstr)
{
        int                    ret            = -1;
        uint32_t               cmd            = 0;
        char                   msg[2048]      = {0,};
        char                  *volname        = NULL;
        char                  *brick          = NULL;
        xlator_t              *this           = NULL;
        glusterd_conf_t       *priv           = NULL;
        glusterd_brickinfo_t  *brickinfo      = NULL;
        glusterd_volinfo_t    *volinfo        = NULL;
        dict_t                *vol_opts       = NULL;
        gf_boolean_t           nfs_disabled   = _gf_false;
        gf_boolean_t           shd_enabled    = _gf_true;

        GF_ASSERT (dict);
        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT(priv);

        ret = dict_get_uint32 (dict, "cmd", &cmd);
        if (ret)
                goto out;

        if (cmd & GF_CLI_STATUS_ALL)
                goto out;

        if ((cmd & GF_CLI_STATUS_QUOTAD) &&
            (priv->op_version == GD_OP_VERSION_MIN)) {
                snprintf (msg, sizeof (msg), "The cluster is operating at "
                          "version 1. Getting the status of quotad is not "
                          "allowed in this state.");
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof(msg), FMTSTR_CHECK_VOL_EXISTS, volname);
                ret = -1;
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        ret = glusterd_is_volume_started (volinfo);
        if (!ret) {
                snprintf (msg, sizeof (msg), "Volume %s is not started",
                          volname);
                ret = -1;
                goto out;
        }

        vol_opts = volinfo->dict;

        if ((cmd & GF_CLI_STATUS_NFS) != 0) {
                nfs_disabled = dict_get_str_boolean (vol_opts, "nfs.disable",
                                                     _gf_false);
                if (nfs_disabled) {
                        ret = -1;
                        snprintf (msg, sizeof (msg),
                                  "NFS server is disabled for volume %s",
                                  volname);
                        goto out;
                }
        } else if ((cmd & GF_CLI_STATUS_SHD) != 0) {
                if (!glusterd_is_volume_replicate (volinfo)) {
                        ret = -1;
                        snprintf (msg, sizeof (msg),
                                  "Volume %s is not of type replicate",
                                  volname);
                        goto out;
                }

                shd_enabled = dict_get_str_boolean (vol_opts,
                                                    "cluster.self-heal-daemon",
                                                    _gf_true);
                if (!shd_enabled) {
                        ret = -1;
                        snprintf (msg, sizeof (msg),
                                  "Self-heal Daemon is disabled for volume %s",
                                  volname);
                        goto out;
                }
        } else if ((cmd & GF_CLI_STATUS_QUOTAD) != 0) {
                if (!glusterd_is_volume_quota_enabled (volinfo)) {
                        ret = -1;
                        snprintf (msg, sizeof (msg), "Volume %s does not have "
                                  "quota enabled", volname);
                        goto out;
                }
        } else if ((cmd & GF_CLI_STATUS_BRICK) != 0) {
                ret = dict_get_str (dict, "brick", &brick);
                if (ret)
                        goto out;

                ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                              &brickinfo);
                if (ret) {
                        snprintf (msg, sizeof(msg), "No brick %s in"
                                  " volume %s", brick, volname);
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;

 out:
        if (ret) {
                if (msg[0] != '\0')
                        *op_errstr = gf_strdup (msg);
                else
                        *op_errstr = gf_strdup ("Validation Failed for Status");
        }

        gf_log (this->name, GF_LOG_DEBUG, "Returning: %d", ret);
        return ret;
}


static gf_boolean_t
glusterd_is_profile_on (glusterd_volinfo_t *volinfo)
{
        int                                     ret = -1;
        gf_boolean_t                            is_latency_on = _gf_false;
        gf_boolean_t                            is_fd_stats_on = _gf_false;

        GF_ASSERT (volinfo);

        ret = glusterd_volinfo_get_boolean (volinfo, VKEY_DIAG_CNT_FOP_HITS);
        if (ret != -1)
                is_fd_stats_on = ret;
        ret = glusterd_volinfo_get_boolean (volinfo, VKEY_DIAG_LAT_MEASUREMENT);
        if (ret != -1)
                is_latency_on = ret;
        if ((_gf_true == is_latency_on) &&
            (_gf_true == is_fd_stats_on))
                return _gf_true;
        return _gf_false;
}

static int
glusterd_op_stage_stats_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        gf_boolean_t                            exists = _gf_false;
        char                                    msg[2048] = {0,};
        int32_t                                 stats_op = GF_CLI_STATS_NONE;
        glusterd_volinfo_t                      *volinfo = NULL;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (msg, sizeof (msg), "Volume name get failed");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if ((!exists) || (ret < 0)) {
                snprintf (msg, sizeof (msg), "Volume %s, "
                         "doesn't exist", volname);
                ret = -1;
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "op", &stats_op);
        if (ret) {
                snprintf (msg, sizeof (msg), "Volume profile op get failed");
                goto out;
        }

        if (GF_CLI_STATS_START == stats_op) {
                if (_gf_true == glusterd_is_profile_on (volinfo)) {
                        snprintf (msg, sizeof (msg), "Profile on Volume %s is"
                                  " already started", volinfo->volname);
                        ret = -1;
                        goto out;
                }

        }
        if ((GF_CLI_STATS_STOP == stats_op) ||
            (GF_CLI_STATS_INFO == stats_op)) {
                if (_gf_false == glusterd_is_profile_on (volinfo)) {
                        snprintf (msg, sizeof (msg), "Profile on Volume %s is"
                                  " not started", volinfo->volname);
                        ret = -1;

                        goto out;
                }
        }
        if ((GF_CLI_STATS_TOP == stats_op) ||
            (GF_CLI_STATS_INFO == stats_op)) {
                if (_gf_false == glusterd_is_volume_started (volinfo)) {
                        snprintf (msg, sizeof (msg), "Volume %s is not started.",
                                  volinfo->volname);
                        gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);
                        ret = -1;
                        goto out;
                }
        }
        ret = 0;
out:
        if (msg[0] != '\0') {
                gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
        }
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


static int
_delete_reconfig_opt (dict_t *this, char *key, data_t *value, void *data)
{
        int32_t        *is_force = 0;

        GF_ASSERT (data);
        is_force = (int32_t*)data;

        if (*is_force != 1) {
                if (_gf_true == glusterd_check_voloption_flags (key,
                                                         OPT_FLAG_FORCE)) {
                /* indicate to caller that we don't set the option
                 * due to being protected
                 */
                        *is_force = *is_force | GD_OP_PROTECTED;
                        goto out;
                } else {
                        *is_force = *is_force | GD_OP_UNPROTECTED;
                }
        }

        gf_log ("", GF_LOG_DEBUG, "deleting dict with key=%s,value=%s",
                key, value->data);
        dict_del (this, key);
out:
        return 0;
}

static int
_delete_reconfig_global_opt (dict_t *this, char *key, data_t *value, void *data)
{
        int32_t        *is_force = 0;

        GF_ASSERT (data);
        is_force = (int32_t*)data;

        if (strcmp (GLUSTERD_GLOBAL_OPT_VERSION, key) == 0)
                goto out;

        _delete_reconfig_opt (this, key, value, data);
out:
        return 0;
}

static int
glusterd_options_reset (glusterd_volinfo_t *volinfo, char *key,
                        int32_t *is_force)
{
        int                      ret = 0;
        data_t                  *value = NULL;
        char                    *key_fixed = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (volinfo->dict);
        GF_ASSERT (key);

        if (!strncmp(key, "all", 3))
                dict_foreach (volinfo->dict, _delete_reconfig_opt, is_force);
        else {
                value = dict_get (volinfo->dict, key);
                if (!value) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "no value set for option %s", key);
                        goto out;
                }
                _delete_reconfig_opt (volinfo->dict, key, value, is_force);
        }

        gd_update_volume_op_versions (volinfo);

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to create volfile for"
                        " 'volume reset'");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                ret = glusterd_nodesvcs_handle_reconfigure (volinfo);
                if (ret)
                        goto out;
        }

        ret = 0;

out:
        GF_FREE (key_fixed);
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
glusterd_op_reset_all_volume_options (xlator_t *this, dict_t *dict)
{
        char            *key            = NULL;
        char            *key_fixed      = NULL;
        int             ret             = -1;
        int32_t         is_force        = 0;
        glusterd_conf_t *conf           = NULL;
        dict_t          *dup_opt        = NULL;
        gf_boolean_t    all             = _gf_false;
        char            *next_version   = NULL;
        gf_boolean_t    quorum_action   = _gf_false;

        conf = this->private;
        ret = dict_get_str (dict, "key", &key);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get key");
                goto out;
        }

        ret = dict_get_int32 (dict, "force", &is_force);
        if (ret)
                is_force = 0;

        if (strcmp (key, "all")) {
                ret = glusterd_check_option_exists (key, &key_fixed);
                if (ret <= 0) {
                        gf_log (this->name, GF_LOG_ERROR, "Option %s does not "
                                "exist", key);
                        ret = -1;
                        goto out;
                }
        } else {
                all = _gf_true;
        }

        if (key_fixed)
                key = key_fixed;

        ret = -1;
        dup_opt = dict_new ();
        if (!dup_opt)
                goto out;
        if (!all) {
                dict_copy (conf->opts, dup_opt);
                dict_del (dup_opt, key);
        }
        ret = glusterd_get_next_global_opt_version_str (conf->opts,
                                                        &next_version);
        if (ret)
                goto out;

        ret = dict_set_str (dup_opt, GLUSTERD_GLOBAL_OPT_VERSION, next_version);
        if (ret)
                goto out;

        ret = glusterd_store_options (this, dup_opt);
        if (ret)
                goto out;

        if (glusterd_is_quorum_changed (conf->opts, key, NULL))
                quorum_action = _gf_true;

        ret = dict_set_dynstr (conf->opts, GLUSTERD_GLOBAL_OPT_VERSION,
                               next_version);
        if (ret)
                goto out;
        else
                next_version = NULL;

        if (!all) {
                dict_del (conf->opts, key);
        } else {
                dict_foreach (conf->opts, _delete_reconfig_global_opt,
                              &is_force);
        }
out:
        GF_FREE (key_fixed);
        if (dup_opt)
                dict_unref (dup_opt);

        gf_log (this->name, GF_LOG_DEBUG, "returning %d", ret);
        if (quorum_action)
                glusterd_do_quorum_action ();
        GF_FREE (next_version);
        return ret;
}

static int
glusterd_op_reset_volume (dict_t *dict, char **op_rspstr)
{
        glusterd_volinfo_t      *volinfo    = NULL;
        int                     ret         = -1;
        char                    *volname    = NULL;
        char                    *key        = NULL;
        char                    *key_fixed  = NULL;
        int32_t                 is_force    = 0;
        gf_boolean_t            quorum_action = _gf_false;
        xlator_t                *this         = NULL;

        this = THIS;
        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name" );
                goto out;
        }

        if (strcasecmp (volname, "all") == 0) {
                ret = glusterd_op_reset_all_volume_options (this, dict);
                goto out;
        }

        ret = dict_get_int32 (dict, "force", &is_force);
        if (ret)
                is_force = 0;

        ret = dict_get_str (dict, "key", &key);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get option key");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                goto out;
        }

        if (strcmp (key, "all") &&
            glusterd_check_option_exists (key, &key_fixed) != 1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "volinfo dict inconsistency: option %s not found",
                        key);
                ret = -1;
                goto out;
        }
        if (key_fixed)
                key = key_fixed;

        if (glusterd_is_quorum_changed (volinfo->dict, key, NULL))
                quorum_action = _gf_true;

        ret = glusterd_options_reset (volinfo, key, &is_force);
        if (ret == -1) {
                gf_asprintf(op_rspstr, "Volume reset : failed");
        } else if (is_force & GD_OP_PROTECTED) {
                if (is_force & GD_OP_UNPROTECTED) {
                        gf_asprintf (op_rspstr, "All unprotected fields were"
                                     " reset. To reset the protected fields,"
                                     " use 'force'.");
                } else {
                        ret = -1;
                        gf_asprintf (op_rspstr, "'%s' is protected. To reset"
                                     " use 'force'.", key);
                }
        }

out:
        GF_FREE (key_fixed);
        if (quorum_action)
                glusterd_do_quorum_action ();

        gf_log (this->name, GF_LOG_DEBUG, "'volume reset' returning %d", ret);
        return ret;
}

int
glusterd_stop_bricks (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t                    *brickinfo = NULL;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
            /*TODO: Need to change @del_brick in brick_stop to _gf_true
             * once we enable synctask in peer rpc prog */
                if (glusterd_brick_stop (volinfo, brickinfo, _gf_false))
                        return -1;
        }

        return 0;
}

int
glusterd_start_bricks (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t                    *brickinfo = NULL;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (glusterd_brick_start (volinfo, brickinfo, _gf_false))
                        return -1;
        }

        return 0;
}

static int
glusterd_op_set_all_volume_options (xlator_t *this, dict_t *dict)
{
        char            *key            = NULL;
        char            *key_fixed      = NULL;
        char            *value          = NULL;
        char            *dup_value      = NULL;
        int             ret             = -1;
        glusterd_conf_t *conf           = NULL;
        dict_t          *dup_opt        = NULL;
        char            *next_version   = NULL;
        gf_boolean_t    quorum_action   = _gf_false;

        conf = this->private;
        ret = dict_get_str (dict, "key1", &key);
        if (ret)
                goto out;

        ret = dict_get_str (dict, "value1", &value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "invalid key,value pair in 'volume set'");
                goto out;
        }
        ret = glusterd_check_option_exists (key, &key_fixed);
        if (ret <= 0) {
                gf_log (this->name, GF_LOG_ERROR, "Invalid key %s", key);
                ret = -1;
                goto out;
        }

        if (key_fixed)
                key = key_fixed;

        ret = -1;
        dup_opt = dict_new ();
        if (!dup_opt)
                goto out;
        dict_copy (conf->opts, dup_opt);
        ret = dict_set_str (dup_opt, key, value);
        if (ret)
                goto out;

        ret = glusterd_get_next_global_opt_version_str (conf->opts,
                                                        &next_version);
        if (ret)
                goto out;

        ret = dict_set_str (dup_opt, GLUSTERD_GLOBAL_OPT_VERSION, next_version);
        if (ret)
                goto out;

        dup_value = gf_strdup (value);
        if (!dup_value)
                goto out;

        ret = glusterd_store_options (this, dup_opt);
        if (ret)
                goto out;

        if (glusterd_is_quorum_changed (conf->opts, key, value))
                quorum_action = _gf_true;

        ret = dict_set_dynstr (conf->opts, GLUSTERD_GLOBAL_OPT_VERSION,
                               next_version);
        if (ret)
                goto out;
        else
                next_version = NULL;

        ret = dict_set_dynstr (conf->opts, key, dup_value);
        if (ret)
                goto out;
out:
        GF_FREE (key_fixed);
        if (dup_opt)
                dict_unref (dup_opt);

        gf_log (this->name, GF_LOG_DEBUG, "returning %d", ret);
        if (quorum_action)
                glusterd_do_quorum_action ();
        GF_FREE (next_version);
        return ret;
}

static int
glusterd_op_set_volume (dict_t *dict)
{
        int                                      ret = 0;
        glusterd_volinfo_t                      *volinfo = NULL;
        char                                    *volname = NULL;
        xlator_t                                *this = NULL;
        glusterd_conf_t                         *priv = NULL;
        int                                      count = 1;
        char                                    *key = NULL;
        char                                    *key_fixed = NULL;
        char                                    *value = NULL;
        char                                     str[50] = {0, };
        char                                    *op_errstr = NULL;
        gf_boolean_t                             global_opt    = _gf_false;
        gf_boolean_t                             global_opts_set = _gf_false;
        glusterd_volinfo_t                      *voliter = NULL;
        int32_t                                  dict_count = 0;
        gf_boolean_t                             check_op_version = _gf_false;
        uint32_t                                 new_op_version = 0;
        gf_boolean_t                            quorum_action  = _gf_false;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_int32 (dict, "count", &dict_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Count(dict),not set in Volume-Set");
                goto out;
        }

        if (dict_count == 0) {
                ret = glusterd_volset_help (NULL, &op_errstr);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "%s", 
                                       (op_errstr)? op_errstr:
                                       "Volume set help internal error");
                }

                GF_FREE(op_errstr);
                goto out;
         }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        if (strcasecmp (volname, "all") == 0) {
                ret = glusterd_op_set_all_volume_options (this, dict);
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                goto out;
        }

        // TODO: Remove this once v3.3 compatability is not required
        check_op_version = dict_get_str_boolean (dict, "check-op-version",
                                                 _gf_false);

        if (check_op_version) {
                ret = dict_get_uint32 (dict, "new-op-version", &new_op_version);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to get new op-version from dict");
                        goto out;
                }
        }

        for (count = 1; ret != -1 ; count++) {

                sprintf (str, "key%d", count);
                ret = dict_get_str (dict, str, &key);
                if (ret)
                        break;

                sprintf (str, "value%d", count);
                ret = dict_get_str (dict, str, &value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "invalid key,value pair in 'volume set'");
                        ret = -1;
                        goto out;
                }

                if (strcmp (key, "config.memory-accounting") == 0) {
                        ret = gf_string2boolean (value,
                                                 &volinfo->memory_accounting);
                }

                if (strcmp (key, "config.transport") == 0) {
                        gf_log (this->name, GF_LOG_INFO,
                                "changing transport-type for volume %s to %s",
                                volname, value);
                        ret = 0;
                        if (strcasecmp (value, "rdma") == 0) {
                                volinfo->transport_type = GF_TRANSPORT_RDMA;
                        } else if (strcasecmp (value, "tcp") == 0) {
                                volinfo->transport_type = GF_TRANSPORT_TCP;
                        } else if ((strcasecmp (value, "tcp,rdma") == 0) ||
                                   (strcasecmp (value, "rdma,tcp") == 0)) {
                                volinfo->transport_type =
                                        GF_TRANSPORT_BOTH_TCP_RDMA;
                        } else {
                                ret = -1;
                                goto out;
                        }
                }

                if (!is_key_glusterd_hooks_friendly (key)) {
                        ret = glusterd_check_option_exists (key, &key_fixed);
                        GF_ASSERT (ret);
                        if (ret <= 0) {
                                key_fixed = NULL;
                                goto out;
                        }
                }

                global_opt = _gf_false;
                if (glusterd_check_globaloption (key)) {
                        global_opt = _gf_true;
                        global_opts_set = _gf_true;
                }

                if (!global_opt)
                        value = gf_strdup (value);

                if (!value) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to set the options in 'volume set'");
                        ret = -1;
                        goto out;
                }

                if (key_fixed)
                        key = key_fixed;

                if (glusterd_is_quorum_changed (volinfo->dict, key, value))
                        quorum_action = _gf_true;

                if (global_opt) {
                       list_for_each_entry (voliter, &priv->volumes, vol_list) {
                               value = gf_strdup (value);
                               ret = dict_set_dynstr (voliter->dict, key, value);
                               if (ret)
                                       goto out;
                       }
                } else {
                        ret = dict_set_dynstr (volinfo->dict, key, value);
                        if (ret)
                                goto out;
                }

                if (key_fixed) {
                        GF_FREE (key_fixed);
                        key_fixed = NULL;
                }
        }

        if (count == 1) {
                gf_log (this->name, GF_LOG_ERROR, "No options received ");
                ret = -1;
                goto out;
        }

        /* Update the cluster op-version before regenerating volfiles so that
         * correct volfiles are generated
         */
        if (new_op_version > priv->op_version) {
                priv->op_version = new_op_version;
                ret = glusterd_store_global_info (this);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to store op-version");
                        goto out;
                }
        }

        if (!global_opts_set) {
                gd_update_volume_op_versions (volinfo);
                ret = glusterd_create_volfiles_and_notify_services (volinfo);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to create volfile for"
                                " 'volume set'");
                        ret = -1;
                        goto out;
                }

                ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret)
                        goto out;

                if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                        ret = glusterd_nodesvcs_handle_reconfigure (volinfo);
                        if (ret) {
                                gf_log (this->name, GF_LOG_WARNING,
                                         "Unable to restart NFS-Server");
                                goto out;
                        }
                }

        } else {
                list_for_each_entry (voliter, &priv->volumes, vol_list) {
                        volinfo = voliter;
                        gd_update_volume_op_versions (volinfo);
                        ret = glusterd_create_volfiles_and_notify_services (volinfo);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Unable to create volfile for"
                                        " 'volume set'");
                                ret = -1;
                                goto out;
                        }

                        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                        if (ret)
                                goto out;

                        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                                ret = glusterd_nodesvcs_handle_reconfigure (volinfo);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_WARNING,
                                                "Unable to restart NFS-Server");
                                        goto out;
                                }
                        }
                }
        }

 out:
        GF_FREE (key_fixed);
        gf_log (this->name, GF_LOG_DEBUG, "returning %d", ret);
        if (quorum_action)
                glusterd_do_quorum_action ();
        return ret;
}


static int
glusterd_op_sync_volume (dict_t *dict, char **op_errstr,
                         dict_t *rsp_dict)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        char                                    *hostname = NULL;
        char                                    msg[2048] = {0,};
        int                                     count = 1;
        int                                     vol_count = 0;
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        xlator_t                                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret) {
                snprintf (msg, sizeof (msg), "hostname couldn't be "
                          "retrieved from msg");
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        if (!gf_is_local_addr (hostname)) {
                ret = 0;
                goto out;
        }

        //volname is not present in case of sync all
        ret = dict_get_str (dict, "volname", &volname);
        if (!ret) {
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Volume with name: %s "
                                "not exists", volname);
                        goto out;
                }
        }

        if (!rsp_dict) {
                //this should happen only on source
                ret = 0;
                goto out;
        }

        if (volname) {
                ret = glusterd_add_volume_to_dict (volinfo, rsp_dict,
                                                   1);
                vol_count = 1;
        } else {
                list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                        ret = glusterd_add_volume_to_dict (volinfo,
                                                           rsp_dict, count);
                        if (ret)
                                goto out;

                        vol_count = count++;
                }
        }
        ret = dict_set_int32 (rsp_dict, "count", vol_count);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_add_profile_volume_options (glusterd_volinfo_t *volinfo)
{
        int                                     ret = -1;
        char                                    *latency_key = NULL;
        char                                    *fd_stats_key = NULL;

        GF_ASSERT (volinfo);

        latency_key = VKEY_DIAG_LAT_MEASUREMENT;
        fd_stats_key = VKEY_DIAG_CNT_FOP_HITS;

        ret = dict_set_str (volinfo->dict, latency_key, "on");
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "failed to set the volume %s "
                        "option %s value %s",
                        volinfo->volname, latency_key, "on");
                goto out;
        }

        ret = dict_set_str (volinfo->dict, fd_stats_key, "on");
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "failed to set the volume %s "
                        "option %s value %s",
                        volinfo->volname, fd_stats_key, "on");
                goto out;
        }
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static void
glusterd_remove_profile_volume_options (glusterd_volinfo_t *volinfo)
{
        char                                    *latency_key = NULL;
        char                                    *fd_stats_key = NULL;

        GF_ASSERT (volinfo);

        latency_key = VKEY_DIAG_LAT_MEASUREMENT;
        fd_stats_key = VKEY_DIAG_CNT_FOP_HITS;
        dict_del (volinfo->dict, latency_key);
        dict_del (volinfo->dict, fd_stats_key);
}

static int
glusterd_op_stats_volume (dict_t *dict, char **op_errstr,
                          dict_t *rsp_dict)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        char                                    msg[2048] = {0,};
        glusterd_volinfo_t                      *volinfo = NULL;
        int32_t                                 stats_op = GF_CLI_STATS_NONE;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "volume name get failed");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), "Volume %s does not exists",
                          volname);

                gf_log ("", GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        ret = dict_get_int32 (dict, "op", &stats_op);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "volume profile op get failed");
                goto out;
        }

        switch (stats_op) {
        case GF_CLI_STATS_START:
                ret = glusterd_add_profile_volume_options (volinfo);
                if (ret)
                        goto out;
                break;
        case GF_CLI_STATS_STOP:
                glusterd_remove_profile_volume_options (volinfo);
                break;
        case GF_CLI_STATS_INFO:
        case GF_CLI_STATS_TOP:
                //info is already collected in brick op.
                //just goto out;
                ret = 0;
                goto out;
                break;
        default:
                GF_ASSERT (0);
                gf_log ("glusterd", GF_LOG_ERROR, "Invalid profile op: %d",
                        stats_op);
                ret = -1;
                goto out;
                break;
        }
        ret = glusterd_create_volfiles_and_notify_services (volinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to create volfile for"
                                          " 'volume set'");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_nodesvcs_handle_reconfigure (volinfo);

        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
_add_brick_name_to_dict (dict_t *dict, char *key, glusterd_brickinfo_t *brick)
{
        int     ret = -1;
        char    tmp[1024] = {0,};
        char    *brickname = NULL;
        xlator_t *this = NULL;

        GF_ASSERT (dict);
        GF_ASSERT (key);
        GF_ASSERT (brick);

        this = THIS;
        GF_ASSERT (this);

        snprintf (tmp, sizeof (tmp), "%s:%s", brick->hostname, brick->path);
        brickname = gf_strdup (tmp);
        if (!brickname) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to dup brick name");
                goto out;
        }

        ret = dict_set_dynstr (dict, key, brickname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to add brick name to dict");
                goto out;
        }
        brickname = NULL;
out:
        if (brickname)
                GF_FREE (brickname);
        return ret;
}

static int
_add_remove_bricks_to_dict (dict_t *dict, glusterd_volinfo_t *volinfo,
                            char *prefix)
{
        int             ret = -1;
        int             count = 0;
        int             i = 0;
        char            brick_key[1024] = {0,};
        char            dict_key[1024] ={0,};
        char            *brick = NULL;
        xlator_t        *this = NULL;

        GF_ASSERT (dict);
        GF_ASSERT (volinfo);
        GF_ASSERT (prefix);

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_int32 (volinfo->rebal.dict, "count", &count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to get brick count");
                goto out;
        }

        snprintf (dict_key, sizeof (dict_key), "%s.count", prefix);
        ret = dict_set_int32 (dict, dict_key, count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set brick count in dict");
                goto out;
        }

        for (i = 1; i <= count; i++) {
                memset (brick_key, 0, sizeof (brick_key));
                snprintf (brick_key, sizeof (brick_key), "brick%d", i);

                ret = dict_get_str (volinfo->rebal.dict, brick_key, &brick);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to get %s", brick_key);
                        goto out;
                }

                memset (dict_key, 0, sizeof (dict_key));
                snprintf (dict_key, sizeof (dict_key), "%s.%s", prefix,
                          brick_key);
                ret = dict_set_str (dict, dict_key, brick);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to add brick to dict");
                        goto out;
                }
                brick = NULL;
        }

out:
        return ret;
}

/* This adds the respective task-id and all available parameters of a task into
 * a dictionary
 */
static int
_add_task_to_dict (dict_t *dict, glusterd_volinfo_t *volinfo, int op, int index)
{

        int             ret = -1;
        char            key[128] = {0,};
        char            *uuid_str = NULL;
        int             status = 0;
        xlator_t        *this = NULL;

        GF_ASSERT (dict);
        GF_ASSERT (volinfo);

        this = THIS;
        GF_ASSERT (this);

        switch (op) {
        case GD_OP_REMOVE_BRICK:
                snprintf (key, sizeof (key), "task%d", index);
                ret = _add_remove_bricks_to_dict (dict, volinfo, key);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to add remove bricks to dict");
                        goto out;
                }
        case GD_OP_REBALANCE:
                uuid_str = gf_strdup (uuid_utoa (volinfo->rebal.rebalance_id));
                status = volinfo->rebal.defrag_status;
                break;

        case GD_OP_REPLACE_BRICK:
                snprintf (key, sizeof (key), "task%d.src-brick", index);
                ret = _add_brick_name_to_dict (dict, key,
                                               volinfo->rep_brick.src_brick);
                if (ret)
                        goto out;
                memset (key, 0, sizeof (key));

                snprintf (key, sizeof (key), "task%d.dst-brick", index);
                ret = _add_brick_name_to_dict (dict, key,
                                               volinfo->rep_brick.dst_brick);
                if (ret)
                        goto out;
                memset (key, 0, sizeof (key));

                uuid_str = gf_strdup (uuid_utoa (volinfo->rep_brick.rb_id));
                status = volinfo->rep_brick.rb_status;
                break;

        default:
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "%s operation doesn't have a"
                        " task_id", gd_op_list[op]);
                goto out;
        }

        snprintf (key, sizeof (key), "task%d.type", index);
        ret = dict_set_str (dict, key, (char *)gd_op_list[op]);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error setting task type in dict");
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "task%d.id", index);

        if (!uuid_str)
                goto out;
        ret = dict_set_dynstr (dict, key, uuid_str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error setting task id in dict");
                goto out;
        }
        uuid_str = NULL;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "task%d.status", index);
        ret = dict_set_int32 (dict, key, status);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error setting task status in dict");
                goto out;
        }

out:
        if (uuid_str)
                GF_FREE (uuid_str);
        return ret;
}

static int
glusterd_aggregate_task_status (dict_t *rsp_dict, glusterd_volinfo_t *volinfo)
{
        int        ret   = -1;
        int        tasks = 0;
        xlator_t  *this  = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (!uuid_is_null (volinfo->rebal.rebalance_id)) {
                ret = _add_task_to_dict (rsp_dict, volinfo, volinfo->rebal.op,
                                         tasks);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to add task details to dict");
                        goto out;
                }
                tasks++;
        }

        if (!uuid_is_null (volinfo->rep_brick.rb_id)) {
                ret = _add_task_to_dict (rsp_dict, volinfo, GD_OP_REPLACE_BRICK,
                                         tasks);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to add task details to dict");
                        goto out;
                }
                tasks++;
        }

        ret = dict_set_int32 (rsp_dict, "tasks", tasks);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error setting tasks count in dict");
                goto out;
        }
        ret = 0;

out:
        return ret;
}

static int
glusterd_op_status_volume (dict_t *dict, char **op_errstr,
                           dict_t *rsp_dict)
{
        int                     ret             = -1;
        int                     node_count      = 0;
        int                     brick_index     = -1;
        int                     other_count     = 0;
        int                     other_index     = 0;
        uint32_t                cmd             = 0;
        char                   *volname         = NULL;
        char                   *brick           = NULL;
        xlator_t               *this            = NULL;
        glusterd_volinfo_t     *volinfo         = NULL;
        glusterd_brickinfo_t   *brickinfo       = NULL;
        glusterd_conf_t        *priv            = NULL;
        dict_t                 *vol_opts        = NULL;
        gf_boolean_t            nfs_disabled    = _gf_false;
        gf_boolean_t            shd_enabled     = _gf_true;
        gf_boolean_t            origin_glusterd = _gf_false;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);

        GF_ASSERT (dict);

        origin_glusterd = is_origin_glusterd ();

        ret = dict_get_uint32 (dict, "cmd", &cmd);
        if (ret)
                goto out;

        if (origin_glusterd) {
                ret = 0;
                if ((cmd & GF_CLI_STATUS_ALL)) {
                        ret = glusterd_get_all_volnames (rsp_dict);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "failed to get all volume "
                                        "names for status");
                }
        }

        ret = dict_set_uint32 (rsp_dict, "cmd", cmd);
        if (ret)
                goto out;

        if (cmd & GF_CLI_STATUS_ALL)
                goto out;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Volume with name: %s "
                        "does not exist", volname);
                goto out;
        }
        vol_opts = volinfo->dict;

        if ((cmd & GF_CLI_STATUS_NFS) != 0) {
                ret = glusterd_add_node_to_dict ("nfs", rsp_dict, 0, vol_opts);
                if (ret)
                        goto out;
                other_count++;
                node_count++;

        } else if ((cmd & GF_CLI_STATUS_SHD) != 0) {
                ret = glusterd_add_node_to_dict ("glustershd", rsp_dict, 0,
                                                 vol_opts);
                if (ret)
                        goto out;
                other_count++;
                node_count++;

        } else if ((cmd & GF_CLI_STATUS_QUOTAD) != 0) {
                ret = glusterd_add_node_to_dict ("quotad", rsp_dict, 0,
                                                 vol_opts);
                if (ret)
                        goto out;
                other_count++;
                node_count++;

        } else if ((cmd & GF_CLI_STATUS_BRICK) != 0) {
                ret = dict_get_str (dict, "brick", &brick);
                if (ret)
                        goto out;

                ret = glusterd_volume_brickinfo_get_by_brick (brick,
                                                              volinfo,
                                                              &brickinfo);
                if (ret)
                        goto out;

                if (uuid_compare (brickinfo->uuid, MY_UUID))
                        goto out;

                glusterd_add_brick_to_dict (volinfo, brickinfo, rsp_dict,
                                            ++brick_index);
                if (cmd & GF_CLI_STATUS_DETAIL)
                        glusterd_add_brick_detail_to_dict (volinfo, brickinfo,
                                                           rsp_dict,
                                                           brick_index);
                node_count++;

        } else if ((cmd & GF_CLI_STATUS_TASKS) != 0) {
                ret = glusterd_aggregate_task_status (rsp_dict, volinfo);
                goto out;

        } else {
                list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                        brick_index++;
                        if (uuid_compare (brickinfo->uuid, MY_UUID))
                                continue;

                        glusterd_add_brick_to_dict (volinfo, brickinfo,
                                                    rsp_dict, brick_index);

                        if (cmd & GF_CLI_STATUS_DETAIL) {
                                glusterd_add_brick_detail_to_dict (volinfo,
                                                                   brickinfo,
                                                                   rsp_dict,
                                                                   brick_index);
                        }
                        node_count++;
                }

                if ((cmd & GF_CLI_STATUS_MASK) == GF_CLI_STATUS_NONE) {
                        other_index = brick_index + 1;

                        nfs_disabled = dict_get_str_boolean (vol_opts,
                                                             "nfs.disable",
                                                             _gf_false);
                        if (!nfs_disabled) {
                                ret = glusterd_add_node_to_dict ("nfs",
                                                                 rsp_dict,
                                                                 other_index,
                                                                 vol_opts);
                                if (ret)
                                        goto out;
                                other_index++;
                                other_count++;
                                node_count++;
                        }

                        shd_enabled = dict_get_str_boolean
                                        (vol_opts, "cluster.self-heal-daemon",
                                         _gf_true);
                        if (glusterd_is_volume_replicate (volinfo)
                            && shd_enabled) {
                                ret = glusterd_add_node_to_dict ("glustershd",
                                                                 rsp_dict,
                                                                 other_index,
                                                                 vol_opts);
                                if (ret)
                                        goto out;
                                other_count++;
                                node_count++;
                                other_index++;
                        }
                        if (glusterd_is_volume_quota_enabled (volinfo)) {
                                ret = glusterd_add_node_to_dict ("quotad",
                                                                 rsp_dict,
                                                                 other_index,
                                                                 vol_opts);
                                if (ret)
                                        goto out;
                                other_count++;
                                node_count++;
                        }
                }
        }

        ret = dict_set_int32 (rsp_dict, "brick-index-max", brick_index);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error setting brick-index-max to dict");
                goto out;
        }
        ret = dict_set_int32 (rsp_dict, "other-count", other_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error setting other-count to dict");
                goto out;
        }
        ret = dict_set_int32 (rsp_dict, "count", node_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error setting node count to dict");
                goto out;
        }

        /* Active tasks */
        /* Tasks are added only for normal volume status request for either a
         * single volume or all volumes
         */
        if (!glusterd_status_has_tasks (cmd))
                goto out;

        ret = glusterd_aggregate_task_status (rsp_dict, volinfo);
        if (ret)
                goto out;
        ret = 0;

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_ac_none (glusterd_op_sm_event_t *event, void *ctx)
{
        int ret = 0;

        gf_log (THIS->name, GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_op_ac_send_lock (glusterd_op_sm_event_t *event, void *ctx)
{
        int                   ret      = 0;
        rpc_clnt_procedure_t *proc     = NULL;
        glusterd_conf_t      *priv     = NULL;
        xlator_t             *this     = NULL;
        glusterd_peerinfo_t  *peerinfo = NULL;
        uint32_t             pending_count = 0;

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (!peerinfo->connected || !peerinfo->mgmt)
                        continue;
                if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
                    (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
                        continue;

                proc = &peerinfo->mgmt->proctable[GLUSTERD_MGMT_CLUSTER_LOCK];
                if (proc->fn) {
                        ret = proc->fn (NULL, this, peerinfo);
                        if (ret) {
                                gf_log (this->name, GF_LOG_WARNING, "Failed to "
                                        "send lock request for operation "
                                        "'Volume %s' to peer %s",
                                        gd_op_list[opinfo.op],
                                        peerinfo->hostname);
                                continue;
                        }
                        pending_count++;
                }
        }

        opinfo.pending_count = pending_count;
        if (!opinfo.pending_count)
                ret = glusterd_op_sm_inject_all_acc ();

        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_op_ac_send_unlock (glusterd_op_sm_event_t *event, void *ctx)
{
        int                   ret      = 0;
        rpc_clnt_procedure_t *proc     = NULL;
        glusterd_conf_t      *priv     = NULL;
        xlator_t             *this     = NULL;
        glusterd_peerinfo_t  *peerinfo = NULL;
        uint32_t             pending_count = 0;

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);

        /*ret = glusterd_unlock (MY_UUID);

        if (ret)
                goto out;
        */

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (!peerinfo->connected || !peerinfo->mgmt)
                        continue;
                if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
                    (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
                        continue;

                proc = &peerinfo->mgmt->proctable[GLUSTERD_MGMT_CLUSTER_UNLOCK];
                if (proc->fn) {
                        ret = proc->fn (NULL, this, peerinfo);
                        if (ret) {
                                gf_log (this->name, GF_LOG_WARNING, "Failed to "
                                        "send unlock request for operation "
                                        "'Volume %s' to peer %s",
                                        gd_op_list[opinfo.op],
                                        peerinfo->hostname);
                                continue;
                        }
                        pending_count++;
                }
        }

        opinfo.pending_count = pending_count;
        if (!opinfo.pending_count)
                ret = glusterd_op_sm_inject_all_acc ();

        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;

}

static int
glusterd_op_ac_ack_drain (glusterd_op_sm_event_t *event, void *ctx)
{
        int ret = 0;

        if (opinfo.pending_count > 0)
                opinfo.pending_count--;

        if (!opinfo.pending_count)
                ret = glusterd_op_sm_inject_event (GD_OP_EVENT_ALL_ACK, NULL);

        gf_log (THIS->name, GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_op_ac_send_unlock_drain (glusterd_op_sm_event_t *event, void *ctx)
{
        return glusterd_op_ac_ack_drain (event, ctx);
}

static int
glusterd_op_ac_lock (glusterd_op_sm_event_t *event, void *ctx)
{
        glusterd_op_lock_ctx_t   *lock_ctx = NULL;
        int32_t                  ret = 0;

        GF_ASSERT (event);
        GF_ASSERT (ctx);

        lock_ctx = (glusterd_op_lock_ctx_t *)ctx;

        ret = glusterd_lock (lock_ctx->uuid);

        gf_log (THIS->name, GF_LOG_DEBUG, "Lock Returned %d", ret);

        glusterd_op_lock_send_resp (lock_ctx->req, ret);

        return ret;
}

static int
glusterd_op_ac_unlock (glusterd_op_sm_event_t *event, void *ctx)
{
        int                      ret = 0;
        glusterd_op_lock_ctx_t   *lock_ctx = NULL;
        xlator_t                 *this = NULL;
        glusterd_conf_t          *priv = NULL;

        GF_ASSERT (event);
        GF_ASSERT (ctx);

        this = THIS;
        priv = this->private;
        lock_ctx = (glusterd_op_lock_ctx_t *)ctx;

        ret = glusterd_unlock (lock_ctx->uuid);

        gf_log (this->name, GF_LOG_DEBUG, "Unlock Returned %d", ret);

        glusterd_op_unlock_send_resp (lock_ctx->req, ret);

        if (priv->pending_quorum_action)
                glusterd_do_quorum_action ();
        return ret;
}

static int
glusterd_op_ac_local_unlock (glusterd_op_sm_event_t *event, void *ctx)
{
        int              ret          = 0;
        uuid_t          *originator   = NULL;

        GF_ASSERT (event);
        GF_ASSERT (ctx);

        originator = (uuid_t *) ctx;

        ret = glusterd_unlock (*originator);

        gf_log (THIS->name, GF_LOG_DEBUG, "Unlock Returned %d", ret);

        return ret;
}

static int
glusterd_op_ac_rcvd_lock_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;

        GF_ASSERT (event);

        if (opinfo.pending_count > 0)
                opinfo.pending_count--;

        if (opinfo.pending_count > 0)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_ALL_ACC, NULL);

        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

out:
        return ret;
}

static int
glusterd_dict_set_volid (dict_t *dict, char *volname, char **op_errstr)
{
        int                     ret = -1;
        glusterd_volinfo_t      *volinfo = NULL;
        char                    *volid = NULL;
        char                    msg[1024] = {0,};
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (!dict || !volname)
                goto out;

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), FMTSTR_CHECK_VOL_EXISTS, volname);
                goto out;
        }
        volid = gf_strdup (uuid_utoa (volinfo->volume_id));
        if (!volid) {
                ret = -1;
                goto out;
        }
        ret = dict_set_dynstr (dict, "vol-id", volid);
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to set volume id of volume"
                          " %s", volname);
                goto out;
        }
out:
        if (msg[0] != '\0') {
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
        }
        return ret;
}

int
glusterd_op_build_payload (dict_t **req, char **op_errstr, dict_t *op_ctx)
{
        int                     ret = -1;
        void                    *ctx = NULL;
        dict_t                  *dict = NULL;
        dict_t                  *req_dict = NULL;
        glusterd_op_t           op = GD_OP_NONE;
        char                    *volname = NULL;
        uint32_t                status_cmd = GF_CLI_STATUS_NONE;
        char                    *errstr = NULL;
        xlator_t                *this = NULL;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);

        req_dict = dict_new ();
        if (!req_dict)
                goto out;

        if (!op_ctx) {
                op  = glusterd_op_get_op ();
                ctx = (void*)glusterd_op_get_ctx ();
                if (!ctx) {
                        gf_log (this->name, GF_LOG_ERROR, "Null Context for "
                                "op %d", op);
                        ret = -1;
                        goto out;
                }

        } else {
#define GD_SYNC_OPCODE_KEY "sync-mgmt-operation"
                ret = dict_get_int32 (op_ctx, GD_SYNC_OPCODE_KEY, (int32_t*)&op);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get volume"
                                " operation");
                        goto out;
                }
                ctx = op_ctx;
#undef GD_SYNC_OPCODE_KEY
        }

        dict = ctx;
        switch (op) {
                case GD_OP_CREATE_VOLUME:
                        {
                                ++glusterfs_port;
                                ret = dict_set_int32 (dict, "port",
                                                      glusterfs_port);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Failed to set port in "
                                                "dictionary");
                                        goto out;
                                }
                                dict_copy (dict, req_dict);
                        }
                        break;

                case GD_OP_GSYNC_CREATE:
                case GD_OP_GSYNC_SET:
                        {
                                ret = glusterd_op_gsync_args_get (dict,
                                                                  &errstr,
                                                                  &volname,
                                                                  NULL, NULL);
                                if (ret == 0) {
                                        ret = glusterd_dict_set_volid
                                                (dict, volname, op_errstr);
                                        if (ret)
                                                goto out;
                                }
                                dict_copy (dict, req_dict);
                        }
                        break;

                case GD_OP_SET_VOLUME:
                        {
                                ret = dict_get_str (dict, "volname", &volname);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_CRITICAL,
                                                "volname is not present in "
                                                "operation ctx");
                                        goto out;
                                }
                                if (strcmp (volname, "help") &&
                                    strcmp (volname, "help-xml") &&
                                    strcasecmp (volname, "all")) {
                                        ret = glusterd_dict_set_volid
                                                 (dict, volname, op_errstr);
                                        if (ret)
                                                goto out;
                                }
                                dict_destroy (req_dict);
                                req_dict = dict_ref (dict);
                        }
                        break;

                case GD_OP_SYNC_VOLUME:
                        {
                                dict_copy (dict, req_dict);
                                break;
                        }

                case GD_OP_REMOVE_BRICK:
                        {
                                dict_t *dict = ctx;
                                ret = dict_get_str (dict, "volname", &volname);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_CRITICAL,
                                                "volname is not present in "
                                                "operation ctx");
                                        goto out;
                                }

                                ret = glusterd_dict_set_volid (dict, volname,
                                                               op_errstr);
                                if (ret)
                                        goto out;

                                dict_destroy (req_dict);
                                req_dict = dict_ref (dict);
                        }
                        break;

                case GD_OP_STATUS_VOLUME:
                        {
                                ret = dict_get_uint32 (dict, "cmd",
                                                       &status_cmd);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Status command not present "
                                                "in op ctx");
                                        goto out;
                                }
                                if (GF_CLI_STATUS_ALL & status_cmd) {
                                        dict_copy (dict, req_dict);
                                        break;
                                }
                        }
                        /*fall-through*/
                case GD_OP_DELETE_VOLUME:
                case GD_OP_START_VOLUME:
                case GD_OP_STOP_VOLUME:
                case GD_OP_ADD_BRICK:
                case GD_OP_REPLACE_BRICK:
                case GD_OP_RESET_VOLUME:
                case GD_OP_LOG_ROTATE:
                case GD_OP_QUOTA:
                case GD_OP_PROFILE_VOLUME:
                case GD_OP_REBALANCE:
                case GD_OP_HEAL_VOLUME:
                case GD_OP_STATEDUMP_VOLUME:
                case GD_OP_CLEARLOCKS_VOLUME:
                case GD_OP_DEFRAG_BRICK_VOLUME:
                        {
                                ret = dict_get_str (dict, "volname", &volname);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_CRITICAL,
                                                "volname is not present in "
                                                "operation ctx");
                                        goto out;
                                }

                                if (strcasecmp (volname, "all")) {
                                        ret = glusterd_dict_set_volid (dict,
                                                                       volname,
                                                                     op_errstr);
                                        if (ret)
                                                goto out;
                                }
                                dict_copy (dict, req_dict);
                        }
                        break;

                case GD_OP_COPY_FILE:
                        {
                                dict_copy (dict, req_dict);
                                break;
                        }

                case GD_OP_SYS_EXEC:
                        {
                                dict_copy (dict, req_dict);
                                break;
                        }

                default:
                        break;
        }

        *req = req_dict;
        ret = 0;

out:
        return ret;
}

gf_boolean_t
glusterd_is_get_op (xlator_t *this, glusterd_op_t op, dict_t *dict)
{
        char            *key = NULL;
        char            *volname = NULL;
        int             ret = 0;

        if (op == GD_OP_STATUS_VOLUME)
                return _gf_true;

        if ((op == GD_OP_SET_VOLUME)) {
                //check for set volume help
                ret = dict_get_str (dict, "volname", &volname);
                if (volname &&
                    ((strcmp (volname, "help") == 0) ||
                     (strcmp (volname, "help-xml") == 0))) {
                        ret = dict_get_str (dict, "key1", &key);
                        if (ret < 0)
                                return _gf_true;
                }
        }

        return _gf_false;
}

gf_boolean_t
glusterd_is_op_quorum_validation_required (xlator_t *this, glusterd_op_t op,
                                           dict_t *dict)
{
        gf_boolean_t    required = _gf_true;
        char            *key = NULL;
        char            *key_fixed = NULL;
        int             ret = -1;

        if (glusterd_is_get_op (this, op, dict)) {
                required = _gf_false;
                goto out;
        }
        if ((op != GD_OP_SET_VOLUME) && (op != GD_OP_RESET_VOLUME))
                goto out;
        if (op == GD_OP_SET_VOLUME)
                ret = dict_get_str (dict, "key1", &key);
        else if (op == GD_OP_RESET_VOLUME)
                ret = dict_get_str (dict, "key", &key);
        if (ret)
                goto out;
        ret = glusterd_check_option_exists (key, &key_fixed);
        if (ret <= 0)
                goto out;
        if (key_fixed)
                key = key_fixed;
        if (glusterd_is_quorum_option (key))
                required = _gf_false;
out:
        GF_FREE (key_fixed);
        return required;
}

static int
glusterd_op_validate_quorum (xlator_t *this, glusterd_op_t op,
                             dict_t *dict, char **op_errstr)
{
        int                     ret = 0;
        char                    *volname = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        char                    *errstr = NULL;


        errstr = "Quorum not met. Volume operation not allowed.";
        if (!glusterd_is_op_quorum_validation_required (this, op, dict))
                goto out;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                ret = 0;
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                ret = 0;
                goto out;
        }

        if (does_gd_meet_server_quorum (this)) {
                ret = 0;
                goto out;
        }

        if (glusterd_is_volume_in_server_quorum (volinfo)) {
                ret = -1;
                *op_errstr = gf_strdup (errstr);
                goto out;
        }
        ret = 0;
out:
        return ret;
}

static int
glusterd_op_ac_send_stage_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        rpc_clnt_procedure_t    *proc = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;
        glusterd_peerinfo_t     *peerinfo = NULL;
        dict_t                  *dict = NULL;
        char                    *op_errstr  = NULL;
        glusterd_op_t           op = GD_OP_NONE;
        uint32_t                pending_count = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        op = glusterd_op_get_op ();

        ret = glusterd_op_build_payload (&dict, &op_errstr, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, LOGSTR_BUILD_PAYLOAD,
                        gd_op_list[op]);
                if (op_errstr == NULL)
                        gf_asprintf (&op_errstr, OPERRSTR_BUILD_PAYLOAD);
                opinfo.op_errstr = op_errstr;
                goto out;
        }

        ret = glusterd_op_validate_quorum (this, op, dict, &op_errstr);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "%s", op_errstr);
                opinfo.op_errstr = op_errstr;
                goto out;
        }

        /* rsp_dict NULL from source */
        ret = glusterd_op_stage_validate (op, dict, &op_errstr, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, LOGSTR_STAGE_FAIL,
                        gd_op_list[op], "localhost",
                        (op_errstr) ? ":" : " ", (op_errstr) ? op_errstr : " ");
                if (op_errstr == NULL)
                        gf_asprintf (&op_errstr, OPERRSTR_STAGE_FAIL,
                                     "localhost");
                opinfo.op_errstr = op_errstr;
                goto out;
        }

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (!peerinfo->connected || !peerinfo->mgmt)
                        continue;
                if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
                    (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
                        continue;

                proc = &peerinfo->mgmt->proctable[GLUSTERD_MGMT_STAGE_OP];
                GF_ASSERT (proc);
                if (proc->fn) {
                        ret = dict_set_static_ptr (dict, "peerinfo", peerinfo);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "set peerinfo");
                                goto out;
                        }

                        ret = proc->fn (NULL, this, dict);
                        if (ret) {
                                gf_log (this->name, GF_LOG_WARNING, "Failed to "
                                        "send stage request for operation "
                                        "'Volume %s' to peer %s",
                                        gd_op_list[op], peerinfo->hostname);
                                continue;
                        }
                        pending_count++;
                }
        }

        opinfo.pending_count = pending_count;
out:
        if (dict)
                dict_unref (dict);
        if (ret) {
                glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT, NULL);
                opinfo.op_ret = ret;
        }

        gf_log (this->name, GF_LOG_DEBUG, "Sent stage op request for "
                "'Volume %s' to %d peers", gd_op_list[op],
                opinfo.pending_count);

        if (!opinfo.pending_count)
                ret = glusterd_op_sm_inject_all_acc ();

        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;

}

static int32_t
glusterd_op_start_rb_timer (dict_t *dict)
{
        int32_t         op = 0;
        struct timespec timeout = {0, };
        glusterd_conf_t *priv = NULL;
        int32_t         ret = -1;
        dict_t          *rb_ctx = NULL;

        GF_ASSERT (dict);
        priv = THIS->private;

        ret = dict_get_int32 (dict, "operation", &op);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "dict_get on operation failed");
                goto out;
        }

        if (op != GF_REPLACE_OP_START) {
                ret = glusterd_op_sm_inject_all_acc ();
                goto out;
        }

        timeout.tv_sec  = 5;
        timeout.tv_nsec = 0;


        rb_ctx = dict_copy (dict, rb_ctx);
        if (!rb_ctx) {
                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't copy "
                        "replace brick context. Can't start replace brick");
                ret = -1;
                goto out;
        }
        priv->timer = gf_timer_call_after (THIS->ctx, timeout,
                                           glusterd_do_replace_brick,
                                           (void *) rb_ctx);

        ret = 0;

out:
        return ret;
}

/* This function takes a dict and converts the uuid values of key specified
 * into hostnames
 */
static int
glusterd_op_volume_dict_uuid_to_hostname (dict_t *dict, const char *key_fmt,
                                          int idx_min, int idx_max)
{
        int             ret = -1;
        int             i = 0;
        char            key[1024];
        char            *uuid_str = NULL;
        uuid_t          uuid = {0,};
        char            *hostname = NULL;
        xlator_t        *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (dict);
        GF_ASSERT (key_fmt);

        for (i = idx_min; i < idx_max; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), key_fmt, i);
                ret = dict_get_str (dict, key, &uuid_str);
                if (ret)
                        continue;

                gf_log (this->name, GF_LOG_DEBUG, "Got uuid %s",
                        uuid_str);

                ret = uuid_parse (uuid_str, uuid);
                /* if parsing fails don't error out
                 * let the original value be retained
                 */
                if (ret)
                        continue;

                hostname = glusterd_uuid_to_hostname (uuid);
                if (hostname) {
                        gf_log (this->name, GF_LOG_DEBUG, "%s -> %s",
                                uuid_str, hostname);
                        ret = dict_set_dynstr (dict, key, hostname);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Error setting hostname %s to dict",
                                        hostname);
                                GF_FREE (hostname);
                                goto out;
                        }
                }
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
reassign_defrag_status (dict_t *dict, char *key, gf_defrag_status_t *status)
{
        int ret = 0;

        if (!*status)
                return ret;

        switch (*status) {
        case GF_DEFRAG_STATUS_STARTED:
                *status = GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED;
                break;

        case GF_DEFRAG_STATUS_STOPPED:
                *status = GF_DEFRAG_STATUS_LAYOUT_FIX_STOPPED;
                break;

        case GF_DEFRAG_STATUS_COMPLETE:
                *status = GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE;
                break;

        case GF_DEFRAG_STATUS_FAILED:
                *status = GF_DEFRAG_STATUS_LAYOUT_FIX_FAILED;
                break;
        default:
                break;
         }

        ret = dict_set_int32(dict, key, *status);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to reset defrag %s in dict", key);

        return ret;
}

/* Check and reassign the defrag_status enum got from the rebalance process
 * of all peers so that the rebalance-status CLI command can display if a
 * full-rebalance or just a fix-layout was carried out.
 */
static int
glusterd_op_check_peer_defrag_status (dict_t *dict, int count)
{
        glusterd_volinfo_t *volinfo  = NULL;
        gf_defrag_status_t status    = GF_DEFRAG_STATUS_NOT_STARTED;
        char               key[256]  = {0,};
        char               *volname  = NULL;
        int                ret       = -1;
        int                i         = 1;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                goto out;
        }

        if (volinfo->rebal.defrag_cmd != GF_DEFRAG_CMD_START_LAYOUT_FIX) {
                /* Fix layout was not issued; we don't need to reassign
                   the status */
                ret = 0;
                goto out;
        }

        do {
                memset (key, 0, 256);
                snprintf (key, 256, "status-%d", i);
                ret = dict_get_int32 (dict, key, (int32_t *)&status);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "failed to get defrag %s", key);
                        goto out;
                }
                ret = reassign_defrag_status (dict, key, &status);
                if (ret)
                        goto out;
                i++;
        } while (i <= count);

        ret = 0;
out:
        return ret;

}

/* This function is used to modify the op_ctx dict before sending it back
 * to cli. This is useful in situations like changing the peer uuids to
 * hostnames etc.
 */
void
glusterd_op_modify_op_ctx (glusterd_op_t op, void *ctx)
{
        int             ret = -1;
        dict_t          *op_ctx = NULL;
        int             brick_index_max = -1;
        int             other_count = 0;
        int             count = 0;
        uint32_t        cmd = GF_CLI_STATUS_NONE;
        xlator_t        *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (ctx)
                op_ctx = ctx;
        else
                op_ctx = glusterd_op_get_ctx();

        if (!op_ctx) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Operation context is not present.");
                goto out;
        }

        switch (op) {
        case GD_OP_STATUS_VOLUME:
                ret = dict_get_uint32 (op_ctx, "cmd", &cmd);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Failed to get status cmd");
                        goto out;
                }
                if (!(cmd & GF_CLI_STATUS_NFS || cmd & GF_CLI_STATUS_SHD ||
                    (cmd & GF_CLI_STATUS_MASK) == GF_CLI_STATUS_NONE)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "op_ctx modification not required for status "
                                "operation being performed");
                        goto out;
                }

                ret = dict_get_int32 (op_ctx, "brick-index-max",
                                      &brick_index_max);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Failed to get brick-index-max");
                        goto out;
                }

                ret = dict_get_int32 (op_ctx, "other-count", &other_count);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Failed to get other-count");
                        goto out;
                }

                count = brick_index_max + other_count + 1;

                /* add 'brick%d.peerid' into op_ctx with value of 'brick%d.path'.
                   nfs/sshd like services have this additional uuid */
                {
                        char  key[1024];
                        char *uuid_str = NULL;
                        char *uuid = NULL;
                        int   i;

                        for (i = brick_index_max + 1; i < count; i++) {
                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key), "brick%d.path", i);
                                ret = dict_get_str (op_ctx, key, &uuid_str);
                                if (!ret) {
                                        memset (key, 0, sizeof (key));
                                        snprintf (key, sizeof (key),
                                                  "brick%d.peerid", i);
                                        uuid = gf_strdup (uuid_str);
                                        if (!uuid) {
                                                gf_log (this->name, GF_LOG_DEBUG,
                                                        "unable to create dup of"
                                                        " uuid_str");
                                                continue;
                                        }
                                        ret = dict_set_dynstr (op_ctx, key,
                                                               uuid);
                                        if (ret != 0) {
                                                GF_FREE (uuid);
                                        }
                                }
                        }
                }

                ret = glusterd_op_volume_dict_uuid_to_hostname (op_ctx,
                                                                "brick%d.path",
                                                                0, count);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "Failed uuid to hostname conversion");

                break;

        case GD_OP_PROFILE_VOLUME:
                ret = dict_get_str_boolean (op_ctx, "nfs", _gf_false);
                if (!ret)
                        goto out;

                ret = dict_get_int32 (op_ctx, "count", &count);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Failed to get brick count");
                        goto out;
                }

                ret = glusterd_op_volume_dict_uuid_to_hostname (op_ctx,
                                                                "%d-brick",
                                                                1, (count + 1));
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "Failed uuid to hostname conversion");

                break;

        /* For both rebalance and remove-brick status, the glusterd op is the
         * same
         */
        case GD_OP_DEFRAG_BRICK_VOLUME:
                ret = dict_get_int32 (op_ctx, "count", &count);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Failed to get count");
                        goto out;
                }

                /* add 'node-name-%d' into op_ctx with value uuid_str.
                   this will be used to convert to hostname later */
                {
                        char  key[1024];
                        char *uuid_str = NULL;
                        char *uuid = NULL;
                        int   i;

                        for (i = 1; i <= count; i++) {
                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key), "node-uuid-%d", i);
                                ret = dict_get_str (op_ctx, key, &uuid_str);
                                if (!ret) {
                                        memset (key, 0, sizeof (key));
                                        snprintf (key, sizeof (key),
                                                  "node-name-%d", i);
                                        uuid = gf_strdup (uuid_str);
                                        if (!uuid) {
                                                gf_log (this->name, GF_LOG_DEBUG,
                                                        "unable to create dup of"
                                                        " uuid_str");
                                                continue;
                                        }
                                        ret = dict_set_dynstr (op_ctx, key,
                                                               uuid);
                                        if (ret != 0) {
                                                GF_FREE (uuid);
                                        }
                                }
                        }
                }

                ret = glusterd_op_volume_dict_uuid_to_hostname (op_ctx,
                                                                "node-name-%d",
                                                                1, (count + 1));
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "Failed uuid to hostname conversion");

                ret = glusterd_op_check_peer_defrag_status (op_ctx, count);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to reset defrag status for fix-layout");
                break;

        default:
                ret = 0;
                gf_log (this->name, GF_LOG_DEBUG,
                        "op_ctx modification not required");
                break;

        }

out:
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "op_ctx modification failed");
        return;
}

static int
glusterd_op_commit_hook (glusterd_op_t op, dict_t *op_ctx,
                         glusterd_commit_hook_type_t type)
{
        glusterd_conf_t *priv                   = NULL;
        char            hookdir[PATH_MAX]       = {0, };
        char            scriptdir[PATH_MAX]     = {0, };
        char            type_subdir[256]        = {0, };
        char            *cmd_subdir             = NULL;
        int             ret                     = -1;

        priv = THIS->private;
        switch (type) {
                case GD_COMMIT_HOOK_NONE:
                case GD_COMMIT_HOOK_MAX:
                        /*Won't be called*/
                        break;

                case GD_COMMIT_HOOK_PRE:
                        strcpy (type_subdir, "pre");
                        break;
                case GD_COMMIT_HOOK_POST:
                        strcpy (type_subdir, "post");
                        break;
        }

        cmd_subdir = glusterd_hooks_get_hooks_cmd_subdir (op);
        if (strlen (cmd_subdir) == 0)
                return -1;

        GLUSTERD_GET_HOOKS_DIR (hookdir, GLUSTERD_HOOK_VER, priv);
        snprintf (scriptdir, sizeof (scriptdir), "%s/%s/%s",
                  hookdir, cmd_subdir, type_subdir);

        switch (type) {
                case GD_COMMIT_HOOK_NONE:
                case GD_COMMIT_HOOK_MAX:
                        /*Won't be called*/
                        break;

                case GD_COMMIT_HOOK_PRE:
                        ret = glusterd_hooks_run_hooks (scriptdir, op, op_ctx,
                                                        type);
                        break;
                case GD_COMMIT_HOOK_POST:
                        ret = glusterd_hooks_post_stub_enqueue (scriptdir, op,
                                                                op_ctx);
                        break;
        }

        return ret;
}

static int
glusterd_op_ac_send_commit_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        rpc_clnt_procedure_t    *proc = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;
        dict_t                  *dict = NULL;
        dict_t                  *op_dict = NULL;
        glusterd_peerinfo_t     *peerinfo = NULL;
        char                    *op_errstr  = NULL;
        glusterd_op_t           op = GD_OP_NONE;
        uint32_t                pending_count = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        op      = glusterd_op_get_op ();
        op_dict = glusterd_op_get_ctx ();

        ret = glusterd_op_build_payload (&dict, &op_errstr, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, LOGSTR_BUILD_PAYLOAD,
                        gd_op_list[op]);
                if (op_errstr == NULL)
                        gf_asprintf (&op_errstr, OPERRSTR_BUILD_PAYLOAD);
                opinfo.op_errstr = op_errstr;
                goto out;
        }

        ret = glusterd_op_commit_perform (op, dict, &op_errstr, NULL); //rsp_dict invalid for source
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, LOGSTR_COMMIT_FAIL,
                        gd_op_list[op], "localhost", (op_errstr) ? ":" : " ",
                        (op_errstr) ? op_errstr : " ");
                if (op_errstr == NULL)
                        gf_asprintf (&op_errstr, OPERRSTR_COMMIT_FAIL,
                                     "localhost");
                opinfo.op_errstr = op_errstr;
                goto out;
        }


        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (!peerinfo->connected || !peerinfo->mgmt)
                        continue;
                if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
                    (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
                        continue;

                proc = &peerinfo->mgmt->proctable[GLUSTERD_MGMT_COMMIT_OP];
                GF_ASSERT (proc);
                if (proc->fn) {
                        ret = dict_set_static_ptr (dict, "peerinfo", peerinfo);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "failed to set peerinfo");
                                goto out;
                        }
                        ret = proc->fn (NULL, this, dict);
                        if (ret) {
                                gf_log (this->name, GF_LOG_WARNING, "Failed to "
                                        "send commit request for operation "
                                        "'Volume %s' to peer %s",
                                        gd_op_list[op], peerinfo->hostname);
                                continue;
                        }
                        pending_count++;
                }
        }

        opinfo.pending_count = pending_count;
        gf_log (this->name, GF_LOG_DEBUG, "Sent commit op req for 'Volume %s' "
                "to %d peers", gd_op_list[op], opinfo.pending_count);
out:
        if (dict)
                dict_unref (dict);
        if (ret) {
                glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT, NULL);
                opinfo.op_ret = ret;
        }

        if (!opinfo.pending_count) {
                if (op == GD_OP_REPLACE_BRICK) {
                        ret = glusterd_op_start_rb_timer (op_dict);

                } else {
                        glusterd_op_modify_op_ctx (op, NULL);
                        ret = glusterd_op_sm_inject_all_acc ();
                }
                goto err;
        }

err:
        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;

}

static int
glusterd_op_ac_rcvd_stage_op_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;

        GF_ASSERT (event);

        if (opinfo.pending_count > 0)
                opinfo.pending_count--;

        if (opinfo.pending_count > 0)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_STAGE_ACC, NULL);

out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_ac_stage_op_failed (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;

        GF_ASSERT (event);

        if (opinfo.pending_count > 0)
                opinfo.pending_count--;

        if (opinfo.pending_count > 0)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_ALL_ACK, NULL);

out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_ac_commit_op_failed (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;

        GF_ASSERT (event);

        if (opinfo.pending_count > 0)
                opinfo.pending_count--;

        if (opinfo.pending_count > 0)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_ALL_ACK, NULL);

out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_ac_brick_op_failed (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        glusterd_op_brick_rsp_ctx_t *ev_ctx = NULL;
        gf_boolean_t                free_errstr = _gf_false;
        xlator_t                    *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (event);
        GF_ASSERT (ctx);
        ev_ctx = ctx;

        ret = glusterd_remove_pending_entry (&opinfo.pending_bricks, ev_ctx->pending_node->node);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "unknown response received ");
                ret = -1;
                free_errstr = _gf_true;
                goto out;
        }
        if (opinfo.brick_pending_count > 0)
                opinfo.brick_pending_count--;
        if (opinfo.op_ret == 0)
                opinfo.op_ret = ev_ctx->op_ret;

        if (opinfo.op_errstr == NULL)
                opinfo.op_errstr = ev_ctx->op_errstr;
        else
                free_errstr = _gf_true;

        if (opinfo.brick_pending_count > 0)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_ALL_ACK, ev_ctx->commit_ctx);

out:
        if (ev_ctx->rsp_dict)
                dict_unref (ev_ctx->rsp_dict);
        if (free_errstr && ev_ctx->op_errstr)
                GF_FREE (ev_ctx->op_errstr);
        GF_FREE (ctx);
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_ac_rcvd_commit_op_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        dict_t                 *op_ctx            = NULL;
        int                     ret               = 0;
        gf_boolean_t            commit_ack_inject = _gf_true;
        glusterd_op_t           op                = GD_OP_NONE;
        xlator_t               *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        op = glusterd_op_get_op ();
        GF_ASSERT (event);

        if (opinfo.pending_count > 0)
                opinfo.pending_count--;

        if (opinfo.pending_count > 0)
                goto out;

        if (op == GD_OP_REPLACE_BRICK) {
                op_ctx = glusterd_op_get_ctx ();
                if (!op_ctx) {
                        gf_log (this->name, GF_LOG_CRITICAL, "Operation "
                                "context is not present.");
                        ret = -1;
                        goto out;
                }

                ret = glusterd_op_start_rb_timer (op_ctx);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Couldn't start "
                                "replace-brick operation.");
                        goto out;
                }

                commit_ack_inject = _gf_false;
                goto out;
        }


out:
        if (commit_ack_inject) {
                if (ret)
                        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT, NULL);
                else if (!opinfo.pending_count) {
                        glusterd_op_modify_op_ctx (op, NULL);
                        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_COMMIT_ACC, NULL);
                }
                /*else do nothing*/
        }

        return ret;
}

static int
glusterd_op_ac_rcvd_unlock_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;

        GF_ASSERT (event);

        if (opinfo.pending_count > 0)
                opinfo.pending_count--;

        if (opinfo.pending_count > 0)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_ALL_ACC, NULL);

        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

out:
        return ret;
}

int32_t
glusterd_op_clear_errstr() {
        opinfo.op_errstr = NULL;
        return 0;
}

int32_t
glusterd_op_set_ctx (void *ctx)
{

        opinfo.op_ctx = ctx;

        return 0;

}

int32_t
glusterd_op_reset_ctx ()
{

        glusterd_op_set_ctx (NULL);

        return 0;
}

int32_t
glusterd_op_txn_complete ()
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        int32_t                 op = -1;
        int32_t                 op_ret = 0;
        int32_t                 op_errno = 0;
        rpcsvc_request_t        *req = NULL;
        void                    *ctx = NULL;
        char                    *op_errstr = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        op  = glusterd_op_get_op ();
        ctx = glusterd_op_get_ctx ();
        op_ret = opinfo.op_ret;
        op_errno = opinfo.op_errno;
        req = opinfo.req;
        if (opinfo.op_errstr)
                op_errstr = opinfo.op_errstr;

        opinfo.op_ret = 0;
        opinfo.op_errno = 0;
        glusterd_op_clear_op ();
        glusterd_op_reset_ctx ();
        glusterd_op_clear_errstr ();

        ret = glusterd_unlock (MY_UUID);

        /* unlock cant/shouldnt fail here!! */
        if (ret) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Unable to clear local lock, ret: %d", ret);
        } else {
                gf_log (this->name, GF_LOG_DEBUG, "Cleared local lock");
        }

        ret = glusterd_op_send_cli_response (op, op_ret,
                                             op_errno, req, ctx, op_errstr);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Responding to cli failed, "
                        "ret: %d", ret);
                //Ignore this error, else state machine blocks
                ret = 0;
        }

        if (op_errstr && (strcmp (op_errstr, "")))
                GF_FREE (op_errstr);


        if (priv->pending_quorum_action)
                glusterd_do_quorum_action ();
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
glusterd_op_ac_unlocked_all (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;

        GF_ASSERT (event);

        ret = glusterd_op_txn_complete ();

        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_ac_stage_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = -1;
        glusterd_req_ctx_t      *req_ctx = NULL;
        int32_t                 status = 0;
        dict_t                  *rsp_dict  = NULL;
        char                    *op_errstr = NULL;
        dict_t                  *dict = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (ctx);

        req_ctx = ctx;

        dict = req_ctx->dict;

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to get new dictionary");
                return -1;
        }

        status = glusterd_op_stage_validate (req_ctx->op, dict, &op_errstr,
                                             rsp_dict);

        if (status) {
                gf_log (this->name, GF_LOG_ERROR, "Stage failed on operation"
                        " 'Volume %s', Status : %d", gd_op_list[req_ctx->op],
                        status);
        }

        ret = glusterd_op_stage_send_resp (req_ctx->req, req_ctx->op,
                                           status, op_errstr, rsp_dict);

        if (op_errstr && (strcmp (op_errstr, "")))
                GF_FREE (op_errstr);

        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);

        if (rsp_dict)
                dict_unref (rsp_dict);

        return ret;
}

static gf_boolean_t
glusterd_need_brick_op (glusterd_op_t op)
{
        gf_boolean_t ret        = _gf_false;

        GF_ASSERT (GD_OP_NONE < op && op < GD_OP_MAX);

        switch (op) {
        case GD_OP_PROFILE_VOLUME:
        case GD_OP_STATUS_VOLUME:
        case GD_OP_DEFRAG_BRICK_VOLUME:
        case GD_OP_HEAL_VOLUME:
                ret = _gf_true;
                break;
        default:
                ret = _gf_false;
        }

        return ret;
}

dict_t*
glusterd_op_init_commit_rsp_dict (glusterd_op_t op)
{
        dict_t                  *rsp_dict = NULL;
        dict_t                  *op_ctx   = NULL;

        GF_ASSERT (GD_OP_NONE < op && op < GD_OP_MAX);

        if (glusterd_need_brick_op (op)) {
                op_ctx = glusterd_op_get_ctx ();
                GF_ASSERT (op_ctx);
                rsp_dict = dict_ref (op_ctx);
        } else {
                rsp_dict = dict_new ();
        }

        return rsp_dict;
}

static int
glusterd_op_ac_commit_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                       ret        = 0;
        glusterd_req_ctx_t       *req_ctx    = NULL;
        int32_t                   status     = 0;
        char                     *op_errstr  = NULL;
        dict_t                   *dict       = NULL;
        dict_t                   *rsp_dict   = NULL;
        xlator_t                 *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (ctx);

        req_ctx = ctx;

        dict = req_ctx->dict;

        rsp_dict = glusterd_op_init_commit_rsp_dict (req_ctx->op);
        if (NULL == rsp_dict)
                return -1;


        if (GD_OP_CLEARLOCKS_VOLUME == req_ctx->op) {
                /*clear locks should be run only on
                 * originator glusterd*/
                status = 0;

        } else {
                status = glusterd_op_commit_perform (req_ctx->op, dict,
                                                     &op_errstr, rsp_dict);
        }

        if (status)
                gf_log (this->name, GF_LOG_ERROR, "Commit of operation "
                        "'Volume %s' failed: %d", gd_op_list[req_ctx->op],
                        status);

        ret = glusterd_op_commit_send_resp (req_ctx->req, req_ctx->op,
                                            status, op_errstr, rsp_dict);

        glusterd_op_fini_ctx ();
        if (op_errstr && (strcmp (op_errstr, "")))
                GF_FREE (op_errstr);

        if (rsp_dict)
                dict_unref (rsp_dict);

        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_op_ac_send_commit_failed (glusterd_op_sm_event_t *event, void *ctx)
{
        int                             ret = 0;
        glusterd_req_ctx_t              *req_ctx = NULL;
        dict_t                          *op_ctx = NULL;

        GF_ASSERT (ctx);

        req_ctx = ctx;

        op_ctx = glusterd_op_get_ctx ();

        ret = glusterd_op_commit_send_resp (req_ctx->req, req_ctx->op,
                                            opinfo.op_ret, opinfo.op_errstr,
                                            op_ctx);

        glusterd_op_fini_ctx ();
        if (opinfo.op_errstr && (strcmp (opinfo.op_errstr, ""))) {
                GF_FREE (opinfo.op_errstr);
                opinfo.op_errstr = NULL;
        }

        gf_log (THIS->name, GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

static int
glusterd_op_sm_transition_state (glusterd_op_info_t *opinfo,
                                 glusterd_op_sm_t *state,
                                 glusterd_op_sm_event_type_t event_type)
{
        glusterd_conf_t         *conf = NULL;

        GF_ASSERT (state);
        GF_ASSERT (opinfo);

        conf = THIS->private;
        GF_ASSERT (conf);

        (void) glusterd_sm_tr_log_transition_add (&conf->op_sm_log,
                                           opinfo->state.state,
                                           state[event_type].next_state,
                                           event_type);

        opinfo->state.state = state[event_type].next_state;
        return 0;
}

int32_t
glusterd_op_stage_validate (glusterd_op_t op, dict_t *dict, char **op_errstr,
                            dict_t *rsp_dict)
{
        int ret = -1;
        xlator_t *this = THIS;

        switch (op) {
                case GD_OP_CREATE_VOLUME:
                        ret = glusterd_op_stage_create_volume (dict, op_errstr);
                        break;

                case GD_OP_START_VOLUME:
                        ret = glusterd_op_stage_start_volume (dict, op_errstr);
                        break;

                case GD_OP_STOP_VOLUME:
                        ret = glusterd_op_stage_stop_volume (dict, op_errstr);
                        break;

                case GD_OP_DELETE_VOLUME:
                        ret = glusterd_op_stage_delete_volume (dict, op_errstr);
                        break;

                case GD_OP_ADD_BRICK:
                        ret = glusterd_op_stage_add_brick (dict, op_errstr);
                        break;

                case GD_OP_REPLACE_BRICK:
                        ret = glusterd_op_stage_replace_brick (dict, op_errstr,
                                                               rsp_dict);
                        break;

                case GD_OP_SET_VOLUME:
                        ret = glusterd_op_stage_set_volume (dict, op_errstr);
                        break;

                case GD_OP_RESET_VOLUME:
                        ret = glusterd_op_stage_reset_volume (dict, op_errstr);
                        break;

                case GD_OP_REMOVE_BRICK:
                        ret = glusterd_op_stage_remove_brick (dict, op_errstr);
                        break;

                case GD_OP_LOG_ROTATE:
                        ret = glusterd_op_stage_log_rotate (dict, op_errstr);
                        break;

                case GD_OP_SYNC_VOLUME:
                        ret = glusterd_op_stage_sync_volume (dict, op_errstr);
                        break;

                case GD_OP_GSYNC_CREATE:
                        ret = glusterd_op_stage_gsync_create (dict, op_errstr);
                        break;

                case GD_OP_GSYNC_SET:
                        ret = glusterd_op_stage_gsync_set (dict, op_errstr);
                        break;

                case GD_OP_PROFILE_VOLUME:
                        ret = glusterd_op_stage_stats_volume (dict, op_errstr);
                        break;

                case GD_OP_QUOTA:
                        ret = glusterd_op_stage_quota (dict, op_errstr,
                                                       rsp_dict);
                        break;

                case GD_OP_STATUS_VOLUME:
                        ret = glusterd_op_stage_status_volume (dict, op_errstr);
                        break;

                case GD_OP_REBALANCE:
                case GD_OP_DEFRAG_BRICK_VOLUME:
                        ret = glusterd_op_stage_rebalance (dict, op_errstr);
                        break;

                case GD_OP_HEAL_VOLUME:
                        ret = glusterd_op_stage_heal_volume (dict, op_errstr);
                        break;

                case GD_OP_STATEDUMP_VOLUME:
                        ret = glusterd_op_stage_statedump_volume (dict,
                                                                  op_errstr);
                        break;
                case GD_OP_CLEARLOCKS_VOLUME:
                        ret = glusterd_op_stage_clearlocks_volume (dict,
                                                                   op_errstr);
                        break;

                case GD_OP_COPY_FILE:
                        ret = glusterd_op_stage_copy_file (dict, op_errstr);
                        break;

                case GD_OP_SYS_EXEC:
                        ret = glusterd_op_stage_sys_exec (dict, op_errstr);
                        break;

                default:
                        gf_log (this->name, GF_LOG_ERROR, "Unknown op %s",
                                gd_op_list[op]);
        }

        gf_log (this->name, GF_LOG_DEBUG, "OP = %d. Returning %d", op, ret);
        return ret;
}


int32_t
glusterd_op_commit_perform (glusterd_op_t op, dict_t *dict, char **op_errstr,
                            dict_t *rsp_dict)
{
        int ret = -1;
        xlator_t *this = THIS;

        glusterd_op_commit_hook (op, dict, GD_COMMIT_HOOK_PRE);
        switch (op) {
                case GD_OP_CREATE_VOLUME:
                        ret = glusterd_op_create_volume (dict, op_errstr);
                        break;

                case GD_OP_START_VOLUME:
                        ret = glusterd_op_start_volume (dict, op_errstr);
                        break;

                case GD_OP_STOP_VOLUME:
                        ret = glusterd_op_stop_volume (dict);
                        break;

                case GD_OP_DELETE_VOLUME:
                        ret = glusterd_op_delete_volume (dict);
                        break;

                case GD_OP_ADD_BRICK:
                        ret = glusterd_op_add_brick (dict, op_errstr);
                        break;

                case GD_OP_REPLACE_BRICK:
                        ret = glusterd_op_replace_brick (dict, rsp_dict);
                        break;

                case GD_OP_SET_VOLUME:
                        ret = glusterd_op_set_volume (dict);
                        break;

                case GD_OP_RESET_VOLUME:
                        ret = glusterd_op_reset_volume (dict, op_errstr);
                        break;

                case GD_OP_REMOVE_BRICK:
                        ret = glusterd_op_remove_brick (dict, op_errstr);
                        break;

                case GD_OP_LOG_ROTATE:
                        ret = glusterd_op_log_rotate (dict);
                        break;

                case GD_OP_SYNC_VOLUME:
                        ret = glusterd_op_sync_volume (dict, op_errstr, rsp_dict);
                        break;

                case GD_OP_GSYNC_CREATE:
                        ret = glusterd_op_gsync_create (dict, op_errstr,
                                                        rsp_dict);
                        break;

                case GD_OP_GSYNC_SET:
                        ret = glusterd_op_gsync_set (dict, op_errstr, rsp_dict);
                        break;

                case GD_OP_PROFILE_VOLUME:
                        ret = glusterd_op_stats_volume (dict, op_errstr,
                                                        rsp_dict);
                        break;

                case GD_OP_QUOTA:
                        ret = glusterd_op_quota (dict, op_errstr, rsp_dict);
                        break;

                case GD_OP_STATUS_VOLUME:
                        ret = glusterd_op_status_volume (dict, op_errstr, rsp_dict);
                        break;

                case GD_OP_REBALANCE:
                case GD_OP_DEFRAG_BRICK_VOLUME:
                        ret = glusterd_op_rebalance (dict, op_errstr, rsp_dict);
                        break;

                case GD_OP_HEAL_VOLUME:
                        ret = glusterd_op_heal_volume (dict, op_errstr);
                        break;

                case GD_OP_STATEDUMP_VOLUME:
                        ret = glusterd_op_statedump_volume (dict, op_errstr);
                        break;

                case GD_OP_CLEARLOCKS_VOLUME:
                        ret = glusterd_op_clearlocks_volume (dict, op_errstr,
                                                             rsp_dict);
                        break;

                case GD_OP_COPY_FILE:
                        ret = glusterd_op_copy_file (dict, op_errstr);
                        break;

                case GD_OP_SYS_EXEC:
                        ret = glusterd_op_sys_exec (dict, op_errstr, rsp_dict);
                        break;

                default:
                        gf_log (this->name, GF_LOG_ERROR, "Unknown op %s",
                                gd_op_list[op]);
                        break;
        }

        if (ret == 0)
            glusterd_op_commit_hook (op, dict, GD_COMMIT_HOOK_POST);

        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


static int
glusterd_bricks_select_stop_volume (dict_t *dict, char **op_errstr,
                                    struct list_head *selected)
{
        int                                     ret = 0;
        int                                     flags = 0;
        char                                    *volname = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        glusterd_pending_node_t                 *pending_node = NULL;

        ret = glusterd_op_stop_volume_args_get (dict, &volname, &flags);
        if (ret)
                goto out;

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                gf_asprintf (op_errstr, FMTSTR_CHECK_VOL_EXISTS, volname);
                goto out;
        }

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (glusterd_is_brick_started (brickinfo)) {
                        pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                                  gf_gld_mt_pending_node_t);
                        if (!pending_node) {
                                ret = -1;
                                goto out;
                        } else {
                                pending_node->node = brickinfo;
                                pending_node->type = GD_NODE_BRICK;
                                list_add_tail (&pending_node->list, selected);
                                pending_node = NULL;
                        }
                }
        }

out:
        return ret;
}

static int
glusterd_bricks_select_remove_brick (dict_t *dict, char **op_errstr,
                                     struct list_head *selected)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        char                                    *brick = NULL;
        int32_t                                 count = 0;
        int32_t                                 i = 1;
        char                                    key[256] = {0,};
        glusterd_pending_node_t                 *pending_node = NULL;
        int32_t                                 force = 0;



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

        ret = dict_get_int32 (dict, "force", &force);
        if (ret) {
                gf_log (THIS->name, GF_LOG_INFO, "force flag is not set");
                ret = 0;
                goto out;
        }

        while ( i <= count) {
                snprintf (key, 256, "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "Unable to get brick");
                        goto out;
                }

                ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                              &brickinfo);
                if (ret)
                        goto out;
                if (glusterd_is_brick_started (brickinfo)) {
                        pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                                  gf_gld_mt_pending_node_t);
                        if (!pending_node) {
                                ret = -1;
                                goto out;
                        } else {
                                pending_node->node = brickinfo;
                                pending_node->type = GD_NODE_BRICK;
                                list_add_tail (&pending_node->list, selected);
                                pending_node = NULL;
                        }
                }
                i++;
        }

out:
        return ret;
}

static int
glusterd_bricks_select_profile_volume (dict_t *dict, char **op_errstr,
                                       struct list_head *selected)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        char                                    msg[2048] = {0,};
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        xlator_t                                *this = NULL;
        int32_t                                 stats_op = GF_CLI_STATS_NONE;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        glusterd_pending_node_t                 *pending_node = NULL;
        char                                    *brick = NULL;



        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);


        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "volume name get failed");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), "Volume %s does not exists",
                          volname);

                *op_errstr = gf_strdup (msg);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        ret = dict_get_int32 (dict, "op", &stats_op);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "volume profile op get failed");
                goto out;
        }

        switch (stats_op) {
        case GF_CLI_STATS_START:
        case GF_CLI_STATS_STOP:
                goto out;
                break;
        case GF_CLI_STATS_INFO:
                ret = dict_get_str_boolean (dict, "nfs", _gf_false);
                if (ret) {
                        if (!glusterd_is_nodesvc_online ("nfs")) {
                                ret = -1;
                                gf_log (this->name, GF_LOG_ERROR, "NFS server"
                                        " is not running");
                                goto out;
                        }
                        pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                                  gf_gld_mt_pending_node_t);
                        if (!pending_node) {
                                ret = -1;
                                goto out;
                        }
                        pending_node->node = priv->nfs;
                        pending_node->type = GD_NODE_NFS;
                        list_add_tail (&pending_node->list, selected);
                        pending_node = NULL;

                        ret = 0;
                        goto out;

                }
                list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                        if (glusterd_is_brick_started (brickinfo)) {
                                pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                                          gf_gld_mt_pending_node_t);
                                if (!pending_node) {
                                        ret = -1;
                                        goto out;
                                } else {
                                        pending_node->node = brickinfo;
                                        pending_node->type = GD_NODE_BRICK;
                                        list_add_tail (&pending_node->list,
                                                       selected);
                                        pending_node = NULL;
                                }
                        }
                }
                break;

        case GF_CLI_STATS_TOP:
                ret = dict_get_str_boolean (dict, "nfs", _gf_false);
                if (ret) {
                        if (!glusterd_is_nodesvc_online ("nfs")) {
                                ret = -1;
                                gf_log (this->name, GF_LOG_ERROR, "NFS server"
                                        " is not running");
                                goto out;
                        }
                        pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                                  gf_gld_mt_pending_node_t);
                        if (!pending_node) {
                                ret = -1;
                                goto out;
                        }
                        pending_node->node = priv->nfs;
                        pending_node->type = GD_NODE_NFS;
                        list_add_tail (&pending_node->list, selected);
                        pending_node = NULL;

                        ret = 0;
                        goto out;

                }
                ret = dict_get_str (dict, "brick", &brick);
                if (!ret) {
                        ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                                      &brickinfo);
                        if (ret)
                                goto out;

                        if (!glusterd_is_brick_started (brickinfo))
                                goto out;

                        pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                                  gf_gld_mt_pending_node_t);
                        if (!pending_node) {
                                ret = -1;
                                goto out;
                        } else {
                                pending_node->node = brickinfo;
                                pending_node->type = GD_NODE_BRICK;
                                list_add_tail (&pending_node->list,
                                               selected);
                                pending_node = NULL;
                                goto out;
                        }
                }
                ret = 0;
                list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                        if (glusterd_is_brick_started (brickinfo)) {
                                pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                                          gf_gld_mt_pending_node_t);
                                if (!pending_node) {
                                        ret = -1;
                                        goto out;
                                } else {
                                        pending_node->node = brickinfo;
                                        pending_node->type = GD_NODE_BRICK;
                                        list_add_tail (&pending_node->list,
                                                       selected);
                                        pending_node = NULL;
                                }
                        }
                }
                break;

        default:
                GF_ASSERT (0);
                gf_log ("glusterd", GF_LOG_ERROR, "Invalid profile op: %d",
                        stats_op);
                ret = -1;
                goto out;
                break;
        }


out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
_add_rxlator_to_dict (dict_t *dict, char *volname, int index, int count)
{
        int     ret             = -1;
        char    key[128]        = {0,};
        char    *xname          = NULL;

        snprintf (key, sizeof (key), "xl-%d", count);
        ret = gf_asprintf (&xname, "%s-replicate-%d", volname, index);
        if (ret == -1)
                goto out;

        ret = dict_set_dynstr (dict, key, xname);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, xname, index);
out:
        return ret;
}

int
get_replica_index_for_per_replica_cmd (glusterd_volinfo_t *volinfo,
                                       dict_t *dict) {
        int                     ret = 0;
        char                    *hostname = NULL;
        char                    *path = NULL;
        int                     index = 0;
        glusterd_brickinfo_t   *brickinfo = NULL;
        int                     cmd_replica_index = -1;
        int                     replica_count = -1;


        if (!dict)  {
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "per-replica-cmd-hostname", &hostname);
        if (ret)
                goto out;
        ret = dict_get_str (dict, "per-replica-cmd-path", &path);
        if (ret)
                goto out;

        replica_count = volinfo->replica_count;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (uuid_is_null (brickinfo->uuid))
                        (void)glusterd_resolve_brick (brickinfo);
                if (!strcmp (brickinfo->path,  path) &&
                    !strcmp (brickinfo->hostname, hostname)) {
                        cmd_replica_index = index/(replica_count);
                        goto out;
                }
                index++;
        }


out:
        if (ret)
                cmd_replica_index = -1;

        return cmd_replica_index;
}

int
_select_rxlators_with_local_bricks (xlator_t *this, glusterd_volinfo_t *volinfo,
                                    dict_t *dict, cli_cmd_type type)
{
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_conf_t         *priv   = NULL;
        int                     index = 0;
        int                     rxlator_count = 0;
        int                     replica_count = 0;
        gf_boolean_t            add     = _gf_false;
        int                     ret = 0;
        int                     cmd_replica_index = -1;

        priv = this->private;
        replica_count = volinfo->replica_count;

        if (type == PER_REPLICA) {

                cmd_replica_index = get_replica_index_for_per_replica_cmd
                                    (volinfo, dict);
                if (cmd_replica_index == -1) {
                        ret = -1;
                        goto err;
                }
        }

        index = 1;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (uuid_is_null (brickinfo->uuid))
                        (void)glusterd_resolve_brick (brickinfo);

                switch (type) {
                        case ALL_REPLICA:
                                if (!uuid_compare (MY_UUID, brickinfo->uuid))
                                        add = _gf_true;
                                break;
                        case PER_REPLICA:
                                if (!uuid_compare (MY_UUID, brickinfo->uuid) &&
                                 ((index-1)/replica_count == cmd_replica_index))

                                                add = _gf_true;
                                break;
                }

                if (index % replica_count == 0) {
                        if (add) {
                                _add_rxlator_to_dict (dict, volinfo->volname,
                                                      (index-1)/replica_count,
                                                      rxlator_count);
                                rxlator_count++;
                        }
                        add = _gf_false;
                }

                index++;
        }
err:
        if (ret)
                rxlator_count = -1;

        return rxlator_count;
}

int
_select_rxlators_for_full_self_heal (xlator_t *this,
                                     glusterd_volinfo_t *volinfo,
                                     dict_t *dict)
{
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_conf_t         *priv   = NULL;
        int                     index = 1;
        int                     rxlator_count = 0;
        int                     replica_count = 0;
        uuid_t                  candidate = {0};

        priv = this->private;
        replica_count = volinfo->replica_count;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (uuid_is_null (brickinfo->uuid))
                        (void)glusterd_resolve_brick (brickinfo);

                if (uuid_compare (brickinfo->uuid, candidate) > 0)
                        uuid_copy (candidate, brickinfo->uuid);

                if (index % replica_count == 0) {
                        if (!uuid_compare (MY_UUID, candidate)) {
                                _add_rxlator_to_dict (dict, volinfo->volname,
                                                      (index-1)/replica_count,
                                                      rxlator_count);
                                rxlator_count++;
                        }
                        uuid_clear (candidate);
                }

                index++;
        }
        return rxlator_count;
}


static int
fill_shd_status_for_local_bricks (dict_t *dict, glusterd_volinfo_t *volinfo,
                                  cli_cmd_type type, dict_t *req_dict)
{
        glusterd_brickinfo_t    *brickinfo = NULL;
        char                    msg[1024] = {0,};
        char                    key[1024]  = {0,};
        char                    value[1024] = {0,};
        int                     index = 0;
        int                     ret = 0;
        xlator_t               *this = NULL;
        int                     cmd_replica_index = -1;

        this = THIS;
        snprintf (msg, sizeof (msg), "self-heal-daemon is not running on");

        if (type == PER_REPLICA) {
                cmd_replica_index = get_replica_index_for_per_replica_cmd
                                    (volinfo, req_dict);
                if (cmd_replica_index == -1) {
                        gf_log (THIS->name, GF_LOG_ERROR, "Could not find the "
                                "replica index for per replica type command");
                        ret = -1;
                        goto out;
                }
        }

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (uuid_is_null (brickinfo->uuid))
                        (void)glusterd_resolve_brick (brickinfo);

                if (uuid_compare (MY_UUID, brickinfo->uuid)) {
                        index++;
                        continue;
                }

                if (type == PER_REPLICA) {
                      if (cmd_replica_index != (index/volinfo->replica_count)) {
                              index++;
                              continue;
                        }

                }
                snprintf (key, sizeof (key), "%d-status",index);
                snprintf (value, sizeof (value), "%s %s",msg,
                          uuid_utoa(MY_UUID));
                ret = dict_set_dynstr (dict, key, gf_strdup(value));
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Unable to"
                                "set the dictionary for shd status msg");
                        goto out;
                }
                snprintf (key, sizeof (key), "%d-shd-status",index);
                ret = dict_set_str (dict, key, "off");
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Unable to"
                                " set dictionary for shd status msg");
                        goto out;
                }

                index++;
        }

out:
        return ret;

}


static int
glusterd_bricks_select_heal_volume (dict_t *dict, char **op_errstr,
                                    struct list_head *selected,
                                    dict_t *rsp_dict)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        xlator_t                                *this = NULL;
        char                                    msg[2048] = {0,};
        glusterd_pending_node_t                 *pending_node = NULL;
        gf_xl_afr_op_t                          heal_op = GF_AFR_OP_INVALID;
        int                                     rxlator_count = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "volume name get failed");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), "Volume %s does not exist",
                          volname);

                *op_errstr = gf_strdup (msg);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        ret = dict_get_int32 (dict, "heal-op", (int32_t*)&heal_op);
        if (ret || (heal_op == GF_AFR_OP_INVALID)) {
                gf_log ("glusterd", GF_LOG_ERROR, "heal op invalid");
                goto out;
        }

        switch (heal_op) {
                case GF_AFR_OP_INDEX_SUMMARY:
                case GF_AFR_OP_STATISTICS_HEAL_COUNT:
                if (!glusterd_is_nodesvc_online ("glustershd")) {
                        if (!rsp_dict) {
                                gf_log (this->name, GF_LOG_ERROR, "Received "
                                        "empty ctx.");
                                goto out;
                        }

                        ret = fill_shd_status_for_local_bricks (rsp_dict,
                                                                volinfo,
                                                                ALL_REPLICA,
                                                                dict);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR, "Unable to "
                                        "fill the shd status for the local "
                                        "bricks");
                        goto out;

                }
                break;
                case GF_AFR_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
                if (!glusterd_is_nodesvc_online ("glustershd")) {
                        if (!rsp_dict) {
                                gf_log (this->name, GF_LOG_ERROR, "Received "
                                        "empty ctx.");
                                goto out;
                        }
                        ret = fill_shd_status_for_local_bricks (rsp_dict,
                                                                volinfo,
                                                                PER_REPLICA,
                                                                dict);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR, "Unable to "
                                        "fill the shd status for the local"
                                        " bricks.");
                        goto out;

                }
                break;
                default:
                        break;
        }


        switch (heal_op) {
        case GF_AFR_OP_HEAL_FULL:
                rxlator_count = _select_rxlators_for_full_self_heal (this,
                                                                     volinfo,
                                                                     dict);
                break;
        case GF_AFR_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
                rxlator_count = _select_rxlators_with_local_bricks (this,
                                                                   volinfo,
                                                                   dict,
                                                                   PER_REPLICA);
                break;
        default:
                rxlator_count = _select_rxlators_with_local_bricks (this,
                                                                    volinfo,
                                                                    dict,
                                                                   ALL_REPLICA);
                break;
        }
        if (!rxlator_count)
                goto out;
        if (rxlator_count == -1){
                gf_log (this->name, GF_LOG_ERROR, "Could not determine the"
                        "translator count");
                ret = -1;
                goto out;
        }

        ret = dict_set_int32 (dict, "count", rxlator_count);
        if (ret)
                goto out;

        pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                  gf_gld_mt_pending_node_t);
        if (!pending_node) {
                ret = -1;
                goto out;
        } else {
                pending_node->node = priv->shd;
                pending_node->type = GD_NODE_SHD;
                list_add_tail (&pending_node->list, selected);
                pending_node = NULL;
        }

out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning ret %d", ret);
        return ret;

}

static int
glusterd_bricks_select_rebalance_volume (dict_t *dict, char **op_errstr,
                                         struct list_head *selected)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        xlator_t                                *this = NULL;
        char                                    msg[2048] = {0,};
        glusterd_pending_node_t                 *pending_node = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "volume name get failed");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), "Volume %s does not exist",
                          volname);

                *op_errstr = gf_strdup (msg);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                goto out;
        }
        pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                  gf_gld_mt_pending_node_t);
        if (!pending_node) {
                ret = -1;
                goto out;
        } else {
                pending_node->node = volinfo;
                pending_node->type = GD_NODE_REBALANCE;
                list_add_tail (&pending_node->list,
                               &opinfo.pending_bricks);
                pending_node = NULL;
        }

out:
        return ret;
}

static int
glusterd_bricks_select_status_volume (dict_t *dict, char **op_errstr,
                                      struct list_head *selected)
{
        int                     ret = -1;
        int                     cmd = 0;
        int                     brick_index = -1;
        char                    *volname = NULL;
        char                    *brickname = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_pending_node_t *pending_node = NULL;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (dict);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_int32 (dict, "cmd", &cmd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get status type");
                goto out;
        }

        if (cmd & GF_CLI_STATUS_ALL)
                goto out;

        switch (cmd & GF_CLI_STATUS_MASK) {
        case GF_CLI_STATUS_MEM:
        case GF_CLI_STATUS_CLIENTS:
        case GF_CLI_STATUS_INODE:
        case GF_CLI_STATUS_FD:
        case GF_CLI_STATUS_CALLPOOL:
        case GF_CLI_STATUS_NFS:
        case GF_CLI_STATUS_SHD:
        case GF_CLI_STATUS_QUOTAD:
                break;
        default:
                goto out;
        }
        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volname");
                goto out;
        }
        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                goto out;
        }

        if ( (cmd & GF_CLI_STATUS_BRICK) != 0) {
                ret = dict_get_str (dict, "brick", &brickname);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to get brick");
                        goto out;
                }
                ret = glusterd_volume_brickinfo_get_by_brick (brickname,
                                                              volinfo,
                                                              &brickinfo);
                if (ret)
                        goto out;

                if (uuid_compare (brickinfo->uuid, MY_UUID)||
                    !glusterd_is_brick_started (brickinfo))
                        goto out;

                pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                          gf_gld_mt_pending_node_t);
                if (!pending_node) {
                        ret = -1;
                        goto out;
                }
                pending_node->node = brickinfo;
                pending_node->type = GD_NODE_BRICK;
                pending_node->index = 0;
                list_add_tail (&pending_node->list, selected);

                ret = 0;
        } else if ((cmd & GF_CLI_STATUS_NFS) != 0) {
                if (!glusterd_is_nodesvc_online ("nfs")) {
                        ret = -1;
                        gf_log (this->name, GF_LOG_ERROR,
                                "NFS server is not running");
                        goto out;
                }
                pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                          gf_gld_mt_pending_node_t);
                if (!pending_node) {
                        ret = -1;
                        goto out;
                }
                pending_node->node = priv->nfs;
                pending_node->type = GD_NODE_NFS;
                pending_node->index = 0;
                list_add_tail (&pending_node->list, selected);

                ret = 0;
        } else if ((cmd & GF_CLI_STATUS_SHD) != 0) {
                if (!glusterd_is_nodesvc_online ("glustershd")) {
                        ret = -1;
                        gf_log (this->name, GF_LOG_ERROR,
                                "Self-heal daemon is not running");
                        goto out;
                }
                pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                          gf_gld_mt_pending_node_t);
                if (!pending_node) {
                        ret = -1;
                        goto out;
                }
                pending_node->node = priv->shd;
                pending_node->type = GD_NODE_SHD;
                pending_node->index = 0;
                list_add_tail (&pending_node->list, selected);

                ret = 0;
        } else if ((cmd & GF_CLI_STATUS_QUOTAD) != 0) {
                if (!glusterd_is_nodesvc_online ("quotad")) {
                        gf_log (this->name, GF_LOG_ERROR, "Quotad is not "
                                "running");
                        ret = -1;
                        goto out;
                }
                pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                          gf_gld_mt_pending_node_t);
                if (!pending_node) {
                        ret = -1;
                        goto out;
                }
                pending_node->node = priv->quotad;
                pending_node->type = GD_NODE_QUOTAD;
                pending_node->index = 0;
                list_add_tail (&pending_node->list, selected);

                ret = 0;
        } else {
                list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                        brick_index++;
                        if (uuid_compare (brickinfo->uuid, MY_UUID) ||
                            !glusterd_is_brick_started (brickinfo)) {
                                continue;
                        }
                        pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                                  gf_gld_mt_pending_node_t);
                        if (!pending_node) {
                                ret = -1;
                                gf_log (THIS->name ,GF_LOG_ERROR,
                                        "Unable to allocate memory");
                                goto out;
                        }
                        pending_node->node = brickinfo;
                        pending_node->type = GD_NODE_BRICK;
                        pending_node->index = brick_index;
                        list_add_tail (&pending_node->list, selected);
                        pending_node = NULL;
                }
        }
out:
        return ret;
}

static int
glusterd_op_ac_send_brick_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                             ret = 0;
        rpc_clnt_procedure_t            *proc = NULL;
        glusterd_conf_t                 *priv = NULL;
        xlator_t                        *this = NULL;
        glusterd_op_t                   op = GD_OP_NONE;
        glusterd_req_ctx_t              *req_ctx = NULL;
        char                            *op_errstr = NULL;

        this = THIS;
        priv = this->private;

        if (ctx) {
                req_ctx = ctx;
        } else {
                req_ctx = GF_CALLOC (1, sizeof (*req_ctx),
                                     gf_gld_mt_op_allack_ctx_t);
                op = glusterd_op_get_op ();
                req_ctx->op = op;
                uuid_copy (req_ctx->uuid, MY_UUID);
                ret = glusterd_op_build_payload (&req_ctx->dict, &op_errstr,
                                                 NULL);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, LOGSTR_BUILD_PAYLOAD,
                                gd_op_list[op]);
                        if (op_errstr == NULL)
                                gf_asprintf (&op_errstr,
                                             OPERRSTR_BUILD_PAYLOAD);
                        opinfo.op_errstr = op_errstr;
                        goto out;
                }
        }

        proc = &priv->gfs_mgmt->proctable[GLUSTERD_BRICK_OP];
        if (proc->fn) {
                ret = proc->fn (NULL, this, req_ctx);
                if (ret)
                        goto out;
        }

        if (!opinfo.pending_count && !opinfo.brick_pending_count) {
                glusterd_clear_pending_nodes (&opinfo.pending_bricks);
                ret = glusterd_op_sm_inject_event (GD_OP_EVENT_ALL_ACK, req_ctx);
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}


static int
glusterd_op_ac_rcvd_brick_op_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        glusterd_op_brick_rsp_ctx_t *ev_ctx = NULL;
        char                        *op_errstr = NULL;
        glusterd_op_t               op = GD_OP_NONE;
        gd_node_type                type = GD_NODE_NONE;
        dict_t                      *op_ctx = NULL;
        glusterd_req_ctx_t          *req_ctx = NULL;
        void                        *pending_entry = NULL;
        xlator_t                    *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (event);
        GF_ASSERT (ctx);
        ev_ctx = ctx;

        req_ctx = ev_ctx->commit_ctx;
        GF_ASSERT (req_ctx);

        op = req_ctx->op;
        op_ctx = glusterd_op_get_ctx ();
        pending_entry = ev_ctx->pending_node->node;
        type = ev_ctx->pending_node->type;

        ret = glusterd_remove_pending_entry (&opinfo.pending_bricks,
                                             pending_entry);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "unknown response received ");
                ret = -1;
                goto out;
        }

        if (opinfo.brick_pending_count > 0)
                opinfo.brick_pending_count--;

        glusterd_handle_node_rsp (req_ctx->dict, pending_entry, op, ev_ctx->rsp_dict,
                                  op_ctx, &op_errstr, type);

        if (opinfo.brick_pending_count > 0)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_ALL_ACK, ev_ctx->commit_ctx);

out:
        if (ev_ctx->rsp_dict)
                dict_unref (ev_ctx->rsp_dict);
        GF_FREE (ev_ctx);
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
glusterd_op_bricks_select (glusterd_op_t op, dict_t *dict, char **op_errstr,
                           struct list_head *selected, dict_t *rsp_dict)
{
        int     ret = 0;

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (op > GD_OP_NONE);
        GF_ASSERT (op < GD_OP_MAX);

        switch (op) {
        case GD_OP_STOP_VOLUME:
                ret = glusterd_bricks_select_stop_volume (dict, op_errstr,
                                                          selected);
                break;

        case GD_OP_REMOVE_BRICK:
                ret = glusterd_bricks_select_remove_brick (dict, op_errstr,
                                                           selected);
                break;

        case GD_OP_PROFILE_VOLUME:
                ret = glusterd_bricks_select_profile_volume (dict, op_errstr,
                                                             selected);
                break;

        case GD_OP_HEAL_VOLUME:
                ret = glusterd_bricks_select_heal_volume (dict, op_errstr,
                                                          selected, rsp_dict);
                break;

        case GD_OP_STATUS_VOLUME:
                ret = glusterd_bricks_select_status_volume (dict, op_errstr,
                                                            selected);
                break;

        case GD_OP_DEFRAG_BRICK_VOLUME:
                ret = glusterd_bricks_select_rebalance_volume (dict, op_errstr,
                                                               selected);
                break;

        default:
                break;
         }

        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

glusterd_op_sm_t glusterd_op_state_default [] = {
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_send_lock},//EVENT_START_LOCK
        {GD_OP_STATE_LOCKED, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_RCVD_ACC
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_RCVD_RJT
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_ALL_ACK
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_lock_sent [] = {
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_rcvd_lock_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_send_stage_op}, //EVENT_ALL_ACC
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_send_unlock_drain}, //EVENT_RCVD_RJT
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_ALL_ACK
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_locked [] = {
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_LOCKED, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_RCVD_ACC
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_RCVD_RJT
        {GD_OP_STATE_STAGED, glusterd_op_ac_stage_op}, //EVENT_STAGE_OP
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_ALL_ACK
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_local_unlock}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_stage_op_sent [] = {
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_rcvd_stage_op_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_send_brick_op}, //EVENT_ALL_ACC
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_send_brick_op}, //EVENT_STAGE_ACC
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_STAGE_OP_FAILED,   glusterd_op_ac_stage_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_ALL_ACK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_stage_op_failed [] = {
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_stage_op_failed}, //EVENT_RCVD_ACC
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_stage_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock}, //EVENT_ALL_ACK
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_staged [] = {
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_STAGED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_STAGED, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_RCVD_ACC
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_RCVD_RJT
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_send_brick_op}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_ALL_ACK
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_local_unlock}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_brick_op_sent [] = {
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_rcvd_brick_op_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_BRICK_OP_FAILED,   glusterd_op_ac_brick_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_BRICK_OP
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_send_commit_op}, //EVENT_ALL_ACK
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_brick_op_failed [] = {
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_brick_op_failed}, //EVENT_RCVD_ACC
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_brick_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_BRICK_OP
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock}, //EVENT_ALL_ACK
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_brick_committed [] = {
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_rcvd_brick_op_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_brick_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_COMMITED, glusterd_op_ac_commit_op}, //EVENT_ALL_ACK
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_local_unlock}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_brick_commit_failed [] = {
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_brick_op_failed}, //EVENT_RCVD_ACC
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_brick_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_send_commit_failed}, //EVENT_ALL_ACK
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_local_unlock}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_commit_op_failed [] = {
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_commit_op_failed}, //EVENT_RCVD_ACC
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_commit_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock}, //EVENT_ALL_ACK
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_commit_op_sent [] = {
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_rcvd_commit_op_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_UNLOCK_SENT,    glusterd_op_ac_send_unlock}, //EVENT_ALL_ACC
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_UNLOCK_SENT,    glusterd_op_ac_send_unlock}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_commit_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT,        glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_ALL_ACK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_committed [] = {
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_COMMITED, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_RCVD_ACC
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_RCVD_RJT
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_ALL_ACK
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_local_unlock}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_unlock_sent [] = {
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_rcvd_unlock_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlocked_all}, //EVENT_ALL_ACC
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_rcvd_unlock_acc}, //EVENT_RCVD_RJT
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_ALL_ACK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_ack_drain [] = {
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_send_unlock_drain}, //EVENT_RCVD_ACC
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_send_unlock_drain}, //EVENT_RCVD_RJT
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock}, //EVENT_ALL_ACK
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_LOCAL_UNLOCK_NO_RESP
        {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t *glusterd_op_state_table [] = {
        glusterd_op_state_default,
        glusterd_op_state_lock_sent,
        glusterd_op_state_locked,
        glusterd_op_state_stage_op_sent,
        glusterd_op_state_staged,
        glusterd_op_state_commit_op_sent,
        glusterd_op_state_committed,
        glusterd_op_state_unlock_sent,
        glusterd_op_state_stage_op_failed,
        glusterd_op_state_commit_op_failed,
        glusterd_op_state_brick_op_sent,
        glusterd_op_state_brick_op_failed,
        glusterd_op_state_brick_committed,
        glusterd_op_state_brick_commit_failed,
        glusterd_op_state_ack_drain
};

int
glusterd_op_sm_new_event (glusterd_op_sm_event_type_t event_type,
                          glusterd_op_sm_event_t **new_event)
{
        glusterd_op_sm_event_t      *event = NULL;

        GF_ASSERT (new_event);
        GF_ASSERT (GD_OP_EVENT_NONE <= event_type &&
                        GD_OP_EVENT_MAX > event_type);

        event = GF_CALLOC (1, sizeof (*event), gf_gld_mt_op_sm_event_t);

        if (!event)
                return -1;

        *new_event = event;
        event->event = event_type;
        INIT_LIST_HEAD (&event->list);

        return 0;
}

int
glusterd_op_sm_inject_event (glusterd_op_sm_event_type_t event_type,
                             void *ctx)
{
        int32_t                 ret = -1;
        glusterd_op_sm_event_t  *event = NULL;

        GF_ASSERT (event_type < GD_OP_EVENT_MAX &&
                        event_type >= GD_OP_EVENT_NONE);

        ret = glusterd_op_sm_new_event (event_type, &event);

        if (ret)
                goto out;

        event->ctx = ctx;

        gf_log (THIS->name, GF_LOG_DEBUG, "Enqueue event: '%s'",
                glusterd_op_sm_event_name_get (event->event));
        list_add_tail (&event->list, &gd_op_sm_queue);

out:
        return ret;
}

void
glusterd_destroy_req_ctx (glusterd_req_ctx_t *ctx)
{
        if (!ctx)
                return;
        if (ctx->dict)
                dict_unref (ctx->dict);
        GF_FREE (ctx);
}

void
glusterd_destroy_local_unlock_ctx (uuid_t *ctx)
{
        if (!ctx)
                return;
        GF_FREE (ctx);
}

void
glusterd_destroy_op_event_ctx (glusterd_op_sm_event_t *event)
{
        if (!event)
                return;

        switch (event->event) {
        case GD_OP_EVENT_LOCK:
        case GD_OP_EVENT_UNLOCK:
                glusterd_destroy_lock_ctx (event->ctx);
                break;
        case GD_OP_EVENT_STAGE_OP:
        case GD_OP_EVENT_ALL_ACK:
                glusterd_destroy_req_ctx (event->ctx);
                break;
        case GD_OP_EVENT_LOCAL_UNLOCK_NO_RESP:
                glusterd_destroy_local_unlock_ctx (event->ctx);
                break;
        default:
                break;
        }
}

int
glusterd_op_sm ()
{
        glusterd_op_sm_event_t          *event = NULL;
        glusterd_op_sm_event_t          *tmp = NULL;
        int                             ret = -1;
        int                             lock_err = 0;
        glusterd_op_sm_ac_fn            handler = NULL;
        glusterd_op_sm_t                *state = NULL;
        glusterd_op_sm_event_type_t     event_type = GD_OP_EVENT_NONE;
        xlator_t                        *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        if ((lock_err = pthread_mutex_trylock (&gd_op_sm_lock))) {
                gf_log (this->name, GF_LOG_ERROR, "lock failed due to %s",
                        strerror (lock_err));
                goto lock_failed;
        }

        while (!list_empty (&gd_op_sm_queue)) {

                list_for_each_entry_safe (event, tmp, &gd_op_sm_queue, list) {

                        list_del_init (&event->list);
                        event_type = event->event;
                        gf_log (this->name, GF_LOG_DEBUG, "Dequeued event of "
                                "type: '%s'",
                                glusterd_op_sm_event_name_get(event_type));

                        state = glusterd_op_state_table[opinfo.state.state];

                        GF_ASSERT (state);

                        handler = state[event_type].handler;
                        GF_ASSERT (handler);

                        ret = handler (event, event->ctx);

                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "handler returned: %d", ret);
                                glusterd_destroy_op_event_ctx (event);
                                GF_FREE (event);
                                continue;
                        }

                        ret = glusterd_op_sm_transition_state (&opinfo, state,
                                                                event_type);

                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Unable to transition"
                                        "state from '%s' to '%s'",
                         glusterd_op_sm_state_name_get(opinfo.state.state),
                         glusterd_op_sm_state_name_get(state[event_type].next_state));
                                (void ) pthread_mutex_unlock (&gd_op_sm_lock);
                                return ret;
                        }

                        glusterd_destroy_op_event_ctx (event);
                        GF_FREE (event);
                }
        }


        (void ) pthread_mutex_unlock (&gd_op_sm_lock);
        ret = 0;

lock_failed:

        return ret;
}

int32_t
glusterd_op_set_op (glusterd_op_t op)
{

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        opinfo.op = op;

        return 0;

}

int32_t
glusterd_op_get_op ()
{

        return opinfo.op;

}

int32_t
glusterd_op_set_req (rpcsvc_request_t *req)
{

        GF_ASSERT (req);
        opinfo.req = req;
        return 0;
}

int32_t
glusterd_op_clear_op (glusterd_op_t op)
{

        opinfo.op = GD_OP_NONE;

        return 0;

}

int32_t
glusterd_op_init_ctx (glusterd_op_t op)
{
        int     ret = 0;
        dict_t *dict = NULL;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (GD_OP_NONE < op && op < GD_OP_MAX);

        if (_gf_false == glusterd_need_brick_op (op)) {
                gf_log (this->name, GF_LOG_DEBUG, "Received op: %s, returning",
                        gd_op_list[op]);
                goto out;
        }
        dict = dict_new ();
        if (dict == NULL) {
                ret = -1;
                goto out;
        }
        ret = glusterd_op_set_ctx (dict);
        if (ret)
                goto out;
out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}



int32_t
glusterd_op_fini_ctx ()
{
        dict_t *dict = NULL;

        dict = glusterd_op_get_ctx ();
        if (dict)
                dict_unref (dict);

        glusterd_op_reset_ctx ();
        return 0;
}



int32_t
glusterd_op_free_ctx (glusterd_op_t op, void *ctx)
{

        if (ctx) {
                switch (op) {
                case GD_OP_CREATE_VOLUME:
                case GD_OP_DELETE_VOLUME:
                case GD_OP_STOP_VOLUME:
                case GD_OP_ADD_BRICK:
                case GD_OP_REMOVE_BRICK:
                case GD_OP_REPLACE_BRICK:
                case GD_OP_LOG_ROTATE:
                case GD_OP_SYNC_VOLUME:
                case GD_OP_SET_VOLUME:
                case GD_OP_START_VOLUME:
                case GD_OP_RESET_VOLUME:
                case GD_OP_GSYNC_SET:
                case GD_OP_QUOTA:
                case GD_OP_PROFILE_VOLUME:
                case GD_OP_STATUS_VOLUME:
                case GD_OP_REBALANCE:
                case GD_OP_HEAL_VOLUME:
                case GD_OP_STATEDUMP_VOLUME:
                case GD_OP_CLEARLOCKS_VOLUME:
                case GD_OP_DEFRAG_BRICK_VOLUME:
                        dict_unref (ctx);
                        break;
                default:
                        GF_ASSERT (0);
                        break;
                }
        }

        glusterd_op_reset_ctx ();
        return 0;

}

void *
glusterd_op_get_ctx ()
{

        return opinfo.op_ctx;

}

int
glusterd_op_sm_init ()
{
        INIT_LIST_HEAD (&gd_op_sm_queue);
        pthread_mutex_init (&gd_op_sm_lock, NULL);
        return 0;
}
