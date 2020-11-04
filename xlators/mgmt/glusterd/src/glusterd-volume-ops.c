/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <glusterfs/common-utils.h>
#include <glusterfs/syscall.h>
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-geo-rep.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "glusterd-messages.h"
#include <glusterfs/run.h>
#include "glusterd-snapshot-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-svc-helper.h"
#include "glusterd-shd-svc.h"
#include "glusterd-snapd-svc.h"
#include "glusterd-mgmt.h"
#include "glusterd-server-quorum.h"

#include <stdint.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>

#define glusterd_op_start_volume_args_get(dict, volname, flags)                \
    glusterd_op_stop_volume_args_get(dict, volname, flags)

int
__glusterd_handle_create_volume(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gf_cli_req cli_req = {{
        0,
    }};
    dict_t *dict = NULL;
    char *bricks = NULL;
    char *volname = NULL;
    int brick_count = 0;
    int thin_arbiter_count = 0;
    void *cli_rsp = NULL;
    char err_str[2048] = {
        0,
    };
    gf_cli_rsp rsp = {
        0,
    };
    xlator_t *this = NULL;
    glusterd_conf_t *conf = NULL;
    char *free_ptr = NULL;
    char *trans_type = NULL;
    char *address_family_str = NULL;
    uuid_t volume_id = {
        0,
    };
    uuid_t tmp_uuid = {0};
    int32_t type = 0;
    char *username = NULL;
    char *password = NULL;
#ifdef IPV6_DEFAULT
    char *addr_family = "inet6";
#else
    char *addr_family = "inet";
#endif
    glusterd_volinfo_t *volinfo = NULL;

    GF_ASSERT(req);

    this = THIS;
    GF_ASSERT(this);

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    ret = -1;
    ret = xdr_to_generic(req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        snprintf(err_str, sizeof(err_str),
                 "Failed to decode request "
                 "received from cli");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REQ_DECODE_FAIL, "%s",
               err_str);
        goto out;
    }

    gf_msg_debug(this->name, 0, "Received create volume req");

    if (cli_req.dict.dict_len) {
        /* Unserialize the dictionary */
        dict = dict_new();

        ret = dict_unserialize(cli_req.dict.dict_val, cli_req.dict.dict_len,
                               &dict);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_UNSERIALIZE_FAIL,
                   "failed to "
                   "unserialize req-buffer to dictionary");
            snprintf(err_str, sizeof(err_str),
                     "Unable to decode "
                     "the command");
            goto out;
        } else {
            dict->extra_stdfree = cli_req.dict.dict_val;
        }
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);

    if (ret) {
        snprintf(err_str, sizeof(err_str),
                 "Unable to get volume "
                 "name");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               err_str);
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (!ret) {
        ret = -1;
        snprintf(err_str, sizeof(err_str), "Volume %s already exists", volname);
        gf_msg(this->name, GF_LOG_ERROR, EEXIST, GD_MSG_VOL_ALREADY_EXIST, "%s",
               err_str);
        goto out;
    }

    ret = dict_get_int32n(dict, "count", SLEN("count"), &brick_count);
    if (ret) {
        snprintf(err_str, sizeof(err_str),
                 "Unable to get brick count"
                 " for volume %s",
                 volname);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               err_str);
        goto out;
    }

    ret = dict_get_int32n(dict, "type", SLEN("type"), &type);
    if (ret) {
        snprintf(err_str, sizeof(err_str),
                 "Unable to get type of "
                 "volume %s",
                 volname);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               err_str);
        goto out;
    }

    ret = dict_get_strn(dict, "transport", SLEN("transport"), &trans_type);
    if (ret) {
        snprintf(err_str, sizeof(err_str),
                 "Unable to get "
                 "transport-type of volume %s",
                 volname);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               err_str);
        goto out;
    }

    ret = dict_get_strn(this->options, "transport.address-family",
                        SLEN("transport.address-family"), &address_family_str);

    if (!ret) {
        ret = dict_set_dynstr_with_alloc(dict, "transport.address-family",
                                         address_family_str);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR,
                   "failed to set transport.address-family");
            goto out;
        }
    } else if (!strcmp(trans_type, "tcp")) {
        /* Setting default as inet for trans_type tcp if the op-version
         * is >= 3.8.0
         */
        if (conf->op_version >= GD_OP_VERSION_3_8_0) {
            ret = dict_set_dynstr_with_alloc(dict, "transport.address-family",
                                             addr_family);
            if (ret) {
                gf_log(this->name, GF_LOG_ERROR,
                       "failed to set "
                       "transport.address-family "
                       "to %s",
                       addr_family);
                goto out;
            }
        }
    }
    ret = dict_get_strn(dict, "bricks", SLEN("bricks"), &bricks);
    if (ret) {
        snprintf(err_str, sizeof(err_str),
                 "Unable to get bricks for "
                 "volume %s",
                 volname);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               err_str);
        goto out;
    }

    ret = dict_get_int32n(dict, "thin-arbiter-count",
                          SLEN("thin-arbiter-count"), &thin_arbiter_count);
    if (thin_arbiter_count && conf->op_version < GD_OP_VERSION_7_0) {
        snprintf(err_str, sizeof(err_str),
                 "Cannot execute command. "
                 "The cluster is operating at version %d. "
                 "Thin-arbiter volume creation is unavailable in "
                 "this version",
                 conf->op_version);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_OP_FAILED, "%s",
               err_str);
        ret = -1;
        goto out;
    }

    if (!dict_getn(dict, "force", SLEN("force"))) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get 'force' flag");
        goto out;
    }

    gf_uuid_generate(volume_id);
    free_ptr = gf_strdup(uuid_utoa(volume_id));
    ret = dict_set_dynstrn(dict, "volume-id", SLEN("volume-id"), free_ptr);
    if (ret) {
        snprintf(err_str, sizeof(err_str),
                 "Unable to set volume "
                 "id of volume %s",
                 volname);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED, "%s",
               err_str);
        goto out;
    }
    free_ptr = NULL;

    /* generate internal username and password */

    gf_uuid_generate(tmp_uuid);
    username = gf_strdup(uuid_utoa(tmp_uuid));
    ret = dict_set_dynstrn(dict, "internal-username", SLEN("internal-username"),
                           username);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set username for "
               "volume %s",
               volname);
        goto out;
    }

    gf_uuid_generate(tmp_uuid);
    password = gf_strdup(uuid_utoa(tmp_uuid));
    ret = dict_set_dynstrn(dict, "internal-password", SLEN("internal-password"),
                           password);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set password for "
               "volume %s",
               volname);
        goto out;
    }

    ret = glusterd_op_begin_synctask(req, GD_OP_CREATE_VOLUME, dict);

out:
    if (ret) {
        rsp.op_ret = -1;
        rsp.op_errno = 0;
        if (err_str[0] == '\0')
            snprintf(err_str, sizeof(err_str), "Operation failed");
        rsp.op_errstr = err_str;
        cli_rsp = &rsp;
        glusterd_to_cli(req, cli_rsp, NULL, 0, NULL, (xdrproc_t)xdr_gf_cli_rsp,
                        dict);
        ret = 0;  // Client response sent, prevent second response
    }

    GF_FREE(free_ptr);

    return ret;
}

int
glusterd_handle_create_volume(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, __glusterd_handle_create_volume);
}

int
__glusterd_handle_cli_start_volume(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gf_cli_req cli_req = {{
        0,
    }};
    char *volname = NULL;
    dict_t *dict = NULL;
    glusterd_op_t cli_op = GD_OP_START_VOLUME;
    char errstr[2048] = {
        0,
    };
    xlator_t *this = NULL;
    glusterd_conf_t *conf = NULL;

    this = THIS;
    GF_ASSERT(this);
    GF_ASSERT(req);

    conf = this->private;
    GF_ASSERT(conf);
    ret = xdr_to_generic(req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
    if (ret < 0) {
        snprintf(errstr, sizeof(errstr),
                 "Failed to decode message "
                 "received from cli");
        req->rpc_err = GARBAGE_ARGS;
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REQ_DECODE_FAIL, "%s",
               errstr);
        goto out;
    }

    if (cli_req.dict.dict_len) {
        /* Unserialize the dictionary */
        dict = dict_new();

        ret = dict_unserialize(cli_req.dict.dict_val, cli_req.dict.dict_len,
                               &dict);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_UNSERIALIZE_FAIL,
                   "failed to "
                   "unserialize req-buffer to dictionary");
            snprintf(errstr, sizeof(errstr),
                     "Unable to decode "
                     "the command");
            goto out;
        }
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        snprintf(errstr, sizeof(errstr), "Unable to get volume name");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               errstr);
        goto out;
    }

    gf_msg_debug(this->name, 0,
                 "Received start vol req"
                 " for volume %s",
                 volname);

    if (conf->op_version <= GD_OP_VERSION_3_7_6) {
        gf_msg_debug(this->name, 0,
                     "The cluster is operating at "
                     "version less than or equal to %d. Volume start "
                     "falling back to syncop framework.",
                     GD_OP_VERSION_3_7_6);
        ret = glusterd_op_begin_synctask(req, GD_OP_START_VOLUME, dict);
    } else {
        ret = glusterd_mgmt_v3_initiate_all_phases(req, GD_OP_START_VOLUME,
                                                   dict);
    }
out:
    free(cli_req.dict.dict_val);  // its malloced by xdr

    if (ret) {
        if (errstr[0] == '\0')
            snprintf(errstr, sizeof(errstr), "Operation failed");
        ret = glusterd_op_send_cli_response(cli_op, ret, 0, req, dict, errstr);
    }

    return ret;
}

int
glusterd_handle_cli_start_volume(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, __glusterd_handle_cli_start_volume);
}

int
__glusterd_handle_cli_stop_volume(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gf_cli_req cli_req = {{
        0,
    }};
    char *dup_volname = NULL;
    dict_t *dict = NULL;
    glusterd_op_t cli_op = GD_OP_STOP_VOLUME;
    xlator_t *this = NULL;
    char err_str[64] = {
        0,
    };
    glusterd_conf_t *conf = NULL;

    this = THIS;
    GF_ASSERT(this);
    GF_ASSERT(req);
    conf = this->private;
    GF_ASSERT(conf);

    ret = xdr_to_generic(req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
    if (ret < 0) {
        snprintf(err_str, sizeof(err_str),
                 "Failed to decode message "
                 "received from cli");
        req->rpc_err = GARBAGE_ARGS;
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REQ_DECODE_FAIL, "%s",
               err_str);
        goto out;
    }
    if (cli_req.dict.dict_len) {
        /* Unserialize the dictionary */
        dict = dict_new();

        ret = dict_unserialize(cli_req.dict.dict_val, cli_req.dict.dict_len,
                               &dict);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_UNSERIALIZE_FAIL,
                   "failed to "
                   "unserialize req-buffer to dictionary");
            snprintf(err_str, sizeof(err_str),
                     "Unable to decode "
                     "the command");
            goto out;
        }
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &dup_volname);

    if (ret) {
        snprintf(err_str, sizeof(err_str),
                 "Failed to get volume "
                 "name");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               err_str);
        goto out;
    }

    gf_msg_debug(this->name, 0,
                 "Received stop vol req "
                 "for volume %s",
                 dup_volname);

    if (conf->op_version < GD_OP_VERSION_4_1_0) {
        gf_msg_debug(this->name, 0,
                     "The cluster is operating at "
                     "version less than %d. Volume start "
                     "falling back to syncop framework.",
                     GD_OP_VERSION_4_1_0);
        ret = glusterd_op_begin_synctask(req, GD_OP_STOP_VOLUME, dict);
    } else {
        ret = glusterd_mgmt_v3_initiate_all_phases(req, GD_OP_STOP_VOLUME,
                                                   dict);
    }

out:
    free(cli_req.dict.dict_val);  // its malloced by xdr

    if (ret) {
        if (err_str[0] == '\0')
            snprintf(err_str, sizeof(err_str), "Operation failed");
        ret = glusterd_op_send_cli_response(cli_op, ret, 0, req, dict, err_str);
    }

    return ret;
}

int
glusterd_handle_cli_stop_volume(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, __glusterd_handle_cli_stop_volume);
}

int
__glusterd_handle_cli_delete_volume(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gf_cli_req cli_req = {
        {
            0,
        },
    };
    glusterd_op_t cli_op = GD_OP_DELETE_VOLUME;
    dict_t *dict = NULL;
    char *volname = NULL;
    char err_str[64] = {
        0,
    };
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    GF_ASSERT(req);

    ret = xdr_to_generic(req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
    if (ret < 0) {
        snprintf(err_str, sizeof(err_str),
                 "Failed to decode request "
                 "received from cli");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REQ_DECODE_FAIL, "%s",
               err_str);
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    if (cli_req.dict.dict_len) {
        /* Unserialize the dictionary */
        dict = dict_new();

        ret = dict_unserialize(cli_req.dict.dict_val, cli_req.dict.dict_len,
                               &dict);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_UNSERIALIZE_FAIL,
                   "failed to "
                   "unserialize req-buffer to dictionary");
            snprintf(err_str, sizeof(err_str),
                     "Unable to decode "
                     "the command");
            goto out;
        }
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        snprintf(err_str, sizeof(err_str),
                 "Failed to get volume "
                 "name");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               err_str);
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    gf_msg_debug(this->name, 0,
                 "Received delete vol req"
                 "for volume %s",
                 volname);

    ret = glusterd_op_begin_synctask(req, GD_OP_DELETE_VOLUME, dict);

out:
    free(cli_req.dict.dict_val);  // its malloced by xdr

    if (ret) {
        if (err_str[0] == '\0')
            snprintf(err_str, sizeof(err_str), "Operation failed");
        ret = glusterd_op_send_cli_response(cli_op, ret, 0, req, dict, err_str);
    }

    return ret;
}
int
glusterd_handle_cli_delete_volume(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req,
                                       __glusterd_handle_cli_delete_volume);
}
static int
glusterd_handle_heal_options_enable_disable(rpcsvc_request_t *req, dict_t *dict,
                                            glusterd_volinfo_t *volinfo)
{
    gf_xl_afr_op_t heal_op = GF_SHD_OP_INVALID;
    int ret = 0;
    char *key = NULL;
    char *value = NULL;
    xlator_t *this = THIS;
    GF_ASSERT(this);

    ret = dict_get_int32n(dict, "heal-op", SLEN("heal-op"),
                          (int32_t *)&heal_op);
    if (ret || (heal_op == GF_SHD_OP_INVALID)) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_GET_FAILED,
                "Key=heal-op", NULL);
        ret = -1;
        goto out;
    }

    if ((heal_op != GF_SHD_OP_HEAL_ENABLE) &&
        (heal_op != GF_SHD_OP_HEAL_DISABLE) &&
        (heal_op != GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE) &&
        (heal_op != GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE)) {
        ret = -EINVAL;
        goto out;
    }

    if (((heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE) ||
         (heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE)) &&
        (volinfo->type != GF_CLUSTER_TYPE_REPLICATE)) {
        ret = -1;
        goto out;
    }

    if ((heal_op == GF_SHD_OP_HEAL_ENABLE) ||
        (heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE)) {
        value = "enable";
    } else if ((heal_op == GF_SHD_OP_HEAL_DISABLE) ||
               (heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE)) {
        value = "disable";
    }

    if ((heal_op == GF_SHD_OP_HEAL_ENABLE) ||
        (heal_op == GF_SHD_OP_HEAL_DISABLE)) {
        key = volgen_get_shd_key(volinfo->type);
        if (!key) {
            ret = -1;
            goto out;
        }
    } else {
        key = "cluster.granular-entry-heal";
        ret = dict_set_int8(dict, "is-special-key", 1);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                    "Key=is-special-key", NULL);
            goto out;
        }
    }

    ret = dict_set_strn(dict, "key1", SLEN("key1"), key);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=key1", NULL);
        goto out;
    }

    ret = dict_set_strn(dict, "value1", SLEN("value1"), value);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=value1", NULL);
        goto out;
    }

    ret = dict_set_int32n(dict, "count", SLEN("count"), 1);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=count", NULL);
        goto out;
    }

    ret = glusterd_op_begin_synctask(req, GD_OP_SET_VOLUME, dict);

out:
    return ret;
}

int
__glusterd_handle_cli_heal_volume(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gf_cli_req cli_req = {{
        0,
    }};
    dict_t *dict = NULL;
    glusterd_op_t cli_op = GD_OP_HEAL_VOLUME;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = NULL;
    char op_errstr[2048] = {
        0,
    };

    this = THIS;
    GF_ASSERT(this);

    GF_ASSERT(req);

    ret = xdr_to_generic(req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
    if (ret < 0) {
        // failed to decode msg;
        req->rpc_err = GARBAGE_ARGS;
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto out;
    }

    if (cli_req.dict.dict_len) {
        /* Unserialize the dictionary */
        dict = dict_new();

        ret = dict_unserialize(cli_req.dict.dict_val, cli_req.dict.dict_len,
                               &dict);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_UNSERIALIZE_FAIL,
                   "failed to "
                   "unserialize req-buffer to dictionary");
            snprintf(op_errstr, sizeof(op_errstr),
                     "Unable to decode the command");
            goto out;
        } else {
            dict->extra_stdfree = cli_req.dict.dict_val;
        }
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        snprintf(op_errstr, sizeof(op_errstr),
                 "Unable to find "
                 "volume name");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               op_errstr);
        goto out;
    }

    gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_HEAL_VOL_REQ_RCVD,
           "Received heal vol req "
           "for volume %s",
           volname);

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(op_errstr, sizeof(op_errstr), "Volume %s does not exist",
                 volname);
        goto out;
    }

    ret = glusterd_handle_heal_options_enable_disable(req, dict, volinfo);
    if (ret == -EINVAL) {
        ret = 0;
    } else {
        /*
         * If the return value is -ve but not -EINVAL then the command
         * failed. If the return value is 0 then the synctask for the
         * op has begun, so in both cases just 'goto out'. If there was
         * a failure it will respond with an error, otherwise the
         * synctask will take the responsibility of sending the
         * response.
         */
        goto out;
    }

    ret = glusterd_add_bricks_hname_path_to_dict(dict, volinfo);
    if (ret)
        goto out;

    ret = dict_set_int32n(dict, "count", SLEN("count"), volinfo->brick_count);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=count", NULL);
        goto out;
    }

    ret = glusterd_op_begin_synctask(req, GD_OP_HEAL_VOLUME, dict);

out:
    if (ret) {
        if (op_errstr[0] == '\0')
            snprintf(op_errstr, sizeof(op_errstr), "operation failed");
        gf_msg((this ? this->name : "glusterd"), GF_LOG_ERROR, 0,
               GD_MSG_GLUSTERD_OP_FAILED, "%s", op_errstr);
        ret = glusterd_op_send_cli_response(cli_op, ret, 0, req, dict,
                                            op_errstr);
    }

    return ret;
}

int
glusterd_handle_cli_heal_volume(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, __glusterd_handle_cli_heal_volume);
}

int
__glusterd_handle_cli_statedump_volume(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gf_cli_req cli_req = {{
        0,
    }};
    char *volname = NULL;
    char *options = NULL;
    dict_t *dict = NULL;
    int32_t option_cnt = 0;
    glusterd_op_t cli_op = GD_OP_STATEDUMP_VOLUME;
    char err_str[128] = {
        0,
    };
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    GF_ASSERT(req);

    ret = -1;
    ret = xdr_to_generic(req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto out;
    }
    if (cli_req.dict.dict_len) {
        /* Unserialize the dictionary */
        dict = dict_new();

        ret = dict_unserialize(cli_req.dict.dict_val, cli_req.dict.dict_len,
                               &dict);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_UNSERIALIZE_FAIL,
                   "failed to "
                   "unserialize req-buffer to dictionary");
            snprintf(err_str, sizeof(err_str),
                     "Unable to "
                     "decode the command");
            goto out;
        }
    }
    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        snprintf(err_str, sizeof(err_str), "Unable to get the volume name");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               err_str);
        goto out;
    }

    ret = dict_get_strn(dict, "options", SLEN("options"), &options);
    if (ret) {
        snprintf(err_str, sizeof(err_str), "Unable to get options");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               err_str);
        goto out;
    }

    ret = dict_get_int32n(dict, "option_cnt", SLEN("option_cnt"), &option_cnt);
    if (ret) {
        snprintf(err_str, sizeof(err_str),
                 "Unable to get option "
                 "count");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               err_str);
        goto out;
    }

    if (priv->op_version == GD_OP_VERSION_MIN && strstr(options, "quotad")) {
        snprintf(err_str, sizeof(err_str),
                 "The cluster is operating "
                 "at op-version 1. Taking quotad's statedump is "
                 "disallowed in this state");
        ret = -1;
        goto out;
    }

    gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_STATEDUMP_VOL_REQ_RCVD,
           "Received statedump request for "
           "volume %s with options %s",
           volname, options);

    ret = glusterd_op_begin_synctask(req, GD_OP_STATEDUMP_VOLUME, dict);

out:
    if (ret) {
        if (err_str[0] == '\0')
            snprintf(err_str, sizeof(err_str), "Operation failed");
        ret = glusterd_op_send_cli_response(cli_op, ret, 0, req, dict, err_str);
    }
    free(cli_req.dict.dict_val);

    return ret;
}

int
glusterd_handle_cli_statedump_volume(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req,
                                       __glusterd_handle_cli_statedump_volume);
}

/* op-sm */
int
glusterd_op_stage_create_volume(dict_t *dict, char **op_errstr,
                                dict_t *rsp_dict)
{
    int ret = 0;
    char *volname = NULL;
    char *bricks = NULL;
    char *brick_list = NULL;
    char *free_ptr = NULL;
    char key[64] = "";
    glusterd_brickinfo_t *brick_info = NULL;
    int32_t brick_count = 0;
    int32_t local_brick_count = 0;
    int32_t i = 0;
    int32_t type = 0;
    int32_t replica_count = 0;
    int32_t disperse_count = 0;
    char *brick = NULL;
    char *tmpptr = NULL;
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;
    char msg[2048] = {0};
    uuid_t volume_uuid;
    char *volume_uuid_str;
    gf_boolean_t is_force = _gf_false;
    glusterd_volinfo_t *volinfo = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);
    GF_ASSERT(rsp_dict);

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (!ret) {
        snprintf(msg, sizeof(msg), "Volume %s already exists", volname);
        ret = -1;
        goto out;
    }

    ret = dict_get_int32n(dict, "count", SLEN("count"), &brick_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get brick count "
               "for volume %s",
               volname);
        goto out;
    }

    ret = dict_get_strn(dict, "volume-id", SLEN("volume-id"), &volume_uuid_str);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume id of "
               "volume %s",
               volname);
        goto out;
    }

    ret = gf_uuid_parse(volume_uuid_str, volume_uuid);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UUID_PARSE_FAIL,
               "Unable to parse volume id of"
               " volume %s",
               volname);
        goto out;
    }

    ret = dict_get_strn(dict, "bricks", SLEN("bricks"), &bricks);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get bricks for "
               "volume %s",
               volname);
        goto out;
    }

    is_force = dict_get_str_boolean(dict, "force", _gf_false);

    if (bricks) {
        brick_list = gf_strdup(bricks);
        if (!brick_list) {
            ret = -1;
            goto out;
        } else {
            free_ptr = brick_list;
        }
    }

    /*Check brick order if the volume type is replicate or disperse. If
     * force at the end of command not given then check brick order.
     */
    if (is_origin_glusterd(dict)) {
        ret = dict_get_int32n(dict, "type", SLEN("type"), &type);
        if (ret) {
            snprintf(msg, sizeof(msg),
                     "Unable to get type of "
                     "volume %s",
                     volname);
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED, "%s",
                   msg);
            goto out;
        }

        if (!is_force) {
            if (type == GF_CLUSTER_TYPE_REPLICATE) {
                ret = dict_get_int32n(dict, "replica-count",
                                      SLEN("replica-count"), &replica_count);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                           "Bricks check : Could"
                           " not retrieve replica count");
                    goto out;
                }
                gf_msg_debug(this->name, 0,
                             "Replicate cluster type "
                             "found. Checking brick order.");
                ret = glusterd_check_brick_order(dict, msg, type, &volname,
                                                 &bricks, &brick_count,
                                                 replica_count);
            } else if (type == GF_CLUSTER_TYPE_DISPERSE) {
                ret = dict_get_int32n(dict, "disperse-count",
                                      SLEN("disperse-count"), &disperse_count);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                           "Bricks check : Could"
                           " not retrieve disperse count");
                    goto out;
                }
                gf_msg_debug(this->name, 0,
                             "Disperse cluster type"
                             " found. Checking brick order.");
                ret = glusterd_check_brick_order(dict, msg, type, &volname,
                                                 &bricks, &brick_count,
                                                 disperse_count);
            }
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BAD_BRKORDER,
                       "Not creating the volume because of "
                       "bad brick order. %s",
                       msg);
                *op_errstr = gf_strdup(msg);
                goto out;
            }
        }
    }

    while (i < brick_count) {
        i++;
        brick = strtok_r(brick_list, " \n", &tmpptr);
        brick_list = tmpptr;

        if (!glusterd_store_is_valid_brickpath(volname, brick)) {
            snprintf(msg, sizeof(msg),
                     "brick path %s is too "
                     "long.",
                     brick);
            ret = -1;
            goto out;
        }

        if (!glusterd_is_valid_volfpath(volname, brick)) {
            snprintf(msg, sizeof(msg),
                     "Volume file path for "
                     "volume %s and brick path %s is too long.",
                     volname, brick);
            ret = -1;
            goto out;
        }

        ret = glusterd_brickinfo_new_from_brick(brick, &brick_info, _gf_true,
                                                op_errstr);
        if (ret)
            goto out;

        ret = glusterd_new_brick_validate(brick, brick_info, msg, sizeof(msg),
                                          NULL);
        if (ret)
            goto out;

        ret = glusterd_resolve_brick(brick_info);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RESOLVE_BRICK_FAIL,
                   FMTSTR_RESOLVE_BRICK, brick_info->hostname,
                   brick_info->path);
            goto out;
        }

        if (!gf_uuid_compare(brick_info->uuid, MY_UUID)) {
            ret = glusterd_validate_and_create_brickpath(
                brick_info, volume_uuid, volname, op_errstr, is_force,
                _gf_false);
            if (ret)
                goto out;

            /* A bricks mount dir is required only by snapshots which were
             * introduced in gluster-3.6.0
             */
            if (priv->op_version >= GD_OP_VERSION_3_6_0) {
                ret = glusterd_get_brick_mount_dir(brick_info->path,
                                                   brick_info->hostname,
                                                   brick_info->mount_dir);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_BRICK_MOUNTDIR_GET_FAIL,
                           "Failed to get brick mount_dir");
                    goto out;
                }

                snprintf(key, sizeof(key), "brick%d.mount_dir", i);
                ret = dict_set_dynstr_with_alloc(rsp_dict, key,
                                                 brick_info->mount_dir);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                           "Failed to set %s", key);
                    goto out;
                }
            }
            local_brick_count = i;

            brick_list = tmpptr;
        }
        glusterd_brickinfo_delete(brick_info);
        brick_info = NULL;
    }

    ret = dict_set_int32n(rsp_dict, "brick_count", SLEN("brick_count"),
                          local_brick_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set local_brick_count");
        goto out;
    }
out:
    GF_FREE(free_ptr);
    if (brick_info)
        glusterd_brickinfo_delete(brick_info);

    if (msg[0] != '\0') {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_OP_STAGE_CREATE_VOL_FAIL,
               "%s", msg);
        *op_errstr = gf_strdup(msg);
    }
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

int
glusterd_op_stop_volume_args_get(dict_t *dict, char **volname, int *flags)
{
    int ret = -1;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    if (!dict) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    if (!volname) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    if (!flags) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), volname);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                "Key=volname", NULL);
        goto out;
    }

    ret = dict_get_int32n(dict, "flags", SLEN("flags"), flags);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                "Key=flags", NULL);
        goto out;
    }
out:
    return ret;
}

int
glusterd_op_statedump_volume_args_get(dict_t *dict, char **volname,
                                      char **options, int *option_cnt)
{
    int ret = -1;

    if (!dict || !volname || !options || !option_cnt) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), volname);
    if (ret) {
        gf_smsg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                "Key=volname", NULL);
        goto out;
    }

    ret = dict_get_strn(dict, "options", SLEN("options"), options);
    if (ret) {
        gf_smsg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                "Key=options", NULL);
        goto out;
    }

    ret = dict_get_int32n(dict, "option_cnt", SLEN("option_cnt"), option_cnt);
    if (ret) {
        gf_smsg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                "Key=option_cnt", NULL);
        goto out;
    }

out:
    return ret;
}

int
glusterd_op_stage_start_volume(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    int ret = 0;
    char *volname = NULL;
    char key[64] = "";
    int flags = 0;
    int32_t brick_count = 0;
    int32_t local_brick_count = 0;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    char msg[2048] = {
        0,
    };
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;
    uuid_t volume_id = {
        0,
    };
    char volid[50] = {
        0,
    };
    char xattr_volid[50] = {
        0,
    };
    int32_t len = 0;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);
    GF_ASSERT(rsp_dict);

    ret = glusterd_op_start_volume_args_get(dict, &volname, &flags);
    if (ret)
        goto out;

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), FMTSTR_CHECK_VOL_EXISTS, volname);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
               FMTSTR_CHECK_VOL_EXISTS, volname);
        goto out;
    }

    /* This is an incremental approach to have all the volinfo objects ref
     * count. The first attempt is made in volume start transaction to
     * ensure it doesn't race with import volume where stale volume is
     * deleted. There are multiple instances of GlusterD crashing in
     * bug-948686.t because of this. Once this approach is full proof, all
     * other volinfo objects will be refcounted.
     */
    glusterd_volinfo_ref(volinfo);

    if (priv->op_version > GD_OP_VERSION_3_7_5) {
        ret = glusterd_validate_quorum(this, GD_OP_START_VOLUME, dict,
                                       op_errstr);
        if (ret) {
            gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_SERVER_QUORUM_NOT_MET,
                   "Server quorum not met. Rejecting operation.");
            goto out;
        }
    }

    ret = glusterd_validate_volume_id(dict, volinfo);
    if (ret)
        goto out;

    if (!(flags & GF_CLI_FLAG_OP_FORCE)) {
        if (glusterd_is_volume_started(volinfo)) {
            snprintf(msg, sizeof(msg),
                     "Volume %s already "
                     "started",
                     volname);
            ret = -1;
            goto out;
        }
    }

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        brick_count++;
        ret = glusterd_resolve_brick(brickinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RESOLVE_BRICK_FAIL,
                   FMTSTR_RESOLVE_BRICK, brickinfo->hostname, brickinfo->path);
            goto out;
        }

        if ((gf_uuid_compare(brickinfo->uuid, MY_UUID)) ||
            (brickinfo->snap_status == -1))
            continue;

        ret = gf_lstat_dir(brickinfo->path, NULL);
        if (ret && (flags & GF_CLI_FLAG_OP_FORCE)) {
            continue;
        } else if (ret) {
            len = snprintf(msg, sizeof(msg),
                           "Failed to find "
                           "brick directory %s for volume %s. "
                           "Reason : %s",
                           brickinfo->path, volname, strerror(errno));
            if (len < 0) {
                strcpy(msg, "<error>");
            }
            goto out;
        }
        ret = sys_lgetxattr(brickinfo->path, GF_XATTR_VOL_ID_KEY, volume_id,
                            16);
        if (ret < 0 && (!(flags & GF_CLI_FLAG_OP_FORCE))) {
            len = snprintf(msg, sizeof(msg),
                           "Failed to get "
                           "extended attribute %s for brick dir "
                           "%s. Reason : %s",
                           GF_XATTR_VOL_ID_KEY, brickinfo->path,
                           strerror(errno));
            if (len < 0) {
                strcpy(msg, "<error>");
            }
            ret = -1;
            goto out;
        } else if (ret < 0) {
            ret = sys_lsetxattr(brickinfo->path, GF_XATTR_VOL_ID_KEY,
                                volinfo->volume_id, 16, XATTR_CREATE);
            if (ret == -1) {
                len = snprintf(msg, sizeof(msg),
                               "Failed to "
                               "set extended attribute %s on "
                               "%s. Reason: %s",
                               GF_XATTR_VOL_ID_KEY, brickinfo->path,
                               strerror(errno));
                if (len < 0) {
                    strcpy(msg, "<error>");
                }
                goto out;
            } else {
                continue;
            }
        }
        if (gf_uuid_compare(volinfo->volume_id, volume_id)) {
            len = snprintf(msg, sizeof(msg),
                           "Volume id "
                           "mismatch for brick %s:%s. Expected "
                           "volume id %s, volume id %s found",
                           brickinfo->hostname, brickinfo->path,
                           uuid_utoa_r(volinfo->volume_id, volid),
                           uuid_utoa_r(volume_id, xattr_volid));
            if (len < 0) {
                strcpy(msg, "<error>");
            }
            ret = -1;
            goto out;
        }

        /* A bricks mount dir is required only by snapshots which were
         * introduced in gluster-3.6.0
         */
        if (priv->op_version >= GD_OP_VERSION_3_6_0) {
            if (strlen(brickinfo->mount_dir) < 1) {
                ret = glusterd_get_brick_mount_dir(
                    brickinfo->path, brickinfo->hostname, brickinfo->mount_dir);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_BRICK_MOUNTDIR_GET_FAIL,
                           "Failed to get brick mount_dir");
                    goto out;
                }

                snprintf(key, sizeof(key), "brick%d.mount_dir", brick_count);
                ret = dict_set_dynstr_with_alloc(rsp_dict, key,
                                                 brickinfo->mount_dir);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                           "Failed to set %s", key);
                    goto out;
                }
                local_brick_count = brick_count;
            }
        }
    }

    ret = dict_set_int32n(rsp_dict, "brick_count", SLEN("brick_count"),
                          local_brick_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set local_brick_count");
        goto out;
    }

    ret = 0;
out:
    if (volinfo)
        glusterd_volinfo_unref(volinfo);

    if (ret && (msg[0] != '\0')) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_OP_STAGE_START_VOL_FAIL,
               "%s", msg);
        *op_errstr = gf_strdup(msg);
    }
    return ret;
}

int
glusterd_op_stage_stop_volume(dict_t *dict, char **op_errstr)
{
    int ret = -1;
    char *volname = NULL;
    int flags = 0;
    glusterd_volinfo_t *volinfo = NULL;
    char msg[2048] = {0};
    xlator_t *this = NULL;
    gsync_status_param_t param = {
        0,
    };

    this = THIS;
    GF_ASSERT(this);

    ret = glusterd_op_stop_volume_args_get(dict, &volname, &flags);
    if (ret) {
        snprintf(msg, sizeof(msg), "Failed to get details of volume %s",
                 volname);
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_STOP_ARGS_GET_FAILED,
                "Volume name=%s", volname, NULL);
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), FMTSTR_CHECK_VOL_EXISTS, volname);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL, "%s", msg);
        goto out;
    }

    ret = glusterd_validate_volume_id(dict, volinfo);
    if (ret)
        goto out;

    /* If 'force' flag is given, no check is required */
    if (flags & GF_CLI_FLAG_OP_FORCE)
        goto out;

    if (_gf_false == glusterd_is_volume_started(volinfo)) {
        snprintf(msg, sizeof(msg),
                 "Volume %s "
                 "is not in the started state",
                 volname);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_STARTED, "%s", msg);
        ret = -1;
        goto out;
    }

    /* If geo-rep is configured, for this volume, it should be stopped. */
    param.volinfo = volinfo;
    ret = glusterd_check_geo_rep_running(&param, op_errstr);
    if (ret || param.is_active) {
        ret = -1;
        goto out;
    }

    ret = glusterd_check_ganesha_export(volinfo);
    if (ret) {
        ret = ganesha_manage_export(dict, "off", _gf_false, op_errstr);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_WARNING, 0,
                   GD_MSG_NFS_GNS_UNEXPRT_VOL_FAIL,
                   "Could not "
                   "unexport volume via NFS-Ganesha");
            ret = 0;
        }
    }

    if (glusterd_is_defrag_on(volinfo)) {
        snprintf(msg, sizeof(msg),
                 "rebalance session is "
                 "in progress for the volume '%s'",
                 volname);
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_OIP, "%s", msg);
        ret = -1;
        goto out;
    }

out:
    if (msg[0] != 0)
        *op_errstr = gf_strdup(msg);
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

int
glusterd_op_stage_delete_volume(dict_t *dict, char **op_errstr)
{
    int ret = 0;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    char msg[2048] = {0};
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), FMTSTR_CHECK_VOL_EXISTS, volname);
        goto out;
    }

    ret = glusterd_validate_volume_id(dict, volinfo);
    if (ret)
        goto out;

    if (glusterd_is_volume_started(volinfo)) {
        snprintf(msg, sizeof(msg),
                 "Volume %s has been started."
                 "Volume needs to be stopped before deletion.",
                 volname);
        ret = -1;
        goto out;
    }

    if (volinfo->snap_count > 0 || !cds_list_empty(&volinfo->snap_volumes)) {
        snprintf(msg, sizeof(msg),
                 "Cannot delete Volume %s ,"
                 "as it has %" PRIu64
                 " snapshots. "
                 "To delete the volume, "
                 "first delete all the snapshots under it.",
                 volname, volinfo->snap_count);
        ret = -1;
        goto out;
    }

    if (!glusterd_are_all_peers_up()) {
        ret = -1;
        snprintf(msg, sizeof(msg), "Some of the peers are down");
        goto out;
    }
    volinfo->stage_deleted = _gf_true;
    gf_log(this->name, GF_LOG_INFO,
           "Setting stage deleted flag to true for "
           "volume %s",
           volinfo->volname);
    ret = 0;

out:
    if (msg[0] != '\0') {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_OP_STAGE_DELETE_VOL_FAIL,
               "%s", msg);
        *op_errstr = gf_strdup(msg);
    }
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
glusterd_handle_heal_cmd(xlator_t *this, glusterd_volinfo_t *volinfo,
                         dict_t *dict, char **op_errstr)
{
    glusterd_svc_t *svc = NULL;
    gf_xl_afr_op_t heal_op = GF_SHD_OP_INVALID;
    int ret = 0;
    char msg[2408] = {
        0,
    };
    char *offline_msg =
        "Self-heal daemon is not running. "
        "Check self-heal daemon log file.";

    ret = dict_get_int32n(dict, "heal-op", SLEN("heal-op"),
                          (int32_t *)&heal_op);
    if (ret) {
        ret = -1;
        *op_errstr = gf_strdup("Heal operation not specified");
        goto out;
    }

    svc = &(volinfo->shd.svc);
    switch (heal_op) {
        case GF_SHD_OP_INVALID:
        case GF_SHD_OP_HEAL_ENABLE: /* This op should be handled in volume-set*/
        case GF_SHD_OP_HEAL_DISABLE: /* This op should be handled in
                                        volume-set*/
        case GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE:  /* This op should be handled
                                                       in volume-set */
        case GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE: /* This op should be handled
                                                       in volume-set */
        case GF_SHD_OP_HEAL_SUMMARY:                /*glfsheal cmd*/
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE:  /*glfsheal cmd*/
        case GF_SHD_OP_SBRAIN_HEAL_FROM_LATEST_MTIME: /*glfsheal cmd*/
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK:        /*glfsheal cmd*/
            ret = -1;
            *op_errstr = gf_strdup("Invalid heal-op");
            goto out;

        case GF_SHD_OP_HEAL_INDEX:
        case GF_SHD_OP_HEAL_FULL:
            if (!glusterd_is_shd_compatible_volume(volinfo)) {
                ret = -1;
                snprintf(msg, sizeof(msg),
                         "Volume %s is not of type "
                         "replicate or disperse",
                         volinfo->volname);
                *op_errstr = gf_strdup(msg);
                goto out;
            }

            if (!svc->online) {
                ret = -1;
                *op_errstr = gf_strdup(offline_msg);
                goto out;
            }
            break;
        case GF_SHD_OP_INDEX_SUMMARY:
        case GF_SHD_OP_SPLIT_BRAIN_FILES:
        case GF_SHD_OP_STATISTICS:
        case GF_SHD_OP_STATISTICS_HEAL_COUNT:
        case GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
            if (!glusterd_is_volume_replicate(volinfo)) {
                ret = -1;
                snprintf(msg, sizeof(msg),
                         "This command is supported "
                         "for only volume of replicated "
                         "type. Volume %s is not of type "
                         "replicate",
                         volinfo->volname);
                *op_errstr = gf_strdup(msg);
                goto out;
            }

            if (!svc->online) {
                ret = -1;
                *op_errstr = gf_strdup(offline_msg);
                goto out;
            }
            break;
        case GF_SHD_OP_HEALED_FILES:
        case GF_SHD_OP_HEAL_FAILED_FILES:
            ret = -1;
            snprintf(msg, sizeof(msg),
                     "Command not supported. "
                     "Please use \"gluster volume heal %s info\" "
                     "and logs to find the heal information.",
                     volinfo->volname);
            *op_errstr = gf_strdup(msg);
            goto out;
    }
out:
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_HANDLE_HEAL_CMD_FAIL, "%s",
               *op_errstr);
    return ret;
}

int
glusterd_op_stage_heal_volume(dict_t *dict, char **op_errstr)
{
    int ret = 0;
    char *volname = NULL;
    gf_boolean_t enabled = _gf_false;
    glusterd_volinfo_t *volinfo = NULL;
    char msg[2048];
    glusterd_conf_t *priv = NULL;
    dict_t *opt_dict = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    priv = this->private;
    if (!priv) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PRIV_NULL, "priv is NULL");
        goto out;
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        ret = -1;
        snprintf(msg, sizeof(msg), "Volume %s does not exist", volname);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND, "%s", msg);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    ret = glusterd_validate_volume_id(dict, volinfo);
    if (ret)
        goto out;

    if (!glusterd_is_volume_started(volinfo)) {
        ret = -1;
        snprintf(msg, sizeof(msg), "Volume %s is not started.", volname);
        gf_smsg(this->name, GF_LOG_WARNING, 0, GD_MSG_VOL_NOT_STARTED,
                "Volume=%s", volname, NULL);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    opt_dict = volinfo->dict;
    if (!opt_dict) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_GET_FAILED, NULL);
        ret = 0;
        goto out;
    }
    enabled = gd_is_self_heal_enabled(volinfo, opt_dict);
    if (!enabled) {
        ret = -1;
        snprintf(msg, sizeof(msg),
                 "Self-heal-daemon is "
                 "disabled. Heal will not be triggered on volume %s",
                 volname);
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SELF_HEALD_DISABLED, "%s",
               msg);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    ret = glusterd_handle_heal_cmd(this, volinfo, dict, op_errstr);
    if (ret)
        goto out;

    ret = 0;
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}

int
glusterd_op_stage_statedump_volume(dict_t *dict, char **op_errstr)
{
    int ret = -1;
    char *volname = NULL;
    char *options = NULL;
    int option_cnt = 0;
    gf_boolean_t is_running = _gf_false;
    glusterd_volinfo_t *volinfo = NULL;
    char msg[2408] = {
        0,
    };
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    ret = glusterd_op_statedump_volume_args_get(dict, &volname, &options,
                                                &option_cnt);
    if (ret)
        goto out;

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), FMTSTR_CHECK_VOL_EXISTS, volname);
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_VOLINFO_GET_FAIL,
                "Volume=%s", volname, NULL);
        goto out;
    }

    ret = glusterd_validate_volume_id(dict, volinfo);
    if (ret)
        goto out;

    is_running = glusterd_is_volume_started(volinfo);
    if (!is_running) {
        snprintf(msg, sizeof(msg),
                 "Volume %s is not in the started"
                 " state",
                 volname);
        ret = -1;
        goto out;
    }

    if (priv->op_version == GD_OP_VERSION_MIN && strstr(options, "quotad")) {
        snprintf(msg, sizeof(msg),
                 "The cluster is operating "
                 "at op-version 1. Taking quotad's statedump is "
                 "disallowed in this state");
        ret = -1;
        goto out;
    }
    if ((strstr(options, "quotad")) &&
        (!glusterd_is_volume_quota_enabled(volinfo))) {
        snprintf(msg, sizeof(msg),
                 "Quota is not enabled on "
                 "volume %s",
                 volname);
        ret = -1;
        goto out;
    }
out:
    if (ret && msg[0] != '\0')
        *op_errstr = gf_strdup(msg);
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_op_stage_clearlocks_volume(dict_t *dict, char **op_errstr)
{
    int ret = -1;
    char *volname = NULL;
    char *path = NULL;
    char *type = NULL;
    char *kind = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    char msg[2048] = {
        0,
    };

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        snprintf(msg, sizeof(msg), "Failed to get volume name");
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s", msg);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    ret = dict_get_strn(dict, "path", SLEN("path"), &path);
    if (ret) {
        snprintf(msg, sizeof(msg), "Failed to get path");
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s", msg);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    ret = dict_get_strn(dict, "kind", SLEN("kind"), &kind);
    if (ret) {
        snprintf(msg, sizeof(msg), "Failed to get kind");
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s", msg);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    ret = dict_get_strn(dict, "type", SLEN("type"), &type);
    if (ret) {
        snprintf(msg, sizeof(msg), "Failed to get type");
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s", msg);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), "Volume %s does not exist", volname);
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND, "%s", msg);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    ret = glusterd_validate_volume_id(dict, volinfo);
    if (ret)
        goto out;

    if (!glusterd_is_volume_started(volinfo)) {
        snprintf(msg, sizeof(msg), "Volume %s is not started", volname);
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_STARTED, "%s", msg);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    ret = 0;
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

int
glusterd_op_create_volume(dict_t *dict, char **op_errstr)
{
    int ret = 0;
    char *volname = NULL;
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    gf_boolean_t vol_added = _gf_false;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_brickinfo_t *ta_brickinfo = NULL;
    xlator_t *this = NULL;
    char *brick = NULL;
    char *ta_brick = NULL;
    int32_t count = 0;
    int32_t i = 1;
    char *bricks = NULL;
    char *ta_bricks = NULL;
    char *brick_list = NULL;
    char *ta_brick_list = NULL;
    char *free_ptr = NULL;
    char *ta_free_ptr = NULL;
    char *saveptr = NULL;
    char *ta_saveptr = NULL;
    char *trans_type = NULL;
    char *str = NULL;
    char *username = NULL;
    char *password = NULL;
    int brickid = 0;
    char msg[1024] __attribute__((unused)) = {
        0,
    };
    char *brick_mount_dir = NULL;
    char key[64] = "";
    char *address_family_str = NULL;
    struct statvfs brickstat = {
        0,
    };

    this = THIS;
    GF_ASSERT(this);

    priv = this->private;
    GF_ASSERT(priv);

    ret = glusterd_volinfo_new(&volinfo);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Unable to allocate memory for volinfo");
        goto out;
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    if (snprintf(volinfo->volname, sizeof(volinfo->volname), "%s", volname) >=
        sizeof(volinfo->volname)) {
        ret = -1;
        goto out;
    }

    GF_ASSERT(volinfo->volname);

    ret = dict_get_int32n(dict, "type", SLEN("type"), &volinfo->type);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get type of volume"
               " %s",
               volname);
        goto out;
    }

    ret = dict_get_int32n(dict, "count", SLEN("count"), &volinfo->brick_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get brick count of"
               " volume %s",
               volname);
        goto out;
    }

    ret = dict_get_int32n(dict, "port", SLEN("port"), &volinfo->port);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get port");
        goto out;
    }

    ret = dict_get_strn(dict, "bricks", SLEN("bricks"), &bricks);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get bricks for "
               "volume %s",
               volname);
        goto out;
    }

    /* replica-count 1 means, no replication, file is in one brick only */
    volinfo->replica_count = 1;
    /* stripe-count 1 means, no striping, file is present as a whole */
    volinfo->stripe_count = 1;

    if (GF_CLUSTER_TYPE_REPLICATE == volinfo->type) {
        /* performance.client-io-threads is turned on to default,
         * however this has adverse effects on replicate volumes due to
         * replication design issues, till that get addressed
         * performance.client-io-threads option is turned off for all
         * replicate volumes
         */
        if (priv->op_version >= GD_OP_VERSION_3_12_2) {
            ret = dict_set_nstrn(volinfo->dict, "performance.client-io-threads",
                                 SLEN("performance.client-io-threads"), "off",
                                 SLEN("off"));
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set "
                       "performance.client-io-threads to off");
                goto out;
            }
        }
        ret = dict_get_int32n(dict, "replica-count", SLEN("replica-count"),
                              &volinfo->replica_count);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get "
                   "replica count for volume %s",
                   volname);
            goto out;
        }

        /* coverity[unused_value] arbiter count is optional */
        ret = dict_get_int32n(dict, "arbiter-count", SLEN("arbiter-count"),
                              &volinfo->arbiter_count);
        ret = dict_get_int32n(dict, "thin-arbiter-count",
                              SLEN("thin-arbiter-count"),
                              &volinfo->thin_arbiter_count);
        if (volinfo->thin_arbiter_count) {
            ret = dict_get_strn(dict, "ta-brick", SLEN("ta-brick"), &ta_bricks);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Unable to get thin arbiter brick for "
                       "volume %s",
                       volname);
                goto out;
            }
        }

    } else if (GF_CLUSTER_TYPE_DISPERSE == volinfo->type) {
        ret = dict_get_int32n(dict, "disperse-count", SLEN("disperse-count"),
                              &volinfo->disperse_count);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get "
                   "disperse count for volume %s",
                   volname);
            goto out;
        }
        ret = dict_get_int32n(dict, "redundancy-count",
                              SLEN("redundancy-count"),
                              &volinfo->redundancy_count);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get "
                   "redundancy count for volume %s",
                   volname);
            goto out;
        }
        if (priv->op_version < GD_OP_VERSION_3_6_0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UNSUPPORTED_VERSION,
                   "Disperse volume "
                   "needs op-version 3.6.0 or higher");
            ret = -1;
            goto out;
        }
    }

    /* dist-leaf-count is the count of brick nodes for a given
       subvolume of distribute */
    volinfo->dist_leaf_count = glusterd_get_dist_leaf_count(volinfo);

    /* subvol_count is the count of number of subvolumes present
       for a given distribute volume */
    volinfo->subvol_count = (volinfo->brick_count / volinfo->dist_leaf_count);

    /* Keep sub-count same as earlier, for the sake of backward
       compatibility */
    if (volinfo->dist_leaf_count > 1)
        volinfo->sub_count = volinfo->dist_leaf_count;

    ret = dict_get_strn(dict, "transport", SLEN("transport"), &trans_type);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get transport type of volume %s", volname);
        goto out;
    }

    ret = dict_get_strn(dict, "volume-id", SLEN("volume-id"), &str);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume-id of volume %s", volname);
        goto out;
    }
    ret = gf_uuid_parse(str, volinfo->volume_id);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UUID_PARSE_FAIL,
               "unable to parse uuid %s of volume %s", str, volname);
        goto out;
    }

    ret = dict_get_strn(dict, "internal-username", SLEN("internal-username"),
                        &username);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "unable to get internal username of volume %s", volname);
        goto out;
    }
    glusterd_auth_set_username(volinfo, username);

    ret = dict_get_strn(dict, "internal-password", SLEN("internal-password"),
                        &password);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "unable to get internal password of volume %s", volname);
        goto out;
    }
    glusterd_auth_set_password(volinfo, password);

    if (strcasecmp(trans_type, "rdma") == 0) {
        volinfo->transport_type = GF_TRANSPORT_RDMA;
    } else if (strcasecmp(trans_type, "tcp") == 0) {
        volinfo->transport_type = GF_TRANSPORT_TCP;
    } else {
        volinfo->transport_type = GF_TRANSPORT_BOTH_TCP_RDMA;
    }

    if (ta_bricks) {
        ta_brick_list = gf_strdup(ta_bricks);
        ta_free_ptr = ta_brick_list;
    }

    if (volinfo->thin_arbiter_count) {
        ta_brick = strtok_r(ta_brick_list + 1, " \n", &ta_saveptr);

        count = 1;
        brickid = volinfo->replica_count;
        /* assign brickid to ta_bricks
         * Following loop runs for number of subvols times. Although
         * there is only one ta-brick for a volume but the volume fuse volfile
         * requires an entry of ta-brick for each subvolume. Also, the ta-brick
         * id needs to be adjusted according to the subvol count.
         * For eg- For first subvolume ta-brick id is volname-ta-2, for second
         * subvol ta-brick id is volname-ta-5.
         */
        while (count <= volinfo->subvol_count) {
            ret = glusterd_brickinfo_new_from_brick(ta_brick, &ta_brickinfo,
                                                    _gf_false, op_errstr);
            if (ret)
                goto out;

            GLUSTERD_ASSIGN_BRICKID_TO_TA_BRICKINFO(ta_brickinfo, volinfo,
                                                    brickid);
            cds_list_add_tail(&ta_brickinfo->brick_list, &volinfo->ta_bricks);
            count++;
            brickid += volinfo->replica_count + 1;
        }
    }

    if (bricks) {
        brick_list = gf_strdup(bricks);
        free_ptr = brick_list;
    }

    count = volinfo->brick_count;

    if (count)
        brick = strtok_r(brick_list + 1, " \n", &saveptr);

    brickid = glusterd_get_next_available_brickid(volinfo);
    if (brickid < 0)
        goto out;
    while (i <= count) {
        ret = glusterd_brickinfo_new_from_brick(brick, &brickinfo, _gf_true,
                                                op_errstr);
        if (ret)
            goto out;
        if (volinfo->thin_arbiter_count == 1 &&
            (brickid + 1) % (volinfo->replica_count + 1) == 0) {
            brickid = brickid + 1;
        }
        GLUSTERD_ASSIGN_BRICKID_TO_BRICKINFO(brickinfo, volinfo, brickid++);

        ret = glusterd_resolve_brick(brickinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RESOLVE_BRICK_FAIL,
                   FMTSTR_RESOLVE_BRICK, brickinfo->hostname, brickinfo->path);
            goto out;
        }

        /* A bricks mount dir is required only by snapshots which were
         * introduced in gluster-3.6.0
         */
        if (priv->op_version >= GD_OP_VERSION_3_6_0) {
            brick_mount_dir = NULL;
            ret = snprintf(key, sizeof(key), "brick%d.mount_dir", i);
            ret = dict_get_strn(dict, key, ret, &brick_mount_dir);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "%s not present", key);
                goto out;
            }
            snprintf(brickinfo->mount_dir, sizeof(brickinfo->mount_dir), "%s",
                     brick_mount_dir);
        }

        if (!gf_uuid_compare(brickinfo->uuid, MY_UUID)) {
            ret = sys_statvfs(brickinfo->path, &brickstat);
            if (ret) {
                gf_log("brick-op", GF_LOG_ERROR,
                       "Failed to fetch disk"
                       " utilization from the brick (%s:%s). Please "
                       "check health of the brick. Error code was %s",
                       brickinfo->hostname, brickinfo->path, strerror(errno));
                goto out;
            }
            brickinfo->statfs_fsid = brickstat.f_fsid;
        }

        cds_list_add_tail(&brickinfo->brick_list, &volinfo->bricks);
        brick = strtok_r(NULL, " \n", &saveptr);
        i++;
    }

    ret = glusterd_enable_default_options(volinfo, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FAIL_DEFAULT_OPT_SET,
               "Failed to set default "
               "options on create for volume %s",
               volinfo->volname);
        goto out;
    }

    ret = dict_get_strn(dict, "transport.address-family",
                        SLEN("transport.address-family"), &address_family_str);

    if (!ret) {
        ret = dict_set_dynstr_with_alloc(
            volinfo->dict, "transport.address-family", address_family_str);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR,
                   "Failed to set transport.address-family for %s",
                   volinfo->volname);
            goto out;
        }
    }

    gd_update_volume_op_versions(volinfo);

    ret = glusterd_store_volinfo(volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
    if (ret) {
        glusterd_store_delete_volume(volinfo);
        *op_errstr = gf_strdup(
            "Failed to store the "
            "Volume information");
        goto out;
    }

    ret = glusterd_create_volfiles_and_notify_services(volinfo);
    if (ret) {
        *op_errstr = gf_strdup("Failed to create volume files");
        goto out;
    }

    volinfo->rebal.defrag_status = 0;
    glusterd_list_add_order(&volinfo->vol_list, &priv->volumes,
                            glusterd_compare_volume_name);
    vol_added = _gf_true;

out:
    GF_FREE(free_ptr);
    GF_FREE(ta_free_ptr);
    if (!vol_added && volinfo)
        glusterd_volinfo_unref(volinfo);
    return ret;
}

int
glusterd_start_volume(glusterd_volinfo_t *volinfo, int flags, gf_boolean_t wait)

{
    int ret = 0;
    glusterd_brickinfo_t *brickinfo = NULL;
    xlator_t *this = NULL;
    glusterd_volinfo_ver_ac_t verincrement = 0;

    this = THIS;
    GF_ASSERT(this);
    GF_ASSERT(volinfo);

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        /* Mark start_triggered to false so that in case if this brick
         * was brought down through gf_attach utility, the
         * brickinfo->start_triggered wouldn't have been updated to
         * _gf_false
         */
        if (flags & GF_CLI_FLAG_OP_FORCE) {
            brickinfo->start_triggered = _gf_false;
        }
        ret = glusterd_brick_start(volinfo, brickinfo, wait, _gf_false);
        /* If 'force' try to start all bricks regardless of success or
         * failure
         */
        if (!(flags & GF_CLI_FLAG_OP_FORCE) && ret)
            goto out;
    }

    /* Increment the volinfo version only if there is a
     * change in status. Force option can be used to start
     * dead bricks even if the volume is in started state.
     * In such case volume status will be GLUSTERD_STATUS_STARTED.
     * Therefore we should not increment the volinfo version.*/
    if (GLUSTERD_STATUS_STARTED != volinfo->status) {
        verincrement = GLUSTERD_VOLINFO_VER_AC_INCREMENT;
    } else {
        verincrement = GLUSTERD_VOLINFO_VER_AC_NONE;
    }

    glusterd_set_volume_status(volinfo, GLUSTERD_STATUS_STARTED);
    /* Update volinfo on disk in critical section because
       attach_brick_callback can also call store_volinfo for same
       volume to update volinfo on disk
    */
    /* coverity[ORDER_REVERSAL] */
    LOCK(&volinfo->lock);
    ret = glusterd_store_volinfo(volinfo, verincrement);
    UNLOCK(&volinfo->lock);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_SET_FAIL,
               "Failed to store volinfo of "
               "%s volume",
               volinfo->volname);
        goto out;
    }
out:
    gf_msg_trace(this->name, 0, "returning %d ", ret);
    return ret;
}

int
glusterd_op_start_volume(dict_t *dict, char **op_errstr)
{
    int ret = 0;
    int32_t brick_count = 0;
    char *brick_mount_dir = NULL;
    char key[64] = "";
    char *volname = NULL;
    int flags = 0;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    xlator_t *this = NULL;
    glusterd_conf_t *conf = NULL;
    glusterd_svc_t *svc = NULL;
    char *str = NULL;
    gf_boolean_t option = _gf_false;

    this = THIS;
    GF_ASSERT(this);
    conf = this->private;
    GF_ASSERT(conf);

    ret = glusterd_op_start_volume_args_get(dict, &volname, &flags);
    if (ret)
        goto out;

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               FMTSTR_CHECK_VOL_EXISTS, volname);
        goto out;
    }

    /* This is an incremental approach to have all the volinfo objects ref
     * count. The first attempt is made in volume start transaction to
     * ensure it doesn't race with import volume where stale volume is
     * deleted. There are multiple instances of GlusterD crashing in
     * bug-948686.t because of this. Once this approach is full proof, all
     * other volinfo objects will be refcounted.
     */
    glusterd_volinfo_ref(volinfo);

    /* A bricks mount dir is required only by snapshots which were
     * introduced in gluster-3.6.0
     */
    if (conf->op_version >= GD_OP_VERSION_3_6_0) {
        cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
        {
            brick_count++;
            /* Don't check bricks that are not owned by you
             */
            if (gf_uuid_compare(brickinfo->uuid, MY_UUID))
                continue;
            if (strlen(brickinfo->mount_dir) < 1) {
                brick_mount_dir = NULL;
                ret = snprintf(key, sizeof(key), "brick%d.mount_dir",
                               brick_count);
                ret = dict_get_strn(dict, key, ret, &brick_mount_dir);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                           "%s not present", key);
                    goto out;
                }
                if (snprintf(brickinfo->mount_dir, sizeof(brickinfo->mount_dir),
                             "%s",
                             brick_mount_dir) >= sizeof(brickinfo->mount_dir)) {
                    ret = -1;
                    goto out;
                }
            }
        }
    }

    ret = dict_get_str(conf->opts, GLUSTERD_STORE_KEY_GANESHA_GLOBAL, &str);
    if (ret != 0) {
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
               "Global dict not present.");
        ret = 0;

    } else {
        ret = gf_string2boolean(str, &option);
        /* Check if the feature is enabled and set nfs-disable to true */
        if (option) {
            gf_msg_debug(this->name, 0, "NFS-Ganesha is enabled");
            /* Gluster-nfs should not start when NFS-Ganesha is enabled*/
            ret = dict_set_str(volinfo->dict, NFS_DISABLE_MAP_KEY, "on");
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set nfs.disable for"
                       "volume %s",
                       volname);
                goto out;
            }
        }
    }

    ret = glusterd_start_volume(volinfo, flags, _gf_true);
    if (ret)
        goto out;

    if (!volinfo->is_snap_volume) {
        svc = &(volinfo->snapd.svc);
        ret = svc->manager(svc, volinfo, PROC_START_NO_WAIT);
        if (ret)
            goto out;
    }

    svc = &(volinfo->gfproxyd.svc);
    ret = svc->manager(svc, volinfo, PROC_START_NO_WAIT);
    ret = glusterd_svcs_manager(volinfo);

out:
    if (volinfo)
        glusterd_volinfo_unref(volinfo);

    gf_msg_trace(this->name, 0, "returning %d ", ret);
    return ret;
}

int
glusterd_stop_volume(glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    glusterd_brickinfo_t *brickinfo = NULL;
    xlator_t *this = NULL;
    glusterd_svc_t *svc = NULL;

    this = THIS;
    GF_ASSERT(this);

    GF_VALIDATE_OR_GOTO(this->name, volinfo, out);

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        ret = glusterd_brick_stop(volinfo, brickinfo, _gf_false);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_STOP_FAIL,
                   "Failed to stop "
                   "brick (%s)",
                   brickinfo->path);
            goto out;
        }
    }

    glusterd_set_volume_status(volinfo, GLUSTERD_STATUS_STOPPED);

    ret = glusterd_store_volinfo(volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_SET_FAIL,
               "Failed to store volinfo of "
               "%s volume",
               volinfo->volname);
        goto out;
    }

    if (!volinfo->is_snap_volume) {
        svc = &(volinfo->snapd.svc);
        ret = svc->manager(svc, volinfo, PROC_START_NO_WAIT);
        if (ret)
            goto out;
    }

    ret = glusterd_svcs_manager(volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_GRAPH_CHANGE_NOTIFY_FAIL,
               "Failed to notify graph "
               "change for %s volume",
               volinfo->volname);

        goto out;
    }

out:
    return ret;
}

int
glusterd_op_stop_volume(dict_t *dict)
{
    int ret = 0;
    int flags = 0;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    ret = glusterd_op_stop_volume_args_get(dict, &volname, &flags);
    if (ret)
        goto out;

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               FMTSTR_CHECK_VOL_EXISTS, volname);
        goto out;
    }

    ret = glusterd_stop_volume(volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_STOP_FAILED,
               "Failed to stop %s volume", volname);
        goto out;
    }
out:
    gf_msg_trace(this->name, 0, "returning %d ", ret);
    return ret;
}

int
glusterd_op_delete_volume(dict_t *dict)
{
    int ret = 0;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               FMTSTR_CHECK_VOL_EXISTS, volname);
        goto out;
    }

    if (glusterd_check_ganesha_export(volinfo) && is_origin_glusterd(dict)) {
        ret = manage_export_config(volname, "off", NULL);
        if (ret)
            gf_msg(this->name, GF_LOG_WARNING, 0, 0,
                   "Could not delete ganesha export conf file "
                   "for %s",
                   volname);
    }

    ret = glusterd_delete_volume(volinfo);
out:
    gf_msg_debug(this->name, 0, "returning %d", ret);
    return ret;
}

int
glusterd_op_heal_volume(dict_t *dict, char **op_errstr)
{
    int ret = 0;
    /* Necessary subtasks of heal are completed in brick op */

    return ret;
}

int
glusterd_op_statedump_volume(dict_t *dict, char **op_errstr)
{
    int ret = 0;
    char *volname = NULL;
    char *options = NULL;
    int option_cnt = 0;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;

    ret = glusterd_op_statedump_volume_args_get(dict, &volname, &options,
                                                &option_cnt);
    if (ret)
        goto out;

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret)
        goto out;
    gf_msg_debug("glusterd", 0, "Performing statedump on volume %s", volname);
    if (strstr(options, "quotad")) {
        ret = glusterd_quotad_statedump(options, option_cnt, op_errstr);
        if (ret)
            goto out;
#ifdef BUILD_GNFS
    } else if (strstr(options, "nfs") != NULL) {
        ret = glusterd_nfs_statedump(options, option_cnt, op_errstr);
        if (ret)
            goto out;
#endif
    } else if (strstr(options, "client")) {
        ret = glusterd_client_statedump(volname, options, option_cnt,
                                        op_errstr);
        if (ret)
            goto out;

    } else {
        cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
        {
            ret = glusterd_brick_statedump(volinfo, brickinfo, options,
                                           option_cnt, op_errstr);
            /* Let us take the statedump of other bricks instead of
             * exiting, if statedump of this brick fails.
             */
            if (ret)
                gf_msg(THIS->name, GF_LOG_WARNING, 0, GD_MSG_BRK_STATEDUMP_FAIL,
                       "could not "
                       "take the statedump of the brick %s:%s."
                       " Proceeding to other bricks",
                       brickinfo->hostname, brickinfo->path);
        }
    }

out:
    return ret;
}

int
glusterd_clearlocks_send_cmd(glusterd_volinfo_t *volinfo, char *cmd, char *path,
                             char *result, char *errstr, int err_len,
                             char *mntpt)
{
    int ret = -1;
    char abspath[PATH_MAX] = {
        0,
    };

    snprintf(abspath, sizeof(abspath), "%s/%s", mntpt, path);
    ret = sys_lgetxattr(abspath, cmd, result, PATH_MAX);
    if (ret < 0) {
        snprintf(errstr, err_len,
                 "clear-locks getxattr command "
                 "failed. Reason: %s",
                 strerror(errno));
        gf_msg_debug(THIS->name, 0, "%s", errstr);
        goto out;
    }

    ret = 0;
out:
    return ret;
}

int
glusterd_clearlocks_rmdir_mount(glusterd_volinfo_t *volinfo, char *mntpt)
{
    int ret = -1;

    ret = sys_rmdir(mntpt);
    if (ret) {
        gf_msg_debug(THIS->name, 0, "rmdir failed");
        goto out;
    }

    ret = 0;
out:
    return ret;
}

void
glusterd_clearlocks_unmount(glusterd_volinfo_t *volinfo, char *mntpt)
{
    glusterd_conf_t *priv = NULL;
    runner_t runner = {
        0,
    };
    int ret = 0;

    priv = THIS->private;

    /*umount failures are ignored. Using stat we could have avoided
     * attempting to unmount a non-existent filesystem. But a failure of
     * stat() on mount can be due to network failures.*/

    runinit(&runner);
    runner_add_args(&runner, _PATH_UMOUNT, "-f", NULL);
    runner_argprintf(&runner, "%s", mntpt);

    synclock_unlock(&priv->big_lock);
    ret = runner_run(&runner);
    synclock_lock(&priv->big_lock);
    if (ret) {
        ret = 0;
        gf_msg_debug("glusterd", 0, "umount failed on maintenance client");
    }

    return;
}

int
glusterd_clearlocks_create_mount(glusterd_volinfo_t *volinfo, char **mntpt)
{
    int ret = -1;
    char template[PATH_MAX] = {
        0,
    };
    char *tmpl = NULL;

    snprintf(template, sizeof(template), "/tmp/%s.XXXXXX", volinfo->volname);
    tmpl = mkdtemp(template);
    if (!tmpl) {
        gf_msg_debug(THIS->name, 0,
                     "Couldn't create temporary "
                     "mount directory. Reason %s",
                     strerror(errno));
        goto out;
    }

    *mntpt = gf_strdup(tmpl);
    ret = 0;
out:
    return ret;
}

int
glusterd_clearlocks_mount(glusterd_volinfo_t *volinfo, char **xl_opts,
                          char *mntpt)
{
    int ret = -1;
    int i = 0;
    glusterd_conf_t *priv = NULL;
    runner_t runner = {
        0,
    };
    char client_volfpath[PATH_MAX] = {
        0,
    };
    char self_heal_opts[3][1024] = {"*replicate*.data-self-heal=off",
                                    "*replicate*.metadata-self-heal=off",
                                    "*replicate*.entry-self-heal=off"};

    priv = THIS->private;

    runinit(&runner);
    glusterd_get_trusted_client_filepath(client_volfpath, volinfo,
                                         volinfo->transport_type);
    runner_add_args(&runner, SBIN_DIR "/glusterfs", "-f", NULL);
    runner_argprintf(&runner, "%s", client_volfpath);
    runner_add_arg(&runner, "-l");
    runner_argprintf(&runner, "%s/%s-clearlocks-mnt.log", priv->logdir,
                     volinfo->volname);
    if (volinfo->memory_accounting)
        runner_add_arg(&runner, "--mem-accounting");

    for (i = 0; i < volinfo->brick_count && xl_opts[i]; i++) {
        runner_add_arg(&runner, "--xlator-option");
        runner_argprintf(&runner, "%s", xl_opts[i]);
    }

    for (i = 0; i < 3; i++) {
        runner_add_args(&runner, "--xlator-option", self_heal_opts[i], NULL);
    }

    runner_argprintf(&runner, "%s", mntpt);
    synclock_unlock(&priv->big_lock);
    ret = runner_run(&runner);
    synclock_lock(&priv->big_lock);
    if (ret) {
        gf_msg_debug(THIS->name, 0, "Could not start glusterfs");
        goto out;
    }
    gf_msg_debug(THIS->name, 0, "Started glusterfs successfully");

out:
    return ret;
}

int
glusterd_clearlocks_get_local_client_ports(glusterd_volinfo_t *volinfo,
                                           char **xl_opts)
{
    glusterd_brickinfo_t *brickinfo = NULL;
    char brickname[PATH_MAX] = {
        0,
    };
    int index = 0;
    int ret = -1;
    int i = 0;
    int port = 0;
    int32_t len = 0;

    GF_ASSERT(xl_opts);
    if (!xl_opts) {
        gf_msg_debug(THIS->name, 0,
                     "Should pass non-NULL "
                     "xl_opts");
        goto out;
    }

    index = -1;
    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        index++;
        if (gf_uuid_compare(brickinfo->uuid, MY_UUID))
            continue;

        if (volinfo->transport_type == GF_TRANSPORT_RDMA) {
            len = snprintf(brickname, sizeof(brickname), "%s.rdma",
                           brickinfo->path);
        } else
            len = snprintf(brickname, sizeof(brickname), "%s", brickinfo->path);
        if ((len < 0) || (len >= sizeof(brickname))) {
            ret = -1;
            goto out;
        }

        port = pmap_registry_search(THIS, brickname, GF_PMAP_PORT_BRICKSERVER,
                                    _gf_false);
        if (!port) {
            ret = -1;
            gf_msg_debug(THIS->name, 0,
                         "Couldn't get port "
                         " for brick %s:%s",
                         brickinfo->hostname, brickinfo->path);
            goto out;
        }

        ret = gf_asprintf(&xl_opts[i], "%s-client-%d.remote-port=%d",
                          volinfo->volname, index, port);
        if (ret == -1) {
            xl_opts[i] = NULL;
            goto out;
        }
        i++;
    }

    ret = 0;
out:
    return ret;
}

int
glusterd_op_clearlocks_volume(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    int32_t ret = -1;
    int i = 0;
    char *volname = NULL;
    char *path = NULL;
    char *kind = NULL;
    char *type = NULL;
    char *opts = NULL;
    char *cmd_str = NULL;
    char *free_ptr = NULL;
    char msg[PATH_MAX] = {
        0,
    };
    char result[PATH_MAX] = {
        0,
    };
    char *mntpt = NULL;
    char **xl_opts = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = THIS;
    GF_ASSERT(this);

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                "Key=volname", NULL);
        goto out;
    }
    gf_msg_debug("glusterd", 0, "Performing clearlocks on volume %s", volname);

    ret = dict_get_strn(dict, "path", SLEN("path"), &path);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "Key=path",
                NULL);
        goto out;
    }

    ret = dict_get_strn(dict, "kind", SLEN("kind"), &kind);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "Key=kind",
                NULL);
        goto out;
    }

    ret = dict_get_strn(dict, "type", SLEN("type"), &type);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "Key=type",
                NULL);
        goto out;
    }

    ret = dict_get_strn(dict, "opts", SLEN("opts"), &opts);
    if (ret)
        ret = 0;

    gf_smsg(this->name, GF_LOG_INFO, 0, GD_MSG_CLRCLK_VOL_REQ_RCVD,
            "Volume=%s, Kind=%s, Type=%s, Options=%s", volname, kind, type,
            opts, NULL);

    if (opts)
        ret = gf_asprintf(&cmd_str, GF_XATTR_CLRLK_CMD ".t%s.k%s.%s", type,
                          kind, opts);
    else
        ret = gf_asprintf(&cmd_str, GF_XATTR_CLRLK_CMD ".t%s.k%s", type, kind);
    if (ret == -1)
        goto out;

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), "Volume %s doesn't exist.", volname);
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND, "Volume=%s",
                volname, NULL);
        goto out;
    }

    xl_opts = GF_CALLOC(volinfo->brick_count + 1, sizeof(char *),
                        gf_gld_mt_charptr);
    if (!xl_opts) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY, NULL);
        goto out;
    }

    ret = glusterd_clearlocks_get_local_client_ports(volinfo, xl_opts);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Couldn't get port numbers of "
                 "local bricks");
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRK_PORT_NUM_GET_FAIL,
                NULL);
        goto out;
    }

    ret = glusterd_clearlocks_create_mount(volinfo, &mntpt);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Creating mount directory "
                 "for clear-locks failed.");
        gf_smsg(this->name, GF_LOG_ERROR, 0,
                GD_MSG_CLRLOCKS_MOUNTDIR_CREATE_FAIL, NULL);
        goto out;
    }

    ret = glusterd_clearlocks_mount(volinfo, xl_opts, mntpt);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Failed to mount clear-locks "
                 "maintenance client.");
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_CLRLOCKS_CLNT_MOUNT_FAIL,
                NULL);
        goto out;
    }

    ret = glusterd_clearlocks_send_cmd(volinfo, cmd_str, path, result, msg,
                                       sizeof(msg), mntpt);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_CLRCLK_SND_CMD_FAIL, NULL);
        goto umount;
    }

    free_ptr = gf_strdup(result);
    if (dict_set_dynstrn(rsp_dict, "lk-summary", SLEN("lk-summary"),
                         free_ptr)) {
        GF_FREE(free_ptr);
        snprintf(msg, sizeof(msg),
                 "Failed to set clear-locks "
                 "result");
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                "Key=lk-summary", NULL);
    }

umount:
    glusterd_clearlocks_unmount(volinfo, mntpt);

    if (glusterd_clearlocks_rmdir_mount(volinfo, mntpt))
        gf_smsg(this->name, GF_LOG_WARNING, 0, GD_MSG_CLRLOCKS_CLNT_UMOUNT_FAIL,
                NULL);

out:
    if (ret)
        *op_errstr = gf_strdup(msg);

    if (xl_opts) {
        for (i = 0; i < volinfo->brick_count && xl_opts[i]; i++)
            GF_FREE(xl_opts[i]);
        GF_FREE(xl_opts);
    }

    GF_FREE(cmd_str);

    GF_FREE(mntpt);

    return ret;
}
