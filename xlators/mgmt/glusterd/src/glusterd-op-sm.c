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
#include "glusterd-volgen.h"
#include "syscall.h"
#include "cli1.h"
#include "common-utils.h"
#include "run.h"

#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#define glusterd_op_start_volume_args_get(dict, volname, flags) \
        glusterd_op_stop_volume_args_get (dict, volname, flags)

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
        "GD_OP_EVENT_INVALID"
};

static char *gsync_reserved_opts[] = {
        "gluster-command",
        "pid-file",
        "state-file",
        "session-owner",
        NULL
};

static int
glusterd_restart_brick_servers (glusterd_volinfo_t *);


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

gf_boolean_t
glusterd_are_all_volumes_stopped ()
{
        glusterd_conf_t                         *priv = NULL;
        xlator_t                                *this = NULL;
        glusterd_volinfo_t                      *voliter = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (voliter->status == GLUSTERD_STATUS_STARTED)
                        return _gf_false;
        }

        return _gf_true;

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
        gf1_cli_top_op          top_op = 0;
        double                  throughput = 0;
        double                  time = 0;
        int32_t                 blk_size = 0;
        int32_t                 blk_count = 0;

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
                brick_req->op = GF_BRICK_TERMINATE;
                brick_req->name = "";
        break;
        case GD_OP_PROFILE_VOLUME:
                brick_req = GF_CALLOC (1, sizeof (*brick_req),
                                       gf_gld_mt_mop_brick_req_t);

                if (!brick_req) {
                        gf_log ("", GF_LOG_ERROR, "Out of Memory");
                        goto out;
                }

                brick_req->op = GF_BRICK_XLATOR_INFO;
                brick_req->name = brickinfo->path;

                ret = dict_get_int32 (dict, "top-op", (int32_t*)&top_op);
                if (ret)
                        goto cont; 
                if (top_op == GF_CLI_TOP_READ_PERF ||
                    top_op == GF_CLI_TOP_WRITE_PERF) {

                        ret = dict_get_int32 (dict, "blk-size", &blk_size);
                        if (ret) {
                                goto cont;
                        }
                        ret = dict_get_int32 (dict, "blk-cnt", &blk_count);
                        if (ret)
                                goto out;
                        if (top_op == GF_CLI_TOP_READ_PERF)
                                 ret = glusterd_volume_stats_read_perf (
                                        brickinfo->path, blk_size, blk_count, 
                                        &throughput, &time);
                        else if (!ret && top_op == GF_CLI_TOP_WRITE_PERF)
                                ret = glusterd_volume_stats_write_perf (
                                        brickinfo->path, blk_size, blk_count, 
                                        &throughput, &time);

                        if (ret) 
                                goto out;

                        ret = dict_set_double (dict, "throughput",
                                              throughput);
                        if (ret)
                                goto out;
                        ret = dict_set_double (dict, "time", time);
                        if (ret)
                                goto out;
                }
                break;
        default:
                goto out;
        break;
        }

cont:
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

static int
glusterd_op_stop_volume_args_get (dict_t *dict, char** volname,
                                  int *flags)
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

static int
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

static int
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

        if (!(flags & GF_CLI_FLAG_OP_FORCE)) {
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

        }


out:

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
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

static int
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

char *
glusterd_check_brick_rb_part (char *bricks, int count, glusterd_volinfo_t *volinfo)
{
        char                                    *saveptr = NULL;
        char                                    *brick = NULL;
        char                                    *brick_list = NULL;
        int                                     ret = 0;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        uint32_t                                i = 0;
        char                                    *str = NULL;
        char                                    msg[2048] = {0,};

        brick_list = gf_strdup (bricks);
        if (!brick_list) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "Out of memory");
                ret = -1;
                goto out;
        }

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);


        while ( i < count) {
                ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
                if (ret) {
                        snprintf (msg, sizeof(msg), "Unable to"
                                  " get brickinfo");
                        gf_log ("", GF_LOG_ERROR, "%s", msg);
                        ret = -1;
                        goto out;
                }

                if (glusterd_is_replace_running (volinfo, brickinfo)) {
                        snprintf (msg, sizeof(msg), "Volume %s: replace brick is running"
                          " and the brick %s:%s you are trying to add is the destination brick"
                          " for replace brick", volinfo->volname, brickinfo->hostname, brickinfo->path);
                        ret = -1;
                        goto out;
                }

                glusterd_brickinfo_delete (brickinfo);
                brickinfo = NULL;
                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
        }

out:
        if (brick_list)
                GF_FREE(brick_list);
        if (brickinfo)
                glusterd_brickinfo_delete (brickinfo);
        if (ret)
                str = gf_strdup (msg);
        return str;
}

static int
glusterd_get_rb_dst_brickinfo (glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t **brickinfo)
{
        int32_t                 ret = -1;

        if (!volinfo || !brickinfo)
                goto out;

        *brickinfo = volinfo->dst_brick;

        ret = 0;

out:
        return ret;
}

static int
glusterd_op_stage_replace_brick (dict_t *dict, char **op_errstr,
                                 dict_t *rsp_dict)
{
        int                                      ret           = 0;
        char                                    *src_brick     = NULL;
        char                                    *dst_brick     = NULL;
        char                                    *volname       = NULL;
        int                                      replace_op    = 0;
        glusterd_volinfo_t                      *volinfo       = NULL;
        glusterd_brickinfo_t                    *src_brickinfo = NULL;
        char                                    *host          = NULL;
        char                                    *path          = NULL;
        char                                    msg[2048]      = {0};
        char                                    *dup_dstbrick  = NULL;
        glusterd_peerinfo_t                     *peerinfo = NULL;
        glusterd_brickinfo_t                    *dst_brickinfo = NULL;
        gf_boolean_t                            is_run         = _gf_false;

        ret = dict_get_str (dict, "src-brick", &src_brick);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get src brick");
                goto out;
        }

        gf_log ("", GF_LOG_DEBUG, "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get dest brick");
                goto out;
        }

        gf_log ("", GF_LOG_DEBUG, "dst brick=%s", dst_brick);

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = dict_get_int32 (dict, "operation", (int32_t *)&replace_op);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "dict get on replace-brick operation failed");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), "volume: %s does not exist",
                          volname);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        if (GLUSTERD_STATUS_STARTED != volinfo->status) {
                ret = -1;
                snprintf (msg, sizeof (msg), "volume: %s is not started",
                          volname);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        if (!glusterd_store_is_valid_brickpath (volname, dst_brick) ||
                !glusterd_is_valid_volfpath (volname, dst_brick)) {
                snprintf (msg, sizeof (msg), "brick path %s is too "
                          "long.", dst_brick);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
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
                                "for the volume %s.\nStop "GEOREP "sessions "
                                "involved in this volume. Use 'volume "GEOREP
                                " status' command for more info.",
                                volname);
                *op_errstr = gf_strdup (msg);
                ret = -1;
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

        switch (replace_op) {
        case GF_REPLACE_OP_START:
                if (glusterd_is_rb_started (volinfo)) {
                        gf_log ("", GF_LOG_ERROR, "Replace brick is already "
                                "started for volume ");
                        ret = -1;
                        goto out;
                }
                break;
        case GF_REPLACE_OP_PAUSE:
                if (glusterd_is_rb_paused (volinfo)) {
                        gf_log ("", GF_LOG_ERROR, "Replace brick is already "
                                "paused for volume ");
                        ret = -1;
                        goto out;
                } else if (!glusterd_is_rb_started(volinfo)) {
                        gf_log ("", GF_LOG_ERROR, "Replace brick is not"
                                " started for volume ");
                        ret = -1;
                        goto out;
                }
                break;

        case GF_REPLACE_OP_ABORT:
                if ((!glusterd_is_rb_paused (volinfo)) &&
                     (!glusterd_is_rb_started (volinfo))) {
                        gf_log ("", GF_LOG_ERROR, "Replace brick is not"
                                " started or paused for volume ");
                        ret = -1;
                        goto out;
                }
                break;

        case GF_REPLACE_OP_COMMIT:
                if (!glusterd_is_rb_started (volinfo)) {
                        gf_log ("", GF_LOG_ERROR, "Replace brick is not "
                                "started for volume ");
                        ret = -1;
                        goto out;
                }
                break;

        case GF_REPLACE_OP_COMMIT_FORCE: break;
        case GF_REPLACE_OP_STATUS:
                break;
        default:
                ret = -1;
                goto out;
        }

        ret = glusterd_volume_brickinfo_get_by_brick (src_brick, volinfo,
                                                      &src_brickinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), "brick: %s does not exist in "
                          "volume: %s", src_brick, volname);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        if (!glusterd_is_local_addr (src_brickinfo->hostname)) {
                gf_log ("", GF_LOG_DEBUG,
                        "I AM THE SOURCE HOST");
                if (src_brickinfo->port && rsp_dict) {
                        ret = dict_set_int32 (rsp_dict, "src-brick-port",
                                              src_brickinfo->port);
                        if (ret) {
                                gf_log ("", GF_LOG_DEBUG,
                                        "Could not set src-brick-port=%d",
                                        src_brickinfo->port);
                        }
                }

        }

        dup_dstbrick = gf_strdup (dst_brick);
        if (!dup_dstbrick) {
                ret = -1;
                gf_log ("", GF_LOG_ERROR, "Memory allocation failed");
                goto out;
        }
        host = strtok (dup_dstbrick, ":");
        path = strtok (NULL, ":");

        if (!host || !path) {
                gf_log ("", GF_LOG_ERROR,
                        "dst brick %s is not of form <HOSTNAME>:<export-dir>",
                        dst_brick);
                ret = -1;
                goto out;
        }
        if (!glusterd_brickinfo_get (NULL, host, path, NULL)) {
                snprintf(msg, sizeof(msg), "Brick: %s:%s already in use",
                         host, path);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        if ((volinfo->rb_status ==GF_RB_STATUS_NONE) &&
            (replace_op == GF_REPLACE_OP_START)) {
                ret = glusterd_brickinfo_from_brick (dst_brick, &dst_brickinfo);
                volinfo->src_brick = src_brickinfo;
                volinfo->dst_brick = dst_brickinfo;
        } else {
                ret = glusterd_get_rb_dst_brickinfo (volinfo, &dst_brickinfo);
        }

        if (glusterd_rb_check_bricks (volinfo, src_brickinfo, dst_brickinfo)) {
                gf_log ("", GF_LOG_ERROR, "replace brick: incorrect source or"
                       "  destination bricks specified");
                ret = -1;
                goto out;
       }
        if (!glusterd_is_local_addr (host)) {
                ret = glusterd_brick_create_path (host, path,
                                                  volinfo->volume_id, 0777,
                                                  op_errstr);
                if (ret)
                        goto out;
        } else {
                ret = glusterd_friend_find (NULL, host, &peerinfo);
                if (ret) {
                        snprintf (msg, sizeof (msg), "%s, is not a friend",
                                  host);
                        *op_errstr = gf_strdup (msg);
                        goto out;
                }

                if (!peerinfo->connected) {
                        snprintf (msg, sizeof (msg), "%s, is not connected at "
                                  "the moment", host);
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }

                if (GD_FRIEND_STATE_BEFRIENDED != peerinfo->state.state) {
                        snprintf (msg, sizeof (msg), "%s, is not befriended "
                                  "at the moment", host);
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }
        }
        ret = 0;

out:
        if (dup_dstbrick)
                GF_FREE (dup_dstbrick);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
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

static int
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

static int
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

static int
glusterd_op_stage_set_volume (dict_t *dict, char **op_errstr)
{
        int                                      ret           = 0;
        char                                    *volname       = NULL;
 	int                                      exists        = 0;
 	char					*key	       = NULL;
        char                                    *key_fixed     = NULL;
        char                                    *value         = NULL;
 	char					 str[100]      = {0, };
 	int					 count	       = 0;
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


out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}


static int
glusterd_op_perform_remove_brick (glusterd_volinfo_t  *volinfo, char *brick)
{

        glusterd_brickinfo_t    *brickinfo = NULL;
        char                    *dup_brick = NULL;
        glusterd_conf_t         *priv = NULL;
        int32_t                 ret = -1;

        GF_ASSERT (volinfo);
        GF_ASSERT (brick);

        priv = THIS->private;

        dup_brick = gf_strdup (brick);
        if (!dup_brick)
                goto out;

        ret = glusterd_volume_brickinfo_get_by_brick (dup_brick, volinfo,  &brickinfo);
        if (ret)
                goto out;

        ret = glusterd_resolve_brick (brickinfo);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                ret = glusterd_brick_stop (volinfo, brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to stop "
                                "glusterfs, ret: %d", ret);
                        goto out;
                }
        }
        glusterd_delete_brick (volinfo, brickinfo);
out:
        if (dup_brick)
                GF_FREE (dup_brick);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
glusterd_op_perform_replace_brick (glusterd_volinfo_t  *volinfo,
                                   char *old_brick, char  *new_brick)
{
        glusterd_brickinfo_t                    *old_brickinfo = NULL;
        glusterd_brickinfo_t                    *new_brickinfo = NULL;
        int32_t                                 ret = -1;
        glusterd_conf_t                         *priv = NULL;

        priv = THIS->private;

        GF_ASSERT (volinfo);

        ret = glusterd_brickinfo_from_brick (new_brick,
                                             &new_brickinfo);
        if (ret)
                goto out;

        ret = glusterd_volume_brickinfo_get_by_brick (old_brick, volinfo,
                                                      &old_brickinfo);
        if (ret)
                goto out;

        ret = glusterd_resolve_brick (new_brickinfo);
        if (ret)
                goto out;

        list_add_tail (&new_brickinfo->brick_list,
                       &old_brickinfo->brick_list);

        volinfo->brick_count++;

        ret = glusterd_op_perform_remove_brick (volinfo, old_brick);
        if (ret)
                goto out;

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                ret = glusterd_brick_start (volinfo, new_brickinfo);
                if (ret)
                        goto out;
        }


out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
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
        glusterd_conf_t                         *priv = NULL;

        priv = THIS->private;

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


static int
glusterd_op_stage_remove_brick (dict_t *dict)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        dict_t                                  *ctx     = NULL;
        char                                    *errstr  = NULL;
        int32_t                                 brick_count = 0;

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

        if (glusterd_is_defrag_on(volinfo)) {
                ctx = glusterd_op_get_ctx (GD_OP_REMOVE_BRICK);
                errstr = gf_strdup("Rebalance is in progress. Please retry"
                                    " after completion");
                if (!errstr) {
                        ret = -1;
                        goto out;
                }
                gf_log ("glusterd", GF_LOG_ERROR, "%s", errstr);
                ret = dict_set_dynstr (ctx, "errstr", errstr);
                if (ret) {
                        GF_FREE (errstr);
                        gf_log ("", GF_LOG_DEBUG,
                                "failed to set errstr ctx");
                        goto out;
                }

                ret = -1;
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get brick count");
                goto out;
        }

        if (volinfo->brick_count == brick_count) {
                ctx = glusterd_op_get_ctx (GD_OP_REMOVE_BRICK);
                if (!ctx) {
                        gf_log ("", GF_LOG_ERROR,
                                "Operation Context is not present");
                        ret = -1;
                        goto out;
                }
                errstr = gf_strdup ("Deleting all the bricks of the "
                                    "volume is not allowed");
                if (!errstr) {
                        gf_log ("", GF_LOG_ERROR, "Out of memory");
                        ret = -1;
                        goto out;
                }

                ret = dict_set_dynstr (ctx, "errstr", errstr);
                if (ret) {
                        GF_FREE (errstr);
                        gf_log ("", GF_LOG_DEBUG,
                                "failed to set pump status in ctx");
                        goto out;
                }

                ret = -1;
                goto out;
        }

out:
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


int
glusterd_query_extutil (char *resbuf, runner_t *runner)
{
        char               *ptr = NULL;
        int                 ret = 0;

        runner_redir (runner, STDOUT_FILENO, RUN_PIPE);
        if (runner_start (runner) != 0) {
                gf_log ("", GF_LOG_ERROR, "spawning child failed");

                return -1;
        }

        ptr = fgets(resbuf, PATH_MAX, runner_chio (runner, STDOUT_FILENO));
        if (ptr)
                resbuf[strlen(resbuf)-1] = '\0'; //strip off \n

        ret = runner_end (runner);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "reading data from child failed");

        return ret ? -1 : 0;
}

static int
glusterd_get_canon_url (char *cann, char *name, gf_boolean_t cann_esc)
{
        runner_t            runner = {0,};
        glusterd_conf_t    *priv  = NULL;

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;

        runinit (&runner);
        runner_add_arg (&runner, GSYNCD_PREFIX"/gsyncd");
        runner_argprintf (&runner, "--canonicalize-%surl",
                          cann_esc ? "escape-" : "");
        runner_add_arg(&runner, name);

        return glusterd_query_extutil (cann, &runner);
}

int
glusterd_gsync_get_param_file (char *prmfile, const char *param, char *master,
                               char *slave, char *gl_workdir)
{
        runner_t            runner = {0,};

        runinit (&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s/"GSYNC_CONF, gl_workdir);
        runner_argprintf (&runner, ":%s", master);
        runner_add_args  (&runner, slave, "--config-get", NULL);
        runner_argprintf (&runner, "%s-file", param);

        return glusterd_query_extutil (prmfile, &runner);
}

static int
gsyncd_getpidfile (char *master, char *slave, char *pidfile)
{
        int                ret             = -1;
        glusterd_conf_t    *priv  = NULL;

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;

        GF_VALIDATE_OR_GOTO ("gsync", master, out);
        GF_VALIDATE_OR_GOTO ("gsync", slave, out);

        ret = glusterd_gsync_get_param_file (pidfile, "pid", master,
                                              slave, priv->workdir);
        if (ret == -1) {
                ret = -2;
                gf_log ("", GF_LOG_WARNING, "failed to create the pidfile string");
                goto out;
        }

        ret = open (pidfile, O_RDWR);

 out:
        return ret;
}

static int
gsync_status_byfd (int fd)
{
        GF_ASSERT (fd >= -1);

        if (lockf (fd, F_TEST, 0) == -1 &&
            (errno == EAGAIN || errno == EACCES))
                /* gsyncd keeps the pidfile locked */
                return 0;

        return -1;
}

/* status: return 0 when gsync is running
 * return -1 when not running
 */
int
gsync_status (char *master, char *slave, int *status)
{
        char pidfile[PATH_MAX] = {0,};
        int  fd                = -1;

        fd = gsyncd_getpidfile (master, slave, pidfile);
        if (fd == -2)
                return -1;

        *status = gsync_status_byfd (fd);

        close (fd);
        return 0;
}


int32_t
glusterd_gsync_volinfo_dict_set (glusterd_volinfo_t *volinfo,
                                 char *key, char *value)
{
        int32_t  ret            = -1;
        char    *gsync_status   = NULL;

        gsync_status = gf_strdup (value);
        if (!gsync_status) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = dict_set_dynstr (volinfo->dict, key, gsync_status);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set dict");
                goto out;
        }

        ret = 0;
out:
        return 0;
}

int
gsync_verify_config_options (dict_t *dict, char **op_errstr)
{
        char  **resopt    = NULL;
        int     i         = 0;
        char   *subop     = NULL;
        char   *slave     = NULL;
        char   *op_name   = NULL;
        char   *op_value  = NULL;
        gf_boolean_t banned = _gf_true;

        if (dict_get_str (dict, "subop", &subop) != 0) {
                gf_log ("", GF_LOG_WARNING, "missing subop");
                *op_errstr = gf_strdup ("Invalid config request");
                return -1;
        }

        if (dict_get_str (dict, "slave", &slave) != 0) {
                gf_log ("", GF_LOG_WARNING, GEOREP" CONFIG: no slave given");
                *op_errstr = gf_strdup ("Slave required");
                return -1;
        }

        if (strcmp (subop, "get-all") == 0)
                return 0;

        if (dict_get_str (dict, "op_name", &op_name) != 0) {
                gf_log ("", GF_LOG_WARNING, "option name missing");
                *op_errstr = gf_strdup ("Option name missing");
                return -1;
        }

        if (runcmd (GSYNCD_PREFIX"/gsyncd", "--config-check", op_name, NULL)) {
                gf_log ("", GF_LOG_WARNING, "Invalid option %s", op_name);
                *op_errstr = gf_strdup ("Invalid option");

                return -1;
        }

        if (strcmp (subop, "get") == 0)
                return 0;

        if (strcmp (subop, "set") != 0 && strcmp (subop, "del") != 0) {
                gf_log ("", GF_LOG_WARNING, "unknown subop %s", subop);
                *op_errstr = gf_strdup ("Invalid config request");
                return -1;
        }

        if (strcmp (subop, "set") == 0 &&
            dict_get_str (dict, "op_value", &op_value) != 0) {
                gf_log ("", GF_LOG_WARNING, "missing value for set");
                *op_errstr = gf_strdup ("missing value");
        }

        /* match option name against reserved options, modulo -/_
         * difference
         */
        for (resopt = gsync_reserved_opts; *resopt; resopt++) {
                banned = _gf_true;
                for (i = 0; (*resopt)[i] && op_name[i]; i++) {
                        if ((*resopt)[i] == op_name[i] ||
                            ((*resopt)[i] == '-' && op_name[i] == '_'))
                                continue;
                        banned = _gf_false;
                }
                if (banned) {
                        gf_log ("", GF_LOG_WARNING, "Reserved option %s", op_name);
                        *op_errstr = gf_strdup ("Reserved option");

                        return -1;
                        break;
                }
        }

        return 0;
}

static void
_get_status_mst_slv (dict_t *this, char *key, data_t *value, void *data)
{
        glusterd_gsync_status_temp_t  *param = NULL;
        char                          *slave = NULL;
        int                           ret = 0;

        param = (glusterd_gsync_status_temp_t *)data;

        GF_ASSERT (param);
        GF_ASSERT (param->volinfo);

        slave = strchr(value->data, ':');
        if (slave)
                slave ++;
        else
                return;

        ret = glusterd_get_gsync_status_mst_slv(param->volinfo,
                                                slave, param->rsp_dict);

}

/* The return   status indicates success (ret_status = 0) if the host uuid
 *  matches,    status indicates failure (ret_status = -1) if the host uuid
 *  mismatches, status indicates not found if the slave is not found to be
 *  spawned for the given master */
static void
_compare_host_uuid (dict_t *this, char *key, data_t *value, void *data)
{
        glusterd_gsync_slaves_t     *status = NULL;
        char                        *slave = NULL;
        int                          uuid_len = 0;

        status = (glusterd_gsync_slaves_t *)data;

        if ((status->ret_status == -1) || (status->ret_status == 0))
                return;
        slave = strchr(value->data, ':');
        if (slave)
                slave ++;

        uuid_len = (slave - value->data - 1);

        if (strncmp (slave, status->slave, PATH_MAX) == 0) {
                if (strncmp (value->data, status->host_uuid, uuid_len) == 0) {
                        status->ret_status = 0;
                } else {
                        status->ret_status = -1;
                        strncpy (status->rmt_hostname, value->data, uuid_len);
                        status->rmt_hostname[uuid_len] = '\0';
                }
        }

}

static void
_get_max_gsync_slave_num (dict_t *this, char *key, data_t *value, void *data)
{
        int                          tmp_slvnum = 0;
        glusterd_gsync_slaves_t     *status = NULL;

        status = (glusterd_gsync_slaves_t *)data;

        sscanf (key, "slave%d", &tmp_slvnum);
        if (tmp_slvnum > status->ret_status)
                status->ret_status = tmp_slvnum;
}

static void
_remove_gsync_slave (dict_t *this, char *key, data_t *value, void *data)
{
        glusterd_gsync_slaves_t     *status = NULL;
        char                        *slave = NULL;


        status = (glusterd_gsync_slaves_t *)data;

        slave = strchr(value->data, ':');
        if (slave)
                slave ++;

        if (strncmp (slave, status->slave, PATH_MAX) == 0)
                dict_del (this, key);

}

static int
glusterd_remove_slave_in_info (glusterd_volinfo_t *volinfo, char *slave,
                               char *host_uuid, char **op_errstr)
{
        int                         ret = 0;
        glusterd_gsync_slaves_t     status = {0, };
        char                        cann_slave[PATH_MAX] = {0,  };

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (host_uuid);

        ret = glusterd_get_canon_url (cann_slave, slave, _gf_false);
        if (ret)
                goto out;

        status.slave = cann_slave;
        status.host_uuid = host_uuid;
        status.ret_status = 1;

        dict_foreach (volinfo->gsync_slaves, _remove_gsync_slave, &status);

        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                 *op_errstr = gf_strdup ("Failed to store the Volume"
                                         "information");
                goto out;
        }
 out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;

}

static int
glusterd_gsync_get_uuid (char *slave, glusterd_volinfo_t *vol,
                         uuid_t uuid)
{

        int                         ret = 0;
        glusterd_gsync_slaves_t     status = {0, };
        char                        cann_slave[PATH_MAX] = {0,  };
        char                        host_uuid_str[64] = {0};
        xlator_t                    *this = NULL;
        glusterd_conf_t             *priv = NULL;


        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (vol);
        GF_ASSERT (slave);

        uuid_utoa_r (priv->uuid, host_uuid_str);
        ret = glusterd_get_canon_url (cann_slave, slave, _gf_false);
        if (ret)
                goto out;

        status.slave = cann_slave;
        status.host_uuid = host_uuid_str;
        status.ret_status = 1;
        dict_foreach (vol->gsync_slaves, _compare_host_uuid, &status);
        if (status.ret_status == 0) {
                uuid_copy (uuid, priv->uuid);
        } else if (status.ret_status == -1) {
                uuid_parse (status.rmt_hostname, uuid);
        } else {
                ret = -1;
                goto out;
        }
        ret = 0;
 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

static int
glusterd_check_gsync_running_local (char *master, char *slave,
                                    gf_boolean_t *is_run)
{
        int                 ret    = -1;
        int                 ret_status = 0;

        GF_ASSERT (master);
        GF_ASSERT (slave);
        GF_ASSERT (is_run);

        *is_run = _gf_false;
        ret = gsync_status (master, slave, &ret_status);
        if (ret == 0 && ret_status == 0) {
                *is_run = _gf_true;
        } else if (ret == -1) {
                gf_log ("", GF_LOG_WARNING, GEOREP" validation "
                        " failed");
                goto out;
        }
        ret = 0;
 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

static int
glusterd_store_slave_in_info (glusterd_volinfo_t *volinfo, char *slave,
                              char *host_uuid, char **op_errstr)
{
        int                         ret = 0;
        glusterd_gsync_slaves_t     status = {0, };
        char                        cann_slave[PATH_MAX] = {0,  };
        char                       *value = NULL;
        char                        key[512] = {0, };

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (host_uuid);

        ret = glusterd_get_canon_url (cann_slave, slave, _gf_false);
        if (ret)
                goto out;

        status.slave = cann_slave;
        status.host_uuid = host_uuid;
        status.ret_status = 1;
        dict_foreach (volinfo->gsync_slaves, _compare_host_uuid, &status);

        if (status.ret_status == -1) {
                gf_log ("", GF_LOG_ERROR, GEOREP" has already been invoked for "
                                          "the %s (master) and %s (slave)"
                                          "from a different machine",
                                           volinfo->volname, slave);
                 *op_errstr = gf_strdup (GEOREP" already running in an an"
                                        "orhter machine");
                ret = -1;
                goto out;
        }

        memset (&status, 0, sizeof (status));

        dict_foreach (volinfo->gsync_slaves, _get_max_gsync_slave_num, &status);

        gf_asprintf (&value,  "%s:%s", host_uuid, cann_slave);
        snprintf (key, 512, "slave%d", status.ret_status +1);
        ret = dict_set_dynstr (volinfo->gsync_slaves, key, value);

        if (ret)
                goto out;
        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                 *op_errstr = gf_strdup ("Failed to store the Volume"
                                         "information");
                goto out;
        }
        ret = 0;
 out:
        return ret;
}


static int
glusterd_op_verify_gsync_start_options (glusterd_volinfo_t *volinfo,
                                        char *slave, char **op_errstr)
{
        int                     ret = -1;
        gf_boolean_t            is_running = _gf_false;
        char                    msg[2048] = {0};
        uuid_t                  uuid = {0};
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;

        this = THIS;

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (op_errstr);
        GF_ASSERT (this && this->private);

        priv  = this->private;

        if (GLUSTERD_STATUS_STARTED != volinfo->status) {
                snprintf (msg, sizeof (msg), "Volume %s needs to be started "
                          "before "GEOREP" start", volinfo->volname);
                goto out;
        }
        /*Check if the gsync is already started in cmd. inited host
         * If so initiate add it into the glusterd's priv*/
        ret = glusterd_gsync_get_uuid (slave, volinfo, uuid);
        if ((ret == 0) && (uuid_compare (priv->uuid, uuid) == 0)) {
                ret = glusterd_check_gsync_running_local (volinfo->volname,
                                                          slave, &is_running);
                if (ret) {
                        snprintf (msg, sizeof (msg), GEOREP" start option "
                                  "validation failed ");
                        goto out;
                }
                if (_gf_true == is_running) {
                        snprintf (msg, sizeof (msg), GEOREP " session between"
                                  " %s & %s already started", volinfo->volname,
                                  slave);
                        ret = -1;
                        goto out;
                }
        }
        ret = 0;
out:
        if (ret && (msg[0] != '\0')) {
                *op_errstr = gf_strdup (msg);
        }
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_check_gsync_running (glusterd_volinfo_t *volinfo, gf_boolean_t *flag)
{

        GF_ASSERT (volinfo);
        GF_ASSERT (flag);

        if (volinfo->gsync_slaves->count)
                *flag = _gf_true;
        else
                *flag = _gf_false;

        return 0;
}

static int
glusterd_op_verify_gsync_running (glusterd_volinfo_t *volinfo,
                                  char *slave, char **op_errstr)
{
        int                     ret = -1;
        char                    msg[2048] = {0};
        uuid_t                  uuid = {0};
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (THIS && THIS->private);
        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (op_errstr);

        priv = THIS->private;

        if (GLUSTERD_STATUS_STARTED != volinfo->status) {
                snprintf (msg, sizeof (msg), "Volume %s needs to be started "
                          "before "GEOREP" start", volinfo->volname);

                goto out;
        }
        ret = glusterd_gsync_get_uuid (slave, volinfo, uuid);
        if (ret == -1) {
                snprintf (msg, sizeof (msg), GEOREP" session between %s & %s"
                          " not active", volinfo->volname, slave);
                goto out;
        }

        ret = 0;
out:
        if (ret && (msg[0] != '\0')) {
                *op_errstr = gf_strdup (msg);
        }
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
glusterd_verify_gsync_status_opts (dict_t *dict, char **op_errstr)
{
        char               *slave  = NULL;
        char               *volname = NULL;
        char               errmsg[PATH_MAX] = {0, };
        gf_boolean_t       exists = _gf_false;
        glusterd_volinfo_t *volinfo = NULL;
        int                ret = 0;

        ret = dict_get_str (dict, "master", &volname);
        if (ret < 0) {
                ret = 0;
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if ((ret) || (!exists)) {
                gf_log ("", GF_LOG_WARNING, "volume name does not exist");
                snprintf (errmsg, sizeof(errmsg), "Volume name %s does not"
                          " exist", volname);
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0) {
                ret = 0;
                goto out;
        }

 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}


static int
glusterd_op_gsync_args_get (dict_t *dict, char **op_errstr,
                            char **master, char **slave)
{

        int             ret = -1;
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (master);
        GF_ASSERT (slave);

        ret = dict_get_str (dict, "master", master);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "master not found");
                *op_errstr = gf_strdup ("master not found");
                goto out;
        }

        ret = dict_get_str (dict, "slave", slave);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "slave not found");
                *op_errstr = gf_strdup ("slave not found");
                goto out;
        }


        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
glusterd_op_stage_gsync_set (dict_t *dict, char **op_errstr)
{
        int                     ret     = 0;
        int                     type    = 0;
        char                    *volname = NULL;
        char                    *slave   = NULL;
        gf_boolean_t            exists   = _gf_false;
        glusterd_volinfo_t      *volinfo = NULL;
        char                    errmsg[PATH_MAX] = {0,};


        ret = dict_get_int32 (dict, "type", &type);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "command type not found");
                *op_errstr = gf_strdup ("command unsuccessful");
                goto out;
        }

        switch (type) {
        case GF_GSYNC_OPTION_TYPE_STATUS:
                ret = glusterd_verify_gsync_status_opts (dict, op_errstr);

                goto out;
        case GF_GSYNC_OPTION_TYPE_CONFIG:
                ret = gsync_verify_config_options (dict, op_errstr);

                goto out;
        }

        ret = glusterd_op_gsync_args_get (dict, op_errstr, &volname, &slave);
        if (ret)
                goto out;

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if ((ret) || (!exists)) {
                gf_log ("", GF_LOG_WARNING, "volume name does not exist");
                snprintf (errmsg, sizeof(errmsg), "Volume name %s does not"
                          " exist", volname);
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }

        switch (type) {
        case GF_GSYNC_OPTION_TYPE_START:
                ret = glusterd_op_verify_gsync_start_options (volinfo, slave,
                                                              op_errstr);
                break;
        case GF_GSYNC_OPTION_TYPE_STOP:
                ret = glusterd_op_verify_gsync_running (volinfo, slave,
                                                        op_errstr);
                break;
        }

out:
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

static int
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
        int32_t               sub_count  = 0;
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

        if (GF_CLUSTER_TYPE_REPLICATE == volinfo->type) {
                ret = dict_get_int32 (dict, "replica-count",
                                      &sub_count);
                if (ret)
                        goto out;
        } else if (GF_CLUSTER_TYPE_STRIPE == volinfo->type) {
                ret = dict_get_int32 (dict, "stripe-count",
                                      &sub_count);
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

                sub_count = volinfo->stripe_count * volinfo->replica_count;
        }

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

        volinfo->sub_count = sub_count;

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

static int
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

static int
rb_regenerate_volfiles (glusterd_volinfo_t *volinfo,
                        glusterd_brickinfo_t *brickinfo,
                        int32_t pump_needed)
{
        dict_t *dict = NULL;
        int ret = 0;

        dict = volinfo->dict;

        gf_log ("", GF_LOG_DEBUG,
                "attempting to set pump value=%d", pump_needed);

        ret = dict_set_int32 (dict, "enable-pump", pump_needed);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "could not dict_set enable-pump");
                goto out;
        }

        ret = glusterd_create_rb_volfiles (volinfo, brickinfo);

out:
        return ret;
}

static int
rb_src_brick_restart (glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *src_brickinfo,
                      int activate_pump)
{
        int                     ret = 0;

        gf_log ("", GF_LOG_DEBUG,
                "Attempting to kill src");

        ret = glusterd_nfs_server_stop ();

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to stop nfs, ret: %d",
                        ret);
        }

        ret = glusterd_volume_stop_glusterfs (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to stop "
                        "glusterfs, ret: %d", ret);
                goto out;
        }

        glusterd_delete_volfile (volinfo, src_brickinfo);

        if (activate_pump) {
                ret = rb_regenerate_volfiles (volinfo, src_brickinfo, 1);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not regenerate volfiles with pump");
                        goto out;
                }
        } else {
                ret = rb_regenerate_volfiles (volinfo, src_brickinfo, 0);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not regenerate volfiles without pump");
                        goto out;
                }

        }

        sleep (2);
        ret = glusterd_volume_start_glusterfs (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to start "
                        "glusterfs, ret: %d", ret);
                goto out;
        }

out:
        ret = glusterd_nfs_server_start ();
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to start nfs, ret: %d",
                        ret);
        }
        return ret;
}

static int
rb_send_xattr_command (glusterd_volinfo_t *volinfo,
                       glusterd_brickinfo_t *src_brickinfo,
                       glusterd_brickinfo_t *dst_brickinfo,
                       const char *xattr_key,
                       const char *value)
{
        glusterd_conf_t  *priv                        = NULL;
        char              mount_point_path[PATH_MAX]  = {0,};
        struct stat       buf;
        int               ret                         = -1;

        priv = THIS->private;

        snprintf (mount_point_path, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_CLIENT_MOUNTPOINT);

        ret = stat (mount_point_path, &buf);
         if (ret) {
                 gf_log ("", GF_LOG_DEBUG,
                         "stat failed. Could not send "
                         " %s command", xattr_key);
                 goto out;
         }

        ret = sys_lsetxattr (mount_point_path, xattr_key,
                         value,
                         strlen (value) + 1,
                         0);

        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "setxattr failed");
                goto out;
        }

        ret = 0;

out:
        return ret;
}

static int
rb_spawn_dst_brick (glusterd_volinfo_t *volinfo,
                    glusterd_brickinfo_t *brickinfo)
{
        glusterd_conf_t    *priv           = NULL;
        runner_t            runner         = {0,};
        int                 ret            = -1;
        int32_t             port           = 0;

        priv = THIS->private;

        port = pmap_registry_alloc (THIS);
        brickinfo->port = port;

        GF_ASSERT (port);

        runinit (&runner);
        runner_add_arg (&runner, GFS_PREFIX"/sbin/glusterfs");
        runner_argprintf (&runner, "-f" "%s/vols/%s/"RB_DSTBRICKVOL_FILENAME,
                          priv->workdir, volinfo->volname);
        runner_argprintf (&runner, "-p" "%s/vols/%s/"RB_DSTBRICK_PIDFILE,
                          priv->workdir, volinfo->volname);
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "src-server.listen-port=%d", port);

        ret = runner_run (&runner);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not start glusterfs");
                goto out;
        }

        gf_log ("", GF_LOG_DEBUG,
                "Successfully started glusterfs: brick=%s:%s",
                brickinfo->hostname, brickinfo->path);

        ret = 0;

out:
        return ret;
}

static int
rb_spawn_glusterfs_client (glusterd_volinfo_t *volinfo,
                           glusterd_brickinfo_t *brickinfo)
{
        glusterd_conf_t    *priv           = NULL;
        char                cmd_str[8192]  = {0,};
        runner_t            runner         = {0,};
        struct stat         buf;
        int                 ret            = -1;

        priv = THIS->private;

        runinit (&runner);
        runner_add_arg (&runner, GFS_PREFIX"/sbin/glusterfs");
        runner_argprintf (&runner, "-f" "%s/vols/%s/"RB_CLIENTVOL_FILENAME,
                          priv->workdir, volinfo->volname);
        runner_argprintf (&runner, "%s/vols/%s/"RB_CLIENT_MOUNTPOINT,
                          priv->workdir, volinfo->volname);

        ret = runner_run (&runner);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not start glusterfs");
                goto out;
        }

        gf_log ("", GF_LOG_DEBUG,
                "Successfully started glusterfs: brick=%s:%s",
                brickinfo->hostname, brickinfo->path);

        memset (cmd_str, 0, sizeof (cmd_str));

        snprintf (cmd_str, 4096, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_CLIENT_MOUNTPOINT);

        ret = stat (cmd_str, &buf);
        if (ret) {
                 gf_log ("", GF_LOG_DEBUG,
                         "stat on mountpoint failed");
                 goto out;
         }

         gf_log ("", GF_LOG_DEBUG,
                 "stat on mountpoint succeeded");

        ret = 0;

out:
        return ret;
}

static const char *client_volfile_str =  "volume mnt-client\n"
        " type protocol/client\n"
        " option remote-host %s\n"
        " option remote-subvolume %s\n"
        " option remote-port %d\n"
        " option transport-type %s\n"
        "end-volume\n"
        "volume mnt-wb\n"
        " type performance/write-behind\n"
        " subvolumes mnt-client\n"
        "end-volume\n";

static int
rb_generate_client_volfile (glusterd_volinfo_t *volinfo,
                            glusterd_brickinfo_t *src_brickinfo)
{
        glusterd_conf_t  *priv                  = NULL;
        FILE             *file                  = NULL;
        char              filename[PATH_MAX]    = {0, };
        int               ret                   = -1;
        char             *ttype                 = NULL;

        priv = THIS->private;

        gf_log ("", GF_LOG_DEBUG,
                "Creating volfile");

        snprintf (filename, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_CLIENTVOL_FILENAME);

        file = fopen (filename, "w+");
        if (!file) {
                gf_log ("", GF_LOG_DEBUG,
                        "Open of volfile failed");
                ret = -1;
                goto out;
        }

        GF_ASSERT (src_brickinfo->port);

	ttype = glusterd_get_trans_type_rb (volinfo->transport_type);
	if (NULL == ttype){
		ret = -1;
		goto out;
	}

        fprintf (file, client_volfile_str, src_brickinfo->hostname,
                 src_brickinfo->path, src_brickinfo->port, ttype);

        fclose (file);
        GF_FREE (ttype);

        ret = 0;

out:
        return ret;
}

static const char *dst_brick_volfile_str = "volume src-posix\n"
        " type storage/posix\n"
        " option directory %s\n"
        "end-volume\n"
        "volume %s\n"
        " type features/locks\n"
        " subvolumes src-posix\n"
        "end-volume\n"
        "volume src-server\n"
        " type protocol/server\n"
        " option auth.addr.%s.allow *\n"
        " option transport-type %s\n"
        " subvolumes %s\n"
        "end-volume\n";

static int
rb_generate_dst_brick_volfile (glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t *dst_brickinfo)
{
        glusterd_conf_t    *priv                = NULL;
        FILE               *file                = NULL;
        char                filename[PATH_MAX]  = {0, };
        int                 ret                 = -1;
        char               *trans_type          = NULL;

        priv = THIS->private;

        gf_log ("", GF_LOG_DEBUG,
                "Creating volfile");

        snprintf (filename, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_DSTBRICKVOL_FILENAME);

        file = fopen (filename, "w+");
        if (!file) {
                gf_log ("", GF_LOG_DEBUG,
                        "Open of volfile failed");
                ret = -1;
                goto out;
        }

	trans_type = glusterd_get_trans_type_rb (volinfo->transport_type);
	if (NULL == trans_type){
		ret = -1;
		goto out;
	}

        fprintf (file, dst_brick_volfile_str, dst_brickinfo->path,
                 dst_brickinfo->path, dst_brickinfo->path,
                 trans_type, dst_brickinfo->path);

	GF_FREE (trans_type);

        fclose (file);

        ret = 0;

out:
        return ret;
}


static int
rb_mountpoint_mkdir (glusterd_volinfo_t *volinfo,
                     glusterd_brickinfo_t *src_brickinfo)
{
        glusterd_conf_t *priv                       = NULL;
        char             mount_point_path[PATH_MAX] = {0,};
        int              ret                        = -1;

        priv = THIS->private;

        snprintf (mount_point_path, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_CLIENT_MOUNTPOINT);

        ret = mkdir (mount_point_path, 0777);
        if (ret && (errno != EEXIST)) {
                gf_log ("", GF_LOG_DEBUG, "mkdir failed, errno: %d",
                        errno);
                goto out;
        }

        ret = 0;

out:
        return ret;
}

static int
rb_mountpoint_rmdir (glusterd_volinfo_t *volinfo,
                     glusterd_brickinfo_t *src_brickinfo)
{
        glusterd_conf_t *priv                       = NULL;
        char             mount_point_path[PATH_MAX] = {0,};
        int              ret                        = -1;

        priv = THIS->private;

        snprintf (mount_point_path, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_CLIENT_MOUNTPOINT);

        ret = rmdir (mount_point_path);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "rmdir failed");
                goto out;
        }

        ret = 0;

out:
        return ret;
}

static int
rb_destroy_maintenance_client (glusterd_volinfo_t *volinfo,
                                glusterd_brickinfo_t *src_brickinfo)
{
        glusterd_conf_t  *priv                        = NULL;
        runner_t          runner                      = {0,};
        char              filename[PATH_MAX]          = {0,};
        struct stat       buf;
        char              mount_point_path[PATH_MAX]  = {0,};
        int               ret                         = -1;

        priv = THIS->private;

        snprintf (mount_point_path, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_CLIENT_MOUNTPOINT);

        ret = stat (mount_point_path, &buf);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "stat failed. Cannot destroy maintenance "
                        "client");
                goto out;
        }

        runinit (&runner);
        runner_add_args (&runner, "/bin/umount", "-f", NULL);
        runner_argprintf (&runner, "%s/vols/%s/"RB_CLIENT_MOUNTPOINT,
                          priv->workdir, volinfo->volname);

        ret = runner_run (&runner);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "umount failed on maintenance client");
                goto out;
        }

        ret = rb_mountpoint_rmdir (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "rmdir of mountpoint failed");
                goto out;
        }

        snprintf (filename, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_CLIENTVOL_FILENAME);

        ret = unlink (filename);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "unlink failed");
                goto out;
        }

        ret = 0;

out:
        return ret;
}

static int
rb_spawn_maintenance_client (glusterd_volinfo_t *volinfo,
                             glusterd_brickinfo_t *src_brickinfo)
{
        int ret = -1;

        ret = rb_generate_client_volfile (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to generate client "
                        "volfile");
                goto out;
        }

        ret = rb_mountpoint_mkdir (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to mkdir "
                        "mountpoint");
                goto out;
        }

        ret = rb_spawn_glusterfs_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to start glusterfs");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

static int
rb_spawn_destination_brick (glusterd_volinfo_t *volinfo,
                            glusterd_brickinfo_t *dst_brickinfo)

{
        int ret = -1;

        ret = rb_generate_dst_brick_volfile (volinfo, dst_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to generate client "
                        "volfile");
                goto out;
        }

        ret = rb_spawn_dst_brick (volinfo, dst_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to start glusterfs");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

static int
rb_do_operation_start (glusterd_volinfo_t *volinfo,
                       glusterd_brickinfo_t *src_brickinfo,
                       glusterd_brickinfo_t *dst_brickinfo)
{
        char start_value[8192] = {0,};
        int ret = -1;


        gf_log ("", GF_LOG_DEBUG,
                "replace-brick sending start xattr");

        ret = rb_spawn_maintenance_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not spawn maintenance "
                        "client");
                goto out;
        }

	gf_log ("", GF_LOG_DEBUG,
		"mounted the replace brick client");

        snprintf (start_value, 8192, "%s:%s:%d",
                  dst_brickinfo->hostname,
                  dst_brickinfo->path,
                  dst_brickinfo->port);


        ret = rb_send_xattr_command (volinfo, src_brickinfo,
                                     dst_brickinfo, RB_PUMP_START_CMD,
                                     start_value);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Failed to send command to pump");
        }

        ret = rb_destroy_maintenance_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Failed to destroy maintenance "
                        "client");
                goto out;
        }

        gf_log ("", GF_LOG_DEBUG,
		"unmounted the replace brick client");
        ret = 0;

out:
        return ret;
}

static int
rb_do_operation_pause (glusterd_volinfo_t *volinfo,
                       glusterd_brickinfo_t *src_brickinfo,
                       glusterd_brickinfo_t *dst_brickinfo)
{
        int ret = -1;

        gf_log ("", GF_LOG_INFO,
                "replace-brick send pause xattr");

        ret = rb_spawn_maintenance_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not spawn maintenance "
                        "client");
                goto out;
        }

	gf_log ("", GF_LOG_DEBUG,
		"mounted the replace brick client");

        ret = rb_send_xattr_command (volinfo, src_brickinfo,
                                     dst_brickinfo, RB_PUMP_PAUSE_CMD,
                                     "jargon");
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Failed to send command to pump");

        }

        ret = rb_destroy_maintenance_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Failed to destroy maintenance "
                        "client");
                goto out;
        }


	gf_log ("", GF_LOG_DEBUG,
		"unmounted the replace brick client");

        ret = 0;

out:
	if (!glusterd_is_local_addr (src_brickinfo->hostname)) {
	        ret = rb_src_brick_restart (volinfo, src_brickinfo,
				                    0);
		 if (ret) {
			gf_log ("", GF_LOG_DEBUG,
                       "Could not restart src-brick");
		}
        }
        return ret;
}

static int
rb_kill_destination_brick (glusterd_volinfo_t *volinfo,
                           glusterd_brickinfo_t *dst_brickinfo)
{
        glusterd_conf_t  *priv               = NULL;
        char              pidfile[PATH_MAX]  = {0,};

        priv = THIS->private;

        snprintf (pidfile, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_DSTBRICK_PIDFILE);

        return glusterd_service_stop ("brick", pidfile, SIGTERM, _gf_true);
}

static int
rb_do_operation_commit (glusterd_volinfo_t *volinfo,
                       glusterd_brickinfo_t *src_brickinfo,
                       glusterd_brickinfo_t *dst_brickinfo)
{
        int ret     = -1;
        int cmd_ret = -1;

        gf_log ("", GF_LOG_DEBUG,
                "replace-brick sending commit xattr");

        ret = rb_spawn_maintenance_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not spawn maintenance "
                        "client");
                goto out;
        }

	gf_log ("", GF_LOG_DEBUG,
		"mounted the replace brick client");

        cmd_ret = rb_send_xattr_command (volinfo, src_brickinfo,
                                     dst_brickinfo, RB_PUMP_COMMIT_CMD,
                                     "jargon");
        if (cmd_ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Failed to send command to pump");
	}

        ret = rb_destroy_maintenance_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Failed to destroy maintenance "
                        "client");
                goto out;
        }

	gf_log ("", GF_LOG_DEBUG,
		"unmounted the replace brick client");

        ret = 0;

out:
        return cmd_ret || ret;
}

static int
rb_do_operation_abort (glusterd_volinfo_t *volinfo,
                       glusterd_brickinfo_t *src_brickinfo,
                       glusterd_brickinfo_t *dst_brickinfo)
{
        int ret = -1;

        gf_log ("", GF_LOG_DEBUG,
                "replace-brick sending abort xattr");

        ret = rb_spawn_maintenance_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not spawn maintenance "
                        "client");
                goto out;
        }

	gf_log ("", GF_LOG_DEBUG,
		"mounted the replace brick client");

        ret = rb_send_xattr_command (volinfo, src_brickinfo,
                                     dst_brickinfo, RB_PUMP_ABORT_CMD,
                                     "jargon");
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Failed to send command to pump");
	}

        ret = rb_destroy_maintenance_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Failed to destroy maintenance "
                        "client");
                goto out;
        }

	gf_log ("", GF_LOG_DEBUG,
		"unmounted the replace brick client");

        ret = 0;

out:
	if (!glusterd_is_local_addr (src_brickinfo->hostname)) {
	        ret = rb_src_brick_restart (volinfo, src_brickinfo,
					    0);
		 if (ret) {
			gf_log ("", GF_LOG_DEBUG,
                       "Could not restart src-brick");
		}
        }
        return ret;
}


static int
rb_get_xattr_command (glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *src_brickinfo,
                      glusterd_brickinfo_t *dst_brickinfo,
                      const char *xattr_key,
                      char *value)
{
        glusterd_conf_t  *priv                        = NULL;
        char              mount_point_path[PATH_MAX]  = {0,};
        struct stat       buf;
        int               ret                         = -1;

        priv = THIS->private;

        snprintf (mount_point_path, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_CLIENT_MOUNTPOINT);

       ret = stat (mount_point_path, &buf);
         if (ret) {
                 gf_log ("", GF_LOG_DEBUG,
                         "stat failed. Could not send "
                         " %s command", xattr_key);
                 goto out;
         }

        ret = sys_lgetxattr (mount_point_path, xattr_key, value, 8192);

        if (ret < 0) {
                gf_log ("", GF_LOG_DEBUG,
                        "getxattr failed");
                goto out;
        }

        ret = 0;

out:
        return ret;
}

static int
rb_do_operation_status (glusterd_volinfo_t *volinfo,
                        glusterd_brickinfo_t *src_brickinfo,
                        glusterd_brickinfo_t *dst_brickinfo)
{
        char            status[8192] = {0,};
        char            *status_reply = NULL;
        dict_t          *ctx          = NULL;
        int             ret = 0;
        gf_boolean_t    origin = _gf_false;

        ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
        if (!ctx) {
                gf_log ("", GF_LOG_ERROR,
                        "Operation Context is not present");
                goto out;
        }

        origin = _gf_true;

        if (origin) {
                ret = rb_spawn_maintenance_client (volinfo, src_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not spawn maintenance "
                                "client");
                        goto out;
                }

		gf_log ("", GF_LOG_DEBUG,
			"mounted the replace brick client");

                ret = rb_get_xattr_command (volinfo, src_brickinfo,
                                            dst_brickinfo, RB_PUMP_STATUS_CMD,
                                            status);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to get status from pump");
                        goto umount;
                }

                gf_log ("", GF_LOG_DEBUG,
                        "pump status is %s", status);

                status_reply = gf_strdup (status);
                if (!status_reply) {
                        gf_log ("", GF_LOG_ERROR, "Out of memory");
                        ret = -1;
                        goto umount;
                }

                ret = dict_set_dynstr (ctx, "status-reply",
                                       status_reply);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "failed to set pump status in ctx");

                }

	umount:
                ret = rb_destroy_maintenance_client (volinfo, src_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to destroy maintenance "
                                "client");
			goto out;
		}
        }

	gf_log ("", GF_LOG_DEBUG,
		"unmounted the replace brick client");
out:
        return ret;
}

/* Set src-brick's port number to be used in the maintainance mount
 * after all commit acks are received.
 */
static int
rb_update_srcbrick_port (glusterd_brickinfo_t *src_brickinfo, dict_t *rsp_dict,
                         dict_t *req_dict, int32_t replace_op)
{
        xlator_t *this            = NULL;
        dict_t   *ctx             = NULL;
        int       ret             = 0;
        int       dict_ret        = 0;
        int       src_port        = 0;

        this = THIS;

        ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
        if (ctx) {
                dict_ret = dict_get_int32 (req_dict, "src-brick-port", &src_port);
                if (src_port)
                        src_brickinfo->port = src_port;
        }

        if (!glusterd_is_local_addr (src_brickinfo->hostname)) {
                gf_log ("", GF_LOG_INFO,
                        "adding src-brick port no");

                src_brickinfo->port = pmap_registry_search (this,
                                      src_brickinfo->path, GF_PMAP_PORT_BRICKSERVER);
                if (!src_brickinfo->port &&
                    replace_op != GF_REPLACE_OP_COMMIT_FORCE ) {
                        gf_log ("", GF_LOG_ERROR,
                                "Src brick port not available");
                        ret = -1;
                        goto out;
                }

                if (rsp_dict) {
                        ret = dict_set_int32 (rsp_dict, "src-brick-port", src_brickinfo->port);
                        if (ret) {
                                gf_log ("", GF_LOG_DEBUG,
                                        "Could not set src-brick port no");
                                goto out;
                        }
                }

                ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
                if (ctx) {
                        ret = dict_set_int32 (ctx, "src-brick-port", src_brickinfo->port);
                        if (ret) {
                                gf_log ("", GF_LOG_DEBUG,
                                        "Could not set src-brick port no");
                                goto out;
                        }
                }

        }

out:
        return ret;

}

static int
rb_update_dstbrick_port (glusterd_brickinfo_t *dst_brickinfo, dict_t *rsp_dict,
                         dict_t *req_dict, int32_t replace_op)
{
        dict_t *ctx           = NULL;
        int     ret           = 0;
        int     dict_ret      = 0;
        int     dst_port      = 0;

        ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
        if (ctx) {
                dict_ret = dict_get_int32 (req_dict, "dst-brick-port", &dst_port);
                if (dst_port)
                        dst_brickinfo->port = dst_port;

        }

        if (!glusterd_is_local_addr (dst_brickinfo->hostname)) {
                gf_log ("", GF_LOG_INFO,
                        "adding dst-brick port no");

                if (rsp_dict) {
                        ret = dict_set_int32 (rsp_dict, "dst-brick-port",
                                              dst_brickinfo->port);
                        if (ret) {
                                gf_log ("", GF_LOG_DEBUG,
                                        "Could not set dst-brick port no in rsp dict");
                                goto out;
                        }
                }

                ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
                if (ctx) {
                        ret = dict_set_int32 (ctx, "dst-brick-port",
                                              dst_brickinfo->port);
                        if (ret) {
                                gf_log ("", GF_LOG_DEBUG,
                                        "Could not set dst-brick port no");
                                goto out;
                        }
                }
        }
out:
        return ret;
}



static int
glusterd_op_replace_brick (dict_t *dict, dict_t *rsp_dict)
{
        int                                      ret = 0;
        dict_t                                  *ctx  = NULL;
        int                                      replace_op = 0;
        glusterd_volinfo_t                      *volinfo = NULL;
        char                                    *volname = NULL;
        xlator_t                                *this = NULL;
        glusterd_conf_t                         *priv = NULL;
        char                                    *src_brick = NULL;
        char                                    *dst_brick = NULL;
        glusterd_brickinfo_t                    *src_brickinfo = NULL;
        glusterd_brickinfo_t                    *dst_brickinfo = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "src-brick", &src_brick);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get src brick");
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get dst brick");
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "dst brick=%s", dst_brick);

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = dict_get_int32 (dict, "operation", (int32_t *)&replace_op);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dict_get on operation failed");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = glusterd_volume_brickinfo_get_by_brick (src_brick, volinfo, &src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to get src-brickinfo");
                goto out;
        }


        ret = glusterd_get_rb_dst_brickinfo (volinfo, &dst_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get "
                         "replace brick destination brickinfo");
                goto out;
        }

        ret = glusterd_resolve_brick (dst_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to resolve dst-brickinfo");
                goto out;
        }

        ret = rb_update_srcbrick_port (src_brickinfo, rsp_dict,
                                       dict, replace_op);
        if (ret)
                goto out;

	if ((GF_REPLACE_OP_START != replace_op)) {
                ret = rb_update_dstbrick_port (dst_brickinfo, rsp_dict,
                                               dict, replace_op);
                if (ret)
                        goto out;
	}

        switch (replace_op) {
        case GF_REPLACE_OP_START:
        {
                if (!glusterd_is_local_addr (dst_brickinfo->hostname)) {
                        gf_log ("", GF_LOG_INFO,
                                "I AM THE DESTINATION HOST");
                        if (!glusterd_is_rb_paused (volinfo)) {
                                ret = rb_spawn_destination_brick (volinfo, dst_brickinfo);
                                if (ret) {
                                        gf_log ("", GF_LOG_DEBUG,
                                                "Failed to spawn destination brick");
                                        goto out;
                                }
                        } else {
                                gf_log ("", GF_LOG_ERROR, "Replace brick is already "
                                        "started=> no need to restart dst brick ");
                        }
		}


		if (!glusterd_is_local_addr (src_brickinfo->hostname)) {
		        ret = rb_src_brick_restart (volinfo, src_brickinfo,
				                    1);
		        if (ret) {
				gf_log ("", GF_LOG_DEBUG,
	                        "Could not restart src-brick");
			        goto out;
			}
		}

		if (!glusterd_is_local_addr (dst_brickinfo->hostname)) {
			gf_log ("", GF_LOG_INFO,
				"adding dst-brick port no");

                        ret = rb_update_dstbrick_port (dst_brickinfo, rsp_dict,
                                                       dict, replace_op);
                        if (ret)
                                goto out;
                }

                glusterd_set_rb_status (volinfo, GF_RB_STATUS_STARTED);
		break;
	}

        case GF_REPLACE_OP_COMMIT:
        case GF_REPLACE_OP_COMMIT_FORCE:
        {
                ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
                if (ctx) {
                        ret = rb_do_operation_commit (volinfo, src_brickinfo, dst_brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR,
                                        "Commit operation failed");
                                goto out;
                        }
                }

                ret = dict_set_int32 (volinfo->dict, "enable-pump", 0);
                gf_log ("", GF_LOG_DEBUG,
                        "Received commit - will be adding dst brick and "
                        "removing src brick");

                if (!glusterd_is_local_addr (dst_brickinfo->hostname) &&
                    replace_op != GF_REPLACE_OP_COMMIT_FORCE) {
                        gf_log ("", GF_LOG_INFO,
                                "I AM THE DESTINATION HOST");
                        ret = rb_kill_destination_brick (volinfo, dst_brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_DEBUG,
                                        "Failed to kill destination brick");
                                goto out;
                        }
                }

                if (ret) {
                        gf_log ("", GF_LOG_CRITICAL,
                                "Unable to cleanup dst brick");
                        goto out;
                }


                ret = glusterd_nfs_server_stop ();
                if (ret) {
                        gf_log ("", GF_LOG_ERROR,
                                "Unable to stop nfs server, ret: %d", ret);
                }

		ret = glusterd_op_perform_replace_brick (volinfo, src_brick,
							 dst_brick);
		if (ret) {
			gf_log ("", GF_LOG_CRITICAL, "Unable to add "
				"dst-brick: %s to volume: %s",
				dst_brick, volinfo->volname);
		        (void) glusterd_check_generate_start_nfs ();
			goto out;
		}

		volinfo->defrag_status = 0;

		ret = glusterd_check_generate_start_nfs ();
		if (ret) {
                        gf_log ("", GF_LOG_CRITICAL,
                                "Failed to generate nfs volume file");
		}


		ret = glusterd_fetchspec_notify (THIS);
                glusterd_set_rb_status (volinfo, GF_RB_STATUS_NONE);
                glusterd_brickinfo_delete (volinfo->dst_brick);
                volinfo->src_brick = volinfo->dst_brick = NULL;
        }
        break;

        case GF_REPLACE_OP_PAUSE:
        {
                gf_log ("", GF_LOG_DEBUG,
                        "Received pause - doing nothing");
                ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
                if (ctx) {
                        ret = rb_do_operation_pause (volinfo, src_brickinfo,
                                                     dst_brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR,
                                        "Pause operation failed");
                                goto out;
                        }
                }

                glusterd_set_rb_status (volinfo, GF_RB_STATUS_PAUSED);
        }
                break;

        case GF_REPLACE_OP_ABORT:
        {

                ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
                if (ctx) {
                        ret = rb_do_operation_abort (volinfo, src_brickinfo, dst_brickinfo);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_ERROR,
                                        "Abort operation failed");
                                goto out;
                        }
                }

                ret = dict_set_int32 (volinfo->dict, "enable-pump", 0);
		if (ret) {
			gf_log (THIS->name, GF_LOG_CRITICAL, "Unable to disable pump");
		}


                if (!glusterd_is_local_addr (dst_brickinfo->hostname)) {
                        gf_log (THIS->name, GF_LOG_INFO,
                                "I AM THE DESTINATION HOST");
                        ret = rb_kill_destination_brick (volinfo, dst_brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_DEBUG,
                                        "Failed to kill destination brick");
                                goto out;
                        }
                }
                glusterd_set_rb_status (volinfo, GF_RB_STATUS_NONE);
                glusterd_brickinfo_delete (volinfo->dst_brick);
                volinfo->src_brick = volinfo->dst_brick = NULL;
        }
        break;

        case GF_REPLACE_OP_STATUS:
        {
                gf_log ("", GF_LOG_DEBUG,
                        "received status - doing nothing");
                ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
                if (ctx) {
                        if (glusterd_is_rb_paused (volinfo)) {
                                ret = dict_set_str (ctx, "status-reply",
                                                 "replace brick has been paused");
                                if (ret)
                                        gf_log (THIS->name, GF_LOG_ERROR,
                                                "failed to set pump status"
                                                "in ctx");
                                goto out;
                        }

                        ret = rb_do_operation_status (volinfo, src_brickinfo,
                                                      dst_brickinfo);
                        if (ret)
                                goto out;
                }

        }
                break;

        default:
                ret = -1;
                goto out;
        }
        if (!ret && replace_op != GF_REPLACE_OP_STATUS)
		ret = glusterd_store_volinfo (volinfo,
                                              GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't store"
                        " replace brick operation's state");

out:
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
glusterd_options_reset (glusterd_volinfo_t *volinfo, int32_t is_force)
{
        int                      ret = 0;

        gf_log ("", GF_LOG_DEBUG, "Received volume set reset command");

        GF_ASSERT (volinfo->dict);

        dict_foreach (volinfo->dict, _delete_reconfig_opt, &is_force);

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

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_check_generate_start_nfs ();
        if (ret)
                goto out;

        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


static int
glusterd_op_reset_volume (dict_t *dict)
{
        glusterd_volinfo_t     *volinfo    = NULL;
        int                     ret        = -1;
        char                   *volname    = NULL;
        int32_t                is_force    = 0;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name " );
                goto out;
        }

        ret = dict_get_int32 (dict, "force", &is_force);
        if (ret)
                is_force = 0;

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = glusterd_options_reset (volinfo, is_force);

out:
        gf_log ("", GF_LOG_DEBUG, "'volume reset' returning %d", ret);
        return ret;

}

int
stop_gsync (char *master, char *slave, char **msg)
{
        int32_t         ret     = 0;
        int             pfd     = -1;
        pid_t           pid     = 0;
        char            pidfile[PATH_MAX] = {0,};
        char            buf [1024] = {0,};
        int             i       = 0;
        glusterd_conf_t *priv = NULL;

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;

        pfd = gsyncd_getpidfile (master, slave, pidfile);
        if (pfd == -2) {
                gf_log ("", GF_LOG_ERROR, GEOREP" stop validation "
                        " failed for %s & %s", master, slave);
                ret = -1;
                goto out;
        }
        if (gsync_status_byfd (pfd) == -1) {
                gf_log ("", GF_LOG_ERROR, "gsyncd b/w %s & %s is not"
                        " running", master, slave);
                if (msg)
                        *msg = gf_strdup ("Warning: "GEOREP" session was in "
                                          "corrupt  state");
                /* monitor gsyncd already dead */
                goto out;
        }

        ret = read (pfd, buf, 1024);
        if (ret > 0) {
                pid = strtol (buf, NULL, 10);
                ret = kill (-pid, SIGTERM);
                if (ret) {
                        gf_log ("", GF_LOG_WARNING,
                                "failed to kill gsyncd");
                        goto out;
                }
                for (i = 0; i < 20; i++) {
                        if (gsync_status_byfd (pfd) == -1) {
                                /* monitor gsyncd is dead but worker may
                                 * still be alive, give some more time
                                 * before SIGKILL (hack)
                                 */
                                usleep (50000);
                                break;
                        }
                        usleep (50000);
                }
                kill (-pid, SIGKILL);
                unlink (pidfile);
        }
        ret = 0;

out:
        close (pfd);
        return ret;
}

int
glusterd_check_restart_gsync_session (glusterd_volinfo_t *volinfo, char *slave,
                                      dict_t *resp_dict);

int
glusterd_gsync_configure (glusterd_volinfo_t *volinfo, char *slave,
                          dict_t *dict, dict_t *resp_dict, char **op_errstr)
{
        int32_t         ret     = -1;
        char            *op_name = NULL;
        char            *op_value = NULL;
        runner_t        runner    = {0,};
        glusterd_conf_t *priv   = NULL;
        char            *subop  = NULL;
        char            *master = NULL;

        GF_ASSERT (slave);
        GF_ASSERT (op_errstr);
        GF_ASSERT (dict);
        GF_ASSERT (resp_dict);

        ret = dict_get_str (dict, "subop", &subop);
        if (ret != 0)
                goto out;

        if (strcmp (subop, "get") == 0 || strcmp (subop, "get-all") == 0) {
                /* deferred to cli */
                gf_log ("", GF_LOG_DEBUG, "Returning 0");
                return 0;
        }

        ret = dict_get_str (dict, "op_name", &op_name);
        if (ret != 0)
                goto out;

        if (strcmp (subop, "set") == 0) {
                ret = dict_get_str (dict, "op_value", &op_value);
                if (ret != 0)
                        goto out;
        }

        if (THIS)
                priv = THIS->private;
        if (priv == NULL) {
                gf_log ("", GF_LOG_ERROR, "priv of glusterd not present");
                *op_errstr = gf_strdup ("glusterd defunct");
                goto out;
        }

        master = "";
        runinit (&runner);
        runner_add_args (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s/"GSYNC_CONF, priv->workdir);
        if (volinfo) {
                master = volinfo->volname;
                runner_argprintf (&runner, ":%s", master);
        }
        runner_add_arg (&runner, slave);
        runner_argprintf (&runner, "--config-%s", subop);
        runner_add_arg (&runner, op_name);
        if (op_value)
                runner_add_arg (&runner, op_value);
        ret = runner_run (&runner);
        if (ret) {
                gf_log ("", GF_LOG_WARNING, "gsyncd failed to "
                        "%s %s option for %s %s peers",
                        subop, op_name, master, slave);

                gf_asprintf (op_errstr, GEOREP" config-%s failed for %s %s",
                             subop, master, slave);

                goto out;
        }
        ret = 0;
        gf_asprintf (op_errstr, "config-%s successful", subop);

out:
        if (!ret && volinfo) {
                ret = glusterd_check_restart_gsync_session (volinfo, slave,
                                                            resp_dict);
                if (ret)
                        *op_errstr = gf_strdup ("internal error");
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_gsync_read_frm_status (char *path, char *data)
{
        int                 ret = 0;
        FILE               *status_file = NULL;

        GF_ASSERT (path);
        GF_ASSERT (data);
        status_file = fopen (path, "r");
        if (status_file  == NULL) {
                gf_log ("", GF_LOG_WARNING, "Unable to read gsyncd status"
                        " file");
                return -1;
        }
        ret = fread (data, PATH_MAX, 1, status_file);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "Status file of gsyncd is corrupt");
                return -1;
        }

        data[strlen(data)-1] = '\0';

        return 0;
}

int
glusterd_read_status_file (char *master, char *slave,
                           dict_t *dict)
{
        glusterd_conf_t  *priv = NULL;
        int              ret = 0;
        char             statusfile[PATH_MAX] = {0, };
        char             buff[PATH_MAX] = {0, };
        char             mst[PATH_MAX] = {0, };
        char             slv[PATH_MAX] = {0, };
        char             sts[PATH_MAX] = {0, };
        int              gsync_count = 0;
        int              status = 0;

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;
        ret = glusterd_gsync_get_param_file (statusfile, "state", master,
                                             slave, priv->workdir);
        if (ret) {
                gf_log ("", GF_LOG_WARNING, "Unable to get the name of status"
                        "file for %s(master), %s(slave)", master, slave);
                goto out;

        }

        ret = gsync_status (master, slave, &status);
        if (ret == 0 && status == -1) {
                strncpy (buff, "corrupt", sizeof (buff));
                goto done;
        } else if (ret == -1)
                goto out;

        ret = glusterd_gsync_read_frm_status (statusfile, buff);
        if (ret) {
                gf_log ("", GF_LOG_WARNING, "Unable to read the status"
                        "file for %s(master), %s(slave)", master, slave);
                goto out;

        }

 done:
        ret = dict_get_int32 (dict, "gsync-count", &gsync_count);

        if (ret)
                gsync_count = 1;
        else
                gsync_count++;

        snprintf (mst, sizeof (mst), "master%d", gsync_count);
        ret = dict_set_dynstr (dict, mst, gf_strdup (master));
        if (ret)
                goto out;

        snprintf (slv, sizeof (slv), "slave%d", gsync_count);
        ret = dict_set_dynstr (dict, slv, gf_strdup (slave));
        if (ret)
                goto out;

        snprintf (sts, sizeof (slv), "status%d", gsync_count);
        ret = dict_set_dynstr (dict, sts, gf_strdup (buff));
        if (ret)
                goto out;
        ret = dict_set_int32 (dict, "gsync-count", gsync_count);
        if (ret)
                goto out;

        ret = 0;
 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d ", ret);
        return ret;
}

int
glusterd_check_restart_gsync_session (glusterd_volinfo_t *volinfo, char *slave,
                                      dict_t *resp_dict)
{

        int                    ret = 0;
        uuid_t                 uuid = {0, };
        glusterd_conf_t        *priv = NULL;
        char                   *status_msg = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;

        if (glusterd_gsync_get_uuid (slave, volinfo, uuid))
                /* session does not exist, nothing to do */
                goto out;
        if (uuid_compare (priv->uuid, uuid) == 0) {
                ret = stop_gsync (volinfo->volname, slave, &status_msg);
                if (ret == 0 && status_msg)
                        ret = dict_set_str (resp_dict, "gsync-status",
                                            status_msg);
                if (ret == 0)
                        ret = glusterd_start_gsync (volinfo, slave,
                                                    uuid_utoa(priv->uuid), NULL);
        }

 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_marker_create_volfile (glusterd_volinfo_t *volinfo)
{
        int32_t          ret     = 0;

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to create volfile"
                        " for setting of marker while '"GEOREP" start'");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_check_generate_start_nfs ();
        ret = 0;
out:
        return ret;
}

int
glusterd_set_marker_gsync (glusterd_volinfo_t *volinfo)
{
        int                      ret     = -1;
        int                      marker_set = _gf_false;
        char                    *gsync_status = NULL;
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;

        marker_set = glusterd_volinfo_get_boolean (volinfo, VKEY_MARKER_XTIME);
        if (marker_set == -1) {
                gf_log ("", GF_LOG_ERROR, "failed to get the marker status");
                ret = -1;
                goto out;
        }

        if (marker_set == _gf_false) {
                gsync_status = gf_strdup ("on");
                if (gsync_status == NULL) {
                        ret = -1;
                        goto out;
                }

                ret = glusterd_gsync_volinfo_dict_set (volinfo,
                                                       VKEY_MARKER_XTIME, gsync_status);
                if (ret < 0)
                        goto out;

                ret = glusterd_marker_create_volfile (volinfo);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Setting dict failed");
                        goto out;
                }
        }
        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}




int
glusterd_get_gsync_status_mst_slv( glusterd_volinfo_t *volinfo,
                                   char *slave, dict_t *rsp_dict)
{
        uuid_t             uuid = {0, };
        glusterd_conf_t    *priv = NULL;
        int                ret = 0;

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;

        ret = glusterd_gsync_get_uuid (slave, volinfo, uuid);
        if ((ret == 0) && (uuid_compare (priv->uuid, uuid) != 0))
                goto out;

        if (ret) {
                ret = 0;
                gf_log ("", GF_LOG_INFO, "geo-replication status %s %s :"
                        "session is not active", volinfo->volname, slave);
                goto out;
        }

        ret = glusterd_read_status_file (volinfo->volname, slave, rsp_dict);
 out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

static int
glusterd_get_gsync_status_mst (glusterd_volinfo_t *volinfo, dict_t *rsp_dict)
{
        glusterd_gsync_status_temp_t  param = {0, };

        GF_ASSERT (volinfo);

        param.rsp_dict = rsp_dict;
        param.volinfo = volinfo;
        dict_foreach (volinfo->gsync_slaves, _get_status_mst_slv, &param);

        return 0;
}

static int
glusterd_get_gsync_status_all ( dict_t *rsp_dict)
{

        int32_t                 ret = 0;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *volinfo = NULL;

        GF_ASSERT (THIS);
        priv = THIS->private;

        GF_ASSERT (priv);

        list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                ret = glusterd_get_gsync_status_mst (volinfo, rsp_dict);
                if (ret)
                        goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;

}

static int
glusterd_get_gsync_status (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        char               *slave  = NULL;
        char               *volname = NULL;
        char               errmsg[PATH_MAX] = {0, };
        gf_boolean_t       exists = _gf_false;
        glusterd_volinfo_t *volinfo = NULL;
        int                ret = 0;


        ret = dict_get_str (dict, "master", &volname);
        if (ret < 0){
                ret = glusterd_get_gsync_status_all (rsp_dict);
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if ((ret) || (!exists)) {
                gf_log ("", GF_LOG_WARNING, "volume name does not exist");
                snprintf (errmsg, sizeof(errmsg), "Volume name %s does not"
                          " exist", volname);
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }


        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0) {
                ret = glusterd_get_gsync_status_mst (volinfo, rsp_dict);
                goto out;
        }

        ret = glusterd_get_gsync_status_mst_slv (volinfo, slave, rsp_dict);

 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;


}


int
glusterd_op_gsync_set (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        int32_t             ret     = -1;
        int32_t             type    = -1;
        dict_t             *ctx    = NULL;
        dict_t             *resp_dict = NULL;
        char               *host_uuid = NULL;
        char               *slave  = NULL;
        char               *volname = NULL;
        glusterd_volinfo_t *volinfo = NULL;
        glusterd_conf_t    *priv = NULL;
        char               *status_msg = NULL;
        uuid_t              uuid = {0, };

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        priv = THIS->private;

        ret = dict_get_int32 (dict, "type", &type);
        if (ret < 0)
                goto out;

        ret = dict_get_str (dict, "host-uuid", &host_uuid);
        if (ret < 0)
                goto out;

        ctx = glusterd_op_get_ctx (GD_OP_GSYNC_SET);
        resp_dict = ctx ? ctx : rsp_dict;
        GF_ASSERT (resp_dict);

        if (type == GF_GSYNC_OPTION_TYPE_STATUS) {
                ret = glusterd_get_gsync_status (dict, op_errstr, resp_dict);
                goto out;
        }

        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0)
                goto out;

        if (dict_get_str (dict, "master", &volname) == 0) {
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        gf_log ("", GF_LOG_WARNING, "Volinfo for %s (master) not found",
                                volname);
                        goto out;
                }
        }

        if (type == GF_GSYNC_OPTION_TYPE_CONFIG) {
                ret = glusterd_gsync_configure (volinfo, slave, dict, resp_dict,
                                                op_errstr);
                goto out;
        }

        if (!volinfo) {
                ret = -1;
                goto out;
        }

        if (type == GF_GSYNC_OPTION_TYPE_START) {

                ret = glusterd_set_marker_gsync (volinfo);
                if (ret != 0) {
                        gf_log ("", GF_LOG_WARNING, "marker start failed");
                        *op_errstr = gf_strdup ("failed to initialize indexing");
                        ret = -1;
                        goto out;
                }
                ret = glusterd_store_slave_in_info(volinfo, slave,
                                                   host_uuid, op_errstr);
                if (ret)
                        goto out;

                ret = glusterd_start_gsync (volinfo, slave, host_uuid,
                                            op_errstr);
        }

        if (type == GF_GSYNC_OPTION_TYPE_STOP) {

                ret = glusterd_gsync_get_uuid (slave, volinfo, uuid);
                if (ret) {
                        gf_log ("", GF_LOG_WARNING, GEOREP" is not set up for"
                                "%s(master) and %s(slave)", volname, slave);
                        *op_errstr = strdup (GEOREP" is not set up");
                        goto out;
                }

                ret = glusterd_remove_slave_in_info(volinfo, slave,
                                                    host_uuid, op_errstr);
                if (ret)
                        goto out;

                if (uuid_compare (priv->uuid, uuid) != 0) {
                        goto out;
                }

                ret = stop_gsync (volname, slave, &status_msg);
                if (ret == 0 && status_msg)
                        ret = dict_set_str (resp_dict, "gsync-status",
                                            status_msg);
                if (ret != 0)
                        *op_errstr = gf_strdup ("internal error");
        }

out:
        gf_log ("", GF_LOG_DEBUG,"Returning %d", ret);
        return ret;
}

int32_t
glusterd_check_if_quota_trans_enabled (glusterd_volinfo_t *volinfo)
{
        int32_t  ret           = 0;
        int      flag          = _gf_false;

        flag = glusterd_volinfo_get_boolean (volinfo, VKEY_FEATURES_QUOTA);
        if (flag == -1) {
                gf_log ("", GF_LOG_ERROR, "failed to get the quota status");
                ret = -1;
                goto out;
        }

        if (flag == _gf_false) {
                gf_log ("", GF_LOG_ERROR, "first enable the quota translator");
                ret = -1;
                goto out;
        }
        ret = 0;
out:
        return ret;
}

/* At the end of the function, the variable found will be set
 * to true if the path to be removed was present in the limit-list,
 * else will be false.
 */
int32_t
_glusterd_quota_remove_limits (char **quota_limits, char *path,
                               gf_boolean_t *found)
{
        int      ret      = 0;
        int      i        = 0;
        int      size     = 0;
        int      len      = 0;
        int      pathlen  = 0;
        int      skiplen  = 0;
        int      flag     = 0;
        char    *limits   = NULL;
        char    *qlimits  = NULL;

        if (found != NULL)
                *found = _gf_false;

        if (*quota_limits == NULL)
                return -1;

        qlimits = *quota_limits;

        pathlen = strlen (path);

        len = strlen (qlimits);

        limits = GF_CALLOC (len + 1, sizeof (char), gf_gld_mt_char);
        if (!limits)
                return -1;

        while (i < len) {
                if (!memcmp ((void *) &qlimits [i], (void *)path, pathlen))
                        if (qlimits [i + pathlen] == ':') {
                                flag = 1;
                                if (found != NULL)
                                        *found = _gf_true;
                        }

                while (qlimits [i + size] != ',' &&
                       qlimits [i + size] != '\0')
                        size++;

                if (!flag) {
                        memcpy ((void *) &limits [i], (void *) &qlimits [i], size + 1);
                } else {
                        skiplen = size + 1;
                        size = len - i - size;
                        memcpy ((void *) &limits [i], (void *) &qlimits [i + skiplen], size);
                        break;
                }

                i += size + 1;
                size = 0;
        }

        if (!flag) {
                ret = 1;
        } else {
                len = strlen (limits);

                if (len == 0) {
                        GF_FREE (qlimits);

                        *quota_limits = NULL;

                        goto out;
                }

                if (limits[len - 1] == ',') {
                        limits[len - 1] = '\0';
                        len --;
                }

                GF_FREE (qlimits);

                qlimits = GF_CALLOC (len + 1, sizeof (char), gf_gld_mt_char);

                if (!qlimits) {
                        ret = -1;
                        goto out;
                }

                memcpy ((void *) qlimits, (void *) limits, len + 1);

                *quota_limits = qlimits;

                ret = 0;
        }

out:
        if (limits)
                GF_FREE (limits);

        return ret;
}

int32_t
glusterd_quota_initiate_fs_crawl (glusterd_conf_t *priv, char *volname)
{
        int32_t   ret = 0;
        pid_t     pid;
        char      mountdir [] = "/tmp/mntXXXXXX";
        runner_t  runner = {0,};
        int       status = 0;

        if (mkdtemp (mountdir) == NULL) {
                gf_log ("glusterd", GF_LOG_DEBUG,
                        "failed to create a temporary mount directory");
                ret = -1;
                goto out;
        }

        runinit (&runner);
        runner_add_args (&runner, GFS_PREFIX"/sbin/glusterfs", "-s",
                         "localhost", "--volfile-id", volname, "-l",
                         DEFAULT_LOG_FILE_DIRECTORY"/quota-crawl.log",
                         mountdir, NULL);

        ret = runner_run_reuse (&runner);
        if (ret == -1) {
                runner_log (&runner, "glusterd", GF_LOG_DEBUG, "command failed");
                runner_end (&runner);
                goto out;
        }
        runner_end (&runner);

        if ((pid = fork ()) < 0) {
                gf_log ("glusterd", GF_LOG_WARNING, "fork from parent failed");
                ret = -1;
                goto out;
        } else if (pid == 0) {//first child
                /* fork one more to not hold back main process on
                 * blocking call below
                 */
                pid = fork ();
                if (pid)
                        _exit (pid > 0 ? EXIT_SUCCESS : EXIT_FAILURE);

                ret = chdir (mountdir);
                if (ret == -1) {
                        gf_log ("glusterd", GF_LOG_WARNING, "chdir %s failed, "
                                "reason: %s", mountdir, strerror (errno));
                        exit (EXIT_FAILURE);
                }
                runinit (&runner);
                runner_add_args (&runner, "/usr/bin/find", "find", ".", NULL);
                if (runner_start (&runner) == -1)
                        _exit (EXIT_FAILURE);

#ifndef GF_LINUX_HOST_OS
                runner_end (&runner); /* blocks in waitpid */
                runcmd ("umount", mountdir, NULL);
#else
                runcmd ("umount", "-l", mountdir, NULL);
#endif
                rmdir (mountdir);
                _exit (EXIT_SUCCESS);
        }
        ret = (waitpid (pid, &status, 0) == pid &&
               WIFEXITED (status) && WEXITSTATUS (status) == EXIT_SUCCESS) ? 0 : -1;

out:
        return ret;
}

char *
glusterd_quota_get_limit_value (char *quota_limits, char *path)
{
        int32_t i, j, k, l, len;
        int32_t pat_len, diff;
        char   *ret_str = NULL;

        len = strlen (quota_limits);
        pat_len = strlen (path);
        i = 0;
        j = 0;

        while (i < len) {
                j = i;
                k = 0;
                while (path [k] == quota_limits [j]) {
                        j++;
                        k++;
                }

                l = j;

                while (quota_limits [j] != ',' &&
                       quota_limits [j] != '\0')
                        j++;

                if (quota_limits [l] == ':' && pat_len == (l - i)) {
                        diff = j - i;
                        ret_str = GF_CALLOC (diff + 1, sizeof (char),
                                             gf_gld_mt_char);

                        strncpy (ret_str, &quota_limits [i], diff);

                        break;
                }
                i = ++j; //skip ','
        }

        return ret_str;
}

char*
_glusterd_quota_get_limit_usages (glusterd_volinfo_t *volinfo,
                                  char *path, char **op_errstr)
{
        int32_t  ret          = 0;
        char    *quota_limits = NULL;
        char    *ret_str      = NULL;

        if (volinfo == NULL)
                return NULL;

        ret = glusterd_volinfo_get (volinfo, VKEY_FEATURES_LIMIT_USAGE,
                                    &quota_limits);
        if (ret)
                return NULL;
        if (quota_limits == NULL) {
                ret_str = NULL;
                *op_errstr = gf_strdup ("Limit not set on any directory");
        } else if (path == NULL)
                ret_str = gf_strdup (quota_limits);
        else
                ret_str = glusterd_quota_get_limit_value (quota_limits, path);

        return ret_str;
}

int32_t
glusterd_quota_get_limit_usages (glusterd_conf_t *priv,
                                 glusterd_volinfo_t *volinfo,
                                 char *volname,
                                 dict_t *dict,
                                 char **op_errstr)
{
        int32_t i               = 0;
        int32_t ret             = 0;
        int32_t count           = 0;
        char    *path           = NULL;
        dict_t  *ctx            = NULL;
        char    cmd_str [1024]  = {0, };
        char   *ret_str         = NULL;

        ctx = glusterd_op_get_ctx (GD_OP_QUOTA);
        if (ctx == NULL)
                return 0;

        ret = dict_get_int32 (dict, "count", &count);
        if (ret < 0)
                goto out;

        if (count == 0) {
                ret_str = _glusterd_quota_get_limit_usages (volinfo, NULL,
                                                            op_errstr);
        } else {
                i = 0;
                while (count--) {
                        snprintf (cmd_str, 1024, "path%d", i++);

                        ret = dict_get_str (dict, cmd_str, &path);
                        if (ret < 0)
                                goto out;

                        ret_str = _glusterd_quota_get_limit_usages (volinfo, path, op_errstr);
                }
        }

        if (ret_str) {
                ret = dict_set_dynstr (ctx, "limit_list", ret_str);
        }
out:
        return ret;
}

int32_t
glusterd_quota_enable (glusterd_volinfo_t *volinfo, char **op_errstr,
                       gf_boolean_t *crawl)
{
        int32_t         ret     = -1;
        char            *quota_status = NULL;

        GF_VALIDATE_OR_GOTO ("glusterd", volinfo, out);
        GF_VALIDATE_OR_GOTO ("glusterd", crawl, out);
        GF_VALIDATE_OR_GOTO ("glusterd", op_errstr, out);

        if (glusterd_is_volume_started (volinfo) == 0) {
                *op_errstr = gf_strdup ("Volume is stopped, start volume "
                                        "to enable quota.");
                goto out;
        }

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret == 0) {
                *op_errstr = gf_strdup ("Quota is already enabled");
                goto out;
        }

        quota_status = gf_strdup ("on");
        if (!quota_status) {
                gf_log ("", GF_LOG_ERROR, "memory allocation failed");
                *op_errstr = gf_strdup ("Enabling quota has been unsuccessful");
                goto out;
        }

        ret = dict_set_dynstr (volinfo->dict, VKEY_FEATURES_QUOTA, quota_status);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "dict set failed");
                *op_errstr = gf_strdup ("Enabling quota has been unsuccessful");
                goto out;
        }

        *op_errstr = gf_strdup ("Enabling quota has been successful");

        *crawl = _gf_true;

        ret = 0;
out:
        return ret;
}

int32_t
glusterd_quota_disable (glusterd_volinfo_t *volinfo, char **op_errstr)
{
        int32_t  ret            = -1;
        char    *quota_status   = NULL, *quota_limits = NULL;

        GF_VALIDATE_OR_GOTO ("glusterd", volinfo, out);
        GF_VALIDATE_OR_GOTO ("glusterd", op_errstr, out);

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret == -1) {
                *op_errstr = gf_strdup ("Quota is already disabled");
                goto out;
        }

        quota_status = gf_strdup ("off");
        if (!quota_status) {
                gf_log ("", GF_LOG_ERROR, "memory allocation failed");
                *op_errstr = gf_strdup ("Disabling quota has been unsuccessful");
                goto out;
        }

        ret = dict_set_dynstr (volinfo->dict, VKEY_FEATURES_QUOTA, quota_status);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "dict set failed");
                *op_errstr = gf_strdup ("Disabling quota has been unsuccessful");
                goto out;
        }

        *op_errstr = gf_strdup ("Disabling quota has been successful");

        ret = glusterd_volinfo_get (volinfo, VKEY_FEATURES_LIMIT_USAGE,
                                    &quota_limits);
        if (ret) {
                gf_log ("", GF_LOG_WARNING, "failed to get the quota limits");
        } else {
                GF_FREE (quota_limits);
        }

        dict_del (volinfo->dict, VKEY_FEATURES_LIMIT_USAGE);

out:
        return ret;
}

int32_t
glusterd_quota_limit_usage (glusterd_volinfo_t *volinfo, dict_t *dict, char **op_errstr)
{
        int32_t          ret    = -1;
        char            *path   = NULL;
        char            *limit  = NULL;
        char            *value  = NULL;
        char             msg [1024] = {0,};
        char            *quota_limits = NULL;

        GF_VALIDATE_OR_GOTO ("glusterd", dict, out);
        GF_VALIDATE_OR_GOTO ("glusterd", volinfo, out);
        GF_VALIDATE_OR_GOTO ("glusterd", op_errstr, out);

        ret = glusterd_volinfo_get (volinfo, VKEY_FEATURES_LIMIT_USAGE,
                                    &quota_limits);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "failed to get the quota limits");
                *op_errstr = gf_strdup ("failed to set limit");
                goto out;
        }

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to fetch quota limits" );
                *op_errstr = gf_strdup ("failed to set limit");
                goto out;
        }

        ret = dict_get_str (dict, "limit", &limit);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to fetch quota limits" );
                *op_errstr = gf_strdup ("failed to set limit");
                goto out;
        }

        if (quota_limits) {
                ret = _glusterd_quota_remove_limits (&quota_limits, path, NULL);
                if (ret == -1) {
                        gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                        *op_errstr = gf_strdup ("failed to set limit");
                        goto out;
                }
        }

        if (quota_limits == NULL) {
                ret = gf_asprintf (&value, "%s:%s", path, limit);
                if (ret == -1) {
                        gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                        *op_errstr = gf_strdup ("failed to set limit");
                        goto out;
                }
        } else {
                ret = gf_asprintf (&value, "%s,%s:%s",
                                   quota_limits, path, limit);
                if (ret == -1) {
                        gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                        *op_errstr = gf_strdup ("failed to set limit");
                        goto out;
                }

                GF_FREE (quota_limits);
        }

        quota_limits = value;

        ret = dict_set_str (volinfo->dict, VKEY_FEATURES_LIMIT_USAGE,
                            quota_limits);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set quota limits" );
                *op_errstr = gf_strdup ("failed to set limit");
                goto out;
        }
        snprintf (msg, 1024, "limit set on %s", path);
        *op_errstr = gf_strdup (msg);

        ret = 0;
out:
        return ret;
}

int32_t
glusterd_quota_remove_limits (glusterd_volinfo_t *volinfo, dict_t *dict, char **op_errstr)
{
        int32_t         ret                   = -1;
        char            str [PATH_MAX + 1024] = {0,};
        char            *quota_limits         = NULL;
        char            *path                 = NULL;
        gf_boolean_t     flag                 = _gf_false;

        GF_VALIDATE_OR_GOTO ("glusterd", dict, out);
        GF_VALIDATE_OR_GOTO ("glusterd", volinfo, out);
        GF_VALIDATE_OR_GOTO ("glusterd", op_errstr, out);

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret == -1) {
                *op_errstr = gf_strdup ("Quota is disabled, please enable quota");
                goto out;
        }

        ret = glusterd_volinfo_get (volinfo, VKEY_FEATURES_LIMIT_USAGE,
                                    &quota_limits);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "failed to get the quota limits");
                goto out;
        }

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to fetch quota limits" );
                goto out;
        }

        ret = _glusterd_quota_remove_limits (&quota_limits, path, &flag);
        if (ret == -1) {
                if (flag == _gf_true)
                        snprintf (str, sizeof (str), "Removing limit on %s has "
                                  "been unsuccessful", path);
                else
                        snprintf (str, sizeof (str), "%s has no limit set", path);
                *op_errstr = gf_strdup (str);
                goto out;
        } else {
                if (flag == _gf_true)
                        snprintf (str, sizeof (str), "Removed quota limit on "
                                  "%s", path);
                else
                        snprintf (str, sizeof (str), "no limit set on %s",
                                  path);
                *op_errstr = gf_strdup (str);
        }

        if (quota_limits) {
                ret = dict_set_str (volinfo->dict, VKEY_FEATURES_LIMIT_USAGE,
                                    quota_limits);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to set quota limits" );
                        goto out;
                }
        } else {
               dict_del (volinfo->dict, VKEY_FEATURES_LIMIT_USAGE);
        }

        ret = 0;

out:
        return ret;
}


int
glusterd_op_quota (dict_t *dict, char **op_errstr)
{
        glusterd_volinfo_t     *volinfo      = NULL;
        int32_t                 ret          = -1;
        char                   *volname      = NULL;
        dict_t                 *ctx          = NULL;
        int                     type         = -1;
        gf_boolean_t            start_crawl  = _gf_false;
        glusterd_conf_t        *priv         = NULL;

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        priv = THIS->private;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name " );
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);

        if (type == GF_QUOTA_OPTION_TYPE_ENABLE) {
                ret = glusterd_quota_enable (volinfo, op_errstr, &start_crawl);
                if (ret < 0)
                        goto out;

                goto create_vol;
        }

        if (type == GF_QUOTA_OPTION_TYPE_DISABLE) {
                ret = glusterd_quota_disable (volinfo, op_errstr);
                if (ret < 0)
                        goto out;

                goto create_vol;
        }

        if (type == GF_QUOTA_OPTION_TYPE_LIMIT_USAGE) {
                ret = glusterd_quota_limit_usage (volinfo, dict, op_errstr);
                if (ret < 0)
                        goto out;

                goto create_vol;
        }

        if (type == GF_QUOTA_OPTION_TYPE_REMOVE) {
                ret = glusterd_quota_remove_limits (volinfo, dict, op_errstr);
                if (ret < 0)
                        goto out;

                goto create_vol;
        }

        if (type == GF_QUOTA_OPTION_TYPE_LIST) {
                ret = glusterd_check_if_quota_trans_enabled (volinfo);
                if (ret == -1) {
                        *op_errstr = gf_strdup ("cannot list the limits, "
                                                "quota is disabled");
                        goto out;
                }

                ret = glusterd_quota_get_limit_usages (priv, volinfo, volname,
                                                       dict, op_errstr);

                goto out;
        }
create_vol:
        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to re-create volfile for"
                                          " 'quota'");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_check_generate_start_nfs ();

        ret = 0;

out:
        ctx = glusterd_op_get_ctx (GD_OP_QUOTA);
        if (ctx && start_crawl == _gf_true)
                glusterd_quota_initiate_fs_crawl (priv, volname);

        if (ctx && *op_errstr) {
                ret = dict_set_dynstr (ctx, "errstr", *op_errstr);
                if (ret) {
                        GF_FREE (*op_errstr);
                        gf_log ("", GF_LOG_DEBUG,
                                "failed to set error message in ctx");
                }
                *op_errstr = NULL;
        }

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
glusterd_restart_brick_servers (glusterd_volinfo_t *volinfo)
{
        if (!volinfo)
                return -1;
        if (glusterd_stop_bricks (volinfo)) {
                gf_log ("", GF_LOG_ERROR, "Restart Failed: Unable to "
                                          "stop brick servers");
                return -1;
        }
        usleep (500000);
        if (glusterd_start_bricks (volinfo)) {
                gf_log ("", GF_LOG_ERROR, "Restart Failed: Unable to "
                                          "start brick servers");
                return -1;
        }
        return 0;
}

static int
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
        int                                      restart_flag = 0;
	char					*key = NULL;
	char					*key_fixed = NULL;
	char					*value = NULL;
	char					 str[50] = {0, };
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

                if (restart_flag) {
                        if (glusterd_restart_brick_servers (volinfo)) {
                                ret = -1;
                                goto out;
                        }
                }

                ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret)
                        goto out;

                if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                        ret = glusterd_check_generate_start_nfs ();
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

                        if (restart_flag) {
                                if (glusterd_restart_brick_servers (volinfo)) {
                                        ret = -1;
                                        goto out;
                                }
                        }

                        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                        if (ret)
                                goto out;

                        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                                ret = glusterd_check_generate_start_nfs ();
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
glusterd_op_remove_brick (dict_t *dict)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        char                                    *brick = NULL;
        int32_t                                 count = 0;
        int32_t                                 i = 1;
        char                                    key[256] = {0,};

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


        while ( i <= count) {
                snprintf (key, 256, "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to get %s", key);
                        goto out;
                }

                ret = glusterd_op_perform_remove_brick (volinfo, brick);
                if (ret)
                        goto out;
                i++;
        }

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret)
                goto out;

        volinfo->defrag_status = 0;

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);

        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_check_generate_start_nfs ();

out:
        return ret;
}


static int
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

static int
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

        ret = glusterd_check_generate_start_nfs ();

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d ", ret);
        return ret;
}

static int
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

static int
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


static int
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

        if (glusterd_are_all_volumes_stopped ()) {
                if (glusterd_is_nfs_started ()) {
                        ret = glusterd_nfs_server_stop ();
                        if (ret)
                                goto out;
                }
        } else {
                ret = glusterd_check_generate_start_nfs ();
        }

out:
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
                ret = glusterd_check_generate_start_nfs ();

        ret = 0;

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
glusterd_op_ac_lock (glusterd_op_sm_event_t *event, void *ctx)
{
        int                      ret = 0;
        glusterd_op_lock_ctx_t   *lock_ctx = NULL;
        int32_t                  status = 0;


        GF_ASSERT (event);
        GF_ASSERT (ctx);

        lock_ctx = (glusterd_op_lock_ctx_t *)ctx;

        status = glusterd_lock (lock_ctx->uuid);

        gf_log ("", GF_LOG_DEBUG, "Lock Returned %d", status);

        ret = glusterd_op_lock_send_resp (lock_ctx->req, status);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

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

        ret = glusterd_op_unlock_send_resp (lock_ctx->req, ret);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

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
glusterd_op_build_payload (glusterd_op_t op, dict_t **req)
{
        int                     ret = -1;
        void                    *ctx = NULL;
        dict_t                  *req_dict = NULL;

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);
        GF_ASSERT (req);

        req_dict = dict_new ();
        if (!req_dict)
                goto out;

        ctx = (void*)glusterd_op_get_ctx (op);
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
                        {
                                glusterd_op_delete_volume_ctx_t *ctx1 = ctx;
                                ret = dict_set_str (req_dict, "volname",
                                                    ctx1->volume_name);
                                if (ret)
                                        goto out;
                        }
                        break;

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
        int                      i = 0;
        uint32_t                pending_count = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        for ( i = GD_OP_NONE; i < GD_OP_MAX; i++) {
                if (opinfo.pending_op[i])
                        break;
        }

        if (GD_OP_MAX == i) {
                //No pending ops, inject stage_acc
                ret = glusterd_op_sm_inject_event
                        (GD_OP_EVENT_STAGE_ACC, NULL);

                return ret;
        }

        glusterd_op_clear_pending_op (i);

        ret = glusterd_op_build_payload (i, &dict);
        if (ret)
                goto out;

        /* rsp_dict NULL from source */
        ret = glusterd_op_stage_validate (i, dict, &op_errstr, NULL);
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


        priv->timer = gf_timer_call_after (THIS->ctx, timeout,
                                           glusterd_do_replace_brick,
                                           (void *) dict);

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
        int                      i = 0;
        uint32_t                 pending_count = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        for ( i = GD_OP_NONE; i < GD_OP_MAX; i++) {
                if (opinfo.commit_op[i])
                        break;
        }

        if (GD_OP_MAX == i) {
                //No pending ops, return
                return 0;
        }

        glusterd_op_clear_commit_op (i);

        ret = glusterd_op_build_payload (i, &dict);

        if (ret)
                goto out;

        ret = glusterd_op_commit_perform (i, dict, &op_errstr, NULL); //rsp_dict invalid for source
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
                op_dict = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
                if (!op_dict) {
                        ret = glusterd_op_sm_inject_all_acc ();
                        goto err;
                }

                op_dict = dict_ref (op_dict);
                ret = glusterd_op_start_rb_timer (op_dict);
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
        glusterd_brickinfo_t        *brickinfo = NULL;
        gf_boolean_t                free_errstr = _gf_false;

        GF_ASSERT (event);
        GF_ASSERT (ctx);
        ev_ctx = ctx;
        brickinfo = ev_ctx->brickinfo;
        GF_ASSERT (brickinfo);

        ret = glusterd_remove_pending_entry (&opinfo.pending_bricks, brickinfo);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "unknown response received "
                        "from %s:%s", brickinfo->hostname, brickinfo->path);
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

void
glusterd_op_brick_disconnect (void *data)
{
        glusterd_brickinfo_t *brickinfo = NULL;
        glusterd_op_brick_rsp_ctx_t *ev_ctx = NULL;

        ev_ctx = data;
        GF_ASSERT (ev_ctx);
        brickinfo = ev_ctx->brickinfo;
        GF_ASSERT (brickinfo);

	if (brickinfo->timer) {
		gf_timer_call_cancel (THIS->ctx, brickinfo->timer);
		brickinfo->timer = NULL;
                gf_log ("", GF_LOG_DEBUG,
                        "Cancelled timer thread");
	}

        glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_ACC, ev_ctx);
        glusterd_op_sm ();
}

void
glusterd_do_replace_brick (void *data)
{
        glusterd_volinfo_t     *volinfo = NULL;
        int32_t                 op      = 0;
        int32_t                 src_port = 0;
        int32_t                 dst_port = 0;
        dict_t                 *dict    = NULL;
        char                   *src_brick = NULL;
        char                   *dst_brick = NULL;
        char                   *volname   = NULL;
        glusterd_brickinfo_t   *src_brickinfo = NULL;
        glusterd_brickinfo_t   *dst_brickinfo = NULL;
	glusterd_conf_t	       *priv = NULL;

        int ret = 0;

        dict = data;

	GF_ASSERT (THIS);

	priv = THIS->private;

	if (priv->timer) {
		gf_timer_call_cancel (THIS->ctx, priv->timer);
		priv->timer = NULL;
                gf_log ("", GF_LOG_DEBUG,
                        "Cancelled timer thread");
	}

        gf_log ("", GF_LOG_DEBUG,
                "Replace brick operation detected");

        ret = dict_get_int32 (dict, "operation", &op);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "dict_get on operation failed");
                goto out;
        }
        ret = dict_get_str (dict, "src-brick", &src_brick);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get src brick");
                goto out;
        }

        gf_log ("", GF_LOG_DEBUG,
                "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get dst brick");
                goto out;
        }

        gf_log ("", GF_LOG_DEBUG,
                "dst brick=%s", dst_brick);

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

        ret = glusterd_volume_brickinfo_get_by_brick (src_brick, volinfo, &src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to get src-brickinfo");
                goto out;
        }

        ret = glusterd_get_rb_dst_brickinfo (volinfo, &dst_brickinfo);
        if (!dst_brickinfo) {
                gf_log ("", GF_LOG_DEBUG, "Unable to get dst-brickinfo");
                goto out;
        }

        ret = glusterd_resolve_brick (dst_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to resolve dst-brickinfo");
                goto out;
        }

        ret = dict_get_int32 (dict, "src-brick-port", &src_port);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get src-brick port");
                goto out;
        }

        ret = dict_get_int32 (dict, "dst-brick-port", &dst_port);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get dst-brick port");
        }

        dst_brickinfo->port = dst_port;
        src_brickinfo->port = src_port;

        switch (op) {
        case GF_REPLACE_OP_START:
                if (!dst_port) {
                        ret = -1;
                        goto out;
                }

                ret = rb_do_operation_start (volinfo, src_brickinfo, dst_brickinfo);
                if (ret) {
                        glusterd_set_rb_status (volinfo, GF_RB_STATUS_NONE);
                        goto out;
                }
                break;
        case GF_REPLACE_OP_PAUSE:
        case GF_REPLACE_OP_ABORT:
        case GF_REPLACE_OP_COMMIT:
        case GF_REPLACE_OP_COMMIT_FORCE:
        case GF_REPLACE_OP_STATUS:
                break;
        default:
                ret = -1;
                goto out;
        }

out:
        if (ret)
                ret = glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT, NULL);
        else
                ret = glusterd_op_sm_inject_event (GD_OP_EVENT_COMMIT_ACC, NULL);

//        if (dict)
//                dict_unref (dict);

        glusterd_op_sm ();
}



static int
glusterd_op_ac_rcvd_commit_op_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        glusterd_conf_t        *priv              = NULL;
        dict_t                 *dict              = NULL;
        int                     ret               = 0;
        gf_boolean_t            commit_ack_inject = _gf_false;

        priv = THIS->private;
        GF_ASSERT (event);

        if (opinfo.pending_count > 0)
                opinfo.pending_count--;

        if (opinfo.pending_count > 0)
                goto out;

        dict = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
        if (dict) {
                ret = glusterd_op_start_rb_timer (dict);
                if (ret)
                        goto out;
                commit_ack_inject = _gf_false;
	        goto out;
        }

	commit_ack_inject = _gf_true;
out:
        if (commit_ack_inject) {
                if (ret)
                        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT, NULL);
                else
                        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_COMMIT_ACC, NULL);
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
glusterd_op_set_ctx (glusterd_op_t op, void *ctx)
{

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        opinfo.op_ctx[op] = ctx;

        return 0;

}

int32_t
glusterd_op_reset_ctx (glusterd_op_t op)
{

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        glusterd_op_set_ctx (op, NULL);

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
        int32_t                 cli_op = 0;
        rpcsvc_request_t        *req = NULL;
        void                    *ctx = NULL;
        gf_boolean_t            ctx_free = _gf_false;
        char                    *op_errstr = NULL;


        priv = THIS->private;
        GF_ASSERT (priv);

        ret = glusterd_unlock (priv->uuid);

        if (ret) {
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Unable to clear local lock, ret: %d", ret);
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Cleared local lock");

        op_ret = opinfo.op_ret;
        op_errno = opinfo.op_errno;
        cli_op = opinfo.cli_op;
        req = opinfo.req;
        if (opinfo.op_errstr)
                op_errstr = opinfo.op_errstr;


        opinfo.op_ret = 0;
        opinfo.op_errno = 0;

        op = glusterd_op_get_op ();

        if (op != -1) {
                glusterd_op_clear_pending_op (op);
                glusterd_op_clear_commit_op (op);
                glusterd_op_clear_op (op);
                ctx = glusterd_op_get_ctx (op);
                ctx_free = glusterd_op_get_ctx_free (op);
                glusterd_op_reset_ctx (op);
                glusterd_op_clear_ctx_free (op);
                glusterd_op_clear_errstr ();
        }

out:
        pthread_mutex_unlock (&opinfo.lock);
        ret = glusterd_op_send_cli_response (cli_op, op_ret,
                                             op_errno, req, ctx, op_errstr);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Responding to cli failed, ret: %d",
                        ret);
                //Ignore this error, else state machine blocks
                ret = 0;
        }

        if (ctx_free && ctx && (op != -1))
                glusterd_op_free_ctx (op, ctx, ctx_free);
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
glusterd_op_stage_quota (dict_t *dict, char **op_errstr)
{
        int                ret           = 0;
        char              *volname       = NULL;
        gf_boolean_t       exists        = _gf_false;

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        if (!exists) {
                gf_log ("", GF_LOG_ERROR, "Volume with name: %s "
                                "does not exist",
                                volname);
                *op_errstr = gf_strdup ("Invalid volume name");
                ret = -1;
                goto out;
        }

out:
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
        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        switch (op) {
        case GD_OP_PROFILE_VOLUME:
                return _gf_true;
        default:
                return _gf_false;
        }
        return _gf_false;
}

static dict_t*
glusterd_op_init_commit_rsp_dict (glusterd_op_t op)
{
        dict_t                  *rsp_dict = NULL;
        dict_t                  *op_ctx   = NULL;

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        if (glusterd_need_brick_op (op)) {
                op_ctx = glusterd_op_get_ctx (op);
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

        glusterd_op_fini_ctx (req_ctx->op);
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

        op_ctx = glusterd_op_get_ctx (req_ctx->op);

        ret = glusterd_op_commit_send_resp (req_ctx->req, req_ctx->op,
                                            opinfo.op_ret, opinfo.op_errstr,
                                            op_ctx);

        glusterd_op_fini_ctx (req_ctx->op);
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
                        ret = glusterd_op_stage_remove_brick (dict);
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
                        ret = glusterd_op_remove_brick (dict);
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
        int                                     all_bricks_down = 0;

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
                all_bricks_down = 1;
                list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                        if (glusterd_is_brick_started (brickinfo)) {
                                all_bricks_down = 0;
                                pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                                          gf_gld_mt_pending_node_t);
                                if (!pending_node) {
                                        ret = -1;
                                        goto out;
                                } else {
                                        pending_node->node = brickinfo;
                                        list_add_tail (&pending_node->list,
                                                       &opinfo.pending_bricks);
                                        pending_node = NULL;
                                }
                        }
                }
                break;

        case GF_CLI_STATS_TOP:
                all_bricks_down = 1;
                ret = dict_get_str (dict, "brick", &brick);
                if (!ret) {
                        ret = glusterd_volume_brickinfo_get_by_brick (brick,
                                                        volinfo, &brickinfo); 
                        if (ret)
                                goto out;

                        if (!glusterd_is_brick_started (brickinfo)) {
                                ret = -1;
                                goto out;
                        } else {
                                all_bricks_down = 0;
                        }

                        pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                                  gf_gld_mt_pending_node_t);
                        if (!pending_node) {
                                ret = -1;
                                goto out;
                        } else {
                                pending_node->node = brickinfo;
                                list_add_tail (&pending_node->list,
                                               &opinfo.pending_bricks);
                                pending_node = NULL;
                                goto out;
                        }
                }
                ret = 0;
                list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                        if (glusterd_is_brick_started (brickinfo)) {
                                all_bricks_down = 0;
                                pending_node = GF_CALLOC (1, sizeof (*pending_node),
                                                          gf_gld_mt_pending_node_t);
                                if (!pending_node) {
                                        ret = -1;
                                        goto out;
                                } else {
                                        pending_node->node = brickinfo;
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
        if (all_bricks_down) {
                ret = -1;
                *op_errstr = gf_strdup ("Cannot reach bricks. Bricks are down");
        }
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

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
                ret = glusterd_op_build_payload (op, &req_ctx->dict);
                if (ret)//TODO:what to do??
                        goto out;
        }

        proc = &priv->gfs_mgmt->proctable[GD_MGMT_BRICK_OP];
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
        glusterd_brickinfo_t        *brickinfo = NULL;
        char                        *op_errstr = NULL;
        glusterd_op_t               op = GD_OP_NONE;
        dict_t                      *op_ctx = NULL;
        gf_boolean_t                free_errstr = _gf_true;
        glusterd_req_ctx_t          *req_ctx = NULL;

        GF_ASSERT (event);
        GF_ASSERT (ctx);
        ev_ctx = ctx;

        req_ctx = ev_ctx->commit_ctx;
        GF_ASSERT (req_ctx);

        brickinfo = ev_ctx->brickinfo;
        GF_ASSERT (brickinfo);

        ret = glusterd_remove_pending_entry (&opinfo.pending_bricks, brickinfo);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "unknown response received "
                        "from %s:%s", brickinfo->hostname, brickinfo->path);
                ret = -1;
                free_errstr = _gf_true;
                goto out;
        }

        if (opinfo.brick_pending_count > 0)
                opinfo.brick_pending_count--;
        op = req_ctx->op;
        op_ctx = glusterd_op_get_ctx (op);

        glusterd_handle_brick_rsp (brickinfo, op, ev_ctx->rsp_dict,
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
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_lock_sent [] = {
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_rcvd_lock_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_send_stage_op}, //EVENT_ALL_ACC
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock}, //EVENT_RCVD_RJT
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_ALL_ACK
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_locked [] = {
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_LOCK
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
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_stage_op_sent [] = {
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_rcvd_stage_op_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_BRICK_OP_SENT,  glusterd_op_ac_send_brick_op}, //EVENT_ALL_ACC
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_send_brick_op}, //EVENT_STAGE_ACC
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_STAGE_OP_FAILED,   glusterd_op_ac_stage_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_ALL_ACK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_stage_op_failed [] = {
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_stage_op_failed}, //EVENT_RCVD_ACC
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_stage_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock}, //EVENT_ALL_ACK
        {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_staged [] = {
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_STAGED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_LOCK
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
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_brick_op_sent [] = {
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_rcvd_brick_op_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_BRICK_OP_FAILED,   glusterd_op_ac_brick_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_BRICK_OP
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_send_commit_op}, //EVENT_ALL_ACK
        {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_brick_op_failed [] = {
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_brick_op_failed}, //EVENT_RCVD_ACC
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_brick_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_BRICK_OP
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock}, //EVENT_ALL_ACK
        {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_brick_committed [] = {
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none}, //EVENT_LOCK
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
        {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_brick_commit_failed [] = {
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none}, //EVENT_LOCK
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
        {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_commit_op_failed [] = {
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_commit_op_failed}, //EVENT_RCVD_ACC
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_commit_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock}, //EVENT_ALL_ACK
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_commit_op_sent [] = {
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_rcvd_commit_op_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_UNLOCK_SENT,    glusterd_op_ac_send_unlock}, //EVENT_ALL_ACC
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_UNLOCK_SENT,    glusterd_op_ac_send_unlock}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_commit_op_failed}, //EVENT_RCVD_RJT
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT,        glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock}, //EVENT_START_UNLOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_ALL_ACK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_committed [] = {
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_LOCK
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
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_unlock_sent [] = {
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_rcvd_unlock_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlocked_all}, //EVENT_ALL_ACC
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_RCVD_RJT
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT,     glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_ALL_ACK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_MAX
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
        glusterd_op_state_brick_commit_failed
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

        opinfo.op[op] = 1;
        opinfo.pending_op[op] = 1;
        opinfo.commit_op[op] = 1;

        return 0;

}

int32_t
glusterd_op_get_op ()
{

        int     i = 0;
        int32_t ret = 0;

        for ( i = 0; i < GD_OP_MAX; i++) {
                if (opinfo.op[i])
                        break;
        }

        if ( i == GD_OP_MAX)
                ret = -1;
        else
                ret = i;

        return ret;

}


int32_t
glusterd_op_set_cli_op (glusterd_op_t op)
{

        int32_t         ret = 0;

        ret = pthread_mutex_trylock (&opinfo.lock);

        if (ret)
                goto out;

        opinfo.cli_op = op;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_op_set_req (rpcsvc_request_t *req)
{

        GF_ASSERT (req);
        opinfo.req = req;
        return 0;
}

int32_t
glusterd_op_clear_pending_op (glusterd_op_t op)
{

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        opinfo.pending_op[op] = 0;

        return 0;

}

int32_t
glusterd_op_clear_commit_op (glusterd_op_t op)
{

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        opinfo.commit_op[op] = 0;

        return 0;

}

int32_t
glusterd_op_clear_op (glusterd_op_t op)
{

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        opinfo.op[op] = 0;

        return 0;

}

int32_t
glusterd_op_init_ctx (glusterd_op_t op)
{
        int     ret = 0;
        dict_t *dict = NULL;

        if (_gf_false == glusterd_need_brick_op (op)) {
                gf_log ("", GF_LOG_DEBUG, "Received op: %d, returning", op);
                goto out;
        }
        dict = dict_new ();
        if (dict == NULL) {
                ret = -1;
                goto out;
        }
        ret = glusterd_op_set_ctx (op, dict);
        if (ret)
                goto out;
        ret = glusterd_op_set_ctx_free (op, _gf_true);
        if (ret)
                goto out;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}



int32_t
glusterd_op_fini_ctx (glusterd_op_t op)
{
        dict_t *dict = NULL;

        if (glusterd_op_get_ctx_free (op)) {
                dict = glusterd_op_get_ctx (op);
                if (dict)
                        dict_unref (dict);
        }
        glusterd_op_reset_ctx (op);
        return 0;
}



int32_t
glusterd_op_free_ctx (glusterd_op_t op, void *ctx, gf_boolean_t ctx_free)
{

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        if (ctx && ctx_free) {
                switch (op) {
                case GD_OP_CREATE_VOLUME:
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
                        dict_unref (ctx);
                        break;
                case GD_OP_DELETE_VOLUME:
                        GF_FREE (ctx);
                        break;
                default:
                        GF_ASSERT (0);
                        break;
                }
        }
        return 0;

}

void *
glusterd_op_get_ctx (glusterd_op_t op)
{
        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        return opinfo.op_ctx[op];

}

int32_t
glusterd_op_set_ctx_free (glusterd_op_t op, gf_boolean_t ctx_free)
{

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        opinfo.ctx_free[op] = ctx_free;

        return 0;

}

int32_t
glusterd_op_clear_ctx_free (glusterd_op_t op)
{

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        opinfo.ctx_free[op] = _gf_false;

        return 0;

}

gf_boolean_t
glusterd_op_get_ctx_free (glusterd_op_t op)
{
        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        return opinfo.ctx_free[op];

}

int
glusterd_op_sm_init ()
{
        INIT_LIST_HEAD (&gd_op_sm_queue);
        pthread_mutex_init (&gd_op_sm_lock, NULL);
        return 0;
}

int32_t
glusterd_opinfo_unlock(){
        return (pthread_mutex_unlock(&opinfo.lock));
}
int32_t
glusterd_volume_stats_write_perf (char *brick_path, int32_t blk_size,
                int32_t blk_count, double *throughput, double *time)
{
        int32_t          fd = -1;
        int32_t          input_fd = -1;
        char             export_path[1024];
        char            *buf = NULL;
        int32_t          iter = 0;
        int32_t          ret = -1;
        int64_t          total_blks = 0;
        struct timeval   begin, end = {0, };


        GF_VALIDATE_OR_GOTO ("stripe", brick_path, out);

        snprintf (export_path, sizeof(export_path), "%s/%s",
                 brick_path, ".gf_tmp_stats_perf");
        fd = open (export_path, O_CREAT|O_RDWR, S_IRWXU);
        if (fd == -1)
                return errno;
        buf = GF_MALLOC (blk_size * sizeof(*buf), gf_common_mt_char);

        if (!buf)
                return ret;

        input_fd = open("/dev/zero", O_RDONLY);
        if (input_fd == -1)
                return errno;
        gettimeofday (&begin, NULL);
        for (iter = 0; iter < blk_count; iter++) {
                ret = read (input_fd, buf, blk_size);
                if (ret != blk_size) {
                        ret = -1;
                        goto out;
                }
                ret = write (fd, buf, blk_size);
                if (ret != blk_size) {
                        ret = -1;
                        goto out;
                }
                total_blks += ret;
        }
        ret = 0;
        if (total_blks != (blk_size * blk_count)) {
                gf_log ("glusterd", GF_LOG_WARNING, "Errors in write");
                ret = -1;
                goto out;
        }

        gettimeofday (&end, NULL);
        *time = (end.tv_sec - begin.tv_sec) * 1e6
                + (end.tv_usec - begin.tv_usec);

        *throughput = total_blks / *time;
        gf_log ("glusterd", GF_LOG_INFO, "Throughput %.2f MBps time %.2f secs bytes "
                "written %"PRId64, *throughput, *time / 1e6, total_blks);
out:
        if (fd >= 0)
                close (fd);
        if (input_fd >= 0)
                close (input_fd);
        if (buf)
                GF_FREE (buf);
        unlink (export_path);
        return ret;
}

int32_t
glusterd_volume_stats_read_perf (char *brick_path, int32_t blk_size,
                int32_t blk_count, double *throughput, double *time)
{
        int32_t          fd = -1;
        int32_t          output_fd = -1;
        int32_t          input_fd = -1;
        char             export_path[1024];
        char            *buf = NULL;
        int32_t          iter = 0;
        int32_t          ret = -1;
        int64_t          total_blks = 0;
        struct timeval   begin, end = {0, };


        GF_VALIDATE_OR_GOTO ("glusterd", brick_path, out);

        snprintf (export_path, sizeof(export_path), "%s/%s",
                 brick_path, ".gf_tmp_stats_perf");
        fd = open (export_path, O_CREAT|O_RDWR, S_IRWXU);
        if (fd == -1)
                return errno;
        buf = GF_MALLOC (blk_size * sizeof(*buf), gf_common_mt_char);

        if (!buf)
                return ret;

        output_fd = open("/dev/null", O_RDWR);
        if (output_fd == -1)
                return errno;
        input_fd = open("/dev/zero", O_RDONLY);
        if (input_fd == -1)
                return errno;
        for (iter = 0; iter < blk_count; iter++) {
                ret = read (input_fd, buf, blk_size);
                if (ret != blk_size) {
                        ret = -1;
                        goto out;
                }
                ret = write (fd, buf, blk_size);
                if (ret != blk_size) {
                        ret = -1;
                        goto out;
                }
        }


        lseek (fd, 0L, 0);
        gettimeofday (&begin, NULL);
        for (iter = 0; iter < blk_count; iter++) {
                ret = read (fd, buf, blk_size);
                if (ret != blk_size) {
                        ret = -1;
                        goto out;
                }
                ret = write (output_fd, buf, blk_size);
                if (ret != blk_size) {
                        ret = -1;
                        goto out;
                }
                total_blks += ret;
        }
        ret = 0;
        if (total_blks != (blk_size * blk_count)) {
                gf_log ("glusterd", GF_LOG_WARNING, "Errors in write");
                ret = -1;
                goto out;
        }

        gettimeofday (&end, NULL);
        *time = (end.tv_sec - begin.tv_sec) * 1e6
                + (end.tv_usec - begin.tv_usec);

        *throughput = total_blks / *time;
        gf_log ("glusterd", GF_LOG_INFO, "Throughput %.2f MBps time %.2f secs bytes "
                "read %"PRId64, *throughput, *time / 1e6, total_blks);
out:
        if (fd >= 0)
                close (fd);
        if (input_fd >= 0)
                close (input_fd);
        if (output_fd >= 0)
                close (output_fd);
        if (buf)
                GF_FREE (buf);
        unlink (export_path);
        return ret;
}
