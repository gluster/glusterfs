/*
  Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
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
#include <time.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/mount.h>

#include <libgen.h>
#include <uuid/uuid.h>

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
#include "glusterd-volgen.h"
#include "syscall.h"
#include "cli1-xdr.h"
#include "common-utils.h"
#include "run.h"

#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>

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

int
glusterd_brick_op_build_payload (glusterd_op_t op, glusterd_brickinfo_t *brickinfo,
                                 gd1_mgmt_brick_op_req **req, dict_t *dict)
{
        int                     ret = -1;
        gd1_mgmt_brick_op_req   *brick_req = NULL;

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);
        GF_ASSERT (req);


        switch (op) {
        case GD_OP_REMOVE_BRICK:
        case GD_OP_STOP_VOLUME:
                brick_req = GF_CALLOC (1, sizeof (*brick_req),
                                       gf_gld_mt_mop_brick_req_t);
                if (!brick_req) {
                        gf_log ("", GF_LOG_ERROR, "Out of Memory");
                        goto out;
                }
                brick_req->op = GLUSTERD_BRICK_TERMINATE;
                brick_req->name = "";
        break;
        case GD_OP_PROFILE_VOLUME:
                brick_req = GF_CALLOC (1, sizeof (*brick_req),
                                       gf_gld_mt_mop_brick_req_t);

                if (!brick_req) {
                        gf_log ("", GF_LOG_ERROR, "Out of Memory");
                        goto out;
                }

                brick_req->op = GLUSTERD_BRICK_XLATOR_INFO;
                brick_req->name = brickinfo->path;

                break;
        case GD_OP_HEAL_VOLUME:
        {
                brick_req = GF_CALLOC (1, sizeof (*brick_req),
                                       gf_gld_mt_mop_brick_req_t);
                if (!brick_req)
                        goto out;

                brick_req->op = GLUSTERD_BRICK_XLATOR_HEAL;
                brick_req->name = "";
        }
                break;
        default:
                goto out;
        break;
        }

        ret = dict_allocate_and_serialize (dict, &brick_req->input.input_val,
                                           (size_t*)&brick_req->input.input_len);
        if (ret)
                goto out;
        *req = brick_req;
        ret = 0;

out:
        if (ret && brick_req)
                GF_FREE (brick_req);
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


static int
glusterd_op_stage_set_volume (dict_t *dict, char **op_errstr)
{
        int                                      ret           = 0;
        char                                    *volname       = NULL;
        int                                      exists        = 0;
        char                                    *key               = NULL;
        char                                    *key_fixed     = NULL;
        char                                    *value         = NULL;
        char                                     str[100]      = {0, };
        int                                      count         = 0;
        int                                      dict_count    = 0;
        char                                     errstr[2048]  = {0, };
        glusterd_volinfo_t                      *volinfo       = NULL;
        dict_t                                  *val_dict      = NULL;
        gf_boolean_t                             global_opt    = _gf_false;
        glusterd_volinfo_t                      *voliter       = NULL;
        glusterd_conf_t                         *priv          = NULL;
        xlator_t                                *this          = NULL;

        GF_ASSERT (dict);
        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        val_dict = dict_new();
        if (!val_dict)
                goto out;

        ret = dict_get_int32 (dict, "count", &dict_count);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Count(dict),not set in Volume-Set");
                goto out;
        }

        if ( dict_count == 0 ) {
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
                        gf_log ("", GF_LOG_ERROR, "libxml not present in the"
                                   "system");
                        *op_errstr = gf_strdup ("Error: xml libraries not "
                                                "present to produce xml-output");
                        goto out;
#endif
                }
                gf_log ("", GF_LOG_ERROR, "No options received ");
                *op_errstr = gf_strdup ("Options not specified");
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        if (!exists) {
                snprintf (errstr, sizeof (errstr), "Volume %s does not exist",
                          volname);
                gf_log ("", GF_LOG_ERROR, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        for ( count = 1; ret != 1 ; count++ ) {
                global_opt = _gf_false;
                sprintf (str, "key%d", count);
                ret = dict_get_str (dict, str, &key);


                if (ret)
                        break;

                exists = glusterd_check_option_exists (key, &key_fixed);
                if (exists == -1) {
                        ret = -1;
                        goto out;
                }
                if (!exists) {
                        gf_log ("", GF_LOG_ERROR, "Option with name: %s "
                                "does not exist", key);
                        ret = snprintf (errstr, 2048,
                                       "option : %s does not exist",
                                       key);
                        if (key_fixed)
                                snprintf (errstr + ret, 2048 - ret,
                                          "\nDid you mean %s?", key_fixed);
                        *op_errstr = gf_strdup (errstr);
                        ret = -1;
                        goto out;
                }

                sprintf (str, "value%d", count);
                ret = dict_get_str (dict, str, &value);

                if (ret) {
                        gf_log ("", GF_LOG_ERROR,
                                "invalid key,value pair in 'volume set'");
                        ret = -1;
                        goto out;
                }

                if (key_fixed)
                        key = key_fixed;

                ret = glusterd_check_globaloption (key);
                if (ret)
                        global_opt = _gf_true;

                ret = dict_set_str (val_dict, key, value);

                if (ret) {
                        gf_log ("", GF_LOG_ERROR,
                                "Unable to set the options in 'volume set'");
                        ret = -1;
                        goto out;
                }

                *op_errstr = NULL;
                if (!global_opt)
                        ret = glusterd_validate_reconfopts (volinfo, val_dict, op_errstr);
                else {
                        voliter = NULL;
                        list_for_each_entry (voliter, &priv->volumes, vol_list) {
                                ret = glusterd_validate_globalopts (voliter, val_dict, op_errstr);
                                if (ret)
                                        break;
                        }
                }

                if (ret) {
                        gf_log ("glusterd", GF_LOG_DEBUG, "Could not create temp "
                                "volfile, some option failed: %s", *op_errstr);
                        goto out;
                }
                dict_del (val_dict, key);

                if (key_fixed) {
                        GF_FREE (key_fixed);
                        key_fixed = NULL;
                }
        }


        ret = 0;

out:
        if (val_dict)
                dict_unref (val_dict);

        if (key_fixed)
                GF_FREE (key_fixed);

        if (ret) {
                if (!(*op_errstr)) {
                        *op_errstr = gf_strdup ("Error, Validation Failed");
                        gf_log ("glsuterd", GF_LOG_DEBUG,
                                "Error, Cannot Validate option :%s",
                                *op_errstr);
                } else {
                        gf_log ("glsuterd", GF_LOG_DEBUG,
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

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);

        if (!exists) {
                snprintf (msg, sizeof (msg), "Volume %s does not "
                          "exist", volname);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "key", &key);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get option key");
                goto out;
        }
        if (strcmp(key, "all")) {
                exists = glusterd_check_option_exists (key, &key_fixed);
                if (exists == -1) {
                        ret = -1;
                        goto out;
                }
                if (!exists) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Option %s does not exist", key);
                        ret = snprintf (msg, 2048,
                                        "Option %s does not exist", key);
                        if (key_fixed)
                                snprintf (msg + ret, 2048 - ret,
                                          "\nDid you mean %s?", key_fixed);
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }
        }

out:
        if (key_fixed)
                GF_FREE (key_fixed);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

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

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret) {
                snprintf (msg, sizeof (msg), "hostname couldn't be "
                          "retrieved from msg");
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = glusterd_is_local_addr (hostname);
        if (ret) {
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
        } else {

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
                } else {
                        ret = 0;
                }
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_stage_status_volume (dict_t *dict, char **op_errstr)
{
        int                 ret            = -1;
        gf_boolean_t        exists         = _gf_false;
        char               *volname        = NULL;
        glusterd_conf_t    *priv           = NULL;
        xlator_t           *this           = NULL;
        char msg[2048]                     = {0,};

        GF_ASSERT (dict);
        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT(priv);


        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        if (!exists) {
                snprintf (msg, sizeof(msg), "Volume %s does not exist", volname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);

                *op_errstr = gf_strdup(msg);
                ret = -1;
                goto out;
        }

        ret = 0;

 out:
        if (ret && !(*op_errstr))
                *op_errstr = gf_strdup ("Validation Failed for Status");

        gf_log ("glusterd", GF_LOG_DEBUG, "Returning: %d", ret);
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


void
_delete_reconfig_opt (dict_t *this, char *key, data_t *value, void *data)
{
        int             exists = 0;
        int32_t         is_force = 0;

        GF_ASSERT (data);
        is_force = *((int32_t*)data);
        exists = glusterd_check_option_exists(key, NULL);

        if (exists != 1)
                goto out;

        if ((!is_force) &&
            (_gf_true == glusterd_check_voloption_flags (key,
                                                         OPT_FLAG_FORCE)))
                goto out;

        gf_log ("", GF_LOG_DEBUG, "deleting dict with key=%s,value=%s",
                key, value->data);
        dict_del (this, key);
out:
        return;
}

int
glusterd_options_reset (glusterd_volinfo_t *volinfo, char *key,
                        int32_t is_force)
{
        int                      ret = 0;
        data_t                  *value = NULL;

        gf_log ("", GF_LOG_DEBUG, "Received volume set reset command");

        GF_ASSERT (volinfo->dict);
        GF_ASSERT (key);

        if (!strncmp(key, "all", 3))
                dict_foreach (volinfo->dict, _delete_reconfig_opt, &is_force);
        else {
                value = dict_get (volinfo->dict, key);
                if (!value) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Could not get value");
                        goto out;
                }
                _delete_reconfig_opt (volinfo->dict, key, value, &is_force);
        }

        ret = glusterd_create_volfiles_and_notify_services (volinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to create volfile for"
                        " 'volume set'");
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
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


static int
glusterd_op_reset_volume (dict_t *dict)
{
        glusterd_volinfo_t      *volinfo    = NULL;
        int                     ret         = -1;
        char                    *volname    = NULL;
        char                    *key        = NULL;
        int32_t                 is_force    = 0;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name " );
                goto out;
        }

        ret = dict_get_int32 (dict, "force", &is_force);
        if (ret)
                is_force = 0;

        ret = dict_get_str (dict, "key", &key);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get option key");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = glusterd_options_reset (volinfo, key, is_force);

out:
        gf_log ("", GF_LOG_DEBUG, "'volume reset' returning %d", ret);
        return ret;

}


int
glusterd_stop_bricks (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t                    *brickinfo = NULL;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (glusterd_brick_stop (volinfo, brickinfo))
                        return -1;
        }

        return 0;
}

int
glusterd_start_bricks (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t                    *brickinfo = NULL;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (glusterd_brick_start (volinfo, brickinfo))
                        return -1;
        }

        return 0;
}

static int
glusterd_volset_help (dict_t *dict)
{
        int                     ret = -1;
        gf_boolean_t            xml_out = _gf_false;

        if (dict_get (dict, "help" ))
                xml_out = _gf_false;
        else if (dict_get (dict, "help-xml" ))
                xml_out = _gf_true;
        else
                goto out;

        ret = glusterd_get_volopt_content (xml_out);
 out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
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
        gf_boolean_t                             global_opt    = _gf_false;
        glusterd_volinfo_t                      *voliter = NULL;
        int32_t                                  dict_count = 0;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_int32 (dict, "count", &dict_count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Count(dict),not set in Volume-Set");
                goto out;
        }

        if ( dict_count == 0 ) {
                ret = glusterd_volset_help (dict);
                if (ret)
                        gf_log ("glusterd", GF_LOG_ERROR, "Volume set help"
                                                        "internal error");
                goto out;
        }

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

        for ( count = 1; ret != -1 ; count++ ) {

                global_opt = _gf_false;
                sprintf (str, "key%d", count);
                ret = dict_get_str (dict, str, &key);

                if (ret) {
                        break;
                }

                if (!ret) {
                        ret = glusterd_check_option_exists (key, &key_fixed);
                        GF_ASSERT (ret);
                        if (ret == -1) {
                                key_fixed = NULL;
                                goto out;
                        }
                        ret = 0;
                }

                ret = glusterd_check_globaloption (key);
                if (ret)
                        global_opt = _gf_true;

                sprintf (str, "value%d", count);
                ret = dict_get_str (dict, str, &value);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR,
                                "invalid key,value pair in 'volume set'");
                        ret = -1;
                        goto out;
                }

                if (!global_opt)
                        value = gf_strdup (value);

                if (!value) {
                        gf_log ("", GF_LOG_ERROR,
                                "Unable to set the options in 'volume set'");
                        ret = -1;
                        goto out;
                }

                if (key_fixed)
                        key = key_fixed;

                if (global_opt) {
                       list_for_each_entry (voliter, &priv->volumes, vol_list) {
                               value = gf_strdup (value);
                               ret = dict_set_dynstr (voliter->dict, key, value);
                               if (ret)
                                       goto out;
                       }
                }
                else {
                        ret = dict_set_dynstr (volinfo->dict, key, value);
                        if (ret)
                                goto out;
                }

                if (key_fixed) {
                        GF_FREE (key_fixed);

                        key_fixed = NULL;
                }
        }

        if ( count == 1 ) {
                gf_log ("", GF_LOG_ERROR, "No options received ");
                ret = -1;
                goto out;
        }

        if (!global_opt) {
                ret = glusterd_create_volfiles_and_notify_services (volinfo);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to create volfile for"
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
                                gf_log ("", GF_LOG_WARNING,
                                         "Unable to restart NFS-Server");
                                goto out;
                        }
                }

        }
        else {
                list_for_each_entry (voliter, &priv->volumes, vol_list) {
                        volinfo = voliter;
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

                        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                                ret = glusterd_nodesvcs_handle_reconfigure (volinfo);
                                if (ret) {
                                        gf_log ("", GF_LOG_WARNING,
                                                "Unable to restart NFS-Server");
                                        goto out;
                                }
                        }
                }
        }

        ret = 0;
 out:
        if (key_fixed)
                GF_FREE (key_fixed);
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
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

        if (glusterd_is_local_addr (hostname)) {
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
glusterd_op_status_volume (dict_t *dict, char **op_errstr,
                           dict_t *rsp_dict)
{
        int                     ret = -1;
        char                    *volname = NULL;
        int                     count = 0;
        int                     brick_count = 0;
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;
        int32_t                 brick_index = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);

        GF_ASSERT (dict);


        ret = dict_get_str (dict, "volname", &volname);
        if (!ret) {
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Volume with name: %s "
                                "does not exist", volname);
                        goto out;
                }
        }

        if (!rsp_dict) {
                //this should happen only on source
                ret = 0;
                rsp_dict = glusterd_op_get_ctx ();
        }

        if (volname) {
                list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                        if (!uuid_compare (brickinfo->uuid, priv->uuid)) {
                                ret = glusterd_add_brick_to_dict (volinfo,
                                                                  brickinfo,
                                                                  rsp_dict,
                                                                  brick_index);
                                count++;
                                brick_count = count;
                        }
                        brick_index++;
                }
        }

        ret = dict_set_int32 (rsp_dict, "count", brick_count);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_ac_none (glusterd_op_sm_event_t *event, void *ctx)
{
        int ret = 0;

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

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
                        if (ret)
                                continue;
                        pending_count++;
                }
        }

        opinfo.pending_count = pending_count;
        if (!opinfo.pending_count)
                ret = glusterd_op_sm_inject_all_acc ();

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

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

        /*ret = glusterd_unlock (priv->uuid);

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
                        if (ret)
                                continue;
                        pending_count++;
                }
        }

        opinfo.pending_count = pending_count;
        if (!opinfo.pending_count)
                ret = glusterd_op_sm_inject_all_acc ();

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

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

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

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

        gf_log ("", GF_LOG_DEBUG, "Lock Returned %d", ret);

        glusterd_op_lock_send_resp (lock_ctx->req, ret);

        return ret;
}

static int
glusterd_op_ac_unlock (glusterd_op_sm_event_t *event, void *ctx)
{
        int ret = 0;
        glusterd_op_lock_ctx_t   *lock_ctx = NULL;

        GF_ASSERT (event);
        GF_ASSERT (ctx);

        lock_ctx = (glusterd_op_lock_ctx_t *)ctx;

        ret = glusterd_unlock (lock_ctx->uuid);

        gf_log ("", GF_LOG_DEBUG, "Unlock Returned %d", ret);

        glusterd_op_unlock_send_resp (lock_ctx->req, ret);

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

        gf_log ("", GF_LOG_DEBUG, "Unlock Returned %d", ret);

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

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

out:
        return ret;
}

int
glusterd_op_build_payload (dict_t **req)
{
        int                     ret = -1;
        void                    *ctx = NULL;
        dict_t                  *req_dict = NULL;
        glusterd_op_t           op = GD_OP_NONE;

        GF_ASSERT (req);

        req_dict = dict_new ();
        if (!req_dict)
                goto out;

        op  = glusterd_op_get_op ();
        ctx = (void*)glusterd_op_get_ctx ();
        if (!ctx) {
                gf_log ("", GF_LOG_ERROR, "Null Context for "
                        "op %d", op);
                ret = -1;
                goto out;
        }

        switch (op) {
                case GD_OP_CREATE_VOLUME:
                        {
                                dict_t  *dict = ctx;
                                ++glusterfs_port;
                                ret = dict_set_int32 (dict, "port", glusterfs_port);
                                if (ret)
                                        goto out;
                                dict_copy (dict, req_dict);
                        }
                        break;

                case GD_OP_DELETE_VOLUME:
                case GD_OP_START_VOLUME:
                case GD_OP_STOP_VOLUME:
                case GD_OP_ADD_BRICK:
                case GD_OP_REPLACE_BRICK:
                case GD_OP_SET_VOLUME:
                case GD_OP_RESET_VOLUME:
                case GD_OP_REMOVE_BRICK:
                case GD_OP_LOG_FILENAME:
                case GD_OP_LOG_ROTATE:
                case GD_OP_SYNC_VOLUME:
                case GD_OP_QUOTA:
                case GD_OP_GSYNC_SET:
                case GD_OP_PROFILE_VOLUME:
                case GD_OP_LOG_LEVEL:
                case GD_OP_STATUS_VOLUME:
                case GD_OP_REBALANCE:
                case GD_OP_HEAL_VOLUME:
                case GD_OP_STATEDUMP_VOLUME:
                        {
                                dict_t  *dict = ctx;
                                dict_copy (dict, req_dict);
                        }
                        break;

                default:
                        break;
        }

        *req = req_dict;
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

        ret = glusterd_op_build_payload (&dict);
        if (ret)
                goto out;

        /* rsp_dict NULL from source */
        ret = glusterd_op_stage_validate (op, dict, &op_errstr, NULL);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Staging failed");
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
                                gf_log ("", GF_LOG_ERROR, "failed to set peerinfo");
                                goto out;
                        }

                        ret = proc->fn (NULL, this, dict);
                        if (ret)
                                continue;
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

        gf_log ("glusterd", GF_LOG_INFO, "Sent op req to %d peers",
                opinfo.pending_count);

        if (!opinfo.pending_count)
                ret = glusterd_op_sm_inject_all_acc ();

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;

}

static int32_t
glusterd_op_start_rb_timer (dict_t *dict)
{
        int32_t         op = 0;
        struct timeval  timeout = {0, };
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

        if (op == GF_REPLACE_OP_START ||
            op == GF_REPLACE_OP_ABORT)
                timeout.tv_sec  = 5;
        else
                timeout.tv_sec = 1;

        timeout.tv_usec = 0;


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

        op = glusterd_op_get_op ();
        ret = glusterd_op_build_payload (&dict);

        if (ret)
                goto out;

        ret = glusterd_op_commit_perform (op, dict, &op_errstr, NULL); //rsp_dict invalid for source
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Commit failed");
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
                                gf_log ("", GF_LOG_ERROR, "failed to set peerinfo");
                                goto out;
                        }
                        ret = proc->fn (NULL, this, dict);
                        if (ret)
                                continue;
                        pending_count++;
                }
        }

        opinfo.pending_count = pending_count;
        gf_log ("glusterd", GF_LOG_INFO, "Sent op req to %d peers",
                opinfo.pending_count);
out:
        if (dict)
                dict_unref (dict);
        if (ret) {
                glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT, NULL);
                opinfo.op_ret = ret;
        }

        if (!opinfo.pending_count) {
                if (op == GD_OP_REPLACE_BRICK) {
                        op_dict = glusterd_op_get_ctx ();
                        ret = glusterd_op_start_rb_timer (op_dict);

                } else {

                        ret = glusterd_op_sm_inject_all_acc ();
                }
                goto err;
        }

err:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

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
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

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
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

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
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_ac_brick_op_failed (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        glusterd_op_brick_rsp_ctx_t *ev_ctx = NULL;
        gf_boolean_t                free_errstr = _gf_false;

        GF_ASSERT (event);
        GF_ASSERT (ctx);
        ev_ctx = ctx;

        ret = glusterd_remove_pending_entry (&opinfo.pending_bricks, ev_ctx->pending_node->node);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "unknown response received ");
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
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_ac_rcvd_commit_op_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        dict_t                 *dict              = NULL;
        int                     ret               = 0;
        gf_boolean_t            commit_ack_inject = _gf_true;
        glusterd_op_t           op                = GD_OP_NONE;

        op = glusterd_op_get_op ();
        GF_ASSERT (event);

        if (opinfo.pending_count > 0)
                opinfo.pending_count--;

        if (opinfo.pending_count > 0)
                goto out;

        if (op == GD_OP_REPLACE_BRICK) {
                dict = glusterd_op_get_ctx ();
                if (!dict) {
                        gf_log (THIS->name, GF_LOG_CRITICAL, "Operation "
                                "context is not present.");
                        ret = -1;
                        goto out;
                }

                ret = glusterd_op_start_rb_timer (dict);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR, "Couldn't start "
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
                else if (!opinfo.pending_count)
                        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_COMMIT_ACC, NULL);

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

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

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


        priv = THIS->private;
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

        ret = glusterd_unlock (priv->uuid);

        /* unlock cant/shouldnt fail here!! */
        if (ret) {
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Unable to clear local lock, ret: %d", ret);
        } else {
                gf_log ("glusterd", GF_LOG_INFO, "Cleared local lock");
        }

        ret = glusterd_op_send_cli_response (op, op_ret,
                                             op_errno, req, ctx, op_errstr);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Responding to cli failed, ret: %d",
                        ret);
                //Ignore this error, else state machine blocks
                ret = 0;
        }

        glusterd_op_free_ctx (op, ctx);
        if (op_errstr && (strcmp (op_errstr, "")))
                GF_FREE (op_errstr);


        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
glusterd_op_ac_unlocked_all (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;

        GF_ASSERT (event);

        ret = glusterd_op_txn_complete ();

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

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

        GF_ASSERT (ctx);

        req_ctx = ctx;

        dict = req_ctx->dict;

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_log ("", GF_LOG_DEBUG,
                        "Out of memory");
                return -1;
        }

        status = glusterd_op_stage_validate (req_ctx->op, dict, &op_errstr,
                                             rsp_dict);

        if (status) {
                gf_log ("", GF_LOG_ERROR, "Validate failed: %d", status);
        }

        ret = glusterd_op_stage_send_resp (req_ctx->req, req_ctx->op,
                                           status, op_errstr, rsp_dict);

        if (op_errstr && (strcmp (op_errstr, "")))
                GF_FREE (op_errstr);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

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
                ret = _gf_true;
                break;
        default:
                ret = _gf_false;
        }

        return ret;
}

static dict_t*
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
        glusterd_req_ctx_t       *req_ctx = NULL;
        int32_t                   status     = 0;
        char                     *op_errstr  = NULL;
        dict_t                   *dict       = NULL;
        dict_t                   *rsp_dict = NULL;

        GF_ASSERT (ctx);

        req_ctx = ctx;

        dict = req_ctx->dict;

        rsp_dict = glusterd_op_init_commit_rsp_dict (req_ctx->op);
        if (NULL == rsp_dict)
                return -1;
        status = glusterd_op_commit_perform (req_ctx->op, dict, &op_errstr,
                                             rsp_dict);

        if (status) {
                gf_log ("", GF_LOG_ERROR, "Commit failed: %d", status);
        }

        ret = glusterd_op_commit_send_resp (req_ctx->req, req_ctx->op,
                                            status, op_errstr, rsp_dict);

        glusterd_op_fini_ctx ();
        if (op_errstr && (strcmp (op_errstr, "")))
                GF_FREE (op_errstr);

        if (rsp_dict)
                dict_unref (rsp_dict);
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

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

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
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

                case GD_OP_LOG_FILENAME:
                        ret = glusterd_op_stage_log_filename (dict, op_errstr);
                        break;

                case GD_OP_LOG_ROTATE:
                        ret = glusterd_op_stage_log_rotate (dict, op_errstr);
                        break;

                case GD_OP_SYNC_VOLUME:
                        ret = glusterd_op_stage_sync_volume (dict, op_errstr);
                        break;

                case GD_OP_GSYNC_SET:
                        ret = glusterd_op_stage_gsync_set (dict, op_errstr);
                        break;

                case GD_OP_PROFILE_VOLUME:
                        ret = glusterd_op_stage_stats_volume (dict, op_errstr);
                        break;

                case GD_OP_QUOTA:
                        ret = glusterd_op_stage_quota (dict, op_errstr);
                        break;

                case GD_OP_LOG_LEVEL:
                        ret = glusterd_op_stage_log_level (dict, op_errstr);
                        break;

                case GD_OP_STATUS_VOLUME:
                        ret = glusterd_op_stage_status_volume (dict, op_errstr);
                        break;

                case GD_OP_REBALANCE:
                        ret = glusterd_op_stage_rebalance (dict, op_errstr);
                        break;

                case GD_OP_HEAL_VOLUME:
                        ret = glusterd_op_stage_heal_volume (dict, op_errstr);
                        break;

                case GD_OP_STATEDUMP_VOLUME:
                        ret = glusterd_op_stage_statedump_volume (dict,
                                                                  op_errstr);
                        break;

                default:
                        gf_log ("", GF_LOG_ERROR, "Unknown op %d",
                                op);
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}


int32_t
glusterd_op_commit_perform (glusterd_op_t op, dict_t *dict, char **op_errstr,
                            dict_t *rsp_dict)
{
        int ret = -1;

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
                        ret = glusterd_op_reset_volume (dict);
                        break;

                case GD_OP_REMOVE_BRICK:
                        ret = glusterd_op_remove_brick (dict, op_errstr);
                        break;

                case GD_OP_LOG_FILENAME:
                        ret = glusterd_op_log_filename (dict);
                        break;

                case GD_OP_LOG_ROTATE:
                        ret = glusterd_op_log_rotate (dict);
                        break;

                case GD_OP_SYNC_VOLUME:
                        ret = glusterd_op_sync_volume (dict, op_errstr, rsp_dict);
                        break;

                case GD_OP_GSYNC_SET:
                        ret = glusterd_op_gsync_set (dict, op_errstr, rsp_dict);
                        break;

                case GD_OP_PROFILE_VOLUME:
                        ret = glusterd_op_stats_volume (dict, op_errstr,
                                                        rsp_dict);
                        break;

                case GD_OP_QUOTA:
                        ret = glusterd_op_quota (dict, op_errstr);
                        break;

                case GD_OP_LOG_LEVEL:
                        ret = glusterd_op_log_level (dict);
                        break;

                case GD_OP_STATUS_VOLUME:
                        ret = glusterd_op_status_volume (dict, op_errstr, rsp_dict);
                        break;

                case GD_OP_REBALANCE:
                        ret = glusterd_op_rebalance (dict, op_errstr, rsp_dict);
                        break;

                case GD_OP_HEAL_VOLUME:
                        ret = glusterd_op_heal_volume (dict, op_errstr);
                        break;

                case GD_OP_STATEDUMP_VOLUME:
                        ret = glusterd_op_statedump_volume (dict);
                        break;

                default:
                        gf_log ("", GF_LOG_ERROR, "Unknown op %d",
                                op);
                        break;
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

void
_profile_volume_add_brick_rsp (dict_t *this, char *key, data_t *value,
                             void *data)
{
        char    new_key[256] = {0};
        glusterd_pr_brick_rsp_conv_t *rsp_ctx = NULL;
        data_t  *new_value = NULL;

        rsp_ctx = data;
        new_value = data_copy (value);
        GF_ASSERT (new_value);
        snprintf (new_key, sizeof (new_key), "%d-%s", rsp_ctx->count, key);
        dict_set (rsp_ctx->dict, new_key, new_value);
}

int
glusterd_profile_volume_brick_rsp (glusterd_brickinfo_t *brickinfo,
                                   dict_t *rsp_dict, dict_t *op_ctx,
                                   char **op_errstr)
{
        int     ret = 0;
        glusterd_pr_brick_rsp_conv_t rsp_ctx = {0};
        int32_t count = 0;
        char    brick[PATH_MAX+1024] = {0};
        char    key[256] = {0};
        char    *full_brick = NULL;

        GF_ASSERT (rsp_dict);
        GF_ASSERT (op_ctx);
        GF_ASSERT (op_errstr);
        GF_ASSERT (brickinfo);

        ret = dict_get_int32 (op_ctx, "count", &count);
        if (ret) {
                count = 1;
        } else {
                count++;
        }
        snprintf (key, sizeof (key), "%d-brick", count);
        snprintf (brick, sizeof (brick), "%s:%s", brickinfo->hostname,
                  brickinfo->path);
        full_brick = gf_strdup (brick);
        GF_ASSERT (full_brick);
        ret = dict_set_dynstr (op_ctx, key, full_brick);

        rsp_ctx.count = count;
        rsp_ctx.dict = op_ctx;
        dict_foreach (rsp_dict, _profile_volume_add_brick_rsp, &rsp_ctx);
        dict_del (op_ctx, "count");
        ret = dict_set_int32 (op_ctx, "count", count);
        return ret;
}

int32_t
glusterd_handle_brick_rsp (glusterd_brickinfo_t *brickinfo,
                           glusterd_op_t op, dict_t *rsp_dict, dict_t *op_ctx,
                           char **op_errstr)
{
        int     ret = 0;

        GF_ASSERT (op_errstr);

        switch (op) {
        case GD_OP_PROFILE_VOLUME:
                ret = glusterd_profile_volume_brick_rsp (brickinfo, rsp_dict,
                                                         op_ctx, op_errstr);
        break;

        default:
                break;
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
glusterd_bricks_select_stop_volume (dict_t *dict, char **op_errstr)
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

        if (ret)
                goto out;

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
                                list_add_tail (&pending_node->list, &opinfo.pending_bricks);
                                pending_node = NULL;
                        }
                }
        }

out:
        return ret;
}

static int
glusterd_bricks_select_remove_brick (dict_t *dict, char **op_errstr)
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
                                                              &brickinfo,
                                                              GF_PATH_COMPLETE);
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
                                list_add_tail (&pending_node->list, &opinfo.pending_bricks);
                                pending_node = NULL;
                        }
                }
                i++;
        }

out:
        return ret;
}

static int
glusterd_bricks_select_profile_volume (dict_t *dict, char **op_errstr)
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
                                                       &opinfo.pending_bricks);
                                        pending_node = NULL;
                                }
                        }
                }
                break;

        case GF_CLI_STATS_TOP:
                ret = dict_get_str (dict, "brick", &brick);
                if (!ret) {
                        ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                                      &brickinfo,
                                                                      GF_PATH_COMPLETE);
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
                                               &opinfo.pending_bricks);
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
                                                       &opinfo.pending_bricks);
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

        snprintf (key, sizeof (key), "heal-%d", count);
        ret = gf_asprintf (&xname, "%s-replicate-%d", volname, index);
        if (ret == -1)
                goto out;

        ret = dict_set_dynstr (dict, key, xname);
out:
        return ret;
}

static int
glusterd_bricks_select_heal_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        xlator_t                                *this = NULL;
        char                                    msg[2048] = {0,};
        int                                     replica_count = 0;
        int                                     index = 1;
        int                                     rxlator_count = 0;
        uuid_t                                  candidate = {0};
        glusterd_pending_node_t                 *pending_node = NULL;

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

        replica_count = volinfo->replica_count;

        index = 1;
        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (uuid_is_null (brickinfo->uuid))
                        (void)glusterd_resolve_brick (brickinfo);

                if (uuid_compare (brickinfo->uuid, candidate) > 0)
                        uuid_copy (candidate, brickinfo->uuid);

                if (index % replica_count == 0) {
                        if (!uuid_compare (priv->uuid, candidate)) {
                                _add_rxlator_to_dict (dict, volname,
                                                      (index-1)/replica_count,
                                                      rxlator_count);
                                rxlator_count++;
                        }
                        uuid_clear (candidate);
                }

                index++;
        }
        ret = dict_set_int32 (dict, "count", rxlator_count);
        if (ret)
                goto out;

        if (rxlator_count) {
                pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                          gf_gld_mt_pending_node_t);
                if (!pending_node) {
                        ret = -1;
                        goto out;
                } else {
                        pending_node->node = priv->shd;
                        pending_node->type = GD_NODE_SHD;
                        list_add_tail (&pending_node->list,
                                       &opinfo.pending_bricks);
                        pending_node = NULL;
                }
        }


out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning ret %d", ret);
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

        this = THIS;
        priv = this->private;

        if (ctx) {
                req_ctx = ctx;
        } else {
                req_ctx = GF_CALLOC (1, sizeof (*req_ctx),
                                     gf_gld_mt_op_allack_ctx_t);
                op = glusterd_op_get_op ();
                req_ctx->op = op;
                uuid_copy (req_ctx->uuid, priv->uuid);
                ret = glusterd_op_build_payload (&req_ctx->dict);
                if (ret)//TODO:what to do??
                        goto out;
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
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}


static int
glusterd_op_ac_rcvd_brick_op_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        glusterd_op_brick_rsp_ctx_t *ev_ctx = NULL;
        char                        *op_errstr = NULL;
        glusterd_op_t               op = GD_OP_NONE;
        dict_t                      *op_ctx = NULL;
        glusterd_req_ctx_t          *req_ctx = NULL;

        GF_ASSERT (event);
        GF_ASSERT (ctx);
        ev_ctx = ctx;

        req_ctx = ev_ctx->commit_ctx;
        GF_ASSERT (req_ctx);

        op = req_ctx->op;
        op_ctx = glusterd_op_get_ctx ();

        ret = glusterd_remove_pending_entry (&opinfo.pending_bricks,
                                             ev_ctx->pending_node->node);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "unknown response received ");
                ret = -1;
                goto out;
        }

        if (opinfo.brick_pending_count > 0)
                opinfo.brick_pending_count--;

        glusterd_handle_brick_rsp (ev_ctx->pending_node->node, op, ev_ctx->rsp_dict,
                                           op_ctx, &op_errstr);
        if (opinfo.brick_pending_count > 0)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_ALL_ACK, ev_ctx->commit_ctx);

out:
        if (ev_ctx->rsp_dict)
                dict_unref (ev_ctx->rsp_dict);
        GF_FREE (ev_ctx);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
glusterd_op_bricks_select (glusterd_op_t op, dict_t *dict, char **op_errstr)
{
        int     ret = 0;

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (op > GD_OP_NONE);
        GF_ASSERT (op < GD_OP_MAX);

        switch (op) {
        case GD_OP_STOP_VOLUME:
                ret = glusterd_bricks_select_stop_volume (dict, op_errstr);
                break;

        case GD_OP_REMOVE_BRICK:
                ret = glusterd_bricks_select_remove_brick (dict, op_errstr);
                break;

        case GD_OP_PROFILE_VOLUME:
                ret = glusterd_bricks_select_profile_volume (dict, op_errstr);
                break;

        case GD_OP_HEAL_VOLUME:
                ret = glusterd_bricks_select_heal_volume (dict, op_errstr);
                break;

        default:
                break;
         }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

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

        gf_log ("glusterd", GF_LOG_DEBUG, "Enqueuing event: '%s'",
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
        glusterd_op_sm_ac_fn            handler = NULL;
        glusterd_op_sm_t                *state = NULL;
        glusterd_op_sm_event_type_t     event_type = GD_OP_EVENT_NONE;

        (void ) pthread_mutex_lock (&gd_op_sm_lock);

        while (!list_empty (&gd_op_sm_queue)) {

                list_for_each_entry_safe (event, tmp, &gd_op_sm_queue, list) {

                        list_del_init (&event->list);
                        event_type = event->event;
                        gf_log ("", GF_LOG_DEBUG, "Dequeued event of type: '%s'",
                                glusterd_op_sm_event_name_get(event_type));

                        state = glusterd_op_state_table[opinfo.state.state];

                        GF_ASSERT (state);

                        handler = state[event_type].handler;
                        GF_ASSERT (handler);

                        ret = handler (event, event->ctx);

                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR,
                                        "handler returned: %d", ret);
                                glusterd_destroy_op_event_ctx (event);
                                GF_FREE (event);
                                continue;
                        }

                        ret = glusterd_op_sm_transition_state (&opinfo, state,
                                                                event_type);

                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR,
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

        GF_ASSERT (GD_OP_NONE < op && op < GD_OP_MAX);

        if (_gf_false == glusterd_need_brick_op (op)) {
                gf_log ("", GF_LOG_DEBUG, "Received op: %d, returning", op);
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
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
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
                case GD_OP_LOG_FILENAME:
                case GD_OP_LOG_ROTATE:
                case GD_OP_SYNC_VOLUME:
                case GD_OP_SET_VOLUME:
                case GD_OP_START_VOLUME:
                case GD_OP_RESET_VOLUME:
                case GD_OP_GSYNC_SET:
                case GD_OP_QUOTA:
                case GD_OP_PROFILE_VOLUME:
                case GD_OP_LOG_LEVEL:
                case GD_OP_STATUS_VOLUME:
                case GD_OP_REBALANCE:
                case GD_OP_HEAL_VOLUME:
                case GD_OP_STATEDUMP_VOLUME:
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

