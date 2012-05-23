/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
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
glusterd_handle_replace_brick (rpcsvc_request_t *req)
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

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf_cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received replace brick req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "could not get volname");
                goto out;
        }

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
                gf_log ("", GF_LOG_ERROR, "Unable to get dest brick");
                goto out;
        }

        (void) glusterd_get_replace_op_str (op, operation);
        gf_log ("", GF_LOG_DEBUG, "dst brick=%s", dst_brick);
        gf_log ("glusterd", GF_LOG_INFO, "Received replace brick %s request",
                operation);
        gf_cmd_log ("Volume replace-brick","volname: %s src_brick:%s"
                    " dst_brick:%s op:%s", volname, src_brick, dst_brick,
                    operation);

        ret = glusterd_op_begin (req, GD_OP_REPLACE_BRICK, dict);
        gf_cmd_log ("Volume replace-brick","on volname: %s %s", volname,
                   (ret) ? "FAILED" : "SUCCESS");

out:
        if (ret && dict)
                dict_unref (dict);
        if (cli_req.dict.dict_val)
                free (cli_req.dict.dict_val);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret)
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     NULL, "operation failed");

        return ret;
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

int
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
        dict_t                                  *ctx           = NULL;
        glusterd_conf_t                         *priv          = NULL;
        char                                    voldir[PATH_MAX] = {0};
        char                                    pidfile[PATH_MAX] = {0};

        priv = THIS->private;
        GF_ASSERT (priv);

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
                if (!glusterd_is_rb_ongoing (volinfo)) {
                        gf_log ("", GF_LOG_ERROR, "Replace brick is not"
                                " started or paused for volume ");
                        ret = -1;
                        goto out;
                }
                break;

        case GF_REPLACE_OP_COMMIT:
                if (!glusterd_is_rb_ongoing (volinfo)) {
                        gf_log ("", GF_LOG_ERROR, "Replace brick is not "
                                "started for volume ");
                        ret = -1;
                        goto out;
                }
                break;

        case GF_REPLACE_OP_COMMIT_FORCE: break;

        case GF_REPLACE_OP_STATUS:

                if (glusterd_is_rb_ongoing (volinfo) == _gf_false) {
                        ret = gf_asprintf (op_errstr, "replace-brick not"
                                           " started on volume %s",
                                           volinfo->volname);
                        if (ret < 0) {
                                *op_errstr = NULL;
                                goto out;
                        }

                        gf_log (THIS->name, GF_LOG_ERROR, "%s", *op_errstr);
                        ret = -1;
                        goto out;
                }
                break;

        default:
                ret = -1;
                goto out;
        }

        ret = glusterd_volume_brickinfo_get_by_brick (src_brick, volinfo,
                                                      &src_brickinfo,
                                                      GF_PATH_COMPLETE);
        if (ret) {
                snprintf (msg, sizeof (msg), "brick: %s does not exist in "
                          "volume: %s", src_brick, volname);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ctx = glusterd_op_get_ctx();
        if (ctx) {
                if (!glusterd_is_fuse_available ()) {
                        gf_log ("glusterd", GF_LOG_ERROR, "Unable to open /dev/"
                                "fuse (%s), replace-brick command failed",
                                strerror (errno));
                        snprintf (msg, sizeof(msg), "Fuse unavailable\n "
                                "Replace-brick failed");
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }
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

                GLUSTERD_GET_VOLUME_DIR (voldir, volinfo, priv);
                GLUSTERD_GET_BRICK_PIDFILE (pidfile, voldir,
                                            src_brickinfo->hostname,
                                            src_brickinfo->path);
                if ((replace_op != GF_REPLACE_OP_COMMIT_FORCE) &&
                    !glusterd_is_service_running (pidfile, NULL)) {
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

        ret = glusterd_brickinfo_from_brick (dst_brick, &dst_brickinfo);
        if (ret)
                goto out;

       if (!glusterd_is_rb_ongoing (volinfo) &&
            (replace_op == GF_REPLACE_OP_START ||
             replace_op == GF_REPLACE_OP_COMMIT_FORCE)) {

                volinfo->src_brick = src_brickinfo;
                volinfo->dst_brick = dst_brickinfo;
        }

        if (glusterd_rb_check_bricks (volinfo, src_brickinfo, dst_brickinfo)) {

                ret = -1;
                *op_errstr = gf_strdup ("Incorrect source or "
                                        "destination brick");
                if (*op_errstr)
                        gf_log (THIS->name, GF_LOG_ERROR, "%s", *op_errstr);
                goto out;
       }

        if (!glusterd_is_rb_ongoing (volinfo) &&
            !glusterd_is_local_addr (host)) {
                ret = glusterd_brick_create_path (host, path,
                                                  volinfo->volume_id,
                                                  op_errstr);
                if (ret)
                        goto out;
        }

        if (glusterd_is_local_addr (host)) {
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

        ret = glusterd_nfs_server_stop (volinfo);

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
        glusterd_conf_t  *priv                        = NULL;
        xlator_t         *this                        = NULL;
        char              mount_point_path[PATH_MAX]  = {0,};
        int               ret                         = -1;

        this = THIS;
        priv = this->private;

        snprintf (mount_point_path, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname, RB_CLIENT_MOUNTPOINT);

        ret = sys_lsetxattr (mount_point_path, xattr_key, value,
                             strlen (value) + 1, 0);
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG, "setxattr on key: "
                        "%s failed", xattr_key);
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
        runner_add_arg (&runner, SBIN_DIR"/glusterfs");
        runner_argprintf (&runner, "-f" "%s/vols/%s/"RB_DSTBRICKVOL_FILENAME,
                          priv->workdir, volinfo->volname);
        runner_argprintf (&runner, "-p" "%s/vols/%s/"RB_DSTBRICK_PIDFILE,
                          priv->workdir, volinfo->volname);
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "src-server.listen-port=%d", port);
        if (volinfo->memory_accounting)
                runner_add_arg (&runner, "--mem-accounting");

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
        runner_add_arg (&runner, SBIN_DIR"/glusterfs");
        runner_argprintf (&runner, "-f" "%s/vols/%s/"RB_CLIENTVOL_FILENAME,
                          priv->workdir, volinfo->volname);
        runner_argprintf (&runner, "%s/vols/%s/"RB_CLIENT_MOUNTPOINT,
                          priv->workdir, volinfo->volname);
        if (volinfo->memory_accounting)
                runner_add_arg (&runner, "--mem-accounting");

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
        int               ret                         = -1;

        priv = THIS->private;

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
        glusterd_conf_t  *priv                        = NULL;
        xlator_t         *this                        = NULL;
        char              mount_point_path[PATH_MAX]  = {0,};
        int               ret                         = -1;

        this = THIS;
        priv = THIS->private;

        snprintf (mount_point_path, PATH_MAX, "%s/vols/%s/%s",
                  priv->workdir, volinfo->volname,
                  RB_CLIENT_MOUNTPOINT);

        ret = sys_lgetxattr (mount_point_path, xattr_key, value, 8192);

        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG, "getxattr on key: %s "
                        "failed", xattr_key);
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
        char         start_value[8192] = {0,};
        char         status[8192]      = {0,};
        char        *status_reply      = NULL;
        dict_t      *ctx               = NULL;
        int          ret               = 0;

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
                                                    status);
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

                        status_reply = gf_strdup (status);
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

        ctx = glusterd_op_get_ctx ();
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

        ctx = glusterd_op_get_ctx ();
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

        ret = glusterd_brickinfo_from_brick (new_brick,
                                             &new_brickinfo);
        if (ret)
                goto out;

        ret = glusterd_volume_brickinfo_get_by_brick (old_brick, volinfo,
                                                      &old_brickinfo,
                                                      GF_PATH_COMPLETE);
        if (ret)
                goto out;

        ret = glusterd_resolve_brick (new_brickinfo);
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
                ret = glusterd_brick_start (volinfo, new_brickinfo);
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

        ret = glusterd_volume_brickinfo_get_by_brick (src_brick, volinfo,
                                                      &src_brickinfo,
                                                      GF_PATH_COMPLETE);
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
        {
                ctx = glusterd_op_get_ctx ();
                if (ctx) {
                        ret = rb_do_operation (volinfo, src_brickinfo,
                                               dst_brickinfo,
                                               GF_REPLACE_OP_COMMIT);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR,
                                        "Commit operation failed");
                                goto out;
                        }
                }
        }
                /* fall through */
        case GF_REPLACE_OP_COMMIT_FORCE:
        {
                ret = dict_set_int32 (volinfo->dict, "enable-pump", 0);
                gf_log (THIS->name, GF_LOG_DEBUG,
                        "Received commit - will be adding dst brick and "
                        "removing src brick");

                if (!glusterd_is_local_addr (dst_brickinfo->hostname)) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "I AM THE DESTINATION HOST");
                        ret = rb_kill_destination_brick (volinfo, dst_brickinfo);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_CRITICAL,
                                        "Unable to cleanup dst brick");
                                goto out;
                        }
                }

                ret = glusterd_nodesvcs_stop (volinfo);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "Unable to stop nfs server, ret: %d", ret);
                }

		ret = glusterd_op_perform_replace_brick (volinfo, src_brick,
							 dst_brick);
		if (ret) {
			gf_log (THIS->name, GF_LOG_CRITICAL, "Unable to add "
				"dst-brick: %s to volume: %s",
				dst_brick, volinfo->volname);
		        (void) glusterd_nodesvcs_handle_graph_change (volinfo);
			goto out;
		}

		volinfo->defrag_status = 0;

		ret = glusterd_nodesvcs_handle_graph_change (volinfo);
		if (ret) {
                        gf_log (THIS->name, GF_LOG_CRITICAL,
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
                ctx = glusterd_op_get_ctx ();
                if (ctx) {
                        ret = rb_do_operation (volinfo, src_brickinfo,
                                               dst_brickinfo,
                                               GF_REPLACE_OP_PAUSE);
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

                ctx = glusterd_op_get_ctx ();
                if (ctx) {
                        ret = rb_do_operation (volinfo, src_brickinfo,
                                               dst_brickinfo,
                                               GF_REPLACE_OP_ABORT);
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

                if (!glusterd_is_local_addr (src_brickinfo->hostname)) {
                        ret = rb_src_brick_restart (volinfo, src_brickinfo,
                                                    0);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Couldn't restart src brick "
                                        "with pump xlator disabled.");
                                goto out;
                        }
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
                ctx = glusterd_op_get_ctx ();
                if (ctx) {
                        if (glusterd_is_rb_paused (volinfo)) {
                                ret = dict_set_str (ctx, "status-reply",
                                                 "replace brick has been paused");
                                if (ret)
                                        gf_log (THIS->name, GF_LOG_ERROR,
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
                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't store"
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
                                                      &src_brickinfo,
                                                      GF_PATH_COMPLETE);
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

        if (dict)
                dict_unref (dict);

        glusterd_op_sm ();
}
