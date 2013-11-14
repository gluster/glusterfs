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
#include "glusterfs.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "run.h"
#include "syscall.h"

#include <signal.h>

#define GLUSTERD_GET_RB_MNTPT(path, len, volinfo)                           \
        snprintf (path, len,                                                \
                  DEFAULT_VAR_RUN_DIRECTORY"/%s-"RB_CLIENT_MOUNTPOINT,      \
                  volinfo->volname);


int
glusterd_get_replace_op_str (gf1_cli_replace_op op, char *op_str)
{
        int     ret = -1;

        if (!op_str)
                goto out;

        switch (op) {
                case GF_REPLACE_OP_START:
                        strcpy (op_str, "start");
                        break;
                case GF_REPLACE_OP_COMMIT:
                        strcpy (op_str, "commit");
                        break;
                case GF_REPLACE_OP_PAUSE:
                        strcpy (op_str, "pause");
                        break;
                case GF_REPLACE_OP_ABORT:
                        strcpy (op_str, "abort");
                        break;
                case GF_REPLACE_OP_STATUS:
                        strcpy (op_str, "status");
                        break;
                case GF_REPLACE_OP_COMMIT_FORCE:
                        strcpy (op_str, "commit-force");
                        break;
                default:
                        strcpy (op_str, "unknown");
                        break;
        }

        ret = 0;
out:
        return ret;
}

int
__glusterd_handle_replace_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        char                            *src_brick = NULL;
        char                            *dst_brick = NULL;
        int32_t                         op = 0;
        char                            operation[256];
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
                        gf_log (this->name, GF_LOG_ERROR,
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
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        ret = dict_get_int32 (dict, "operation", &op);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dict_get on operation failed");
                snprintf (msg, sizeof (msg), "Could not get operation");
                goto out;
        }

        ret = dict_get_str (dict, "src-brick", &src_brick);

        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get src brick");
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                goto out;
        }
        gf_log (this->name, GF_LOG_DEBUG,
                "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);

        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get dest brick");
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        (void) glusterd_get_replace_op_str (op, operation);
        gf_log (this->name, GF_LOG_DEBUG, "dst brick=%s", dst_brick);
        gf_log (this->name, GF_LOG_INFO, "Received replace brick %s request",
                operation);

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
        int                                      ret           = 0;
        int32_t                                  port          = 0;
        char                                    *src_brick     = NULL;
        char                                    *dst_brick     = NULL;
        char                                    *volname       = NULL;
        int                                      replace_op    = 0;
        glusterd_volinfo_t                      *volinfo       = NULL;
        glusterd_brickinfo_t                    *src_brickinfo = NULL;
        char                                    *host          = NULL;
        char                                    *path          = NULL;
        char                                     msg[2048]     = {0};
        char                                    *dup_dstbrick  = NULL;
        glusterd_peerinfo_t                     *peerinfo = NULL;
        glusterd_brickinfo_t                    *dst_brickinfo = NULL;
        gf_boolean_t                             is_run        = _gf_false;
        dict_t                                  *ctx           = NULL;
        glusterd_conf_t                         *priv          = NULL;
        char                                    *savetok       = NULL;
        char                                     pidfile[PATH_MAX] = {0};
        char                                    *task_id_str = NULL;
        xlator_t                                *this = NULL;
        gf_boolean_t                            is_force = _gf_false;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "src-brick", &src_brick);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get src brick");
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG, "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get dest brick");
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG, "dst brick=%s", dst_brick);

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = dict_get_int32 (dict, "operation", (int32_t *)&replace_op);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
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
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);

                ret = -1;
                goto out;
        }

        ret = glusterd_check_gsync_running (volinfo, &is_run);
        if (ret && (is_run == _gf_false))
                gf_log (this->name, GF_LOG_WARNING, "Unable to get the status"
                                " of active "GEOREP" session");
        if (is_run) {
                gf_log (this->name, GF_LOG_WARNING, GEOREP" sessions active"
                        "for the volume %s ", volname);
                snprintf (msg, sizeof(msg), GEOREP" sessions are active "
                                "for the volume %s.\nStop "GEOREP " sessions "
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
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        ctx = glusterd_op_get_ctx();

        switch (replace_op) {
        case GF_REPLACE_OP_START:
                if (glusterd_is_rb_started (volinfo)) {
                        snprintf (msg, sizeof (msg), "Replace brick is already "
                                  "started for volume");
                        gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }
                if (is_origin_glusterd ()) {
                        if (!ctx) {
                                ret = -1;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get op_ctx");
                                goto out;
                        }

                        ret = glusterd_generate_and_set_task_id
                                (ctx, GF_REPLACE_BRICK_TID_KEY);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to generate task-id");
                                goto out;
                        }

                } else {
                        ret = dict_get_str (dict, GF_REPLACE_BRICK_TID_KEY,
                                            &task_id_str);
                        if (ret) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Missing replace-brick-id");
                                ret = 0;
                        }
                }
                is_force = dict_get_str_boolean (dict, "force", _gf_false);

                break;

        case GF_REPLACE_OP_PAUSE:
                if (glusterd_is_rb_paused (volinfo)) {
                        gf_log (this->name, GF_LOG_ERROR, "Replace brick is "
                                "already paused for volume ");
                        ret = -1;
                        goto out;
                } else if (!glusterd_is_rb_started(volinfo)) {
                        gf_log (this->name, GF_LOG_ERROR, "Replace brick is not"
                                " started for volume ");
                        ret = -1;
                        goto out;
                }
                break;

        case GF_REPLACE_OP_ABORT:
                if (!glusterd_is_rb_ongoing (volinfo)) {
                        gf_log (this->name, GF_LOG_ERROR, "Replace brick is not"
                                " started or paused for volume ");
                        ret = -1;
                        goto out;
                }
                break;

        case GF_REPLACE_OP_COMMIT:
                if (!glusterd_is_rb_ongoing (volinfo)) {
                        gf_log (this->name, GF_LOG_ERROR, "Replace brick is not "
                                "started for volume ");
                        ret = -1;
                        goto out;
                }
                break;

        case GF_REPLACE_OP_COMMIT_FORCE:
                is_force = _gf_true;
                break;

        case GF_REPLACE_OP_STATUS:

                if (glusterd_is_rb_ongoing (volinfo) == _gf_false) {
                        ret = gf_asprintf (op_errstr, "replace-brick not"
                                           " started on volume %s",
                                           volinfo->volname);
                        if (ret < 0) {
                                *op_errstr = NULL;
                                goto out;
                        }

                        gf_log (this->name, GF_LOG_ERROR, "%s", *op_errstr);
                        ret = -1;
                        goto out;
                }
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

        if (ctx) {
                if (!glusterd_is_fuse_available ()) {
                        gf_log (this->name, GF_LOG_ERROR, "Unable to open /dev/"
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
                gf_log (this->name, GF_LOG_DEBUG,
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

                GLUSTERD_GET_BRICK_PIDFILE (pidfile, volinfo, src_brickinfo,
                                            priv);
                if ((replace_op != GF_REPLACE_OP_COMMIT_FORCE) &&
                    !gf_is_service_running (pidfile, NULL)) {
                        snprintf(msg, sizeof(msg), "Source brick %s:%s "
                                 "is not online.", src_brickinfo->hostname,
                                 src_brickinfo->path);
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }


        }

        dup_dstbrick = gf_strdup (dst_brick);
        if (!dup_dstbrick) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "Memory allocation failed");
                goto out;
        }
        host = strtok_r (dup_dstbrick, ":", &savetok);
        path = strtok_r (NULL, ":", &savetok);

        if (!host || !path) {
                gf_log (this->name, GF_LOG_ERROR,
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

       if (!glusterd_is_rb_ongoing (volinfo) &&
            (replace_op == GF_REPLACE_OP_START ||
             replace_op == GF_REPLACE_OP_COMMIT_FORCE)) {

                volinfo->rep_brick.src_brick = src_brickinfo;
                volinfo->rep_brick.dst_brick = dst_brickinfo;
        }

        if (glusterd_rb_check_bricks (volinfo, src_brickinfo, dst_brickinfo)) {

                ret = -1;
                *op_errstr = gf_strdup ("Incorrect source or "
                                        "destination brick");
                if (*op_errstr)
                        gf_log (this->name, GF_LOG_ERROR, "%s", *op_errstr);
                goto out;
       }

        if (!glusterd_is_rb_ongoing (volinfo) &&
            gf_is_local_addr (host)) {
                ret = glusterd_validate_and_create_brickpath (dst_brickinfo,
                                                  volinfo->volume_id,
                                                  op_errstr, is_force);
                if (ret)
                        goto out;
        }

        if (!gf_is_local_addr (host)) {
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

        if (replace_op == GF_REPLACE_OP_START &&
            gf_is_local_addr (volinfo->rep_brick.dst_brick->hostname)) {
                port = pmap_registry_alloc (THIS);
                if (!port) {
                        gf_log (THIS->name, GF_LOG_CRITICAL,
                                "No free ports available");
                        ret = -1;
                        goto out;
                }

                ctx = glusterd_op_get_ctx();
                ret = dict_set_int32 ((ctx)?ctx:rsp_dict, "dst-brick-port",
                                      port);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR, "Failed to set dst "
                                "brick port");
                        goto out;
                }
                volinfo->rep_brick.dst_brick->port = port;
        }

        ret = 0;

out:
        GF_FREE (dup_dstbrick);
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
rb_set_mntfd (int mntfd)
{
        int     ret = -1;
        dict_t *ctx = NULL;

        ctx = glusterd_op_get_ctx ();
        if (!ctx) {
                gf_log (THIS->name, GF_LOG_CRITICAL, "Failed to get op ctx");
                goto out;
        }
        ret = dict_set_int32 (ctx, "mntfd", mntfd);
        if (ret)
                gf_log (THIS->name, GF_LOG_DEBUG, "Failed to set mnt fd "
                        "in op ctx");
out:
        return ret;
}

static int
rb_get_mntfd (int *mntfd)
{
        int     ret = -1;
        dict_t *ctx = NULL;

        ctx = glusterd_op_get_ctx ();
        if (!ctx) {
                gf_log (THIS->name, GF_LOG_CRITICAL, "Failed to get op ctx");
                goto out;
        }
        ret = dict_get_int32 (ctx, "mntfd", mntfd);
        if (ret)
                gf_log (THIS->name, GF_LOG_DEBUG, "Failed to get mnt fd "
                        "from op ctx");
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

        dict_del (dict, "enable-pump");

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

        ret = glusterd_nfs_server_stop (volinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to stop nfs, ret: %d",
                        ret);
        }

        ret = glusterd_volume_stop_glusterfs (volinfo, src_brickinfo,
                                              _gf_false);
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
        ret = glusterd_volume_start_glusterfs (volinfo, src_brickinfo,
                                              _gf_false);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to start "
                        "glusterfs, ret: %d", ret);
                goto out;
        }

out:
        ret = glusterd_nfs_server_start (volinfo);
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
                       const char *xattr_key, const char *value)
{
        int             ret   = -1;
        int             mntfd = -1;

        ret = rb_get_mntfd (&mntfd);
        if (ret)
                goto out;

        ret = sys_fsetxattr (mntfd, xattr_key, value, strlen (value) + 1, 0);
        if (ret)
                gf_log (THIS->name, GF_LOG_DEBUG, "setxattr on key: "
                        "%s, reason: %s", xattr_key, strerror (errno));

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

        port = brickinfo->port;
        GF_ASSERT (port);

        runinit (&runner);
        runner_add_arg (&runner, SBIN_DIR"/glusterfs");
        runner_argprintf (&runner, "-f" "%s/vols/%s/"RB_DSTBRICKVOL_FILENAME,
                          priv->workdir, volinfo->volname);
        runner_argprintf (&runner, "-p" "%s/vols/%s/"RB_DSTBRICK_PIDFILE,
                          priv->workdir, volinfo->volname);
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "src-server.listen-port=%d", port);
        if (volinfo->memory_accounting)
                runner_add_arg (&runner, "--mem-accounting");

        ret = runner_run_nowait (&runner);
        if (ret) {
                pmap_registry_remove (THIS, 0, brickinfo->path,
                                      GF_PMAP_PORT_BRICKSERVER, NULL);
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
        xlator_t           *this            = NULL;
        glusterd_conf_t    *priv            = NULL;
        runner_t            runner          = {0,};
        struct stat         buf             = {0,};
        char                mntpt[PATH_MAX] = {0,};
        int                 mntfd           = -1;
        int                 ret             = -1;

        this = THIS;
        priv = this->private;

        GLUSTERD_GET_RB_MNTPT (mntpt, sizeof (mntpt), volinfo);
        runinit (&runner);
        runner_add_arg (&runner, SBIN_DIR"/glusterfs");
        runner_argprintf (&runner, "-f" "%s/vols/%s/"RB_CLIENTVOL_FILENAME,
                          priv->workdir, volinfo->volname);
        runner_add_arg (&runner, mntpt);
        if (volinfo->memory_accounting)
                runner_add_arg (&runner, "--mem-accounting");

        ret = runner_run_reuse (&runner);
        if (ret) {
                runner_log (&runner, this->name, GF_LOG_DEBUG,
                            "Could not start glusterfs");
                runner_end (&runner);
                goto out;
        } else {
                runner_log (&runner, this->name, GF_LOG_DEBUG,
                            "Successfully started  glusterfs");
                runner_end (&runner);
        }

        ret = stat (mntpt, &buf);
        if (ret) {
                 gf_log (this->name, GF_LOG_DEBUG, "stat on mount point %s "
                         "failed", mntpt);
                 goto out;
        }

        mntfd = open (mntpt, O_DIRECTORY);
        if (mntfd == -1)
                goto out;

        ret = rb_set_mntfd (mntfd);
        if (ret)
                goto out;

        runinit (&runner);
        runner_add_args (&runner, "/bin/umount", "-l", mntpt, NULL);
        ret = runner_run_reuse (&runner);
        if (ret) {
                runner_log (&runner, this->name, GF_LOG_DEBUG,
                            "Lazy unmount failed on maintenance client");
                runner_end (&runner);
                goto out;
        } else {
                runner_log (&runner, this->name, GF_LOG_DEBUG,
                            "Successfully unmounted  maintenance client");
                runner_end (&runner);
        }


out:

        return ret;
}

static const char *client_volfile_str =  "volume mnt-client\n"
        " type protocol/client\n"
        " option remote-host %s\n"
        " option remote-subvolume %s\n"
        " option remote-port %d\n"
        " option transport-type %s\n"
        " option username %s\n"
        " option password %s\n"
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
        xlator_t         *this                  = NULL;
        FILE             *file                  = NULL;
        char              filename[PATH_MAX]    = {0, };
        int               ret                   = -1;
        int               fd                    = -1;
        char             *ttype                 = NULL;

        this = THIS;
        priv = this->private;

        gf_log (this->name, GF_LOG_DEBUG, "Creating volfile");

        snprintf (filename, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_CLIENTVOL_FILENAME);

        fd = open (filename, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
        if (fd < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s", strerror (errno));
                goto out;
        }
        close (fd);

        file = fopen (filename, "w+");
        if (!file) {
                gf_log (this->name, GF_LOG_DEBUG,
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
                 src_brickinfo->path,
                 src_brickinfo->port, ttype,
                 glusterd_auth_get_username (volinfo),
                 glusterd_auth_get_password (volinfo));

        fclose (file);
        GF_FREE (ttype);

        ret = 0;

out:
        return ret;
}

static const char *dst_brick_volfile_str = "volume src-posix\n"
        " type storage/posix\n"
        " option directory %s\n"
        " option volume-id %s\n"
        "end-volume\n"
        "volume %s\n"
        " type features/locks\n"
        " subvolumes src-posix\n"
        "end-volume\n"
        "volume src-server\n"
        " type protocol/server\n"
        " option auth.login.%s.allow %s\n"
        " option auth.login.%s.password %s\n"
        " option auth.addr.%s.allow *\n"
        " option transport-type %s\n"
        " subvolumes %s\n"
        "end-volume\n";

static int
rb_generate_dst_brick_volfile (glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t *dst_brickinfo)
{
        glusterd_conf_t    *priv                = NULL;
        xlator_t           *this                = NULL;
        FILE               *file                = NULL;
        char                filename[PATH_MAX]  = {0, };
        int                 ret                 = -1;
        int                 fd                  = -1;
        char               *trans_type          = NULL;

        this = THIS;
        priv = this->private;

        gf_log (this->name, GF_LOG_DEBUG,
                "Creating volfile");

        snprintf (filename, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_DSTBRICKVOL_FILENAME);

        fd = creat (filename, S_IRUSR | S_IWUSR);
        if (fd < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s", strerror (errno));
                goto out;
        }
        close (fd);

        file = fopen (filename, "w+");
        if (!file) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Open of volfile failed");
                ret = -1;
                goto out;
        }

	trans_type = glusterd_get_trans_type_rb (volinfo->transport_type);
	if (NULL == trans_type){
		ret = -1;
		goto out;
	}

        fprintf (file, dst_brick_volfile_str,
                 dst_brickinfo->path,
                 uuid_utoa (volinfo->volume_id),
                 dst_brickinfo->path,
                 dst_brickinfo->path,
                 glusterd_auth_get_username (volinfo),
                 glusterd_auth_get_username (volinfo),
                 glusterd_auth_get_password (volinfo),
                 dst_brickinfo->path,
                 trans_type,
                 dst_brickinfo->path);

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
        char             mntpt[PATH_MAX]        = {0,};
        int              ret                    = -1;

        GLUSTERD_GET_RB_MNTPT (mntpt, sizeof (mntpt), volinfo);
        ret = mkdir (mntpt, 0777);
        if (ret && (errno != EEXIST)) {
                gf_log ("", GF_LOG_DEBUG, "mkdir failed, due to %s",
                        strerror (errno));
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
        char             mntpt[PATH_MAX] = {0,};
        int              ret                        = -1;

        GLUSTERD_GET_RB_MNTPT (mntpt, sizeof (mntpt), volinfo);
        ret = rmdir (mntpt);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "rmdir failed, reason: %s",
                        strerror (errno));
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
        xlator_t         *this                        = NULL;
        glusterd_conf_t  *priv                        = NULL;
        char              volfile[PATH_MAX]           = {0,};
        int               ret                         = -1;
        int               mntfd                       = -1;

        this = THIS;
        priv = this->private;

        ret = rb_get_mntfd (&mntfd);
        if (ret)
                goto out;

        ret = close (mntfd);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "Failed to close mount "
                        "point directory");
                goto out;
        }

        ret = rb_mountpoint_rmdir (volinfo, src_brickinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "rmdir of mountpoint "
                        "failed");
                goto out;
        }

        snprintf (volfile, PATH_MAX, "%s/vols/%s/%s", priv->workdir,
                  volinfo->volname, RB_CLIENTVOL_FILENAME);

        ret = unlink (volfile);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "unlink of %s failed, reason: %s",
                        volfile, strerror (errno));
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
rb_get_xattr_command (glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *src_brickinfo,
                      glusterd_brickinfo_t *dst_brickinfo,
                      const char *xattr_key,
                      char *value)
{
        int             ret    = -1;
        int             mntfd  = -1;

        ret = rb_get_mntfd (&mntfd);
        if (ret)
                goto out;

        ret = sys_fgetxattr (mntfd, xattr_key, value, 8192);

        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_DEBUG, "getxattr on key: %s "
                        "failed, reason: %s", xattr_key, strerror (errno));
                goto out;
        }

        ret = 0;
out:
        return ret;
}

static int
rb_send_cmd (glusterd_volinfo_t *volinfo,
             glusterd_brickinfo_t *src,
             glusterd_brickinfo_t *dst,
             gf1_cli_replace_op op)
{
        char         start_value[8192]          = {0,};
        char         status_str[8192]           = {0,};
        char        *status_reply               = NULL;
        char        *tmp                        = NULL;
        char        *save_ptr                   = NULL;
        char         filename[PATH_MAX]         = {0,};
        char        *current_file               = NULL;
        uint64_t     files                      = 0;
        int          status                     = 0;
        dict_t      *ctx                        = NULL;
        int          ret                        = 0;

        GF_ASSERT (volinfo);
        GF_ASSERT (src);
        GF_ASSERT (dst);
        GF_ASSERT ((op > GF_REPLACE_OP_NONE)
                   && (op <= GF_REPLACE_OP_COMMIT_FORCE));

        switch (op) {
                case GF_REPLACE_OP_START:
                {
                        snprintf (start_value, sizeof (start_value),
                                  "%s:%s:%d", dst->hostname, dst->path,
                                  dst->port);
                        ret = rb_send_xattr_command (volinfo, src, dst,
                                                     RB_PUMP_CMD_START,
                                                     start_value);
                }
                break;
                case GF_REPLACE_OP_PAUSE:
                {
                        ret = rb_send_xattr_command (volinfo, src, dst,
                                                     RB_PUMP_CMD_PAUSE,
                                                     RB_PUMP_DEF_ARG);
                }
                break;
                case GF_REPLACE_OP_ABORT:
                {
                        ret = rb_send_xattr_command (volinfo, src, dst,
                                                     RB_PUMP_CMD_ABORT,
                                                     RB_PUMP_DEF_ARG);
                }
                break;
                case GF_REPLACE_OP_COMMIT:
                {
                        ret = rb_send_xattr_command (volinfo, src, dst,
                                                     RB_PUMP_CMD_COMMIT,
                                                     RB_PUMP_DEF_ARG);
                }
                break;
                case GF_REPLACE_OP_STATUS:
                {
                        ret = rb_get_xattr_command (volinfo, src, dst,
                                                    RB_PUMP_CMD_STATUS,
                                                    status_str);
                        if (ret)
                                goto out;

                        ctx = glusterd_op_get_ctx ();
                        GF_ASSERT (ctx);
                        if (!ctx) {
                                ret = -1;
                                gf_log (THIS->name, GF_LOG_CRITICAL,
                                        "ctx is not present.");
                                goto out;
                        }

                        /* Split status reply into different parts */
                        tmp = strtok_r (status_str, ":", &save_ptr);
                        if (!tmp) {
                                ret = -1;
                                gf_log (THIS->name, GF_LOG_ERROR,
                                        "Couldn't tokenize status string");
                                goto out;
                        }
                        sscanf (tmp, "status=%d", &status);
                        ret = dict_set_int32 (ctx, "status", status);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't "
                                        "set rb status in context");
                                goto out;
                        }

                        tmp = NULL;
                        tmp = strtok_r (NULL, ":", &save_ptr);
                        if (!tmp) {
                                ret = -1;
                                gf_log (THIS->name, GF_LOG_ERROR,
                                        "Couldn't tokenize status string");
                                goto out;
                        }
                        sscanf (tmp, "no_of_files=%"SCNu64, &files);
                        ret = dict_set_uint64 (ctx, "files", files);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't "
                                        "set rb files in context");
                                goto out;
                        }

                        if (status == 0) {
                                tmp = NULL;
                                tmp = strtok_r (NULL, ":", &save_ptr);
                                if (!tmp) {
                                        ret = -1;
                                        gf_log (THIS->name, GF_LOG_ERROR,
                                                "Couldn't tokenize status "
                                                "string");
                                        goto out;
                                }
                                sscanf (tmp, "current_file=%s", filename);
                                current_file = gf_strdup (filename);
                                ret = dict_set_dynstr (ctx, "current_file",
                                                       current_file);
                                if (ret) {
                                        GF_FREE (current_file);
                                        gf_log (THIS->name, GF_LOG_ERROR,
                                                "Couldn't set rb current file "
                                                "in context");
                                        goto out;
                                }
                        }
                        if (status) {
                                ret = gf_asprintf (&status_reply,
                                                  "Number of files migrated = %"
                                                  PRIu64"\tMigration complete",
                                                  files);
                        } else {
                                ret = gf_asprintf (&status_reply,
                                                  "Number of files migrated = %"
                                                  PRIu64"\tCurrent file = %s",
                                                  files, filename);
                        }
                        if (ret == -1) {
                                gf_log (THIS->name, GF_LOG_ERROR,
                                        "Failed to create status_reply string");
                                goto out;
                        }
                        ret = dict_set_dynstr (ctx, "status-reply",
                                               status_reply);
                        if (ret) {
                                GF_FREE (status_reply);
                                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't "
                                        "set rb status response in context.");
                                goto out;
                        }
                }
                break;
                default:
                {
                        GF_ASSERT (0);
                        ret = -1;
                        gf_log (THIS->name, GF_LOG_CRITICAL, "Invalid replace"
                                " brick subcommand.");
                }
                break;
        }
out:
        return ret;
}

static int
rb_do_operation (glusterd_volinfo_t *volinfo,
                 glusterd_brickinfo_t *src_brickinfo,
                 glusterd_brickinfo_t *dst_brickinfo,
                 gf1_cli_replace_op op)
{

        int             ret             = -1;
        char            op_str[256]     = {0, };
        xlator_t        *this           = NULL;

        this = THIS;

        ret = rb_spawn_maintenance_client (volinfo, src_brickinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "Could not spawn "
                        "maintenance client");
                goto umount;
        }

        ret = rb_send_cmd (volinfo, src_brickinfo, dst_brickinfo, op);
        if (ret) {
                (void) glusterd_get_replace_op_str (op, op_str);
                gf_log (this->name, GF_LOG_DEBUG, "Sending replace-brick "
                        "sub-command %s failed.", op_str);
        }

umount:
        if (rb_destroy_maintenance_client (volinfo, src_brickinfo))
                gf_log (this->name, GF_LOG_DEBUG, "Failed to destroy "
                        "maintenance client");

         return ret;
}

/* Set src-brick's port number to be used in the maintenance mount
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

        dict_ret = dict_get_int32 (req_dict, "src-brick-port", &src_port);
        if (src_port)
                src_brickinfo->port = src_port;

        if (gf_is_local_addr (src_brickinfo->hostname)) {
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

                ctx = glusterd_op_get_ctx ();
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
                                   char *old_brick, char  *new_brick)
{
        glusterd_brickinfo_t                    *old_brickinfo = NULL;
        glusterd_brickinfo_t                    *new_brickinfo = NULL;
        int32_t                                 ret = -1;

        GF_ASSERT (volinfo);

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

        list_add_tail (&new_brickinfo->brick_list,
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
        char                                    *task_id_str = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "src-brick", &src_brick);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get src brick");
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG, "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get dst brick");
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG, "dst brick=%s", dst_brick);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
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
                gf_log (this->name, GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = glusterd_volume_brickinfo_get_by_brick (src_brick, volinfo,
                                                      &src_brickinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Unable to get src-brickinfo");
                goto out;
        }


        ret = glusterd_get_rb_dst_brickinfo (volinfo, &dst_brickinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get "
                         "replace brick destination brickinfo");
                goto out;
        }

        ret = glusterd_resolve_brick (dst_brickinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Unable to resolve dst-brickinfo");
                goto out;
        }

        ret = rb_update_srcbrick_port (src_brickinfo, rsp_dict,
                                       dict, replace_op);
        if (ret)
                goto out;


	if ((GF_REPLACE_OP_START != replace_op)) {

                /* Set task-id, if available, in op_ctx dict for operations
                 * other than start
                 */
                if  (is_origin_glusterd ()) {
                        ctx = glusterd_op_get_ctx();
                        if (!ctx) {
                                gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                        "get op_ctx");
                                ret = -1;
                                goto out;
                        }
                        if (!uuid_is_null (volinfo->rep_brick.rb_id)) {
                                ret = glusterd_copy_uuid_to_dict
                                        (volinfo->rep_brick.rb_id, ctx,
                                         GF_REPLACE_BRICK_TID_KEY);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Failed to set "
                                                "replace-brick-id");
                                        goto out;
                                }
                        }
                }
	}
        ret = rb_update_dstbrick_port (dst_brickinfo, rsp_dict,
                                       dict, replace_op);
        if (ret)
                goto out;

        switch (replace_op) {
        case GF_REPLACE_OP_START:
        {
                ret = dict_get_str (dict, GF_REPLACE_BRICK_TID_KEY, &task_id_str);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Missing replace-brick-id");
                        ret = 0;
                } else {
                        uuid_parse (task_id_str, volinfo->rep_brick.rb_id);
                }

                if (gf_is_local_addr (dst_brickinfo->hostname)) {
                        gf_log (this->name, GF_LOG_INFO,
                                "I AM THE DESTINATION HOST");
                        if (!glusterd_is_rb_paused (volinfo)) {
                                ret = rb_spawn_destination_brick
                                        (volinfo, dst_brickinfo);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_DEBUG,
                                                "Failed to spawn destination "
                                                "brick");
                                        goto out;
                                }
                        } else {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Replace brick is already started=> no "
                                        "need to restart dst brick ");
                        }
		}


		if (gf_is_local_addr (src_brickinfo->hostname)) {
		        ret = rb_src_brick_restart (volinfo, src_brickinfo,
				                    1);
		        if (ret) {
				gf_log (this->name, GF_LOG_DEBUG,
	                                "Could not restart src-brick");
			        goto out;
			}
		}

		if (gf_is_local_addr (dst_brickinfo->hostname)) {
			gf_log (this->name, GF_LOG_INFO,
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
        {
                ctx = glusterd_op_get_ctx ();
                if (ctx) {
                        ret = rb_do_operation (volinfo, src_brickinfo,
                                               dst_brickinfo,
                                               GF_REPLACE_OP_COMMIT);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Commit operation failed");
                                goto out;
                        }
                }
        }
                /* fall through */
        case GF_REPLACE_OP_COMMIT_FORCE:
        {
                if (gf_is_local_addr (dst_brickinfo->hostname)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "I AM THE DESTINATION HOST");
                        ret = rb_kill_destination_brick (volinfo,
                                                         dst_brickinfo);
                        if (ret) {
                                gf_log (this->name, GF_LOG_CRITICAL,
                                        "Unable to cleanup dst brick");
                                goto out;
                        }
                }

                ret = glusterd_nodesvcs_stop (volinfo);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to stop nfs server, ret: %d", ret);
                }

		ret = glusterd_op_perform_replace_brick (volinfo, src_brick,
							 dst_brick);
		if (ret) {
			gf_log (this->name, GF_LOG_CRITICAL, "Unable to add "
				"dst-brick: %s to volume: %s", dst_brick,
                                volinfo->volname);
		        (void) glusterd_nodesvcs_handle_graph_change (volinfo);
			goto out;
		}

		volinfo->rebal.defrag_status = 0;

		ret = glusterd_nodesvcs_handle_graph_change (volinfo);
		if (ret) {
                        gf_log (this->name, GF_LOG_CRITICAL,
                                "Failed to generate nfs volume file");
		}


		ret = glusterd_fetchspec_notify (THIS);
                glusterd_set_rb_status (volinfo, GF_RB_STATUS_NONE);
                glusterd_brickinfo_delete (volinfo->rep_brick.dst_brick);
                volinfo->rep_brick.src_brick = NULL;
                volinfo->rep_brick.dst_brick = NULL;
                uuid_clear (volinfo->rep_brick.rb_id);
        }
        break;

        case GF_REPLACE_OP_PAUSE:
        {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Received pause - doing nothing");
                ctx = glusterd_op_get_ctx ();
                if (ctx) {
                        ret = rb_do_operation (volinfo, src_brickinfo,
                                               dst_brickinfo,
                                               GF_REPLACE_OP_PAUSE);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Pause operation failed");
                                goto out;
                        }
                }

                glusterd_set_rb_status (volinfo, GF_RB_STATUS_PAUSED);
        }
                break;

        case GF_REPLACE_OP_ABORT:
        {

                ctx = glusterd_op_get_ctx ();
                if (ctx) {
                        ret = rb_do_operation (volinfo, src_brickinfo,
                                               dst_brickinfo,
                                               GF_REPLACE_OP_ABORT);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Abort operation failed");
                                goto out;
                        }
                }

                if (gf_is_local_addr (src_brickinfo->hostname)) {
                        ret = rb_src_brick_restart (volinfo, src_brickinfo,
                                                    0);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Couldn't restart src brick "
                                        "with pump xlator disabled.");
                                goto out;
                        }
                }

                if (gf_is_local_addr (dst_brickinfo->hostname)) {
                        gf_log (this->name, GF_LOG_INFO,
                                "I AM THE DESTINATION HOST");
                        ret = rb_kill_destination_brick (volinfo, dst_brickinfo);
                        if (ret) {
                                gf_log ("", GF_LOG_DEBUG,
                                        "Failed to kill destination brick");
                                goto out;
                        }
                }
                glusterd_set_rb_status (volinfo, GF_RB_STATUS_NONE);
                glusterd_brickinfo_delete (volinfo->rep_brick.dst_brick);
                volinfo->rep_brick.src_brick = NULL;
                volinfo->rep_brick.dst_brick = NULL;
        }
        break;

        case GF_REPLACE_OP_STATUS:
        {
                gf_log (this->name, GF_LOG_DEBUG,
                        "received status - doing nothing");
                ctx = glusterd_op_get_ctx ();
                if (ctx) {
                        if (glusterd_is_rb_paused (volinfo)) {
                                ret = dict_set_str (ctx, "status-reply",
                                                 "replace brick has been paused");
                                if (ret)
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "failed to set pump status"
                                                " in ctx");
                                goto out;
                        }

                        ret = rb_do_operation (volinfo, src_brickinfo,
                                               dst_brickinfo,
                                               GF_REPLACE_OP_STATUS);
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
                gf_log (this->name, GF_LOG_ERROR, "Couldn't store"
                        " replace brick operation's state");

out:
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
                        "Cancelling timer thread");
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

        ret = glusterd_volume_brickinfo_get_by_brick (src_brick, volinfo,
                                                      &src_brickinfo);
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

                ret = rb_do_operation (volinfo, src_brickinfo, dst_brickinfo,
                                       GF_REPLACE_OP_START);
                if (ret)
                        goto out;
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

        glusterd_op_sm ();
}
