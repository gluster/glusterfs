/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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

#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/statvfs.h>

#include "globals.h"
#include "compat.h"
#include "protocol-common.h"
#include "xlator.h"
#include "logging.h"
#include "timer.h"
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include "run.h"

#include "syscall.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"

/* return values - 0: success, +ve: stopped, -ve: failure */
int
gf_glusterd_rebalance_move_data (glusterd_volinfo_t *volinfo, const char *dir)
{
        int                     ret                    = -1;
        DIR                    *fd                     = NULL;
        glusterd_defrag_info_t *defrag                 = NULL;
        struct dirent          *entry                  = NULL;
        struct stat             stbuf                  = {0,};
        char                    full_path[PATH_MAX]    = {0,};
        char                    linkinfo[PATH_MAX]     = {0,};
        char                    force_string[64]       = {0,};

        if (!volinfo->defrag)
                goto out;

        defrag = volinfo->defrag;

        fd = opendir (dir);
        if (!fd)
                goto out;

        if ((defrag->cmd == GF_DEFRAG_CMD_START_MIGRATE_DATA_FORCE) ||
            (defrag->cmd == GF_DEFRAG_CMD_START_FORCE)) {
                strcpy (force_string, "force");
        } else {
                strcpy (force_string, "not-force");
        }

        while ((entry = readdir (fd))) {
                if (!entry)
                        break;

                /* We have to honor 'stop' (or 'pause'|'commit') as early
                   as possible */
                if (volinfo->defrag_status !=
                    GF_DEFRAG_STATUS_MIGRATE_DATA_STARTED) {
                        /* It can be one of 'stopped|paused|commit' etc */
                        closedir (fd);
                        ret = 1;
                        goto out;
                }

                if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
                        continue;

                snprintf (full_path, PATH_MAX, "%s/%s", dir, entry->d_name);

                ret = lstat (full_path, &stbuf);
                if (ret == -1)
                        continue;

                if (S_ISDIR (stbuf.st_mode))
                        continue;

                defrag->num_files_lookedup += 1;

                /* TODO: bring in feature to support hardlink rebalance */
                if (stbuf.st_nlink > 1)
                        continue;

                /* if distribute is present, it will honor this key.
                   -1 is returned if distribute is not present or file doesn't
                   have a link-file. If file has link-file, the path of
                   link-file will be the value, and also that guarantees
                   that file has to be mostly migrated */
                ret = sys_lgetxattr (full_path, GF_XATTR_LINKINFO_KEY,
                                     &linkinfo, PATH_MAX);
                if (ret <= 0)
                        continue;

                ret = sys_lsetxattr (full_path, "distribute.migrate-data",
                                     force_string, strlen (force_string), 0);

                /* if errno is not ENOSPC or ENOTCONN, we can still continue
                   with rebalance process */
                if ((ret == -1) && ((errno != ENOSPC) ||
                                    (errno != ENOTCONN)))
                        continue;

                if ((ret == -1) && (errno == ENOTCONN)) {
                        /* Most probably mount point went missing (mostly due
                           to a brick down), say rebalance failure to user,
                           let him restart it if everything is fine */
                        volinfo->defrag_status = GF_DEFRAG_STATUS_FAILED;
                        break;
                }

                if ((ret == -1) && (errno == ENOSPC)) {
                        /* rebalance process itself failed, may be
                           remote brick went down, or write failed due to
                           disk full etc etc.. */
                        volinfo->defrag_status = GF_DEFRAG_STATUS_FAILED;
                        break;
                }

                LOCK (&defrag->lock);
                {
                        defrag->total_files += 1;
                        defrag->total_data += stbuf.st_size;
                }
                UNLOCK (&defrag->lock);
        }
        closedir (fd);

        fd = opendir (dir);
        if (!fd)
                goto out;
        while ((entry = readdir (fd))) {
                if (!entry)
                        break;

                /* We have to honor 'stop' (or 'pause'|'commit') as early
                   as possible */
                if (volinfo->defrag_status !=
                    GF_DEFRAG_STATUS_MIGRATE_DATA_STARTED) {
                        /* It can be one of 'stopped|paused|commit' etc */
                        closedir (fd);
                        ret = 1;
                        goto out;
                }

                if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
                        continue;

                snprintf (full_path, 1024, "%s/%s", dir, entry->d_name);

                ret = lstat (full_path, &stbuf);
                if (ret == -1)
                        continue;

                if (!S_ISDIR (stbuf.st_mode))
                        continue;

                ret = gf_glusterd_rebalance_move_data (volinfo, full_path);
                if (ret)
                        break;
        }
        closedir (fd);

        if (!entry)
                ret = 0;
out:
        return ret;
}

/* return values - 0: success, +ve: stopped, -ve: failure */
int
gf_glusterd_rebalance_fix_layout (glusterd_volinfo_t *volinfo, const char *dir)
{
        int            ret             = -1;
        char           full_path[1024] = {0,};
        struct stat    stbuf           = {0,};
        DIR           *fd              = NULL;
        struct dirent *entry           = NULL;

        if (!volinfo->defrag)
                goto out;

        fd = opendir (dir);
        if (!fd)
                goto out;

        while ((entry = readdir (fd))) {
                if (!entry)
                        break;

                /* We have to honor 'stop' (or 'pause'|'commit') as early
                   as possible */
                if (volinfo->defrag_status !=
                    GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED) {
                        /* It can be one of 'stopped|paused|commit' etc */
                        closedir (fd);
                        ret = 1;
                        goto out;
                }

                if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
                        continue;

                snprintf (full_path, 1024, "%s/%s", dir, entry->d_name);

                ret = lstat (full_path, &stbuf);
                if (ret == -1)
                        continue;

                if (S_ISDIR (stbuf.st_mode)) {
                        /* Fix the layout of the directory */
                        /* TODO: isn't error code not important ? */
                        sys_lsetxattr (full_path, "trusted.distribute.fix.layout",
                                       "yes", 3, 0);

                        volinfo->defrag->total_files += 1;

                        /* Traverse into subdirectory */
                        ret = gf_glusterd_rebalance_fix_layout (volinfo,
                                                                full_path);
                        if (ret)
                                break;
                }
        }
        closedir (fd);

        if (!entry)
                ret = 0;

out:
        return ret;
}

void *
glusterd_defrag_start (void *data)
{
        glusterd_volinfo_t     *volinfo = data;
        glusterd_defrag_info_t *defrag  = NULL;
        int                     ret     = -1;
        struct stat             stbuf   = {0,};

        THIS = volinfo->xl;
        defrag = volinfo->defrag;
        if (!defrag)
                goto out;

        sleep (1);
        ret = lstat (defrag->mount, &stbuf);
        if ((ret == -1) && (errno == ENOTCONN)) {
                /* Wait for some more time before starting rebalance */
                sleep (2);
                ret = lstat (defrag->mount, &stbuf);
                if (ret == -1) {
                        volinfo->defrag_status   = GF_DEFRAG_STATUS_FAILED;
                        volinfo->rebalance_files = 0;
                        volinfo->rebalance_data  = 0;
                        volinfo->lookedup_files  = 0;
                        goto out;
                }
        }

        /* Fix the root ('/') first */
        sys_lsetxattr (defrag->mount, "trusted.distribute.fix.layout",
                       "yes", 3, 0);

        if ((defrag->cmd == GF_DEFRAG_CMD_START) ||
            (defrag->cmd == GF_DEFRAG_CMD_START_LAYOUT_FIX)) {
                /* root's layout got fixed */
                defrag->total_files = 1;

                /* Step 1: Fix layout of all the directories */
                ret = gf_glusterd_rebalance_fix_layout (volinfo, defrag->mount);
                if (ret < 0)
                        volinfo->defrag_status = GF_DEFRAG_STATUS_FAILED;
                /* in both 'stopped' or 'failure' cases goto out */
                if (ret) {
                        goto out;
                }

                /* Completed first step */
                volinfo->defrag_status = GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE;
        }

        if (defrag->cmd != GF_DEFRAG_CMD_START_LAYOUT_FIX) {
                /* It was used by number of layout fixes on directories */
                defrag->total_files = 0;

                volinfo->defrag_status = GF_DEFRAG_STATUS_MIGRATE_DATA_STARTED;

                /* Step 2: Iterate over directories to move data */
                ret = gf_glusterd_rebalance_move_data (volinfo, defrag->mount);
                if (ret < 0)
                        volinfo->defrag_status = GF_DEFRAG_STATUS_FAILED;
                /* in both 'stopped' or 'failure' cases goto out */
                if (ret) {
                        goto out;
                }

                /* Completed second step */
                volinfo->defrag_status = GF_DEFRAG_STATUS_MIGRATE_DATA_COMPLETE;
        }

        /* Completed whole process */
        if ((defrag->cmd == GF_DEFRAG_CMD_START) ||
            (defrag->cmd == GF_DEFRAG_CMD_START_FORCE))
                volinfo->defrag_status = GF_DEFRAG_STATUS_COMPLETE;

        volinfo->rebalance_files = defrag->total_files;
        volinfo->rebalance_data  = defrag->total_data;
        volinfo->lookedup_files  = defrag->num_files_lookedup;
out:
        volinfo->defrag = NULL;
        if (defrag) {
                gf_log ("rebalance", GF_LOG_INFO, "rebalance on %s complete",
                        defrag->mount);

                ret = runcmd ("umount", "-l", defrag->mount, NULL);
                LOCK_DESTROY (&defrag->lock);

                if (defrag->cbk_fn) {
                        defrag->cbk_fn (volinfo, volinfo->defrag_status);
                }

                GF_FREE (defrag);
        }
        return NULL;
}

int
glusterd_defrag_stop_validate (glusterd_volinfo_t *volinfo,
                               char *op_errstr, size_t len)
{
        int     ret = -1;
        if (glusterd_is_defrag_on (volinfo) == 0) {
                snprintf (op_errstr, len, "Rebalance on %s is either Completed "
                          "or not yet started", volinfo->volname);
                goto out;
        }
        ret = 0;
out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_defrag_stop (glusterd_volinfo_t *volinfo, u_quad_t *files,
                      u_quad_t *size, char *op_errstr, size_t len)
{
        /* TODO: set a variaeble 'stop_defrag' here, it should be checked
           in defrag loop */
        int     ret = -1;
        GF_ASSERT (volinfo);
        GF_ASSERT (files);
        GF_ASSERT (size);
        GF_ASSERT (op_errstr);

        if (!volinfo) {
                ret = -1;
                goto out;
        }

        ret = glusterd_defrag_stop_validate (volinfo, op_errstr, len);
        if (ret) {
                /* rebalance may be happening on other nodes */
                ret = 0;
                goto out;
        }

        ret = 0;
        if (volinfo->defrag_status == GF_DEFRAG_STATUS_NOT_STARTED) {
                goto out;
        }

        LOCK (&volinfo->defrag->lock);
        {
                volinfo->defrag_status = GF_DEFRAG_STATUS_STOPPED;
                *files = volinfo->defrag->total_files;
                *size = volinfo->defrag->total_data;
        }
        UNLOCK (&volinfo->defrag->lock);

        ret = 0;
out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_defrag_status_get_v2 (glusterd_volinfo_t *volinfo,
                               dict_t *dict)
{
        int      ret    = 0;
        uint64_t files  = 0;
        uint64_t size   = 0;
        uint64_t lookup = 0;

        if (!volinfo || !dict)
                goto out;

        ret = 0;
        if (volinfo->defrag_status == GF_DEFRAG_STATUS_NOT_STARTED)
                goto out;

        if (volinfo->defrag) {
                LOCK (&volinfo->defrag->lock);
                {
                        files  = volinfo->defrag->total_files;
                        size   = volinfo->defrag->total_data;
                        lookup = volinfo->defrag->num_files_lookedup;
                }
                UNLOCK (&volinfo->defrag->lock);
        } else {
                files  = volinfo->rebalance_files;
                size   = volinfo->rebalance_data;
                lookup = volinfo->lookedup_files;
        }

        ret = dict_set_uint64 (dict, "files", files);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set file count");

        ret = dict_set_uint64 (dict, "size", size);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set size of xfer");

        ret = dict_set_uint64 (dict, "lookups", lookup);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set lookedup file count");

        ret = dict_set_int32 (dict, "status", volinfo->defrag_status);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set status");

out:
        return 0;
}

int
glusterd_defrag_status_get (glusterd_volinfo_t *volinfo,
                            gf1_cli_defrag_vol_rsp *rsp)
{
        if (!volinfo)
                goto out;

        if (volinfo->defrag) {
                LOCK (&volinfo->defrag->lock);
                {
                        rsp->files = volinfo->defrag->total_files;
                        rsp->size = volinfo->defrag->total_data;
                        rsp->lookedup_files = volinfo->defrag->num_files_lookedup;
                }
                UNLOCK (&volinfo->defrag->lock);
        } else {
                rsp->files = volinfo->rebalance_files;
                rsp->size  = volinfo->rebalance_data;
                rsp->lookedup_files = volinfo->lookedup_files;
        }

        rsp->op_errno = volinfo->defrag_status;
        rsp->op_ret = 0;
out:
        return 0;
}

void
glusterd_rebalance_cmd_attempted_log (int cmd, char *volname)
{
        switch (cmd) {
                case GF_DEFRAG_CMD_START_LAYOUT_FIX:
                        gf_cmd_log ("Volume rebalance"," on volname: %s "
                                    "cmd: start fix layout , attempted",
                                    volname);
                        break;
                case GF_DEFRAG_CMD_START_MIGRATE_DATA:
                case GF_DEFRAG_CMD_START_MIGRATE_DATA_FORCE:
                        gf_cmd_log ("Volume rebalance"," on volname: %s "
                                    "cmd: start data migrate attempted",
                                    volname);
                        break;
                case GF_DEFRAG_CMD_START:
                        gf_cmd_log ("Volume rebalance"," on volname: %s "
                                    "cmd: start, attempted", volname);
                        break;
                case GF_DEFRAG_CMD_STOP:
                        gf_cmd_log ("Volume rebalance"," on volname: %s "
                                    "cmd: stop, attempted", volname);
                        break;
                default:
                        break;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received rebalance volume %d on %s",
                cmd, volname);
}

void
glusterd_rebalance_cmd_log (int cmd, char *volname, int status)
{
        if (cmd != GF_DEFRAG_CMD_STATUS) {
                gf_cmd_log ("volume rebalance"," on volname: %s %d %s",
                            volname, cmd, ((status)?"FAILED":"SUCCESS"));
        }
}

int
glusterd_defrag_start_validate (glusterd_volinfo_t *volinfo, char *op_errstr,
                                size_t len)
{
        int     ret = -1;

        if (glusterd_is_defrag_on (volinfo)) {
                gf_log ("glusterd", GF_LOG_DEBUG,
                        "rebalance on volume %s already started",
                        volinfo->volname);
                snprintf (op_errstr, len, "Rebalance on %s is already started",
                          volinfo->volname);
                goto out;
        }

        if (glusterd_is_rb_started (volinfo) ||
            glusterd_is_rb_paused (volinfo)) {
                gf_log ("glusterd", GF_LOG_DEBUG,
                        "Rebalance failed as replace brick is in progress on volume %s",
                        volinfo->volname);
                snprintf (op_errstr, len, "Rebalance failed as replace brick is in progress on "
                          "volume %s", volinfo->volname);
                goto out;
        }
        ret = 0;
out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_handle_defrag_start (glusterd_volinfo_t *volinfo, char *op_errstr,
                              size_t len, int cmd, defrag_cbk_fn_t cbk)
{
        int                    ret = -1;
        glusterd_defrag_info_t *defrag =  NULL;
        runner_t               runner = {0,};
        glusterd_conf_t        *priv = NULL;

        priv    = THIS->private;

        GF_ASSERT (volinfo);
        GF_ASSERT (op_errstr);

        ret = glusterd_defrag_start_validate (volinfo, op_errstr, len);
        if (ret)
                goto out;
        if (!volinfo->defrag)
                volinfo->defrag = GF_CALLOC (1, sizeof (glusterd_defrag_info_t),
                                             gf_gld_mt_defrag_info);
        if (!volinfo->defrag)
                goto out;

        defrag = volinfo->defrag;

        defrag->cmd = cmd;

        LOCK_INIT (&defrag->lock);
        snprintf (defrag->mount, 1024, "%s/mount/%s",
                  priv->workdir, volinfo->volname);
        /* Create a directory, mount glusterfs over it, start glusterfs-defrag */
        runinit (&runner);
        runner_add_args (&runner, "mkdir", "-p", defrag->mount, NULL);
        ret = runner_run_reuse (&runner);
        if (ret) {
                runner_log (&runner, "glusterd", GF_LOG_DEBUG, "command failed");
                runner_end (&runner);
                goto out;
        }
        runner_end (&runner);

        runinit (&runner);
        runner_add_args (&runner, SBIN_DIR"/glusterfs",
                         "-s", "localhost", "--volfile-id", volinfo->volname,
                         "--xlator-option", "*dht.use-readdirp=yes",
                         "--xlator-option", "*dht.lookup-unhashed=yes",
                         "--xlator-option", "*dht.assert-no-child-down=yes",
                         defrag->mount, NULL);
        ret = runner_run_reuse (&runner);
        if (ret) {
                runner_log (&runner, "glusterd", GF_LOG_DEBUG, "command failed");
                runner_end (&runner);
                goto out;
        }
        runner_end (&runner);

        volinfo->defrag_status = GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED;
        if ((cmd == GF_DEFRAG_CMD_START_MIGRATE_DATA) ||
            (cmd == GF_DEFRAG_CMD_START_MIGRATE_DATA_FORCE)) {
                volinfo->defrag_status = GF_DEFRAG_STATUS_MIGRATE_DATA_STARTED;
        }

        if (cbk)
                defrag->cbk_fn = cbk;

        ret = pthread_create (&defrag->th, NULL, glusterd_defrag_start,
                              volinfo);
        if (ret) {
                runinit (&runner);
                runner_add_args (&runner, "umount", "-l", defrag->mount, NULL);
                ret = runner_run_reuse (&runner);
                if (ret)
                        runner_log (&runner, "glusterd", GF_LOG_DEBUG, "command failed");
                runner_end (&runner);
        }
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_rebalance_cmd_validate (int cmd, char *volname,
                                 glusterd_volinfo_t **volinfo,
                                 char *op_errstr, size_t len)
{
        int ret = -1;

        if (glusterd_volinfo_find(volname, volinfo)) {
                gf_log ("glusterd", GF_LOG_ERROR, "Received rebalance on invalid"
                        " volname %s", volname);
                snprintf (op_errstr, len, "Volume %s does not exist",
                          volname);
                goto out;
        }
        if ((*volinfo)->brick_count <= (*volinfo)->dist_leaf_count) {
                gf_log ("glusterd", GF_LOG_ERROR, "Volume %s is not a "
                "distribute type or contains only 1 brick", volname);
                snprintf (op_errstr, len, "Volume %s is not a distribute "
                          "volume or contains only 1 brick.\n"
                          "Not performing rebalance", volname);
                goto out;
        }

        if ((*volinfo)->status != GLUSTERD_STATUS_STARTED) {
                gf_log ("glusterd", GF_LOG_ERROR, "Received rebalance on stopped"
                        " volname %s", volname);
                snprintf (op_errstr, len, "Volume %s needs to "
                          "be started to perform rebalance", volname);
                goto out;
        }

        ret = 0;

out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_handle_defrag_volume (rpcsvc_request_t *req)
{
        int32_t                ret           = -1;
        gf1_cli_defrag_vol_req cli_req       = {0,};
        glusterd_conf_t         *priv = NULL;
        char                   cmd_str[4096] = {0,};
        glusterd_volinfo_t      *volinfo = NULL;
        gf1_cli_defrag_vol_rsp rsp = {0,};
        char                    msg[2048] = {0};

        GF_ASSERT (req);

        priv    = THIS->private;

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf1_cli_defrag_vol_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        glusterd_rebalance_cmd_attempted_log (cli_req.cmd, cli_req.volname);

        rsp.volname = cli_req.volname;
        rsp.op_ret = -1;

        ret = glusterd_rebalance_cmd_validate (cli_req.cmd, cli_req.volname,
                                               &volinfo, msg, sizeof (msg));
        if (ret)
                goto out;
        switch (cli_req.cmd) {
        case GF_DEFRAG_CMD_START:
        case GF_DEFRAG_CMD_START_LAYOUT_FIX:
        case GF_DEFRAG_CMD_START_MIGRATE_DATA:
        case GF_DEFRAG_CMD_START_MIGRATE_DATA_FORCE:
        {
                ret = glusterd_handle_defrag_start (volinfo, msg, sizeof (msg),
                                                    cli_req.cmd, NULL);
                rsp.op_ret = ret;
                break;
        }
        case GF_DEFRAG_CMD_STOP:
                ret = glusterd_defrag_stop (volinfo, &rsp.files, &rsp.size,
                                            msg, sizeof (msg));
                rsp.op_ret = ret;
                break;
        case GF_DEFRAG_CMD_STATUS:
                ret = glusterd_defrag_status_get (volinfo, &rsp);
                break;
        default:
                break;
        }
        if (ret)
                gf_log("glusterd", GF_LOG_DEBUG, "command: %s failed",cmd_str);

        if (cli_req.cmd != GF_DEFRAG_CMD_STATUS) {
                gf_cmd_log ("volume rebalance"," on volname: %s %d %s",
                            cli_req.volname,
                            cli_req.cmd, ((ret)?"FAILED":"SUCCESS"));
        }

out:
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf1_cli_defrag_vol_rsp);
        if (cli_req.volname)
                free (cli_req.volname);//malloced by xdr

        return 0;
}

int
glusterd_handle_defrag_volume_v2 (rpcsvc_request_t *req)
{
        int32_t                 ret     = -1;
        gf1_cli_defrag_vol_req  cli_req = {0,};
        glusterd_conf_t        *priv    = NULL;
        dict_t                 *dict    = NULL;
        char                   *volname = NULL;

        GF_ASSERT (req);

        priv = THIS->private;

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf1_cli_defrag_vol_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        glusterd_rebalance_cmd_attempted_log (cli_req.cmd, cli_req.volname);

        dict = dict_new ();
        if (!dict)
                goto out;

        volname = gf_strdup (cli_req.volname);
        if (!volname)
                goto out;

        /* let 'volname' be freed in dict_destroy */
        ret = dict_set_dynstr (dict, "volname", volname);
        if (ret)
                goto out;

        ret = dict_set_static_bin (dict, "node-uuid", priv->uuid, 16);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "rebalance-command", cli_req.cmd);
        if (ret)
                goto out;

        ret = glusterd_op_begin (req, GD_OP_REBALANCE, dict);

out:

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                if (dict)
                        dict_unref (dict);
                ret = glusterd_op_send_cli_response (GD_OP_REBALANCE, ret, 0, req,
                                                     NULL, "operation failed");
        }

        if (cli_req.volname)
                free (cli_req.volname);//malloced by xdr

        return 0;
}


int
glusterd_op_stage_rebalance (dict_t *dict, char **op_errstr)
{
        char *volname = NULL;
        int ret = 0;
        int32_t cmd = 0;
        char msg[2048] = {0};
        glusterd_volinfo_t  *volinfo = NULL;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "volname not found");
                goto out;
        }
        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "cmd not found");
                goto out;
        }

        ret = glusterd_rebalance_cmd_validate (cmd, volname, &volinfo,
                                               msg, sizeof (msg));
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "failed to validate");
                goto out;
        }
        switch (cmd) {
        case GF_DEFRAG_CMD_START:
        case GF_DEFRAG_CMD_START_LAYOUT_FIX:
        case GF_DEFRAG_CMD_START_MIGRATE_DATA:
        case GF_DEFRAG_CMD_START_MIGRATE_DATA_FORCE:
                ret = glusterd_defrag_start_validate (volinfo,
                                                      msg, sizeof (msg));
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "start validate failed");
                        goto out;
                }
        default:
                break;
        }

        ret = 0;
out:
        if (ret && op_errstr && msg[0])
                *op_errstr = gf_strdup (msg);

        return ret;
}


int
glusterd_op_rebalance (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        char               *volname   = NULL;
        int                 ret       = 0;
        int32_t             cmd       = 0;
        char                msg[2048] = {0};
        glusterd_volinfo_t *volinfo   = NULL;
        uint64_t            files     = 0;
        uint64_t            size      = 0;
        void               *node_uuid = NULL;
        glusterd_conf_t    *priv      = NULL;
        dict_t             *tmp_dict  = NULL;

        priv = THIS->private;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "volname not given");
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "command not given");
                goto out;
        }

        ret = glusterd_rebalance_cmd_validate (cmd, volname, &volinfo,
                                               msg, sizeof (msg));
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "cmd validate failed");
                goto out;
        }

        if ((cmd != GF_DEFRAG_CMD_STATUS) &&
            (cmd != GF_DEFRAG_CMD_STOP)) {
                ret = dict_get_ptr (dict, "node-uuid", &node_uuid);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG, "node-uuid not found");
                        goto out;
                }

                /* perform this on only the node which has
                   issued the command */
                if (uuid_compare (node_uuid, priv->uuid)) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "not the source node %s", uuid_utoa (priv->uuid));
                        goto out;
                }
        }

        switch (cmd) {
        case GF_DEFRAG_CMD_START:
        case GF_DEFRAG_CMD_START_LAYOUT_FIX:
        case GF_DEFRAG_CMD_START_MIGRATE_DATA:
        case GF_DEFRAG_CMD_START_MIGRATE_DATA_FORCE:
                ret = glusterd_handle_defrag_start (volinfo, msg, sizeof (msg),
                                                    cmd, NULL);
                 break;
         case GF_DEFRAG_CMD_STOP:
                 ret = glusterd_defrag_stop (volinfo, &files, &size,
                                             msg, sizeof (msg));
                if (!ret && rsp_dict) {
                        ret = dict_set_uint64 (rsp_dict, "files", files);
                        if (ret)
                                gf_log (THIS->name, GF_LOG_WARNING,
                                        "failed to set file count");

                        ret = dict_set_uint64 (rsp_dict, "size", size);
                        if (ret)
                                gf_log (THIS->name, GF_LOG_WARNING,
                                        "failed to set xfer size");

                        /* Don't want to propagate errors from dict_set() */
                        ret = 0;
                }
                break;
        case GF_DEFRAG_CMD_STATUS:

                if (rsp_dict)
                        tmp_dict = rsp_dict;

                /* On source node, there will be no 'rsp_dict' */
                if (!tmp_dict)
                        tmp_dict = glusterd_op_get_ctx (GD_OP_REBALANCE);

                ret = glusterd_defrag_status_get_v2 (volinfo, tmp_dict);
                break;
        default:
                break;
        }

        glusterd_rebalance_cmd_log (cmd, volname, ret);

out:
        if (ret && op_errstr && msg[0])
                *op_errstr = gf_strdup (msg);

        return ret;
}
