/*
   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <glusterfs/common-utils.h>
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include <glusterfs/glusterfs.h>
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-geo-rep.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-svc-helper.h"
#include "glusterd-volgen.h"
#include "glusterd-messages.h"
#include "glusterd-mgmt.h"
#include <glusterfs/run.h>
#include <glusterfs/syscall.h>

#include <signal.h>

int
glusterd_reset_brick_prevalidate(dict_t *dict, char **op_errstr,
                                 dict_t *rsp_dict)
{
    int ret = 0;
    char *src_brick = NULL;
    char *dst_brick = NULL;
    char *volname = NULL;
    char *op = NULL;
    glusterd_op_t gd_op = -1;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *src_brickinfo = NULL;
    char *host = NULL;
    char msg[2048] = {0};
    glusterd_peerinfo_t *peerinfo = NULL;
    glusterd_brickinfo_t *dst_brickinfo = NULL;
    glusterd_conf_t *priv = NULL;
    char pidfile[PATH_MAX] = {0};
    xlator_t *this = NULL;
    gf_boolean_t is_force = _gf_false;
    int32_t ignore_partition = 0;
    pid_t pid = -1;
    uuid_t volume_id = {
        0,
    };
    char *dup_dstbrick = NULL;

    this = THIS;
    GF_ASSERT(this);

    priv = this->private;
    GF_ASSERT(priv);

    ret = glusterd_brick_op_prerequisites(dict, &op, &gd_op, &volname, &volinfo,
                                          &src_brick, &src_brickinfo, pidfile,
                                          op_errstr, rsp_dict);
    if (ret)
        goto out;

    if (!strcmp(op, "GF_RESET_OP_START"))
        goto done;

    if (!strcmp(op, "GF_RESET_OP_COMMIT_FORCE"))
        is_force = _gf_true;

    ret = glusterd_get_dst_brick_info(&dst_brick, volname, op_errstr,
                                      &dst_brickinfo, &host, dict,
                                      &dup_dstbrick);
    if (ret)
        goto out;

    ret = glusterd_new_brick_validate(dst_brick, dst_brickinfo, msg,
                                      sizeof(msg), op);
    /* if bricks are not same and reset brick was used, fail command.
     * Only replace brick should be used to replace with new bricks
     * to the volume.
     */
    if (ret == 0) {
        if (!gf_uuid_compare(MY_UUID, dst_brickinfo->uuid)) {
            ret = -1;
            *op_errstr = gf_strdup(
                "When destination brick is new,"
                " please use"
                " gluster volume "
                "replace-brick <volname> "
                "<src-brick> <dst-brick> "
                "commit force");
            if (*op_errstr)
                gf_msg(this->name, GF_LOG_ERROR, EPERM,
                       GD_MSG_BRICK_VALIDATE_FAIL, "%s", *op_errstr);
            goto out;
        }
    } else if (ret == 1) {
        if (gf_is_service_running(pidfile, &pid)) {
            ret = -1;
            *op_errstr = gf_strdup(
                "Source brick"
                " must be stopped."
                " Please use "
                "gluster volume "
                "reset-brick <volname> "
                "<dst-brick> start.");
            if (*op_errstr)
                gf_msg(this->name, GF_LOG_ERROR, EPERM,
                       GD_MSG_BRICK_VALIDATE_FAIL, "%s", *op_errstr);
            goto out;
        }
        ret = sys_lgetxattr(dst_brickinfo->path, GF_XATTR_VOL_ID_KEY, volume_id,
                            16);
        if (gf_uuid_compare(dst_brickinfo->uuid, src_brickinfo->uuid) ||
            (ret >= 0 && is_force == _gf_false)) {
            ret = -1;
            *op_errstr = gf_strdup(
                "Brick not available."
                "It may be containing "
                "or be contained "
                "by an existing brick."
                "Use 'force' option to "
                "override this.");
            if (*op_errstr)
                gf_msg(this->name, GF_LOG_ERROR, EPERM,
                       GD_MSG_BRICK_VALIDATE_FAIL, "%s", *op_errstr);
            goto out;
        }
        ret = 0;
    } else {
        *op_errstr = gf_strdup(msg);
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_VALIDATE_FAIL, "%s",
               *op_errstr);
        goto out;
    }

    volinfo->rep_brick.src_brick = src_brickinfo;
    volinfo->rep_brick.dst_brick = dst_brickinfo;

    ret = dict_get_int32n(dict, "ignore-partition", SLEN("ignore-partition"),
                          &ignore_partition);
    ret = 0;
    if (glusterd_gf_is_local_addr(host)) {
        ret = glusterd_validate_and_create_brickpath(
            dst_brickinfo, volinfo->volume_id, volinfo->volname, op_errstr,
            is_force, ignore_partition);
        if (ret)
            goto out;
    } else {
        RCU_READ_LOCK;

        peerinfo = glusterd_peerinfo_find(NULL, host);
        if (peerinfo == NULL) {
            RCU_READ_UNLOCK;
            ret = -1;
            snprintf(msg, sizeof(msg), "%s, is not a friend.", host);
            *op_errstr = gf_strdup(msg);
            goto out;

        } else if (!peerinfo->connected) {
            RCU_READ_UNLOCK;
            ret = -1;
            snprintf(msg, sizeof(msg),
                     "%s,"
                     "is not connected at "
                     "the moment.",
                     host);
            *op_errstr = gf_strdup(msg);
            goto out;

        } else if (GD_FRIEND_STATE_BEFRIENDED != peerinfo->state.state) {
            RCU_READ_UNLOCK;
            ret = -1;
            snprintf(msg, sizeof(msg),
                     "%s, is not befriended "
                     "at the moment.",
                     host);
            *op_errstr = gf_strdup(msg);
            goto out;
        }
        RCU_READ_UNLOCK;
    }

    if (!(gf_uuid_compare(dst_brickinfo->uuid, MY_UUID))) {
        ret = glusterd_get_brick_mount_dir(dst_brickinfo->path,
                                           dst_brickinfo->hostname,
                                           dst_brickinfo->mount_dir);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_MOUNTDIR_GET_FAIL,
                   "Failed to get brick mount_dir");
            goto out;
        }
        ret = dict_set_dynstr_with_alloc(rsp_dict, "brick1.mount_dir",
                                         dst_brickinfo->mount_dir);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set brick.mount_dir");
            goto out;
        }
    }

    ret = dict_set_int32n(rsp_dict, "brick_count", SLEN("brick_count"), 1);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set local_brick_count.");
        goto out;
    }

done:
    ret = 0;
out:
    GF_FREE(dup_dstbrick);
    gf_msg_debug(this->name, 0, "Returning %d.", ret);

    return ret;
}

int
glusterd_op_reset_brick(dict_t *dict, dict_t *rsp_dict)
{
    int ret = 0;
    char *op = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    char *volname = NULL;
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;
    char *src_brick = NULL;
    char *dst_brick = NULL;
    glusterd_brickinfo_t *src_brickinfo = NULL;
    glusterd_brickinfo_t *dst_brickinfo = NULL;

    this = THIS;
    GF_ASSERT(this);

    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_strn(dict, "operation", SLEN("operation"), &op);
    if (ret) {
        gf_msg_debug(this->name, 0, "dict_get on operation failed");
        goto out;
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret)
        goto out;

    ret = dict_get_strn(dict, "src-brick", SLEN("src-brick"), &src_brick);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get src brick");
        goto out;
    }

    gf_msg_debug(this->name, 0, "src brick=%s", src_brick);

    ret = glusterd_volume_brickinfo_get_by_brick(src_brick, volinfo,
                                                 &src_brickinfo, _gf_false);
    if (ret) {
        gf_msg_debug(this->name, 0, "Unable to get src-brickinfo");
        goto out;
    }

    if (!strcmp(op, "GF_RESET_OP_START")) {
        ret = glusterd_volume_stop_glusterfs(volinfo, src_brickinfo, _gf_false);
        if (ret) {
            gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_BRICK_STOP_FAIL,
                   "Unable to stop"
                   " brick: %s:%s",
                   src_brickinfo->hostname, src_brickinfo->path);
        }

        goto out;

    } else if (!strcmp(op, "GF_RESET_OP_COMMIT") ||
               !strcmp(op, "GF_RESET_OP_COMMIT_FORCE")) {
        ret = dict_get_strn(dict, "dst-brick", SLEN("dst-brick"), &dst_brick);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get dst brick");
            goto out;
        }

        gf_msg_debug(this->name, 0, "dst brick=%s", dst_brick);

        ret = glusterd_get_rb_dst_brickinfo(volinfo, &dst_brickinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RB_BRICKINFO_GET_FAIL,
                   "Unable to get "
                   "reset brick "
                   "destination brickinfo");
            goto out;
        }

        ret = glusterd_resolve_brick(dst_brickinfo);
        if (ret) {
            gf_msg_debug(this->name, 0, "Unable to resolve dst-brickinfo");
            goto out;
        }

        ret = rb_update_dstbrick_port(dst_brickinfo, rsp_dict, dict);
        if (ret)
            goto out;

        if (gf_uuid_compare(dst_brickinfo->uuid, MY_UUID)) {
            gf_msg_debug(this->name, 0, "I AM THE DESTINATION HOST");
            ret = glusterd_volume_stop_glusterfs(volinfo, src_brickinfo,
                                                 _gf_false);
            if (ret) {
                gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_BRICK_STOP_FAIL,
                       "Unable to stop brick: %s:%s", src_brickinfo->hostname,
                       src_brickinfo->path);
                goto out;
            }
        }

        ret = glusterd_svcs_stop(volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_GLUSTER_SERVICES_STOP_FAIL,
                   "Unable to stop gluster services, ret: %d", ret);
            goto out;
        }
        ret = glusterd_op_perform_replace_brick(volinfo, src_brick, dst_brick,
                                                dict);
        if (ret) {
            gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_BRICK_ADD_FAIL,
                   "Unable to add dst-brick: "
                   "%s to volume: %s",
                   dst_brick, volinfo->volname);
            (void)glusterd_svcs_manager(volinfo);
            goto out;
        }

        volinfo->rebal.defrag_status = 0;

        ret = glusterd_svcs_manager(volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_CRITICAL, 0,
                   GD_MSG_GLUSTER_SERVICE_START_FAIL,
                   "Failed to start one or more gluster services.");
        }

        ret = glusterd_fetchspec_notify(THIS);
        glusterd_brickinfo_delete(volinfo->rep_brick.dst_brick);
        volinfo->rep_brick.src_brick = NULL;
        volinfo->rep_brick.dst_brick = NULL;

        if (!ret)
            ret = glusterd_store_volinfo(volinfo,
                                         GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RBOP_STATE_STORE_FAIL,
                   "Couldn't store"
                   " reset brick operation's state.");
        }
    } else {
        ret = -1;
        goto out;
    }

out:
    return ret;
}
