/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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
#include "cli1.h"
#include "glusterd-volgen.h"

#include <sys/types.h>
#include <signal.h>

static struct list_head gd_op_sm_queue;
glusterd_op_info_t    opinfo = {{0},};
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

                case GD_OP_REPLACE_BRICK:
                case GD_OP_ADD_BRICK:
                        {
                                dict_t *dict = glusterd_op_get_ctx (op);
                                ret = dict_serialized_length (dict);
                                return ret;
                        }

                 case GD_OP_REMOVE_BRICK:
                        {
                                dict_t *dict = glusterd_op_get_ctx (op);
                                ret = dict_serialized_length (dict);
                                return ret;
                        }
                        break;
                       break;

                default:
                        GF_ASSERT (op);

        }

        return 0;
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
                                dict_t  *dict = NULL;
                                dict = glusterd_op_get_ctx (op);
                                if (!dict) {
                                        gf_log ("", GF_LOG_ERROR, "Null Context for "
                                                "stop volume");
                                        ret = -1;
                                        goto out;
                                }
                                ret = dict_allocate_and_serialize (dict,
                                                &stage_req->buf.buf_val,
                                        (size_t *)&stage_req->buf.buf_len);
                                if (ret) {
                                        goto out;
                                }
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

                case GD_OP_REPLACE_BRICK:
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

                case GD_OP_REMOVE_BRICK:
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

                case GD_OP_LOG_FILENAME:
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

                case GD_OP_LOG_ROTATE:
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
glusterd_check_generate_start_nfs (glusterd_volinfo_t *volinfo)
{
        int ret = -1;

        if (!volinfo) {
                gf_log ("", GF_LOG_ERROR, "Invalid Arguments");
                goto out;
        }

        ret = volgen_generate_nfs_volfile (volinfo);
        if (ret)
                goto out;

        if (glusterd_is_nfs_started ()) {
                ret = glusterd_nfs_server_stop ();
                if (ret)
                        goto out;
        }

        ret = glusterd_nfs_server_start ();
out:
        return ret;
}

static int
glusterd_op_stage_create_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        dict_t                                  *dict = NULL;
        char                                    *volname = NULL;
        gf_boolean_t                            exists = _gf_false;
        char                                    *bricks = NULL;
        char                                    *brick_list = NULL;
        glusterd_brickinfo_t                    *brick_info = NULL;
        int32_t                                 brick_count = 0;
        int32_t                                 i = 0;
        struct stat                             st_buf = {0,};
        char                                    *brick = NULL;
        char                                    *tmpptr = NULL;
        char                                    cmd_str[1024];
        xlator_t                                *this = NULL;
        glusterd_conf_t                         *priv = NULL;

        GF_ASSERT (req);

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

        if (bricks)
                brick_list = gf_strdup (bricks);

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
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "cannot resolve brick");
                        goto out;
                }

                if (!uuid_compare (brick_info->uuid, priv->uuid)) {
                        ret = stat (cmd_str, &st_buf);
                        if (ret == -1) {
                                gf_log ("glusterd", GF_LOG_ERROR, "Volname %s, brick"
                                        ":%s path %s not present", volname,
                                        brick, brick_info->path);
                                goto out;
                        }
                        brick_list = tmpptr;
                }
                glusterd_brickinfo_delete (brick_info);
        }
out:
        if (dict)
                dict_unref (dict);
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

        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "volume already started");
                ret = -1;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_stop_volume_args_get (gd1_mgmt_stage_op_req *req,
                                  dict_t *dict, char** volname,
                                  int *flags)
{
        int ret = -1;

        if (!req || !dict || !volname || !flags)
                goto out;

        ret = dict_unserialize (req->buf.buf_val, req->buf.buf_len, &dict);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to unserialize dict");
                goto out;
        }

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
glusterd_op_stage_stop_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = -1;
        dict_t                                  *dict = NULL;
        char                                    *volname = NULL;
        int                                     flags = 0;
        gf_boolean_t                            exists = _gf_false;
        glusterd_volinfo_t                      *volinfo = NULL;

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = glusterd_op_stop_volume_args_get (req, dict, &volname, &flags);
        if (ret)
                goto out;

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

        if (!(flags & GF_CLI_FLAG_OP_FORCE)) {
                ret = glusterd_is_volume_started (volinfo);

                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Volume %s "
                                "has not been started", volname);
                        goto out;
                }
        }


out:
        if (dict)
                dict_unref (dict);

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
                goto out;
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
                ret = -1;
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
        int                                     count = 0;
        int                                     i = 0;
        char                                    *bricks    = NULL;
        char                                    *brick_list = NULL;
        char                                    *saveptr = NULL;
        char                                    *free_ptr = NULL;
        char                                    *brick = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        struct stat                             st_buf = {0,};
        char                                    cmd_str[1024];

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

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to find volume: %s", volname);
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
                free_ptr = brick_list;
        }

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);

        while ( i < count) {
                ret = glusterd_brickinfo_get (brick, volinfo, &brickinfo);
                if (!ret) {
                        gf_log ("", GF_LOG_ERROR, "Adding duplicate brick: %s",
                                brick);
                        ret = -1;
                        goto out;
                } else {
                        ret = glusterd_brickinfo_from_brick(brick, &brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, "Add-brick: Unable"
                                       " to get brickinfo");
                                goto out;
                        }
                }
                snprintf (cmd_str, 1024, "%s", brickinfo->path);
                ret = stat (cmd_str, &st_buf);
                if (ret == -1) {
                        gf_log ("glusterd", GF_LOG_ERROR, "Volname %s, brick"
                                ":%s path %s not present", volname,
                                brick, brickinfo->path);
                        goto out;
                }

                glusterd_brickinfo_delete (brickinfo);
                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
        }

out:
        if (dict)
                dict_unref (dict);
        if (free_ptr)
                GF_FREE (free_ptr);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_stage_replace_brick (gd1_mgmt_stage_op_req *req)
{
        int                                      ret           = 0;
        dict_t                                  *dict          = NULL;
        char                                    *src_brick     = NULL;
        char                                    *dst_brick     = NULL;
        char                                    *volname       = NULL;
        glusterd_volinfo_t                      *volinfo       = NULL;
        glusterd_brickinfo_t                    *src_brickinfo = NULL;
        gf_boolean_t                             exists        = _gf_false;

        GF_ASSERT (req);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_unserialize (req->buf.buf_val, req->buf.buf_len, &dict);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to unserialize dict");
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
                gf_log ("", GF_LOG_ERROR, "Unable to get dest brick");
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

        ret = glusterd_brickinfo_get (src_brick, volinfo,
                                      &src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to get src-brickinfo");
                goto out;
        }

        if (!glusterd_is_local_addr (src_brickinfo->hostname)) {
                gf_log ("", GF_LOG_DEBUG,
                        "I AM THE SOURCE HOST");
                exists = glusterd_check_volume_exists (volname);

                if (!exists) {
                        gf_log ("", GF_LOG_ERROR, "Volume with name: %s "
                                "does not exist",
                                volname);
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;

out:
        if (dict)
                dict_unref (dict);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_stage_log_filename (gd1_mgmt_stage_op_req *req)
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
                gf_log ("", GF_LOG_ERROR, "Volume with name: %s not exists",
                        volname);
                ret = -1;
                goto out;
        }

out:
        if (dict)
                dict_unref (dict);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_stage_log_rotate (gd1_mgmt_stage_op_req *req)
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
                gf_log ("", GF_LOG_ERROR, "Volume with name: %s not exists",
                        volname);
                ret = -1;
                goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_stage_remove_brick (gd1_mgmt_stage_op_req *req)
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
                goto out;
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
        char                                    *free_ptr   = NULL;
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

                list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
        }
        list_add_tail (&volinfo->vol_list, &priv->volumes);
        volinfo->version++;

        ret = glusterd_store_create_volume (volinfo);

        if (ret)
                goto out;

        ret = glusterd_create_volfiles (volinfo);

        ret = glusterd_volume_compute_cksum (volinfo);
        if (ret)
                goto out;

out:
        if (dict)
                dict_unref (dict);

        if (free_ptr)
                GF_FREE(free_ptr);

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
        char                                    *free_ptr1  = NULL;
        char                                    *free_ptr2  = NULL;
        char                                    *saveptr = NULL;
        gf_boolean_t                            glfs_started = _gf_false;
        int32_t                                 mybrick = 0;

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

                list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
        }

        brick_list = gf_strdup (bricks);
        free_ptr2 = brick_list;
        i = 1;

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);

        while (i <= count) {

                ret = glusterd_brickinfo_get (brick, volinfo, &brickinfo);
                if (ret)
                        goto out;

                ret = glusterd_resolve_brick (brickinfo);

                if (!ret && (!uuid_compare (brickinfo->uuid, priv->uuid)) &&
                                (GLUSTERD_STATUS_STARTED == volinfo->status)) {
                        ret = glusterd_create_volfiles (volinfo);
                        if (ret)
                                goto out;

                        gf_log ("", GF_LOG_NORMAL, "About to start glusterfs"
                                " for brick %s:%s", brickinfo->hostname,
                                brickinfo->path);
                        ret = glusterd_volume_start_glusterfs
                                                (volinfo, brickinfo, mybrick);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, "Unable to start "
                                        "glusterfs, ret: %d", ret);
                                goto out;
                        }
                        glfs_started = _gf_true;
                        mybrick++;
                }
                i++;
                brick = strtok_r (NULL, " \n", &saveptr);
        }

        if (!glfs_started) {
                ret = glusterd_create_volfiles (volinfo);
                if (ret)
                        goto out;
        }

        volinfo->version++;


        ret = glusterd_store_update_volume (volinfo);

        if (ret)
                goto out;

        ret = glusterd_volume_compute_cksum (volinfo);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_check_generate_start_nfs (volinfo);

out:
        if (dict)
                dict_unref (dict);
        if (free_ptr1)
                GF_FREE (free_ptr1);
        if (free_ptr2)
                GF_FREE (free_ptr2);
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

        ret = lsetxattr (mount_point_path, xattr_key,
                         value,
                         strlen (value),
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

        priv = THIS->private;

        snprintf (cmd_str, 8192,
                  "%s/sbin/glusterfs -f %s/vols/%s/%s -p %s/vols/%s/%s",
                  GFS_PREFIX, priv->workdir, volinfo->volname,
                  RB_DSTBRICKVOL_FILENAME,
                  priv->workdir, volinfo->volname,
                  RB_DSTBRICK_PIDFILE);

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

static const char *client_volfile_str =  "volume client/protocol\n"
        "type protocol/client\n"
        "option remote-host %s\n"
        "option remote-subvolume %s\n"
        "option remote-port %d\n"
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
        "type storage/posix\n"
        "option directory %s\n"
        "end-volume\n"
        "volume %s\n"
        "type features/locks\n"
        "subvolumes src-posix\n"
        "end-volume\n"
        "volume src-server\n"
        "type protocol/server\n"
        "option auth.addr.%s.allow *\n"
        "option listen-port 34034\n"
        "subvolumes %s\n"
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
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "mkdir failed");
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

        if (!glusterd_is_local_addr (src_brickinfo->hostname)) {
                gf_log ("", GF_LOG_NORMAL,
                        "I AM THE SOURCE HOST");
                ret = rb_spawn_maintainence_client (volinfo, src_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not spawn maintainence "
                                "client");
                        goto out;
                }

                snprintf (start_value, 8192, "%s:%s:",
                          dst_brickinfo->hostname,
                          dst_brickinfo->path);

                ret = rb_send_xattr_command (volinfo, src_brickinfo,
                                             dst_brickinfo, RB_PUMP_START_CMD,
                                             start_value);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to send command to pump");
                        goto out;
                }

                ret = rb_destroy_maintainence_client (volinfo, src_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to destroy maintainence "
                                "client");
                        goto out;
                }

        } else if (!glusterd_is_local_addr (dst_brickinfo->hostname)) {
                gf_log ("", GF_LOG_NORMAL,
                        "I AM THE DESTINATION HOST");
                ret = rb_spawn_destination_brick (volinfo, dst_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to spawn destination brick");
                        goto out;
                }

        } else {
                gf_log ("", GF_LOG_DEBUG,
                        "Not a source or destination brick");
                ret = 0;
                goto out;
        }

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

        if (!glusterd_is_local_addr (src_brickinfo->hostname)) {
                gf_log ("", GF_LOG_NORMAL,
                        "I AM THE SOURCE HOST");
                ret = rb_spawn_maintainence_client (volinfo, src_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not spawn maintainence "
                                "client");
                        goto out;
                }

                ret = rb_send_xattr_command (volinfo, src_brickinfo,
                                             dst_brickinfo, RB_PUMP_PAUSE_CMD,
                                             "jargon");
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to send command to pump");
                        goto out;
                }

                ret = rb_destroy_maintainence_client (volinfo, src_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to destroy maintainence "
                                "client");
                        goto out;
                }
        }

        ret = 0;

out:
        return ret;
}

static int
rb_kill_destination_brick (glusterd_volinfo_t *volinfo,
                           glusterd_brickinfo_t *dst_brickinfo)
{
        glusterd_conf_t  *priv               = NULL;
        int               ret                = -1;
        char              pidfile[PATH_MAX]  = {0,};
        pid_t             pid                = -1;
        FILE             *file               = NULL;

        priv = THIS->private;

        snprintf (pidfile, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_DSTBRICK_PIDFILE);

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

        gf_log ("", GF_LOG_NORMAL, "Stopping glusterfs running in pid: %d",
                pid);

        ret = kill (pid, SIGQUIT);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to kill pid %d", pid);
                goto out;
        }

        ret = 0;

out:
        return ret;
}

static int
rb_do_operation_abort (glusterd_volinfo_t *volinfo,
                       glusterd_brickinfo_t *src_brickinfo,
                       glusterd_brickinfo_t *dst_brickinfo)
{
        int ret = -1;

        if (!glusterd_is_local_addr (src_brickinfo->hostname)) {
                gf_log ("", GF_LOG_NORMAL,
                        "I AM THE SOURCE HOST");
                ret = rb_spawn_maintainence_client (volinfo, src_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not spawn maintainence "
                                "client");
                        goto out;
                }

                ret = rb_send_xattr_command (volinfo, src_brickinfo,
                                             dst_brickinfo, RB_PUMP_ABORT_CMD,
                                             "jargon");
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to send command to pump");
                        goto out;
                }

                ret = rb_destroy_maintainence_client (volinfo, src_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to destroy maintainence "
                                "client");
                        goto out;
                }
        }
        else if (!glusterd_is_local_addr (dst_brickinfo->hostname)) {
                gf_log ("", GF_LOG_NORMAL,
                        "I AM THE DESTINATION HOST");
                ret = rb_kill_destination_brick (volinfo, dst_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to kill destination brick");
                        goto out;
                }
        }

        ret = 0;

out:
        return ret;
}

static int
rb_do_operation_commit (glusterd_volinfo_t *volinfo,
                        glusterd_brickinfo_t *src_brickinfo,
                        glusterd_brickinfo_t *dst_brickinfo)
{

        gf_log ("", GF_LOG_DEBUG,
                "received commit on %s:%s to %s:%s "
                "on volume %s",
                src_brickinfo->hostname,
                src_brickinfo->path,
                dst_brickinfo->hostname,
                dst_brickinfo->path,
                volinfo->volname);

        return 0;
}

static int
rb_get_xattr_command (glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *src_brickinfo,
                      glusterd_brickinfo_t *dst_brickinfo,
                      const char *xattr_key,
                      const char **value)
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
                         (char *)(*value),
                         8192);

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
rb_do_operation_status (glusterd_volinfo_t *volinfo,
                        glusterd_brickinfo_t *src_brickinfo,
                        glusterd_brickinfo_t *dst_brickinfo,
                        dict_t *dict)
{
        const char *status       = NULL;
        char       *status_reply = NULL;
        dict_t     *ctx          = NULL;
        int ret = -1;

        if (!glusterd_is_local_addr (src_brickinfo->hostname)) {
                gf_log ("", GF_LOG_NORMAL,
                        "I AM THE SOURCE HOST");
                ret = rb_spawn_maintainence_client (volinfo, src_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not spawn maintainence "
                                "client");
                        goto out;
                }

                ret = rb_get_xattr_command (volinfo, src_brickinfo,
                                            dst_brickinfo, RB_PUMP_STATUS_CMD,
                                            &status);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to get status from pump");
                        goto out;
                }

                gf_log ("", GF_LOG_DEBUG,
                        "pump status is %s", status);

                ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
                if (!ctx) {
                        gf_log ("", GF_LOG_ERROR,
                                "Operation Context is not present");
                        ret = -1;
                        goto out;
                }
                status_reply = gf_strdup (status);
                if (!status_reply) {
                        gf_log ("", GF_LOG_ERROR,
                                "Out of memory");
                        ret = -1;
                        goto out;
                }

                ret = dict_set_dynstr (ctx, "status-reply",
                                       status_reply);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "failed to set pump status in ctx");
                        goto out;
                }

                ret = rb_destroy_maintainence_client (volinfo, src_brickinfo);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Failed to destroy maintainence "
                                "client");
                        goto out;
                }
        }

out:
        return ret;
}

static int
glusterd_op_replace_brick (gd1_mgmt_stage_op_req *req)
{
        int                                      ret = 0;
        dict_t                                  *dict = NULL;
        gf1_cli_replace_op                       replace_op;
        glusterd_volinfo_t                      *volinfo = NULL;
        char                                    *volname = NULL;
        xlator_t                                *this = NULL;
        glusterd_conf_t                         *priv = NULL;
        char                                    *src_brick = NULL;
        char                                    *dst_brick = NULL;
        glusterd_brickinfo_t                    *src_brickinfo = NULL;
        glusterd_brickinfo_t                    *dst_brickinfo = NULL;

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

        ret = glusterd_brickinfo_get (src_brick, volinfo, &src_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to get src-brickinfo");
                goto out;
        }

        ret = glusterd_brickinfo_from_brick (dst_brick, &dst_brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to get dst-brickinfo");
                goto out;
        }

        switch (replace_op) {
        case GF_REPLACE_OP_START:
                ret = rb_do_operation_start (volinfo, src_brickinfo, dst_brickinfo);
                break;
        case GF_REPLACE_OP_COMMIT:
                ret = rb_do_operation_commit (volinfo, src_brickinfo, dst_brickinfo);
                break;
        case GF_REPLACE_OP_PAUSE:
                ret = rb_do_operation_pause (volinfo, src_brickinfo, dst_brickinfo);
                break;
        case GF_REPLACE_OP_ABORT:
                ret = rb_do_operation_abort (volinfo, src_brickinfo, dst_brickinfo);
                break;
        case GF_REPLACE_OP_STATUS:
                ret = rb_do_operation_status (volinfo, src_brickinfo, dst_brickinfo,
                                              dict);
                break;
        default:
                ret = -1;
                goto out;
        }

        if (ret)
                goto out;

out:
        if (dict)
                dict_unref (dict);
        return ret;
}

static int
glusterd_op_remove_brick (gd1_mgmt_stage_op_req *req)
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
        gf_boolean_t                            glfs_stopped = _gf_false;
        int32_t                                 mybrick = 0;
        char                                    key[256] = {0,};
        char                                    *dup_brick = NULL;

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


        while ( i <= count) {
                snprintf (key, 256, "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to get %s", key);
                        goto out;
                }
                if (dup_brick)
                        GF_FREE (dup_brick);
                dup_brick = gf_strdup (brick);
                if (!dup_brick)
                        goto out;

                ret = glusterd_brickinfo_get (dup_brick, volinfo,  &brickinfo);
                if (ret)
                        goto out;


                ret = glusterd_resolve_brick (brickinfo);

                if (ret)
                        goto out;

                if ((!uuid_compare (brickinfo->uuid, priv->uuid)) &&
                    (GLUSTERD_STATUS_STARTED == volinfo->status)) {
                        gf_log ("", GF_LOG_NORMAL, "About to stop glusterfs"
                                " for brick %s:%s", brickinfo->hostname,
                                brickinfo->path);
                        ret = glusterd_volume_stop_glusterfs
                                                (volinfo, brickinfo, mybrick);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, "Unable to stop "
                                        "glusterfs, ret: %d", ret);
                                goto out;
                        }
                        glfs_stopped = _gf_true;
                        mybrick++;
                }

                glusterd_delete_volfile (volinfo, brickinfo);
                glusterd_store_delete_brick (volinfo, brickinfo);
                glusterd_brickinfo_delete (brickinfo);
                volinfo->brick_count--;

                i++;
        }

        ret = glusterd_create_volfiles (volinfo);
        if (ret)
                goto out;

        volinfo->version++;


        ret = glusterd_store_update_volume (volinfo);

        if (ret)
                goto out;

        ret = glusterd_volume_compute_cksum (volinfo);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_check_generate_start_nfs (volinfo);

out:
        if (dict)
                dict_unref (dict);
        if (dup_brick)
                GF_FREE (dup_brick);
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
glusterd_op_start_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        char                                    volname[1024] = {0,};
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        xlator_t                                *this = NULL;
        int32_t                                 mybrick = 0;

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
                                                (volinfo, brickinfo, mybrick);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, "Unable to start "
                                        "glusterfs, ret: %d", ret);
                                goto out;
                        }
                        mybrick++;
                }
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
        return ret;
}

static int
glusterd_op_log_filename (gd1_mgmt_stage_op_req *req)
{
        int                   ret                = 0;
        dict_t               *dict               = NULL;
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

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        dict = dict_new ();
        if (!dict) {
                gf_log ("", GF_LOG_ERROR, "ENOMEM, !dict");
                goto out;
        }

        ret = dict_unserialize (req->buf.buf_val, req->buf.buf_len, &dict);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to unserialize dict");
                goto out;
        }

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

        ret = stat (path, &stbuf);
        if (!S_ISDIR (stbuf.st_mode)) {
                ret = -1;
                gf_log ("", GF_LOG_ERROR, "not a directory");
                goto out;
        }

        ret = dict_get_str (dict, "brick", &brick);
        if (ret)
                goto out;

        if (!strchr (brick, ':'))
                brick = NULL;

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (brick && strcmp (brickinfo->path, brick))
                        continue;

                GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, exp_path);

                snprintf (logfile, PATH_MAX, "%s/%s.log", path, exp_path);

                if (brickinfo->logfile)
                        GF_FREE (brickinfo->logfile);
                brickinfo->logfile = gf_strdup (logfile);
        }

        ret = 0;

out:
        return ret;
}

static int
glusterd_op_log_rotate (gd1_mgmt_stage_op_req *req)
{
        int                   ret                = 0;
        dict_t               *dict               = NULL;
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

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        dict = dict_new ();
        if (!dict) {
                gf_log ("", GF_LOG_ERROR, "ENOMEM, !dict");
                goto out;
        }

        ret = dict_unserialize (req->buf.buf_val, req->buf.buf_len, &dict);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to unserialize dict");
                goto out;
        }

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

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (brick && strcmp (brickinfo->path, brick))
                        continue;

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
        }

        ret = 0;

out:
        return ret;
}

static int
glusterd_op_stop_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        int                                     flags = 0;
        char                                    *volname = NULL;
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        xlator_t                                *this = NULL;
        int32_t                                 mybrick = 0;
        dict_t                                  *dict = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = glusterd_op_stop_volume_args_get (req, dict, &volname, &flags);
        if (ret)
                goto out;

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (!uuid_compare (brickinfo->uuid, priv->uuid)) {
                        gf_log ("", GF_LOG_NORMAL, "About to stop glusterfs"
                                " for brick %s:%s", brickinfo->hostname,
                                brickinfo->path);
                        ret = glusterd_volume_stop_glusterfs
                                (volinfo, brickinfo, mybrick);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, "Unable to stop "
                                        "glusterfs, ret: %d", ret);
                                goto out;
                        }
                        mybrick++;
                }
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
        if (flags & GF_CLI_FLAG_OP_FORCE)
                ret = 0;
        if (dict)
                dict_unref (dict);
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

        /*ret = glusterd_unlock (priv->uuid);

        if (ret)
                goto out;
        */

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

        GF_ASSERT (event);

        opinfo.pending_count--;

        if (opinfo.pending_count)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_ALL_ACC, NULL);

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

        GF_ASSERT (event);

        opinfo.pending_count--;

        if (opinfo.pending_count)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_STAGE_ACC, NULL);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_ac_rcvd_commit_op_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;

        GF_ASSERT (event);

        opinfo.pending_count--;

        if (opinfo.pending_count)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_COMMIT_ACC, NULL);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

out:
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
glusterd_op_send_cli_response (int32_t op, int32_t op_ret,
                               int32_t op_errno, rpcsvc_request_t *req)
{
        int32_t         ret = -1;
        gd_serialize_t  sfunc = NULL;
        void            *cli_rsp = NULL;
        dict_t          *ctx = NULL;

        switch (op) {
                case GD_MGMT_CLI_CREATE_VOLUME:
                        {
                                gf1_cli_create_vol_rsp rsp = {0,};
                                rsp.op_ret = op_ret;
                                rsp.op_errno = op_errno;
                                rsp.volname = "";
				 rsp.op_errstr = "";
                                cli_rsp = &rsp;
                                sfunc = gf_xdr_serialize_cli_create_vol_rsp;
                                break;
                        }

                case GD_MGMT_CLI_START_VOLUME:
                        {
                                gf1_cli_start_vol_rsp rsp = {0,};
                                rsp.op_ret = op_ret;
                                rsp.op_errno = op_errno;
                                rsp.volname = "";
                                cli_rsp = &rsp;
                                sfunc = gf_xdr_serialize_cli_start_vol_rsp;
                                break;
                        }

                case GD_MGMT_CLI_STOP_VOLUME:
                        {
                                gf1_cli_stop_vol_rsp rsp = {0,};
                                rsp.op_ret = op_ret;
                                rsp.op_errno = op_errno;
                                rsp.volname = "";
                                cli_rsp = &rsp;
                                sfunc = gf_xdr_serialize_cli_stop_vol_rsp;
                                break;
                        }

                case GD_MGMT_CLI_DELETE_VOLUME:
                        {
                                gf1_cli_delete_vol_rsp rsp = {0,};
                                rsp.op_ret = op_ret;
                                rsp.op_errno = op_errno;
                                rsp.volname = "";
                                cli_rsp = &rsp;
                                sfunc = gf_xdr_serialize_cli_delete_vol_rsp;
                                break;
                        }

                case GD_MGMT_CLI_DEFRAG_VOLUME:
                        {
                                gf1_cli_defrag_vol_rsp rsp = {0,};
                                rsp.op_ret = op_ret;
                                rsp.op_errno = op_errno;
                                //rsp.volname = "";
                                cli_rsp = &rsp;
                                sfunc = gf_xdr_serialize_cli_defrag_vol_rsp;
                                break;
                        }

                case GD_MGMT_CLI_ADD_BRICK:
                        {
                                gf1_cli_add_brick_rsp rsp = {0,};
                                rsp.op_ret = op_ret;
                                rsp.op_errno = op_errno;
                                rsp.volname = "";
                                rsp.op_errstr = "";
                                cli_rsp = &rsp;
                                sfunc = gf_xdr_serialize_cli_add_brick_rsp;
                                break;
                        }

                case GD_MGMT_CLI_REMOVE_BRICK:
                        {
                                gf1_cli_remove_brick_rsp rsp = {0,};
                                rsp.op_ret = op_ret;
                                rsp.op_errno = op_errno;
                                rsp.volname = "";
                                rsp.op_errstr = "";
                                cli_rsp = &rsp;
                                sfunc = gf_xdr_serialize_cli_remove_brick_rsp;
                                break;
                        }

                case GD_MGMT_CLI_REPLACE_BRICK:
                        {
                                gf1_cli_replace_brick_rsp rsp = {0,};
                                ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
                                if (!ctx) {
                                        gf_log ("", GF_LOG_ERROR,
                                                "Operation Context is not present");
                                        ret = -1;
                                        goto out;
                                }
                                if (dict_get_str (ctx, "status-reply", &rsp.status))
                                        rsp.status = "";
                                rsp.op_ret = op_ret;
                                rsp.op_errno = op_errno;
                                rsp.volname = "";
                                cli_rsp = &rsp;
                                sfunc = gf_xdr_serialize_cli_replace_brick_rsp;
                                break;
                        }

                case GD_MGMT_CLI_LOG_FILENAME:
                        {
                                gf1_cli_log_filename_rsp rsp = {0,};
                                rsp.op_ret = op_ret;
                                rsp.op_errno = op_errno;
                                rsp.errstr = "";
                                cli_rsp = &rsp;
                                sfunc = gf_xdr_serialize_cli_log_filename_rsp;
                                break;
                        }
                case GD_MGMT_CLI_LOG_ROTATE:
                        {
                                gf1_cli_log_rotate_rsp rsp = {0,};
                                rsp.op_ret = op_ret;
                                rsp.op_errno = op_errno;
                                rsp.errstr = "";
                                cli_rsp = &rsp;
                                sfunc = gf_xdr_serialize_cli_log_rotate_rsp;
                                break;
                        }
        }

        ret = glusterd_submit_reply (req, cli_rsp, NULL, 0, NULL,
                                     sfunc);

        if (ret)
                goto out;

out:
        pthread_mutex_unlock (&opinfo.lock);
        gf_log ("", GF_LOG_NORMAL, "Returning %d", ret);
        return ret;
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


        opinfo.op_ret = 0;
        opinfo.op_errno = 0;

        op = glusterd_op_get_op ();

        if (op != -1) {
                glusterd_op_clear_pending_op (op);
                glusterd_op_clear_commit_op (op);
                glusterd_op_clear_op (op);
                glusterd_op_clear_ctx (op);
                glusterd_op_clear_ctx_free (op);
        }

out:
        pthread_mutex_unlock (&opinfo.lock);
        ret = glusterd_op_send_cli_response (cli_op, op_ret,
                                             op_errno, req);
        gf_log ("glusterd", GF_LOG_NORMAL, "Returning %d", ret);
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

                case GD_OP_REPLACE_BRICK:
                        ret = glusterd_op_stage_replace_brick (req);
                        break;

                case GD_OP_REMOVE_BRICK:
                        ret = glusterd_op_stage_remove_brick (req);
                        break;

                case GD_OP_LOG_FILENAME:
                        ret = glusterd_op_stage_log_filename (req);
                        break;

                case GD_OP_LOG_ROTATE:
                        ret = glusterd_op_stage_log_rotate (req);
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

                case GD_OP_REPLACE_BRICK:
                        ret = glusterd_op_replace_brick (req);
                        break;

                case GD_OP_REMOVE_BRICK:
                        ret = glusterd_op_remove_brick (req);
                        break;

                case GD_OP_LOG_FILENAME:
                        ret = glusterd_op_log_filename (req);
                        break;

                case GD_OP_LOG_ROTATE:
                        ret = glusterd_op_log_rotate (req);
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
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_START_UNLOCK
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

        gf_log ("glusterd", GF_LOG_NORMAL, "Enqueuing event: %d",
                        event->event);
        list_add_tail (&event->list, &gd_op_sm_queue);

out:
        return ret;
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
                                GF_FREE (event);
                                continue;
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
glusterd_op_set_cli_op (gf_mgmt_procnum op)
{

        int32_t         ret;

        ret = pthread_mutex_trylock (&opinfo.lock);

        if (ret)
                goto out;

        opinfo.cli_op = op;

out:
        gf_log ("", GF_LOG_NORMAL, "Returning %d", ret);
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
glusterd_op_clear_ctx (glusterd_op_t op)
{

        void    *ctx = NULL;

        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        ctx = opinfo.op_ctx[op];

        opinfo.op_ctx[op] = NULL;

        if (ctx && glusterd_op_get_ctx_free(op)) {
                switch (op) {
                case GD_OP_CREATE_VOLUME:
                case GD_OP_STOP_VOLUME:
                case GD_OP_ADD_BRICK:
                case GD_OP_REMOVE_BRICK:
                case GD_OP_REPLACE_BRICK:
                        dict_unref (ctx);
                        break;
                case GD_OP_DELETE_VOLUME:
                case GD_OP_START_VOLUME:
                        GF_FREE (ctx);
                        break;
                default:
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
        return 0;
}

int32_t
glusterd_opinfo_unlock(){
        return (pthread_mutex_unlock(&opinfo.lock));
}
