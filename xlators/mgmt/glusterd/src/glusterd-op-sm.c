/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <time.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/mount.h>

#include <libgen.h>
#include <glusterfs/compat-uuid.h>

#include "fnmatch.h"
#include <glusterfs/xlator.h>
#include "protocol-common.h"
#include "glusterd.h"
#include <glusterfs/call-stub.h>
#include <glusterfs/list.h>
#include <glusterfs/dict.h>
#include <glusterfs/compat.h>
#include <glusterfs/compat-errno.h>
#include <glusterfs/statedump.h>
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include "glusterd-locks.h"
#include "glusterd-quota.h"
#include <glusterfs/syscall.h>
#include "cli1-xdr.h"
#include "glusterd-snapshot-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-svc-helper.h"
#include "glusterd-shd-svc-helper.h"
#include "glusterd-shd-svc.h"
#include "glusterd-quotad-svc.h"
#include "glusterd-server-quorum.h"
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include "glusterd-gfproxyd-svc-helper.h"

#define len_strcmp(key, len, str)                                              \
    ((len == SLEN(str)) && (strcmp(key, str) == 0))

extern char local_node_hostname[PATH_MAX];
static int
glusterd_set_shared_storage(dict_t *dict, char *key, char *value,
                            char **op_errstr);

/*
 * Valid options for all volumes to be listed in the valid_all_vol_opts table.
 * To add newer options to all volumes, we can just add more entries to this
 * table.
 *
 * It's important that every value have a default, or have a special handler
 * in glusterd_get_global_options_for_all_vols, or else we might crash there.
 */
const glusterd_all_vol_opts valid_all_vol_opts[] = {
    {GLUSTERD_QUORUM_RATIO_KEY, "51"},
    {GLUSTERD_SHARED_STORAGE_KEY, "disable"},
    /* This one actually gets filled in dynamically. */
    {GLUSTERD_GLOBAL_OP_VERSION_KEY, "BUG_NO_OP_VERSION"},
    /*
     * This one should be filled in dynamically, but it didn't used to be
     * (before the defaults were added here) so the value is unclear.
     *
     * TBD: add a dynamic handler to set the appropriate value
     */
    {GLUSTERD_MAX_OP_VERSION_KEY, "BUG_NO_MAX_OP_VERSION"},
    {GLUSTERD_BRICK_MULTIPLEX_KEY, "disable"},
    /* Set this value to 0 by default implying brick-multiplexing
     * behaviour with no limit set on the number of brick instances that
     * can be attached per process.
     * TBD: Discuss the default value for this. Maybe this should be a
     * dynamic value depending on the memory specifications per node */
    {GLUSTERD_BRICKMUX_LIMIT_KEY, GLUSTERD_BRICKMUX_LIMIT_DFLT_VALUE},
    {GLUSTERD_VOL_CNT_PER_THRD, GLUSTERD_VOL_CNT_PER_THRD_DEFAULT_VALUE},
    {GLUSTERD_LOCALTIME_LOGGING_KEY, "disable"},
    {GLUSTERD_DAEMON_LOG_LEVEL_KEY, "INFO"},
    {NULL},
};

static struct cds_list_head gd_op_sm_queue;
synclock_t gd_op_sm_lock;
glusterd_op_info_t opinfo = {
    {0},
};

int32_t
glusterd_txn_opinfo_dict_init()
{
    int32_t ret = -1;
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    priv->glusterd_txn_opinfo = dict_new();
    if (!priv->glusterd_txn_opinfo) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        ret = -1;
        goto out;
    }

    memset(priv->global_txn_id, '\0', sizeof(uuid_t));

    ret = 0;
out:
    return ret;
}

void
glusterd_txn_opinfo_dict_fini()
{
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    if (priv->glusterd_txn_opinfo)
        dict_unref(priv->glusterd_txn_opinfo);
}

void
glusterd_txn_opinfo_init(glusterd_op_info_t *opinfo,
                         glusterd_op_sm_state_info_t *state, int *op,
                         dict_t *op_ctx, rpcsvc_request_t *req)
{
    glusterd_conf_t *conf = NULL;

    GF_ASSERT(opinfo);

    conf = THIS->private;
    GF_ASSERT(conf);

    if (state)
        opinfo->state = *state;

    if (op)
        opinfo->op = *op;

    if (op_ctx)
        opinfo->op_ctx = dict_ref(op_ctx);
    else
        opinfo->op_ctx = NULL;

    if (req)
        opinfo->req = req;

    opinfo->txn_generation = conf->generation;
    cmm_smp_rmb();

    return;
}

int32_t
glusterd_generate_txn_id(dict_t *dict, uuid_t **txn_id)
{
    int32_t ret = -1;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);
    GF_ASSERT(dict);

    *txn_id = GF_MALLOC(sizeof(uuid_t), gf_common_mt_uuid_t);
    if (!*txn_id) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY, NULL);
        goto out;
    }

    if (priv->op_version < GD_OP_VERSION_3_6_0)
        gf_uuid_copy(**txn_id, priv->global_txn_id);
    else
        gf_uuid_generate(**txn_id);

    ret = dict_set_bin(dict, "transaction_id", *txn_id, sizeof(**txn_id));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set transaction id.");
        goto out;
    }

    gf_msg_debug(this->name, 0, "Transaction_id = %s", uuid_utoa(**txn_id));
out:
    if (ret && *txn_id) {
        GF_FREE(*txn_id);
        *txn_id = NULL;
    }

    return ret;
}

int32_t
glusterd_get_txn_opinfo(uuid_t *txn_id, glusterd_op_info_t *opinfo)
{
    int32_t ret = -1;
    glusterd_txn_opinfo_obj *opinfo_obj = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    if (!txn_id || !opinfo) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_ID_GET_FAIL,
                         "Empty transaction id or opinfo received.");
        ret = -1;
        goto out;
    }

    ret = dict_get_bin(priv->glusterd_txn_opinfo, uuid_utoa(*txn_id),
                       (void **)&opinfo_obj);
    if (ret)
        goto out;

    (*opinfo) = opinfo_obj->opinfo;

    gf_msg_debug(this->name, 0,
                 "Successfully got opinfo for transaction ID : %s",
                 uuid_utoa(*txn_id));

    ret = 0;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int32_t
glusterd_set_txn_opinfo(uuid_t *txn_id, glusterd_op_info_t *opinfo)
{
    int32_t ret = -1;
    glusterd_txn_opinfo_obj *opinfo_obj = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    if (!txn_id) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_ID_GET_FAIL,
                         "Empty transaction id received.");
        ret = -1;
        goto out;
    }

    ret = dict_get_bin(priv->glusterd_txn_opinfo, uuid_utoa(*txn_id),
                       (void **)&opinfo_obj);
    if (ret) {
        opinfo_obj = GF_CALLOC(1, sizeof(glusterd_txn_opinfo_obj),
                               gf_common_mt_txn_opinfo_obj_t);
        if (!opinfo_obj) {
            ret = -1;
            goto out;
        }

        ret = dict_set_bin(priv->glusterd_txn_opinfo, uuid_utoa(*txn_id),
                           opinfo_obj, sizeof(glusterd_txn_opinfo_obj));
        if (ret) {
            gf_msg_callingfn(this->name, GF_LOG_ERROR, errno,
                             GD_MSG_DICT_SET_FAILED,
                             "Unable to set opinfo for transaction"
                             " ID : %s",
                             uuid_utoa(*txn_id));
            goto out;
        }
    }

    opinfo_obj->opinfo = (*opinfo);

    gf_msg_debug(this->name, 0,
                 "Successfully set opinfo for transaction ID : %s",
                 uuid_utoa(*txn_id));
    ret = 0;
out:
    if (ret)
        if (opinfo_obj)
            GF_FREE(opinfo_obj);

    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int32_t
glusterd_clear_txn_opinfo(uuid_t *txn_id)
{
    int32_t ret = -1;
    glusterd_op_info_t txn_op_info = {
        {0},
    };
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    if (!txn_id) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_ID_GET_FAIL,
               "Empty transaction id received.");
        ret = -1;
        goto out;
    }

    ret = glusterd_get_txn_opinfo(txn_id, &txn_op_info);
    if (ret) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, 0,
                         GD_MSG_TRANS_OPINFO_GET_FAIL,
                         "Unable to get transaction opinfo "
                         "for transaction ID : %s",
                         uuid_utoa(*txn_id));
        goto out;
    }

    if (txn_op_info.op_ctx)
        dict_unref(txn_op_info.op_ctx);

    dict_del(priv->glusterd_txn_opinfo, uuid_utoa(*txn_id));

    gf_msg_debug(this->name, 0,
                 "Successfully cleared opinfo for transaction ID : %s",
                 uuid_utoa(*txn_id));

    ret = 0;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int glusterfs_port = GLUSTERD_DEFAULT_PORT;
static char *glusterd_op_sm_state_names[] = {
    "Default",
    "Lock sent",
    "Locked",
    "Stage op sent",
    "Staged",
    "Commit op sent",
    "Committed",
    "Unlock sent",
    "Stage op failed",
    "Commit op failed",
    "Brick op sent",
    "Brick op failed",
    "Brick op Committed",
    "Brick op Commit failed",
    "Ack drain",
    "Invalid",
};

static char *glusterd_op_sm_event_names[] = {
    "GD_OP_EVENT_NONE",       "GD_OP_EVENT_START_LOCK",
    "GD_OP_EVENT_LOCK",       "GD_OP_EVENT_RCVD_ACC",
    "GD_OP_EVENT_ALL_ACC",    "GD_OP_EVENT_STAGE_ACC",
    "GD_OP_EVENT_COMMIT_ACC", "GD_OP_EVENT_RCVD_RJT",
    "GD_OP_EVENT_STAGE_OP",   "GD_OP_EVENT_COMMIT_OP",
    "GD_OP_EVENT_UNLOCK",     "GD_OP_EVENT_START_UNLOCK",
    "GD_OP_EVENT_ALL_ACK",    "GD_OP_EVENT_LOCAL_UNLOCK_NO_RESP",
    "GD_OP_EVENT_INVALID"};

char *
glusterd_op_sm_state_name_get(int state)
{
    if (state < 0 || state >= GD_OP_STATE_MAX)
        return glusterd_op_sm_state_names[GD_OP_STATE_MAX];
    return glusterd_op_sm_state_names[state];
}

char *
glusterd_op_sm_event_name_get(int event)
{
    if (event < 0 || event >= GD_OP_EVENT_MAX)
        return glusterd_op_sm_event_names[GD_OP_EVENT_MAX];
    return glusterd_op_sm_event_names[event];
}

static void
glusterd_destroy_lock_ctx(glusterd_op_lock_ctx_t *ctx)
{
    if (!ctx)
        return;
    GF_FREE(ctx);
}

void
glusterd_set_volume_status(glusterd_volinfo_t *volinfo,
                           glusterd_volume_status status)
{
    GF_ASSERT(volinfo);
    volinfo->status = status;
}

static int
glusterd_op_sm_inject_all_acc(uuid_t *txn_id)
{
    int ret = -1;
    ret = glusterd_op_sm_inject_event(GD_OP_EVENT_ALL_ACC, txn_id, NULL);
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_check_bitrot_cmd(char *key, const int keylen, char *errstr,
                          const size_t size)
{
    int ret = -1;

    if (len_strcmp(key, keylen, "bitrot") ||
        len_strcmp(key, keylen, "features.bitrot")) {
        snprintf(errstr, size,
                 " 'gluster volume set <VOLNAME> %s' is invalid command."
                 " Use 'gluster volume bitrot <VOLNAME> {enable|disable}'"
                 " instead.",
                 key);
        goto out;
    } else if (len_strcmp(key, keylen, "scrub-freq") ||
               len_strcmp(key, keylen, "features.scrub-freq")) {
        snprintf(errstr, size,
                 " 'gluster volume set <VOLNAME> %s' is invalid command."
                 " Use 'gluster volume bitrot <VOLNAME> scrub-frequency"
                 " {hourly|daily|weekly|biweekly|monthly}' instead.",
                 key);
        goto out;
    } else if (len_strcmp(key, keylen, "scrub") ||
               len_strcmp(key, keylen, "features.scrub")) {
        snprintf(errstr, size,
                 " 'gluster volume set <VOLNAME> %s' is invalid command."
                 " Use 'gluster volume bitrot <VOLNAME> scrub {pause|resume}'"
                 " instead.",
                 key);
        goto out;
    } else if (len_strcmp(key, keylen, "scrub-throttle") ||
               len_strcmp(key, keylen, "features.scrub-throttle")) {
        snprintf(errstr, size,
                 " 'gluster volume set <VOLNAME> %s' is invalid command."
                 " Use 'gluster volume bitrot <VOLNAME> scrub-throttle "
                 " {lazy|normal|aggressive}' instead.",
                 key);
        goto out;
    }

    ret = 0;
out:
    return ret;
}

static int
glusterd_check_quota_cmd(char *key, const int keylen, char *value, char *errstr,
                         size_t size)
{
    int ret = -1;
    gf_boolean_t b = _gf_false;

    if (len_strcmp(key, keylen, "quota") ||
        len_strcmp(key, keylen, "features.quota")) {
        ret = gf_string2boolean(value, &b);
        if (ret)
            goto out;
        ret = -1;
        if (b) {
            snprintf(errstr, size,
                     " 'gluster volume set <VOLNAME> %s %s' is deprecated."
                     " Use 'gluster volume quota <VOLNAME> enable' instead.",
                     key, value);
        } else {
            snprintf(errstr, size,
                     " 'gluster volume set <VOLNAME> %s %s' is deprecated."
                     " Use 'gluster volume quota <VOLNAME> disable' instead.",
                     key, value);
        }
        goto out;
    } else if (len_strcmp(key, keylen, "inode-quota") ||
               len_strcmp(key, keylen, "features.inode-quota")) {
        ret = gf_string2boolean(value, &b);
        if (ret)
            goto out;
        ret = -1;
        if (b) {
            snprintf(
                errstr, size,
                " 'gluster volume set <VOLNAME> %s %s' is deprecated."
                " Use 'gluster volume inode-quota <VOLNAME> enable' instead.",
                key, value);
        } else {
            /* inode-quota disable not supported,
             * use quota disable
             */
            snprintf(errstr, size,
                     " 'gluster volume set <VOLNAME> %s %s' is deprecated."
                     " Use 'gluster volume quota <VOLNAME> disable' instead.",
                     key, value);
        }
        goto out;
    }

    ret = 0;
out:
    return ret;
}

int
glusterd_brick_op_build_payload(glusterd_op_t op,
                                glusterd_brickinfo_t *brickinfo,
                                gd1_mgmt_brick_op_req **req, dict_t *dict)
{
    int ret = -1;
    gd1_mgmt_brick_op_req *brick_req = NULL;
    char *volname = NULL;
    char name[1024] = {
        0,
    };
    gf_xl_afr_op_t heal_op = GF_SHD_OP_INVALID;
    xlator_t *this = NULL;
    glusterd_volinfo_t *volinfo = NULL;

    this = THIS;
    GF_ASSERT(this);

    GF_ASSERT(op < GD_OP_MAX);
    GF_ASSERT(op > GD_OP_NONE);
    GF_ASSERT(req);

    switch (op) {
        case GD_OP_REMOVE_BRICK:
        case GD_OP_STOP_VOLUME:
            brick_req = GF_CALLOC(1, sizeof(*brick_req),
                                  gf_gld_mt_mop_brick_req_t);
            if (!brick_req) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY,
                        NULL);
                goto out;
            }
            brick_req->op = GLUSTERD_BRICK_TERMINATE;
            brick_req->name = brickinfo->path;
            glusterd_set_brick_status(brickinfo, GF_BRICK_STOPPING);
            break;
        case GD_OP_PROFILE_VOLUME:
            brick_req = GF_CALLOC(1, sizeof(*brick_req),
                                  gf_gld_mt_mop_brick_req_t);

            if (!brick_req) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY,
                        NULL);
                goto out;
            }

            brick_req->op = GLUSTERD_BRICK_XLATOR_INFO;
            brick_req->name = brickinfo->path;

            break;
        case GD_OP_HEAL_VOLUME: {
            brick_req = GF_CALLOC(1, sizeof(*brick_req),
                                  gf_gld_mt_mop_brick_req_t);
            if (!brick_req) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY,
                        NULL);
                goto out;
            }

            brick_req->op = GLUSTERD_BRICK_XLATOR_OP;
            brick_req->name = "";
            ret = dict_get_int32n(dict, "heal-op", SLEN("heal-op"),
                                  (int32_t *)&heal_op);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_GET_FAILED,
                        "Key=heal-op", NULL);
                goto out;
            }
            ret = dict_set_int32n(dict, "xl-op", SLEN("xl-op"), heal_op);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                        "Key=xl-op", NULL);
                goto out;
            }
        } break;
        case GD_OP_STATUS_VOLUME: {
            brick_req = GF_CALLOC(1, sizeof(*brick_req),
                                  gf_gld_mt_mop_brick_req_t);
            if (!brick_req) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY,
                        NULL);
                goto out;
            }
            brick_req->op = GLUSTERD_BRICK_STATUS;
            brick_req->name = "";
            ret = dict_set_strn(dict, "brick-name", SLEN("brick-name"),
                                brickinfo->path);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                        "Key=brick-name", NULL);
                goto out;
            }
        } break;
        case GD_OP_REBALANCE:
        case GD_OP_DEFRAG_BRICK_VOLUME:
            brick_req = GF_CALLOC(1, sizeof(*brick_req),
                                  gf_gld_mt_mop_brick_req_t);
            if (!brick_req) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY,
                        NULL);
                goto out;
            }

            brick_req->op = GLUSTERD_BRICK_XLATOR_DEFRAG;
            ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_GET_FAILED,
                        "Key=volname", NULL);
                goto out;
            }
            ret = glusterd_volinfo_find(volname, &volinfo);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, errno,
                        GD_MSG_VOLINFO_GET_FAIL, "Volume=%s", volname, NULL);
                goto out;
            }
            snprintf(name, sizeof(name), "%s-dht", volname);
            brick_req->name = gf_strdup(name);

            break;
        case GD_OP_SNAP:
        case GD_OP_BARRIER:
            brick_req = GF_CALLOC(1, sizeof(*brick_req),
                                  gf_gld_mt_mop_brick_req_t);
            if (!brick_req) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY,
                        NULL);
                goto out;
            }
            brick_req->op = GLUSTERD_BRICK_BARRIER;
            brick_req->name = brickinfo->path;
            break;

        default:
            goto out;
            break;
    }

    brick_req->dict.dict_len = 0;
    brick_req->dict.dict_val = NULL;
    ret = dict_allocate_and_serialize(dict, &brick_req->input.input_val,
                                      &brick_req->input.input_len);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno,
                GD_MSG_DICT_ALLOC_AND_SERL_LENGTH_GET_FAIL, NULL);
        goto out;
    }
    *req = brick_req;
    ret = 0;

out:
    if (ret && brick_req)
        GF_FREE(brick_req);
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_node_op_build_payload(glusterd_op_t op, gd1_mgmt_brick_op_req **req,
                               dict_t *dict)
{
    int ret = -1;
    gd1_mgmt_brick_op_req *brick_req = NULL;
    char *volname = NULL;

    GF_ASSERT(op < GD_OP_MAX);
    GF_ASSERT(op > GD_OP_NONE);
    GF_ASSERT(req);
    xlator_t *this = NULL;
    this = THIS;
    GF_ASSERT(this);

    switch (op) {
        case GD_OP_PROFILE_VOLUME:
            brick_req = GF_CALLOC(1, sizeof(*brick_req),
                                  gf_gld_mt_mop_brick_req_t);
            if (!brick_req) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY,
                        NULL);
                goto out;
            }

            brick_req->op = GLUSTERD_NODE_PROFILE;
            brick_req->name = "";

            break;

        case GD_OP_STATUS_VOLUME:
            brick_req = GF_CALLOC(1, sizeof(*brick_req),
                                  gf_gld_mt_mop_brick_req_t);
            if (!brick_req) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY,
                        NULL);
                goto out;
            }

            brick_req->op = GLUSTERD_NODE_STATUS;
            brick_req->name = "";

            break;

        case GD_OP_SCRUB_STATUS:
        case GD_OP_SCRUB_ONDEMAND:
            brick_req = GF_CALLOC(1, sizeof(*brick_req),
                                  gf_gld_mt_mop_brick_req_t);
            if (!brick_req) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY,
                        NULL);
                goto out;
            }

            brick_req->op = GLUSTERD_NODE_BITROT;

            ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_GET_FAILED,
                        "Key=volname", NULL);
                goto out;
            }

            brick_req->name = gf_strdup(volname);
            break;
        default:
            goto out;
    }

    brick_req->dict.dict_len = 0;
    brick_req->dict.dict_val = NULL;
    ret = dict_allocate_and_serialize(dict, &brick_req->input.input_val,
                                      &brick_req->input.input_len);

    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno,
                GD_MSG_DICT_ALLOC_AND_SERL_LENGTH_GET_FAIL, NULL);
        goto out;
    }

    *req = brick_req;
    ret = 0;

out:
    if (ret && brick_req)
        GF_FREE(brick_req);
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_validate_quorum_options(xlator_t *this, char *fullkey, char *value,
                                 char **op_errstr)
{
    int ret = 0;
    char *key = NULL;
    volume_option_t *opt = NULL;

    if (!glusterd_is_quorum_option(fullkey))
        goto out;
    key = strchr(fullkey, '.');
    if (key == NULL) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_STRCHR_FAIL, NULL);
        ret = -1;
        goto out;
    }
    key++;
    opt = xlator_volume_option_get(this, key);
    if (!opt) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_VOLINFO_GET_FAIL, NULL);
        ret = -1;
        goto out;
    }
    ret = xlator_option_validate(this, key, value, opt, op_errstr);
out:
    return ret;
}

static int
glusterd_validate_brick_mx_options(xlator_t *this, char *fullkey, char *value,
                                   char **op_errstr)
{
    int ret = 0;

    // Placeholder function for now

    return ret;
}

static int
glusterd_validate_shared_storage(char *value, char *errstr)
{
    int32_t ret = -1;
    int32_t count = -1;
    char *op = NULL;
    char hook_script[PATH_MAX] = "";
    xlator_t *this = NULL;
    glusterd_conf_t *conf = NULL;
    int32_t len = 0;
    glusterd_volinfo_t *volinfo = NULL;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    GF_VALIDATE_OR_GOTO(this->name, value, out);
    GF_VALIDATE_OR_GOTO(this->name, errstr, out);

    if ((strcmp(value, "enable")) && (strcmp(value, "disable"))) {
        snprintf(errstr, PATH_MAX,
                 "Invalid option(%s). Valid options "
                 "are 'enable' and 'disable'",
                 value);
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY, "%s",
               errstr);
        ret = -1;
        goto out;
    }

    len = snprintf(hook_script, sizeof(hook_script),
                   "%s" GLUSTERD_SHRD_STRG_HOOK_SCRIPT, conf->workdir);
    if ((len < 0) || (len >= sizeof(hook_script))) {
        ret = -1;
        goto out;
    }

    ret = sys_access(hook_script, R_OK | X_OK);
    if (ret) {
        len = snprintf(errstr, PATH_MAX,
                       "The hook-script (%s) required "
                       "for this operation is not present. "
                       "Please install the hook-script "
                       "and retry",
                       hook_script);
        if (len < 0) {
            strncpy(errstr, "<error>", PATH_MAX);
        }
        gf_msg(this->name, GF_LOG_ERROR, ENOENT, GD_MSG_FILE_OP_FAILED, "%s",
               errstr);
        goto out;
    }

    if (!strncmp(value, "disable", SLEN("disable"))) {
        ret = dict_get_strn(conf->opts, GLUSTERD_SHARED_STORAGE_KEY,
                            SLEN(GLUSTERD_SHARED_STORAGE_KEY), &op);
        if (ret || !strncmp(op, "disable", SLEN("disable"))) {
            snprintf(errstr, PATH_MAX,
                     "Shared storage volume "
                     "does not exist. Please enable shared storage"
                     " for creating shared storage volume.");
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_SHARED_STORAGE_DOES_NOT_EXIST, "%s", errstr);
            ret = -1;
            goto out;
        }
        goto out;
    }

    ret = glusterd_volinfo_find(GLUSTER_SHARED_STORAGE, &volinfo);
    if (!ret) {
        snprintf(errstr, PATH_MAX,
                 "Shared storage volume(" GLUSTER_SHARED_STORAGE
                 ") already exists.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_ALREADY_EXIST, "%s",
               errstr);
        ret = -1;
        goto out;
    }

    ret = glusterd_count_connected_peers(&count);
    if (ret) {
        snprintf(errstr, PATH_MAX,
                 "Failed to calculate number of connected peers.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PEER_COUNT_GET_FAIL, "%s",
               errstr);
        goto out;
    }

    if (count <= 1) {
        snprintf(errstr, PATH_MAX,
                 "More than one node should "
                 "be up/present in the cluster to enable this option");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INSUFFICIENT_UP_NODES, "%s",
               errstr);
        ret = -1;
        goto out;
    }

out:
    return ret;
}

static int
glusterd_validate_localtime_logging(char *value, char *errstr)
{
    int32_t ret = -1;
    xlator_t *this = NULL;
    glusterd_conf_t *conf = NULL;
    int already_enabled = 0;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);
    GF_VALIDATE_OR_GOTO(this->name, value, out);

    already_enabled = gf_log_get_localtime();

    ret = 0;
    if (strcmp(value, "enable") == 0) {
        gf_log_set_localtime(1);
        if (!already_enabled)
            gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_LOCALTIME_LOGGING_ENABLE,
                   "localtime logging enable");
    } else if (strcmp(value, "disable") == 0) {
        gf_log_set_localtime(0);
        if (already_enabled)
            gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_LOCALTIME_LOGGING_DISABLE,
                   "localtime logging disable");
    } else {
        ret = -1;
        GF_VALIDATE_OR_GOTO(this->name, errstr, out);
        snprintf(errstr, PATH_MAX,
                 "Invalid option(%s). Valid options "
                 "are 'enable' and 'disable'",
                 value);
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY, "%s",
               errstr);
    }

out:
    return ret;
}

static int
glusterd_validate_daemon_log_level(char *value, char *errstr)
{
    int32_t ret = -1;
    xlator_t *this = NULL;
    glusterd_conf_t *conf = NULL;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    GF_VALIDATE_OR_GOTO(this->name, value, out);

    ret = 0;

    if ((strcmp(value, "INFO")) && (strcmp(value, "WARNING")) &&
        (strcmp(value, "DEBUG")) && (strcmp(value, "TRACE")) &&
        (strcmp(value, "ERROR"))) {
        ret = -1;
        GF_VALIDATE_OR_GOTO(this->name, errstr, out);
        snprintf(errstr, PATH_MAX,
                 "Invalid option(%s). Valid options "
                 "are 'INFO' or 'WARNING' or 'ERROR' or 'DEBUG' or "
                 " 'TRACE'",
                 value);
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY, "%s",
               errstr);
    }

out:
    return ret;
}

static int
glusterd_op_stage_set_volume(dict_t *dict, char **op_errstr)
{
    int ret = -1;
    char *volname = NULL;
    int exists = 0;
    char *key = NULL;
    char *key_fixed = NULL;
    char *value = NULL;
    char *val_dup = NULL;
    char keystr[100] = {
        0,
    };
    int keystr_len;
    int keylen;
    char *trash_path = NULL;
    int trash_path_len = 0;
    int count = 0;
    int dict_count = 0;
    char errstr[PATH_MAX] = {
        0,
    };
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    dict_t *val_dict = NULL;
    gf_boolean_t global_opt = _gf_false;
    gf_boolean_t key_matched = _gf_false; /* if a key was processed or not*/
    glusterd_volinfo_t *voliter = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;
    uint32_t new_op_version = GD_OP_VERSION_MIN;
    uint32_t local_new_op_version = GD_OP_VERSION_MIN;
    uint32_t local_new_client_op_version = GD_OP_VERSION_MIN;
    uint32_t key_op_version = GD_OP_VERSION_MIN;
    uint32_t local_key_op_version = GD_OP_VERSION_MIN;
    gf_boolean_t origin_glusterd = _gf_true;
    gf_boolean_t check_op_version = _gf_true;
    gf_boolean_t trash_enabled = _gf_false;
    gf_boolean_t all_vol = _gf_false;
    struct volopt_map_entry *vmep = NULL;

    GF_ASSERT(dict);
    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    /* Check if we can support the required op-version
     * This check is not done on the originator glusterd. The originator
     * glusterd sets this value.
     */
    origin_glusterd = is_origin_glusterd(dict);

    if (!origin_glusterd) {
        /* Check for v3.3.x origin glusterd */
        check_op_version = dict_get_str_boolean(dict, "check-op-version",
                                                _gf_false);

        if (check_op_version) {
            ret = dict_get_uint32(dict, "new-op-version", &new_op_version);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Key=new-op-version", NULL);
                goto out;
            }

            if ((new_op_version > GD_OP_VERSION_MAX) ||
                (new_op_version < GD_OP_VERSION_MIN)) {
                ret = -1;
                snprintf(errstr, sizeof(errstr),
                         "Required op_version (%d) is not supported."
                         " Max supported op version is %d",
                         new_op_version, priv->op_version);
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UNSUPPORTED_VERSION,
                       "%s", errstr);
                goto out;
            }
        }
    }

    ret = dict_get_int32_sizen(dict, "count", &dict_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Count(dict),not set in Volume-Set");
        goto out;
    }

    if (dict_count == 0) {
        /*No options would be specified of volume set help */
        if (dict_get_sizen(dict, "help")) {
            ret = 0;
            goto out;
        }

        if (dict_get_sizen(dict, "help-xml")) {
#if (HAVE_LIB_XML)
            ret = 0;
            goto out;
#else
            ret = -1;
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MODULE_NOT_INSTALLED,
                   "libxml not present in the system");
            *op_errstr = gf_strdup(
                "Error: xml libraries not present to produce xml-output");
            goto out;
#endif
        }
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_NO_OPTIONS_GIVEN,
               "No options received ");
        *op_errstr = gf_strdup("Options not specified");
        ret = -1;
        goto out;
    }

    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                "Key=volname", NULL);
        goto out;
    }

    if (strcasecmp(volname, "all") != 0) {
        ret = glusterd_volinfo_find(volname, &volinfo);
        if (ret) {
            snprintf(errstr, sizeof(errstr), FMTSTR_CHECK_VOL_EXISTS, volname);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
                   FMTSTR_CHECK_VOL_EXISTS, volname);
            goto out;
        }

        ret = glusterd_validate_volume_id(dict, volinfo);
        if (ret)
            goto out;

        local_new_op_version = volinfo->op_version;
        local_new_client_op_version = volinfo->client_op_version;

    } else {
        all_vol = _gf_true;
    }

    val_dict = dict_new();
    if (!val_dict) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        goto out;
    }

    for (count = 1; ret != 1; count++) {
        keystr_len = sprintf(keystr, "key%d", count);
        ret = dict_get_strn(dict, keystr, keystr_len, &key);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_GET_FAILED,
                    "Key=%s", keystr, NULL);
            break;
        }

        keystr_len = sprintf(keystr, "value%d", count);
        ret = dict_get_strn(dict, keystr, keystr_len, &value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "invalid key,value pair in 'volume set'");
            ret = -1;
            goto out;
        }

        key_matched = _gf_false;
        keylen = strlen(key);
        if (len_strcmp(key, keylen, "config.memory-accounting")) {
            key_matched = _gf_true;
            gf_msg_debug(this->name, 0,
                         "enabling memory accounting for volume %s", volname);
            ret = 0;
        } else if (len_strcmp(key, keylen, "config.transport")) {
            key_matched = _gf_true;
            gf_msg_debug(this->name, 0, "changing transport-type for volume %s",
                         volname);
            ret = 0;
            /* if value is none of 'tcp/rdma/tcp,rdma' error out */
            if (!((strcasecmp(value, "rdma") == 0) ||
                  (strcasecmp(value, "tcp") == 0) ||
                  (strcasecmp(value, "tcp,rdma") == 0) ||
                  (strcasecmp(value, "rdma,tcp") == 0))) {
                ret = snprintf(errstr, sizeof(errstr),
                               "transport-type %s does not exist", value);
                /* lets not bother about above return value,
                   its a failure anyways */
                ret = -1;
                goto out;
            }
        } else if (len_strcmp(key, keylen, "ganesha.enable")) {
            key_matched = _gf_true;
            if (strcmp(value, "off") == 0) {
                ret = ganesha_manage_export(dict, "off", _gf_true, op_errstr);
                if (ret)
                    goto out;
            }
        }

        if (!key_matched) {
            ret = glusterd_check_bitrot_cmd(key, keylen, errstr,
                                            sizeof(errstr));
            if (ret)
                goto out;
            ret = glusterd_check_quota_cmd(key, keylen, value, errstr,
                                           sizeof(errstr));
            if (ret)
                goto out;
        }

        if (is_key_glusterd_hooks_friendly(key))
            continue;

        ret = glusterd_volopt_validate(volinfo, dict, key, value, op_errstr);
        if (ret)
            goto out;

        exists = glusterd_check_option_exists(key, &key_fixed);
        if (exists == -1) {
            ret = -1;
            goto out;
        }

        if (!exists) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
                   "Option with name: %s does not exist", key);
            ret = snprintf(errstr, sizeof(errstr), "option : %s does not exist",
                           key);
            if (key_fixed)
                snprintf(errstr + ret, sizeof(errstr) - ret,
                         "\nDid you mean %s?", key_fixed);
            ret = -1;
            goto out;
        }

        if (key_fixed) {
            key = key_fixed;
            keylen = strlen(key_fixed);
        }
#ifdef HAVE_LIBURING
        if (len_strcmp(key, keylen, "storage.linux-io_uring")) {
            if (volinfo == NULL) {
                snprintf(errstr, sizeof(errstr), "vol info is NULL for %s.",
                         volname);
                ret = -1;
                goto out;
            }
            if (volinfo->status == GLUSTERD_STATUS_STARTED) {
                snprintf(errstr, sizeof(errstr),
                         "Changing this option is "
                         "not supported when volume is in started state. "
                         "Please stop the volume.");
                ret = -1;
                goto out;
            }
        }
#endif
        if (len_strcmp(key, keylen, "cluster.granular-entry-heal")) {
            /* For granular entry-heal, if the set command was
             * invoked through volume-set CLI, then allow the
             * command only if the volume is still in 'Created'
             * state
             */
            if (volinfo && volinfo->status != GLUSTERD_STATUS_NONE &&
                (dict_get_sizen(dict, "is-special-key") == NULL)) {
                snprintf(errstr, sizeof(errstr),
                         " 'gluster volume set <VOLNAME> %s {enable, disable}'"
                         " is not supported."
                         " Use 'gluster volume heal <VOLNAME> "
                         "granular-entry-heal {enable, disable}' instead.",
                         key);
                ret = -1;
                goto out;
            }
        } else if (len_strcmp(key, keylen, GLUSTERD_GLOBAL_OP_VERSION_KEY)) {
            /* Check if the key is cluster.op-version and set
             * local_new_op_version to the value given if possible.
             */
            if (!all_vol) {
                ret = -1;
                snprintf(errstr, sizeof(errstr),
                         "Option \"%s\" is not valid for a single volume", key);
                goto out;
            }
            /* Check if cluster.op-version is the only option being
             * set
             */
            if (count != 1) {
                ret = -1;
                snprintf(errstr, sizeof(errstr),
                         "Option \"%s\" cannot be set along with other options",
                         key);
                goto out;
            }
            /* Just reusing the variable, but I'm using it for
             * storing the op-version from value
             */
            ret = gf_string2uint(value, &local_key_op_version);
            if (ret) {
                snprintf(errstr, sizeof(errstr),
                         "invalid number format \"%s\" in option \"%s\"", value,
                         key);
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY, "%s",
                       errstr);
                goto out;
            }

            if (local_key_op_version > GD_OP_VERSION_MAX ||
                local_key_op_version < GD_OP_VERSION_MIN) {
                ret = -1;
                snprintf(errstr, sizeof(errstr),
                         "Required op_version (%d) is not supported."
                         " Max supported op version is %d",
                         local_key_op_version, priv->op_version);
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VERSION_UNSUPPORTED,
                       "%s", errstr);
                goto out;
            }
            if (local_key_op_version > priv->op_version) {
                local_new_op_version = local_key_op_version;
            } else {
                ret = -1;
                snprintf(errstr, sizeof(errstr),
                         "Required op-version (%d) should"
                         " not be equal or lower than current"
                         " cluster op-version (%d).",
                         local_key_op_version, priv->op_version);
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VERSION_UNSUPPORTED,
                       "%s", errstr);
                goto out;
            }

            goto cont;
        }

        ALL_VOLUME_OPTION_CHECK(volname, _gf_false, key, ret, op_errstr, out);
        ret = glusterd_validate_quorum_options(this, key, value, op_errstr);
        if (ret)
            goto out;

        ret = glusterd_validate_brick_mx_options(this, key, value, op_errstr);
        if (ret)
            goto out;

        vmep = gd_get_vmep(key);
        local_key_op_version = glusterd_get_op_version_from_vmep(vmep);
        if (local_key_op_version > local_new_op_version)
            local_new_op_version = local_key_op_version;
        if (gd_is_client_option(vmep) &&
            (local_key_op_version > local_new_client_op_version))
            local_new_client_op_version = local_key_op_version;

        sprintf(keystr, "op-version%d", count);
        if (origin_glusterd) {
            ret = dict_set_uint32(dict, keystr, local_key_op_version);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set key-op-version in dict");
                goto out;
            }
        } else if (check_op_version) {
            ret = dict_get_uint32(dict, keystr, &key_op_version);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Failed to get key-op-version from dict");
                goto out;
            }
            if (local_key_op_version != key_op_version) {
                ret = -1;
                snprintf(errstr, sizeof(errstr),
                         "option: %s op-version mismatch", key);
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_OP_VERSION_MISMATCH,
                       "%s, required op-version = %" PRIu32
                       ", available op-version = %" PRIu32,
                       errstr, key_op_version, local_key_op_version);
                goto out;
            }
        }

        global_opt = glusterd_check_globaloption(key);

        if (len_strcmp(key, keylen, GLUSTERD_SHARED_STORAGE_KEY)) {
            ret = glusterd_validate_shared_storage(value, errstr);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_SHARED_STRG_VOL_OPT_VALIDATE_FAIL,
                       "Failed to validate shared storage volume options");
                goto out;
            }
        } else if (len_strcmp(key, keylen, GLUSTERD_LOCALTIME_LOGGING_KEY)) {
            ret = glusterd_validate_localtime_logging(value, errstr);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_LOCALTIME_LOGGING_VOL_OPT_VALIDATE_FAIL,
                       "Failed to validate localtime logging volume options");
                goto out;
            }
        } else if (len_strcmp(key, keylen, GLUSTERD_DAEMON_LOG_LEVEL_KEY)) {
            ret = glusterd_validate_daemon_log_level(value, errstr);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_DAEMON_LOG_LEVEL_VOL_OPT_VALIDATE_FAIL,
                       "Failed to validate daemon-log-level volume options");
                goto out;
            }
        } else if (len_strcmp(key, keylen, "features.trash-dir")) {
            if (volinfo) {
                ret = glusterd_volinfo_get(volinfo, VKEY_FEATURES_TRASH,
                                           &val_dup);
                if (!ret && val_dup) {
                    ret = gf_string2boolean(val_dup, &trash_enabled);
                    if (ret)
                        goto out;
                }
            }
            if (!trash_enabled) {
                snprintf(errstr, sizeof(errstr),
                         "Trash translator is not enabled. "
                         "Use volume set %s trash on",
                         volname);
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_SET_FAIL,
                       "Unable to set the options in 'volume set': %s", errstr);
                ret = -1;
                goto out;
            }
            if (strchr(value, '/')) {
                snprintf(errstr, sizeof(errstr),
                         "Path is not allowed as option");
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_SET_FAIL,
                       "Unable to set the options in 'volume set': %s", errstr);
                ret = -1;
                goto out;
            }

            list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
            {
                /* Check for local brick */
                if (!gf_uuid_compare(brickinfo->uuid, MY_UUID)) {
                    trash_path_len = strlen(value) + strlen(brickinfo->path) +
                                     2;
                    trash_path = GF_MALLOC(trash_path_len, gf_common_mt_char);
                    snprintf(trash_path, trash_path_len, "%s/%s",
                             brickinfo->path, value);

                    /* Checks whether a directory with
                       given option exists or not */
                    if (!sys_access(trash_path, R_OK)) {
                        snprintf(errstr, sizeof(errstr), "Path %s exists",
                                 value);
                        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_SET_FAIL,
                               "Unable to set the options in 'volume set': %s",
                               errstr);
                        ret = -1;
                        goto out;
                    } else {
                        gf_msg_debug(this->name, 0,
                                     "Directory with given name does not exist,"
                                     " continuing");
                    }

                    if (volinfo->status == GLUSTERD_STATUS_STARTED &&
                        brickinfo->status != GF_BRICK_STARTED) {
                        /* If volume is in started state , checks
                           whether bricks are online */
                        snprintf(errstr, sizeof(errstr),
                                 "One or more bricks are down");
                        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_SET_FAIL,
                               "Unable to set the options in 'volume set': %s",
                               errstr);
                        ret = -1;
                        goto out;
                    }
                }
                if (trash_path) {
                    GF_FREE(trash_path);
                    trash_path = NULL;
                }
            }
        }

        ret = dict_set_strn(val_dict, key, keylen, value);

        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Unable to set the options in 'volume set'");
            ret = -1;
            goto out;
        }

        *op_errstr = NULL;
        if (!global_opt && !all_vol)
            ret = glusterd_validate_reconfopts(volinfo, val_dict, op_errstr);
        else if (!all_vol) {
            voliter = NULL;
            cds_list_for_each_entry(voliter, &priv->volumes, vol_list)
            {
                ret = glusterd_validate_globalopts(voliter, val_dict,
                                                   op_errstr);
                if (ret)
                    break;
            }
        }

        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
                   "Could not create temp volfile, some option failed: %s",
                   *op_errstr);
            goto out;
        }
        dict_deln(val_dict, key, keylen);

        if (key_fixed) {
            GF_FREE(key_fixed);
            key_fixed = NULL;
        }
    }

    /* Check if all the connected clients support the new client-op-version
     */
    ret = glusterd_check_client_op_version_support(
        volname, local_new_client_op_version, op_errstr);
    if (ret)
        goto out;
cont:
    if (origin_glusterd) {
        ret = dict_set_uint32(dict, "new-op-version", local_new_op_version);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set new-op-version in dict");
            goto out;
        }
        /* Set this value in dict so other peers know to check for
         * op-version. This is a hack for 3.3.x compatibility
         *
         * TODO: Remove this and the other places this is referred once
         * 3.3.x compatibility is not required
         */
        ret = dict_set_int32_sizen(dict, "check-op-version", 1);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set check-op-version in dict");
            goto out;
        }
    }

    ret = 0;

out:
    if (val_dict)
        dict_unref(val_dict);

    if (trash_path)
        GF_FREE(trash_path);

    GF_FREE(key_fixed);
    if (errstr[0] != '\0')
        *op_errstr = gf_strdup(errstr);

    if (ret) {
        if (!(*op_errstr)) {
            *op_errstr = gf_strdup("Error, Validation Failed");
            gf_msg_debug(this->name, 0, "Error, Cannot Validate option :%s",
                         *op_errstr);
        } else {
            gf_msg_debug(this->name, 0, "Error, Cannot Validate option");
        }
    }
    return ret;
}

static int
glusterd_op_stage_reset_volume(dict_t *dict, char **op_errstr)
{
    int ret = 0;
    char *volname = NULL;
    int exists = 0;
    char msg[2048] = {0};
    char *key = NULL;
    char *key_fixed = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;

    this = THIS;
    GF_ASSERT(this);

    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    if (strcasecmp(volname, "all") != 0) {
        ret = glusterd_volinfo_find(volname, &volinfo);
        if (ret) {
            snprintf(msg, sizeof(msg), FMTSTR_CHECK_VOL_EXISTS, volname);
            goto out;
        }

        ret = glusterd_validate_volume_id(dict, volinfo);
        if (ret)
            goto out;
    }

    ret = dict_get_strn(dict, "key", SLEN("key"), &key);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get option key");
        goto out;
    }

    /* *
     * If key ganesha.enable is set, then volume should be unexported from
     * ganesha server. Also it is a volume-level option, perform only when
     * volume name not equal to "all"(in other words if volinfo != NULL)
     */
    if (volinfo && (!strcmp(key, "all") || !strcmp(key, "ganesha.enable"))) {
        if (glusterd_check_ganesha_export(volinfo)) {
            ret = ganesha_manage_export(dict, "off", _gf_true, op_errstr);
            if (ret)
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_NFS_GNS_RESET_FAIL,
                       "Could not reset ganesha.enable key");
        }
    }

    if (strcmp(key, "all")) {
        exists = glusterd_check_option_exists(key, &key_fixed);
        if (exists == -1) {
            ret = -1;
            goto out;
        }

        if (!exists) {
            ret = snprintf(msg, sizeof(msg), "Option %s does not exist", key);
            if (key_fixed)
                snprintf(msg + ret, sizeof(msg) - ret, "\nDid you mean %s?",
                         key_fixed);
            ret = -1;
            goto out;
        } else if (exists > 0) {
            if (key_fixed)
                key = key_fixed;

            /* 'gluster volume set/reset <VOLNAME>
             * features.quota/features.inode-quota' should
             * not be allowed as it is deprecated.
             * Setting and resetting quota/inode-quota features
             * should be allowed only through 'gluster volume quota
             * <VOLNAME> enable/disable'.
             * But, 'gluster volume set features.quota-deem-statfs'
             * can be turned on/off when quota is enabled.
             */

            if (strcmp(VKEY_FEATURES_INODE_QUOTA, key) == 0 ||
                strcmp(VKEY_FEATURES_QUOTA, key) == 0) {
                snprintf(msg, sizeof(msg),
                         "'gluster volume "
                         "reset <VOLNAME> %s' is deprecated. "
                         "Use 'gluster volume quota <VOLNAME> "
                         "disable' instead.",
                         key);
                ret = -1;
                goto out;
            }
            ALL_VOLUME_OPTION_CHECK(volname, _gf_false, key, ret, op_errstr,
                                    out);
        }
    }

out:
    GF_FREE(key_fixed);

    if (msg[0] != '\0') {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_OP_STAGE_RESET_VOL_FAIL,
               "%s", msg);
        *op_errstr = gf_strdup(msg);
    }

    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
glusterd_op_stage_sync_volume(dict_t *dict, char **op_errstr)
{
    int ret = -1;
    char *volname = NULL;
    char *hostname = NULL;
    glusterd_peerinfo_t *peerinfo = NULL;
    char msg[2048] = {
        0,
    };
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = NULL;
    this = THIS;
    GF_ASSERT(this);

    ret = dict_get_strn(dict, "hostname", SLEN("hostname"), &hostname);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "hostname couldn't be "
                 "retrieved from msg");
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_GET_FAILED,
                "Key=hostname", NULL);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    if (glusterd_gf_is_local_addr(hostname)) {
        // volname is not present in case of sync all
        ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
        if (!ret) {
            ret = glusterd_volinfo_find(volname, &volinfo);
            if (ret) {
                snprintf(msg, sizeof(msg),
                         "Volume %s "
                         "does not exist",
                         volname);
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_VOL_NOT_FOUND,
                        "Volume=%s", volname, NULL);
                *op_errstr = gf_strdup(msg);
                goto out;
            }
        }
    } else {
        RCU_READ_LOCK;

        peerinfo = glusterd_peerinfo_find(NULL, hostname);
        if (peerinfo == NULL) {
            RCU_READ_UNLOCK;
            ret = -1;
            snprintf(msg, sizeof(msg), "%s, is not a friend", hostname);
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_PEER_NOT_FOUND,
                    "Peer_name=%s", hostname, NULL);
            *op_errstr = gf_strdup(msg);
            goto out;

        } else if (!peerinfo->connected) {
            RCU_READ_UNLOCK;
            ret = -1;
            snprintf(msg, sizeof(msg),
                     "%s, is not connected at "
                     "the moment",
                     hostname);
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_PEER_DISCONNECTED,
                    "Peer_name=%s", hostname, NULL);
            *op_errstr = gf_strdup(msg);
            goto out;
        }

        RCU_READ_UNLOCK;
    }

out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}

static int
glusterd_op_stage_status_volume(dict_t *dict, char **op_errstr)
{
    int ret = -1;
    uint32_t cmd = 0;
    char msg[2048] = {
        0,
    };
    char *volname = NULL;
    char *brick = NULL;
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    dict_t *vol_opts = NULL;
#ifdef BUILD_GNFS
    gf_boolean_t nfs_disabled = _gf_false;
#endif
    gf_boolean_t shd_enabled = _gf_false;

    GF_ASSERT(dict);
    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_uint32(dict, "cmd", &cmd);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_GET_FAILED,
                "Key=cmd", NULL);
        goto out;
    }

    if (cmd & GF_CLI_STATUS_ALL)
        goto out;

    if ((cmd & GF_CLI_STATUS_QUOTAD) &&
        (priv->op_version == GD_OP_VERSION_MIN)) {
        snprintf(msg, sizeof(msg),
                 "The cluster is operating at "
                 "version 1. Getting the status of quotad is not "
                 "allowed in this state.");
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_QUOTA_GET_STAT_FAIL,
                msg, NULL);
        ret = -1;
        goto out;
    }

    if ((cmd & GF_CLI_STATUS_SNAPD) &&
        (priv->op_version < GD_OP_VERSION_3_6_0)) {
        snprintf(msg, sizeof(msg),
                 "The cluster is operating at "
                 "version less than %d. Getting the "
                 "status of snapd is not allowed in this state.",
                 GD_OP_VERSION_3_6_0);
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_SNAP_STATUS_FAIL, msg,
                NULL);
        ret = -1;
        goto out;
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), FMTSTR_CHECK_VOL_EXISTS, volname);
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_VOLINFO_GET_FAIL,
                "Volume=%s", volname, NULL);
        ret = -1;
        goto out;
    }

    ret = glusterd_validate_volume_id(dict, volinfo);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_VALIDATE_FAILED, NULL);
        goto out;
    }

    ret = glusterd_is_volume_started(volinfo);
    if (!ret) {
        snprintf(msg, sizeof(msg), "Volume %s is not started", volname);
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_VOL_NOT_STARTED,
                "Volume=%s", volname, NULL);
        ret = -1;
        goto out;
    }

    vol_opts = volinfo->dict;

    if ((cmd & GF_CLI_STATUS_SHD) != 0) {
        if (glusterd_is_shd_compatible_volume(volinfo)) {
            shd_enabled = gd_is_self_heal_enabled(volinfo, vol_opts);
        } else {
            ret = -1;
            snprintf(msg, sizeof(msg), "Volume %s is not Self-heal compatible",
                     volname);
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_VOL_SHD_NOT_COMP,
                    "Volume=%s", volname, NULL);
            goto out;
        }
        if (!shd_enabled) {
            ret = -1;
            snprintf(msg, sizeof(msg),
                     "Self-heal Daemon is disabled for volume %s", volname);
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_SELF_HEALD_DISABLED,
                    "Volume=%s", volname, NULL);
            goto out;
        }
#ifdef BUILD_GNFS
    } else if ((cmd & GF_CLI_STATUS_NFS) != 0) {
        nfs_disabled = dict_get_str_boolean(vol_opts, NFS_DISABLE_MAP_KEY,
                                            _gf_false);
        if (nfs_disabled) {
            ret = -1;
            snprintf(msg, sizeof(msg), "NFS server is disabled for volume %s",
                     volname);
            gf_smsg(this->name, GF_LOG_ERROR, errno,
                    GD_MSG_NFS_GANESHA_DISABLED, "Volume=%s", volname, NULL);
            goto out;
        }
#endif
    } else if ((cmd & GF_CLI_STATUS_QUOTAD) != 0) {
        if (!glusterd_is_volume_quota_enabled(volinfo)) {
            ret = -1;
            snprintf(msg, sizeof(msg),
                     "Volume %s does not have "
                     "quota enabled",
                     volname);
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_QUOTA_DISABLED,
                    "Volume=%s", volname, NULL);
            goto out;
        }
    } else if ((cmd & GF_CLI_STATUS_BITD) != 0) {
        if (!glusterd_is_bitrot_enabled(volinfo)) {
            ret = -1;
            snprintf(msg, sizeof(msg),
                     "Volume %s does not have "
                     "bitrot enabled",
                     volname);
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_BITROT_NOT_ENABLED,
                    "Volume=%s", volname, NULL);
            goto out;
        }
    } else if ((cmd & GF_CLI_STATUS_SCRUB) != 0) {
        if (!glusterd_is_bitrot_enabled(volinfo)) {
            ret = -1;
            snprintf(msg, sizeof(msg),
                     "Volume %s does not have "
                     "bitrot enabled. Scrubber will be enabled "
                     "automatically if bitrot is enabled",
                     volname);
            gf_smsg(
                this->name, GF_LOG_ERROR, errno, GD_MSG_BITROT_NOT_ENABLED,
                "Scrubber will be enabled automatically if bitrot is enabled",
                "Volume=%s", volname, NULL);
            goto out;
        }
    } else if ((cmd & GF_CLI_STATUS_SNAPD) != 0) {
        if (!glusterd_is_snapd_enabled(volinfo)) {
            ret = -1;
            snprintf(msg, sizeof(msg),
                     "Volume %s does not have "
                     "uss enabled",
                     volname);
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_SNAPD_NOT_RUNNING,
                    "Volume=%s", volname, NULL);
            goto out;
        }
    } else if ((cmd & GF_CLI_STATUS_BRICK) != 0) {
        ret = dict_get_strn(dict, "brick", SLEN("brick"), &brick);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                    "Key=brick", NULL);
            goto out;
        }

        ret = glusterd_volume_brickinfo_get_by_brick(brick, volinfo, &brickinfo,
                                                     _gf_false);
        if (ret) {
            snprintf(msg, sizeof(msg),
                     "No brick %s in"
                     " volume %s",
                     brick, volname);
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_BRICK_NOT_FOUND,
                    "Brick=%s, Volume=%s", brick, volname, NULL);
            ret = -1;
            goto out;
        }
    }

    ret = 0;

out:
    if (ret) {
        if (msg[0] != '\0')
            *op_errstr = gf_strdup(msg);
        else
            *op_errstr = gf_strdup("Validation Failed for Status");
    }

    gf_msg_debug(this->name, 0, "Returning: %d", ret);
    return ret;
}

int
glusterd_op_stage_stats_volume(dict_t *dict, char **op_errstr)
{
    int ret = -1;
    char *volname = NULL;
    char msg[2048] = {
        0,
    };
    int32_t stats_op = GF_CLI_STATS_NONE;
    glusterd_volinfo_t *volinfo = NULL;

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        snprintf(msg, sizeof(msg), "Volume name get failed");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Volume %s, "
                 "doesn't exist",
                 volname);
        goto out;
    }

    ret = glusterd_validate_volume_id(dict, volinfo);
    if (ret)
        goto out;

    ret = dict_get_int32n(dict, "op", SLEN("op"), &stats_op);
    if (ret) {
        snprintf(msg, sizeof(msg), "Volume profile op get failed");
        goto out;
    }

    if (GF_CLI_STATS_START == stats_op) {
        if (_gf_true == glusterd_is_profile_on(volinfo)) {
            snprintf(msg, sizeof(msg),
                     "Profile on Volume %s is"
                     " already started",
                     volinfo->volname);
            ret = -1;
            goto out;
        }
    } else if ((GF_CLI_STATS_STOP == stats_op) ||
               (GF_CLI_STATS_INFO == stats_op)) {
        if (_gf_false == glusterd_is_profile_on(volinfo)) {
            snprintf(msg, sizeof(msg),
                     "Profile on Volume %s is"
                     " not started",
                     volinfo->volname);
            ret = -1;

            goto out;
        }
    }
    if ((GF_CLI_STATS_TOP == stats_op) || (GF_CLI_STATS_INFO == stats_op)) {
        if (_gf_false == glusterd_is_volume_started(volinfo)) {
            snprintf(msg, sizeof(msg), "Volume %s is not started.",
                     volinfo->volname);
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_STARTED, "%s",
                   msg);
            ret = -1;
            goto out;
        }
    }
    ret = 0;
out:
    if (msg[0] != '\0') {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_OP_STAGE_STATS_VOL_FAIL,
               "%s", msg);
        *op_errstr = gf_strdup(msg);
    }
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

static int
_delete_reconfig_opt(dict_t *this, char *key, data_t *value, void *data)
{
    int32_t *is_force = 0;

    GF_ASSERT(data);
    is_force = (int32_t *)data;

    /* Keys which has the flag VOLOPT_FLAG_NEVER_RESET
     * should not be deleted
     */

    if (_gf_true ==
        glusterd_check_voloption_flags(key, VOLOPT_FLAG_NEVER_RESET)) {
        if (*is_force != 1)
            *is_force = *is_force | GD_OP_PROTECTED;
        goto out;
    }

    if (*is_force != 1) {
        if (_gf_true ==
            glusterd_check_voloption_flags(key, VOLOPT_FLAG_FORCE)) {
            /* indicate to caller that we don't set the option
             * due to being protected
             */
            *is_force = *is_force | GD_OP_PROTECTED;
            goto out;
        } else {
            *is_force = *is_force | GD_OP_UNPROTECTED;
        }
    }

    gf_msg_debug("glusterd", 0, "deleting dict with key=%s,value=%s", key,
                 value->data);
    dict_del(this, key);
    /**Delete scrubber (pause/resume) option from the dictionary if bitrot
     * option is going to be reset
     * */
    if (!strncmp(key, VKEY_FEATURES_BITROT, strlen(VKEY_FEATURES_BITROT))) {
        dict_del_sizen(this, VKEY_FEATURES_SCRUB);
    }
out:
    return 0;
}

static int
_delete_reconfig_global_opt(dict_t *this, char *key, data_t *value, void *data)
{
    GF_ASSERT(data);

    if (strcmp(GLUSTERD_GLOBAL_OPT_VERSION, key) == 0)
        goto out;

    _delete_reconfig_opt(this, key, value, data);
out:
    return 0;
}

static int
glusterd_options_reset(glusterd_volinfo_t *volinfo, char *key,
                       int32_t *is_force)
{
    int ret = 0;
    data_t *value = NULL;
    char *key_fixed = NULL;
    xlator_t *this = NULL;
    glusterd_svc_t *svc = NULL;

    this = THIS;
    GF_ASSERT(this);
    GF_ASSERT(volinfo->dict);
    GF_ASSERT(key);

    if (!strncmp(key, "all", 3)) {
        dict_foreach(volinfo->dict, _delete_reconfig_opt, is_force);
        ret = glusterd_enable_default_options(volinfo, NULL);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FAIL_DEFAULT_OPT_SET,
                   "Failed to set "
                   "default options on reset for volume %s",
                   volinfo->volname);
            goto out;
        }
    } else {
        value = dict_get(volinfo->dict, key);
        if (!value) {
            gf_msg_debug(this->name, 0, "no value set for option %s", key);
            goto out;
        }
        _delete_reconfig_opt(volinfo->dict, key, value, is_force);
        ret = glusterd_enable_default_options(volinfo, key);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FAIL_DEFAULT_OPT_SET,
                   "Failed to set "
                   "default value for option '%s' on reset for "
                   "volume %s",
                   key, volinfo->volname);
            goto out;
        }
    }

    gd_update_volume_op_versions(volinfo);
    if (!volinfo->is_snap_volume) {
        svc = &(volinfo->snapd.svc);
        ret = svc->manager(svc, volinfo, PROC_START_NO_WAIT);
        if (ret)
            goto out;
    }
    svc = &(volinfo->gfproxyd.svc);
    ret = svc->reconfigure(volinfo);
    if (ret)
        goto out;

    svc = &(volinfo->shd.svc);
    ret = svc->reconfigure(volinfo);
    if (ret)
        goto out;

    ret = glusterd_create_volfiles_and_notify_services(volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
               "Unable to create volfile for"
               " 'volume reset'");
        ret = -1;
        goto out;
    }

    ret = glusterd_store_volinfo(volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
    if (ret)
        goto out;

    if (GLUSTERD_STATUS_STARTED == volinfo->status) {
        ret = glusterd_svcs_reconfigure(volinfo);
        if (ret)
            goto out;
    }

    ret = 0;

out:
    GF_FREE(key_fixed);
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_op_reset_all_volume_options(xlator_t *this, dict_t *dict)
{
    char *key = NULL;
    char *key_fixed = NULL;
    int ret = -1;
    int32_t is_force = 0;
    glusterd_conf_t *conf = NULL;
    dict_t *dup_opt = NULL;
    gf_boolean_t all = _gf_false;
    char *next_version = NULL;
    gf_boolean_t quorum_action = _gf_false;

    conf = this->private;
    ret = dict_get_strn(dict, "key", SLEN("key"), &key);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get key");
        goto out;
    }

    ret = dict_get_int32n(dict, "force", SLEN("force"), &is_force);
    if (ret)
        is_force = 0;

    if (strcmp(key, "all")) {
        ret = glusterd_check_option_exists(key, &key_fixed);
        if (ret <= 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
                   "Option %s does not "
                   "exist",
                   key);
            ret = -1;
            goto out;
        }
    } else {
        all = _gf_true;
    }

    if (key_fixed)
        key = key_fixed;

    ret = -1;
    dup_opt = dict_new();
    if (!dup_opt) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        goto out;
    }
    if (!all) {
        dict_copy(conf->opts, dup_opt);
        dict_del(dup_opt, key);
    }
    ret = glusterd_get_next_global_opt_version_str(conf->opts, &next_version);
    if (ret)
        goto out;

    ret = dict_set_strn(dup_opt, GLUSTERD_GLOBAL_OPT_VERSION,
                        SLEN(GLUSTERD_GLOBAL_OPT_VERSION), next_version);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=%s", GLUSTERD_GLOBAL_OPT_VERSION, NULL);
        goto out;
    }

    ret = glusterd_store_options(this, dup_opt);
    if (ret)
        goto out;

    if (glusterd_is_quorum_changed(conf->opts, key, NULL))
        quorum_action = _gf_true;

    ret = dict_set_dynstrn(conf->opts, GLUSTERD_GLOBAL_OPT_VERSION,
                           SLEN(GLUSTERD_GLOBAL_OPT_VERSION), next_version);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=%s", GLUSTERD_GLOBAL_OPT_VERSION, NULL);
        goto out;
    } else
        next_version = NULL;

    if (!all) {
        dict_del(conf->opts, key);
    } else {
        dict_foreach(conf->opts, _delete_reconfig_global_opt, &is_force);
    }
out:
    GF_FREE(key_fixed);
    if (dup_opt)
        dict_unref(dup_opt);

    gf_msg_debug(this->name, 0, "returning %d", ret);
    if (quorum_action)
        glusterd_do_quorum_action();
    GF_FREE(next_version);
    return ret;
}

static int
glusterd_op_reset_volume(dict_t *dict, char **op_rspstr)
{
    glusterd_volinfo_t *volinfo = NULL;
    int ret = -1;
    char *volname = NULL;
    char *key = NULL;
    char *key_fixed = NULL;
    int32_t is_force = 0;
    gf_boolean_t quorum_action = _gf_false;
    xlator_t *this = NULL;

    this = THIS;
    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    if (strcasecmp(volname, "all") == 0) {
        ret = glusterd_op_reset_all_volume_options(this, dict);
        goto out;
    }

    ret = dict_get_int32n(dict, "force", SLEN("force"), &is_force);
    if (ret)
        is_force = 0;

    ret = dict_get_strn(dict, "key", SLEN("key"), &key);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get option key");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               FMTSTR_CHECK_VOL_EXISTS, volname);
        goto out;
    }

    if (strcmp(key, "all") &&
        glusterd_check_option_exists(key, &key_fixed) != 1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
               "volinfo dict inconsistency: option %s not found", key);
        ret = -1;
        goto out;
    }
    if (key_fixed)
        key = key_fixed;

    if (glusterd_is_quorum_changed(volinfo->dict, key, NULL))
        quorum_action = _gf_true;

    ret = glusterd_options_reset(volinfo, key, &is_force);
    if (ret == -1) {
        gf_asprintf(op_rspstr, "Volume reset : failed");
    } else if (is_force & GD_OP_PROTECTED) {
        if (is_force & GD_OP_UNPROTECTED) {
            gf_asprintf(op_rspstr,
                        "All unprotected fields were"
                        " reset. To reset the protected fields,"
                        " use 'force'.");
        } else {
            ret = -1;
            gf_asprintf(op_rspstr,
                        "'%s' is protected. To reset"
                        " use 'force'.",
                        key);
        }
    }

    if (!strcmp(key, "ganesha.enable") || !strcmp(key, "all")) {
        if (glusterd_check_ganesha_export(volinfo) &&
            is_origin_glusterd(dict)) {
            ret = manage_export_config(volname, "off", op_rspstr);
            if (ret)
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_NFS_GNS_RESET_FAIL,
                       "Could not reset ganesha.enable key");
        }
    }

out:
    GF_FREE(key_fixed);
    if (quorum_action)
        glusterd_do_quorum_action();

    gf_msg_debug(this->name, 0, "'volume reset' returning %d", ret);
    return ret;
}

int
glusterd_stop_bricks(glusterd_volinfo_t *volinfo)
{
    glusterd_brickinfo_t *brickinfo = NULL;

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        /*TODO: Need to change @del_brick in brick_stop to _gf_true
         * once we enable synctask in peer rpc prog */
        if (glusterd_brick_stop(volinfo, brickinfo, _gf_false)) {
            gf_event(EVENT_BRICK_STOP_FAILED, "peer=%s;volume=%s;brick=%s",
                     brickinfo->hostname, volinfo->volname, brickinfo->path);
            return -1;
        }
    }

    return 0;
}

int
glusterd_start_bricks(glusterd_volinfo_t *volinfo)

{
    int ret = -1;
    glusterd_brickinfo_t *brickinfo = NULL;

    GF_ASSERT(volinfo);

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (!brickinfo->start_triggered) {
            pthread_mutex_lock(&brickinfo->restart_mutex);
            {
                /* coverity[SLEEP] */
                ret = glusterd_brick_start(volinfo, brickinfo, _gf_false,
                                           _gf_false);
            }
            pthread_mutex_unlock(&brickinfo->restart_mutex);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_DISCONNECTED,
                       "Failed to start %s:%s for %s", brickinfo->hostname,
                       brickinfo->path, volinfo->volname);
                gf_event(EVENT_BRICK_START_FAILED, "peer=%s;volume=%s;brick=%s",
                         brickinfo->hostname, volinfo->volname,
                         brickinfo->path);
                goto out;
            }
        }
    }
    ret = 0;
out:
    return ret;
}

static int
glusterd_update_volumes_dict(glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    xlator_t *this = NULL;
    glusterd_conf_t *conf = NULL;
    char *address_family_str = NULL;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    /* 3.9.0 onwards gNFS will be disabled by default. In case of an upgrade
     * from anything below than 3.9.0 to 3.9.x the volume's dictionary will
     * not have 'nfs.disable' key set which means the same will not be set
     * to on until explicitly done. setnfs.disable to 'on' at op-version
     * bump up flow is the ideal way here. The same is also applicable for
     * transport.address-family where if the transport type is set to tcp
     * then transport.address-family is defaulted to 'inet'.
     */
    if (conf->op_version >= GD_OP_VERSION_3_9_0) {
        if (dict_get_str_boolean(volinfo->dict, NFS_DISABLE_MAP_KEY, 1)) {
            ret = dict_set_dynstr_with_alloc(volinfo->dict, NFS_DISABLE_MAP_KEY,
                                             "on");
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                       "Failed to set "
                       "option ' NFS_DISABLE_MAP_KEY ' on "
                       "volume %s",
                       volinfo->volname);
                goto out;
            }
        }
        ret = dict_get_strn(volinfo->dict, "transport.address-family",
                            SLEN("transport.address-family"),
                            &address_family_str);
        if (ret) {
            if (volinfo->transport_type == GF_TRANSPORT_TCP) {
                ret = dict_set_dynstr_with_alloc(
                    volinfo->dict, "transport.address-family", "inet");
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, errno,
                           GD_MSG_DICT_SET_FAILED,
                           "failed to set transport."
                           "address-family on %s",
                           volinfo->volname);
                    goto out;
                }
            }
        }
    }
    ret = glusterd_store_volinfo(volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);

out:
    return ret;
}

static int
glusterd_set_brick_mx_opts(dict_t *dict, char *key, char *value,
                           char **op_errstr)
{
    int32_t ret = -1;
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);
    GF_VALIDATE_OR_GOTO(this->name, dict, out);
    GF_VALIDATE_OR_GOTO(this->name, key, out);
    GF_VALIDATE_OR_GOTO(this->name, value, out);
    GF_VALIDATE_OR_GOTO(this->name, op_errstr, out);

    ret = 0;

    priv = this->private;

    if (!strcmp(key, GLUSTERD_BRICK_MULTIPLEX_KEY)) {
        ret = dict_set_dynstrn(priv->opts, GLUSTERD_BRICK_MULTIPLEX_KEY,
                               SLEN(GLUSTERD_BRICK_MULTIPLEX_KEY),
                               gf_strdup(value));
    }

out:
    return ret;
}

/* This is a hack to prevent client-io-threads from being loaded in the graph
 * when the cluster-op-version is bumped up from 3.8.x to 3.13.x. The key is
 * deleted subsequently in glusterd_create_volfiles(). */
static int
glusterd_dict_set_skip_cliot_key(glusterd_volinfo_t *volinfo)
{
    return dict_set_int32n(volinfo->dict, "skip-CLIOT", SLEN("skip-CLIOT"), 1);
}

static int
glusterd_op_set_all_volume_options(xlator_t *this, dict_t *dict,
                                   char **op_errstr)
{
    char *key = NULL;
    char *key_fixed = NULL;
    char *value = NULL;
    char *dup_value = NULL;
    int ret = -1;
    glusterd_conf_t *conf = NULL;
    dict_t *dup_opt = NULL;
    char *next_version = NULL;
    gf_boolean_t quorum_action = _gf_false;
    uint32_t op_version = 0;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_svc_t *svc = NULL;
    gf_boolean_t svcs_reconfigure = _gf_false;

    conf = this->private;
    ret = dict_get_strn(dict, "key1", SLEN("key1"), &key);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_GET_FAILED,
                "Key=key1", NULL);
        goto out;
    }

    ret = dict_get_strn(dict, "value1", SLEN("value1"), &value);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "invalid key,value pair in 'volume set'");
        goto out;
    }

    ret = glusterd_check_option_exists(key, &key_fixed);
    if (ret <= 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UNKNOWN_KEY,
               "Invalid key %s", key);
        ret = -1;
        goto out;
    }

    if (key_fixed)
        key = key_fixed;

    ret = glusterd_set_shared_storage(dict, key, value, op_errstr);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SHARED_STRG_SET_FAIL,
               "Failed to set shared storage option");
        goto out;
    }

    ret = glusterd_set_brick_mx_opts(dict, key, value, op_errstr);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_MX_SET_FAIL,
               "Failed to set brick multiplexing option");
        goto out;
    }

    /* If the key is cluster.op-version, set conf->op_version to the value
     * if needed and save it.
     */
    if (strcmp(key, GLUSTERD_GLOBAL_OP_VERSION_KEY) == 0) {
        ret = 0;

        ret = gf_string2uint(value, &op_version);
        if (ret)
            goto out;

        if (op_version >= conf->op_version) {
            conf->op_version = op_version;

            /* When a bump up happens, update the quota.conf file
             * as well. This is because, till 3.7 we had a quota
             * conf version v1.1 in quota.conf. When inode-quota
             * feature is introduced, this needs to be changed to
             * v1.2 in quota.conf and 16 bytes uuid in quota.conf
             * needs to be changed to 17 bytes. Look
             * glusterd_store_quota_config for more details.
             */
            cds_list_for_each_entry(volinfo, &conf->volumes, vol_list)
            {
                ret = glusterd_store_quota_config(
                    volinfo, NULL, NULL, GF_QUOTA_OPTION_TYPE_UPGRADE, NULL);
                if (ret)
                    goto out;
                ret = glusterd_update_volumes_dict(volinfo);
                if (ret)
                    goto out;

                if (glusterd_dict_set_skip_cliot_key(volinfo))
                    goto out;

                if (!volinfo->is_snap_volume) {
                    svc = &(volinfo->snapd.svc);
                    ret = svc->manager(svc, volinfo, PROC_START_NO_WAIT);
                    if (ret)
                        goto out;
                }

                svc = &(volinfo->gfproxyd.svc);
                ret = svc->reconfigure(volinfo);
                if (ret)
                    goto out;

                svc = &(volinfo->shd.svc);
                ret = svc->reconfigure(volinfo);
                if (ret)
                    goto out;

                ret = glusterd_create_volfiles_and_notify_services(volinfo);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_VOLFILE_CREATE_FAIL,
                           "Unable to create volfile for"
                           " 'volume set'");
                    goto out;
                }
                if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                    svcs_reconfigure = _gf_true;
                }
            }
            if (svcs_reconfigure) {
                ret = glusterd_svcs_reconfigure(NULL);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SVC_RESTART_FAIL,
                           "Unable to restart "
                           "services");
                    goto out;
                }
            }

            ret = glusterd_store_global_info(this);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_OP_VERS_STORE_FAIL,
                       "Failed to store op-version.");
            }
        }
        /* No need to save cluster.op-version in conf->opts
         */
        goto out;
    }
    ret = -1;
    dup_opt = dict_new();
    if (!dup_opt) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        goto out;
    }
    dict_copy(conf->opts, dup_opt);
    ret = dict_set_str(dup_opt, key, value);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    }

    ret = glusterd_get_next_global_opt_version_str(conf->opts, &next_version);
    if (ret)
        goto out;

    ret = dict_set_strn(dup_opt, GLUSTERD_GLOBAL_OPT_VERSION,
                        SLEN(GLUSTERD_GLOBAL_OPT_VERSION), next_version);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=%s", GLUSTERD_GLOBAL_OPT_VERSION, NULL);
        goto out;
    }

    ret = glusterd_store_options(this, dup_opt);
    if (ret)
        goto out;

    if (glusterd_is_quorum_changed(conf->opts, key, value))
        quorum_action = _gf_true;

    ret = dict_set_dynstrn(conf->opts, GLUSTERD_GLOBAL_OPT_VERSION,
                           SLEN(GLUSTERD_GLOBAL_OPT_VERSION), next_version);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=%s", GLUSTERD_GLOBAL_OPT_VERSION, NULL);
        goto out;
    } else
        next_version = NULL;

    dup_value = gf_strdup(value);
    if (!dup_value)
        goto out;

    ret = dict_set_dynstr(conf->opts, key, dup_value);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    } else
        dup_value = NULL; /* Protect the allocation from GF_FREE */

out:
    GF_FREE(dup_value);
    GF_FREE(key_fixed);
    if (dup_opt)
        dict_unref(dup_opt);

    gf_msg_debug(this->name, 0, "returning %d", ret);
    if (quorum_action)
        glusterd_do_quorum_action();
    GF_FREE(next_version);
    return ret;
}

int
glusterd_op_get_max_opversion(char **op_errstr, dict_t *rsp_dict)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO(THIS->name, rsp_dict, out);

    ret = dict_set_int32n(rsp_dict, "max-opversion", SLEN("max-opversion"),
                          GD_OP_VERSION_MAX);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Setting value for max-opversion to dict failed");
        goto out;
    }

out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_set_shared_storage(dict_t *dict, char *key, char *value,
                            char **op_errstr)
{
    int32_t ret = -1;
    char hooks_args[PATH_MAX] = {
        0,
    };
    char errstr[PATH_MAX] = {
        0,
    };
    xlator_t *this = NULL;
    int32_t len = 0;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);
    GF_VALIDATE_OR_GOTO(this->name, dict, out);
    GF_VALIDATE_OR_GOTO(this->name, key, out);
    GF_VALIDATE_OR_GOTO(this->name, value, out);
    GF_VALIDATE_OR_GOTO(this->name, op_errstr, out);

    ret = 0;

    if (strcmp(key, GLUSTERD_SHARED_STORAGE_KEY)) {
        goto out;
    }

    /* Re-create the brick path so as to be *
     * able to re-use it                    *
     */
    ret = recursive_rmdir(GLUSTER_SHARED_STORAGE_BRICK_DIR);
    if (ret) {
        snprintf(errstr, PATH_MAX,
                 "Failed to remove shared "
                 "storage brick(%s). "
                 "Reason: %s",
                 GLUSTER_SHARED_STORAGE_BRICK_DIR, strerror(errno));
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED, "%s",
               errstr);
        ret = -1;
        goto out;
    }

    ret = mkdir_p(GLUSTER_SHARED_STORAGE_BRICK_DIR, 0755, _gf_true);
    if (-1 == ret) {
        snprintf(errstr, PATH_MAX,
                 "Failed to create shared "
                 "storage brick(%s). "
                 "Reason: %s",
                 GLUSTER_SHARED_STORAGE_BRICK_DIR, strerror(errno));
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_CREATE_DIR_FAILED, "%s",
               errstr);
        goto out;
    }

    if (is_origin_glusterd(dict)) {
        len = snprintf(hooks_args, sizeof(hooks_args),
                       "is_originator=1,local_node_hostname=%s",
                       local_node_hostname);
    } else {
        len = snprintf(hooks_args, sizeof(hooks_args),
                       "is_originator=0,local_node_hostname=%s",
                       local_node_hostname);
    }
    if ((len < 0) || (len >= sizeof(hooks_args))) {
        ret = -1;
        goto out;
    }

    ret = dict_set_dynstr_with_alloc(dict, "hooks_args", hooks_args);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
               "Failed to set"
               " hooks_args in dict.");
        goto out;
    }

out:
    if (ret && strlen(errstr)) {
        *op_errstr = gf_strdup(errstr);
    }

    return ret;
}

static int
glusterd_op_set_volume(dict_t *dict, char **errstr)
{
    int ret = 0;
    glusterd_volinfo_t *volinfo = NULL;
    char *volname = NULL;
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;
    int count = 1;
    char *key = NULL;
    char *key_fixed = NULL;
    char *value = NULL;
    char keystr[50] = {
        0,
    };
    int keylen;
    gf_boolean_t global_opt = _gf_false;
    gf_boolean_t global_opts_set = _gf_false;
    glusterd_volinfo_t *voliter = NULL;
    int32_t dict_count = 0;
    gf_boolean_t check_op_version = _gf_false;
    uint32_t new_op_version = 0;
    gf_boolean_t quorum_action = _gf_false;
    glusterd_svc_t *svc = NULL;

    this = THIS;
    GF_ASSERT(this);

    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_int32n(dict, "count", SLEN("count"), &dict_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Count(dict),not set in Volume-Set");
        goto out;
    }

    if (dict_count == 0) {
        ret = glusterd_volset_help(NULL, errstr);
        goto out;
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    if (strcasecmp(volname, "all") == 0) {
        ret = glusterd_op_set_all_volume_options(this, dict, errstr);
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               FMTSTR_CHECK_VOL_EXISTS, volname);
        goto out;
    }

    /* TODO: Remove this once v3.3 compatibility is not required */
    check_op_version = dict_get_str_boolean(dict, "check-op-version",
                                            _gf_false);

    if (check_op_version) {
        ret = dict_get_uint32(dict, "new-op-version", &new_op_version);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get new op-version from dict");
            goto out;
        }
    }

    for (count = 1; ret != -1; count++) {
        keylen = snprintf(keystr, sizeof(keystr), "key%d", count);
        ret = dict_get_strn(dict, keystr, keylen, &key);
        if (ret)
            break;

        keylen = snprintf(keystr, sizeof(keystr), "value%d", count);
        ret = dict_get_strn(dict, keystr, keylen, &value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "invalid key,value pair in 'volume set'");
            ret = -1;
            goto out;
        }

        if (strcmp(key, "config.memory-accounting") == 0) {
            ret = gf_string2boolean(value, &volinfo->memory_accounting);
            if (ret == -1) {
                gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                       "Invalid value in key-value pair.");
                goto out;
            }
        }

        if (strcmp(key, "config.transport") == 0) {
            gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_VOL_TRANSPORT_TYPE_CHANGE,
                   "changing transport-type for volume %s to %s", volname,
                   value);
            ret = 0;
            if (strcasecmp(value, "rdma") == 0) {
                volinfo->transport_type = GF_TRANSPORT_RDMA;
            } else if (strcasecmp(value, "tcp") == 0) {
                volinfo->transport_type = GF_TRANSPORT_TCP;
            } else if ((strcasecmp(value, "tcp,rdma") == 0) ||
                       (strcasecmp(value, "rdma,tcp") == 0)) {
                volinfo->transport_type = GF_TRANSPORT_BOTH_TCP_RDMA;
            } else {
                ret = -1;
                goto out;
            }
        }

        ret = glusterd_check_ganesha_cmd(key, value, errstr, dict);
        if (ret == -1)
            goto out;

        if (!is_key_glusterd_hooks_friendly(key)) {
            ret = glusterd_check_option_exists(key, &key_fixed);
            GF_ASSERT(ret);
            if (ret <= 0) {
                key_fixed = NULL;
                goto out;
            }
        }

        global_opt = _gf_false;
        if (glusterd_check_globaloption(key)) {
            global_opt = _gf_true;
            global_opts_set = _gf_true;
        }

        if (!global_opt)
            value = gf_strdup(value);

        if (!value) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_SET_FAIL,
                   "Unable to set the options in 'volume set'");
            ret = -1;
            goto out;
        }

        if (key_fixed)
            key = key_fixed;

        if (glusterd_is_quorum_changed(volinfo->dict, key, value))
            quorum_action = _gf_true;

        if (global_opt) {
            cds_list_for_each_entry(voliter, &priv->volumes, vol_list)
            {
                value = gf_strdup(value);
                ret = dict_set_dynstr(voliter->dict, key, value);
                if (ret)
                    goto out;
            }
        } else {
            ret = dict_set_dynstr(volinfo->dict, key, value);
            if (ret)
                goto out;
        }

        if (key_fixed) {
            GF_FREE(key_fixed);
            key_fixed = NULL;
        }
    }

    if (count == 1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_NO_OPTIONS_GIVEN,
               "No options received ");
        ret = -1;
        goto out;
    }

    /* Update the cluster op-version before regenerating volfiles so that
     * correct volfiles are generated
     */
    if (new_op_version > priv->op_version) {
        priv->op_version = new_op_version;
        ret = glusterd_store_global_info(this);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_OP_VERS_STORE_FAIL,
                   "Failed to store op-version");
            goto out;
        }
    }
    if (!global_opts_set) {
        gd_update_volume_op_versions(volinfo);

        if (!volinfo->is_snap_volume) {
            svc = &(volinfo->snapd.svc);
            ret = svc->manager(svc, volinfo, PROC_START_NO_WAIT);
            if (ret)
                goto out;
        }
        svc = &(volinfo->gfproxyd.svc);
        ret = svc->reconfigure(volinfo);
        if (ret)
            goto out;

        svc = &(volinfo->shd.svc);
        ret = svc->reconfigure(volinfo);
        if (ret)
            goto out;

        ret = glusterd_create_volfiles_and_notify_services(volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
                   "Unable to create volfile for"
                   " 'volume set'");
            ret = -1;
            goto out;
        }

        ret = glusterd_store_volinfo(volinfo,
                                     GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
            goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
            ret = glusterd_svcs_reconfigure(volinfo);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SVC_RESTART_FAIL,
                       "Unable to restart services");
                goto out;
            }
        }

    } else {
        cds_list_for_each_entry(voliter, &priv->volumes, vol_list)
        {
            volinfo = voliter;
            gd_update_volume_op_versions(volinfo);

            if (!volinfo->is_snap_volume) {
                svc = &(volinfo->snapd.svc);
                ret = svc->manager(svc, volinfo, PROC_START_NO_WAIT);
                if (ret)
                    goto out;
            }

            svc = &(volinfo->gfproxyd.svc);
            ret = svc->reconfigure(volinfo);
            if (ret)
                goto out;

            svc = &(volinfo->shd.svc);
            ret = svc->reconfigure(volinfo);
            if (ret)
                goto out;

            ret = glusterd_create_volfiles_and_notify_services(volinfo);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
                       "Unable to create volfile for"
                       " 'volume set'");
                ret = -1;
                goto out;
            }

            ret = glusterd_store_volinfo(volinfo,
                                         GLUSTERD_VOLINFO_VER_AC_INCREMENT);
            if (ret)
                goto out;

            if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                ret = glusterd_svcs_reconfigure(volinfo);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SVC_RESTART_FAIL,
                           "Unable to restart services");
                    goto out;
                }
            }
        }
    }

out:
    GF_FREE(key_fixed);
    gf_msg_debug(this->name, 0, "returning %d", ret);
    if (quorum_action)
        glusterd_do_quorum_action();
    return ret;
}

static int
glusterd_op_sync_volume(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    int ret = -1;
    char *volname = NULL;
    char *hostname = NULL;
    char msg[2048] = {
        0,
    };
    int count = 1;
    int vol_count = 0;
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_strn(dict, "hostname", SLEN("hostname"), &hostname);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "hostname couldn't be "
                 "retrieved from msg");
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_GET_FAILED,
                "Key=hostname", NULL);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    if (!glusterd_gf_is_local_addr(hostname)) {
        ret = 0;
        goto out;
    }

    // volname is not present in case of sync all
    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (!ret) {
        ret = glusterd_volinfo_find(volname, &volinfo);
        if (ret) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
                   "Volume with name: %s "
                   "not exists",
                   volname);
            goto out;
        }
    }

    if (!rsp_dict) {
        // this should happen only on source
        gf_smsg(this->name, GF_LOG_INFO, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        ret = 0;
        goto out;
    }

    if (volname) {
        ret = glusterd_add_volume_to_dict(volinfo, rsp_dict, 1, "volume");
        if (ret)
            goto out;
        vol_count = 1;
    } else {
        cds_list_for_each_entry(volinfo, &priv->volumes, vol_list)
        {
            ret = glusterd_add_volume_to_dict(volinfo, rsp_dict, count,
                                              "volume");
            if (ret)
                goto out;

            vol_count = count++;
        }
    }
    ret = dict_set_int32n(rsp_dict, "count", SLEN("count"), vol_count);

out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}

static int
glusterd_add_profile_volume_options(glusterd_volinfo_t *volinfo)
{
    int ret = -1;

    GF_ASSERT(volinfo);

    ret = dict_set_nstrn(volinfo->dict, VKEY_DIAG_LAT_MEASUREMENT,
                         SLEN(VKEY_DIAG_LAT_MEASUREMENT), "on", SLEN("on"));
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "failed to set the volume %s "
               "option %s value %s",
               volinfo->volname, VKEY_DIAG_LAT_MEASUREMENT, "on");
        goto out;
    }

    ret = dict_set_nstrn(volinfo->dict, VKEY_DIAG_CNT_FOP_HITS,
                         SLEN(VKEY_DIAG_CNT_FOP_HITS), "on", SLEN("on"));
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "failed to set the volume %s "
               "option %s value %s",
               volinfo->volname, VKEY_DIAG_CNT_FOP_HITS, "on");
        goto out;
    }
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

static void
glusterd_remove_profile_volume_options(glusterd_volinfo_t *volinfo)
{
    GF_ASSERT(volinfo);

    dict_del_sizen(volinfo->dict, VKEY_DIAG_LAT_MEASUREMENT);
    dict_del_sizen(volinfo->dict, VKEY_DIAG_CNT_FOP_HITS);
}

int
glusterd_op_stats_volume(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    int ret = -1;
    char *volname = NULL;
    char msg[2048] = {
        0,
    };
    glusterd_volinfo_t *volinfo = NULL;
    int32_t stats_op = GF_CLI_STATS_NONE;

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "volume name get failed");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), "Volume %s does not exists", volname);

        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND, "%s", msg);
        goto out;
    }

    ret = dict_get_int32n(dict, "op", SLEN("op"), &stats_op);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "volume profile op get failed");
        goto out;
    }

    switch (stats_op) {
        case GF_CLI_STATS_START:
            ret = glusterd_add_profile_volume_options(volinfo);
            if (ret)
                goto out;
            break;
        case GF_CLI_STATS_STOP:
            glusterd_remove_profile_volume_options(volinfo);
            break;
        case GF_CLI_STATS_INFO:
        case GF_CLI_STATS_TOP:
            // info is already collected in brick op.
            // just goto out;
            ret = 0;
            goto out;
            break;
        default:
            GF_ASSERT(0);
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
                   "Invalid profile op: %d", stats_op);
            ret = -1;
            goto out;
            break;
    }
    ret = glusterd_create_volfiles_and_notify_services(volinfo);

    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
               "Unable to create volfile for"
               " 'volume set'");
        ret = -1;
        goto out;
    }

    ret = glusterd_store_volinfo(volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
    if (ret)
        goto out;

    if (GLUSTERD_STATUS_STARTED == volinfo->status) {
        ret = glusterd_svcs_reconfigure(volinfo);
        if (ret)
            goto out;
    }

    ret = 0;

out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}

static int
_add_remove_bricks_to_dict(dict_t *dict, glusterd_volinfo_t *volinfo,
                           char *prefix)
{
    int ret = -1;
    int count = 0;
    int i = 0;
    char brick_key[16] = {
        0,
    };
    char dict_key[64] = {
        /* dict_key is small as prefix is up to 32 chars */
        0,
    };
    int keylen;
    char *brick = NULL;
    xlator_t *this = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(volinfo);
    GF_ASSERT(prefix);

    this = THIS;
    GF_ASSERT(this);

    ret = dict_get_int32n(volinfo->rebal.dict, "count", SLEN("count"), &count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get brick count");
        goto out;
    }

    keylen = snprintf(dict_key, sizeof(dict_key), "%s.count", prefix);
    ret = dict_set_int32n(dict, dict_key, keylen, count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set brick count in dict");
        goto out;
    }

    for (i = 1; i <= count; i++) {
        keylen = snprintf(brick_key, sizeof(brick_key), "brick%d", i);

        ret = dict_get_strn(volinfo->rebal.dict, brick_key, keylen, &brick);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get %s", brick_key);
            goto out;
        }

        keylen = snprintf(dict_key, sizeof(dict_key), "%s.%s", prefix,
                          brick_key);
        if ((keylen < 0) || (keylen >= sizeof(dict_key))) {
            ret = -1;
            goto out;
        }
        ret = dict_set_strn(dict, dict_key, keylen, brick);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to add brick to dict");
            goto out;
        }
        brick = NULL;
    }

out:
    return ret;
}

/* This adds the respective task-id and all available parameters of a task into
 * a dictionary
 */
static int
_add_task_to_dict(dict_t *dict, glusterd_volinfo_t *volinfo, int op, int index)
{
    int ret = -1;
    char key[32] = {
        0,
    };
    int keylen;
    char *uuid_str = NULL;
    int status = 0;
    xlator_t *this = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(volinfo);

    this = THIS;
    GF_ASSERT(this);

    switch (op) {
        case GD_OP_REMOVE_BRICK:
            snprintf(key, sizeof(key), "task%d", index);
            ret = _add_remove_bricks_to_dict(dict, volinfo, key);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_ADD_REMOVE_BRICK_FAIL,
                       "Failed to add remove bricks to dict");
                goto out;
            }
        case GD_OP_REBALANCE:
            uuid_str = gf_strdup(uuid_utoa(volinfo->rebal.rebalance_id));
            status = volinfo->rebal.defrag_status;
            break;

        default:
            ret = -1;
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_NO_TASK_ID,
                   "%s operation doesn't have a"
                   " task_id",
                   gd_op_list[op]);
            goto out;
    }

    keylen = snprintf(key, sizeof(key), "task%d.type", index);
    ret = dict_set_strn(dict, key, keylen, (char *)gd_op_list[op]);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Error setting task type in dict");
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "task%d.id", index);

    if (!uuid_str)
        goto out;
    ret = dict_set_dynstrn(dict, key, keylen, uuid_str);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Error setting task id in dict");
        goto out;
    }
    uuid_str = NULL;

    keylen = snprintf(key, sizeof(key), "task%d.status", index);
    ret = dict_set_int32n(dict, key, keylen, status);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Error setting task status in dict");
        goto out;
    }

out:
    if (uuid_str)
        GF_FREE(uuid_str);
    return ret;
}

static int
glusterd_aggregate_task_status(dict_t *rsp_dict, glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    int tasks = 0;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    if (!gf_uuid_is_null(volinfo->rebal.rebalance_id)) {
        ret = _add_task_to_dict(rsp_dict, volinfo, volinfo->rebal.op, tasks);

        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to add task details to dict");
            goto out;
        }
        tasks++;
    }
    ret = dict_set_int32n(rsp_dict, "tasks", SLEN("tasks"), tasks);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Error setting tasks count in dict");
        goto out;
    }
out:
    return ret;
}

static int
glusterd_op_status_volume(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    int ret = -1;
    int node_count = 0;
    int brick_index = -1;
    int other_count = 0;
    int other_index = 0;
    uint32_t cmd = 0;
    char *volname = NULL;
    char *brick = NULL;
    xlator_t *this = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_conf_t *priv = NULL;
    dict_t *vol_opts = NULL;
#ifdef BUILD_GNFS
    gf_boolean_t nfs_disabled = _gf_false;
#endif
    gf_boolean_t shd_enabled = _gf_false;
    gf_boolean_t origin_glusterd = _gf_false;
    int snapd_enabled, bitrot_enabled, volume_quota_enabled;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;

    GF_ASSERT(priv);

    GF_ASSERT(dict);

    origin_glusterd = is_origin_glusterd(dict);

    ret = dict_get_uint32(dict, "cmd", &cmd);
    if (ret)
        goto out;

    if (origin_glusterd) {
        ret = 0;
        if ((cmd & GF_CLI_STATUS_ALL)) {
            ret = glusterd_get_all_volnames(rsp_dict);
            if (ret)
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLNAMES_GET_FAIL,
                       "failed to get all volume "
                       "names for status");
        }
    }

    ret = dict_set_uint32(rsp_dict, "cmd", cmd);
    if (ret)
        goto out;

    if (cmd & GF_CLI_STATUS_ALL)
        goto out;

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret)
        goto out;

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               "Volume with name: %s "
               "does not exist",
               volname);
        goto out;
    }
    vol_opts = volinfo->dict;

    if ((cmd & GF_CLI_STATUS_QUOTAD) != 0) {
        ret = glusterd_add_node_to_dict(priv->quotad_svc.name, rsp_dict, 0,
                                        vol_opts);
        if (ret)
            goto out;
        other_count++;
        node_count++;
#ifdef BUILD_GNFS
    } else if ((cmd & GF_CLI_STATUS_NFS) != 0) {
        ret = glusterd_add_node_to_dict(priv->nfs_svc.name, rsp_dict, 0,
                                        vol_opts);
        if (ret)
            goto out;
        other_count++;
        node_count++;
#endif
    } else if ((cmd & GF_CLI_STATUS_BITD) != 0) {
        ret = glusterd_add_node_to_dict(priv->bitd_svc.name, rsp_dict, 0,
                                        vol_opts);
        if (ret)
            goto out;
        other_count++;
        node_count++;
    } else if ((cmd & GF_CLI_STATUS_SCRUB) != 0) {
        ret = glusterd_add_node_to_dict(priv->scrub_svc.name, rsp_dict, 0,
                                        vol_opts);
        if (ret)
            goto out;
        other_count++;
        node_count++;
    } else if ((cmd & GF_CLI_STATUS_SNAPD) != 0) {
        ret = glusterd_add_snapd_to_dict(volinfo, rsp_dict, other_index);
        if (ret)
            goto out;
        other_count++;
        node_count++;
    } else if ((cmd & GF_CLI_STATUS_SHD) != 0) {
        ret = glusterd_add_shd_to_dict(volinfo, rsp_dict, other_index);
        if (ret)
            goto out;
        other_count++;
        node_count++;
    } else if ((cmd & GF_CLI_STATUS_BRICK) != 0) {
        ret = dict_get_strn(dict, "brick", SLEN("brick"), &brick);
        if (ret)
            goto out;

        ret = glusterd_volume_brickinfo_get_by_brick(brick, volinfo, &brickinfo,
                                                     _gf_false);
        if (ret)
            goto out;

        if (gf_uuid_compare(brickinfo->uuid, MY_UUID))
            goto out;

        glusterd_add_brick_to_dict(volinfo, brickinfo, rsp_dict, ++brick_index);
        if (cmd & GF_CLI_STATUS_DETAIL)
            glusterd_add_brick_detail_to_dict(volinfo, brickinfo, rsp_dict,
                                              brick_index);
        node_count++;

    } else if ((cmd & GF_CLI_STATUS_TASKS) != 0) {
        ret = glusterd_aggregate_task_status(rsp_dict, volinfo);
        goto out;

    } else {
        snapd_enabled = glusterd_is_snapd_enabled(volinfo);
        shd_enabled = gd_is_self_heal_enabled(volinfo, vol_opts);
#ifdef BUILD_GNFS
        nfs_disabled = dict_get_str_boolean(vol_opts, NFS_DISABLE_MAP_KEY,
                                            _gf_false);
#endif
        volume_quota_enabled = glusterd_is_volume_quota_enabled(volinfo);
        bitrot_enabled = glusterd_is_bitrot_enabled(volinfo);

        cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
        {
            brick_index++;
            if (gf_uuid_compare(brickinfo->uuid, MY_UUID))
                continue;

            glusterd_add_brick_to_dict(volinfo, brickinfo, rsp_dict,
                                       brick_index);

            if (cmd & GF_CLI_STATUS_DETAIL) {
                glusterd_add_brick_detail_to_dict(volinfo, brickinfo, rsp_dict,
                                                  brick_index);
            }
            node_count++;
        }

        if ((cmd & GF_CLI_STATUS_MASK) == GF_CLI_STATUS_NONE) {
            other_index = brick_index + 1;
            if (snapd_enabled) {
                ret = glusterd_add_snapd_to_dict(volinfo, rsp_dict,
                                                 other_index);
                if (ret)
                    goto out;
                other_count++;
                other_index++;
                node_count++;
            }

            if (glusterd_is_shd_compatible_volume(volinfo)) {
                if (shd_enabled) {
                    ret = glusterd_add_shd_to_dict(volinfo, rsp_dict,
                                                   other_index);
                    if (ret)
                        goto out;
                    other_count++;
                    other_index++;
                    node_count++;
                }
            }
#ifdef BUILD_GNFS
            if (!nfs_disabled) {
                ret = glusterd_add_node_to_dict(priv->nfs_svc.name, rsp_dict,
                                                other_index, vol_opts);
                if (ret)
                    goto out;
                other_index++;
                other_count++;
                node_count++;
            }
#endif
            if (volume_quota_enabled) {
                ret = glusterd_add_node_to_dict(priv->quotad_svc.name, rsp_dict,
                                                other_index, vol_opts);
                if (ret)
                    goto out;
                other_count++;
                node_count++;
                other_index++;
            }

            if (bitrot_enabled) {
                ret = glusterd_add_node_to_dict(priv->bitd_svc.name, rsp_dict,
                                                other_index, vol_opts);
                if (ret)
                    goto out;
                other_count++;
                node_count++;
                other_index++;
                /* For handling scrub status. Scrub daemon will be
                 * running automatically when bitrot is enable */
                ret = glusterd_add_node_to_dict(priv->scrub_svc.name, rsp_dict,
                                                other_index, vol_opts);
                if (ret)
                    goto out;
                other_count++;
                node_count++;
            }
        }
    }

    ret = dict_set_int32n(rsp_dict, "type", SLEN("type"), volinfo->type);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=type", NULL);
        goto out;
    }

    ret = dict_set_int32n(rsp_dict, "brick-index-max", SLEN("brick-index-max"),
                          brick_index);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                "Key=brick-index-max", NULL);
        goto out;
    }
    ret = dict_set_int32n(rsp_dict, "other-count", SLEN("other-count"),
                          other_count);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                "Key=other-count", NULL);
        goto out;
    }
    ret = dict_set_int32n(rsp_dict, "count", SLEN("count"), node_count);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                "Key=count", NULL);
        goto out;
    }

    /* Active tasks */
    /* Tasks are added only for normal volume status request for either a
     * single volume or all volumes
     */
    if (!glusterd_status_has_tasks(cmd))
        goto out;

    ret = glusterd_aggregate_task_status(rsp_dict, volinfo);
    if (ret)
        goto out;
    ret = 0;

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
glusterd_op_ac_none(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;

    gf_msg_debug(THIS->name, 0, "Returning with %d", ret);

    return ret;
}

static int
glusterd_op_sm_locking_failed(uuid_t *txn_id)
{
    int ret = -1;

    opinfo.op_ret = -1;
    opinfo.op_errstr = gf_strdup("locking failed for one of the peer.");

    ret = glusterd_set_txn_opinfo(txn_id, &opinfo);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");
    /* Inject a reject event such that unlocking gets triggered right away*/
    ret = glusterd_op_sm_inject_event(GD_OP_EVENT_RCVD_RJT, txn_id, NULL);

    return ret;
}

static int
glusterd_op_ac_send_lock(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;
    rpc_clnt_procedure_t *proc = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;
    glusterd_peerinfo_t *peerinfo = NULL;
    uint32_t pending_count = 0;
    dict_t *dict = NULL;

    this = THIS;
    priv = this->private;
    GF_ASSERT(priv);

    RCU_READ_LOCK;
    cds_list_for_each_entry_rcu(peerinfo, &priv->peers, uuid_list)
    {
        /* Only send requests to peers who were available before the
         * transaction started
         */
        if (peerinfo->generation > opinfo.txn_generation)
            continue;

        if (!peerinfo->connected || !peerinfo->mgmt)
            continue;
        if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
            (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
            continue;

        /* Based on the op_version, acquire a cluster or mgmt_v3 lock */
        if (priv->op_version < GD_OP_VERSION_3_6_0) {
            proc = &peerinfo->mgmt->proctable[GLUSTERD_MGMT_CLUSTER_LOCK];
            if (proc->fn) {
                ret = proc->fn(NULL, this, peerinfo);
                if (ret) {
                    RCU_READ_UNLOCK;
                    gf_msg(this->name, GF_LOG_WARNING, 0,
                           GD_MSG_LOCK_REQ_SEND_FAIL,
                           "Failed to send lock request "
                           "for operation 'Volume %s' to "
                           "peer %s",
                           gd_op_list[opinfo.op], peerinfo->hostname);
                    goto out;
                }
                /* Mark the peer as locked*/
                peerinfo->locked = _gf_true;
                pending_count++;
            }
        } else {
            dict = glusterd_op_get_ctx();
            dict_ref(dict);

            proc = &peerinfo->mgmt_v3->proctable[GLUSTERD_MGMT_V3_LOCK];
            if (proc->fn) {
                ret = dict_set_static_ptr(dict, "peerinfo", peerinfo);
                if (ret) {
                    RCU_READ_UNLOCK;
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                           "failed to set peerinfo");
                    dict_unref(dict);
                    goto out;
                }

                ret = proc->fn(NULL, this, dict);
                if (ret) {
                    RCU_READ_UNLOCK;
                    gf_msg(this->name, GF_LOG_WARNING, 0,
                           GD_MSG_MGMTV3_LOCK_REQ_SEND_FAIL,
                           "Failed to send mgmt_v3 lock "
                           "request for operation "
                           "'Volume %s' to peer %s",
                           gd_op_list[opinfo.op], peerinfo->hostname);
                    dict_unref(dict);
                    goto out;
                }
                /* Mark the peer as locked*/
                peerinfo->locked = _gf_true;
                pending_count++;
            }
        }
    }
    RCU_READ_UNLOCK;

    opinfo.pending_count = pending_count;

    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    if (!opinfo.pending_count)
        ret = glusterd_op_sm_inject_all_acc(&event->txn_id);

out:
    if (ret)
        ret = glusterd_op_sm_locking_failed(&event->txn_id);

    gf_msg_debug(this->name, 0, "Returning with %d", ret);
    return ret;
}

static int
glusterd_op_ac_send_unlock(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;
    rpc_clnt_procedure_t *proc = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;
    glusterd_peerinfo_t *peerinfo = NULL;
    uint32_t pending_count = 0;
    dict_t *dict = NULL;

    this = THIS;
    priv = this->private;
    GF_ASSERT(priv);

    RCU_READ_LOCK;
    cds_list_for_each_entry_rcu(peerinfo, &priv->peers, uuid_list)
    {
        /* Only send requests to peers who were available before the
         * transaction started
         */
        if (peerinfo->generation > opinfo.txn_generation)
            continue;

        if (!peerinfo->connected || !peerinfo->mgmt || !peerinfo->locked)
            continue;
        if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
            (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
            continue;
        /* Based on the op_version,
         * release the cluster or mgmt_v3 lock */
        if (priv->op_version < GD_OP_VERSION_3_6_0) {
            proc = &peerinfo->mgmt->proctable[GLUSTERD_MGMT_CLUSTER_UNLOCK];
            if (proc->fn) {
                ret = proc->fn(NULL, this, peerinfo);
                if (ret) {
                    opinfo.op_errstr = gf_strdup(
                        "Unlocking failed for one of "
                        "the peer.");
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_CLUSTER_UNLOCK_FAILED,
                           "Unlocking failed for operation"
                           " volume %s on peer %s",
                           gd_op_list[opinfo.op], peerinfo->hostname);
                    continue;
                }
                pending_count++;
                peerinfo->locked = _gf_false;
            }
        } else {
            dict = glusterd_op_get_ctx();
            dict_ref(dict);

            proc = &peerinfo->mgmt_v3->proctable[GLUSTERD_MGMT_V3_UNLOCK];
            if (proc->fn) {
                ret = dict_set_static_ptr(dict, "peerinfo", peerinfo);
                if (ret) {
                    opinfo.op_errstr = gf_strdup(
                        "Unlocking failed for one of the "
                        "peer.");
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_CLUSTER_UNLOCK_FAILED,
                           "Unlocking failed for operation"
                           " volume %s on peer %s",
                           gd_op_list[opinfo.op], peerinfo->hostname);
                    dict_unref(dict);
                    continue;
                }

                ret = proc->fn(NULL, this, dict);
                if (ret) {
                    opinfo.op_errstr = gf_strdup(
                        "Unlocking failed for one of the "
                        "peer.");
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_CLUSTER_UNLOCK_FAILED,
                           "Unlocking failed for operation"
                           " volume %s on peer %s",
                           gd_op_list[opinfo.op], peerinfo->hostname);
                    dict_unref(dict);
                    continue;
                }
                pending_count++;
                peerinfo->locked = _gf_false;
            }
        }
    }
    RCU_READ_UNLOCK;

    opinfo.pending_count = pending_count;

    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    if (!opinfo.pending_count)
        ret = glusterd_op_sm_inject_all_acc(&event->txn_id);

    gf_msg_debug(this->name, 0, "Returning with %d", ret);
    return ret;
}

static int
glusterd_op_ac_ack_drain(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;

    if (opinfo.pending_count > 0)
        opinfo.pending_count--;

    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    if (!opinfo.pending_count)
        ret = glusterd_op_sm_inject_event(GD_OP_EVENT_ALL_ACK, &event->txn_id,
                                          NULL);

    gf_msg_debug(THIS->name, 0, "Returning with %d", ret);

    return ret;
}

static int
glusterd_op_ac_send_unlock_drain(glusterd_op_sm_event_t *event, void *ctx)
{
    return glusterd_op_ac_ack_drain(event, ctx);
}

static int
glusterd_op_ac_lock(glusterd_op_sm_event_t *event, void *ctx)
{
    int32_t ret = 0;
    char *volname = NULL;
    char *globalname = NULL;
    glusterd_op_lock_ctx_t *lock_ctx = NULL;
    xlator_t *this = NULL;
    uint32_t op_errno = 0;
    glusterd_conf_t *conf = NULL;
    uint32_t timeout = 0;

    GF_ASSERT(event);
    GF_ASSERT(ctx);

    this = THIS;
    GF_ASSERT(this);
    conf = this->private;
    GF_ASSERT(conf);

    lock_ctx = (glusterd_op_lock_ctx_t *)ctx;

    /* If the req came from a node running on older op_version
     * the dict won't be present. Based on it acquiring a cluster
     * or mgmt_v3 lock */
    if (lock_ctx->dict == NULL) {
        ret = glusterd_lock(lock_ctx->uuid);
        glusterd_op_lock_send_resp(lock_ctx->req, ret);
    } else {
        /* Cli will add timeout key to dict if the default timeout is
         * other than 2 minutes. Here we use this value to check whether
         * mgmt_v3_lock_timeout should be set to default value or we
         * need to change the value according to timeout value
         * i.e, timeout + 120 seconds. */
        ret = dict_get_uint32(lock_ctx->dict, "timeout", &timeout);
        if (!ret)
            conf->mgmt_v3_lock_timeout = timeout + 120;

        ret = dict_get_strn(lock_ctx->dict, "volname", SLEN("volname"),
                            &volname);
        if (ret)
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to acquire volname");
        else {
            ret = glusterd_mgmt_v3_lock(volname, lock_ctx->uuid, &op_errno,
                                        "vol");
            if (ret)
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MGMTV3_LOCK_GET_FAIL,
                       "Unable to acquire lock for %s", volname);
            goto out;
        }
        ret = dict_get_strn(lock_ctx->dict, "globalname", SLEN("globalname"),
                            &globalname);
        if (!ret) {
            ret = glusterd_mgmt_v3_lock(globalname, lock_ctx->uuid, &op_errno,
                                        "global");
            if (ret)
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MGMTV3_LOCK_GET_FAIL,
                       "Unable to acquire lock for %s", globalname);
        }
    out:
        glusterd_op_mgmt_v3_lock_send_resp(lock_ctx->req, &event->txn_id, ret);

        dict_unref(lock_ctx->dict);
    }

    gf_msg_debug(THIS->name, 0, "Lock Returned %d", ret);
    return ret;
}

static int
glusterd_op_ac_unlock(glusterd_op_sm_event_t *event, void *ctx)
{
    int32_t ret = 0;
    char *volname = NULL;
    char *globalname = NULL;
    glusterd_op_lock_ctx_t *lock_ctx = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;

    GF_ASSERT(event);
    GF_ASSERT(ctx);

    this = THIS;
    priv = this->private;

    lock_ctx = (glusterd_op_lock_ctx_t *)ctx;

    /* If the req came from a node running on older op_version
     * the dict won't be present. Based on it releasing the cluster
     * or mgmt_v3 lock */
    if (lock_ctx->dict == NULL) {
        ret = glusterd_unlock(lock_ctx->uuid);
        glusterd_op_unlock_send_resp(lock_ctx->req, ret);
    } else {
        ret = dict_get_strn(lock_ctx->dict, "volname", SLEN("volname"),
                            &volname);
        if (ret)
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to acquire volname");
        else {
            ret = glusterd_mgmt_v3_unlock(volname, lock_ctx->uuid, "vol");
            if (ret)
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MGMTV3_UNLOCK_FAIL,
                       "Unable to release lock for %s", volname);
            goto out;
        }

        ret = dict_get_strn(lock_ctx->dict, "globalname", SLEN("globalname"),
                            &globalname);
        if (!ret) {
            ret = glusterd_mgmt_v3_unlock(globalname, lock_ctx->uuid, "global");
            if (ret)
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MGMTV3_UNLOCK_FAIL,
                       "Unable to release lock for %s", globalname);
        }
    out:
        glusterd_op_mgmt_v3_unlock_send_resp(lock_ctx->req, &event->txn_id,
                                             ret);

        dict_unref(lock_ctx->dict);
    }

    gf_msg_debug(this->name, 0, "Unlock Returned %d", ret);

    if (priv->pending_quorum_action)
        glusterd_do_quorum_action();
    return ret;
}

static int
glusterd_op_ac_local_unlock(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;
    uuid_t *originator = NULL;

    GF_ASSERT(event);
    GF_ASSERT(ctx);

    originator = (uuid_t *)ctx;

    ret = glusterd_unlock(*originator);

    gf_msg_debug(THIS->name, 0, "Unlock Returned %d", ret);

    return ret;
}

static int
glusterd_op_ac_rcvd_lock_acc(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;

    GF_ASSERT(event);

    if (opinfo.pending_count > 0)
        opinfo.pending_count--;

    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    if (opinfo.pending_count > 0)
        goto out;

    ret = glusterd_op_sm_inject_event(GD_OP_EVENT_ALL_ACC, &event->txn_id,
                                      NULL);

    gf_msg_debug(THIS->name, 0, "Returning %d", ret);

out:
    return ret;
}

int
glusterd_dict_set_volid(dict_t *dict, char *volname, char **op_errstr)
{
    int ret = -1;
    glusterd_volinfo_t *volinfo = NULL;
    char *volid = NULL;
    char msg[1024] = {
        0,
    };
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    if (!dict || !volname) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), FMTSTR_CHECK_VOL_EXISTS, volname);
        goto out;
    }
    volid = gf_strdup(uuid_utoa(volinfo->volume_id));
    if (!volid) {
        ret = -1;
        goto out;
    }
    ret = dict_set_dynstrn(dict, "vol-id", SLEN("vol-id"), volid);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Failed to set volume id of volume"
                 " %s",
                 volname);
        GF_FREE(volid);
        goto out;
    }
out:
    if (msg[0] != '\0') {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_ID_SET_FAIL, "%s", msg);
        *op_errstr = gf_strdup(msg);
    }
    return ret;
}

int
gd_set_commit_hash(dict_t *dict)
{
    struct timeval tv;
    uint32_t hash;

    /*
     * We need a commit hash that won't conflict with others we might have
     * set, or zero which is the implicit value if we never have.  Using
     * seconds<<3 like this ensures that we'll only get a collision if two
     * consecutive rebalances are separated by exactly 2^29 seconds - about
     * 17 years - and even then there's only a 1/8 chance of a collision in
     * the low order bits.  It's far more likely that this code will have
     * changed completely by then.  If not, call me in 2031.
     *
     * P.S. Time zone changes?  Yeah, right.
     */
    gettimeofday(&tv, NULL);
    hash = tv.tv_sec << 3;

    /*
     * Make sure at least one of those low-order bits is set.  The extra
     * shifting is because not all machines have sub-millisecond time
     * resolution.
     */
    hash |= 1 << ((tv.tv_usec >> 10) % 3);

    return dict_set_uint32(dict, "commit-hash", hash);
}

int
glusterd_op_build_payload(dict_t **req, char **op_errstr, dict_t *op_ctx)
{
    int ret = -1;
    void *ctx = NULL;
    dict_t *dict = NULL;
    dict_t *req_dict = NULL;
    glusterd_op_t op = GD_OP_NONE;
    char *volname = NULL;
    uint32_t status_cmd = GF_CLI_STATUS_NONE;
    xlator_t *this = NULL;
    gf_boolean_t do_common = _gf_false;

    GF_ASSERT(req);

    this = THIS;
    GF_ASSERT(this);

    req_dict = dict_new();
    if (!req_dict)
        goto out;

    if (!op_ctx) {
        op = glusterd_op_get_op();
        ctx = (void *)glusterd_op_get_ctx();
        if (!ctx) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_NO_OPTIONS_GIVEN,
                   "Null Context for "
                   "op %d",
                   op);
            ret = -1;
            goto out;
        }

    } else {
#define GD_SYNC_OPCODE_KEY "sync-mgmt-operation"
        ret = dict_get_int32(op_ctx, GD_SYNC_OPCODE_KEY, (int32_t *)&op);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get volume"
                   " operation");
            goto out;
        }
        ctx = op_ctx;
#undef GD_SYNC_OPCODE_KEY
    }

    dict = ctx;
    switch (op) {
        case GD_OP_CREATE_VOLUME: {
            ++glusterfs_port;
            ret = dict_set_int32n(dict, "port", SLEN("port"), glusterfs_port);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set port in "
                       "dictionary");
                goto out;
            }
            dict_copy(dict, req_dict);
        } break;

        case GD_OP_GSYNC_CREATE:
        case GD_OP_GSYNC_SET: {
            ret = glusterd_op_gsync_args_get(dict, op_errstr, &volname, NULL,
                                             NULL);
            if (ret == 0) {
                ret = glusterd_dict_set_volid(dict, volname, op_errstr);
                if (ret)
                    goto out;
            }
            dict_copy(dict, req_dict);
        } break;

        case GD_OP_SET_VOLUME: {
            ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
            if (ret) {
                gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_DICT_GET_FAILED,
                       "volname is not present in "
                       "operation ctx");
                goto out;
            }
            if (strcmp(volname, "help") && strcmp(volname, "help-xml") &&
                strcasecmp(volname, "all")) {
                ret = glusterd_dict_set_volid(dict, volname, op_errstr);
                if (ret)
                    goto out;
            }
            dict_unref(req_dict);
            req_dict = dict_ref(dict);
        } break;

        case GD_OP_REMOVE_BRICK: {
            dict_t *dict = ctx;
            ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
            if (ret) {
                gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_DICT_GET_FAILED,
                       "volname is not present in "
                       "operation ctx");
                goto out;
            }

            ret = glusterd_dict_set_volid(dict, volname, op_errstr);
            if (ret)
                goto out;

            if (gd_set_commit_hash(dict) != 0) {
                goto out;
            }

            dict_unref(req_dict);
            req_dict = dict_ref(dict);
        } break;

        case GD_OP_STATUS_VOLUME: {
            ret = dict_get_uint32(dict, "cmd", &status_cmd);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Status command not present "
                       "in op ctx");
                goto out;
            }
            if (GF_CLI_STATUS_ALL & status_cmd) {
                dict_copy(dict, req_dict);
                break;
            }
            do_common = _gf_true;
        } break;

        case GD_OP_DELETE_VOLUME:
        case GD_OP_START_VOLUME:
        case GD_OP_STOP_VOLUME:
        case GD_OP_ADD_BRICK:
        case GD_OP_REPLACE_BRICK:
        case GD_OP_RESET_VOLUME:
        case GD_OP_LOG_ROTATE:
        case GD_OP_QUOTA:
        case GD_OP_PROFILE_VOLUME:
        case GD_OP_HEAL_VOLUME:
        case GD_OP_STATEDUMP_VOLUME:
        case GD_OP_CLEARLOCKS_VOLUME:
        case GD_OP_DEFRAG_BRICK_VOLUME:
        case GD_OP_BARRIER:
        case GD_OP_BITROT:
        case GD_OP_SCRUB_STATUS:
        case GD_OP_SCRUB_ONDEMAND:
        case GD_OP_RESET_BRICK: {
            do_common = _gf_true;
        } break;

        case GD_OP_REBALANCE: {
            if (gd_set_commit_hash(dict) != 0) {
                goto out;
            }
            do_common = _gf_true;
        } break;

        case GD_OP_SYNC_VOLUME:
        case GD_OP_COPY_FILE:
        case GD_OP_SYS_EXEC:
        case GD_OP_GANESHA: {
            dict_copy(dict, req_dict);
        } break;

        default:
            break;
    }

    /*
     * This has been moved out of the switch so that multiple ops with
     * other special needs can all "fall through" to it.
     */
    if (do_common) {
        ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
        if (ret) {
            gf_msg(this->name, GF_LOG_CRITICAL, -ret, GD_MSG_DICT_GET_FAILED,
                   "volname is not present in "
                   "operation ctx");
            goto out;
        }

        if (strcasecmp(volname, "all")) {
            ret = glusterd_dict_set_volid(dict, volname, op_errstr);
            if (ret)
                goto out;
        }
        dict_copy(dict, req_dict);
    }

    *req = req_dict;
    ret = 0;

out:
    return ret;
}

static int
glusterd_op_ac_send_stage_op(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;
    int ret1 = 0;
    rpc_clnt_procedure_t *proc = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;
    glusterd_peerinfo_t *peerinfo = NULL;
    dict_t *dict = NULL;
    dict_t *rsp_dict = NULL;
    char *op_errstr = NULL;
    glusterd_op_t op = GD_OP_NONE;
    uint32_t pending_count = 0;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    op = glusterd_op_get_op();

    rsp_dict = dict_new();
    if (!rsp_dict) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_DICT_CREATE_FAIL,
               "Failed to create rsp_dict");
        ret = -1;
        goto out;
    }

    ret = glusterd_op_build_payload(&dict, &op_errstr, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_OP_PAYLOAD_BUILD_FAIL,
               LOGSTR_BUILD_PAYLOAD, gd_op_list[op]);
        if (op_errstr == NULL)
            gf_asprintf(&op_errstr, OPERRSTR_BUILD_PAYLOAD);
        opinfo.op_errstr = op_errstr;
        goto out;
    }

    ret = glusterd_validate_quorum(this, op, dict, &op_errstr);
    if (ret) {
        gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_SERVER_QUORUM_NOT_MET,
               "Server quorum not met. Rejecting operation.");
        opinfo.op_errstr = op_errstr;
        goto out;
    }

    ret = glusterd_op_stage_validate(op, dict, &op_errstr, rsp_dict);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VALIDATE_FAILED,
               LOGSTR_STAGE_FAIL, gd_op_list[op], "localhost",
               (op_errstr) ? ":" : " ", (op_errstr) ? op_errstr : " ");
        if (op_errstr == NULL)
            gf_asprintf(&op_errstr, OPERRSTR_STAGE_FAIL, "localhost");
        opinfo.op_errstr = op_errstr;
        goto out;
    }

    RCU_READ_LOCK;
    cds_list_for_each_entry_rcu(peerinfo, &priv->peers, uuid_list)
    {
        /* Only send requests to peers who were available before the
         * transaction started
         */
        if (peerinfo->generation > opinfo.txn_generation)
            continue;

        if (!peerinfo->connected || !peerinfo->mgmt)
            continue;
        if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
            (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
            continue;

        proc = &peerinfo->mgmt->proctable[GLUSTERD_MGMT_STAGE_OP];
        GF_ASSERT(proc);
        if (proc->fn) {
            ret = dict_set_static_ptr(dict, "peerinfo", peerinfo);
            if (ret) {
                RCU_READ_UNLOCK;
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "failed to "
                       "set peerinfo");
                goto out;
            }

            ret = proc->fn(NULL, this, dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0,
                       GD_MSG_STAGE_REQ_SEND_FAIL,
                       "Failed to "
                       "send stage request for operation "
                       "'Volume %s' to peer %s",
                       gd_op_list[op], peerinfo->hostname);
                continue;
            }
            pending_count++;
        }
    }
    RCU_READ_UNLOCK;

    opinfo.pending_count = pending_count;
out:
    if (ret)
        opinfo.op_ret = ret;

    ret1 = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret1)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    if (rsp_dict)
        dict_unref(rsp_dict);

    if (dict)
        dict_unref(dict);
    if (ret) {
        glusterd_op_sm_inject_event(GD_OP_EVENT_RCVD_RJT, &event->txn_id, NULL);
        opinfo.op_ret = ret;
    }

    gf_msg_debug(this->name, 0,
                 "Sent stage op request for "
                 "'Volume %s' to %d peers",
                 gd_op_list[op], opinfo.pending_count);

    if (!opinfo.pending_count)
        ret = glusterd_op_sm_inject_all_acc(&event->txn_id);

    gf_msg_debug(this->name, 0, "Returning with %d", ret);

    return ret;
}

/* This function takes a dict and converts the uuid values of key specified
 * into hostnames
 */
static int
glusterd_op_volume_dict_uuid_to_hostname(dict_t *dict, const char *key_fmt,
                                         int idx_min, int idx_max)
{
    int ret = -1;
    int i = 0;
    char key[128];
    int keylen;
    char *uuid_str = NULL;
    uuid_t uuid = {
        0,
    };
    char *hostname = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    GF_ASSERT(dict);
    GF_ASSERT(key_fmt);

    for (i = idx_min; i < idx_max; i++) {
        keylen = snprintf(key, sizeof(key), key_fmt, i);
        ret = dict_get_strn(dict, key, keylen, &uuid_str);
        if (ret) {
            ret = 0;
            continue;
        }

        gf_msg_debug(this->name, 0, "Got uuid %s", uuid_str);

        ret = gf_uuid_parse(uuid_str, uuid);
        /* if parsing fails don't error out
         * let the original value be retained
         */
        if (ret) {
            ret = 0;
            continue;
        }

        hostname = glusterd_uuid_to_hostname(uuid);
        if (hostname) {
            gf_msg_debug(this->name, 0, "%s -> %s", uuid_str, hostname);
            ret = dict_set_dynstrn(dict, key, keylen, hostname);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Error setting hostname %s to dict", hostname);
                GF_FREE(hostname);
                goto out;
            }
        }
    }

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
reassign_defrag_status(dict_t *dict, char *key, int keylen,
                       gf_defrag_status_t *status)
{
    int ret = 0;

    if (!*status)
        return ret;

    switch (*status) {
        case GF_DEFRAG_STATUS_STARTED:
            *status = GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED;
            break;

        case GF_DEFRAG_STATUS_STOPPED:
            *status = GF_DEFRAG_STATUS_LAYOUT_FIX_STOPPED;
            break;

        case GF_DEFRAG_STATUS_COMPLETE:
            *status = GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE;
            break;

        case GF_DEFRAG_STATUS_FAILED:
            *status = GF_DEFRAG_STATUS_LAYOUT_FIX_FAILED;
            break;
        default:
            break;
    }

    ret = dict_set_int32n(dict, key, keylen, *status);
    if (ret)
        gf_msg(THIS->name, GF_LOG_WARNING, 0, GD_MSG_DICT_SET_FAILED,
               "failed to reset defrag %s in dict", key);

    return ret;
}

/* Check and reassign the defrag_status enum got from the rebalance process
 * of all peers so that the rebalance-status CLI command can display if a
 * full-rebalance or just a fix-layout was carried out.
 */
static int
glusterd_op_check_peer_defrag_status(dict_t *dict, int count)
{
    glusterd_volinfo_t *volinfo = NULL;
    gf_defrag_status_t status = GF_DEFRAG_STATUS_NOT_STARTED;
    char key[64] = {
        0,
    };
    int keylen;
    char *volname = NULL;
    int ret = -1;
    int i = 1;

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_WARNING, 0, GD_MSG_VOL_NOT_FOUND,
               FMTSTR_CHECK_VOL_EXISTS, volname);
        goto out;
    }

    if (volinfo->rebal.defrag_cmd != GF_DEFRAG_CMD_START_LAYOUT_FIX) {
        /* Fix layout was not issued; we don't need to reassign
           the status */
        ret = 0;
        goto out;
    }

    do {
        keylen = snprintf(key, sizeof(key), "status-%d", i);
        ret = dict_get_int32n(dict, key, keylen, (int32_t *)&status);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                   "failed to get defrag %s", key);
            goto out;
        }
        ret = reassign_defrag_status(dict, key, keylen, &status);
        if (ret)
            goto out;
        i++;
    } while (i <= count);

    ret = 0;
out:
    return ret;
}

/* This function is used to verify if op_ctx indeed
   requires modification. This is necessary since the
   dictionary for certain commands might not have the
   necessary keys required for the op_ctx modification
   to succeed.

   Special Cases:
   - volume status all
   - volume status

   Regular Cases:
   - volume status <volname> <brick>
   - volume status <volname> mem
   - volume status <volname> clients
   - volume status <volname> inode
   - volume status <volname> fd
   - volume status <volname> callpool
   - volume status <volname> tasks
*/

static gf_boolean_t
glusterd_is_volume_status_modify_op_ctx(uint32_t cmd)
{
    if ((cmd & GF_CLI_STATUS_MASK) == GF_CLI_STATUS_NONE) {
        if (cmd & GF_CLI_STATUS_BRICK)
            return _gf_false;
        if (cmd & GF_CLI_STATUS_ALL)
            return _gf_false;
        return _gf_true;
    }
    return _gf_false;
}

int
glusterd_op_modify_port_key(dict_t *op_ctx, int brick_index_max)
{
    char *port = NULL;
    int i = 0;
    int ret = -1;
    char key[64] = {0};
    int keylen;
    char old_key[64] = {0};
    int old_keylen;

    for (i = 0; i <= brick_index_max; i++) {
        keylen = snprintf(key, sizeof(key), "brick%d.rdma_port", i);
        ret = dict_get_strn(op_ctx, key, keylen, &port);

        if (ret) {
            old_keylen = snprintf(old_key, sizeof(old_key), "brick%d.port", i);
            ret = dict_get_strn(op_ctx, old_key, old_keylen, &port);
            if (ret)
                goto out;

            ret = dict_set_strn(op_ctx, key, keylen, port);
            if (ret)
                goto out;
            ret = dict_set_nstrn(op_ctx, old_key, old_keylen, "\0", SLEN("\0"));
            if (ret)
                goto out;
        }
    }
out:
    return ret;
}

/* This function is used to modify the op_ctx dict before sending it back
 * to cli. This is useful in situations like changing the peer uuids to
 * hostnames etc.
 */
void
glusterd_op_modify_op_ctx(glusterd_op_t op, void *ctx)
{
    int ret = -1;
    dict_t *op_ctx = NULL;
    int brick_index_max = -1;
    int other_count = 0;
    int count = 0;
    uint32_t cmd = GF_CLI_STATUS_NONE;
    xlator_t *this = NULL;
    glusterd_conf_t *conf = NULL;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    char *port = 0;
    int i = 0;
    char key[64] = {
        0,
    };
    int keylen;

    this = THIS;
    GF_ASSERT(this);
    conf = this->private;

    if (ctx)
        op_ctx = ctx;
    else
        op_ctx = glusterd_op_get_ctx();

    if (!op_ctx) {
        gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_OPCTX_NULL,
               "Operation context is not present.");
        goto out;
    }

    switch (op) {
        case GD_OP_STATUS_VOLUME:
            ret = dict_get_uint32(op_ctx, "cmd", &cmd);
            if (ret) {
                gf_msg_debug(this->name, 0, "Failed to get status cmd");
                goto out;
            }

            if (!glusterd_is_volume_status_modify_op_ctx(cmd)) {
                gf_msg_debug(this->name, 0,
                             "op_ctx modification not required for status "
                             "operation being performed");
                goto out;
            }

            ret = dict_get_int32n(op_ctx, "brick-index-max",
                                  SLEN("brick-index-max"), &brick_index_max);
            if (ret) {
                gf_msg_debug(this->name, 0, "Failed to get brick-index-max");
                goto out;
            }

            ret = dict_get_int32n(op_ctx, "other-count", SLEN("other-count"),
                                  &other_count);
            if (ret) {
                gf_msg_debug(this->name, 0, "Failed to get other-count");
                goto out;
            }

            count = brick_index_max + other_count + 1;

            /*
             * a glusterd lesser than version 3.7 will be sending the
             * rdma port in older key. Changing that value from here
             * to support backward compatibility
             */
            ret = dict_get_strn(op_ctx, "volname", SLEN("volname"), &volname);
            if (ret)
                goto out;

            for (i = 0; i <= brick_index_max; i++) {
                keylen = snprintf(key, sizeof(key), "brick%d.rdma_port", i);
                ret = dict_get_strn(op_ctx, key, keylen, &port);
                if (ret) {
                    ret = dict_set_nstrn(op_ctx, key, keylen, "\0", SLEN("\0"));
                    if (ret)
                        goto out;
                }
            }
            ret = glusterd_volinfo_find(volname, &volinfo);
            if (ret)
                goto out;
            if (conf->op_version < GD_OP_VERSION_3_7_0 &&
                volinfo->transport_type == GF_TRANSPORT_RDMA) {
                ret = glusterd_op_modify_port_key(op_ctx, brick_index_max);
                if (ret)
                    goto out;
            }
            /* add 'brick%d.peerid' into op_ctx with value of 'brick%d.path'.
               nfs/sshd like services have this additional uuid */
            {
                char *uuid_str = NULL;
                char *uuid = NULL;
                int i;

                for (i = brick_index_max + 1; i < count; i++) {
                    keylen = snprintf(key, sizeof(key), "brick%d.path", i);
                    ret = dict_get_strn(op_ctx, key, keylen, &uuid_str);
                    if (!ret) {
                        keylen = snprintf(key, sizeof(key), "brick%d.peerid",
                                          i);
                        uuid = gf_strdup(uuid_str);
                        if (!uuid) {
                            gf_msg_debug(this->name, 0,
                                         "unable to create dup of"
                                         " uuid_str");
                            continue;
                        }
                        ret = dict_set_dynstrn(op_ctx, key, keylen, uuid);
                        if (ret != 0) {
                            GF_FREE(uuid);
                        }
                    }
                }
            }

            ret = glusterd_op_volume_dict_uuid_to_hostname(
                op_ctx, "brick%d.path", 0, count);
            if (ret)
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_CONVERSION_FAILED,
                       "Failed uuid to hostname conversion");

            break;

        case GD_OP_PROFILE_VOLUME:
            ret = dict_get_str_boolean(op_ctx, "nfs", _gf_false);
            if (!ret)
                goto out;

            ret = dict_get_int32n(op_ctx, "count", SLEN("count"), &count);
            if (ret) {
                gf_msg_debug(this->name, 0, "Failed to get brick count");
                goto out;
            }

            ret = glusterd_op_volume_dict_uuid_to_hostname(op_ctx, "%d-brick",
                                                           1, (count + 1));
            if (ret)
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_CONVERSION_FAILED,
                       "Failed uuid to hostname conversion");

            break;

        /* For both rebalance and remove-brick status, the glusterd op is the
         * same
         */
        case GD_OP_DEFRAG_BRICK_VOLUME:
        case GD_OP_SCRUB_STATUS:
        case GD_OP_SCRUB_ONDEMAND:
            ret = dict_get_int32n(op_ctx, "count", SLEN("count"), &count);
            if (ret) {
                gf_msg_debug(this->name, 0, "Failed to get count");
                goto out;
            }

            /* add 'node-name-%d' into op_ctx with value uuid_str.
               this will be used to convert to hostname later */
            {
                char *uuid_str = NULL;
                char *uuid = NULL;
                int i;

                for (i = 1; i <= count; i++) {
                    keylen = snprintf(key, sizeof(key), "node-uuid-%d", i);
                    ret = dict_get_strn(op_ctx, key, keylen, &uuid_str);
                    if (!ret) {
                        keylen = snprintf(key, sizeof(key), "node-name-%d", i);
                        uuid = gf_strdup(uuid_str);
                        if (!uuid) {
                            gf_msg_debug(this->name, 0,
                                         "unable to create dup of"
                                         " uuid_str");
                            continue;
                        }
                        ret = dict_set_dynstrn(op_ctx, key, keylen, uuid);
                        if (ret != 0) {
                            GF_FREE(uuid);
                        }
                    }
                }
            }

            ret = glusterd_op_volume_dict_uuid_to_hostname(
                op_ctx, "node-name-%d", 1, (count + 1));
            if (ret)
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_CONVERSION_FAILED,
                       "Failed uuid to hostname conversion");

            /* Since Both rebalance and bitrot scrub status/ondemand
             * are going to use same code path till here, we should
             * break in case of scrub status.
             */
            if (op == GD_OP_SCRUB_STATUS || op == GD_OP_SCRUB_ONDEMAND) {
                break;
            }

            ret = glusterd_op_check_peer_defrag_status(op_ctx, count);
            if (ret)
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_DEFRAG_STATUS_UPDATE_FAIL,
                       "Failed to reset defrag status for fix-layout");
            break;

        default:
            ret = 0;
            gf_msg_debug(this->name, 0, "op_ctx modification not required");
            break;
    }

out:
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_OPCTX_UPDATE_FAIL,
               "op_ctx modification failed");
    return;
}

int
glusterd_op_commit_hook(glusterd_op_t op, dict_t *op_ctx,
                        glusterd_commit_hook_type_t type)
{
    glusterd_conf_t *priv = NULL;
    char hookdir[PATH_MAX] = {
        0,
    };
    char scriptdir[PATH_MAX] = {
        0,
    };
    char *type_subdir = "";
    char *cmd_subdir = NULL;
    int ret = -1;
    int32_t len = 0;

    priv = THIS->private;
    switch (type) {
        case GD_COMMIT_HOOK_NONE:
        case GD_COMMIT_HOOK_MAX:
            /*Won't be called*/
            break;

        case GD_COMMIT_HOOK_PRE:
            type_subdir = "pre";
            break;
        case GD_COMMIT_HOOK_POST:
            type_subdir = "post";
            break;
    }

    cmd_subdir = glusterd_hooks_get_hooks_cmd_subdir(op);
    if (strlen(cmd_subdir) == 0)
        return -1;

    GLUSTERD_GET_HOOKS_DIR(hookdir, GLUSTERD_HOOK_VER, priv);
    len = snprintf(scriptdir, sizeof(scriptdir), "%s/%s/%s", hookdir,
                   cmd_subdir, type_subdir);
    if ((len < 0) || (len >= sizeof(scriptdir))) {
        return -1;
    }

    switch (type) {
        case GD_COMMIT_HOOK_NONE:
        case GD_COMMIT_HOOK_MAX:
            /*Won't be called*/
            break;

        case GD_COMMIT_HOOK_PRE:
            ret = glusterd_hooks_run_hooks(scriptdir, op, op_ctx, type);
            break;
        case GD_COMMIT_HOOK_POST:
            ret = glusterd_hooks_post_stub_enqueue(scriptdir, op, op_ctx);
            break;
    }

    return ret;
}

static int
glusterd_op_ac_send_commit_op(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;
    int ret1 = 0;
    rpc_clnt_procedure_t *proc = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;
    dict_t *dict = NULL;
    glusterd_peerinfo_t *peerinfo = NULL;
    char *op_errstr = NULL;
    glusterd_op_t op = GD_OP_NONE;
    uint32_t pending_count = 0;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    op = glusterd_op_get_op();

    ret = glusterd_op_build_payload(&dict, &op_errstr, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_OP_PAYLOAD_BUILD_FAIL,
               LOGSTR_BUILD_PAYLOAD, gd_op_list[op]);
        if (op_errstr == NULL)
            gf_asprintf(&op_errstr, OPERRSTR_BUILD_PAYLOAD);
        opinfo.op_errstr = op_errstr;
        goto out;
    }

    ret = glusterd_op_commit_perform(op, dict, &op_errstr,
                                     NULL);  // rsp_dict invalid for source
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_COMMIT_OP_FAIL,
               LOGSTR_COMMIT_FAIL, gd_op_list[op], "localhost",
               (op_errstr) ? ":" : " ", (op_errstr) ? op_errstr : " ");
        if (op_errstr == NULL)
            gf_asprintf(&op_errstr, OPERRSTR_COMMIT_FAIL, "localhost");
        opinfo.op_errstr = op_errstr;
        goto out;
    }

    RCU_READ_LOCK;
    cds_list_for_each_entry_rcu(peerinfo, &priv->peers, uuid_list)
    {
        /* Only send requests to peers who were available before the
         * transaction started
         */
        if (peerinfo->generation > opinfo.txn_generation)
            continue;

        if (!peerinfo->connected || !peerinfo->mgmt)
            continue;
        if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
            (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
            continue;

        proc = &peerinfo->mgmt->proctable[GLUSTERD_MGMT_COMMIT_OP];
        GF_ASSERT(proc);
        if (proc->fn) {
            ret = dict_set_static_ptr(dict, "peerinfo", peerinfo);
            if (ret) {
                RCU_READ_UNLOCK;
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "failed to set peerinfo");
                goto out;
            }
            ret = proc->fn(NULL, this, dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0,
                       GD_MSG_COMMIT_REQ_SEND_FAIL,
                       "Failed to "
                       "send commit request for operation "
                       "'Volume %s' to peer %s",
                       gd_op_list[op], peerinfo->hostname);
                continue;
            }
            pending_count++;
        }
    }
    RCU_READ_UNLOCK;

    opinfo.pending_count = pending_count;
    gf_msg_debug(this->name, 0,
                 "Sent commit op req for 'Volume %s' "
                 "to %d peers",
                 gd_op_list[op], opinfo.pending_count);
out:
    if (dict)
        dict_unref(dict);

    if (ret)
        opinfo.op_ret = ret;

    ret1 = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret1)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    if (ret) {
        glusterd_op_sm_inject_event(GD_OP_EVENT_RCVD_RJT, &event->txn_id, NULL);
        opinfo.op_ret = ret;
    }

    if (!opinfo.pending_count) {
        if (op == GD_OP_REPLACE_BRICK) {
            ret = glusterd_op_sm_inject_all_acc(&event->txn_id);
        } else {
            glusterd_op_modify_op_ctx(op, NULL);
            ret = glusterd_op_sm_inject_all_acc(&event->txn_id);
        }
        goto err;
    }

err:
    gf_msg_debug(this->name, 0, "Returning with %d", ret);

    return ret;
}

static int
glusterd_op_ac_rcvd_stage_op_acc(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;

    GF_ASSERT(event);

    if (opinfo.pending_count > 0)
        opinfo.pending_count--;

    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    if (opinfo.pending_count > 0)
        goto out;

    ret = glusterd_op_sm_inject_event(GD_OP_EVENT_STAGE_ACC, &event->txn_id,
                                      NULL);

out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);

    return ret;
}

static int
glusterd_op_ac_stage_op_failed(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;

    GF_ASSERT(event);

    if (opinfo.pending_count > 0)
        opinfo.pending_count--;

    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    if (opinfo.pending_count > 0)
        goto out;

    ret = glusterd_op_sm_inject_event(GD_OP_EVENT_ALL_ACK, &event->txn_id,
                                      NULL);

out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);

    return ret;
}

static int
glusterd_op_ac_commit_op_failed(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;

    GF_ASSERT(event);

    if (opinfo.pending_count > 0)
        opinfo.pending_count--;

    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    if (opinfo.pending_count > 0)
        goto out;

    ret = glusterd_op_sm_inject_event(GD_OP_EVENT_ALL_ACK, &event->txn_id,
                                      NULL);

out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);

    return ret;
}

static int
glusterd_op_ac_brick_op_failed(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;
    glusterd_op_brick_rsp_ctx_t *ev_ctx = NULL;
    gf_boolean_t free_errstr = _gf_false;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    GF_ASSERT(event);
    GF_ASSERT(ctx);
    ev_ctx = ctx;

    ret = glusterd_remove_pending_entry(&opinfo.pending_bricks,
                                        ev_ctx->pending_node->node);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UNKNOWN_RESPONSE,
               "unknown response received ");
        ret = -1;
        free_errstr = _gf_true;
        goto out;
    }
    if (opinfo.brick_pending_count > 0)
        opinfo.brick_pending_count--;
    if (opinfo.op_ret == 0)
        opinfo.op_ret = ev_ctx->op_ret;

    if (opinfo.op_errstr == NULL)
        opinfo.op_errstr = ev_ctx->op_errstr;
    else
        free_errstr = _gf_true;

    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    if (opinfo.brick_pending_count > 0)
        goto out;

    ret = glusterd_op_sm_inject_event(GD_OP_EVENT_ALL_ACK, &event->txn_id,
                                      ev_ctx->commit_ctx);

out:
    if (ev_ctx->rsp_dict)
        dict_unref(ev_ctx->rsp_dict);
    if (free_errstr && ev_ctx->op_errstr)
        GF_FREE(ev_ctx->op_errstr);
    GF_FREE(ctx);
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
glusterd_op_ac_rcvd_commit_op_acc(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;
    gf_boolean_t commit_ack_inject = _gf_true;
    glusterd_op_t op = GD_OP_NONE;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    op = glusterd_op_get_op();
    GF_ASSERT(event);

    if (opinfo.pending_count > 0)
        opinfo.pending_count--;

    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    if (opinfo.pending_count > 0)
        goto out;

    if (op == GD_OP_REPLACE_BRICK) {
        ret = glusterd_op_sm_inject_all_acc(&event->txn_id);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RBOP_START_FAIL,
                   "Couldn't start "
                   "replace-brick operation.");
            goto out;
        }

        commit_ack_inject = _gf_false;
        goto out;
    }

out:
    if (commit_ack_inject) {
        if (ret)
            ret = glusterd_op_sm_inject_event(GD_OP_EVENT_RCVD_RJT,
                                              &event->txn_id, NULL);
        else if (!opinfo.pending_count) {
            glusterd_op_modify_op_ctx(op, NULL);
            ret = glusterd_op_sm_inject_event(GD_OP_EVENT_COMMIT_ACC,
                                              &event->txn_id, NULL);
        }
        /*else do nothing*/
    }

    return ret;
}

static int
glusterd_op_ac_rcvd_unlock_acc(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;

    GF_ASSERT(event);

    if (opinfo.pending_count > 0)
        opinfo.pending_count--;

    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    if (opinfo.pending_count > 0)
        goto out;

    ret = glusterd_op_sm_inject_event(GD_OP_EVENT_ALL_ACC, &event->txn_id,
                                      NULL);

    gf_msg_debug(THIS->name, 0, "Returning %d", ret);

out:
    return ret;
}

int32_t
glusterd_op_clear_errstr()
{
    opinfo.op_errstr = NULL;
    return 0;
}

int32_t
glusterd_op_set_ctx(void *ctx)
{
    opinfo.op_ctx = ctx;

    return 0;
}

int32_t
glusterd_op_reset_ctx()
{
    glusterd_op_set_ctx(NULL);

    return 0;
}

int32_t
glusterd_op_txn_complete(uuid_t *txn_id)
{
    int32_t ret = -1;
    glusterd_conf_t *priv = NULL;
    int32_t op = -1;
    int32_t op_ret = 0;
    int32_t op_errno = 0;
    rpcsvc_request_t *req = NULL;
    void *ctx = NULL;
    char *op_errstr = NULL;
    char *volname = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    priv = this->private;
    GF_ASSERT(priv);

    op = glusterd_op_get_op();
    ctx = glusterd_op_get_ctx();
    op_ret = opinfo.op_ret;
    op_errno = opinfo.op_errno;
    req = opinfo.req;
    if (opinfo.op_errstr)
        op_errstr = opinfo.op_errstr;

    opinfo.op_ret = 0;
    opinfo.op_errno = 0;
    glusterd_op_clear_op();
    glusterd_op_reset_ctx();
    glusterd_op_clear_errstr();

    /* Based on the op-version, we release the cluster or mgmt_v3 lock */
    if (priv->op_version < GD_OP_VERSION_3_6_0) {
        ret = glusterd_unlock(MY_UUID);
        /* unlock can't/shouldn't fail here!! */
        if (ret)
            gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_GLUSTERD_UNLOCK_FAIL,
                   "Unable to clear local lock, ret: %d", ret);
        else
            gf_msg_debug(this->name, 0, "Cleared local lock");
    } else {
        ret = dict_get_strn(ctx, "volname", SLEN("volname"), &volname);
        if (ret)
            gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
                   "No Volume name present. "
                   "Locks have not been held.");

        if (volname) {
            ret = glusterd_mgmt_v3_unlock(volname, MY_UUID, "vol");
            if (ret)
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MGMTV3_UNLOCK_FAIL,
                       "Unable to release lock for %s", volname);
        }
    }

    ret = glusterd_op_send_cli_response(op, op_ret, op_errno, req, ctx,
                                        op_errstr);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_NO_CLI_RESP,
               "Responding to cli failed, "
               "ret: %d",
               ret);
        // Ignore this error, else state machine blocks
        ret = 0;
    }

    if (op_errstr && (strcmp(op_errstr, "")))
        GF_FREE(op_errstr);

    if (priv->pending_quorum_action)
        glusterd_do_quorum_action();

    /* Clearing the transaction opinfo */
    ret = glusterd_clear_txn_opinfo(txn_id);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_CLEAR_FAIL,
               "Unable to clear transaction's opinfo");

    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_op_ac_unlocked_all(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;

    GF_ASSERT(event);

    ret = glusterd_op_txn_complete(&event->txn_id);

    gf_msg_debug(THIS->name, 0, "Returning %d", ret);

    return ret;
}

static int
glusterd_op_ac_stage_op(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = -1;
    glusterd_req_ctx_t *req_ctx = NULL;
    int32_t status = 0;
    dict_t *rsp_dict = NULL;
    char *op_errstr = NULL;
    dict_t *dict = NULL;
    xlator_t *this = NULL;
    uuid_t *txn_id = NULL;
    glusterd_op_info_t txn_op_info = {
        {0},
    };
    glusterd_conf_t *priv = NULL;

    this = THIS;
    GF_ASSERT(this);

    priv = this->private;
    GF_ASSERT(priv);

    GF_ASSERT(ctx);

    req_ctx = ctx;

    dict = req_ctx->dict;

    rsp_dict = dict_new();
    if (!rsp_dict) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_DICT_CREATE_FAIL,
               "Failed to get new dictionary");
        return -1;
    }

    status = glusterd_op_stage_validate(req_ctx->op, dict, &op_errstr,
                                        rsp_dict);

    if (status) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VALIDATE_FAILED,
               "Stage failed on operation"
               " 'Volume %s', Status : %d",
               gd_op_list[req_ctx->op], status);
    }

    txn_id = GF_MALLOC(sizeof(uuid_t), gf_common_mt_uuid_t);

    if (txn_id)
        gf_uuid_copy(*txn_id, event->txn_id);
    else {
        ret = -1;
        goto out;
    }
    ret = glusterd_get_txn_opinfo(&event->txn_id, &txn_op_info);

    ret = dict_set_bin(rsp_dict, "transaction_id", txn_id, sizeof(*txn_id));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set transaction id.");
        GF_FREE(txn_id);
        txn_id = NULL;
        goto out;
    }

    ret = glusterd_op_stage_send_resp(req_ctx->req, req_ctx->op, status,
                                      op_errstr, rsp_dict);

out:
    if (op_errstr && (strcmp(op_errstr, "")))
        GF_FREE(op_errstr);

    gf_msg_debug(this->name, 0, "Returning with %d", ret);

    /* for no volname transactions, the txn_opinfo needs to be cleaned up
     * as there's no unlock event triggered. However if the originator node of
     * this transaction is still running with a version lower than 60000,
     * txn_opinfo can't be cleared as that'll lead to a race of referring op_ctx
     * after it's being freed.
     */
    if (txn_op_info.skip_locking && priv->op_version >= GD_OP_VERSION_6_0 &&
        txn_id)
        ret = glusterd_clear_txn_opinfo(txn_id);

    if (rsp_dict)
        dict_unref(rsp_dict);

    return ret;
}

static gf_boolean_t
glusterd_need_brick_op(glusterd_op_t op)
{
    gf_boolean_t ret = _gf_false;

    GF_ASSERT(GD_OP_NONE < op && op < GD_OP_MAX);

    switch (op) {
        case GD_OP_PROFILE_VOLUME:
        case GD_OP_STATUS_VOLUME:
        case GD_OP_DEFRAG_BRICK_VOLUME:
        case GD_OP_HEAL_VOLUME:
        case GD_OP_SCRUB_STATUS:
        case GD_OP_SCRUB_ONDEMAND:
            ret = _gf_true;
            break;
        default:
            ret = _gf_false;
    }

    return ret;
}

dict_t *
glusterd_op_init_commit_rsp_dict(glusterd_op_t op)
{
    dict_t *rsp_dict = NULL;
    dict_t *op_ctx = NULL;

    GF_ASSERT(GD_OP_NONE < op && op < GD_OP_MAX);

    if (glusterd_need_brick_op(op)) {
        op_ctx = glusterd_op_get_ctx();
        GF_ASSERT(op_ctx);
        rsp_dict = dict_ref(op_ctx);
    } else {
        rsp_dict = dict_new();
    }

    return rsp_dict;
}

static int
glusterd_op_ac_commit_op(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;
    glusterd_req_ctx_t *req_ctx = NULL;
    int32_t status = 0;
    char *op_errstr = NULL;
    dict_t *dict = NULL;
    dict_t *rsp_dict = NULL;
    xlator_t *this = NULL;
    uuid_t *txn_id = NULL;
    glusterd_op_info_t txn_op_info = {
        {0},
    };
    gf_boolean_t need_cleanup = _gf_true;

    this = THIS;
    GF_ASSERT(this);
    GF_ASSERT(ctx);

    req_ctx = ctx;

    dict = req_ctx->dict;

    rsp_dict = glusterd_op_init_commit_rsp_dict(req_ctx->op);
    if (NULL == rsp_dict)
        return -1;

    if (GD_OP_CLEARLOCKS_VOLUME == req_ctx->op) {
        /*clear locks should be run only on
         * originator glusterd*/
        status = 0;

    } else {
        status = glusterd_op_commit_perform(req_ctx->op, dict, &op_errstr,
                                            rsp_dict);
    }

    if (status)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_COMMIT_OP_FAIL,
               "Commit of operation "
               "'Volume %s' failed: %d",
               gd_op_list[req_ctx->op], status);

    txn_id = GF_MALLOC(sizeof(uuid_t), gf_common_mt_uuid_t);

    if (txn_id)
        gf_uuid_copy(*txn_id, event->txn_id);
    else {
        ret = -1;
        goto out;
    }
    ret = glusterd_get_txn_opinfo(&event->txn_id, &txn_op_info);
    if (ret) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, 0,
                         GD_MSG_TRANS_OPINFO_GET_FAIL,
                         "Unable to get transaction opinfo "
                         "for transaction ID : %s",
                         uuid_utoa(event->txn_id));
        goto out;
    }

    ret = dict_set_bin(rsp_dict, "transaction_id", txn_id, sizeof(*txn_id));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set transaction id.");
        if (txn_op_info.skip_locking)
            ret = glusterd_clear_txn_opinfo(txn_id);
        need_cleanup = _gf_false;
        GF_FREE(txn_id);
        goto out;
    }

    ret = glusterd_op_commit_send_resp(req_ctx->req, req_ctx->op, status,
                                       op_errstr, rsp_dict);

out:
    if (op_errstr && (strcmp(op_errstr, "")))
        GF_FREE(op_errstr);

    if (rsp_dict)
        dict_unref(rsp_dict);
    /* for no volname transactions, the txn_opinfo needs to be cleaned up
     * as there's no unlock event triggered
     */
    if (need_cleanup && txn_id && txn_op_info.skip_locking)
        ret = glusterd_clear_txn_opinfo(txn_id);
    gf_msg_debug(this->name, 0, "Returning with %d", ret);

    return ret;
}

static int
glusterd_op_ac_send_commit_failed(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;
    glusterd_req_ctx_t *req_ctx = NULL;
    dict_t *op_ctx = NULL;

    GF_ASSERT(ctx);

    req_ctx = ctx;

    op_ctx = glusterd_op_get_ctx();

    ret = glusterd_op_commit_send_resp(req_ctx->req, req_ctx->op, opinfo.op_ret,
                                       opinfo.op_errstr, op_ctx);

    if (opinfo.op_errstr && (strcmp(opinfo.op_errstr, ""))) {
        GF_FREE(opinfo.op_errstr);
        opinfo.op_errstr = NULL;
    }

    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    gf_msg_debug(THIS->name, 0, "Returning with %d", ret);
    return ret;
}

static int
glusterd_op_sm_transition_state(glusterd_op_info_t *opinfo,
                                glusterd_op_sm_t *state,
                                glusterd_op_sm_event_type_t event_type)
{
    glusterd_conf_t *conf = NULL;

    GF_ASSERT(state);
    GF_ASSERT(opinfo);

    conf = THIS->private;
    GF_ASSERT(conf);

    (void)glusterd_sm_tr_log_transition_add(
        &conf->op_sm_log, opinfo->state.state, state[event_type].next_state,
        event_type);

    opinfo->state.state = state[event_type].next_state;
    return 0;
}

int32_t
glusterd_op_stage_validate(glusterd_op_t op, dict_t *dict, char **op_errstr,
                           dict_t *rsp_dict)
{
    int ret = -1;
    xlator_t *this = THIS;

    switch (op) {
        case GD_OP_CREATE_VOLUME:
            ret = glusterd_op_stage_create_volume(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_START_VOLUME:
            ret = glusterd_op_stage_start_volume(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_STOP_VOLUME:
            ret = glusterd_op_stage_stop_volume(dict, op_errstr);
            break;

        case GD_OP_DELETE_VOLUME:
            ret = glusterd_op_stage_delete_volume(dict, op_errstr);
            break;

        case GD_OP_ADD_BRICK:
            ret = glusterd_op_stage_add_brick(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_REPLACE_BRICK:
            ret = glusterd_op_stage_replace_brick(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_SET_VOLUME:
            ret = glusterd_op_stage_set_volume(dict, op_errstr);
            break;

        case GD_OP_GANESHA:
            ret = glusterd_op_stage_set_ganesha(dict, op_errstr);
            break;

        case GD_OP_RESET_VOLUME:
            ret = glusterd_op_stage_reset_volume(dict, op_errstr);
            break;
        case GD_OP_REMOVE_BRICK:
            ret = glusterd_op_stage_remove_brick(dict, op_errstr);
            break;

        case GD_OP_LOG_ROTATE:
            ret = glusterd_op_stage_log_rotate(dict, op_errstr);
            break;

        case GD_OP_SYNC_VOLUME:
            ret = glusterd_op_stage_sync_volume(dict, op_errstr);
            break;

        case GD_OP_GSYNC_CREATE:
            ret = glusterd_op_stage_gsync_create(dict, op_errstr);
            break;

        case GD_OP_GSYNC_SET:
            ret = glusterd_op_stage_gsync_set(dict, op_errstr);
            break;

        case GD_OP_PROFILE_VOLUME:
            ret = glusterd_op_stage_stats_volume(dict, op_errstr);
            break;

        case GD_OP_QUOTA:
            ret = glusterd_op_stage_quota(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_STATUS_VOLUME:
            ret = glusterd_op_stage_status_volume(dict, op_errstr);
            break;

        case GD_OP_REBALANCE:
        case GD_OP_DEFRAG_BRICK_VOLUME:
            ret = glusterd_op_stage_rebalance(dict, op_errstr);
            break;

        case GD_OP_HEAL_VOLUME:
            ret = glusterd_op_stage_heal_volume(dict, op_errstr);
            break;

        case GD_OP_STATEDUMP_VOLUME:
            ret = glusterd_op_stage_statedump_volume(dict, op_errstr);
            break;
        case GD_OP_CLEARLOCKS_VOLUME:
            ret = glusterd_op_stage_clearlocks_volume(dict, op_errstr);
            break;

        case GD_OP_COPY_FILE:
            ret = glusterd_op_stage_copy_file(dict, op_errstr);
            break;

        case GD_OP_SYS_EXEC:
            ret = glusterd_op_stage_sys_exec(dict, op_errstr);
            break;

        case GD_OP_BARRIER:
            ret = glusterd_op_stage_barrier(dict, op_errstr);
            break;

        case GD_OP_BITROT:
        case GD_OP_SCRUB_STATUS:
        case GD_OP_SCRUB_ONDEMAND:
            ret = glusterd_op_stage_bitrot(dict, op_errstr, rsp_dict);
            break;

        default:
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
                   "Unknown op %s", gd_op_list[op]);
    }

    gf_msg_debug(this->name, 0, "OP = %d. Returning %d", op, ret);
    return ret;
}

static void
glusterd_wait_for_blockers(glusterd_conf_t *priv)
{
    while (GF_ATOMIC_GET(priv->blockers)) {
        synccond_wait(&priv->cond_blockers, &priv->big_lock);
    }
}

int32_t
glusterd_op_commit_perform(glusterd_op_t op, dict_t *dict, char **op_errstr,
                           dict_t *rsp_dict)
{
    int ret = -1;
    xlator_t *this = THIS;

    glusterd_op_commit_hook(op, dict, GD_COMMIT_HOOK_PRE);
    switch (op) {
        case GD_OP_CREATE_VOLUME:
            ret = glusterd_op_create_volume(dict, op_errstr);
            break;

        case GD_OP_START_VOLUME:
            ret = glusterd_op_start_volume(dict, op_errstr);
            break;

        case GD_OP_STOP_VOLUME:
            ret = glusterd_op_stop_volume(dict);
            break;

        case GD_OP_DELETE_VOLUME:
            glusterd_wait_for_blockers(this->private);
            ret = glusterd_op_delete_volume(dict);
            break;

        case GD_OP_ADD_BRICK:
            glusterd_wait_for_blockers(this->private);
            ret = glusterd_op_add_brick(dict, op_errstr);
            break;

        case GD_OP_REPLACE_BRICK:
            glusterd_wait_for_blockers(this->private);
            ret = glusterd_op_replace_brick(dict, rsp_dict);
            break;

        case GD_OP_SET_VOLUME:
            ret = glusterd_op_set_volume(dict, op_errstr);
            break;
        case GD_OP_GANESHA:
            ret = glusterd_op_set_ganesha(dict, op_errstr);
            break;
        case GD_OP_RESET_VOLUME:
            ret = glusterd_op_reset_volume(dict, op_errstr);
            break;

        case GD_OP_REMOVE_BRICK:
            glusterd_wait_for_blockers(this->private);
            ret = glusterd_op_remove_brick(dict, op_errstr);
            break;

        case GD_OP_LOG_ROTATE:
            ret = glusterd_op_log_rotate(dict);
            break;

        case GD_OP_SYNC_VOLUME:
            ret = glusterd_op_sync_volume(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_GSYNC_CREATE:
            ret = glusterd_op_gsync_create(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_GSYNC_SET:
            ret = glusterd_op_gsync_set(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_PROFILE_VOLUME:
            ret = glusterd_op_stats_volume(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_QUOTA:
            ret = glusterd_op_quota(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_STATUS_VOLUME:
            ret = glusterd_op_status_volume(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_REBALANCE:
        case GD_OP_DEFRAG_BRICK_VOLUME:
            ret = glusterd_op_rebalance(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_HEAL_VOLUME:
            ret = glusterd_op_heal_volume(dict, op_errstr);
            break;

        case GD_OP_STATEDUMP_VOLUME:
            ret = glusterd_op_statedump_volume(dict, op_errstr);
            break;

        case GD_OP_CLEARLOCKS_VOLUME:
            ret = glusterd_op_clearlocks_volume(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_COPY_FILE:
            ret = glusterd_op_copy_file(dict, op_errstr);
            break;

        case GD_OP_SYS_EXEC:
            ret = glusterd_op_sys_exec(dict, op_errstr, rsp_dict);
            break;

        case GD_OP_BARRIER:
            ret = glusterd_op_barrier(dict, op_errstr);
            break;

        case GD_OP_BITROT:
        case GD_OP_SCRUB_STATUS:
        case GD_OP_SCRUB_ONDEMAND:
            ret = glusterd_op_bitrot(dict, op_errstr, rsp_dict);
            break;

        default:
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
                   "Unknown op %s", gd_op_list[op]);
            break;
    }

    if (ret == 0)
        glusterd_op_commit_hook(op, dict, GD_COMMIT_HOOK_POST);

    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_bricks_select_stop_volume(dict_t *dict, char **op_errstr,
                                   struct cds_list_head *selected)
{
    int ret = 0;
    int flags = 0;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_pending_node_t *pending_node = NULL;

    ret = glusterd_op_stop_volume_args_get(dict, &volname, &flags);
    if (ret)
        goto out;

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               FMTSTR_CHECK_VOL_EXISTS, volname);
        gf_asprintf(op_errstr, FMTSTR_CHECK_VOL_EXISTS, volname);
        goto out;
    }

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (glusterd_is_brick_started(brickinfo)) {
            pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                     gf_gld_mt_pending_node_t);
            if (!pending_node) {
                ret = -1;
                goto out;
            } else {
                pending_node->node = brickinfo;
                pending_node->type = GD_NODE_BRICK;
                cds_list_add_tail(&pending_node->list, selected);
                pending_node = NULL;
            }
            /*
             * This is not really the right place to do it, but
             * it's the most convenient.
             * TBD: move this to *after* the RPC
             */
            brickinfo->status = GF_BRICK_STOPPED;
        }
    }

out:
    return ret;
}

static int
glusterd_bricks_select_remove_brick(dict_t *dict, char **op_errstr,
                                    struct cds_list_head *selected)
{
    int ret = -1;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    char *brick = NULL;
    int32_t count = 0;
    int32_t i = 1;
    char key[64] = {
        0,
    };
    int keylen;
    glusterd_pending_node_t *pending_node = NULL;
    int32_t command = 0;
    int32_t force = 0;

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);

    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);

    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               "Unable to allocate memory");
        goto out;
    }

    ret = dict_get_int32n(dict, "count", SLEN("count"), &count);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
               "Unable to get count");
        goto out;
    }

    ret = dict_get_int32n(dict, "command", SLEN("command"), &command);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
               "Unable to get command");
        goto out;
    }

    ret = dict_get_int32n(dict, "force", SLEN("force"), &force);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
               "force flag is not set");
        ret = 0;
        goto out;
    }

    while (i <= count) {
        keylen = snprintf(key, sizeof(key), "brick%d", i);

        ret = dict_get_strn(dict, key, keylen, &brick);
        if (ret) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get brick");
            goto out;
        }

        ret = glusterd_volume_brickinfo_get_by_brick(brick, volinfo, &brickinfo,
                                                     _gf_false);

        if (ret)
            goto out;

        if (glusterd_is_brick_started(brickinfo)) {
            pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                     gf_gld_mt_pending_node_t);
            if (!pending_node) {
                ret = -1;
                goto out;
            } else {
                pending_node->node = brickinfo;
                pending_node->type = GD_NODE_BRICK;
                cds_list_add_tail(&pending_node->list, selected);
                pending_node = NULL;
            }
            /*
             * This is not really the right place to do it, but
             * it's the most convenient.
             * TBD: move this to *after* the RPC
             */
            brickinfo->status = GF_BRICK_STOPPED;
        }
        i++;
    }

out:
    return ret;
}

static int
glusterd_bricks_select_profile_volume(dict_t *dict, char **op_errstr,
                                      struct cds_list_head *selected)
{
    int ret = -1;
    char *volname = NULL;
    char msg[2048] = {
        0,
    };
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = NULL;
    int32_t stats_op = GF_CLI_STATS_NONE;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_pending_node_t *pending_node = NULL;
    char *brick = NULL;
    int32_t pid = -1;
    char pidfile[PATH_MAX] = {0};

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "volume name get failed");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), "Volume %s does not exists", volname);

        *op_errstr = gf_strdup(msg);
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND, "%s", msg);
        goto out;
    }

    ret = dict_get_int32n(dict, "op", SLEN("op"), &stats_op);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "volume profile op get failed");
        goto out;
    }

    switch (stats_op) {
        case GF_CLI_STATS_START:
        case GF_CLI_STATS_STOP:
            goto out;
            break;
        case GF_CLI_STATS_INFO:
#ifdef BUILD_GNFS
            ret = dict_get_str_boolean(dict, "nfs", _gf_false);
            if (ret) {
                if (!priv->nfs_svc.online) {
                    ret = -1;
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_NFS_SERVER_NOT_RUNNING,
                           "NFS server"
                           " is not running");
                    goto out;
                }
                pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                         gf_gld_mt_pending_node_t);
                if (!pending_node) {
                    ret = -1;
                    goto out;
                }
                pending_node->node = &(priv->nfs_svc);
                pending_node->type = GD_NODE_NFS;
                cds_list_add_tail(&pending_node->list, selected);
                pending_node = NULL;

                ret = 0;
                goto out;
            }
#endif
            cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
            {
                if (glusterd_is_brick_started(brickinfo)) {
                    /*
                     * In normal use, glusterd_is_brick_started
                     * will give us the answer we need.  However,
                     * in our tests the brick gets detached behind
                     * our back, so we need to double-check this
                     * way.
                     */
                    GLUSTERD_GET_BRICK_PIDFILE(pidfile, volinfo, brickinfo,
                                               priv);
                    if (!gf_is_service_running(pidfile, &pid)) {
                        continue;
                    }
                    pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                             gf_gld_mt_pending_node_t);
                    if (!pending_node) {
                        ret = -1;
                        goto out;
                    } else {
                        pending_node->node = brickinfo;
                        pending_node->type = GD_NODE_BRICK;
                        cds_list_add_tail(&pending_node->list, selected);
                        pending_node = NULL;
                    }
                }
            }
            break;

        case GF_CLI_STATS_TOP:
#ifdef BUILD_GNFS
            ret = dict_get_str_boolean(dict, "nfs", _gf_false);
            if (ret) {
                if (!priv->nfs_svc.online) {
                    ret = -1;
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_NFS_SERVER_NOT_RUNNING,
                           "NFS server"
                           " is not running");
                    goto out;
                }
                pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                         gf_gld_mt_pending_node_t);
                if (!pending_node) {
                    ret = -1;
                    goto out;
                }
                pending_node->node = &(priv->nfs_svc);
                pending_node->type = GD_NODE_NFS;
                cds_list_add_tail(&pending_node->list, selected);
                pending_node = NULL;

                ret = 0;
                goto out;
            }
#endif
            ret = dict_get_strn(dict, "brick", SLEN("brick"), &brick);
            if (!ret) {
                ret = glusterd_volume_brickinfo_get_by_brick(
                    brick, volinfo, &brickinfo, _gf_true);
                if (ret)
                    goto out;

                if (!glusterd_is_brick_started(brickinfo))
                    goto out;

                pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                         gf_gld_mt_pending_node_t);
                if (!pending_node) {
                    ret = -1;
                    goto out;
                } else {
                    pending_node->node = brickinfo;
                    pending_node->type = GD_NODE_BRICK;
                    cds_list_add_tail(&pending_node->list, selected);
                    pending_node = NULL;
                    goto out;
                }
            }
            ret = 0;
            cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
            {
                if (glusterd_is_brick_started(brickinfo)) {
                    pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                             gf_gld_mt_pending_node_t);
                    if (!pending_node) {
                        ret = -1;
                        goto out;
                    } else {
                        pending_node->node = brickinfo;
                        pending_node->type = GD_NODE_BRICK;
                        cds_list_add_tail(&pending_node->list, selected);
                        pending_node = NULL;
                    }
                }
            }
            break;

        default:
            GF_ASSERT(0);
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
                   "Invalid profile op: %d", stats_op);
            ret = -1;
            goto out;
            break;
    }

out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}

int
_get_hxl_children_count(glusterd_volinfo_t *volinfo)
{
    if (volinfo->type == GF_CLUSTER_TYPE_DISPERSE) {
        return volinfo->disperse_count;
    } else {
        return volinfo->replica_count;
    }
}

static int
_add_hxlator_to_dict(dict_t *dict, glusterd_volinfo_t *volinfo, int index,
                     int count)
{
    int ret = -1;
    char key[64] = {
        0,
    };
    int keylen;
    char *xname = NULL;
    char *xl_type = 0;

    if (volinfo->type == GF_CLUSTER_TYPE_DISPERSE) {
        xl_type = "disperse";
    } else {
        xl_type = "replicate";
    }
    keylen = snprintf(key, sizeof(key), "xl-%d", count);
    ret = gf_asprintf(&xname, "%s-%s-%d", volinfo->volname, xl_type, index);
    if (ret == -1)
        goto out;

    ret = dict_set_dynstrn(dict, key, keylen, xname);
    if (ret)
        goto out;

    ret = dict_set_int32(dict, xname, index);
out:
    return ret;
}

int
get_replica_index_for_per_replica_cmd(glusterd_volinfo_t *volinfo, dict_t *dict)
{
    int ret = 0;
    char *hostname = NULL;
    char *path = NULL;
    int index = 0;
    glusterd_brickinfo_t *brickinfo = NULL;
    int cmd_replica_index = -1;
    int replica_count = -1;

    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_get_strn(dict, "per-replica-cmd-hostname",
                        SLEN("per-replica-cmd-hostname"), &hostname);
    if (ret)
        goto out;
    ret = dict_get_strn(dict, "per-replica-cmd-path",
                        SLEN("per-replica-cmd-path"), &path);
    if (ret)
        goto out;

    replica_count = volinfo->replica_count;

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (gf_uuid_is_null(brickinfo->uuid))
            (void)glusterd_resolve_brick(brickinfo);
        if (!strcmp(brickinfo->path, path) &&
            !strcmp(brickinfo->hostname, hostname)) {
            cmd_replica_index = index / (replica_count);
            goto out;
        }
        index++;
    }

out:
    if (ret)
        cmd_replica_index = -1;

    return cmd_replica_index;
}

int
_select_hxlator_with_matching_brick(xlator_t *this, glusterd_volinfo_t *volinfo,
                                    dict_t *dict, int *index)
{
    char *path = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    int hxl_children = 0;

    if (!dict || dict_get_strn(dict, "per-replica-cmd-path",
                               SLEN("per-replica-cmd-path"), &path))
        return -1;

    hxl_children = _get_hxl_children_count(volinfo);
    if ((*index) == 0)
        (*index)++;

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (gf_uuid_is_null(brickinfo->uuid))
            (void)glusterd_resolve_brick(brickinfo);

        if ((!gf_uuid_compare(MY_UUID, brickinfo->uuid)) &&
            (!strncmp(brickinfo->path, path, strlen(path)))) {
            _add_hxlator_to_dict(dict, volinfo, ((*index) - 1) / hxl_children,
                                 0);
            return 1;
        }
        (*index)++;
    }

    return 0;
}
void
_select_hxlators_with_local_bricks(xlator_t *this, glusterd_volinfo_t *volinfo,
                                   dict_t *dict, int *index, int *hxlator_count)
{
    glusterd_brickinfo_t *brickinfo = NULL;
    int hxl_children = 0;
    gf_boolean_t add = _gf_false;

    hxl_children = _get_hxl_children_count(volinfo);

    if ((*index) == 0)
        (*index)++;

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (gf_uuid_is_null(brickinfo->uuid))
            (void)glusterd_resolve_brick(brickinfo);

        if (!gf_uuid_compare(MY_UUID, brickinfo->uuid))
            add = _gf_true;

        if ((*index) % hxl_children == 0) {
            if (add) {
                _add_hxlator_to_dict(dict, volinfo,
                                     ((*index) - 1) / hxl_children,
                                     (*hxlator_count));
                (*hxlator_count)++;
            }
            add = _gf_false;
        }

        (*index)++;
    }
}

int
_select_hxlators_for_full_self_heal(xlator_t *this, glusterd_volinfo_t *volinfo,
                                    dict_t *dict, int *index,
                                    int *hxlator_count)
{
    glusterd_brickinfo_t *brickinfo = NULL;
    int hxl_children = 0;
    uuid_t candidate = {0};
    int brick_index = 0;
    glusterd_peerinfo_t *peerinfo = NULL;
    int delta = 0;
    uuid_t candidate_max = {0};

    if ((*index) == 0)
        (*index)++;
    if (volinfo->type == GF_CLUSTER_TYPE_DISPERSE) {
        hxl_children = volinfo->disperse_count;
    } else {
        hxl_children = volinfo->replica_count;
    }

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (gf_uuid_compare(brickinfo->uuid, candidate_max) > 0) {
            if (!gf_uuid_compare(MY_UUID, brickinfo->uuid)) {
                gf_uuid_copy(candidate_max, brickinfo->uuid);
            } else {
                peerinfo = glusterd_peerinfo_find(brickinfo->uuid, NULL);
                if (peerinfo && peerinfo->connected) {
                    gf_uuid_copy(candidate_max, brickinfo->uuid);
                }
            }
        }
    }

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (gf_uuid_is_null(brickinfo->uuid))
            (void)glusterd_resolve_brick(brickinfo);

        delta %= hxl_children;
        if ((*index + delta) == (brick_index + hxl_children)) {
            if (!gf_uuid_compare(MY_UUID, brickinfo->uuid)) {
                gf_uuid_copy(candidate, brickinfo->uuid);
            } else {
                peerinfo = glusterd_peerinfo_find(brickinfo->uuid, NULL);
                if (peerinfo && peerinfo->connected) {
                    gf_uuid_copy(candidate, brickinfo->uuid);
                } else if (peerinfo &&
                           (!gf_uuid_compare(candidate_max, MY_UUID))) {
                    _add_hxlator_to_dict(dict, volinfo,
                                         ((*index) - 1) / hxl_children,
                                         (*hxlator_count));
                    (*hxlator_count)++;
                }
            }

            if (!gf_uuid_compare(MY_UUID, candidate)) {
                _add_hxlator_to_dict(dict, volinfo,
                                     ((*index) - 1) / hxl_children,
                                     (*hxlator_count));
                (*hxlator_count)++;
            }
            gf_uuid_clear(candidate);
            brick_index += hxl_children;
            delta++;
        }

        (*index)++;
    }
    return *hxlator_count;
}

static int
glusterd_bricks_select_snap(dict_t *dict, char **op_errstr,
                            struct cds_list_head *selected)
{
    int ret = -1;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;
    glusterd_pending_node_t *pending_node = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    char *volname = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    int brick_index = -1;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get"
               " volname");
        goto out;
    }
    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret)
        goto out;

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        brick_index++;
        if (gf_uuid_compare(brickinfo->uuid, MY_UUID) ||
            !glusterd_is_brick_started(brickinfo)) {
            continue;
        }
        pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                 gf_gld_mt_pending_node_t);
        if (!pending_node) {
            ret = -1;
            goto out;
        }
        pending_node->node = brickinfo;
        pending_node->type = GD_NODE_BRICK;
        pending_node->index = brick_index;
        cds_list_add_tail(&pending_node->list, selected);
        pending_node = NULL;
    }

    ret = 0;

out:
    gf_msg_debug(THIS->name, 0, "Returning ret %d", ret);
    return ret;
}

static int
fill_shd_status_for_local_bricks(dict_t *dict, glusterd_volinfo_t *volinfo,
                                 cli_cmd_type type, int *index,
                                 dict_t *req_dict)
{
    glusterd_brickinfo_t *brickinfo = NULL;
    static char *msg = "self-heal-daemon is not running on";
    char key[32] = {
        0,
    };
    int keylen;
    char value[128] = {
        0,
    };
    int ret = 0;
    xlator_t *this = NULL;
    int cmd_replica_index = -1;

    this = THIS;

    if (type == PER_HEAL_XL) {
        cmd_replica_index = get_replica_index_for_per_replica_cmd(volinfo,
                                                                  req_dict);
        if (cmd_replica_index == -1) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_REPLICA_INDEX_GET_FAIL,
                   "Could not find the "
                   "replica index for per replica type command");
            ret = -1;
            goto out;
        }
    }

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (gf_uuid_is_null(brickinfo->uuid))
            (void)glusterd_resolve_brick(brickinfo);

        if (gf_uuid_compare(MY_UUID, brickinfo->uuid)) {
            (*index)++;
            continue;
        }

        if (type == PER_HEAL_XL) {
            if (cmd_replica_index != ((*index) / volinfo->replica_count)) {
                (*index)++;
                continue;
            }
        }
        keylen = snprintf(key, sizeof(key), "%d-status", (*index));
        snprintf(value, sizeof(value), "%s %s", msg, uuid_utoa(MY_UUID));
        ret = dict_set_dynstrn(dict, key, keylen, gf_strdup(value));
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Unable to"
                   "set the dictionary for shd status msg");
            goto out;
        }
        keylen = snprintf(key, sizeof(key), "%d-shd-status", (*index));
        ret = dict_set_nstrn(dict, key, keylen, "off", SLEN("off"));
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Unable to"
                   " set dictionary for shd status msg");
            goto out;
        }

        (*index)++;
    }

out:
    return ret;
}
int
glusterd_shd_select_brick_xlator(dict_t *dict, gf_xl_afr_op_t heal_op,
                                 glusterd_volinfo_t *volinfo, int *index,
                                 int *hxlator_count, dict_t *rsp_dict)
{
    int ret = -1;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;
    glusterd_svc_t *svc = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);
    svc = &(volinfo->shd.svc);

    switch (heal_op) {
        case GF_SHD_OP_INDEX_SUMMARY:
        case GF_SHD_OP_STATISTICS_HEAL_COUNT:
            if (!svc->online) {
                if (!rsp_dict) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_OPCTX_NULL,
                           "Received "
                           "empty ctx.");
                    goto out;
                }

                ret = fill_shd_status_for_local_bricks(
                    rsp_dict, volinfo, ALL_HEAL_XL, index, dict);
                if (ret)
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_SHD_STATUS_SET_FAIL,
                           "Unable to "
                           "fill the shd status for the local "
                           "bricks");
                goto out;
            }
            break;

        case GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
            if (!svc->online) {
                if (!rsp_dict) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_OPCTX_NULL,
                           "Received "
                           "empty ctx.");
                    goto out;
                }
                ret = fill_shd_status_for_local_bricks(
                    rsp_dict, volinfo, PER_HEAL_XL, index, dict);
                if (ret)
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_SHD_STATUS_SET_FAIL,
                           "Unable to "
                           "fill the shd status for the local"
                           " bricks.");
                goto out;
            }
            break;

        default:
            break;
    }

    switch (heal_op) {
        case GF_SHD_OP_HEAL_FULL:
            _select_hxlators_for_full_self_heal(this, volinfo, dict, index,
                                                hxlator_count);
            break;
        case GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
            (*hxlator_count) += _select_hxlator_with_matching_brick(
                this, volinfo, dict, index);
            break;
        default:
            _select_hxlators_with_local_bricks(this, volinfo, dict, index,
                                               hxlator_count);
            break;
    }
    ret = (*hxlator_count);
out:
    return ret;
}

static int
glusterd_bricks_select_heal_volume(dict_t *dict, char **op_errstr,
                                   struct cds_list_head *selected,
                                   dict_t *rsp_dict)
{
    int ret = -1;
    char *volname = NULL;
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = NULL;
    char msg[2048] = {
        0,
    };
    glusterd_pending_node_t *pending_node = NULL;
    gf_xl_afr_op_t heal_op = GF_SHD_OP_INVALID;
    int hxlator_count = 0;
    int index = 0;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "volume name get failed");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), "Volume %s does not exist", volname);

        *op_errstr = gf_strdup(msg);
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND, "%s", msg);
        goto out;
    }

    ret = dict_get_int32n(dict, "heal-op", SLEN("heal-op"),
                          (int32_t *)&heal_op);
    if (ret || (heal_op == GF_SHD_OP_INVALID)) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "heal op invalid");
        goto out;
    }
    ret = glusterd_shd_select_brick_xlator(dict, heal_op, volinfo, &index,
                                           &hxlator_count, rsp_dict);
    if (ret < 0) {
        goto out;
    }

    if (!hxlator_count)
        goto out;
    if (hxlator_count == -1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_XLATOR_COUNT_GET_FAIL,
               "Could not determine the"
               "translator count");
        ret = -1;
        goto out;
    }

    ret = dict_set_int32n(dict, "count", SLEN("count"), hxlator_count);
    if (ret)
        goto out;
    pending_node = GF_CALLOC(1, sizeof(*pending_node),
                             gf_gld_mt_pending_node_t);
    if (!pending_node) {
        ret = -1;
        goto out;
    } else {
        pending_node->node = &(volinfo->shd.svc);
        pending_node->type = GD_NODE_SHD;
        cds_list_add_tail(&pending_node->list, selected);
        pending_node = NULL;
    }

out:
    gf_msg_debug(THIS->name, 0, "Returning ret %d", ret);
    return ret;
}

static int
glusterd_bricks_select_rebalance_volume(dict_t *dict, char **op_errstr,
                                        struct cds_list_head *selected)
{
    int ret = -1;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = NULL;
    char msg[2048] = {
        0,
    };
    glusterd_pending_node_t *pending_node = NULL;

    this = THIS;
    GF_ASSERT(this);

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "volume name get failed");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), "Volume %s does not exist", volname);

        *op_errstr = gf_strdup(msg);
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND, "%s", msg);
        goto out;
    }
    pending_node = GF_CALLOC(1, sizeof(*pending_node),
                             gf_gld_mt_pending_node_t);
    if (!pending_node) {
        ret = -1;
        goto out;
    } else {
        pending_node->node = volinfo;
        pending_node->type = GD_NODE_REBALANCE;
        cds_list_add_tail(&pending_node->list, selected);
        pending_node = NULL;
    }

out:
    return ret;
}

static int
glusterd_bricks_select_status_volume(dict_t *dict, char **op_errstr,
                                     struct cds_list_head *selected)
{
    int ret = -1;
    int cmd = 0;
    int brick_index = -1;
    char *volname = NULL;
    char *brickname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_pending_node_t *pending_node = NULL;
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;
    glusterd_svc_t *svc = NULL;

    GF_ASSERT(dict);

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_int32n(dict, "cmd", SLEN("cmd"), &cmd);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get status type");
        goto out;
    }

    if (cmd & GF_CLI_STATUS_ALL)
        goto out;

    switch (cmd & GF_CLI_STATUS_MASK) {
        case GF_CLI_STATUS_MEM:
        case GF_CLI_STATUS_CLIENTS:
        case GF_CLI_STATUS_INODE:
        case GF_CLI_STATUS_FD:
        case GF_CLI_STATUS_CALLPOOL:
        case GF_CLI_STATUS_NFS:
        case GF_CLI_STATUS_SHD:
        case GF_CLI_STATUS_QUOTAD:
        case GF_CLI_STATUS_SNAPD:
        case GF_CLI_STATUS_BITD:
        case GF_CLI_STATUS_SCRUB:
        case GF_CLI_STATUS_CLIENT_LIST:
            break;
        default:
            goto out;
    }
    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volname");
        goto out;
    }
    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        goto out;
    }

    if ((cmd & GF_CLI_STATUS_BRICK) != 0) {
        ret = dict_get_strn(dict, "brick", SLEN("brick"), &brickname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get brick");
            goto out;
        }
        ret = glusterd_volume_brickinfo_get_by_brick(brickname, volinfo,
                                                     &brickinfo, _gf_false);
        if (ret)
            goto out;

        if (gf_uuid_compare(brickinfo->uuid, MY_UUID) ||
            !glusterd_is_brick_started(brickinfo))
            goto out;

        pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                 gf_gld_mt_pending_node_t);
        if (!pending_node) {
            ret = -1;
            goto out;
        }
        pending_node->node = brickinfo;
        pending_node->type = GD_NODE_BRICK;
        pending_node->index = 0;
        cds_list_add_tail(&pending_node->list, selected);

        ret = 0;
#ifdef BUILD_GNFS
    } else if ((cmd & GF_CLI_STATUS_NFS) != 0) {
        if (!priv->nfs_svc.online) {
            ret = -1;
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_NFS_SERVER_NOT_RUNNING,
                   "NFS server is not running");
            goto out;
        }
        pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                 gf_gld_mt_pending_node_t);
        if (!pending_node) {
            ret = -1;
            goto out;
        }
        pending_node->node = &(priv->nfs_svc);
        pending_node->type = GD_NODE_NFS;
        pending_node->index = 0;
        cds_list_add_tail(&pending_node->list, selected);

        ret = 0;
#endif
    } else if ((cmd & GF_CLI_STATUS_SHD) != 0) {
        svc = &(volinfo->shd.svc);
        if (!svc->online) {
            ret = -1;
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SELF_HEALD_DISABLED,
                   "Self-heal daemon is not running");
            goto out;
        }
        pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                 gf_gld_mt_pending_node_t);
        if (!pending_node) {
            ret = -1;
            goto out;
        }
        pending_node->node = svc;
        pending_node->type = GD_NODE_SHD;
        pending_node->index = 0;
        cds_list_add_tail(&pending_node->list, selected);

        ret = 0;
    } else if ((cmd & GF_CLI_STATUS_QUOTAD) != 0) {
        if (!priv->quotad_svc.online) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_QUOTAD_NOT_RUNNING,
                   "Quotad is not "
                   "running");
            ret = -1;
            goto out;
        }
        pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                 gf_gld_mt_pending_node_t);
        if (!pending_node) {
            ret = -1;
            goto out;
        }
        pending_node->node = &(priv->quotad_svc);
        pending_node->type = GD_NODE_QUOTAD;
        pending_node->index = 0;
        cds_list_add_tail(&pending_node->list, selected);

        ret = 0;
    } else if ((cmd & GF_CLI_STATUS_BITD) != 0) {
        if (!priv->bitd_svc.online) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BITROT_NOT_RUNNING,
                   "Bitrot is not "
                   "running");
            ret = -1;
            goto out;
        }
        pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                 gf_gld_mt_pending_node_t);
        if (!pending_node) {
            ret = -1;
            goto out;
        }
        pending_node->node = &(priv->bitd_svc);
        pending_node->type = GD_NODE_BITD;
        pending_node->index = 0;
        cds_list_add_tail(&pending_node->list, selected);

        ret = 0;
    } else if ((cmd & GF_CLI_STATUS_SCRUB) != 0) {
        if (!priv->scrub_svc.online) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SCRUBBER_NOT_RUNNING,
                   "Scrubber is not "
                   "running");
            ret = -1;
            goto out;
        }
        pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                 gf_gld_mt_pending_node_t);
        if (!pending_node) {
            ret = -1;
            goto out;
        }
        pending_node->node = &(priv->scrub_svc);
        pending_node->type = GD_NODE_SCRUB;
        pending_node->index = 0;
        cds_list_add_tail(&pending_node->list, selected);

        ret = 0;
    } else if ((cmd & GF_CLI_STATUS_SNAPD) != 0) {
        if (!volinfo->snapd.svc.online) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAPD_NOT_RUNNING,
                   "snapd is not "
                   "running");
            ret = -1;
            goto out;
        }
        pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                 gf_gld_mt_pending_node_t);
        if (!pending_node) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "failed to allocate "
                   "memory for pending node");
            ret = -1;
            goto out;
        }

        pending_node->node = (void *)(&volinfo->snapd);
        pending_node->type = GD_NODE_SNAPD;
        pending_node->index = 0;
        cds_list_add_tail(&pending_node->list, selected);

        ret = 0;
    } else {
        cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
        {
            brick_index++;
            if (gf_uuid_compare(brickinfo->uuid, MY_UUID) ||
                !glusterd_is_brick_started(brickinfo)) {
                continue;
            }
            pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                     gf_gld_mt_pending_node_t);
            if (!pending_node) {
                ret = -1;
                gf_msg(THIS->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                       "Unable to allocate memory");
                goto out;
            }
            pending_node->node = brickinfo;
            pending_node->type = GD_NODE_BRICK;
            pending_node->index = brick_index;
            cds_list_add_tail(&pending_node->list, selected);
            pending_node = NULL;
        }
    }
out:
    return ret;
}

static int
glusterd_bricks_select_scrub(dict_t *dict, char **op_errstr,
                             struct cds_list_head *selected)
{
    int ret = -1;
    char *volname = NULL;
    char msg[2048] = {
        0,
    };
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_pending_node_t *pending_node = NULL;

    this = THIS;
    priv = this->private;
    GF_ASSERT(this);
    GF_ASSERT(priv);

    GF_ASSERT(dict);

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get"
               " volname");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), "Volume %s does not exist", volname);

        *op_errstr = gf_strdup(msg);
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND, "%s",
               msg);
        goto out;
    }

    if (!priv->scrub_svc.online) {
        ret = 0;
        snprintf(msg, sizeof(msg), "Scrubber daemon is not running");

        gf_msg_debug(this->name, 0, "%s", msg);
        goto out;
    }

    pending_node = GF_CALLOC(1, sizeof(*pending_node),
                             gf_gld_mt_pending_node_t);
    if (!pending_node) {
        ret = -1;
        goto out;
    }

    pending_node->node = &(priv->scrub_svc);
    pending_node->type = GD_NODE_SCRUB;
    cds_list_add_tail(&pending_node->list, selected);
    pending_node = NULL;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}
/* Select the bricks to send the barrier request to.
 * This selects the bricks of the given volume which are present on this peer
 * and are running
 */
static int
glusterd_bricks_select_barrier(dict_t *dict, struct cds_list_head *selected)
{
    int ret = -1;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_pending_node_t *pending_node = NULL;

    GF_ASSERT(dict);

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get volname");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               "Failed to find volume %s", volname);
        goto out;
    }

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (gf_uuid_compare(brickinfo->uuid, MY_UUID) ||
            !glusterd_is_brick_started(brickinfo)) {
            continue;
        }
        pending_node = GF_CALLOC(1, sizeof(*pending_node),
                                 gf_gld_mt_pending_node_t);
        if (!pending_node) {
            ret = -1;
            goto out;
        }
        pending_node->node = brickinfo;
        pending_node->type = GD_NODE_BRICK;
        cds_list_add_tail(&pending_node->list, selected);
        pending_node = NULL;
    }

out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_op_ac_send_brick_op(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = 0;
    rpc_clnt_procedure_t *proc = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = NULL;
    glusterd_op_t op = GD_OP_NONE;
    glusterd_req_ctx_t *req_ctx = NULL;
    char *op_errstr = NULL;
    gf_boolean_t free_req_ctx = _gf_false;

    this = THIS;
    priv = this->private;

    if (ctx) {
        req_ctx = ctx;
    } else {
        req_ctx = GF_CALLOC(1, sizeof(*req_ctx), gf_gld_mt_op_allack_ctx_t);
        if (!req_ctx)
            goto out;
        free_req_ctx = _gf_true;
        op = glusterd_op_get_op();
        req_ctx->op = op;
        gf_uuid_copy(req_ctx->uuid, MY_UUID);
        ret = glusterd_op_build_payload(&req_ctx->dict, &op_errstr, NULL);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_BRICK_OP_PAYLOAD_BUILD_FAIL, LOGSTR_BUILD_PAYLOAD,
                   gd_op_list[op]);
            if (op_errstr == NULL)
                gf_asprintf(&op_errstr, OPERRSTR_BUILD_PAYLOAD);
            opinfo.op_errstr = op_errstr;
            goto out;
        }
    }

    proc = &priv->gfs_mgmt->proctable[GLUSTERD_BRICK_OP];
    if (proc->fn) {
        ret = proc->fn(NULL, this, req_ctx);
        if (ret)
            goto out;
    }

    if (!opinfo.pending_count && !opinfo.brick_pending_count) {
        glusterd_clear_pending_nodes(&opinfo.pending_bricks);
        ret = glusterd_op_sm_inject_event(GD_OP_EVENT_ALL_ACK, &event->txn_id,
                                          req_ctx);
    }

out:
    if (ret && free_req_ctx)
        GF_FREE(req_ctx);
    gf_msg_debug(this->name, 0, "Returning with %d", ret);

    return ret;
}

static int
glusterd_op_ac_rcvd_brick_op_acc(glusterd_op_sm_event_t *event, void *ctx)
{
    int ret = -1;
    glusterd_op_brick_rsp_ctx_t *ev_ctx = NULL;
    char *op_errstr = NULL;
    glusterd_op_t op = GD_OP_NONE;
    gd_node_type type = GD_NODE_NONE;
    dict_t *op_ctx = NULL;
    glusterd_req_ctx_t *req_ctx = NULL;
    void *pending_entry = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);
    GF_VALIDATE_OR_GOTO(this->name, event, out);
    GF_VALIDATE_OR_GOTO(this->name, ctx, out);
    ev_ctx = ctx;
    GF_VALIDATE_OR_GOTO(this->name, ev_ctx, out);

    req_ctx = ev_ctx->commit_ctx;
    GF_VALIDATE_OR_GOTO(this->name, req_ctx, out);

    op = req_ctx->op;
    op_ctx = glusterd_op_get_ctx();
    pending_entry = ev_ctx->pending_node->node;
    type = ev_ctx->pending_node->type;

    ret = glusterd_remove_pending_entry(&opinfo.pending_bricks, pending_entry);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UNKNOWN_RESPONSE,
               "unknown response received ");
        ret = -1;
        goto out;
    }

    if (opinfo.brick_pending_count > 0)
        opinfo.brick_pending_count--;

    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set "
               "transaction's opinfo");

    glusterd_handle_node_rsp(req_ctx->dict, pending_entry, op, ev_ctx->rsp_dict,
                             op_ctx, &op_errstr, type);

    if (opinfo.brick_pending_count > 0)
        goto out;

    ret = glusterd_op_sm_inject_event(GD_OP_EVENT_ALL_ACK, &event->txn_id,
                                      ev_ctx->commit_ctx);

out:
    if (ev_ctx && ev_ctx->rsp_dict)
        dict_unref(ev_ctx->rsp_dict);
    GF_FREE(ev_ctx);
    gf_msg_debug(this ? this->name : "glusterd", 0, "Returning %d", ret);
    return ret;
}

int32_t
glusterd_op_bricks_select(glusterd_op_t op, dict_t *dict, char **op_errstr,
                          struct cds_list_head *selected, dict_t *rsp_dict)
{
    int ret = 0;

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);
    GF_ASSERT(op > GD_OP_NONE);
    GF_ASSERT(op < GD_OP_MAX);

    switch (op) {
        case GD_OP_STOP_VOLUME:
            ret = glusterd_bricks_select_stop_volume(dict, op_errstr, selected);
            break;
        case GD_OP_REMOVE_BRICK:
            ret = glusterd_bricks_select_remove_brick(dict, op_errstr,
                                                      selected);
            break;

        case GD_OP_PROFILE_VOLUME:
            ret = glusterd_bricks_select_profile_volume(dict, op_errstr,
                                                        selected);
            break;

        case GD_OP_HEAL_VOLUME:
            ret = glusterd_bricks_select_heal_volume(dict, op_errstr, selected,
                                                     rsp_dict);
            break;

        case GD_OP_STATUS_VOLUME:
            ret = glusterd_bricks_select_status_volume(dict, op_errstr,
                                                       selected);
            break;
        case GD_OP_DEFRAG_BRICK_VOLUME:
            ret = glusterd_bricks_select_rebalance_volume(dict, op_errstr,
                                                          selected);
            break;

        case GD_OP_BARRIER:
            ret = glusterd_bricks_select_barrier(dict, selected);
            break;
        case GD_OP_SNAP:
            ret = glusterd_bricks_select_snap(dict, op_errstr, selected);
            break;
        case GD_OP_SCRUB_STATUS:
        case GD_OP_SCRUB_ONDEMAND:
            ret = glusterd_bricks_select_scrub(dict, op_errstr, selected);
            break;
        default:
            break;
    }

    gf_msg_debug(THIS->name, 0, "Returning %d", ret);

    return ret;
}

glusterd_op_sm_t glusterd_op_state_default[] = {
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_none},         // EVENT_NONE
    {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_send_lock},  // EVENT_START_LOCK
    {GD_OP_STATE_LOCKED, glusterd_op_ac_lock},          // EVENT_LOCK
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_none},         // EVENT_RCVD_ACC
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_none},         // EVENT_ALL_ACC
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_none},         // EVENT_STAGE_ACC
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_none},         // EVENT_COMMIT_ACC
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_none},         // EVENT_RCVD_RJT
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_none},         // EVENT_STAGE_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_none},         // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},       // EVENT_UNLOCK
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_none},         // EVENT_START_UNLOCK
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_none},         // EVENT_ALL_ACK
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_none},  // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_lock_sent[] = {
    {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none},           // EVENT_NONE
    {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none},           // EVENT_START_LOCK
    {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_lock},           // EVENT_LOCK
    {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_rcvd_lock_acc},  // EVENT_RCVD_ACC
    {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_send_stage_op},  // EVENT_ALL_ACC
    {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none},  // EVENT_STAGE_ACC
    {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none},  // EVENT_COMMIT_ACC
    {GD_OP_STATE_ACK_DRAIN,
     glusterd_op_ac_send_unlock_drain},            // EVENT_RCVD_RJT
    {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none},  // EVENT_STAGE_OP
    {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none},  // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},  // EVENT_UNLOCK
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},  // EVENT_START_UNLOCK
    {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none},  // EVENT_ALL_ACK
    {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none},  // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_locked[] = {
    {GD_OP_STATE_LOCKED, glusterd_op_ac_none},      // EVENT_NONE
    {GD_OP_STATE_LOCKED, glusterd_op_ac_none},      // EVENT_START_LOCK
    {GD_OP_STATE_LOCKED, glusterd_op_ac_lock},      // EVENT_LOCK
    {GD_OP_STATE_LOCKED, glusterd_op_ac_none},      // EVENT_RCVD_ACC
    {GD_OP_STATE_LOCKED, glusterd_op_ac_none},      // EVENT_ALL_ACC
    {GD_OP_STATE_LOCKED, glusterd_op_ac_none},      // EVENT_STAGE_ACC
    {GD_OP_STATE_LOCKED, glusterd_op_ac_none},      // EVENT_COMMIT_ACC
    {GD_OP_STATE_LOCKED, glusterd_op_ac_none},      // EVENT_RCVD_RJT
    {GD_OP_STATE_STAGED, glusterd_op_ac_stage_op},  // EVENT_STAGE_OP
    {GD_OP_STATE_LOCKED, glusterd_op_ac_none},      // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},   // EVENT_UNLOCK
    {GD_OP_STATE_LOCKED, glusterd_op_ac_none},      // EVENT_START_UNLOCK
    {GD_OP_STATE_LOCKED, glusterd_op_ac_none},      // EVENT_ALL_ACK
    {GD_OP_STATE_DEFAULT,
     glusterd_op_ac_local_unlock},              // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_LOCKED, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_stage_op_sent[] = {
    {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none},  // EVENT_NONE
    {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none},  // EVENT_START_LOCK
    {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_lock},  // EVENT_LOCK
    {GD_OP_STATE_STAGE_OP_SENT,
     glusterd_op_ac_rcvd_stage_op_acc},  // EVENT_RCVD_ACC
    {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_send_brick_op},  // EVENT_ALL_ACC
    {GD_OP_STATE_BRICK_OP_SENT,
     glusterd_op_ac_send_brick_op},                    // EVENT_STAGE_ACC
    {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none},  // EVENT_COMMIT_ACC
    {GD_OP_STATE_STAGE_OP_FAILED,
     glusterd_op_ac_stage_op_failed},                  // EVENT_RCVD_RJT
    {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none},  // EVENT_STAGE_OP
    {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none},  // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},      // EVENT_UNLOCK
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},      // EVENT_START_UNLOCK
    {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none},  // EVENT_ALL_ACK
    {GD_OP_STATE_STAGE_OP_SENT,
     glusterd_op_ac_none},  // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_stage_op_failed[] = {
    {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none},  // EVENT_NONE
    {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none},  // EVENT_START_LOCK
    {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_lock},  // EVENT_LOCK
    {GD_OP_STATE_STAGE_OP_FAILED,
     glusterd_op_ac_stage_op_failed},                    // EVENT_RCVD_ACC
    {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none},  // EVENT_ALL_ACC
    {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none},  // EVENT_STAGE_ACC
    {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none},  // EVENT_COMMIT_ACC
    {GD_OP_STATE_STAGE_OP_FAILED,
     glusterd_op_ac_stage_op_failed},                    // EVENT_RCVD_RJT
    {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none},  // EVENT_STAGE_OP
    {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none},  // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},        // EVENT_UNLOCK
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},        // EVENT_START_UNLOCK
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock},  // EVENT_ALL_ACK
    {GD_OP_STATE_STAGE_OP_FAILED,
     glusterd_op_ac_none},  // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_STAGE_OP_FAILED, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_staged[] = {
    {GD_OP_STATE_STAGED, glusterd_op_ac_none},  // EVENT_NONE
    {GD_OP_STATE_STAGED, glusterd_op_ac_none},  // EVENT_START_LOCK
    {GD_OP_STATE_STAGED, glusterd_op_ac_lock},  // EVENT_LOCK
    {GD_OP_STATE_STAGED, glusterd_op_ac_none},  // EVENT_RCVD_ACC
    {GD_OP_STATE_STAGED, glusterd_op_ac_none},  // EVENT_ALL_ACC
    {GD_OP_STATE_STAGED, glusterd_op_ac_none},  // EVENT_STAGE_ACC
    {GD_OP_STATE_STAGED, glusterd_op_ac_none},  // EVENT_COMMIT_ACC
    {GD_OP_STATE_STAGED, glusterd_op_ac_none},  // EVENT_RCVD_RJT
    {GD_OP_STATE_STAGED, glusterd_op_ac_none},  // EVENT_STAGE_OP
    {GD_OP_STATE_BRICK_COMMITTED,
     glusterd_op_ac_send_brick_op},                // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},  // EVENT_UNLOCK
    {GD_OP_STATE_STAGED, glusterd_op_ac_none},     // EVENT_START_UNLOCK
    {GD_OP_STATE_STAGED, glusterd_op_ac_none},     // EVENT_ALL_ACK
    {GD_OP_STATE_DEFAULT,
     glusterd_op_ac_local_unlock},              // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_STAGED, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_brick_op_sent[] = {
    {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none},  // EVENT_NONE
    {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none},  // EVENT_START_LOCK
    {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_lock},  // EVENT_LOCK
    {GD_OP_STATE_BRICK_OP_SENT,
     glusterd_op_ac_rcvd_brick_op_acc},                // EVENT_RCVD_ACC
    {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none},  // EVENT_ALL_ACC
    {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none},  // EVENT_STAGE_ACC
    {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none},  // EVENT_COMMIT_ACC
    {GD_OP_STATE_BRICK_OP_FAILED,
     glusterd_op_ac_brick_op_failed},                  // EVENT_RCVD_RJT
    {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none},  // EVENT_BRICK_OP
    {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none},  // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},      // EVENT_UNLOCK
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},      // EVENT_START_UNLOCK
    {GD_OP_STATE_COMMIT_OP_SENT,
     glusterd_op_ac_send_commit_op},  // EVENT_ALL_ACK
    {GD_OP_STATE_BRICK_OP_SENT,
     glusterd_op_ac_none},  // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_BRICK_OP_SENT, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_brick_op_failed[] = {
    {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none},  // EVENT_NONE
    {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none},  // EVENT_START_LOCK
    {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_lock},  // EVENT_LOCK
    {GD_OP_STATE_BRICK_OP_FAILED,
     glusterd_op_ac_brick_op_failed},                    // EVENT_RCVD_ACC
    {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none},  // EVENT_ALL_ACC
    {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none},  // EVENT_STAGE_ACC
    {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none},  // EVENT_COMMIT_ACC
    {GD_OP_STATE_BRICK_OP_FAILED,
     glusterd_op_ac_brick_op_failed},                    // EVENT_RCVD_RJT
    {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none},  // EVENT_BRICK_OP
    {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none},  // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},        // EVENT_UNLOCK
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},        // EVENT_START_UNLOCK
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock},  // EVENT_ALL_ACK
    {GD_OP_STATE_BRICK_OP_FAILED,
     glusterd_op_ac_none},  // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_BRICK_OP_FAILED, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_brick_committed[] = {
    {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none},  // EVENT_NONE
    {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none},  // EVENT_START_LOCK
    {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_lock},  // EVENT_LOCK
    {GD_OP_STATE_BRICK_COMMITTED,
     glusterd_op_ac_rcvd_brick_op_acc},                  // EVENT_RCVD_ACC
    {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none},  // EVENT_ALL_ACC
    {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none},  // EVENT_STAGE_ACC
    {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none},  // EVENT_COMMIT_ACC
    {GD_OP_STATE_BRICK_COMMIT_FAILED,
     glusterd_op_ac_brick_op_failed},                    // EVENT_RCVD_RJT
    {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none},  // EVENT_STAGE_OP
    {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none},  // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},        // EVENT_UNLOCK
    {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none},  // EVENT_START_UNLOCK
    {GD_OP_STATE_COMMITED, glusterd_op_ac_commit_op},    // EVENT_ALL_ACK
    {GD_OP_STATE_DEFAULT,
     glusterd_op_ac_local_unlock},  // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_BRICK_COMMITTED, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_brick_commit_failed[] = {
    {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none},  // EVENT_NONE
    {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none},  // EVENT_START_LOCK
    {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_lock},  // EVENT_LOCK
    {GD_OP_STATE_BRICK_COMMIT_FAILED,
     glusterd_op_ac_brick_op_failed},                        // EVENT_RCVD_ACC
    {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none},  // EVENT_ALL_ACC
    {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none},  // EVENT_STAGE_ACC
    {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none},  // EVENT_COMMIT_ACC
    {GD_OP_STATE_BRICK_COMMIT_FAILED,
     glusterd_op_ac_brick_op_failed},                        // EVENT_RCVD_RJT
    {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none},  // EVENT_STAGE_OP
    {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none},  // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},            // EVENT_UNLOCK
    {GD_OP_STATE_BRICK_COMMIT_FAILED,
     glusterd_op_ac_none},  // EVENT_START_UNLOCK
    {GD_OP_STATE_BRICK_COMMIT_FAILED,
     glusterd_op_ac_send_commit_failed},  // EVENT_ALL_ACK
    {GD_OP_STATE_DEFAULT,
     glusterd_op_ac_local_unlock},  // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_BRICK_COMMIT_FAILED, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_commit_op_failed[] = {
    {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none},  // EVENT_NONE
    {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none},  // EVENT_START_LOCK
    {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_lock},  // EVENT_LOCK
    {GD_OP_STATE_COMMIT_OP_FAILED,
     glusterd_op_ac_commit_op_failed},                    // EVENT_RCVD_ACC
    {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none},  // EVENT_ALL_ACC
    {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none},  // EVENT_STAGE_ACC
    {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none},  // EVENT_COMMIT_ACC
    {GD_OP_STATE_COMMIT_OP_FAILED,
     glusterd_op_ac_commit_op_failed},                    // EVENT_RCVD_RJT
    {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none},  // EVENT_STAGE_OP
    {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none},  // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},         // EVENT_UNLOCK
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},         // EVENT_START_UNLOCK
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock},  // EVENT_ALL_ACK
    {GD_OP_STATE_COMMIT_OP_FAILED,
     glusterd_op_ac_none},  // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_COMMIT_OP_FAILED, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_commit_op_sent[] = {
    {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none},  // EVENT_NONE
    {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none},  // EVENT_START_LOCK
    {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_lock},  // EVENT_LOCK
    {GD_OP_STATE_COMMIT_OP_SENT,
     glusterd_op_ac_rcvd_commit_op_acc},                    // EVENT_RCVD_ACC
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock},  // EVENT_ALL_ACC
    {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none},      // EVENT_STAGE_ACC
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock},  // EVENT_COMMIT_ACC
    {GD_OP_STATE_COMMIT_OP_FAILED,
     glusterd_op_ac_commit_op_failed},                  // EVENT_RCVD_RJT
    {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none},  // EVENT_STAGE_OP
    {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none},  // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},       // EVENT_UNLOCK
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},       // EVENT_START_UNLOCK
    {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none},  // EVENT_ALL_ACK
    {GD_OP_STATE_COMMIT_OP_SENT,
     glusterd_op_ac_none},  // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_committed[] = {
    {GD_OP_STATE_COMMITED, glusterd_op_ac_none},   // EVENT_NONE
    {GD_OP_STATE_COMMITED, glusterd_op_ac_none},   // EVENT_START_LOCK
    {GD_OP_STATE_COMMITED, glusterd_op_ac_lock},   // EVENT_LOCK
    {GD_OP_STATE_COMMITED, glusterd_op_ac_none},   // EVENT_RCVD_ACC
    {GD_OP_STATE_COMMITED, glusterd_op_ac_none},   // EVENT_ALL_ACC
    {GD_OP_STATE_COMMITED, glusterd_op_ac_none},   // EVENT_STAGE_ACC
    {GD_OP_STATE_COMMITED, glusterd_op_ac_none},   // EVENT_COMMIT_ACC
    {GD_OP_STATE_COMMITED, glusterd_op_ac_none},   // EVENT_RCVD_RJT
    {GD_OP_STATE_COMMITED, glusterd_op_ac_none},   // EVENT_STAGE_OP
    {GD_OP_STATE_COMMITED, glusterd_op_ac_none},   // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},  // EVENT_UNLOCK
    {GD_OP_STATE_COMMITED, glusterd_op_ac_none},   // EVENT_START_UNLOCK
    {GD_OP_STATE_COMMITED, glusterd_op_ac_none},   // EVENT_ALL_ACK
    {GD_OP_STATE_DEFAULT,
     glusterd_op_ac_local_unlock},                // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_COMMITED, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_unlock_sent[] = {
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none},  // EVENT_NONE
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none},  // EVENT_START_LOCK
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_lock},  // EVENT_LOCK
    {GD_OP_STATE_UNLOCK_SENT,
     glusterd_op_ac_rcvd_unlock_acc},                    // EVENT_RCVD_ACC
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlocked_all},  // EVENT_ALL_ACC
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none},      // EVENT_STAGE_ACC
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none},      // EVENT_COMMIT_ACC
    {GD_OP_STATE_UNLOCK_SENT,
     glusterd_op_ac_rcvd_unlock_acc},                // EVENT_RCVD_RJT
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none},  // EVENT_STAGE_OP
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none},  // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},    // EVENT_UNLOCK
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},    // EVENT_START_UNLOCK
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none},  // EVENT_ALL_ACK
    {GD_OP_STATE_UNLOCK_SENT,
     glusterd_op_ac_none},  // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_ack_drain[] = {
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},  // EVENT_NONE
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},  // EVENT_START_LOCK
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_lock},  // EVENT_LOCK
    {GD_OP_STATE_ACK_DRAIN,
     glusterd_op_ac_send_unlock_drain},            // EVENT_RCVD_ACC
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},  // EVENT_ALL_ACC
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},  // EVENT_STAGE_ACC
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},  // EVENT_COMMIT_ACC
    {GD_OP_STATE_ACK_DRAIN,
     glusterd_op_ac_send_unlock_drain},            // EVENT_RCVD_RJT
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},  // EVENT_STAGE_OP
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},  // EVENT_COMMIT_OP
    {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock},  // EVENT_UNLOCK
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},  // EVENT_START_UNLOCK
    {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock},  // EVENT_ALL_ACK
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},  // EVENT_LOCAL_UNLOCK_NO_RESP
    {GD_OP_STATE_ACK_DRAIN, glusterd_op_ac_none},  // EVENT_MAX
};

glusterd_op_sm_t *glusterd_op_state_table[] = {
    glusterd_op_state_default,         glusterd_op_state_lock_sent,
    glusterd_op_state_locked,          glusterd_op_state_stage_op_sent,
    glusterd_op_state_staged,          glusterd_op_state_commit_op_sent,
    glusterd_op_state_committed,       glusterd_op_state_unlock_sent,
    glusterd_op_state_stage_op_failed, glusterd_op_state_commit_op_failed,
    glusterd_op_state_brick_op_sent,   glusterd_op_state_brick_op_failed,
    glusterd_op_state_brick_committed, glusterd_op_state_brick_commit_failed,
    glusterd_op_state_ack_drain};

int
glusterd_op_sm_new_event(glusterd_op_sm_event_type_t event_type,
                         glusterd_op_sm_event_t **new_event)
{
    glusterd_op_sm_event_t *event = NULL;

    GF_ASSERT(new_event);
    GF_ASSERT(GD_OP_EVENT_NONE <= event_type && GD_OP_EVENT_MAX > event_type);

    event = GF_CALLOC(1, sizeof(*event), gf_gld_mt_op_sm_event_t);

    if (!event)
        return -1;

    *new_event = event;
    event->event = event_type;
    CDS_INIT_LIST_HEAD(&event->list);

    return 0;
}

int
glusterd_op_sm_inject_event(glusterd_op_sm_event_type_t event_type,
                            uuid_t *txn_id, void *ctx)
{
    int32_t ret = -1;
    glusterd_op_sm_event_t *event = NULL;

    GF_ASSERT(event_type < GD_OP_EVENT_MAX && event_type >= GD_OP_EVENT_NONE);

    ret = glusterd_op_sm_new_event(event_type, &event);

    if (ret)
        goto out;

    event->ctx = ctx;

    if (txn_id)
        gf_uuid_copy(event->txn_id, *txn_id);

    gf_msg_debug(THIS->name, 0, "Enqueue event: '%s'",
                 glusterd_op_sm_event_name_get(event->event));
    cds_list_add_tail(&event->list, &gd_op_sm_queue);

out:
    return ret;
}

void
glusterd_destroy_req_ctx(glusterd_req_ctx_t *ctx)
{
    if (!ctx)
        return;
    if (ctx->dict)
        dict_unref(ctx->dict);
    GF_FREE(ctx);
}

void
glusterd_destroy_local_unlock_ctx(uuid_t *ctx)
{
    if (!ctx)
        return;
    GF_FREE(ctx);
}

void
glusterd_destroy_op_event_ctx(glusterd_op_sm_event_t *event)
{
    if (!event)
        return;

    switch (event->event) {
        case GD_OP_EVENT_LOCK:
        case GD_OP_EVENT_UNLOCK:
            glusterd_destroy_lock_ctx(event->ctx);
            break;
        case GD_OP_EVENT_STAGE_OP:
        case GD_OP_EVENT_ALL_ACK:
            glusterd_destroy_req_ctx(event->ctx);
            break;
        case GD_OP_EVENT_LOCAL_UNLOCK_NO_RESP:
            glusterd_destroy_local_unlock_ctx(event->ctx);
            break;
        default:
            break;
    }
}

int
glusterd_op_sm()
{
    glusterd_op_sm_event_t *event = NULL;
    glusterd_op_sm_event_t *tmp = NULL;
    int ret = -1;
    int lock_err = 0;
    glusterd_op_sm_ac_fn handler = NULL;
    glusterd_op_sm_t *state = NULL;
    glusterd_op_sm_event_type_t event_type = GD_OP_EVENT_NONE;
    xlator_t *this = NULL;
    glusterd_op_info_t txn_op_info;
    glusterd_conf_t *priv = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    ret = synclock_trylock(&gd_op_sm_lock);
    if (ret) {
        lock_err = errno;
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_LOCK_FAIL,
               "lock failed due to %s", strerror(lock_err));
        goto lock_failed;
    }

    while (!cds_list_empty(&gd_op_sm_queue)) {
        cds_list_for_each_entry_safe(event, tmp, &gd_op_sm_queue, list)
        {
            cds_list_del_init(&event->list);
            event_type = event->event;
            gf_msg_debug(this->name, 0,
                         "Dequeued event of "
                         "type: '%s'",
                         glusterd_op_sm_event_name_get(event_type));

            gf_msg_debug(this->name, 0, "transaction ID = %s",
                         uuid_utoa(event->txn_id));

            ret = glusterd_get_txn_opinfo(&event->txn_id, &txn_op_info);
            if (ret) {
                gf_msg_callingfn(this->name, GF_LOG_ERROR, 0,
                                 GD_MSG_TRANS_OPINFO_GET_FAIL,
                                 "Unable to get transaction "
                                 "opinfo for transaction ID :"
                                 "%s",
                                 uuid_utoa(event->txn_id));
                glusterd_destroy_op_event_ctx(event);
                GF_FREE(event);
                continue;
            } else
                opinfo = txn_op_info;

            state = glusterd_op_state_table[opinfo.state.state];

            GF_ASSERT(state);

            handler = state[event_type].handler;
            GF_ASSERT(handler);

            ret = handler(event, event->ctx);

            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_HANDLER_RETURNED,
                       "handler returned: %d", ret);
                glusterd_destroy_op_event_ctx(event);
                GF_FREE(event);
                continue;
            }

            ret = glusterd_op_sm_transition_state(&opinfo, state, event_type);

            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_EVENT_STATE_TRANSITION_FAIL,
                       "Unable to transition"
                       "state from '%s' to '%s'",
                       glusterd_op_sm_state_name_get(opinfo.state.state),
                       glusterd_op_sm_state_name_get(
                           state[event_type].next_state));
                (void)synclock_unlock(&gd_op_sm_lock);
                return ret;
            }

            if ((state[event_type].next_state == GD_OP_STATE_DEFAULT) &&
                (event_type == GD_OP_EVENT_UNLOCK)) {
                /* Clearing the transaction opinfo */
                ret = glusterd_clear_txn_opinfo(&event->txn_id);
                if (ret)
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_TRANS_OPINFO_CLEAR_FAIL,
                           "Unable to clear "
                           "transaction's opinfo");
            } else {
                if ((priv->op_version < GD_OP_VERSION_6_0) ||
                    !(event_type == GD_OP_EVENT_STAGE_OP &&
                      opinfo.state.state == GD_OP_STATE_STAGED &&
                      opinfo.skip_locking)) {
                    ret = glusterd_set_txn_opinfo(&event->txn_id, &opinfo);
                    if (ret)
                        gf_msg(this->name, GF_LOG_ERROR, 0,
                               GD_MSG_TRANS_OPINFO_SET_FAIL,
                               "Unable to set "
                               "transaction's opinfo");
                }
            }

            glusterd_destroy_op_event_ctx(event);
            GF_FREE(event);
        }
    }

    (void)synclock_unlock(&gd_op_sm_lock);
    ret = 0;

lock_failed:

    return ret;
}

int32_t
glusterd_op_set_op(glusterd_op_t op)
{
    GF_ASSERT(op < GD_OP_MAX);
    GF_ASSERT(op > GD_OP_NONE);

    opinfo.op = op;

    return 0;
}

int32_t
glusterd_op_get_op()
{
    return opinfo.op;
}

int32_t
glusterd_op_set_req(rpcsvc_request_t *req)
{
    GF_ASSERT(req);
    opinfo.req = req;
    return 0;
}

int32_t
glusterd_op_clear_op(glusterd_op_t op)
{
    opinfo.op = GD_OP_NONE;

    return 0;
}

int32_t
glusterd_op_free_ctx(glusterd_op_t op, void *ctx)
{
    if (ctx) {
        switch (op) {
            case GD_OP_CREATE_VOLUME:
            case GD_OP_DELETE_VOLUME:
            case GD_OP_STOP_VOLUME:
            case GD_OP_ADD_BRICK:
            case GD_OP_REMOVE_BRICK:
            case GD_OP_REPLACE_BRICK:
            case GD_OP_LOG_ROTATE:
            case GD_OP_SYNC_VOLUME:
            case GD_OP_SET_VOLUME:
            case GD_OP_START_VOLUME:
            case GD_OP_RESET_VOLUME:
            case GD_OP_GSYNC_SET:
            case GD_OP_QUOTA:
            case GD_OP_PROFILE_VOLUME:
            case GD_OP_STATUS_VOLUME:
            case GD_OP_REBALANCE:
            case GD_OP_HEAL_VOLUME:
            case GD_OP_STATEDUMP_VOLUME:
            case GD_OP_CLEARLOCKS_VOLUME:
            case GD_OP_DEFRAG_BRICK_VOLUME:
            case GD_OP_MAX_OPVERSION:
                dict_unref(ctx);
                break;
            default:
                GF_ASSERT(0);
                break;
        }
    }

    glusterd_op_reset_ctx();
    return 0;
}

void *
glusterd_op_get_ctx()
{
    return opinfo.op_ctx;
}

int
glusterd_op_sm_init()
{
    CDS_INIT_LIST_HEAD(&gd_op_sm_queue);
    synclock_init(&gd_op_sm_lock, SYNC_LOCK_DEFAULT);
    return 0;
}
