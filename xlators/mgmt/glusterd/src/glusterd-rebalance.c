/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
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
#include <inttypes.h>
#include <sys/resource.h>

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

#include "syscall.h"
#include "cli1.h"

int
gf_glusterd_rebalance_move_data (glusterd_volinfo_t *volinfo, const char *dir)
{
        int                     ret                = -1;
        int                     dst_fd             = -1;
        int                     src_fd             = -1;
        DIR                    *fd                 = NULL;
        glusterd_defrag_info_t *defrag             = NULL;
        struct dirent          *entry              = NULL;
        struct stat             stbuf              = {0,};
        struct stat             new_stbuf          = {0,};
        char                    full_path[1024]    = {0,};
        char                    tmp_filename[1024] = {0,};
        char                    value[16]          = {0,};

        if (!volinfo->defrag)
                goto out;

        defrag = volinfo->defrag;

        fd = opendir (dir);
        if (!fd)
                goto out;
        while ((entry = readdir (fd))) {
                if (!entry)
                        break;

                if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
                        continue;

                snprintf (full_path, 1024, "%s/%s", dir, entry->d_name);

                ret = stat (full_path, &stbuf);
                if (ret == -1)
                        continue;

                if (S_ISREG (stbuf.st_mode))
                        defrag->num_files_lookedup += 1;

                if (!(S_ISREG (stbuf.st_mode) &&
                      ((stbuf.st_mode & 01000) == 01000)))
                        continue;

                /* If the file is open, don't run rebalance on it */
                ret = sys_lgetxattr (full_path, GLUSTERFS_OPEN_FD_COUNT,
                                     &value, 16);
                if ((ret < 0) || !strncmp (value, "1", 1))
                        continue;

                /* If its a regular file, and sticky bit is set, we need to
                   rebalance that */
                snprintf (tmp_filename, 1024, "%s/.%s.gfs%llu", dir,
                          entry->d_name,
                          (unsigned long long)stbuf.st_size);

                dst_fd = creat (tmp_filename, (stbuf.st_mode & ~01000));
                if (dst_fd == -1)
                        continue;

                src_fd = open (full_path, O_RDONLY);
                if (src_fd == -1) {
                        close (dst_fd);
                        continue;
                }

                while (1) {
                        ret = read (src_fd, defrag->databuf, 131072);
                        if (!ret || (ret < 0)) {
                                break;
                        }
                        ret = write (dst_fd, defrag->databuf, ret);
                        if (ret < 0) {
                                break;
                        }
                }

                ret = stat (full_path, &new_stbuf);
                if (ret < 0) {
                        close (dst_fd);
                        close (src_fd);
                        continue;
                }
                /* No need to rebalance, if there is some
                   activity on source file */
                if (new_stbuf.st_mtime != stbuf.st_mtime) {
                        close (dst_fd);
                        close (src_fd);
                        continue;
                }

                ret = fchmod (dst_fd, stbuf.st_mode);
                if (ret) {
                        gf_log ("", GF_LOG_WARNING,
                                "failed to set the mode of file %s: %s",
                                tmp_filename, strerror (errno));
                }

                ret = fchown (dst_fd, stbuf.st_uid, stbuf.st_gid);
                if (ret) {
                        gf_log ("", GF_LOG_WARNING,
                                "failed to set the uid/gid of file %s: %s",
                                tmp_filename, strerror (errno));
                }

                ret = rename (tmp_filename, full_path);
                if (ret != -1) {
                        LOCK (&defrag->lock);
                        {
                                defrag->total_files += 1;
                                defrag->total_data += stbuf.st_size;
                        }
                        UNLOCK (&defrag->lock);
                }

                close (dst_fd);
                close (src_fd);

                if (volinfo->defrag_status == GF_DEFRAG_STATUS_STOPED) {
                        closedir (fd);
                        ret = -1;
                        goto out;
                }
        }
        closedir (fd);

        fd = opendir (dir);
        if (!fd)
                goto out;
        while ((entry = readdir (fd))) {
                if (!entry)
                        break;

                if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
                        continue;

                snprintf (full_path, 1024, "%s/%s", dir, entry->d_name);

                ret = stat (full_path, &stbuf);
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

int
gf_glusterd_rebalance_fix_layout (glusterd_volinfo_t *volinfo, const char *dir)
{
        int            ret             = -1;
        char           value[128]      = {0,};
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

                if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
                        continue;

                snprintf (full_path, 1024, "%s/%s", dir, entry->d_name);

                ret = stat (full_path, &stbuf);
                if (ret == -1)
                        continue;

                if (S_ISDIR (stbuf.st_mode)) {
                        /* Fix the layout of the directory */
                        sys_lgetxattr (full_path, "trusted.distribute.fix.layout",
                                       &value, 128);

                        volinfo->defrag->total_files += 1;

                        /* Traverse into subdirectory */
                        ret = gf_glusterd_rebalance_fix_layout (volinfo,
                                                                full_path);
                        if (ret)
                                break;
                }

                if (volinfo->defrag_status == GF_DEFRAG_STATUS_STOPED) {
                        closedir (fd);
                        ret = -1;
                        goto out;
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
        char                    cmd_str[1024] = {0,};
        int                     ret     = -1;
        struct stat             stbuf   = {0,};
        char                    value[128] = {0,};

        defrag = volinfo->defrag;
        if (!defrag)
                goto out;

        sleep (1);
        ret = stat (defrag->mount, &stbuf);
        if ((ret == -1) && (errno == ENOTCONN)) {
                /* Wait for some more time before starting rebalance */
                sleep (2);
                ret = stat (defrag->mount, &stbuf);
                if (ret == -1) {
                        volinfo->defrag_status   = GF_DEFRAG_STATUS_FAILED;
                        volinfo->rebalance_files = 0;
                        volinfo->rebalance_data  = 0;
                        volinfo->lookedup_files  = 0;
                        goto out;
                }
        }

        /* Fix the root ('/') first */
        sys_lgetxattr (defrag->mount, "trusted.distribute.fix.layout",
                       &value, 128);

        if ((defrag->cmd == GF_DEFRAG_CMD_START) ||
            (defrag->cmd == GF_DEFRAG_CMD_START_LAYOUT_FIX)) {
                /* root's layout got fixed */
                defrag->total_files = 1;

                /* Step 1: Fix layout of all the directories */
                ret = gf_glusterd_rebalance_fix_layout (volinfo, defrag->mount);
                if (ret) {
                        volinfo->defrag_status   = GF_DEFRAG_STATUS_FAILED;
                        goto out;
                }

                /* Completed first step */
                volinfo->defrag_status = GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE;
        }

        if ((defrag->cmd == GF_DEFRAG_CMD_START) ||
            (defrag->cmd == GF_DEFRAG_CMD_START_MIGRATE_DATA)) {
                /* It was used by number of layout fixes on directories */
                defrag->total_files = 0;

                volinfo->defrag_status = GF_DEFRAG_STATUS_MIGRATE_DATA_STARTED;

                /* Step 2: Iterate over directories to move data */
                ret = gf_glusterd_rebalance_move_data (volinfo, defrag->mount);
                if (ret) {
                        volinfo->defrag_status   = GF_DEFRAG_STATUS_FAILED;
                        goto out;
                }

                /* Completed second step */
                volinfo->defrag_status = GF_DEFRAG_STATUS_MIGRATE_DATA_COMPLETE;
        }

        /* Completed whole process */
        if (defrag->cmd == GF_DEFRAG_CMD_START)
                volinfo->defrag_status = GF_DEFRAG_STATUS_COMPLETE;

        volinfo->rebalance_files = defrag->total_files;
        volinfo->rebalance_data  = defrag->total_data;
        volinfo->lookedup_files  = defrag->num_files_lookedup;
out:
        volinfo->defrag = NULL;
        if (defrag) {
                gf_log ("rebalance", GF_LOG_NORMAL, "rebalance on %s complete",
                        defrag->mount);

                snprintf (cmd_str, 1024, "umount -l %s", defrag->mount);
                ret = system (cmd_str);
                LOCK_DESTROY (&defrag->lock);
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

        ret = glusterd_defrag_stop_validate (volinfo, op_errstr, len);
        if (ret)
                goto out;
        if (!volinfo || !volinfo->defrag) {
                ret = -1;
                goto out;
        }

        LOCK (&volinfo->defrag->lock);
        {
                volinfo->defrag_status = GF_DEFRAG_STATUS_STOPED;
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
                            gf2_cli_defrag_vol_rsp *rsp)
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

        gf_log ("glusterd", GF_LOG_NORMAL, "Received rebalance volume %d on %s",
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
                        "Replace brick is in progress on volume %s",
                        volinfo->volname);
                snprintf (op_errstr, len, "Replace brick is in progress on "
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
                              size_t len, int cmd)
{
        int                    ret = -1;
        glusterd_defrag_info_t *defrag =  NULL;
        char                   cmd_str[4096] = {0,};
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
        snprintf (cmd_str, sizeof (cmd_str), "mkdir -p %s", defrag->mount);
        ret = system (cmd_str);

        if (ret) {
                gf_log("glusterd", GF_LOG_DEBUG, "command: %s failed", cmd_str);
                goto out;
        }

        snprintf (cmd_str, sizeof (cmd_str), "%s/sbin/glusterfs -s localhost "
                  "--volfile-id %s --volume-name %s-quick-read "
                  "--xlator-option *dht.unhashed-sticky-bit=yes "
                  "--xlator-option *dht.use-readdirp=yes "
                  "--xlator-option *dht.lookup-unhashed=yes %s",
                  GFS_PREFIX, volinfo->volname, volinfo->volname,
                  defrag->mount);
        ret = gf_system (cmd_str);
        if (ret) {
                gf_log("glusterd", GF_LOG_DEBUG, "command: %s failed", cmd_str);
                goto out;
        }

        volinfo->defrag_status = GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED;

        ret = pthread_create (&defrag->th, NULL, glusterd_defrag_start,
                              volinfo);
        if (ret) {
                snprintf (cmd_str, sizeof (cmd_str), "umount -l %s", defrag->mount);
                if (system (cmd_str))
                        gf_log("glusterd", GF_LOG_DEBUG, "command: %s "
                               "failed", cmd_str);
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
glusterd_handle_defrag_volume_v2 (rpcsvc_request_t *req)
{
        int32_t                 ret           = -1;
        gf1_cli_defrag_vol_req  cli_req       = {0,};
        glusterd_volinfo_t     *volinfo = NULL;
        gf2_cli_defrag_vol_rsp rsp = {0,};
        char                    msg[2048] = {0};
        glusterd_conf_t        *priv = NULL;

        GF_ASSERT (req);

        priv    = THIS->private;
        if (!gf_xdr_to_cli_defrag_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        glusterd_rebalance_cmd_attempted_log (cli_req.cmd, cli_req.volname);

        rsp.volname = cli_req.volname;
        rsp.op_ret = -1;
        rsp.op_errstr = msg;

        ret = glusterd_rebalance_cmd_validate (cli_req.cmd, cli_req.volname,
                                               &volinfo, msg, sizeof (msg));
        if (ret)
                goto out;

        switch (cli_req.cmd) {
        case GF_DEFRAG_CMD_START:
        case GF_DEFRAG_CMD_START_LAYOUT_FIX:
        case GF_DEFRAG_CMD_START_MIGRATE_DATA:
                ret = glusterd_handle_defrag_start (volinfo, msg, sizeof (msg),
                                                    cli_req.cmd);
                rsp.op_ret = ret;
                break;
        case GF_DEFRAG_CMD_STOP:
                ret = glusterd_defrag_stop (volinfo, &rsp.files, &rsp.size,
                                            msg, sizeof (msg));
                rsp.op_ret = ret;
                break;
        case GF_DEFRAG_CMD_STATUS:
                ret = glusterd_defrag_status_get_v2 (volinfo, &rsp);
                break;
        default:
                break;
        }
        glusterd_rebalance_cmd_log (cli_req.cmd, cli_req.volname, rsp.op_ret);
out:

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_defrag_vol_rsp_v2);
        if (cli_req.volname)
                free (cli_req.volname);//malloced by xdr

        return 0;
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

        if (!gf_xdr_to_cli_defrag_vol_req (req->msg[0], &cli_req)) {
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
        {
                ret = glusterd_handle_defrag_start (volinfo, msg, sizeof (msg),
                                                    cli_req.cmd);
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
                                     gf_xdr_serialize_cli_defrag_vol_rsp);
        if (cli_req.volname)
                free (cli_req.volname);//malloced by xdr

        return 0;
}
