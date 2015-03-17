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
#include "glusterfs.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-geo-rep.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-svc-helper.h"
#include "glusterd-nfs-svc.h"
#include "glusterd-volgen.h"
#include "glusterd-messages.h"
#include "run.h"
#include "syscall.h"

#include <signal.h>

#define GLUSTERD_GET_RB_MNTPT(path, len, volinfo)                           \
        snprintf (path, len,                                                \
                  DEFAULT_VAR_RUN_DIRECTORY"/%s-"RB_CLIENT_MOUNTPOINT,      \
                  volinfo->volname);

extern uuid_t global_txn_id;

int
__glusterd_handle_replace_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        char                            *src_brick = NULL;
        char                            *dst_brick = NULL;
        int32_t                         op = 0;
        glusterd_op_t                   cli_op = GD_OP_REPLACE_BRICK;
        char                            *volname = NULL;
        char                            msg[2048] = {0,};
        xlator_t                        *this = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received from cli");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log (this->name, GF_LOG_INFO, "Received replace brick req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (msg, sizeof (msg), "Unable to decode the "
                                  "command");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (msg, sizeof (msg), "Could not get volume name");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", msg);
                goto out;
        }

        ret = dict_get_int32 (dict, "operation", &op);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "dict_get on operation failed");
                snprintf (msg, sizeof (msg), "Could not get operation");
                goto out;
        }

        ret = dict_get_str (dict, "src-brick", &src_brick);

        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get src brick");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", msg);
                goto out;
        }
        gf_msg_debug (this->name, 0,
                "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);

        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get dest brick");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", msg);
                goto out;
        }

        gf_msg_debug (this->name, 0, "dst brick=%s", dst_brick);
        gf_log (this->name, GF_LOG_INFO, "Received replace brick commit-force "
                "request operation");

        ret = glusterd_op_begin (req, GD_OP_REPLACE_BRICK, dict,
                                 msg, sizeof (msg));

out:
        free (cli_req.dict.dict_val);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                if (msg[0] == '\0')
                        snprintf (msg, sizeof (msg), "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, msg);
        }

        return ret;
}

int
glusterd_handle_replace_brick (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_replace_brick);
}

static int
glusterd_get_rb_dst_brickinfo (glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t **brickinfo)
{
        int32_t                 ret = -1;

        if (!volinfo || !brickinfo)
                goto out;

        *brickinfo = volinfo->rep_brick.dst_brick;

        ret = 0;

out:
        return ret;
}

int
glusterd_op_stage_replace_brick (dict_t *dict, char **op_errstr,
                                 dict_t *rsp_dict)
{
        int                                      ret                = 0;
        int32_t                                  port               = 0;
        char                                    *src_brick          = NULL;
        char                                    *dst_brick          = NULL;
        char                                    *volname            = NULL;
        char                                    *replace_op         = NULL;
        glusterd_volinfo_t                      *volinfo            = NULL;
        glusterd_brickinfo_t                    *src_brickinfo      = NULL;
        char                                    *host               = NULL;
        char                                    *path               = NULL;
        char                                     msg[2048]          = {0};
        char                                    *dup_dstbrick       = NULL;
        glusterd_peerinfo_t                     *peerinfo           = NULL;
        glusterd_brickinfo_t                    *dst_brickinfo      = NULL;
        gf_boolean_t                             enabled            = _gf_false;
        dict_t                                  *ctx                = NULL;
        glusterd_conf_t                         *priv               = NULL;
        char                                    *savetok            = NULL;
        char                                     pidfile[PATH_MAX]  = {0};
        char                                    *task_id_str        = NULL;
        xlator_t                                *this               = NULL;
        gf_boolean_t                             is_force           = _gf_false;
        gsync_status_param_t                     param              = {0,};

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "src-brick", &src_brick);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get src brick");
                goto out;
        }

        gf_msg_debug (this->name, 0, "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get dest brick");
                goto out;
        }

        gf_msg_debug (this->name, 0, "dst brick=%s", dst_brick);

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        ret = dict_get_str (dict, "operation", &replace_op);
        if (ret) {
                gf_msg_debug (this->name, 0,
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

        ret = glusterd_disallow_op_for_tier (volinfo, GD_OP_REPLACE_BRICK, -1);
        if (ret) {
                snprintf (msg, sizeof (msg), "Replace brick commands are not "
                          "supported on tiered volume %s", volname);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        if (!glusterd_store_is_valid_brickpath (volname, dst_brick) ||
                !glusterd_is_valid_volfpath (volname, dst_brick)) {
                snprintf (msg, sizeof (msg), "brick path %s is too "
                          "long.", dst_brick);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRKPATH_TOO_LONG, "%s", msg);
                *op_errstr = gf_strdup (msg);

                ret = -1;
                goto out;
        }

        /* If geo-rep is configured, for this volume, it should be stopped. */
        param.volinfo = volinfo;
        ret = glusterd_check_geo_rep_running (&param, op_errstr);
        if (ret || param.is_active) {
                ret = -1;
                goto out;
        }

        if (glusterd_is_defrag_on(volinfo)) {
                snprintf (msg, sizeof(msg), "Volume name %s rebalance is in "
                          "progress. Please retry after completion", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OIP_RETRY_LATER, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        ctx = glusterd_op_get_ctx();

        if (!strcmp(replace_op, "GF_REPLACE_OP_COMMIT_FORCE")) {
                is_force = _gf_true;
        } else {
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

        if (ctx) {
                if (!glusterd_is_fuse_available ()) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_RB_CMD_FAIL, "Unable to open /dev/"
                                "fuse (%s), replace-brick command failed",
                                strerror (errno));
                        snprintf (msg, sizeof(msg), "Fuse unavailable\n "
                                "Replace-brick failed");
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }
        }

        if (gf_is_local_addr (src_brickinfo->hostname)) {
                gf_msg_debug (this->name, 0,
                        "I AM THE SOURCE HOST");
                if (src_brickinfo->port && rsp_dict) {
                        ret = dict_set_int32 (rsp_dict, "src-brick-port",
                                              src_brickinfo->port);
                        if (ret) {
                                gf_msg_debug ("", 0,
                                        "Could not set src-brick-port=%d",
                                        src_brickinfo->port);
                        }
                }

                GLUSTERD_GET_BRICK_PIDFILE (pidfile, volinfo, src_brickinfo,
                                            priv);

        }

        dup_dstbrick = gf_strdup (dst_brick);
        if (!dup_dstbrick) {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Memory allocation failed");
                goto out;
        }
        host = strtok_r (dup_dstbrick, ":", &savetok);
        path = strtok_r (NULL, ":", &savetok);

        if (!host || !path) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BAD_FORMAT,
                        "dst brick %s is not of form <HOSTNAME>:<export-dir>",
                        dst_brick);
                ret = -1;
                goto out;
        }

        ret = glusterd_brickinfo_new_from_brick (dst_brick, &dst_brickinfo);
        if (ret)
                goto out;

        ret = glusterd_new_brick_validate (dst_brick, dst_brickinfo,
                                           msg, sizeof (msg));
        if (ret) {
                *op_errstr = gf_strdup (msg);
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "%s", *op_errstr);
                goto out;
        }

        if (!strcmp(replace_op, "GF_REPLACE_OP_COMMIT_FORCE")) {

                volinfo->rep_brick.src_brick = src_brickinfo;
                volinfo->rep_brick.dst_brick = dst_brickinfo;
        }

        if (glusterd_rb_check_bricks (volinfo, src_brickinfo, dst_brickinfo)) {

                ret = -1;
                *op_errstr = gf_strdup ("Incorrect source or "
                                        "destination brick");
                if (*op_errstr)
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_BRICK_NOT_FOUND, "%s", *op_errstr);
                goto out;
       }

        if (gf_is_local_addr (host)) {
                ret = glusterd_validate_and_create_brickpath (dst_brickinfo,
                                                  volinfo->volume_id,
                                                  op_errstr, is_force);
                if (ret)
                        goto out;
        }

        if (!gf_is_local_addr (host)) {
                rcu_read_lock ();

                peerinfo = glusterd_peerinfo_find (NULL, host);
                if (peerinfo == NULL) {
                        ret = -1;
                        snprintf (msg, sizeof (msg), "%s, is not a friend",
                                  host);
                        *op_errstr = gf_strdup (msg);

                } else if (!peerinfo->connected) {
                        snprintf (msg, sizeof (msg), "%s, is not connected at "
                                  "the moment", host);
                        *op_errstr = gf_strdup (msg);
                        ret = -1;

                } else if (GD_FRIEND_STATE_BEFRIENDED !=
                                peerinfo->state.state) {
                        snprintf (msg, sizeof (msg), "%s, is not befriended "
                                  "at the moment", host);
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                }
                rcu_read_unlock ();

                if (ret)
                        goto out;

        } else if (priv->op_version >= GD_OP_VERSION_3_6_0) {
                /* A bricks mount dir is required only by snapshots which were
                 * introduced in gluster-3.6.0
                 */
                ret = glusterd_get_brick_mount_dir (dst_brickinfo->path,
                                                    dst_brickinfo->hostname,
                                                    dst_brickinfo->mount_dir);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_MOUNTDIR_GET_FAIL,
                                "Failed to get brick mount_dir");
                        goto out;
                }

                ret = dict_set_dynstr_with_alloc (rsp_dict, "brick1.mount_dir",
                                                  dst_brickinfo->mount_dir);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to set brick1.mount_dir");
                        goto out;
                }

                ret = dict_set_int32 (rsp_dict, "brick_count", 1);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to set local_brick_count");
                        goto out;
                }
        }

        ret = 0;

out:
        GF_FREE (dup_dstbrick);
        gf_msg_debug (this->name, 0, "Returning %d", ret);

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

/* Set src-brick's port number to be used in the maintenance mount
 * after all commit acks are received.
 */
static int
rb_update_srcbrick_port (glusterd_volinfo_t *volinfo,
                         glusterd_brickinfo_t *src_brickinfo,
                         dict_t *rsp_dict, dict_t *req_dict, char *replace_op)
{
        xlator_t *this                  = NULL;
        dict_t   *ctx                   = NULL;
        int       ret                   = 0;
        int       dict_ret              = 0;
        int       src_port              = 0;
        char      brickname[PATH_MAX]   = {0,};

        this = THIS;
        GF_ASSERT (this);

        dict_ret = dict_get_int32 (req_dict, "src-brick-port", &src_port);
        if (src_port)
                src_brickinfo->port = src_port;

        if (gf_is_local_addr (src_brickinfo->hostname)) {
                gf_log (this->name, GF_LOG_INFO,
                        "adding src-brick port no");

                if (volinfo->transport_type == GF_TRANSPORT_RDMA) {
                        snprintf (brickname, sizeof(brickname), "%s.rdma",
                                  src_brickinfo->path);
                } else
                        snprintf (brickname, sizeof(brickname), "%s",
                                  src_brickinfo->path);

                src_brickinfo->port = pmap_registry_search (this,
                                      brickname, GF_PMAP_PORT_BRICKSERVER);
                if (!src_brickinfo->port) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Src brick port not available");
                        ret = -1;
                        goto out;
                }

                if (rsp_dict) {
                        ret = dict_set_int32 (rsp_dict, "src-brick-port",
                                              src_brickinfo->port);
                        if (ret) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Could not set src-brick port no");
                                goto out;
                        }
                }

                ctx = glusterd_op_get_ctx ();
                if (ctx) {
                        ret = dict_set_int32 (ctx, "src-brick-port",
                                              src_brickinfo->port);
                        if (ret) {
                                gf_log (this->name, GF_LOG_DEBUG,
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
                         dict_t *req_dict, char *replace_op)
{
        dict_t *ctx           = NULL;
        int     ret           = 0;
        int     dict_ret      = 0;
        int     dst_port      = 0;

        dict_ret = dict_get_int32 (req_dict, "dst-brick-port", &dst_port);
        if (!dict_ret)
                dst_brickinfo->port = dst_port;

        if (gf_is_local_addr (dst_brickinfo->hostname)) {
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

                ctx = glusterd_op_get_ctx ();
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
glusterd_op_perform_replace_brick (glusterd_volinfo_t  *volinfo,
                                   char *old_brick, char *new_brick,
                                   dict_t *dict)
{
        char                                    *brick_mount_dir = NULL;
        glusterd_brickinfo_t                    *old_brickinfo = NULL;
        glusterd_brickinfo_t                    *new_brickinfo = NULL;
        int32_t                                 ret = -1;
        xlator_t                                *this = NULL;
        glusterd_conf_t                         *conf = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (volinfo);

        conf = this->private;
        GF_ASSERT (conf);

        ret = glusterd_brickinfo_new_from_brick (new_brick,
                                                 &new_brickinfo);
        if (ret)
                goto out;

        ret = glusterd_resolve_brick (new_brickinfo);

        if (ret)
                goto out;

        ret = glusterd_volume_brickinfo_get_by_brick (old_brick,
                                                      volinfo, &old_brickinfo);
        if (ret)
                goto out;

        strncpy (new_brickinfo->brick_id, old_brickinfo->brick_id,
                 sizeof (new_brickinfo->brick_id));

        /* A bricks mount dir is required only by snapshots which were
         * introduced in gluster-3.6.0
         */
        if (conf->op_version >= GD_OP_VERSION_3_6_0) {
                ret = dict_get_str (dict, "brick1.mount_dir", &brick_mount_dir);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_BRICK_MOUNTDIR_GET_FAIL,
                                "brick1.mount_dir not present");
                        goto out;
                }
                strncpy (new_brickinfo->mount_dir, brick_mount_dir,
                         sizeof(new_brickinfo->mount_dir));
        }

        cds_list_add_tail (&new_brickinfo->brick_list,
                           &old_brickinfo->brick_list);

        volinfo->brick_count++;

        ret = glusterd_op_perform_remove_brick (volinfo, old_brick, 1, NULL);
        if (ret)
                goto out;

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                ret = glusterd_brick_start (volinfo, new_brickinfo, _gf_false);
                if (ret)
                        goto out;
        }

out:

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_op_replace_brick (dict_t *dict, dict_t *rsp_dict)
{
        int                                      ret           = 0;
        dict_t                                  *ctx           = NULL;
        char                                    *replace_op    = NULL;
        glusterd_volinfo_t                      *volinfo       = NULL;
        char                                    *volname       = NULL;
        xlator_t                                *this          = NULL;
        glusterd_conf_t                         *priv          = NULL;
        char                                    *src_brick     = NULL;
        char                                    *dst_brick     = NULL;
        glusterd_brickinfo_t                    *src_brickinfo = NULL;
        glusterd_brickinfo_t                    *dst_brickinfo = NULL;
        char                                    *task_id_str   = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "src-brick", &src_brick);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get src brick");
                goto out;
        }

        gf_msg_debug (this->name, 0, "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get dst brick");
                goto out;
        }

        gf_msg_debug (this->name, 0, "dst brick=%s", dst_brick);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        ret = dict_get_str (dict, "operation", &replace_op);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "dict_get on operation failed");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Unable to allocate memory");
                goto out;
        }

        ret = glusterd_volume_brickinfo_get_by_brick (src_brick, volinfo,
                                                      &src_brickinfo);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "Unable to get src-brickinfo");
                goto out;
        }


        ret = glusterd_get_rb_dst_brickinfo (volinfo, &dst_brickinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RB_BRICKINFO_GET_FAIL, "Unable to get "
                         "replace brick destination brickinfo");
                goto out;
        }

        ret = glusterd_resolve_brick (dst_brickinfo);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "Unable to resolve dst-brickinfo");
                goto out;
        }

        ret = rb_update_srcbrick_port (volinfo, src_brickinfo, rsp_dict,
                                       dict, replace_op);
        if (ret)
                goto out;

                /* Set task-id, if available, in op_ctx dict for*/
        if (is_origin_glusterd (dict)) {
                ctx = glusterd_op_get_ctx();
                if (!ctx) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_OPCTX_GET_FAIL, "Failed to "
                                "get op_ctx");
                        ret = -1;
                        goto out;
                }
        }
        ret = rb_update_dstbrick_port (dst_brickinfo, rsp_dict,
                                       dict, replace_op);
        if (ret)
                goto out;

        if (strcmp(replace_op, "GF_REPLACE_OP_COMMIT_FORCE")) {
                ret = -1;
                goto out;
        }

        if (gf_is_local_addr (dst_brickinfo->hostname)) {
                gf_log (this->name, GF_LOG_DEBUG, "I AM THE DESTINATION HOST");
                ret = rb_kill_destination_brick (volinfo, dst_brickinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_CRITICAL, 0,
                                GD_MSG_BRK_CLEANUP_FAIL,
                                "Unable to cleanup dst brick");
                        goto out;
                }
        }

        ret = glusterd_svcs_stop (volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Unable to stop nfs server, ret: %d", ret);
        }

        ret = glusterd_op_perform_replace_brick (volinfo, src_brick,
                                                 dst_brick, dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_BRICK_ADD_FAIL, "Unable to add dst-brick: "
                        "%s to volume: %s", dst_brick, volinfo->volname);
		(void) glusterd_svcs_manager (volinfo);
                goto out;
        }

        volinfo->rebal.defrag_status = 0;

        ret = glusterd_svcs_manager (volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_NFS_VOL_FILE_GEN_FAIL,
                        "Failed to generate nfs volume file");
        }


        ret = glusterd_fetchspec_notify (THIS);
        glusterd_brickinfo_delete (volinfo->rep_brick.dst_brick);
        volinfo->rep_brick.src_brick = NULL;
        volinfo->rep_brick.dst_brick = NULL;

        if (!ret)
		ret = glusterd_store_volinfo (volinfo,
                                              GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RBOP_STATE_STORE_FAIL, "Couldn't store"
                        " replace brick operation's state");

out:
        return ret;
}

void
glusterd_do_replace_brick (void *data)
{
        glusterd_volinfo_t     *volinfo = NULL;
        int32_t                 src_port = 0;
        int32_t                 dst_port = 0;
        int32_t                 ret      = 0;
        dict_t                 *dict    = NULL;
        char                   *src_brick = NULL;
        char                   *dst_brick = NULL;
        char                   *volname   = NULL;
        glusterd_brickinfo_t   *src_brickinfo = NULL;
        glusterd_brickinfo_t   *dst_brickinfo = NULL;
	glusterd_conf_t	       *priv = NULL;
        uuid_t                 *txn_id = NULL;
        xlator_t               *this = NULL;

        this = THIS;
	GF_ASSERT (this);
	priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (data);

        txn_id = &priv->global_txn_id;
        dict = data;

	if (priv->timer) {
		gf_timer_call_cancel (THIS->ctx, priv->timer);
		priv->timer = NULL;
                gf_msg_debug ("", 0,
                        "Cancelling timer thread");
	}

        gf_msg_debug (this->name, 0,
                "Replace brick operation detected");

        ret = dict_get_bin (dict, "transaction_id", (void **)&txn_id);
        gf_msg_debug (this->name, 0, "transaction ID = %s",
                uuid_utoa (*txn_id));

        ret = dict_get_str (dict, "src-brick", &src_brick);
        if (ret) {
                gf_msg ("", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get src brick");
                goto out;
        }

        gf_msg_debug (this->name, 0,
                "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);
        if (ret) {
                gf_msg ("", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get dst brick");
                goto out;
        }

        gf_msg_debug (this->name, 0,
                "dst brick=%s", dst_brick);

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_VOLINFO_GET_FAIL, "Unable to find volinfo");
                goto out;
        }

        ret = glusterd_volume_brickinfo_get_by_brick (src_brick, volinfo,
                                                      &src_brickinfo);
        if (ret) {
                gf_msg_debug (this->name, 0, "Unable to get src-brickinfo");
                goto out;
        }

        ret = glusterd_get_rb_dst_brickinfo (volinfo, &dst_brickinfo);
        if (!dst_brickinfo) {
                gf_msg_debug (this->name, 0, "Unable to get dst-brickinfo");
                goto out;
        }

        ret = glusterd_resolve_brick (dst_brickinfo);
        if (ret) {
                gf_msg_debug (this->name, 0, "Unable to resolve dst-brickinfo");
                goto out;
        }

        ret = dict_get_int32 (dict, "src-brick-port", &src_port);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get src-brick port");
                goto out;
        }

        ret = dict_get_int32 (dict, "dst-brick-port", &dst_port);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get dst-brick port");
        }

        dst_brickinfo->port = dst_port;
        src_brickinfo->port = src_port;

out:
        if (ret)
                ret = glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT,
                                                   txn_id, NULL);
        else
                ret = glusterd_op_sm_inject_event (GD_OP_EVENT_COMMIT_ACC,
                                                   txn_id, NULL);

        synclock_lock (&priv->big_lock);
        {
                glusterd_op_sm ();
        }
        synclock_unlock (&priv->big_lock);
}
