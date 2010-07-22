/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is GF_FREE software; you can redistribute it and/or modify
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
#include "glusterd-ha.h"
#include "cli1.h"

static struct list_head gd_op_sm_queue;
glusterd_op_info_t    opinfo;
static int glusterfs_port = GLUSTERD_DEFAULT_PORT;

static void
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

static int
glusterd_op_get_len (glusterd_op_t op)
{
        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);
        int             ret = -1;

        switch (op) {
                case GD_OP_CREATE_VOLUME:
                        {
                                dict_t *dict = glusterd_op_get_ctx (op);
                                ret = dict_serialized_length (dict);
                                return ret;
                        }
                        break;

                case GD_OP_START_BRICK:
                        break;

                case GD_OP_ADD_BRICK:
                        {
                                dict_t *dict = glusterd_op_get_ctx (op);
                                ret = dict_serialized_length (dict);
                                return ret;
                        }
                        break;

                default:
                        GF_ASSERT (op);

        }

        return 0;
}

static int
glusterd_op_sm_inject_all_acc ()
{
        glusterd_op_sm_event_t *event = NULL;
        int32_t                 ret = -1;

        ret = glusterd_op_sm_new_event (GD_OP_EVENT_ALL_ACC, &event);

        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (event);
out:
        return ret;
}

int
glusterd_op_build_payload (glusterd_op_t op, gd1_mgmt_stage_op_req **req)
{
        int                     len = 0;
        int                     ret = -1;
        gd1_mgmt_stage_op_req   *stage_req = NULL;

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);
        GF_ASSERT (req);

        len = glusterd_op_get_len (op);

        stage_req = GF_CALLOC (1, sizeof (*stage_req),
                               gf_gld_mt_mop_stage_req_t);

        if (!stage_req) {
                gf_log ("", GF_LOG_ERROR, "Out of Memory");
                goto out;
        }


        glusterd_get_uuid (&stage_req->uuid);
        stage_req->op = op;
        //stage_req->buf.buf_len = len;

        switch (op) {
                case GD_OP_CREATE_VOLUME:
                        {
                                dict_t  *dict = NULL;
                                dict = glusterd_op_get_ctx (op);
                                GF_ASSERT (dict);
                                ++glusterfs_port;
                                ret = dict_set_int32 (dict, "port", glusterfs_port);
                                ret = dict_allocate_and_serialize (dict,
                                                &stage_req->buf.buf_val,
                                        (size_t *)&stage_req->buf.buf_len);
                                if (ret) {
                                        goto out;
                                }
                        }
                        break;

                case GD_OP_START_VOLUME:
                        {
                                glusterd_op_start_volume_ctx_t *ctx = NULL;
                                ctx = glusterd_op_get_ctx (op);
                                GF_ASSERT (ctx);
                                stage_req->buf.buf_len  =
                                        strlen (ctx->volume_name);
                                stage_req->buf.buf_val =
                                        gf_strdup (ctx->volume_name);
                        }
                        break;

                case GD_OP_STOP_VOLUME:
                        {
                                glusterd_op_stop_volume_ctx_t *ctx = NULL;
                                ctx = glusterd_op_get_ctx (op);
                                GF_ASSERT (ctx);
                                stage_req->buf.buf_len  =
                                        strlen (ctx->volume_name);
                                stage_req->buf.buf_val =
                                        gf_strdup (ctx->volume_name);
                        }
                        break;

                case GD_OP_DELETE_VOLUME:
                        {
                                glusterd_op_delete_volume_ctx_t *ctx = NULL;
                                ctx = glusterd_op_get_ctx (op);
                                GF_ASSERT (ctx);
                                stage_req->buf.buf_len  =
                                        strlen (ctx->volume_name);
                                stage_req->buf.buf_val =
                                        gf_strdup (ctx->volume_name);
                        }
                        break;

                case GD_OP_ADD_BRICK:
                        {
                                dict_t  *dict = NULL;
                                dict = glusterd_op_get_ctx (op);
                                GF_ASSERT (dict);
                                ret = dict_allocate_and_serialize (dict,
                                                &stage_req->buf.buf_val,
                                        (size_t *)&stage_req->buf.buf_len);
                                if (ret) {
                                        goto out;
                                }
                        }
                        break;

                default:
                        break;
        }

        *req = stage_req;
        ret = 0;

out:
        return ret;
}

static int
glusterd_volume_create_generate_volfiles (glusterd_volinfo_t *volinfo)
{
        int32_t         ret = -1;
        char            cmd_str[8192] = {0,};
        char            path[PATH_MAX] = {0,};
        glusterd_conf_t *priv = NULL;
        xlator_t        *this = NULL;
        char            bricks[8192] = {0,};
        glusterd_brickinfo_t    *brickinfo = NULL;
        int32_t         len = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);
        GF_ASSERT (volinfo);

        GLUSTERD_GET_VOLUME_DIR(path, volinfo, priv);
        if (!volinfo->port) {
                //volinfo->port = ++glusterfs_port;
        }

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                snprintf (bricks + len, 8192 - len, "%s:%s ",
                          brickinfo->hostname, brickinfo->path);
                len = strlen (bricks);
        }

        gf_log ("", GF_LOG_DEBUG, "Brick string: %s", bricks);

        switch (volinfo->type) {

                case GF_CLUSTER_TYPE_REPLICATE:
                {
                        snprintf (cmd_str, 8192,
                                  "glusterfs-volgen -n %s -c %s -r 1 %s -p %d"
                                  "--num-replica %d",
                                   volinfo->volname, path, bricks,
                                   volinfo->port, volinfo->sub_count);
                        ret = system (cmd_str);
                        break;
                }

                case GF_CLUSTER_TYPE_STRIPE:
                {
                        snprintf (cmd_str, 8192,
                                  "glusterfs-volgen -n %s -c %s -r 0 %s -p %d"
                                  "--num-stripe %d",
                                  volinfo->volname, path, bricks,
                                  volinfo->port, volinfo->sub_count);
                        ret = system (cmd_str);
                        break;
                }

                case GF_CLUSTER_TYPE_NONE:
                {
                        snprintf (cmd_str, 8192,
                                  "glusterfs-volgen -n %s -c %s %s -p %d",
                                  volinfo->volname, path, bricks,
                                  volinfo->port);
                        ret = system (cmd_str);
                        break;
                }

                default:
                        gf_log ("", GF_LOG_ERROR, "Unkown type: %d",
                                volinfo->type);
                        ret = -1;
        }
//out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}



static int
glusterd_op_stage_create_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        dict_t                                  *dict = NULL;
        char                                    *volname = NULL;
        gf_boolean_t                            exists = _gf_false;

        GF_ASSERT (req);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_unserialize (req->buf.buf_val, req->buf.buf_len, &dict);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to unserialize dict");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);

        if (exists) {
                gf_log ("", GF_LOG_ERROR, "Volume with name: %s exists",
                        volname);
                ret = -1;
        } else {
                ret = 0;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_stage_start_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        char                                    volname [1024] = {0,};
        gf_boolean_t                            exists = _gf_false;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;

        GF_ASSERT (req);

        strncpy (volname, req->buf.buf_val, req->buf.buf_len);
        //volname = req->buf.buf_val;

        exists = glusterd_check_volume_exists (volname);

        if (!exists) {
                gf_log ("", GF_LOG_ERROR, "Volume with name %s does not exist",
                        volname);
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
                        gf_log ("", GF_LOG_ERROR, "Unable to resolve brick"
                                " with hostname: %s, export: %s",
                                brickinfo->hostname,brickinfo->path);
                        goto out;
                }
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_stage_stop_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        char                                    volname[1024] = {0,};
        gf_boolean_t                            exists = _gf_false;
        glusterd_volinfo_t                      *volinfo = NULL;

        GF_ASSERT (req);

        strncpy (volname, req->buf.buf_val, req->buf.buf_len);

        exists = glusterd_check_volume_exists (volname);

        if (!exists) {
                gf_log ("", GF_LOG_ERROR, "Volume with name %s does not exist",
                        volname);
                ret = -1;
        } else {
                ret = 0;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        ret = glusterd_is_volume_started (volinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Volume %s has not been started",
                        volname);
                goto out;
        }


out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_stage_delete_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        char                                    volname [1024] = {0,};
        gf_boolean_t                            exists = _gf_false;
        glusterd_volinfo_t                      *volinfo = NULL;

        GF_ASSERT (req);

        strncpy (volname, req->buf.buf_val, req->buf.buf_len);

        exists = glusterd_check_volume_exists (volname);

        if (!exists) {
                gf_log ("", GF_LOG_ERROR, "Volume with name %s does not exist",
                        volname);
                ret = -1;
        } else {
                ret = 0;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        ret = glusterd_is_volume_started (volinfo);

        if (!ret) {
                gf_log ("", GF_LOG_ERROR, "Volume %s has been started."
                        "Volume needs to be stopped before deletion.",
                        volname);
                goto out;
        }

        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_stage_add_brick (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        dict_t                                  *dict = NULL;
        char                                    *volname = NULL;
        gf_boolean_t                            exists = _gf_false;

        GF_ASSERT (req);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_unserialize (req->buf.buf_val, req->buf.buf_len, &dict);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to unserialize dict");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);

        if (!exists) {
                gf_log ("", GF_LOG_ERROR, "Volume with name: %s exists",
                        volname);
                ret = -1;
        } else {
                ret = 0;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_create_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        dict_t                                  *dict = NULL;
        char                                    *volname = NULL;
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        xlator_t                                *this = NULL;
        char                                    *brick = NULL;
        int32_t                                 count = 0;
        int32_t                                 i = 1;
        char                                    *bricks    = NULL;
        char                                    *brick_list = NULL;
        char                                    *saveptr = NULL;
        int32_t                                 sub_count = 0;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_unserialize (req->buf.buf_val, req->buf.buf_len, &dict);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to unserialize dict");
                goto out;
        }

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

        strncpy (volinfo->volname, volname, 1024);
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

        volinfo->sub_count = sub_count;


        if (bricks)
                brick_list = gf_strdup (bricks);

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);

        while ( i <= count) {
                ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
                if (ret)
                        goto out;

                list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
        }
        list_add_tail (&volinfo->vol_list, &priv->volumes);

        ret = glusterd_ha_create_volume (volinfo);

        if (ret)
                goto out;

        ret = glusterd_volume_create_generate_volfiles (volinfo);
        if (ret)
                goto out;


out:
        return ret;
}

static int
glusterd_op_add_brick (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        dict_t                                  *dict = NULL;
        char                                    *volname = NULL;
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        xlator_t                                *this = NULL;
        char                                    *brick = NULL;
        int32_t                                 count = 0;
        int32_t                                 i = 1;
        char                                    *bricks    = NULL;
        char                                    *brick_list = NULL;
        char                                    *saveptr = NULL;
        gf_boolean_t                            glfs_started = _gf_false;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_unserialize (req->buf.buf_val, req->buf.buf_len, &dict);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to unserialize dict");
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


        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }

        volinfo->brick_count += count;

        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get bricks");
                goto out;
        }

        if (bricks)
                brick_list = gf_strdup (bricks);

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);

        while ( i <= count) {
                ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
                if (ret)
                        goto out;

                list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                ret = glusterd_resolve_brick (brickinfo);

                if (!uuid_compare (brickinfo->uuid, priv->uuid)) {
                        ret =
                          glusterd_volume_create_generate_volfiles (volinfo);
                        if (ret)
                                goto out;

                        gf_log ("", GF_LOG_NORMAL, "About to start glusterfs"
                                " for brick %s:%s", brickinfo->hostname,
                                brickinfo->path);
                        ret = glusterd_volume_start_glusterfs
                                                (volinfo, brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, "Unable to start "
                                        "glusterfs, ret: %d", ret);
                                goto out;
                        }
                        glfs_started = _gf_true;
                }

                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
        }

        if (!glfs_started) {
                ret = glusterd_volume_create_generate_volfiles (volinfo);
                if (ret)
                        goto out;
        }

/*        ret = glusterd_ha_update_volume (volinfo);

        if (ret)
                goto out;
*/


out:
        return ret;
}

static int
glusterd_op_delete_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        char                                    volname[1024] = {0,};
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        xlator_t                                *this = NULL;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        strncpy (volname, req->buf.buf_val, req->buf.buf_len);

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        ret = glusterd_ha_delete_volume (volinfo);

        if (ret)
                goto out;

        ret = glusterd_volinfo_delete (volinfo);

        if (ret)
                goto out;

out:
        return ret;
}

static int
glusterd_op_start_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        char                                    volname[1024] = {0,};
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        xlator_t                                *this = NULL;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        strncpy (volname, req->buf.buf_val, req->buf.buf_len);

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (!uuid_compare (brickinfo->uuid, priv->uuid)) {
                        gf_log ("", GF_LOG_NORMAL, "About to start glusterfs"
                                " for brick %s:%s", brickinfo->hostname,
                                brickinfo->path);
                        ret = glusterd_volume_start_glusterfs
                                                (volinfo, brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, "Unable to start "
                                        "glusterfs, ret: %d", ret);
                                goto out;
                        }
                }
        }

        glusterd_set_volume_status (volinfo, GLUSTERD_STATUS_STARTED);

out:
        return ret;
}

static int
glusterd_op_stop_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        char                                    volname[1024] = {0,};
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        xlator_t                                *this = NULL;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        strncpy (volname, req->buf.buf_val, req->buf.buf_len);

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (!uuid_compare (brickinfo->uuid, priv->uuid)) {
                        gf_log ("", GF_LOG_NORMAL, "About to stop glusterfs"
                                " for brick %s:%s", brickinfo->hostname,
                                brickinfo->path);
                        ret = glusterd_volume_stop_glusterfs
                                                (volinfo, brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, "Unable to stop "
                                        "glusterfs, ret: %d", ret);
                                goto out;
                        }
                }
        }

        glusterd_set_volume_status (volinfo, GLUSTERD_STATUS_STOPPED);

out:
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
        int                     ret = 0;
        rpc_clnt_procedure_t    *proc = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        priv = this->private;

        proc = &priv->mgmt->proctable[GD_MGMT_CLUSTER_LOCK];
        if (proc->fn) {
                ret = proc->fn (NULL, this, NULL);
                if (ret)
                        goto out;
        }

        if (!opinfo.pending_count)
                ret = glusterd_op_sm_inject_all_acc ();

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_op_ac_send_unlock (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        rpc_clnt_procedure_t    *proc = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        priv = this->private;

        ret = glusterd_unlock (priv->uuid);

        if (ret)
                goto out;

        proc = &priv->mgmt->proctable[GD_MGMT_CLUSTER_UNLOCK];
        if (proc->fn) {
                ret = proc->fn (NULL, this, NULL);
                if (ret)
                        goto out;
        }

        if (!opinfo.pending_count)
                ret = glusterd_op_sm_inject_all_acc ();

out:
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
        glusterd_op_sm_event_t  *new_event = NULL;

        GF_ASSERT (event);

        opinfo.pending_count--;

        if (opinfo.pending_count)
                goto out;

        ret = glusterd_op_sm_new_event (GD_OP_EVENT_ALL_ACC, &new_event);

        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (new_event);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

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

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (priv->mgmt);

        proc = &priv->mgmt->proctable[GD_MGMT_STAGE_OP];
        GF_ASSERT (proc);
        if (proc->fn) {
                ret = proc->fn (NULL, this, NULL);
                if (ret)
                        goto out;
        }

        if (!opinfo.pending_count)
                ret = glusterd_op_sm_inject_all_acc ();

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;

}

static int
glusterd_op_ac_send_commit_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        rpc_clnt_procedure_t    *proc = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (priv->mgmt);

        proc = &priv->mgmt->proctable[GD_MGMT_COMMIT_OP];
        GF_ASSERT (proc);
        if (proc->fn) {
                ret = proc->fn (NULL, this, NULL);
                if (!ret)
                        goto out;
        }

        if (!opinfo.pending_count)
                ret = glusterd_op_sm_inject_all_acc ();

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;

}

static int
glusterd_op_ac_rcvd_stage_op_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        glusterd_op_sm_event_t  *new_event = NULL;

        GF_ASSERT (event);

        opinfo.pending_count--;

        if (opinfo.pending_count)
                goto out;

        ret = glusterd_op_sm_new_event (GD_OP_EVENT_STAGE_ACC, &new_event);

        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (new_event);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_ac_rcvd_commit_op_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        glusterd_op_sm_event_t  *new_event = NULL;

        GF_ASSERT (event);

        opinfo.pending_count--;

        if (opinfo.pending_count)
                goto out;

        ret = glusterd_op_sm_new_event (GD_OP_EVENT_COMMIT_ACC, &new_event);

        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (new_event);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

out:
        return ret;
}

static int
glusterd_op_ac_rcvd_unlock_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        glusterd_op_sm_event_t  *new_event = NULL;

        GF_ASSERT (event);

        opinfo.pending_count--;

        if (opinfo.pending_count)
                goto out;

        ret = glusterd_op_sm_new_event (GD_OP_EVENT_ALL_ACC, &new_event);

        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (new_event);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

out:
        return ret;
}

static int
glusterd_op_ac_commit_error (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;

        //Log here with who failed the commit
        //
        return ret;
}

static int
glusterd_op_ac_stage_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = -1;
        gd1_mgmt_stage_op_req   *req = NULL;
        glusterd_op_stage_ctx_t *stage_ctx = NULL;
        int32_t                 status = 0;

        GF_ASSERT (ctx);

        stage_ctx = ctx;

        req = &stage_ctx->stage_req;

        status = glusterd_op_stage_validate (req);

        if (status) {
                gf_log ("", GF_LOG_ERROR, "Validate failed: %d", status);
        }

        ret = glusterd_op_stage_send_resp (stage_ctx->req, req->op, status);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_op_ac_commit_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                             ret = 0;
        gd1_mgmt_stage_op_req           *req = NULL;
        glusterd_op_commit_ctx_t        *commit_ctx = NULL;
        int32_t                         status = 0;

        GF_ASSERT (ctx);

        commit_ctx = ctx;

        req = &commit_ctx->stage_req;

        status = glusterd_op_commit_perform (req);

        if (status) {
                gf_log ("", GF_LOG_ERROR, "Commit failed: %d", status);
        }

        ret = glusterd_op_commit_send_resp (commit_ctx->req, req->op, status);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}


static int
glusterd_op_sm_transition_state (glusterd_op_info_t *opinfo,
                                 glusterd_op_sm_t *state,
                                 glusterd_op_sm_event_type_t event_type)
{

        GF_ASSERT (state);
        GF_ASSERT (opinfo);

        gf_log ("", GF_LOG_NORMAL, "Transitioning from %d to %d",
                     opinfo->state.state, state[event_type].next_state);
        opinfo->state.state =
                state[event_type].next_state;
        return 0;
}

int32_t
glusterd_op_stage_validate (gd1_mgmt_stage_op_req *req)
{
        int     ret = -1;

        GF_ASSERT (req);

        switch (req->op) {
                case GD_OP_CREATE_VOLUME:
                        ret = glusterd_op_stage_create_volume (req);
                        break;

                case GD_OP_START_VOLUME:
                        ret = glusterd_op_stage_start_volume (req);
                        break;

                case GD_OP_STOP_VOLUME:
                        ret = glusterd_op_stage_stop_volume (req);
                        break;

                case GD_OP_DELETE_VOLUME:
                        ret = glusterd_op_stage_delete_volume (req);
                        break;

                case GD_OP_ADD_BRICK:
                        ret = glusterd_op_stage_add_brick (req);
                        break;

                default:
                        gf_log ("", GF_LOG_ERROR, "Unknown op %d",
                                req->op);
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}


int32_t
glusterd_op_commit_perform (gd1_mgmt_stage_op_req *req)
{
        int     ret = -1;

        GF_ASSERT (req);

        switch (req->op) {
                case GD_OP_CREATE_VOLUME:
                        ret = glusterd_op_create_volume (req);
                        break;

                case GD_OP_START_VOLUME:
                        ret = glusterd_op_start_volume (req);
                        break;

                case GD_OP_STOP_VOLUME:
                        ret = glusterd_op_stop_volume (req);
                        break;

                case GD_OP_DELETE_VOLUME:
                        ret = glusterd_op_delete_volume (req);
                        break;

                case GD_OP_ADD_BRICK:
                        ret = glusterd_op_add_brick (req);
                        break;
                default:
                        gf_log ("", GF_LOG_ERROR, "Unknown op %d",
                                req->op);
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
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
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
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_unlock_sent [] = {
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_rcvd_unlock_acc}, //EVENT_RCVD_ACC
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_ALL_ACC
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_RCVD_RJT
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
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
glusterd_op_sm_inject_event (glusterd_op_sm_event_t *event)
{
        GF_ASSERT (event);
        gf_log ("glusterd", GF_LOG_NORMAL, "Enqueuing event: %d",
                        event->event);
        list_add_tail (&event->list, &gd_op_sm_queue);

        return 0;
}


int
glusterd_op_sm ()
{
        glusterd_op_sm_event_t          *event = NULL;
        glusterd_op_sm_event_t          *tmp = NULL;
        int                             ret = -1;
        glusterd_op_sm_ac_fn            handler = NULL;
        glusterd_op_sm_t                *state = NULL;
        glusterd_op_sm_event_type_t     event_type = 0;


        while (!list_empty (&gd_op_sm_queue)) {

                list_for_each_entry_safe (event, tmp, &gd_op_sm_queue, list) {

                        list_del_init (&event->list);
                        event_type = event->event;

                        state = glusterd_op_state_table[opinfo.state.state];

                        GF_ASSERT (state);

                        handler = state[event_type].handler;
                        GF_ASSERT (handler);

                        ret = handler (event, event->ctx);

                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR,
                                        "handler returned: %d", ret);
                                return ret;
                        }

                        ret = glusterd_op_sm_transition_state (&opinfo, state,
                                                                event_type);

                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR,
                                        "Unable to transition"
                                        "state from %d to %d",
                                         opinfo.state.state,
                                         state[event_type].next_state);
                                return ret;
                        }

                        GF_FREE (event);
                }
        }


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
glusterd_op_set_ctx (glusterd_op_t op, void *ctx)
{

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        opinfo.op_ctx[op] = ctx;

        return 0;

}


void *
glusterd_op_get_ctx (glusterd_op_t op)
{
        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        return opinfo.op_ctx[op];

}

int
glusterd_op_sm_init ()
{
        INIT_LIST_HEAD (&gd_op_sm_queue);
        return 0;
}
