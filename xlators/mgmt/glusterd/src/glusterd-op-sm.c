/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is GF_FREE software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
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

#include <sys/types.h>
#include <signal.h>

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
        "Commited",
        "Unlock sent",
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
        "GD_OP_EVENT_INVALID"
};

static char *gsync_opname[] = {
        "gluster-command",
        "gluster-log-file",
        "gluster-log-level",
        "log-file",
        "log-level",
        "remote-gsyncd",
        "ssh-command",
        "rsync-command",
        "timeout",
        "sync-jobs",
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
glusterd_destroy_stage_ctx (glusterd_op_stage_ctx_t *ctx)
{
        if (!ctx)
                return;

        if (ctx->dict)
                dict_unref (ctx->dict);

        GF_FREE (ctx);
}

void
glusterd_destroy_commit_ctx (glusterd_op_commit_ctx_t *ctx)
{
        if (!ctx)
                return;

        if (ctx->dict)
                dict_unref (ctx->dict);

        GF_FREE (ctx);
}

void
glusterd_set_volume_status (glusterd_volinfo_t  *volinfo,
                            glusterd_volume_status status)
{
        GF_ASSERT (volinfo);
        volinfo->status = status;
}

static int
glusterd_is_volume_started (glusterd_volinfo_t  *volinfo)
{
        GF_ASSERT (volinfo);
        return (!(volinfo->status == GLUSTERD_STATUS_STARTED));
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
                snprintf (msg, 2048, "Volume %s does not exist", volname);
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
                                                          0777, op_errstr);
                        if (ret)
                                goto out;
                }

                if (!(flags & GF_CLI_FLAG_OP_FORCE)) {
                        ret = glusterd_is_volume_started (volinfo);
                        if (!ret) {
                                snprintf (msg, 2048, "Volume %s already started",
                                          volname);
                                gf_log ("glusterd", GF_LOG_ERROR,
                                        "%s", msg);
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
                ret = glusterd_is_volume_started (volinfo);

                if (ret) {
                        snprintf (msg, sizeof(msg), "Volume %s "
                                  "is not in the started state", volname);
                        gf_log ("", GF_LOG_ERROR, "Volume %s "
                                "has not been started", volname);
                        *op_errstr = gf_strdup (msg);
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

        ret = glusterd_is_volume_started (volinfo);

        if (!ret) {
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
                        gf_log ("", GF_LOG_ERROR, "Replace brick is not "
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
                ret = glusterd_brick_create_path (host, path, 0777, op_errstr);
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

        ret = glusterd_brick_create_path (hostname, path, 0777, op_errstr);
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

        ret = glusterd_is_volume_started (volinfo);

        if (ret) {
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

        ret = dict_get_int32 (dict, "count", &dict_count);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Count(dict),not set in Volume-Set");
                goto out;
        }

        if ( dict_count == 1 ) {
                if (dict_get (dict, "history" )) {
                        ret = 0;
                        goto out;
                }

                gf_log ("", GF_LOG_ERROR, "No options received ");
                *op_errstr = gf_strdup ("Options not specified");
                ret = -1;
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

                if (key_fixed) {
                        GF_FREE (key_fixed);
                        key_fixed = NULL;
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

        glusterd_delete_volfile (volinfo, brickinfo);
        glusterd_store_delete_brick (volinfo, brickinfo);
        glusterd_brickinfo_delete (brickinfo);
        volinfo->brick_count--;

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

        ret = glusterd_create_volfiles (volinfo);
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

        ret = glusterd_create_volfiles (volinfo);
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

char *
volname_from_master (char *master)
{
        if (master == NULL)
                return NULL;

        return gf_strdup (master+1);
}

int
gsync_get_pid_file (char *pidfile, char *master, char *slave)
{
        int     ret      = -1;
        int     i        = 0;
        char    str[256] = {0, };

        GF_VALIDATE_OR_GOTO ("gsync", pidfile, out);
        GF_VALIDATE_OR_GOTO ("gsync", master, out);
        GF_VALIDATE_OR_GOTO ("gsync", slave, out);

        i = 0;
        //change '/' to '-'
        while (slave[i]) {
                (slave[i] == '/') ? (str[i] = '-') : (str[i] = slave[i]);
                i++;
        }

        ret = snprintf (pidfile, 1024, "/etc/glusterd/gsync/%s/%s.pid",
                        master, str);
        if (ret <= 0) {
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        return ret;
}

/* status: return 0 when gsync is running
 * return -1 when not running
 */
int
gsync_status (char *master, char *slave, int *status)
{
        int     ret             = -1;
        char    pidfile[1024]   = {0,};
        FILE    *file           = NULL;

        GF_VALIDATE_OR_GOTO ("gsync", master, out);
        GF_VALIDATE_OR_GOTO ("gsync", slave, out);
        GF_VALIDATE_OR_GOTO ("gsync", status, out);

        ret = gsync_get_pid_file (pidfile, master, slave);
        if (ret == -1)
                goto out;

        file = fopen (pidfile, "r+");
        if (file) {
                ret = lockf (fileno (file), F_TEST, 0);
                if (ret == 0)
                        *status = -1;
                else
                        *status = 0;
        } else
                *status = -1;
        ret = 0;
out:
        return ret;
}

int
gsync_validate_config_type (int32_t config_type)
{
        switch (config_type) {
            case GF_GSYNC_OPTION_TYPE_CONFIG_SET:
            case GF_GSYNC_OPTION_TYPE_CONFIG_DEL:
            case GF_GSYNC_OPTION_TYPE_CONFIG_GET:
            case GF_GSYNC_OPTION_TYPE_CONFIG_GET_ALL:return 0;
            default: return -1;
        }
        return 0;
}

int
gsync_validate_config_option (dict_t *dict, int32_t config_type,
                              char **op_errstr)
{
        int     ret       = -1;
        int     i         = 0;
        char    *op_name  = NULL;

        if (config_type == GF_GSYNC_OPTION_TYPE_CONFIG_GET_ALL)
                return 0;

        ret = dict_get_str (dict, "op_name", &op_name);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "option not specified");
                *op_errstr = gf_strdup ("Please specify the option");
                goto out;
        }

        i = 0;
        while (gsync_opname[i] != NULL) {
                if (strcmp (gsync_opname[i], op_name) == 0) {
                        ret = 0;
                        goto out;
                }
                i++;
        }

        gf_log ("", GF_LOG_WARNING, "Invalid option");
        *op_errstr = gf_strdup ("Invalid option");

        ret = -1;

out:
        return ret;
}

int
gsync_verify_config_options (dict_t *dict, char **op_errstr)
{
        int     ret     = -1;
        int     config_type = 0;

        GF_VALIDATE_OR_GOTO ("gsync", dict, out);
        GF_VALIDATE_OR_GOTO ("gsync", op_errstr, out);

        ret = dict_get_int32 (dict, "config_type", &config_type);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "config type is missing");
                *op_errstr = gf_strdup ("config-type missing");
                goto out;
        }

        ret = gsync_validate_config_type (config_type);
        if (ret == -1) {
                gf_log ("", GF_LOG_WARNING, "Invalid config type");
                *op_errstr = gf_strdup ("Invalid config type");
                goto out;
        }

        ret = gsync_validate_config_option (dict, config_type, op_errstr);
        if (ret < 0)
                goto out;

        ret = 0;
out:
        return ret;
}

static int
glusterd_op_stage_gsync_set (dict_t *dict, char **op_errstr)
{
        int             ret     = 0;
        int             type    = 0;
        int             status  = 0;
        dict_t          *ctx    = NULL;
        char            *volname = NULL;
        char            *master  = NULL;
        char            *slave   = NULL;
        gf_boolean_t    exists   = _gf_false;
        glusterd_volinfo_t *volinfo = NULL;

        ctx = glusterd_op_get_ctx (GD_OP_GSYNC_SET);
        if (!ctx) {
                gf_log ("gsync", GF_LOG_DEBUG, "gsync command doesn't "
                        "correspond to this glusterd");
                goto out;
        }

        ret = dict_get_str (dict, "master", &master);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "master not found");
                *op_errstr = gf_strdup ("master not found");
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "slave not found");
                *op_errstr = gf_strdup ("slave not found");
                ret = -1;
                goto out;
        }

        volname = volname_from_master (master);
        if (volname == NULL) {
                gf_log ("", GF_LOG_WARNING, "volname couldn't be found");
                *op_errstr = gf_strdup ("volname not found");
                ret = -1;
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        if (!exists) {
                gf_log ("", GF_LOG_WARNING, "volname doesnot exist");
                *op_errstr = gf_strdup ("volname doesnot exist");
                ret = -1;
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "command type not found");
                *op_errstr = gf_strdup ("command unsuccessful");
                ret = -1;
                goto out;
        }

        if (type == GF_GSYNC_OPTION_TYPE_START) {
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                         gf_log ("", GF_LOG_WARNING, "volinfo not found "
                                 "for %s", volname);
                         *op_errstr = gf_strdup ("command unsuccessful");
                         ret = -1;
                         goto out;
                }
                if (GLUSTERD_STATUS_STARTED != volinfo->status) {
                        gf_log ("", GF_LOG_WARNING, "%s volume not started",
                                volname);
                        *op_errstr = gf_strdup ("please start the volume");
                        ret = -1;
                        goto out;
                }
                //check if the gsync is already started
                ret = gsync_status (master, slave, &status);
                if (ret == 0 && status == 0) {
                        gf_log ("", GF_LOG_WARNING, "gsync already started");
                        *op_errstr = gf_strdup ("gsync already started");
                        ret = -1;
                        goto out;
                } else if (ret == -1) {
                        gf_log ("", GF_LOG_WARNING, "gsync start validation "
                                " failed");
                        *op_errstr = gf_strdup ("command to failed, please "
                                                "check the log file");
                        goto out;
                }
                ret = 0;
                goto out;
        }

        if (type == GF_GSYNC_OPTION_TYPE_STOP) {
                ret = gsync_status (master, slave, &status);
                if (ret == 0 && status == -1) {
                        gf_log ("", GF_LOG_WARNING, "gsync not running");
                        *op_errstr = gf_strdup ("gsync not running");
                        ret = -1;
                        goto out;
                }  else if (ret == -1) {
                        gf_log ("", GF_LOG_WARNING, "gsync stop validation "
                                " failed");
                        *op_errstr = gf_strdup ("command to failed, please "
                                                "check the log file");
                        goto out;
                }
                ret = 0;
                goto out;
        }

        if (type == GF_GSYNC_OPTION_TYPE_CONFIGURE) {
                ret = gsync_verify_config_options (dict, op_errstr);
                if (ret < 0)
                        goto out;
        }

        ret = 0;
out:
        if (volname)
                GF_FREE (volname);

        return ret;
}

static int
glusterd_op_create_volume (dict_t *dict, char **op_errstr)
{
        int                   ret        = 0;
        char                 *volname    = NULL;
        glusterd_conf_t      *priv       = NULL;
        glusterd_volinfo_t   *volinfo    = NULL;
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
        } else if (strcasecmp (trans_type, "tcp") == 0) {
                volinfo->transport_type = GF_TRANSPORT_TCP;
        } else {
                volinfo->transport_type = GF_TRANSPORT_BOTH_TCP_RDMA;
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
        list_add_tail (&volinfo->vol_list, &priv->volumes);
        volinfo->version++;
        volinfo->defrag_status = 0;

        ret = glusterd_store_create_volume (volinfo);

        if (ret)
                goto out;

        ret = glusterd_create_volfiles (volinfo);

        ret = glusterd_volume_compute_cksum (volinfo);
        if (ret)
                goto out;

out:
        if (free_ptr)
                GF_FREE(free_ptr);

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

        volinfo->version++;
        volinfo->defrag_status = 0;

        ret = glusterd_store_update_volume (volinfo);
        if (ret)
                goto out;

        ret = glusterd_volume_compute_cksum (volinfo);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_check_generate_start_nfs (volinfo);

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
        char                cmd_str[8192]  = {0,};
        int                 ret            = -1;
        int32_t             port           = 0;

        priv = THIS->private;

        port = pmap_registry_alloc (THIS);
        brickinfo->port = port;

        GF_ASSERT (port);

        snprintf (cmd_str, 8192,
                  "%s/sbin/glusterfs -f %s/vols/%s/%s -p %s/vols/%s/%s "
                  "--xlator-option src-server.listen-port=%d",
                  GFS_PREFIX, priv->workdir, volinfo->volname,
                  RB_DSTBRICKVOL_FILENAME,
                  priv->workdir, volinfo->volname,
                  RB_DSTBRICK_PIDFILE,
                  port);

        ret = gf_system (cmd_str);
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
        struct stat         buf;
        int                 ret            = -1;

        priv = THIS->private;

        snprintf (cmd_str, 4096,
                  "%s/sbin/glusterfs -f %s/vols/%s/%s %s/vols/%s/%s",
                  GFS_PREFIX, priv->workdir, volinfo->volname,
                  RB_CLIENTVOL_FILENAME,
                  priv->workdir, volinfo->volname,
                  RB_CLIENT_MOUNTPOINT);

        ret = gf_system (cmd_str);
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
        "end-volume\n"
        "volume mnt-wb\n"
        " type performance/write-behind\n"
        " subvolumes mnt-client\n"
        "end-volume\n";

static int
rb_generate_client_volfile (glusterd_volinfo_t *volinfo,
                            glusterd_brickinfo_t *src_brickinfo)
{
        glusterd_conf_t    *priv = NULL;
        FILE *file = NULL;
        char filename[PATH_MAX];
        int ret = -1;

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

        fprintf (file, client_volfile_str, src_brickinfo->hostname,
                 src_brickinfo->path, src_brickinfo->port);

        fclose (file);

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
        " option transport-type tcp\n"
        " subvolumes %s\n"
        "end-volume\n";

static int
rb_generate_dst_brick_volfile (glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t *dst_brickinfo)
{
        glusterd_conf_t    *priv = NULL;
        FILE *file = NULL;
        char filename[PATH_MAX];
        int ret = -1;

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

        fprintf (file, dst_brick_volfile_str, dst_brickinfo->path,
                 dst_brickinfo->path, dst_brickinfo->path,
                 dst_brickinfo->path);

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
rb_destroy_maintainence_client (glusterd_volinfo_t *volinfo,
                                glusterd_brickinfo_t *src_brickinfo)
{
        glusterd_conf_t  *priv                        = NULL;
        char              cmd_str[8192]               = {0,};
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
                        "stat failed. Cannot destroy maintainence "
                        "client");
                goto out;
        }

        snprintf (cmd_str, 8192, "/bin/umount -f %s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_CLIENT_MOUNTPOINT);

        ret = gf_system (cmd_str);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "umount failed on maintainence client");
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
rb_spawn_maintainence_client (glusterd_volinfo_t *volinfo,
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

        ret = rb_spawn_maintainence_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not spawn maintainence "
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

        ret = rb_destroy_maintainence_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Failed to destroy maintainence "
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

        gf_log ("", GF_LOG_NORMAL,
                "replace-brick send pause xattr");

        ret = rb_spawn_maintainence_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not spawn maintainence "
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

        ret = rb_destroy_maintainence_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Failed to destroy maintainence "
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
rb_do_operation_abort (glusterd_volinfo_t *volinfo,
                       glusterd_brickinfo_t *src_brickinfo,
                       glusterd_brickinfo_t *dst_brickinfo)
{
        int ret = -1;

        gf_log ("", GF_LOG_DEBUG,
                "replace-brick sending abort xattr");

        ret = rb_spawn_maintainence_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not spawn maintainence "
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

        ret = rb_destroy_maintainence_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Failed to destroy maintainence "
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

        ret = lgetxattr (mount_point_path, xattr_key,
                         value,
                         8192);

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
        char            status[2048] = {0,};
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
                ret = rb_spawn_maintainence_client (volinfo, src_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not spawn maintainence "
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
                ret = rb_destroy_maintainence_client (volinfo, src_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to destroy maintainence "
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
                gf_log ("", GF_LOG_NORMAL,
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
                gf_log ("", GF_LOG_NORMAL,
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
                        gf_log ("", GF_LOG_NORMAL,
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
			gf_log ("", GF_LOG_NORMAL,
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
                ret = dict_set_int32 (volinfo->dict, "enable-pump", 0);
                gf_log ("", GF_LOG_DEBUG,
                        "Received commit - will be adding dst brick and "
                        "removing src brick");

                if (!glusterd_is_local_addr (dst_brickinfo->hostname) &&
                    replace_op != GF_REPLACE_OP_COMMIT_FORCE) {
                        gf_log ("", GF_LOG_NORMAL,
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
		        (void) glusterd_check_generate_start_nfs (volinfo);
			goto out;
		}

		volinfo->version++;
		volinfo->defrag_status = 0;

		ret = glusterd_check_generate_start_nfs (volinfo);
		if (ret) {
                        gf_log ("", GF_LOG_CRITICAL,
                                "Failed to generate nfs volume file");
		}

		ret = glusterd_store_update_volume (volinfo);

		if (ret)
			goto out;

		ret = glusterd_volume_compute_cksum (volinfo);
		if (ret)
			goto out;

		ret = glusterd_fetchspec_notify (THIS);
                glusterd_set_rb_status (volinfo, GF_RB_STATUS_NONE);
                glusterd_brickinfo_delete (volinfo->dst_brick);
                volinfo->src_brick = volinfo->dst_brick = NULL;
        }
        break;

        case GF_REPLACE_OP_PAUSE:
        {
                gf_log ("", GF_LOG_DEBUG,
                        "Recieved pause - doing nothing");
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

                ret = dict_set_int32 (volinfo->dict, "enable-pump", 0);
		if (ret) {
			gf_log ("", GF_LOG_CRITICAL, "Unable to disable pump");
		}

                if (!glusterd_is_local_addr (dst_brickinfo->hostname)) {
                        gf_log ("", GF_LOG_NORMAL,
                                "I AM THE DESTINATION HOST");
                        ret = rb_kill_destination_brick (volinfo, dst_brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_DEBUG,
                                        "Failed to kill destination brick");
                                goto out;
                        }
                }

                ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
                if (ctx) {
                        ret = rb_do_operation_abort (volinfo, src_brickinfo, dst_brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR,
                                        "Abort operation failed");
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

        if (ret)
                goto out;

out:
        return ret;
}

void
_delete_reconfig_opt (dict_t *this, char *key, data_t *value, void *data)
{

        int            exists = 0;

        exists = glusterd_check_option_exists(key, NULL);

        if (exists == 1) {
                gf_log ("", GF_LOG_DEBUG, "deleting dict with key=%s,value=%s",
                        key, value->data);
                dict_del (this, key);
        }

}

int
glusterd_options_reset (glusterd_volinfo_t *volinfo)
{
        int                      ret = 0;

        gf_log ("", GF_LOG_DEBUG, "Received volume set reset command");

        GF_ASSERT (volinfo->dict);

        dict_foreach (volinfo->dict, _delete_reconfig_opt, volinfo->dict);

        ret = glusterd_create_volfiles (volinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to create volfile for"
                        " 'volume set'");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_update_volume (volinfo);
        if (ret)
                goto out;

        ret = glusterd_volume_compute_cksum (volinfo);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_check_generate_start_nfs (volinfo);
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

        ret = glusterd_options_reset (volinfo);

out:
        gf_log ("", GF_LOG_DEBUG, "'volume reset' returning %d", ret);
        return ret;

}

int
stop_gsync (char *master, char *slave, char **op_errstr)
{
        int32_t         ret     = -1;
        int32_t         status  = 0;
        pid_t           pid     = 0;
        FILE            *file   = NULL;
        char            pidfile[1024] = {0,};
        char            buf [256] = {0,};

        ret = gsync_status (master, slave, &status);
        if (ret == 0 && status == -1) {
                gf_log ("", GF_LOG_WARNING, "gsync is not running");
                *op_errstr = gf_strdup ("gsync is not running");
                ret = -1;
                goto out;
        } else if (ret == -1) {
                gf_log ("", GF_LOG_WARNING, "gsync stop validation "
                        " failed");
                *op_errstr = gf_strdup ("command to failed, please "
                                        "check the log file");
                goto out;
        }

        //change '/' to '-'
        ret = gsync_get_pid_file (pidfile, master, slave);
        if (ret == -1) {
                ret = -1;
                gf_log ("", GF_LOG_WARNING, "failed to create the pidfile string");
                goto out;
        }

        file = fopen (pidfile, "r+");
        if (!file) {
                gf_log ("", GF_LOG_WARNING, "cannot open pid file");
                *op_errstr = gf_strdup ("stop unsuccessful");
                ret = -1;
                goto out;
        }

        ret = read (fileno(file), buf, 1024);
        if (ret > 0) {
                pid = strtol (buf, NULL, 10);
                ret = kill (pid, SIGTERM);
                if (ret) {
                        gf_log ("", GF_LOG_WARNING,
                                "failed to stop gsyncd");
                        goto out;
                }
                sleep (0.1);
                kill (pid, SIGTERM);
                unlink (pidfile);
        }
        ret = 0;

        *op_errstr = gf_strdup ("gsync stopped successfully");

out:
        return ret;
}

int
gsync_config_set (char *master, char *slave,
                  dict_t *dict, char **op_errstr)
{
        int32_t         ret     = -1;
        char            *op_name = NULL;
        char            *op_value = NULL;
        char            cmd[1024] = {0,};

        ret = dict_get_str (dict, "op_name", &op_name);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "failed to get the "
                        "option name for %s %s", master, slave);

                *op_errstr = gf_strdup ("configure command failed, "
                                        "please check the log-file\n");
                goto out;
        }

        ret = dict_get_str (dict, "op_value", &op_value);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "failed to get "
                        "the option value for %s %s",
                        master, slave);

                *op_errstr = gf_strdup ("configure command "
                                        "failed, please check "
                                        "the log-file\n");
                goto out;
        }

        ret = snprintf (cmd, 1024, GSYNCD_PREFIX "/gsyncd %s %s "
                        "--config-set %s %s", master, slave, op_name, op_value);
        if (ret <= 0) {
                gf_log ("", GF_LOG_WARNING, "failed to "
                        "construct the gsyncd command");

                *op_errstr = gf_strdup ("configure command failed, "
                                        "please check the log-file\n");
                goto out;
        }

        ret = system (cmd);
        if (ret == -1) {
                gf_log ("", GF_LOG_WARNING, "gsyncd failed to "
                        "set %s option for %s %s peer",
                        op_name, master, slave);

                *op_errstr = gf_strdup ("configure command "
                                        "failed, please check "
                                        "the log-file\n");
                goto out;
        }
        ret = 0;
        *op_errstr = gf_strdup ("config-set successful");

out:
        return ret;
}

int
gsync_config_del (char *master, char *slave,
                  dict_t *dict, char **op_errstr)
{
        int32_t         ret     = -1;
        char            *op_name = NULL;
        char            cmd[1024] = {0,};

        ret = dict_get_str (dict, "op_name", &op_name);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "failed to get "
                        "the option for %s %s", master, slave);

                *op_errstr = gf_strdup ("configure command "
                                        "failed, please check "
                                        "the log-file\n");
                goto out;
        }

        ret = snprintf (cmd, 4096, GSYNCD_PREFIX "/gsyncd %s %s "
                        "--config-del %s", master, slave, op_name);
        if (ret <= 0) {
                gf_log ("", GF_LOG_WARNING, "failed to "
                        "construct the gsyncd command");
                *op_errstr = gf_strdup ("configure command "
                                        "failed, please check "
                                        "the log-file\n");
                goto out;
        }

        ret = system (cmd);
        if (ret == -1) {
                gf_log ("", GF_LOG_WARNING, "failed to delete "
                        "%s option for %s %s peer", op_name,
                        master, slave);
                *op_errstr = gf_strdup ("configure command "
                                        "failed, please check "
                                        "the log-file\n");
                goto out;
        }
        ret = 0;
        *op_errstr = gf_strdup ("config-del successful");
out:
        return ret;
}

int
gsync_config_get (char *master, char *slave,
                  dict_t *dict, char **op_errstr)
{
        int32_t         ret     = -1;
        char            *op_name = NULL;
        char            cmd[1024] = {0,};

        ret = dict_get_str (dict, "op_name", &op_name);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "failed to get "
                        "the option for %s %s", master, slave);

                *op_errstr = gf_strdup ("configure command "
                                        "failed, please check "
                                        "the log-file\n");
                goto out;
        }

        ret = snprintf (cmd, 4096, GSYNCD_PREFIX "/gsyncd %s %s "
                        "--config-get %s", master, slave, op_name);
        if (ret <= 0) {
                gf_log ("", GF_LOG_WARNING, "failed to "
                        "construct the gsyncd command");
                *op_errstr = gf_strdup ("configure command "
                                        "failed, please check "
                                        "the log-file\n");
                goto out;
        }

        ret = system (cmd);
        if (ret == -1) {
                gf_log ("", GF_LOG_WARNING, "failed to get "
                        "%s option for %s %s peer", op_name,
                        master, slave);
                *op_errstr = gf_strdup ("configure command "
                                        "failed, please check "
                                        "the log-file\n");
                goto out;
        }
        ret = 0;
        *op_errstr = gf_strdup ("config-get successful");
out:
        return ret;
}

int
gsync_config_get_all (char *master, char *slave, char **op_errstr)
{
        int32_t         ret     = -1;
        char            cmd[1024] = {0,};

        ret = snprintf (cmd, 4096, GSYNCD_PREFIX "/gsyncd %s %s "
                        "config-get-all", master, slave);
        if (ret <= 0) {
                gf_log ("", GF_LOG_WARNING, "failed to "
                        "construct the gsyncd command "
                        "for config-get-all");
                *op_errstr = gf_strdup ("configure command "
                                        "failed, please check "
                                        "the log-file\n");
                goto out;
        }

        ret = system (cmd);
        if (ret == -1) {
                gf_log ("", GF_LOG_WARNING, "failed to get  "
                        "all options for %s %s peer", master,
                        slave);
                *op_errstr = gf_strdup ("configure command "
                                        "failed, please check "
                                        "the log-file\n");
                goto out;
        }
        ret = 0;
        *op_errstr = gf_strdup ("config-get successful");
out:
        return ret;
}

int
gsync_configure (char *master, char *slave,
                 dict_t *dict, char **op_errstr)
{
        int32_t         ret     = -1;
        int32_t         config_type = 0;

        ret = dict_get_int32 (dict, "config_type", &config_type);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "couldn't get the "
                        "config-type for %s %s", master, slave);
                *op_errstr = gf_strdup ("configure command failed, "
                                        "please check the log-file\n");
                goto out;
        }

        if (config_type == GF_GSYNC_OPTION_TYPE_CONFIG_SET) {
                ret = gsync_config_set (master, slave, dict, op_errstr);
                goto out;
        }

        if (config_type == GF_GSYNC_OPTION_TYPE_CONFIG_DEL) {
                ret = gsync_config_del (master, slave, dict, op_errstr);
                goto out;
        }

        if (config_type == GF_GSYNC_OPTION_TYPE_CONFIG_GET) {
                ret = gsync_config_get (master, slave, dict, op_errstr);
                goto out;
        }

        if (config_type == GF_GSYNC_OPTION_TYPE_CONFIG_GET_ALL) {
                ret = gsync_config_get_all (master, slave, op_errstr);
                goto out;
        } else {
                gf_log ("", GF_LOG_WARNING, "Invalid config type");
                *op_errstr = gf_strdup ("Invalid config type");
                ret = -1;
        }

out:
        return ret;
}

int
gsync_command_exec (dict_t *dict, char **op_errstr)
{
        char            *master = NULL;
        char            *slave  = NULL;
        int32_t         ret     = -1;
        int32_t         type    = -1;

        GF_VALIDATE_OR_GOTO ("gsync", dict, out);
        GF_VALIDATE_OR_GOTO ("gsync", op_errstr, out);

        ret = dict_get_int32 (dict, "type", &type);
        if (ret < 0)
                goto out;

        ret = dict_get_str (dict, "master", &master);
        if (ret < 0)
                goto out;

        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0)
                goto out;

        if (type == GF_GSYNC_OPTION_TYPE_START) {
                ret = 0;
                goto out;
        }

        if (type == GF_GSYNC_OPTION_TYPE_STOP) {
                ret = stop_gsync (master, slave, op_errstr);
                goto out;
        }

        if (type == GF_GSYNC_OPTION_TYPE_CONFIGURE) {
                ret = gsync_configure (master, slave, dict, op_errstr);
                goto out;
        } else {
                gf_log ("", GF_LOG_WARNING, "Invalid config type");
                *op_errstr = gf_strdup ("Invalid config type");
                ret = -1;
        }
out:
        return ret;
}

int
glusterd_set_marker_gsync (char *master, char *value)
{
        char                    *volname = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        int                      ret     = -1;

        volname = volname_from_master (master);

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Volume not Found");
                ret = -1;
                goto out;
        }

        ret = dict_set_str (volinfo->dict, MARKER_VOL_KEY, value);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Setting dict failed");
                goto out;
        }

        ret = glusterd_create_volfiles (volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to create volfile"
                        " for setting of marker while 'gsync start'");
                ret = -1;
                goto out;
        }

        ret = glusterd_restart_brick_servers (volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to restart bricks"
                        " for setting of marker while 'gsync start'");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_update_volume (volinfo);
        if (ret)
                goto out;

        ret = glusterd_volume_compute_cksum (volinfo);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_check_generate_start_nfs (volinfo);

out:
        return ret;

}

int
glusterd_op_gsync_set (dict_t *dict)
{
        char            *master = NULL;
        int32_t         ret     = -1;
        int32_t         type    = -1;
        dict_t          *ctx    = NULL;
        char            *op_errstr = NULL;

        ret = dict_get_int32 (dict, "type", &type);
        if (ret < 0)
                goto out;

        ret = dict_get_str (dict, "master", &master);
        if (ret < 0)
                goto out;

        if (type == GF_GSYNC_OPTION_TYPE_START) {
                ret = glusterd_set_marker_gsync (master, "on");
                if (ret != 0) {
                        gf_log ("", GF_LOG_WARNING, "marker start failed");
                        op_errstr = gf_strdup ("gsync start failed");
                        ret = -1;
                        goto out;
                }
        }

        if (type == GF_GSYNC_OPTION_TYPE_STOP) {
                ret = glusterd_set_marker_gsync (master, "off");
                if (ret != 0) {
                        gf_log ("", GF_LOG_WARNING, "marker stop failed");
                        op_errstr = gf_strdup ("gsync stop failed");
                        ret = -1;
                        goto out;
                }
        }
out:
        ctx = glusterd_op_get_ctx (GD_OP_GSYNC_SET);
        if (ctx) {
                ret = gsync_command_exec (dict, &op_errstr);
                if (op_errstr) {
                        ret = dict_set_str (ctx, "errstr", op_errstr);
                        if (ret) {
                                GF_FREE (op_errstr);
                                gf_log ("", GF_LOG_WARNING, "failed to set "
                                        "error message in ctx");
                        }
                }
        }

        return ret;
}

static int
glusterd_stop_bricks (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t                    *brickinfo = NULL;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (glusterd_brick_stop (volinfo, brickinfo))
                        return -1;
        }

        return 0;
}

static int
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
        glusterd_volinfo_t                       *voliter = NULL;

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

                if (strcmp (key, MARKER_VOL_KEY) == 0 &&
                    GLUSTERD_STATUS_STARTED == volinfo->status) {

                        restart_flag = 1;
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
                ret = glusterd_create_volfiles (volinfo);
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

                ret = glusterd_store_update_volume (volinfo);
                if (ret)
                        goto out;

                ret = glusterd_volume_compute_cksum (volinfo);
                if (ret)
                        goto out;

                if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                        ret = glusterd_check_generate_start_nfs (volinfo);
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
                        ret = glusterd_create_volfiles (volinfo);
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

                        ret = glusterd_store_update_volume (volinfo);
                        if (ret)
                                goto out;

                        ret = glusterd_volume_compute_cksum (volinfo);
                        if (ret)
                                goto out;

                        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                                ret = glusterd_check_generate_start_nfs (volinfo);
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

        ret = glusterd_create_volfiles (volinfo);
        if (ret)
                goto out;

        volinfo->version++;
        volinfo->defrag_status = 0;

        ret = glusterd_store_update_volume (volinfo);

        if (ret)
                goto out;

        ret = glusterd_volume_compute_cksum (volinfo);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_check_generate_start_nfs (volinfo);

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

        ret = glusterd_store_delete_volume (volinfo);

        if (ret)
                goto out;

        ret = glusterd_volinfo_delete (volinfo);

        if (ret)
                goto out;

out:
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

        ret = glusterd_store_update_volume (volinfo);
        if (ret)
                goto out;

        ret = glusterd_volume_compute_cksum (volinfo);
        if (ret)
                goto out;

        ret = glusterd_check_generate_start_nfs (volinfo);

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

        ret = glusterd_store_update_volume (volinfo);
        if (ret)
                goto out;

        ret = glusterd_volume_compute_cksum (volinfo);

        if (glusterd_are_all_volumes_stopped ()) {
                if (glusterd_is_nfs_started ()) {
                        ret = glusterd_nfs_server_stop ();
                        if (ret)
                                goto out;
                }
        } else {
                ret = glusterd_check_generate_start_nfs (volinfo);
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
                        snprintf (msg, sizeof (msg), "Volume %s does not exist",
                                  volname);
                        gf_log ("", GF_LOG_ERROR, "%s", msg);
                        *op_errstr = gf_strdup (msg);
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

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (!peerinfo->connected)
                        continue;
                if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
                    (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
                        continue;

                proc = &peerinfo->mgmt->proctable[GD_MGMT_CLUSTER_LOCK];
                if (proc->fn) {
                        ret = proc->fn (NULL, this, peerinfo);
                        if (ret)
                                continue;
                        opinfo.pending_count++;
                }
        }

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

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);

        /*ret = glusterd_unlock (priv->uuid);

        if (ret)
                goto out;
        */

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (!peerinfo->connected)
                        continue;
                if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
                    (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
                        continue;

                proc = &peerinfo->mgmt->proctable[GD_MGMT_CLUSTER_UNLOCK];
                if (proc->fn) {
                        ret = proc->fn (NULL, this, peerinfo);
                        if (ret)
                                continue;
                        opinfo.pending_count++;
                }
        }

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

        opinfo.pending_count--;

        if (opinfo.pending_count)
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

        ret = dict_set_int32 (req_dict, "operation", op);
        if (ret)
                gf_log ("", GF_LOG_WARNING, "failed to set op");

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
                case GD_OP_GSYNC_SET:
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
        ret = glusterd_op_stage_validate (dict, &op_errstr, NULL);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Staging failed");
                opinfo.op_errstr = op_errstr;
                goto out;
        }

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (!peerinfo->connected)
                        continue;
                if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
                    (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
                        continue;

                proc = &peerinfo->mgmt->proctable[GD_MGMT_STAGE_OP];
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
                        opinfo.pending_count++;
                }
        }

out:
        if (ret) {
                glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT, NULL);
                opinfo.op_ret = ret;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Sent op req to %d peers",
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
        glusterd_peerinfo_t     *peerinfo = NULL;
        char                    *op_errstr  = NULL;
        int                      i = 0;

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

        ret = glusterd_op_commit_perform (dict, &op_errstr, NULL); //rsp_dict invalid for source
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Commit failed");
                opinfo.op_errstr = op_errstr;
                goto out;
        }

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (!peerinfo->connected)
                        continue;
                if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
                    (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
                        continue;

                proc = &peerinfo->mgmt->proctable[GD_MGMT_COMMIT_OP];
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
                        opinfo.pending_count++;
                }
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Sent op req to %d peers",
                opinfo.pending_count);
out:
        if (ret) {
                glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT, NULL);
                opinfo.op_ret = ret;
        }

        if (!opinfo.pending_count) {
                dict = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
                if (!dict) {
                        ret = glusterd_op_sm_inject_all_acc ();
                        goto err;
                }

                dict = dict_ref (dict);
                ret = glusterd_op_start_rb_timer (dict);
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

        opinfo.pending_count--;

        if (opinfo.pending_count)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_STAGE_ACC, NULL);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
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

        opinfo.pending_count--;

        if (opinfo.pending_count)
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

        opinfo.pending_count--;

        if (opinfo.pending_count)
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

        gf_log ("glusterd", GF_LOG_NORMAL, "Cleared local lock");

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
                glusterd_op_set_ctx (op, NULL);
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
glusterd_op_ac_commit_error (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;

        //Log here with who failed the commit
        //

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_START_UNLOCK, NULL);

        return ret;
}

static int
glusterd_op_ac_stage_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = -1;
        glusterd_op_stage_ctx_t *stage_ctx = NULL;
        int32_t                 status = 0;
        dict_t                  *rsp_dict  = NULL;
        char                    *op_errstr = NULL;
        dict_t                  *dict = NULL;

        GF_ASSERT (ctx);

        stage_ctx = ctx;

        dict = stage_ctx->dict;

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_log ("", GF_LOG_DEBUG,
                        "Out of memory");
                return -1;
        }

        status = glusterd_op_stage_validate (dict, &op_errstr,
                                             rsp_dict);

        if (status) {
                gf_log ("", GF_LOG_ERROR, "Validate failed: %d", status);
        }

        ret = glusterd_op_stage_send_resp (stage_ctx->req, stage_ctx->op,
                                           status, op_errstr, rsp_dict);

        if (op_errstr && (strcmp (op_errstr, "")))
                GF_FREE (op_errstr);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        if (rsp_dict)
                dict_unref (rsp_dict);

        return ret;
}

static int
glusterd_op_ac_commit_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                       ret        = 0;
        glusterd_op_commit_ctx_t *commit_ctx = NULL;
        int32_t                   status     = 0;
        char                     *op_errstr  = NULL;
        dict_t                   *rsp_dict   = NULL;
        dict_t                   *dict       = NULL;

        GF_ASSERT (ctx);

        commit_ctx = ctx;

        dict = commit_ctx->dict;

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_log ("", GF_LOG_DEBUG,
                        "Out of memory");
                ret = -1;
                goto out;
        }

        status = glusterd_op_commit_perform (dict, &op_errstr, rsp_dict);

        if (status) {
                gf_log ("", GF_LOG_ERROR, "Commit failed: %d", status);
        }

        ret = glusterd_op_commit_send_resp (commit_ctx->req, commit_ctx->op,
                                            status, op_errstr, rsp_dict);

out:
        if (rsp_dict)
                dict_unref (rsp_dict);
        if (op_errstr && (strcmp (op_errstr, "")))
                GF_FREE (op_errstr);

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
glusterd_op_stage_validate (dict_t *dict, char **op_errstr,
                            dict_t *rsp_dict)
{
        int ret = -1;
        int op  = -1;

        ret = dict_get_int32 (dict, "operation", &op);
        if (ret)
                gf_log ("", GF_LOG_WARNING, "operation not set");

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

                default:
                        gf_log ("", GF_LOG_ERROR, "Unknown op %d",
                                op);
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}


int32_t
glusterd_op_commit_perform (dict_t *dict, char **op_errstr,
                            dict_t *rsp_dict)
{
        int ret = -1;
        int op  = -1;

        ret = dict_get_int32 (dict, "operation", &op);
        if (ret)
                gf_log ("", GF_LOG_WARNING, "operation not set");

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
                        ret = glusterd_op_gsync_set (dict);
                        break;

                default:
                        gf_log ("", GF_LOG_ERROR, "Unknown op %d",
                                op);
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
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_stage_op_sent [] = {
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_rcvd_stage_op_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_send_stage_op}, //EVENT_ALL_ACC
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_send_commit_op}, //EVENT_STAGE_ACC
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_UNLOCK_SENT,   glusterd_op_ac_send_unlock}, //EVENT_RCVD_RJT
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_MAX
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
        {GD_OP_STATE_COMMITED, glusterd_op_ac_commit_op}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_commit_op_sent [] = {
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_rcvd_commit_op_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_UNLOCK_SENT,    glusterd_op_ac_send_unlock}, //EVENT_ALL_ACC
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_UNLOCK_SENT,    glusterd_op_ac_send_unlock}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_commit_error}, //EVENT_RCVD_RJT
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock}, //EVENT_START_UNLOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_commited [] = {
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
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_START_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_MAX
};


glusterd_op_sm_t *glusterd_op_state_table [] = {
        glusterd_op_state_default,
        glusterd_op_state_lock_sent,
        glusterd_op_state_locked,
        glusterd_op_state_stage_op_sent,
        glusterd_op_state_staged,
        glusterd_op_state_commit_op_sent,
        glusterd_op_state_commited,
        glusterd_op_state_unlock_sent
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
                glusterd_destroy_stage_ctx (event->ctx);
                break;
        case GD_OP_EVENT_COMMIT_OP:
                glusterd_destroy_commit_ctx (event->ctx);
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

        int32_t         ret;

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
glusterd_op_set_ctx (glusterd_op_t op, void *ctx)
{

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        opinfo.op_ctx[op] = ctx;

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

